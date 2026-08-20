std::string RenderHookHeaderModule() {
    return R"URK(#pragma once

struct URK_ModContext;

namespace ModRenderHook {

bool install(const URK_ModContext *context);
bool uninstall();

} // namespace ModRenderHook
)URK";
}


