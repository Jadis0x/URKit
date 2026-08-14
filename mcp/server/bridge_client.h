#pragma once

#include "mcp/core/bridge_protocol.h"

#include <cstdint>
#include <optional>
#include <string>

namespace URK::DevMcp {

class BridgeClient {
  public:
    explicit BridgeClient(std::optional<std::uint32_t> gamePid);

    bool Transact(const BridgeRequest &request, BridgeResponse *response, std::string *error) const;

  private:
    bool ResolvePipe(std::wstring *pipeName, std::uint32_t *pid, std::string *error) const;

    std::optional<std::uint32_t> gamePid_;
};

}
