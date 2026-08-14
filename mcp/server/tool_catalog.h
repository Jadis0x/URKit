#pragma once

#include <nlohmann/json.hpp>

#include <string_view>
#include <vector>

namespace URK::DevMcp {

const std::vector<nlohmann::json> &ToolCatalog();
bool IsKnownTool(std::string_view name);

}
