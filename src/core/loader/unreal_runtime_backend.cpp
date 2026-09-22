#include "runtime_backend.h"

#include "intro.h"
#include "loader_lifecycle.h"
#include "logger.h"
#include "mod_context.h"
#include "native_mod_loader.h"
#include "safetyhook_backend.h"
#include "unreal_process_memory.h"
#include "unreal_sdk_api.h"

#include <windows.h>

namespace {
// The scan itself costs seconds, and the object array is built long after a
// proxy lands, so this budget covers several attempts rather than one.
constexpr DWORD kBootstrapTimeoutMs = 60000;
constexpr DWORD kBootstrapRetryMs = 500;
constexpr float kIntroWaitingRuntime = 0.24f;
constexpr float kIntroRuntimeReady = 0.58f;
constexpr float kIntroModsBegin = 0.72f;
constexpr float kIntroModsSkippedOrDone = 0.96f;
constexpr float kIntroComplete = 1.0f;

void IntroStage(float value, const char *status) {
    Intro::Progress(value, status ? status : "");
}

void *InstallerAttach(void *, void *target, void *detour, void **trampoline) {
    return SafetyHookBackend_CreateInline(target, detour, trampoline);
}

bool InstallerDetach(void *, void *handle) {
    return SafetyHookBackend_DestroyInline(handle);
}

bool WaitForEngine(URK::Unreal::UnrealEngine &engine) {
    const ULONGLONG deadline = GetTickCount64() + kBootstrapTimeoutMs;
    for (;;) {
        if (engine.EnsureBootstrapped())
            return true;
        if (engine.RuledOut()) {
            Log("[Unreal][ERROR] This process is not an Unreal build; nothing to bootstrap.");
            return false;
        }
        if (LoaderLifecycle_StopRequested() || GetTickCount64() >= deadline)
            return false;

        HANDLE stopEvent = LoaderLifecycle_StopEvent();
        if (stopEvent) {
            if (WaitForSingleObject(stopEvent, kBootstrapRetryMs) == WAIT_OBJECT_0)
                return false;
        } else {
            Sleep(kBootstrapRetryMs);
        }
    }
}

bool RunUnreal(Config &config) {
    using namespace URK::Unreal;
    UnrealEngine &engine = UnrealEngine::Instance();

    IntroStage(kIntroWaitingRuntime, "Waiting for Unreal engine globals...");
    Log(">> UNREAL backend");
    if (config.initDelayMs) {
        Intro::Status("Waiting before Unreal globals are searched...");
        Log("[Unreal] Applying InitDelayMs=%d before the bootstrap scan.", config.initDelayMs);
        Sleep(config.initDelayMs);
    }

    if (!WaitForEngine(engine)) {
        IntroStage(kIntroComplete, "Unreal runtime unavailable");
        return false;
    }

    const EngineVersion &version = engine.Version();
    const ProcessEventLocation &processEvent = engine.ProcessEvent();
    Log("[Unreal] engine=%d.%d.%d branch='%s' fieldProperties=%s processEventSlot=%d.", version.major, version.minor,
        version.patch, version.branch.c_str(), version.UsesFieldProperties() ? "yes" : "no",
        processEvent.Resolved() ? processEvent.vtableIndex : -1);
    IntroStage(kIntroRuntimeReady, "Unreal reflection ready");

    if (config.safeMode) {
        IntroStage(kIntroModsSkippedOrDone, "Safe mode: native mods skipped");
        Log("[safe-mode][Unreal] bootstrap diagnostics complete; native mods disabled.");
        return true;
    }

    const NativeModLoadPlan modPlan = NativeMods_Discover(config);
    if (modPlan.Empty()) {
        IntroStage(kIntroModsSkippedOrDone, "No native mods found");
        return true;
    }

    // An empty installer leaves the API read-only instead of failing the load.
    HookInstaller installer{};
    if (SafetyHookBackend_Available()) {
        installer.attach = &InstallerAttach;
        installer.detach = &InstallerDetach;
    } else {
        Log("[Unreal][WARNING] No inline hook engine; ProcessEvent hooking and calls stay unavailable.");
    }

    const URK_UnrealApi *api = UnrealSdkApi(installer);
    Log("[mods] pid=%lu tid=%lu backend=Unreal module=%p entry", GetCurrentProcessId(), GetCurrentThreadId(),
        reinterpret_cast<void *>(MainModuleBase()));
    IntroStage(kIntroModsBegin, "Loading Unreal mods...");
    NativeMods_Load(modPlan, ModContext_BuildUnreal(config, api, MainModuleBase()));
    return true;
}

const RuntimeBackendDescriptor kUnrealBackend{"Unreal", true, RunUnreal};
} // namespace

const RuntimeBackendDescriptor &RuntimeBackend_Unreal() {
    return kUnrealBackend;
}
