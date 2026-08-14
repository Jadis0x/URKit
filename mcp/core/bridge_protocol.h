#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace URK::DevMcp {

inline constexpr std::size_t kMaximumMessageBytes = 64 * 1024;
inline constexpr std::string_view kBridgeProtocolVersion = "1";

struct BridgeRequest {
    std::string id;
    std::string tool;
    nlohmann::json arguments = nlohmann::json::object();
};

struct BridgeResponse {
    std::string id;
    bool ok = false;
    nlohmann::json result = nlohmann::json::object();
    std::string errorCode;
    std::string errorMessage;
};

bool ParseBridgeRequest(std::string_view text, BridgeRequest *request, std::string *error);
bool ParseBridgeResponse(std::string_view text, BridgeResponse *response, std::string *error);
std::string SerializeBridgeRequest(const BridgeRequest &request);
std::string SerializeBridgeResponse(const BridgeResponse &response);
BridgeResponse BridgeFailure(std::string id, std::string code, std::string message);

}
