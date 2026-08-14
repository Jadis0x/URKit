#include "tool_catalog.h"

#include <algorithm>

namespace URK::DevMcp {

const std::vector<nlohmann::json> &ToolCatalog() {
    using Json = nlohmann::json;
    static const std::vector<Json> catalog{
        {{"name", "project_info"},
         {"description", "Inspect the configured generated URKit mod project and deployment target."},
         {"inputSchema", {{"type", "object"}, {"additionalProperties", false}}},
         {"annotations", {{"readOnlyHint", true}}}},
        {{"name", "build_mod"},
         {"description", "Configure, build, and verify deployment of the URKit mod with a declared CMake preset."},
         {"inputSchema",
          {{"type", "object"},
           {"properties", {{"preset", {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}}}},
           {"additionalProperties", false}}}},
        {{"name", "deploy_mod"},
         {"description", "Copy an existing preset build artifact to the manifest-owned Mods directory."},
         {"inputSchema",
          {{"type", "object"},
           {"properties", {{"preset", {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}}}}},
           {"additionalProperties", false}}}},
        {{"name", "read_logs"},
         {"description", "Read a bounded tail of URKit_logs.log from the configured game directory."},
         {"inputSchema",
          {{"type", "object"},
           {"properties", {{"maximum_lines", {{"type", "integer"}, {"minimum", 1}, {"maximum", 2000}}}}},
           {"additionalProperties", false}}},
         {"annotations", {{"readOnlyHint", true}}}},
        {{"name", "runtime_status"},
         {"description", "Read backend, capability, process, and current scene state from URKit DevBridge."},
         {"inputSchema", {{"type", "object"}, {"additionalProperties", false}}},
         {"annotations", {{"readOnlyHint", true}}}},
        {{"name", "list_runtime_tests"},
         {"description", "List runtime tests exported by loaded URKit mods."},
         {"inputSchema", {{"type", "object"}, {"additionalProperties", false}}},
         {"annotations", {{"readOnlyHint", true}}}},
        {{"name", "run_runtime_test"},
         {"description", "Run one exported URKit runtime test on the Unity main thread."},
         {"inputSchema",
          {{"type", "object"},
           {"properties", {{"name", {{"type", "string"}, {"minLength", 1}, {"maxLength", 300}}}}},
           {"required", {"name"}},
           {"additionalProperties", false}}}}};
    return catalog;
}

bool IsKnownTool(std::string_view name) {
    const auto &catalog = ToolCatalog();
    return std::any_of(catalog.begin(), catalog.end(),
                       [&](const nlohmann::json &tool) { return tool.value("name", std::string{}) == name; });
}

}
