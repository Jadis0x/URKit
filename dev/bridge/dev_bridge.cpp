#include "dev_bridge.h"

#include "mcp/core/bridge_protocol.h"
#include "runtime_test_discovery.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <sddl.h>

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <thread>

namespace URK::DevBridge {
namespace {

using URK::DevMcp::BridgeFailure;
using URK::DevMcp::BridgeRequest;
using URK::DevMcp::BridgeResponse;
using URK::DevMcp::kBridgeProtocolVersion;
using URK::DevMcp::kMaximumMessageBytes;
using URK::DevMcp::ParseBridgeRequest;
using URK::DevMcp::SerializeBridgeResponse;
using Json = nlohmann::json;
namespace fs = std::filesystem;

struct PendingRequest {
    BridgeRequest request;
    std::promise<BridgeResponse> completion;
    std::atomic<bool> cancelled{false};
};

struct State {
    std::atomic<bool> stopping{false};
    std::atomic<bool> running{false};
    const URK_ModContext *context = nullptr;
    std::mutex mutex;
    std::deque<std::shared_ptr<PendingRequest>> pending;
    std::thread serverThread;
    std::wstring pipeName;
    fs::path discoveryPath;
};

State g_state;

fs::path DiscoveryDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return fs::path(buffer.data(), buffer.data() + length) / L"URK" / L"DevBridge" / L"bridges";
}

bool WriteDiscovery(std::string *error) {
    const fs::path directory = DiscoveryDirectory();
    if (directory.empty()) {
        *error = "LOCALAPPDATA could not be resolved";
        return false;
    }
    std::error_code filesystemError;
    fs::create_directories(directory, filesystemError);
    if (filesystemError) {
        *error = "cannot create bridge discovery directory: " + filesystemError.message();
        return false;
    }
    g_state.discoveryPath = directory / (std::to_wstring(GetCurrentProcessId()) + L".json");
    std::ofstream output(g_state.discoveryPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        *error = "cannot open bridge discovery record";
        return false;
    }
    const std::string pipe(g_state.pipeName.begin(), g_state.pipeName.end());
    output << Json{{"protocol", kBridgeProtocolVersion}, {"pid", GetCurrentProcessId()}, {"pipe", pipe}}.dump();
    output.flush();
    if (!output) {
        *error = "cannot publish bridge discovery record";
        return false;
    }
    return true;
}

HANDLE CreatePipe(std::string *error) {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;;GA;;;SY)(A;;GA;;;OW)", SDDL_REVISION_1,
                                                              &descriptor, nullptr)) {
        *error = "cannot create pipe security descriptor";
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};
    const HANDLE pipe = CreateNamedPipeW(g_state.pipeName.c_str(), PIPE_ACCESS_DUPLEX,
                                         PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                         1, static_cast<DWORD>(kMaximumMessageBytes + 1),
                                         static_cast<DWORD>(kMaximumMessageBytes + 1), 5000, &attributes);
    LocalFree(descriptor);
    if (pipe == INVALID_HANDLE_VALUE)
        *error = "cannot create DevBridge named pipe (Windows " + std::to_string(GetLastError()) + ")";
    return pipe;
}

bool ReadLine(HANDLE pipe, std::string *line) {
    line->clear();
    while (!g_state.stopping.load(std::memory_order_acquire) && line->size() <= kMaximumMessageBytes) {
        char value = 0;
        DWORD read = 0;
        if (!ReadFile(pipe, &value, 1, &read, nullptr) || read == 0)
            return false;
        if (value == '\n')
            return true;
        if (value != '\r')
            line->push_back(value);
    }
    return false;
}

bool WriteLine(HANDLE pipe, std::string text) {
    text.push_back('\n');
    std::size_t offset = 0;
    while (offset < text.size()) {
        DWORD written = 0;
        if (!WriteFile(pipe, text.data() + offset, static_cast<DWORD>(text.size() - offset), &written, nullptr) ||
            written == 0)
            return false;
        offset += written;
    }
    return true;
}

BridgeResponse Dispatch(const BridgeRequest &request) {
    const URK_ModContext *context = g_state.context;
    if (!context)
        return BridgeFailure(request.id, "runtime_unavailable", "URKit mod context is unavailable");
    if (request.tool == "runtime_status") {
        Json result{{"process_id", GetCurrentProcessId()},
                    {"sdk_version", context->version},
                    {"runtime_backend", context->runtimeBackend == URK_RUNTIME_BACKEND_MONO     ? "mono"
                                        : context->runtimeBackend == URK_RUNTIME_BACKEND_IL2CPP ? "il2cpp"
                                                                                                : "unknown"},
                    {"runtime_capabilities", context->runtimeCapabilities},
                    {"main_thread", true}};
        URK_SceneInfo scene{};
        scene.size = sizeof(scene);
        const bool hasScene =
            context->runtime && context->runtime->scene_current && context->runtime->scene_current(&scene);
        result["scene_available"] = hasScene;
        if (hasScene)
            result["scene"] = {{"name", scene.name}, {"build_index", scene.buildIndex}, {"handle", scene.handle}};
        return {request.id, true, std::move(result), {}, {}};
    }
    if (request.tool == "list_runtime_tests")
        return {request.id, true, ListRuntimeTests(), {}, {}};
    if (request.tool == "run_runtime_test") {
        if (!request.arguments.contains("name") || !request.arguments["name"].is_string())
            return BridgeFailure(request.id, "invalid_arguments", "run_runtime_test requires a string name");
        Json result;
        std::string code;
        std::string error;
        if (!RunRuntimeTest(request.arguments["name"].get<std::string>(), &result, &code, &error))
            return BridgeFailure(request.id, std::move(code), std::move(error));
        return {request.id, true, std::move(result), {}, {}};
    }
    return BridgeFailure(request.id, "tool_not_found", "runtime tool is not available");
}

