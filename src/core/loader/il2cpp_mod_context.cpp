#include "il2cpp_api.h"
#include "main_thread_dispatcher.h"
#include "mod_context.h"

URK_ModContext &ModContext_BuildIl2Cpp(const Config &config, Il2CppApi &il2cpp, uint64_t backendCapabilities) {
    ModContextBuildOptions options{};
    options.runtimeBackend = URK_RUNTIME_BACKEND_IL2CPP;
    options.backendCapabilities = backendCapabilities;
    options.mainThreadDispatcherAvailable = MainThread_HasDispatchTarget();
    options.apis.il2cpp = ModApi_Il2Cpp(il2cpp.MetadataAccessReady() ? &il2cpp : nullptr);
    options.modules.backendModuleBase = il2cpp.gameAssemblyBase;
    options.modules.gameAssemblyModuleBase = il2cpp.gameAssemblyBase;
    options.modules.unityPlayerModuleBase = il2cpp.unityPlayerBase;
    return ModContext_Build(config, options);
}