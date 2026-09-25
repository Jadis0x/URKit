#include "runtime_backend.h"

#include "intro.h"
#include "cursor_guard.h"
#include "hooks.h"
#include "loader_lifecycle.h"
#include "logger.h"
#include "main_thread_dispatcher.h"
#include "mod_context.h"
#include "native_mod_loader.h"
#include "platform_paths.h"
#include "runtime_events.h"
#include "safetyhook_backend.h"
#include "unreal_code_anchors.h"
#include "unreal_game_loop.h"
#include "unreal_process_memory.h"
#include "unreal_sdk_api.h"
#include "unreal_type_dump.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
// The object array appears late and fallback scans take seconds.
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
    std::unique_ptr<URK::Unreal::TypeDumper> dumper;
    // Blueprint classes to load for the dump, a few per frame.
    bool blueprintsListed = false;
    std::vector<std::string> blueprints;
    std::size_t blueprintNext = 0;
    std::size_t blueprintsLoaded = 0;
    ULONGLONG blueprintsStarted = 0;
    const URK_UnrealApi *api = nullptr;
    bool boundaryNoted = false;
    // Tick's counter write, found by strings: cross-checks the elected site.
    std::int32_t tickSite = -1;
    bool tickFound = false;
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
    HMODULE loader = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(&MainImage), &loader)) {
        const auto *ours = reinterpret_cast<const IMAGE_DOS_HEADER *>(loader);
        image.loaderStamp = reinterpret_cast<const IMAGE_NT_HEADERS *>(reinterpret_cast<const std::uint8_t *>(ours) +
                                                                       ours->e_lfanew)
                                ->FileHeader.TimeDateStamp;
    }
    return image;
}

double NowMs() {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return 1000.0 * static_cast<double>(now.QuadPart) / static_cast<double>(frequency.QuadPart);
}

std::string PlaceText(const URK_UnrealApi *api, const URK_UnrealPlace &place) {
    char text[512] = {};
    return api->place_read_text(&place, text, sizeof(text), nullptr) ? text : std::string();
}

// All Blueprint classes from the asset registry (UE5.1+), loaded or not.
std::vector<std::string> ListBlueprintClasses(const URK_UnrealApi *api) {
    std::vector<std::string> paths;
    const URK_UnrealObject helpers = api->find_object("AssetRegistryHelpers");
    const URK_UnrealObject kismet = api->find_object("KismetSystemLibrary");
    const URK_UnrealObject list = helpers ? api->find_function(helpers, "GetBlueprintAssets") : 0;
    const URK_UnrealObject load = kismet ? api->find_function(kismet, "LoadClassAsset_Blocking") : 0;
    if (!list || !load) {
        Log("[Unreal] Blueprint classes not preloaded: this engine has no GetBlueprintAssets; only loaded maps are dumped.");
        return paths;
    }
    if (URK_UnrealCallFrame *frame = api->call_frame_create(list)) {
        if (api->call(api->default_object_of(helpers), frame)) {
            URK_UnrealPlace assets{};
            assets.frame = frame;
            assets.member = "OutAssetData";
            const std::int32_t count = api->place_count(&assets);
            for (std::int32_t i = 0; i < count; ++i) {
                URK_UnrealPlace field = assets;
                field.step_count = 2;
                field.steps[0] = {URK_UNREAL_STEP_ELEMENT, i, nullptr};
                field.steps[1] = {URK_UNREAL_STEP_MEMBER, 0, "PackageName"};
                const std::string package = PlaceText(api, field);
                field.steps[1].name = "AssetName";
                const std::string asset = PlaceText(api, field);
                if (!package.empty() && !asset.empty())
                    paths.push_back(package + "." + asset + "_C");
            }
        }
        api->call_frame_destroy(frame);
    }
    return paths;
}

