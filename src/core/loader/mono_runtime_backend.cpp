#include "runtime_backend.h"

#include "intro.h"
#include "logger.h"
#include "mod_context.h"
#include "mono_api.h"
#include "mono_runtime_bootstrap.h"
#include "native_mod_loader.h"
#include "runtime_events.h"
#include "runtime_state.h"

#include <windows.h>

#include <memory>

namespace {
constexpr int kRuntimeTimeoutMs = 30000;
constexpr float kIntroWaitingRuntime = 0.24f;
constexpr float kIntroExportsResolved = 0.40f;
constexpr float kIntroAttachingRuntime = 0.50f;
constexpr float kIntroRuntimeAttached = 0.58f;
constexpr float kIntroModsBegin = 0.72f;
constexpr float kIntroModsSkippedOrDone = 0.96f;
constexpr float kIntroComplete = 1.0f;

RuntimeState g_monoState{"Mono"};
MonoApi g_monoApi{};

void IntroStage(float value, const char *status) {
    Intro::Progress(value, status ? status : "");
}

bool RunMono(Config &config) {
    MonoApi &mono = g_monoApi;

    g_monoState.Reset();
    RuntimeEvents_Reset();
    IntroStage(kIntroWaitingRuntime, "Waiting for Mono runtime...");
    Log(">> MONO backend");
    if (!Mono_Resolve(mono, kRuntimeTimeoutMs)) {
        IntroStage(kIntroComplete, "Mono runtime unavailable");
        return false;
    }

    IntroStage(kIntroExportsResolved, "Mono exports resolved");
    g_monoState.Transition(RuntimeReadiness::ModuleSeen, nullptr, "module loader");
    g_monoState.Transition(RuntimeReadiness::ExportsResolved, nullptr, "export resolver");

    IntroStage(kIntroAttachingRuntime, "Attaching loader thread to Mono...");
    const MonoRuntimeBootstrapResult bootstrap = MonoRuntimeBootstrap_Attach(mono, g_monoState, kRuntimeTimeoutMs);
    MonoDomain *monoDomain = bootstrap.domain;
    MonoThread *attachedThread = bootstrap.attachedThread;
    if (!monoDomain || !attachedThread) {
        IntroStage(kIntroComplete, "Mono attach failed");
        return false;
    }
    auto detachLoaderThread = [](MonoThread *thread) { Mono_DetachThread(g_monoApi, thread); };
    std::unique_ptr<MonoThread, decltype(detachLoaderThread)> loaderThreadAttachment(attachedThread,
                                                                                    detachLoaderThread);

    IntroStage(kIntroRuntimeAttached, "Mono loader thread attached");
    if (config.initDelayMs) {
        Intro::Status("Waiting for configured init delay...");
        Sleep(config.initDelayMs);
    }

    Intro::Status("Waiting for Mono UnityEngine metadata...");
    if (!RuntimeEvents_WaitForMonoUnityReady(mono, kRuntimeTimeoutMs)) {
        IntroStage(kIntroComplete, "Mono UnityEngine metadata not ready; native mods skipped");
        return false;
    }
    g_monoState.Transition(RuntimeReadiness::AssembliesStable, monoDomain, g_monoState.DomainSource());

    if (config.safeMode) {
        IntroStage(kIntroModsSkippedOrDone, "Safe mode: native mods skipped");
        Log("[safe-mode][Mono] runtime attach diagnostics complete; native mods and runtime event hooks disabled.");
        return true;
    }

    const NativeModLoadPlan modPlan = NativeMods_Discover(config);
    if (!NativeMods_RequiresRuntimeEvents(config.safeMode, modPlan.paths.size())) {
        IntroStage(kIntroModsSkippedOrDone, "No native mods found");
        return true;
    }

    const uint64_t runtimeEventCapabilities = RuntimeEvents_ConfigureMono(mono);
    g_monoState.Transition(RuntimeReadiness::ModsAllowed, monoDomain, g_monoState.DomainSource());
    Log("[mods] pid=%lu tid=%lu backend=Mono attached=yes domain=%p entry", GetCurrentProcessId(), GetCurrentThreadId(),
        monoDomain);
    IntroStage(kIntroModsBegin, "Loading Mono mods...");
    NativeMods_Load(modPlan, ModContext_BuildMono(config, mono, mono.base, runtimeEventCapabilities));
    RuntimeEvents_AfterModsLoaded();
    return true;
}

const RuntimeBackendDescriptor kMonoBackend{
    "Mono",
    true,
    RunMono,
};
} // namespace

const RuntimeBackendDescriptor &RuntimeBackend_Mono() {
    return kMonoBackend;
}
