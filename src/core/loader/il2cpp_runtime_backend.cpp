#include "runtime_backend.h"

#include "il2cpp_api.h"
#include "intro.h"
#include "logger.h"
#include "mod_context.h"
#include "native_mod_loader.h"
#include "runtime_events.h"
#include "runtime_state.h"

#include <windows.h>

namespace {
constexpr int kRuntimeTimeoutMs = 30000;
constexpr int kIl2CppPreDomainSettleMs = 1500;
constexpr float kIntroWaitingRuntime = 0.24f;
constexpr float kIntroExportsResolved = 0.44f;
constexpr float kIntroModsBegin = 0.72f;
constexpr float kIntroModsSkippedOrDone = 0.96f;
constexpr float kIntroComplete = 1.0f;

RuntimeState g_il2cppState{"IL2CPP"};
Il2CppApi g_il2cppApi{};

void IntroStage(float value, const char *status) {
    Intro::Progress(value, status ? status : "");
}

bool RunIl2Cpp(Config &config) {
    g_il2cppState.Reset();
    g_il2cppApi = {};
    RuntimeEvents_Reset();

    IntroStage(kIntroWaitingRuntime, "Waiting for IL2CPP runtime...");
    Log(">> IL2CPP backend");
    if (!Il2Cpp_BindExports(g_il2cppApi, kRuntimeTimeoutMs)) {
        IntroStage(kIntroComplete, "IL2CPP runtime unavailable");
        return false;
    }

    IntroStage(kIntroExportsResolved, "IL2CPP exports resolved");
    g_il2cppState.Transition(RuntimeReadiness::ModuleSeen, nullptr, "GameAssembly");
    g_il2cppState.Transition(RuntimeReadiness::ExportsResolved, nullptr, "GameAssembly exports");

    if (config.initDelayMs) {
        Intro::Status("Waiting before IL2CPP metadata probes...");
        Log("[IL2CPP] Applying InitDelayMs=%d before domain/metadata readiness "
            "probes.",
            config.initDelayMs);
        Sleep(config.initDelayMs);
    }

    Intro::Status("Waiting for safe IL2CPP metadata access...");
    if (!Il2Cpp_WaitForMetadataReady(g_il2cppApi, kRuntimeTimeoutMs, kIl2CppPreDomainSettleMs)) {
        IntroStage(kIntroComplete, "IL2CPP metadata not ready; native mods skipped");
        return false;
    }

    if (config.safeMode) {
        IntroStage(kIntroModsSkippedOrDone, "Safe mode: native mods skipped");
        Log("[safe-mode][IL2CPP] runtime diagnostics complete; native mods and runtime event hooks disabled.");
        return true;
    }

    const NativeModLoadPlan modPlan = NativeMods_Discover(config);
    if (!NativeMods_RequiresRuntimeEvents(config.safeMode, modPlan.paths.size())) {
        IntroStage(kIntroModsSkippedOrDone, "No native mods found");
        return true;
    }

    const uint64_t runtimeEventCapabilities = RuntimeEvents_ConfigureIl2Cpp(g_il2cppApi);
    g_il2cppState.Transition(RuntimeReadiness::ModsAllowed, g_il2cppApi.Domain(), "IL2CPP domain");
    Log("[mods] pid=%lu tid=%lu backend=IL2CPP domain=%p gameAssembly=%p "
        "unityPlayer=%p entry",
        GetCurrentProcessId(), GetCurrentThreadId(), g_il2cppApi.Domain(),
        reinterpret_cast<void *>(g_il2cppApi.gameAssemblyBase), reinterpret_cast<void *>(g_il2cppApi.unityPlayerBase));
    IntroStage(kIntroModsBegin, "Loading IL2CPP mods...");
    NativeMods_Load(modPlan, ModContext_BuildIl2Cpp(config, g_il2cppApi, runtimeEventCapabilities));
    RuntimeEvents_AfterModsLoaded();
    return true;
}

const RuntimeBackendDescriptor kIl2CppBackend{"IL2CPP", true, RunIl2Cpp};
} // namespace

const RuntimeBackendDescriptor &RuntimeBackend_Il2Cpp() {
    return kIl2CppBackend;
}