bool LoadBlueprintClass(const URK_UnrealApi *api, const std::string &path) {
    const URK_UnrealObject kismet = api->find_object("KismetSystemLibrary");
    const URK_UnrealObject load = kismet ? api->find_function(kismet, "LoadClassAsset_Blocking") : 0;
    URK_UnrealCallFrame *frame = load ? api->call_frame_create(load) : nullptr;
    if (!frame)
        return false;
    URK_UnrealPlace soft{};
    soft.frame = frame;
    soft.member = "AssetClass";
    URK_UnrealObject result = 0;
    const bool loaded = api->place_write_text(&soft, path.c_str()) && api->call(api->default_object_of(kismet), frame) &&
                        api->call_frame_get(frame, "ReturnValue", &result, sizeof(result)) && result;
    api->call_frame_destroy(frame);
    return loaded;
}

// Per frame, so neither loading nor describing stalls the game.
constexpr double kBlueprintBudgetMs = 3.0;
constexpr double kDumpBudgetMs = 2.0;

// Game thread, once per announced world: each map adds its Blueprint classes.
void ScanTypes(const char *map) {
    if (!g_gameLoop.blueprintsListed && g_gameLoop.api) {
        g_gameLoop.blueprintsListed = true;
        g_gameLoop.blueprints = ListBlueprintClasses(g_gameLoop.api);
        g_gameLoop.blueprintsStarted = GetTickCount64();
    }
    g_gameLoop.dumper->Scan(map, URK::Unreal::UnrealSdk_Enums());
}

// Game thread, every frame: Blueprint loads first, then queued types.
void StepTypes() {
    GameLoopState &state = g_gameLoop;
    if (state.blueprintNext < state.blueprints.size()) {
        const double started = NowMs();
        do {
            if (LoadBlueprintClass(state.api, state.blueprints[state.blueprintNext++]))
                ++state.blueprintsLoaded;
        } while (state.blueprintNext < state.blueprints.size() && NowMs() - started < kBlueprintBudgetMs);
        if (state.blueprintNext == state.blueprints.size()) {
            Log("[Unreal] Blueprint classes preloaded for the dump: %zu of %zu listed, in %llums.",
                state.blueprintsLoaded, state.blueprints.size(), GetTickCount64() - state.blueprintsStarted);
            state.dumper->Scan("preloaded Blueprints", URK::Unreal::UnrealSdk_Enums());
        }
        return;
    }
    state.dumper->Step(kDumpBudgetMs);
}

