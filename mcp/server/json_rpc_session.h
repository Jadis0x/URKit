#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace URK::DevMcp {

struct RpcMessage {
    nlohmann::json id;
    std::string method;
    nlohmann::json params = nlohmann::json::object();
    bool notification = false;
};

class JsonRpcSession {
  public:
    enum class Era {
        Undetermined,
        Legacy,
        Modern
    };

    enum class State {
        AwaitingInitialize,
        AwaitingInitialized,
        Operational,
        Closed
    };

    std::optional<RpcMessage> Decode(std::string_view line, nlohmann::json *errorResponse) const;
    bool Permits(const RpcMessage &message, nlohmann::json *errorResponse);
    nlohmann::json Initialize(const RpcMessage &message);
    nlohmann::json Discover(const RpcMessage &message) const;
    nlohmann::json CompleteResult(const nlohmann::json &id, nlohmann::json value) const;
    static nlohmann::json CompleteResult(const nlohmann::json &id, nlohmann::json value, bool modern);
    void Initialized();
    void Close();
    State GetState() const;
    Era GetEra() const;

    static nlohmann::json Error(const nlohmann::json &id, int code, std::string message);
    static nlohmann::json Error(const nlohmann::json &id, int code, std::string message, nlohmann::json data);
    static nlohmann::json Result(const nlohmann::json &id, nlohmann::json value);

  private:
    bool ValidateModernRequest(const RpcMessage &message, nlohmann::json *errorResponse) const;

    State state_ = State::AwaitingInitialize;
    Era era_ = Era::Undetermined;
    std::string protocolVersion_;
};

}
