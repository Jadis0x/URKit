#include "runtime_backend.h"

#include "intro.h"
#include "cursor_guard.h"
#include "loader_lifecycle.h"
#include "logger.h"
#include "main_thread_dispatcher.h"
#include "mod_context.h"
#include "native_mod_loader.h"
#include "platform_paths.h"
#include "runtime_events.h"
#include "safetyhook_backend.h"
#include "unreal_game_loop.h"
#include "unreal_process_memory.h"
#include "unreal_sdk_api.h"
#include "unreal_type_dump.h"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {
// The object array is built long after a proxy lands; a fallback data scan
// costs seconds, so this budget covers several of them.
constexpr DWORD kBootstrapTimeoutMs = 60000;
constexpr DWORD kBootstrapRetryMs = 100;
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

// Game thread only: the frame tick is its sole writer and reader.
struct GameLoopState {
    std::unique_ptr<URK::Unreal::GameLoop> loop;
    URK::Unreal::Address world = URK::Unreal::kNullAddress;
    bool announced = false;
    DWORD thread = 0;
    bool dumpTypes = false;
};
GameLoopState g_gameLoop;

URK::Unreal::TypeDumpImage MainImage(const URK::Unreal::EngineVersion &version) {
    URK::Unreal::TypeDumpImage image;
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(GetModuleHandleA(nullptr));
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(reinterpret_cast<const std::uint8_t *>(dos) +
                                                                 dos->e_lfanew);
    image.timeDateStamp = nt->FileHeader.TimeDateStamp;
    image.sizeOfImage = nt->OptionalHeader.SizeOfImage;
    image.engine = std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
                   std::to_string(version.patch);
    return image;
}

// Game thread, once per announced world: each map adds its Blueprint classes.
void DumpTypes(const char *map) {
    using namespace URK::Unreal;
    UnrealEngine &engine = UnrealEngine::Instance();
    const ULONGLONG started = GetTickCount64();
    const TypeDumpBlocks blocks = DumpClasses(engine.Finder(), engine.Structs(), engine.Chain(), engine.Values(),
                                              engine.Functions(), engine.Types(), UnrealSdk_Enums());
    const std::string path = Platform_ExeDir() + "URKit_unreal_types.txt";
    const int added = WriteTypeDump(path, MainImage(engine.Version()), blocks);
    if (added < 0)
        Log("[Unreal][ERROR] Could not write %s.", path.c_str());
    else
        Log("[Unreal] types of %s dumped in %llums: %zu types, %d new, to %s.", map, GetTickCount64() - started,
            blocks.size(), added, path.c_str());
}

// A world is announced once it has begun play, so a mod handed the scene can
// already find its controller and pawn.
void OnGameFrame(void *) {
    using namespace URK::Unreal;
    const DWORD thread = GetCurrentThreadId();
    if (thread != g_gameLoop.thread) {
        g_gameLoop.thread = thread;
        RuntimeEvents_SetMainThread(thread);
    }

    const WorldState world = g_gameLoop.loop->CurrentWorld();
    if (world.world != g_gameLoop.world) {
        g_gameLoop.world = world.world;
        g_gameLoop.announced = false;
    }
    if (!g_gameLoop.announced && world.world != kNullAddress && world.begunPlay) {
        g_gameLoop.announced = true;
        URK_SceneInfo scene{};
        scene.size = sizeof(scene);
        scene.buildIndex = -1;
        scene.handle = g_gameLoop.loop->ObjectIndex(world.world);
        strncpy_s(scene.name, g_gameLoop.loop->MapName(world.world).c_str(), _TRUNCATE);
        // A new world object is a new load, even of the same map.
        RuntimeEvents_ObserveScene(scene, true);
        if (g_gameLoop.dumpTypes)
            DumpTypes(scene.name);
    }

    RuntimeEvents_PumpExternal();
    MainThread_Drain();
    UnrealSdk_ReleasePending();
}

void UnrealSdkLog(const char *message) {
    Log("%s", message);
}

// The menu cursor never changes engine state; see CursorGuard.
bool UnrealMenuCursor(bool open) {
    if (!open) {
        CursorGuard::Release();
        return true;
    }
    return CursorGuard::Engage();
}

// Holds the ProcessEvent hook for the loader. The tick itself starts only once
// mods are loaded, so none misses the first scene.
bool PrepareGameLoop(URK::Unreal::UnrealEngine &engine, const volatile std::uint64_t **frameCounter) {
    using namespace URK::Unreal;
    if (!UnrealSdk_HoldProcessEventHook()) {
        Log("[Unreal][WARNING] ProcessEvent hook unavailable; update() and scene events stay off.");
        return false;
    }

    std::vector<ScanRegion> writable;
    for (const Address module : engine.Presence().runtimeModules) {
        const std::vector<ScanRegion> regions = ModuleWritableRegions(engine.Reader(), module);
        writable.insert(writable.end(), regions.begin(), regions.end());
    }
    const Address counter = GameLoop::FindFrameCounter(engine.Finder(), engine.Functions(), engine.Bounds(), writable);
    *frameCounter = reinterpret_cast<const volatile std::uint64_t *>(counter);
    g_gameLoop.loop = std::make_unique<GameLoop>(engine.Finder(), engine.Types(), engine.Chain(), engine.Values());
    Log("[Unreal] game loop ready: frames=%s.", counter != kNullAddress ? "GFrameCounter" : "paced by time");

    RuntimeCursorProvider cursor{};
    cursor.read = &CursorGuard::GameState;
    cursor.setMenuOpen = &UnrealMenuCursor;
    RuntimeEvents_ConfigureExternal("Unreal", URK_RUNTIME_CAP_SCENE_EVENTS | URK_RUNTIME_CAP_CURSOR_CONTROL, cursor);
    MainThread_SetDispatchTargetAvailable(true);
    return true;
}

