#include "mcp/core/bridge_protocol.h"
#include "mcp/server/mcp_server.h"
#include "mcp/server/tool_catalog.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Json = nlohmann::json;

void Require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

std::vector<Json> RunServer(std::string input) {
    std::istringstream stream(std::move(input));
    std::ostringstream output;
    URK::DevMcp::McpServer server(std::filesystem::current_path(), std::nullopt);
    Require(server.Run(stream, output) == 0, "server exits cleanly");
    std::vector<Json> messages;
    std::istringstream lines(output.str());
    std::string line;
    while (std::getline(lines, line))
        messages.push_back(Json::parse(line));
    return messages;
}

}

int main() {
    using namespace URK::DevMcp;
    BridgeRequest request{"42", "runtime_status", Json::object()};
    BridgeRequest parsedRequest;
    std::string error;
    Require(ParseBridgeRequest(SerializeBridgeRequest(request), &parsedRequest, &error), "bridge request round trip");
    Require(parsedRequest.id == "42" && parsedRequest.tool == "runtime_status", "bridge request fields survive");

    BridgeResponse response{"42", true, {{"ready", true}}, {}, {}};
    BridgeResponse parsedResponse;
    Require(ParseBridgeResponse(SerializeBridgeResponse(response), &parsedResponse, &error),
            "bridge response round trip");
    Require(parsedResponse.ok && parsedResponse.result.value("ready", false), "bridge response fields survive");

    const std::string initialize =
        R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"test","version":"1"}}})";
    const std::vector<Json> messages =
        RunServer(initialize + "\n" + R"({"jsonrpc":"2.0","method":"notifications/initialized"})" + "\n" +
                  R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})" + "\n");
    Require(messages.size() == 2, "initialize and tools/list return responses");
    Require(messages[0]["result"]["serverInfo"]["name"] == "urkit-dev", "server identity is stable");
    Require(messages[1]["result"]["tools"].size() == ToolCatalog().size(), "complete tool catalog is returned");

    const Json modernMeta = {{"io.modelcontextprotocol/protocolVersion", "2026-07-28"},
                             {"io.modelcontextprotocol/clientInfo", {{"name", "test"}, {"version", "1"}}},
                             {"io.modelcontextprotocol/clientCapabilities", Json::object()}};
    const Json discover = {
        {"jsonrpc", "2.0"}, {"id", "discover"}, {"method", "server/discover"}, {"params", {{"_meta", modernMeta}}}};
    const Json modernList = {
        {"jsonrpc", "2.0"}, {"id", "list"}, {"method", "tools/list"}, {"params", {{"_meta", modernMeta}}}};
    const std::vector<Json> modern = RunServer(discover.dump() + "\n" + modernList.dump() + "\n");
    Require(modern.size() == 2, "modern discovery and tools/list return responses");
    Require(modern[0]["result"]["supportedVersions"][0] == "2026-07-28", "modern protocol is advertised");
    Require(modern[0]["result"]["resultType"] == "complete", "modern discovery has a result type");
    Require(modern[1]["result"]["tools"].size() == ToolCatalog().size(), "modern tool catalog is returned");
    Require(modern[1]["result"]["ttlMs"] == 3600000, "modern tool catalog is cacheable");
    Require(modern[1]["result"]["_meta"]["io.modelcontextprotocol/serverInfo"]["name"] == "urkit-dev",
            "modern responses carry server identity");

    const std::vector<Json> modernDirect = RunServer(modernList.dump() + "\n");
    Require(modernDirect.size() == 1 && modernDirect[0]["result"]["resultType"] == "complete",
            "modern requests work without discovery");

    Json unsupported = modernList;
    unsupported["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"] = "1900-01-01";
    const std::vector<Json> unsupportedResponse = RunServer(unsupported.dump() + "\n");
    Require(unsupportedResponse.size() == 1 && unsupportedResponse[0]["error"]["code"] == -32022,
            "unsupported modern protocol versions are rejected");
    Require(unsupportedResponse[0]["error"]["data"]["requested"] == "1900-01-01",
            "protocol errors identify the requested version");

    const std::vector<Json> invalid = RunServer(
        std::string(R"({"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}})") + "\n" + initialize + "\n");
    Require(invalid.size() == 2 && invalid[0]["error"]["code"] == -32600, "operation before initialize is rejected");
    return 0;
}
