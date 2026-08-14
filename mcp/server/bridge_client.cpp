#include "bridge_client.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace URK::DevMcp {
namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;

struct DiscoveryRecord {
    std::uint32_t pid = 0;
    std::wstring pipe;
};

fs::path DiscoveryDirectory() {
    std::array<wchar_t, 32768> buffer{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size())
        return {};
    return fs::path(buffer.data(), buffer.data() + length) / L"URK" / L"DevBridge" / L"bridges";
}

bool ProcessAlive(std::uint32_t pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process)
        return false;
    const DWORD wait = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait == WAIT_TIMEOUT;
}

std::vector<DiscoveryRecord> ReadRecords() {
    std::vector<DiscoveryRecord> records;
    const fs::path directory = DiscoveryDirectory();
    std::error_code error;
    if (directory.empty() || !fs::is_directory(directory, error))
        return records;
    for (const fs::directory_entry &entry : fs::directory_iterator(directory, error)) {
        if (error)
            break;
        if (!entry.is_regular_file() || entry.path().extension() != L".json")
            continue;
        std::ifstream input(entry.path(), std::ios::binary);
        if (!input)
            continue;
        const Json value = Json::parse(input, nullptr, false);
        if (value.is_discarded() || value.value("protocol", std::string{}) != kBridgeProtocolVersion ||
            !value.contains("pid") || !value["pid"].is_number_unsigned() || !value.contains("pipe") ||
            !value["pipe"].is_string())
            continue;
        const std::uint32_t pid = value["pid"].get<std::uint32_t>();
        const std::string pipe = value["pipe"].get<std::string>();
        if (pid == 0 || pipe.empty() || !ProcessAlive(pid))
            continue;
        records.push_back({pid, std::wstring(pipe.begin(), pipe.end())});
    }
    return records;
}

bool WriteLine(HANDLE pipe, const std::string &line) {
    std::string framed = line;
    framed.push_back('\n');
    std::size_t offset = 0;
    while (offset < framed.size()) {
        DWORD written = 0;
        if (!WriteFile(pipe, framed.data() + offset, static_cast<DWORD>(framed.size() - offset), &written, nullptr) ||
            written == 0)
            return false;
        offset += written;
    }
    return true;
}

bool ReadLine(HANDLE pipe, std::string *line) {
    line->clear();
    while (line->size() <= kMaximumMessageBytes) {
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

}

BridgeClient::BridgeClient(std::optional<std::uint32_t> gamePid) : gamePid_(gamePid) {
}

bool BridgeClient::ResolvePipe(std::wstring *pipeName, std::uint32_t *pid, std::string *error) const {
    const std::vector<DiscoveryRecord> records = ReadRecords();
    if (gamePid_) {
        for (const DiscoveryRecord &record : records) {
            if (record.pid == *gamePid_) {
                *pipeName = record.pipe;
                *pid = record.pid;
                return true;
            }
        }
        if (error)
            *error = "no active URKit DevBridge was found for PID " + std::to_string(*gamePid_);
        return false;
    }
    if (records.empty()) {
        if (error)
            *error = "no active URKit DevBridge was found";
        return false;
    }
    if (records.size() != 1) {
        if (error) {
            *error = "multiple URKit DevBridge instances are active; configure --game-pid";
            for (const DiscoveryRecord &record : records)
                *error += " " + std::to_string(record.pid);
        }
        return false;
    }
    *pipeName = records.front().pipe;
    *pid = records.front().pid;
    return true;
}

bool BridgeClient::Transact(const BridgeRequest &request, BridgeResponse *response, std::string *error) const {
    if (!response) {
        if (error)
            *error = "response output is null";
        return false;
    }
    std::wstring pipeName;
    std::uint32_t pid = 0;
    if (!ResolvePipe(&pipeName, &pid, error))
        return false;
    if (!WaitNamedPipeW(pipeName.c_str(), 5000)) {
        if (error)
            *error = "URKit DevBridge pipe did not become available for PID " + std::to_string(pid);
        return false;
    }
    HANDLE pipe = CreateFileW(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        if (error)
            *error = "cannot connect to URKit DevBridge for PID " + std::to_string(pid) + " (Windows " +
                     std::to_string(GetLastError()) + ")";
        return false;
    }
    const std::string serialized = SerializeBridgeRequest(request);
    std::string responseLine;
    const bool transferred = WriteLine(pipe, serialized) && ReadLine(pipe, &responseLine);
    CloseHandle(pipe);
    if (!transferred) {
        if (error)
            *error = "URKit DevBridge transaction failed for PID " + std::to_string(pid);
        return false;
    }
    if (!ParseBridgeResponse(responseLine, response, error))
        return false;
    if (response->id != request.id) {
        if (error)
            *error = "URKit DevBridge returned a mismatched request id";
        return false;
    }
    return true;
}

}
