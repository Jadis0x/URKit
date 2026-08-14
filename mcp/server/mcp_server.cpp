#include "mcp_server.h"

#include "json_rpc_session.h"
#include "mcp/core/bridge_protocol.h"
#include "process_runner.h"
#include "stdio_transport.h"
#include "tool_catalog.h"

#include <algorithm>
#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace URK::DevMcp {
namespace {

using Json = nlohmann::json;
constexpr std::size_t kMaximumStdioOutputBytes = 512 * 1024;

Json ToolSuccess(Json value) {
    return {{"content", {{{"type", "text"}, {"text", "Operation completed successfully."}}}},
            {"structuredContent", std::move(value)},
            {"isError", false}};
}

Json ToolFailure(std::string code, std::string message) {
    return {{"content", {{{"type", "text"}, {"text", code + ": " + message}}}}, {"isError", true}};
}

Json FromService(const ServiceResult &result) {
    return result.ok ? ToolSuccess(result.value) : ToolFailure(result.code, result.message);
}

bool HasOnlyKeys(const Json &arguments, std::initializer_list<std::string_view> allowed) {
    for (const auto &[key, value] : arguments.items()) {
        (void)value;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            return false;
    }
    return true;
}

}

McpServer::McpServer(std::filesystem::path projectRoot, std::optional<std::uint32_t> gamePid)
    : project_(std::move(projectRoot)), bridge_(gamePid) {
}

Json McpServer::ExecuteBridgeTool(const std::string &name, const Json &arguments) {
    BridgeRequest request{std::to_string(nextBridgeId_.fetch_add(1, std::memory_order_relaxed)), name, arguments};
    BridgeResponse response;
    std::string error;
    if (!bridge_.Transact(request, &response, &error))
        return ToolFailure("bridge_unavailable", std::move(error));
    if (!response.ok)
        return ToolFailure(response.errorCode, response.errorMessage);
    if (name == "run_runtime_test" && !response.result.value("passed", false)) {
        const std::string message = response.result.value("message", std::string("runtime test failed"));
        return {{"content", {{{"type", "text"}, {"text", "test_failed: " + message}}}},
                {"structuredContent", std::move(response.result)},
                {"isError", true}};
    }
    return ToolSuccess(std::move(response.result));
}

Json McpServer::ExecuteTool(const std::string &name, const Json &arguments, ProcessCancellation *cancellation) {
    if (!arguments.is_object())
        return ToolFailure("invalid_arguments", "arguments must be an object");
    if (name == "project_info") {
        if (!arguments.empty())
            return ToolFailure("invalid_arguments", "project_info does not accept arguments");
        return FromService(project_.ProjectInfo());
    }
    if (name == "build_mod" || name == "deploy_mod") {
        if (!HasOnlyKeys(arguments, {"preset"}))
            return ToolFailure("invalid_arguments", "only preset is accepted");
        const Json presetValue = arguments.value("preset", Json("clang-debug"));
        if (!presetValue.is_string() || presetValue.get_ref<const std::string &>().empty() ||
            presetValue.get_ref<const std::string &>().size() > 64)
            return ToolFailure("invalid_arguments", "preset must be a non-empty string up to 64 bytes");
        const std::string preset = presetValue.get<std::string>();
        return FromService(name == "build_mod" ? project_.Build(preset, cancellation) : project_.Deploy(preset));
    }
    if (name == "read_logs") {
        if (!HasOnlyKeys(arguments, {"maximum_lines"}))
            return ToolFailure("invalid_arguments", "only maximum_lines is accepted");
        const Json linesValue = arguments.value("maximum_lines", Json(200));
        if (!linesValue.is_number_integer())
            return ToolFailure("invalid_arguments", "maximum_lines must be an integer");
        const long long lines = linesValue.get<long long>();
        if (lines < 1 || lines > 2000)
            return ToolFailure("invalid_arguments", "maximum_lines must be between 1 and 2000");
        return FromService(project_.ReadLogs(static_cast<std::size_t>(lines)));
    }
    if (name == "runtime_status" || name == "list_runtime_tests") {
        if (!arguments.empty())
            return ToolFailure("invalid_arguments", name + " does not accept arguments");
        return ExecuteBridgeTool(name, arguments);
    }
    if (name == "run_runtime_test") {
        if (!HasOnlyKeys(arguments, {"name"}) || !arguments.contains("name") || !arguments["name"].is_string())
            return ToolFailure("invalid_arguments", "run_runtime_test requires a string name");
        const std::string testName = arguments["name"].get<std::string>();
        if (testName.empty() || testName.size() > 300)
            return ToolFailure("invalid_arguments", "test name must contain between 1 and 300 bytes");
        return ExecuteBridgeTool(name, arguments);
    }
    return ToolFailure("tool_not_found", "unknown tool: " + name);
}

int McpServer::Run() {
    return Run(std::cin, std::cout);
}

