#pragma once

#include "bridge_client.h"
#include "project_service.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <optional>

namespace URK::DevMcp {

class ProcessCancellation;

class McpServer {
  public:
    McpServer(std::filesystem::path projectRoot, std::optional<std::uint32_t> gamePid);

    int Run();
    int Run(std::istream &input, std::ostream &output);

  private:
    nlohmann::json ExecuteTool(const std::string &name, const nlohmann::json &arguments,
                               ProcessCancellation *cancellation);
    nlohmann::json ExecuteBridgeTool(const std::string &name, const nlohmann::json &arguments);

    ProjectService project_;
    BridgeClient bridge_;
    std::atomic<std::uint64_t> nextBridgeId_{1};
};

}
