#include "main_thread_dispatcher.h"
#include "mod_api.h"
#include "mod_context.h"
#include "mono_api.h"

URK_ModContext &ModContext_BuildMono(const Config &config, MonoApi &mono, uintptr_t runtimeModuleBase,
                                     uint64_t backendCapabilities) {
    const URK_MonoApi *monoApi = ModApi_Mono(mono.valid() ? &mono : nullptr);
    ModContextBuildOptions options{};
    options.runtimeBackend = URK_RUNTIME_BACKEND_MONO;
    options.backendCapabilities = backendCapabilities;
    options.apis.mono = monoApi;
    options.modules.backendModuleBase = runtimeModuleBase;
    options.mainThreadDispatcherAvailable = MainThread_HasDispatchTarget();
    return ModContext_Build(config, options);
}