void ServeClient(HANDLE pipe) {
    std::string line;
    while (ReadLine(pipe, &line)) {
        BridgeRequest request;
        std::string parseError;
        if (!ParseBridgeRequest(line, &request, &parseError)) {
            WriteLine(pipe, SerializeBridgeResponse(BridgeFailure("invalid", "invalid_request", parseError)));
            return;
        }
        auto pending = std::make_shared<PendingRequest>();
        pending->request = std::move(request);
        std::future<BridgeResponse> completion = pending->completion.get_future();
        {
            std::lock_guard lock(g_state.mutex);
            if (g_state.pending.size() >= 32) {
                WriteLine(pipe, SerializeBridgeResponse(
                                    BridgeFailure(pending->request.id, "bridge_busy", "runtime queue is full")));
                continue;
            }
            g_state.pending.push_back(pending);
        }
        if (completion.wait_for(std::chrono::seconds(15)) != std::future_status::ready) {
            pending->cancelled.store(true, std::memory_order_release);
            WriteLine(pipe, SerializeBridgeResponse(BridgeFailure(pending->request.id, "runtime_timeout",
                                                                  "Unity main thread did not respond")));
            continue;
        }
        if (!WriteLine(pipe, SerializeBridgeResponse(completion.get())))
            return;
    }
}

void ServerMain() {
    g_state.running.store(true, std::memory_order_release);
    while (!g_state.stopping.load(std::memory_order_acquire)) {
        std::string error;
        HANDLE pipe = CreatePipe(&error);
        if (pipe == INVALID_HANDLE_VALUE) {
            if (g_state.context && g_state.context->Log)
                g_state.context->Log("[DevBridge][ERROR] %s", error.c_str());
            break;
        }
        const BOOL connected = ConnectNamedPipe(pipe, nullptr) ? TRUE : GetLastError() == ERROR_PIPE_CONNECTED;
        if (connected && !g_state.stopping.load(std::memory_order_acquire))
            ServeClient(pipe);
        FlushFileBuffers(pipe);
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    g_state.running.store(false, std::memory_order_release);
}

void WakeServer() {
    if (g_state.pipeName.empty())
        return;
    HANDLE pipe =
        CreateFileW(g_state.pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe != INVALID_HANDLE_VALUE)
        CloseHandle(pipe);
}

}

bool Start(const URK_ModContext *context, std::string *error) {
    if (error)
        error->clear();
    if (!context || g_state.serverThread.joinable()) {
        if (error)
            *error = "DevBridge received an invalid or duplicate start request";
        return false;
    }
    g_state.context = context;
    g_state.stopping.store(false, std::memory_order_release);
    g_state.pipeName = L"\\\\.\\pipe\\URKit.DevBridge." + std::to_wstring(GetCurrentProcessId());
    std::string localError;
    if (!WriteDiscovery(&localError)) {
        g_state.context = nullptr;
        if (error)
            *error = std::move(localError);
        return false;
    }
    try {
        g_state.serverThread = std::thread(ServerMain);
    } catch (const std::exception &exception) {
        std::error_code cleanup;
        fs::remove(g_state.discoveryPath, cleanup);
        g_state.context = nullptr;
        if (error)
            *error = exception.what();
        return false;
    }
    return true;
}

void Tick() {
    std::deque<std::shared_ptr<PendingRequest>> requests;
    {
        std::lock_guard lock(g_state.mutex);
        const std::size_t count = (std::min)(g_state.pending.size(), static_cast<std::size_t>(8));
        for (std::size_t index = 0; index < count; ++index) {
            requests.push_back(g_state.pending.front());
            g_state.pending.pop_front();
        }
    }
    for (const std::shared_ptr<PendingRequest> &request : requests) {
        if (request->cancelled.load(std::memory_order_acquire))
            request->completion.set_value(
                BridgeFailure(request->request.id, "request_cancelled", "runtime request expired before execution"));
        else
            request->completion.set_value(Dispatch(request->request));
    }
}

void Stop() {
    if (!g_state.serverThread.joinable())
        return;
    g_state.stopping.store(true, std::memory_order_release);
    CancelSynchronousIo(g_state.serverThread.native_handle());
    WakeServer();
    g_state.serverThread.join();
    std::deque<std::shared_ptr<PendingRequest>> pending;
    {
        std::lock_guard lock(g_state.mutex);
        pending.swap(g_state.pending);
    }
    for (const std::shared_ptr<PendingRequest> &request : pending)
        request->completion.set_value(
            BridgeFailure(request->request.id, "bridge_stopping", "URKit DevBridge is stopping"));
    std::error_code cleanup;
    fs::remove(g_state.discoveryPath, cleanup);
    g_state.discoveryPath.clear();
    g_state.pipeName.clear();
    g_state.context = nullptr;
}

bool Running() {
    return g_state.running.load(std::memory_order_acquire);
}

}