bool WaitForEngine(URK::Unreal::UnrealEngine &engine) {
    const ULONGLONG deadline = GetTickCount64() + kBootstrapTimeoutMs;
    for (;;) {
        if (engine.EnsureBootstrapped())
            return true;
        if (engine.RuledOut()) {
            Log("[Unreal][ERROR] Unsupported process: %s.", engine.LastFailure());
            return false;
        }
        if (LoaderLifecycle_StopRequested())
            return false;
        if (GetTickCount64() >= deadline) {
            Log("[Unreal][ERROR] Bootstrap gave up after %lums: %s.", kBootstrapTimeoutMs, engine.LastFailure());
            return false;
        }

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

    Log(">> UNREAL backend");
    // The scan costs seconds and reads the whole image; a process with no mods
    // must not pay for it merely because a proxy was loaded.
    const NativeModLoadPlan modPlan = config.safeMode ? NativeModLoadPlan{} : NativeMods_Discover(config);
    const bool dumpTypes = config.unrealDumpTypes && !config.safeMode;
    if (!config.safeMode && modPlan.Empty() && !dumpTypes) {
        IntroStage(kIntroModsSkippedOrDone, "No native mods found");
        return true;
    }

    IntroStage(kIntroWaitingRuntime, "Waiting for Unreal engine globals...");
    if (config.initDelayMs) {
        Intro::Status("Waiting before Unreal globals are searched...");
        Log("[Unreal] Applying InitDelayMs=%d before the bootstrap scan.", config.initDelayMs);
        Sleep(config.initDelayMs);
    }

    const ULONGLONG bootstrapStarted = GetTickCount64();
    if (!WaitForEngine(engine)) {
        IntroStage(kIntroComplete, "Unreal runtime unavailable");
        return false;
    }
    const BootstrapProfile &profile = engine.Profile();
    Log("[Unreal] bootstrap took %llums via %s: attempts=%u scans=%u failed=%llums anchors=%llums (arrays=%zu "
        "pools=%zu) locate=%llums index=%llums offsets=%llums functions=%llums processEvent=%llums queries=%llu.",
        GetTickCount64() - bootstrapStarted, profile.locatedBy, profile.attempts, profile.scans, profile.failedMs,
        profile.anchorMs, profile.anchoredArrays, profile.anchoredPools, profile.locateMs, profile.indexMs,
        profile.offsetsMs, profile.functionsMs, profile.processEventMs, engine.Reader().Queries());

    const EngineVersion &version = engine.Version();
    const ProcessEventLocation &processEvent = engine.ProcessEvent();
    Log("[Unreal] engine=%d.%d.%d branch='%s' processEventSlot=%d.", version.major, version.minor, version.patch,
        version.branch.c_str(), processEvent.Resolved() ? processEvent.vtableIndex : -1);
    IntroStage(kIntroRuntimeReady, "Unreal reflection ready");

    if (config.safeMode) {
        IntroStage(kIntroModsSkippedOrDone, "Safe mode: native mods skipped");
        Log("[safe-mode][Unreal] bootstrap diagnostics complete; native mods disabled.");
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

    UnrealSdk_SetLog(&UnrealSdkLog);
    const URK_UnrealApi *api = UnrealSdkApi(installer);
    const volatile std::uint64_t *frameCounter = nullptr;
    const bool gameLoop = PrepareGameLoop(engine, &frameCounter);

    Log("[mods] pid=%lu tid=%lu backend=Unreal module=%p entry", GetCurrentProcessId(), GetCurrentThreadId(),
        reinterpret_cast<void *>(MainModuleBase()));
    IntroStage(kIntroModsBegin, "Loading Unreal mods...");
    if (!modPlan.Empty())
        NativeMods_Load(modPlan, ModContext_BuildUnreal(config, api, MainModuleBase(), gameLoop));
    g_gameLoop.dumpTypes = dumpTypes;
    if (dumpTypes && !gameLoop)
        Log("[Unreal][ERROR] DumpTypes needs the game loop; nothing will be dumped.");
    if (gameLoop)
        ProcessEventHook::Instance().SetFrameTick(&OnGameFrame, nullptr, frameCounter);
    return true;
}

const RuntimeBackendDescriptor kUnrealBackend{"Unreal", true, RunUnreal};
} // namespace

const RuntimeBackendDescriptor &RuntimeBackend_Unreal() {
    return kUnrealBackend;
}