// Announced after BeginPlay so controller and pawn are findable.
void OnGameFrame(void *) {
    using namespace URK::Unreal;
    const DWORD thread = GetCurrentThreadId();
    if (thread != g_gameLoop.thread) {
        g_gameLoop.thread = thread;
        RuntimeEvents_SetMainThread(thread);
    }
    if (!g_gameLoop.boundaryNoted && ProcessEventHook::Instance().FrameBoundaryProven()) {
        g_gameLoop.boundaryNoted = true;
        Log("[Unreal] frames now tick at the engine's frame boundary (GFrameCounter), game thread %lu.", thread);
        const std::int32_t elected = ProcessEventHook::Instance().ElectedBoundary();
        if (!g_gameLoop.tickFound)
            Log("[Unreal] the frame boundary (site %d) is not cross-checked: FEngineLoop::Tick was not found.", elected);
        else if (elected == g_gameLoop.tickSite)
            Log("[Unreal] the frame boundary (site %d) is FEngineLoop::Tick's counter write, found both ways.", elected);
        else
            Log("[Unreal][WARNING] the elected frame boundary (site %d) is not FEngineLoop::Tick's write (site %d).",
                elected, g_gameLoop.tickSite);
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
        if (g_gameLoop.dumper)
            ScanTypes(scene.name);
    }
    if (g_gameLoop.dumper)
        StepTypes();

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

void OnFrameBoundary(URK_HookRegisters *, void *site) {
    URK::Unreal::ProcessEventHook::Instance().FrameBoundary(reinterpret_cast<std::uintptr_t>(site));
}

// Measured offsets under UE4SS table names, for check-layout.py.
std::string MeasuredLayout(URK::Unreal::UnrealEngine &engine) {
    using namespace URK::Unreal;
    std::string text;
    const auto add = [&text](const char *name, std::int32_t offset) {
        char item[96];
        if (offset == kOffsetNotFound)
            std::snprintf(item, sizeof(item), " %s=?", name);
        else
            std::snprintf(item, sizeof(item), " %s=0x%X", name, static_cast<unsigned>(offset));
        text += item;
    };
    const ObjectOffsets &object = engine.Finder().Offsets();
    add("UObjectBase.ObjectFlags", object.flags);
    add("UObjectBase.InternalIndex", object.index);
    add("UObjectBase.ClassPrivate", object.classPointer);
    add("UObjectBase.NamePrivate", object.name);
    add("UObjectBase.OuterPrivate", object.outer);
    const ObjectItemLayout &item = engine.Finder().Objects().Layout().item;
    add("FUObjectItem.Object", item.pointerOffset);
    add("FUObjectItem.UEP_TotalSize", item.stride);
    const StructOffsets &structs = engine.Structs();
    add("UField.Next", structs.fieldNext);
    add("UStruct.SuperStruct", structs.superStruct);
    add("UStruct.Children", structs.children);
    add("UStruct.PropertiesSize", structs.propertiesSize);
    add("UStruct.MinAlignment", structs.minAlignment);
    add("UClass.ClassCastFlags", structs.castFlags);
    add("UClass.ClassDefaultObject", engine.Classes().classDefaultObject);
    const FieldOffsets &fields = engine.Fields();
    add("UStruct.ChildProperties", fields.childProperties);
    add("FField.ClassPrivate", fields.fieldClass);
    add("FField.Next", fields.fieldNext);
    add("FField.NamePrivate", fields.fieldName);
    add("FFieldClass.CastFlags", fields.fieldClassCastFlags);
    add("FProperty.ArrayDim", fields.arrayDim);
    add("FProperty.ElementSize", fields.elementSize);
    add("FProperty.PropertyFlags", fields.propertyFlags);
    add("FProperty.Offset_Internal", fields.offsetInternal);
    const PropertyTailOffsets &tail = engine.Values().Tail();
    add("FProperty.UEP_TotalSize", tail.tail);
    add("FArrayProperty.Inner", tail.arrayInner);
    add("FSetProperty.ElementProp", tail.setElement);
    add("FMapProperty.KeyProp", tail.mapKey);
    add("FMapProperty.ValueProp", tail.mapValue);
    add("FEnumProperty.Enum", tail.enumPropertyEnum);
    const FunctionOffsets &functions = engine.Functions();
    add("UFunction.FunctionFlags", functions.functionFlags);
    add("UFunction.NumParms", functions.numParms);
    add("UFunction.ParmsSize", functions.parmsSize);
    add("UFunction.ReturnValueOffset", functions.returnValueOffset);
    add("UFunction.Func", functions.func);
    const ProcessEventLocation &processEvent = engine.ProcessEvent();
    add("vtable:UObject.ProcessEvent", processEvent.Resolved() ? processEvent.vtableIndex : kOffsetNotFound);
    return text;
}

// Hooks every GFrameCounter write; ProcessEventHook elects the frame loop.
std::size_t HookFrameBoundary(URK::Unreal::UnrealEngine &engine, URK::Unreal::Address counter, std::size_t *found) {
    using namespace URK::Unreal;
    *found = 0;
    if (counter == kNullAddress || !Hook_MidAvailable())
        return 0;
    std::vector<ScanRegion> code;
    for (const Address module : engine.Presence().runtimeModules) {
        const std::vector<ScanRegion> regions = ModuleCodeRegions(engine.Reader(), module);
        code.insert(code.end(), regions.begin(), regions.end());
    }
    const std::vector<Address> writes = GameLoop::FindFrameCounterWrites(engine.Reader(), code, engine.Bounds(),
                                                                         counter, &SafetyHookBackend_InstructionLength);
    *found = writes.size();
    for (const Address module : engine.Presence().runtimeModules) {
        const Address tick = FindEngineLoopTick(engine.Reader(), module, engine.Bounds());
        if (tick == kNullAddress)
            continue;
        g_gameLoop.tickFound = true;
        for (std::size_t i = 0; i < writes.size(); ++i) {
            if (engine.Bounds().PrimaryBegin(writes[i]) == tick)
                g_gameLoop.tickSite = static_cast<std::int32_t>(i);
        }
    }
    std::size_t hooked = 0;
    for (std::size_t i = 0; i < writes.size() && i < ProcessEventHook::kMaxBoundarySites; ++i) {
        URK_MidHookOptions options{};
        options.size = sizeof(options);
        options.userData = reinterpret_cast<void *>(static_cast<std::uintptr_t>(i));
        if (Hook_MidAttach(reinterpret_cast<void *>(writes[i]), &OnFrameBoundary, &options))
            ++hooked;
    }
    return hooked;
}

// Owns the ProcessEvent hook; the tick starts after mods load.
bool PrepareGameLoop(URK::Unreal::UnrealEngine &engine, const volatile std::uint64_t **frameCounter) {
    using namespace URK::Unreal;
    if (!UnrealSdk_HoldProcessEventHook()) {
        Log("[Unreal][WARNING] ProcessEvent hook unavailable (%s); update() and scene events stay off.",
            engine.ProcessEventResolved() ? "the hook did not install" : engine.ProcessEventFailure().c_str());
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
    std::size_t writes = 0;
    const std::size_t boundaries = HookFrameBoundary(engine, counter, &writes);
    Log("[Unreal] game loop ready: frames=%s, frame boundary hooks=%zu (counter writes found=%zu).",
        counter != kNullAddress ? "GFrameCounter" : "paced by time", boundaries, writes);

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
            for (const std::string &line : engine.ExplainFailure())
                Log("[Unreal][ERROR]   %s", line.c_str());
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
    // The scan is expensive; skip it when there are no mods.
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
    if (std::string_view(profile.locatedBy) == "data scan") {
        for (const std::string &line : engine.ExplainAnchors())
            Log("[Unreal][WARNING] slow start, code anchors made no pair: %s", line.c_str());
    }

    const ULONGLONG versionStarted = GetTickCount64();
    engine.ResolveVersionFromCode(&SafetyHookBackend_InstructionLength);
    const EngineVersion &version = engine.Version();
    const ProcessEventLocation &processEvent = engine.ProcessEvent();
    Log("[Unreal] engine=%d.%d.%d (from %s, %llums) branch='%s' processEventSlot=%d.", version.major, version.minor,
        version.patch, version.Known() ? version.source.c_str() : "nowhere", GetTickCount64() - versionStarted,
        version.branch.c_str(), processEvent.Resolved() ? processEvent.vtableIndex : -1);
    const PropertyTailOffsets &tail = engine.Values().Tail();
    Log("[Unreal] property tail=0x%X arrayInner=0x%X setElement=0x%X mapKey=0x%X mapValue=0x%X enum=0x%X.", tail.tail,
        tail.arrayInner, tail.setElement, tail.mapKey, tail.mapValue, tail.enumPropertyEnum);
    if (!tail.Resolved())
        Log("[Unreal][WARNING] property tail unresolved: %s; containers, structs and object classes are unavailable.",
            tail.failure.c_str());
    Log("[Unreal] layout:%s.", MeasuredLayout(engine).c_str());
    const NameLayout &names = engine.Finder().Names().Layout();
    Log("[Unreal] FName: %d bytes, display index %s, number %s.", names.size,
        names.displayIndexOffset == kOffsetNotFound ? "none" : "at +4",
        names.numberOffset == kOffsetNotFound ? "kept in the name pool" : (names.numberOffset == 8 ? "at +8" : "at +4"));
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
    g_gameLoop.api = api;
    if (dumpTypes && !gameLoop)
        Log("[Unreal][ERROR] DumpTypes needs the game loop; nothing will be dumped.");
    if (dumpTypes && gameLoop) {
        const TypeDumpSources sources{engine.Finder(), engine.Structs(), engine.Chain(), engine.Values(),
                                      engine.Functions(), engine.Types()};
        g_gameLoop.dumper = std::make_unique<TypeDumper>(sources, Platform_ExeDir() + "URKit_unreal_types.txt",
                                                         MainImage(engine.Version()),
                                                         [](const std::string &line) { Log("%s", line.c_str()); });
    }
    if (gameLoop)
        ProcessEventHook::Instance().SetFrameTick(&OnGameFrame, nullptr, frameCounter);
    return true;
}

const RuntimeBackendDescriptor kUnrealBackend{"Unreal", true, RunUnreal};
} // namespace

const RuntimeBackendDescriptor &RuntimeBackend_Unreal() {
    return kUnrealBackend;
}
