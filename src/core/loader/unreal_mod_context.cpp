#include "mod_context.h"

URK_ModContext &ModContext_BuildUnreal(const Config &config, const URK_UnrealApi *unreal,
                                       uintptr_t runtimeModuleBase) {
    ModContextBuildOptions options{};
    options.runtimeBackend = URK_RUNTIME_BACKEND_UNREAL;
    options.backendCapabilities = URK_RUNTIME_CAP_NONE;
    // The engine has its own game thread; Unity's dispatcher never runs here.
    options.mainThreadDispatcherAvailable = false;
    options.apis.unreal = unreal;
    options.modules.backendModuleBase = runtimeModuleBase;
    return ModContext_Build(config, options);
}
