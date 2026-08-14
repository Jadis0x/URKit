#include "json_rpc_session.h"

#include "src/project_version.h"

#include <array>

namespace URK::DevMcp {
namespace {

using Json = nlohmann::json;
constexpr std::string_view kModernVersion = "2026-07-28";
constexpr std::array<std::string_view, 4> kLegacyVersions{"2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05"};
constexpr std::array<std::string_view, 5> kSupportedVersions{"2026-07-28", "2025-11-25", "2025-06-18", "2025-03-26",
                                                             "2024-11-05"};

Json ServerInfo() {
    return {{"name", "urkit-dev"}, {"title", "URKit Development"}, {"version", UrkVersion::kReleaseVersion}};
}

Json Capabilities() {
    return {{"tools", {{"listChanged", false}}}};
}

std::string Instructions() {
    return "Build and validate the configured URKit mod project, read bounded logs, and run registered runtime "
           "tests through the local URKit DevBridge.";
}

Json SupportedVersions() {
    Json versions = Json::array();
    for (const std::string_view version : kSupportedVersions)
        versions.push_back(version);
    return versions;
}

bool ValidId(const Json &id) {
    return id.is_null() || id.is_string() || id.is_number_integer() || id.is_number_unsigned();
}

}

Json JsonRpcSession::Error(const Json &id, int code, std::string message) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"error", {{"code", code}, {"message", std::move(message)}}}};
}

Json JsonRpcSession::Error(const Json &id, int code, std::string message, Json data) {
    return {{"jsonrpc", "2.0"},
            {"id", id},
            {"error", {{"code", code}, {"message", std::move(message)}, {"data", std::move(data)}}}};
}

Json JsonRpcSession::Result(const Json &id, Json value) {
    return {{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(value)}};
}

std::optional<RpcMessage> JsonRpcSession::Decode(std::string_view line, Json *errorResponse) const {
    Json value;
    try {
        value = Json::parse(line);
    } catch (const Json::parse_error &) {
        if (errorResponse)
            *errorResponse = Error(nullptr, -32700, "Parse error");
        return std::nullopt;
    } catch (const Json::exception &) {
        if (errorResponse)
            *errorResponse = Error(nullptr, -32600, "Invalid Request");
        return std::nullopt;
    }
    if (!value.is_object() || value.value("jsonrpc", std::string{}) != "2.0" || !value.contains("method") ||
        !value["method"].is_string()) {
        if (errorResponse)
            *errorResponse =
                Error(value.is_object() && value.contains("id") && ValidId(value["id"]) ? value["id"] : Json(nullptr),
                      -32600, "Invalid Request");
        return std::nullopt;
    }
    RpcMessage message;
    message.notification = !value.contains("id");
    message.id = message.notification ? Json(nullptr) : value["id"];
    if (!message.notification && !ValidId(message.id)) {
        if (errorResponse)
            *errorResponse = Error(nullptr, -32600, "Invalid Request id");
        return std::nullopt;
    }
    message.method = value["method"].get<std::string>();
    if (value.contains("params")) {
        if (!value["params"].is_object()) {
            if (errorResponse)
                *errorResponse = Error(message.id, -32602, "params must be an object");
            return std::nullopt;
        }
        message.params = value["params"];
    }
    return message;
}

bool JsonRpcSession::ValidateModernRequest(const RpcMessage &message, Json *errorResponse) const {
    if (message.notification)
        return true;
    if (!message.params.contains("_meta") || !message.params["_meta"].is_object()) {
        if (errorResponse)
            *errorResponse = Error(message.id, -32602, "request params must include MCP protocol metadata");
        return false;
    }
    const Json &meta = message.params["_meta"];
    constexpr std::string_view versionKey = "io.modelcontextprotocol/protocolVersion";
    constexpr std::string_view capabilitiesKey = "io.modelcontextprotocol/clientCapabilities";
    constexpr std::string_view clientKey = "io.modelcontextprotocol/clientInfo";
    if (!meta.contains(versionKey) || !meta[versionKey].is_string()) {
        if (errorResponse)
            *errorResponse = Error(message.id, -32602, "protocol metadata requires a string protocol version");
        return false;
    }
    const std::string requested = meta[versionKey].get<std::string>();
    if (requested != kModernVersion) {
        if (errorResponse)
            *errorResponse = Error(message.id, -32022, "Unsupported protocol version",
                                   {{"supported", SupportedVersions()}, {"requested", requested}});
        return false;
    }
    if (!meta.contains(capabilitiesKey) || !meta[capabilitiesKey].is_object()) {
        if (errorResponse)
            *errorResponse = Error(message.id, -32602, "protocol metadata requires client capabilities");
        return false;
    }
    if (meta.contains(clientKey)) {
        const Json &client = meta[clientKey];
        if (!client.is_object() || !client.contains("name") || !client["name"].is_string() ||
            !client.contains("version") || !client["version"].is_string()) {
            if (errorResponse)
                *errorResponse = Error(message.id, -32602, "clientInfo must contain string name and version fields");
            return false;
        }
    }
    return true;
}