int McpServer::Run(std::istream &input, std::ostream &output) {
    struct Cancellation {
        void Cancel() {
            std::lock_guard lock(mutex);
            if (responseStarted)
                return;
            cancelled = true;
            process.Cancel();
        }
        bool BeginResponse() {
            std::lock_guard lock(mutex);
            if (cancelled)
                return false;
            responseStarted = true;
            return true;
        }

        std::mutex mutex;
        ProcessCancellation process;
        bool cancelled = false;
        bool responseStarted = false;
    };
    struct Worker {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> done;
        std::shared_ptr<Cancellation> cancellation;
        std::string idKey;
    };
    StdioTransport transport(input, output, kMaximumMessageBytes, kMaximumStdioOutputBytes);
    JsonRpcSession session;
    std::vector<Worker> workers;
    workers.reserve(4);
    std::atomic<unsigned> activeWorkers{0};
    std::string line;
    for (;;) {
        const StdioTransport::ReadResult read = transport.Read(&line);
        for (auto worker = workers.begin(); worker != workers.end();) {
            if (worker->done->load(std::memory_order_acquire)) {
                worker->thread.join();
                worker = workers.erase(worker);
            } else {
                ++worker;
            }
        }
        if (read == StdioTransport::ReadResult::End)
            break;
        if (read == StdioTransport::ReadResult::Error) {
            for (Worker &worker : workers)
                worker.cancellation->Cancel();
            for (Worker &worker : workers)
                if (worker.thread.joinable())
                    worker.thread.join();
            return 1;
        }
        if (read == StdioTransport::ReadResult::TooLarge) {
            transport.Emit(JsonRpcSession::Error(nullptr, -32600, "Message exceeds the 64 KiB limit"));
            continue;
        }
        Json decodeError;
        const std::optional<RpcMessage> decoded = session.Decode(line, &decodeError);
        if (!decoded) {
            transport.Emit(decodeError);
            continue;
        }
        const RpcMessage &message = *decoded;
        Json lifecycleError;
        if (!session.Permits(message, &lifecycleError)) {
            if (!message.notification && !lifecycleError.is_null())
                transport.Emit(lifecycleError);
            continue;
        }
        const bool modern = session.GetEra() == JsonRpcSession::Era::Modern;
        if (message.method == "initialize") {
            transport.Emit(session.Initialize(message));
            continue;
        }
        if (message.method == "server/discover" && modern) {
            if (!message.notification)
                transport.Emit(session.Discover(message));
            continue;
        }
        if (message.method == "notifications/initialized") {
            session.Initialized();
            continue;
        }
        if (message.method == "notifications/cancelled") {
            if (message.params.contains("requestId")) {
                const std::string idKey = message.params["requestId"].dump();
                for (Worker &worker : workers)
                    if (worker.idKey == idKey)
                        worker.cancellation->Cancel();
            }
            continue;
        }
        if (message.method == "ping") {
            if (!message.notification)
                transport.Emit(session.CompleteResult(message.id, Json::object()));
            continue;
        }
        if (message.method == "tools/list") {
            if (!message.notification) {
                Json result = {{"tools", ToolCatalog()}};
                if (modern) {
                    result["ttlMs"] = 3600000;
                    result["cacheScope"] = "public";
                }
                transport.Emit(session.CompleteResult(message.id, std::move(result)));
            }
            continue;
        }
        if (message.method == "tools/call") {
            if (message.notification)
                continue;
            if (!message.params.contains("name") || !message.params["name"].is_string()) {
                transport.Emit(JsonRpcSession::Error(message.id, -32602, "tools/call requires a string name"));
                continue;
            }
            const std::string name = message.params["name"].get<std::string>();
            if (!IsKnownTool(name)) {
                transport.Emit(JsonRpcSession::Error(message.id, -32602, "unknown tool: " + name));
                continue;
            }
            const Json arguments = message.params.value("arguments", Json::object());
            if (!arguments.is_object()) {
                transport.Emit(JsonRpcSession::Error(message.id, -32602, "tool arguments must be an object"));
                continue;
            }
            if (activeWorkers.load(std::memory_order_acquire) >= 4) {
                transport.Emit(session.CompleteResult(
                    message.id, ToolFailure("server_busy", "four tool calls are already active")));
                continue;
            }
            activeWorkers.fetch_add(1, std::memory_order_acq_rel);
            const Json id = message.id;
            auto done = std::make_shared<std::atomic<bool>>(false);
            auto cancellation = std::make_shared<Cancellation>();
            try {
                std::thread thread([this, &transport, &activeWorkers, done, cancellation, id, name, arguments, modern] {
                    Json result;
                    try {
                        result = ExecuteTool(name, arguments, &cancellation->process);
                    } catch (const std::exception &exception) {
                        result = ToolFailure("internal_error", exception.what());
                    } catch (...) {
                        result = ToolFailure("internal_error", "unknown exception");
                    }
                    if (cancellation->BeginResponse())
                        transport.Emit(JsonRpcSession::CompleteResult(id, std::move(result), modern));
                    activeWorkers.fetch_sub(1, std::memory_order_acq_rel);
                    done->store(true, std::memory_order_release);
                });
                workers.push_back({std::move(thread), std::move(done), std::move(cancellation), id.dump()});
            } catch (const std::exception &exception) {
                activeWorkers.fetch_sub(1, std::memory_order_acq_rel);
                transport.Emit(
                    session.CompleteResult(message.id, ToolFailure("worker_start_failed", exception.what())));
            }
            continue;
        }
        if (!message.notification)
            transport.Emit(JsonRpcSession::Error(message.id, -32601, "Method not found"));
    }
    session.Close();
    for (Worker &worker : workers)
        worker.cancellation->Cancel();
    for (Worker &worker : workers)
        if (worker.thread.joinable())
            worker.thread.join();
    return 0;
}

}
