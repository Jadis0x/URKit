#include "bridge_protocol.h"

namespace URK::DevMcp {
namespace {

bool ValidToken(const nlohmann::json &value) {
    return value.is_string() && !value.get_ref<const std::string &>().empty() &&
           value.get_ref<const std::string &>().size() <= 128;
}

}

bool ParseBridgeRequest(std::string_view text, BridgeRequest *request, std::string *error) {
    if (error)
        error->clear();
    if (!request || text.empty() || text.size() > kMaximumMessageBytes) {
        if (error)
            *error = "request size is outside the allowed range";
        return false;
    }
    try {
        const nlohmann::json value = nlohmann::json::parse(text);
        if (!value.is_object() || value.value("version", std::string{}) != kBridgeProtocolVersion ||
            !value.contains("id") || !ValidToken(value["id"]) || !value.contains("tool") ||
            !ValidToken(value["tool"])) {
            if (error)
                *error = "request is missing valid version, id, or tool fields";
            return false;
        }
        BridgeRequest parsed;
        parsed.id = value["id"].get<std::string>();
        parsed.tool = value["tool"].get<std::string>();
        parsed.arguments = value.value("arguments", nlohmann::json::object());
        if (!parsed.arguments.is_object()) {
            if (error)
                *error = "request arguments must be an object";
            return false;
        }
        *request = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception &) {
        if (error)
            *error = "request is not valid JSON";
        return false;
    }
}

bool ParseBridgeResponse(std::string_view text, BridgeResponse *response, std::string *error) {
    if (error)
        error->clear();
    if (!response || text.empty() || text.size() > kMaximumMessageBytes) {
        if (error)
            *error = "response size is outside the allowed range";
        return false;
    }
    try {
        const nlohmann::json value = nlohmann::json::parse(text);
        if (!value.is_object() || value.value("version", std::string{}) != kBridgeProtocolVersion ||
            !value.contains("id") || !ValidToken(value["id"]) || !value.contains("ok") || !value["ok"].is_boolean()) {
            if (error)
                *error = "response is missing valid version, id, or status fields";
            return false;
        }
        BridgeResponse parsed;
        parsed.id = value["id"].get<std::string>();
        parsed.ok = value["ok"].get<bool>();
        parsed.result = value.value("result", nlohmann::json::object());
        parsed.errorCode = value.value("error_code", std::string{});
        parsed.errorMessage = value.value("error_message", std::string{});
        *response = std::move(parsed);
        return true;
    } catch (const nlohmann::json::exception &) {
        if (error)
            *error = "response is not valid JSON";
        return false;
    }
}

std::string SerializeBridgeRequest(const BridgeRequest &request) {
    return nlohmann::json{{"version", kBridgeProtocolVersion},
                          {"id", request.id},
                          {"tool", request.tool},
                          {"arguments", request.arguments}}
        .dump();
}

std::string SerializeBridgeResponse(const BridgeResponse &response) {
    nlohmann::json value{{"version", kBridgeProtocolVersion}, {"id", response.id}, {"ok", response.ok}};
    if (response.ok)
        value["result"] = response.result;
    else {
        value["error_code"] = response.errorCode;
        value["error_message"] = response.errorMessage;
    }
    return value.dump();
}

BridgeResponse BridgeFailure(std::string id, std::string code, std::string message) {
    return {std::move(id), false, nlohmann::json::object(), std::move(code), std::move(message)};
}

}