bool JsonRpcSession::Permits(const RpcMessage &message, Json *errorResponse) {
    if (state_ == State::Closed) {
        if (errorResponse)
            *errorResponse = Error(message.id, -32600, "Session is closed");
        return false;
    }
    if (state_ == State::AwaitingInitialize) {
        if (message.method == "initialize" && !message.notification) {
            era_ = Era::Legacy;
            return true;
        }
        if (!message.notification && message.params.contains("_meta") &&
            ValidateModernRequest(message, errorResponse)) {
            era_ = Era::Modern;
            state_ = State::Operational;
            protocolVersion_ = std::string(kModernVersion);
            return true;
        }
        if (!message.notification && errorResponse && errorResponse->is_null())
            *errorResponse = Error(message.id, -32600, "initialize or modern protocol metadata is required");
        return false;
    }
    if (state_ == State::AwaitingInitialized) {
        if (message.method == "notifications/initialized" && message.notification)
            return true;
        if (!message.notification && errorResponse)
            *errorResponse = Error(message.id, -32600, "notifications/initialized is required");
        return false;
    }
    if (era_ == Era::Modern)
        return ValidateModernRequest(message, errorResponse);
    if (message.method == "initialize") {
        if (errorResponse)
            *errorResponse = Error(message.id, -32600, "Session is already initialized");
        return false;
    }
    return true;
}

Json JsonRpcSession::Initialize(const RpcMessage &message) {
    if (!message.params.contains("protocolVersion") || !message.params["protocolVersion"].is_string() ||
        !message.params.contains("capabilities") || !message.params["capabilities"].is_object() ||
        !message.params.contains("clientInfo") || !message.params["clientInfo"].is_object()) {
        return Error(message.id, -32602, "initialize requires protocolVersion, capabilities, and clientInfo");
    }
    const std::string requested = message.params["protocolVersion"].get<std::string>();
    protocolVersion_ = std::string(kLegacyVersions.front());
    for (const std::string_view supported : kLegacyVersions) {
        if (requested == supported) {
            protocolVersion_ = requested;
            break;
        }
    }
    state_ = State::AwaitingInitialized;
    return Result(message.id, {{"protocolVersion", protocolVersion_},
                               {"capabilities", Capabilities()},
                               {"serverInfo", ServerInfo()},
                               {"instructions", Instructions()}});
}

Json JsonRpcSession::Discover(const RpcMessage &message) const {
    return CompleteResult(message.id, {{"supportedVersions", SupportedVersions()},
                                       {"capabilities", Capabilities()},
                                       {"instructions", Instructions()},
                                       {"ttlMs", 3600000},
                                       {"cacheScope", "public"}});
}

Json JsonRpcSession::CompleteResult(const Json &id, Json value) const {
    return CompleteResult(id, std::move(value), era_ == Era::Modern);
}

Json JsonRpcSession::CompleteResult(const Json &id, Json value, bool modern) {
    if (modern) {
        value["resultType"] = "complete";
        value["_meta"]["io.modelcontextprotocol/serverInfo"] = ServerInfo();
    }
    return Result(id, std::move(value));
}

void JsonRpcSession::Initialized() {
    if (state_ == State::AwaitingInitialized)
        state_ = State::Operational;
}

void JsonRpcSession::Close() {
    state_ = State::Closed;
}

JsonRpcSession::State JsonRpcSession::GetState() const {
    return state_;
}

JsonRpcSession::Era JsonRpcSession::GetEra() const {
    return era_;
}

}
