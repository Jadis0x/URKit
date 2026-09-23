#include "mod_context.h"

URK_ModContext &ModContext_BuildUnreal(const Config &config, const URK_UnrealApi *unreal,
                                       uintptr_t runtimeModuleBase, bool gameLoop) {
    ModContextBuildOptions options{};
    options.runtimeBackend = URK_RUNTIME_BACKEND_UNREAL;
    options.backendCapabilities = gameLoop ? URK_RUNTIME_CAP_SCENE_EVENTS : URK_RUNTIME_CAP_NONE;
    options.mainThreadDispatcherAvailable = gameLoop;
    options.apis.unreal = unreal;
    options.modules.backendModuleBase = runtimeModuleBase;
    return ModContext_Build(config, options);
}
