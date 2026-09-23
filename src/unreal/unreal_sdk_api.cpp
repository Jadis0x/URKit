#include "unreal_sdk_api.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <vector>

namespace URK::Unreal {
namespace {

HookInstaller g_installer;

bool CopyOut(const std::string &text, char *output, std::size_t outputSize) {
    if (!output || outputSize == 0)
        return false;
    const std::size_t copy = std::min(text.size(), outputSize - 1);
    std::memcpy(output, text.data(), copy);
    output[copy] = '\0';
    return true;
}

// One member resolved from an instance. Every value entry goes through this,
// so a missing member or mismatched kind fails the same way everywhere.
struct ResolvedMember {
    PropertyInfo info;
};

std::optional<ResolvedMember> Resolve(UnrealEngine &engine, Address object, const char *memberName) {
    if (object == kNullAddress || !memberName || !engine.Available())
        return std::nullopt;

    const Address classObject = engine.Finder().ClassOf(object);
    if (classObject == kNullAddress)
        return std::nullopt;

    const Address field = engine.Chain().FindMemberDeep(classObject, memberName);
    if (field == kNullAddress)
        return std::nullopt;

    std::optional<PropertyInfo> info = engine.Values().Describe(field);
    if (!info || !info->Resolved())
        return std::nullopt;

    return ResolvedMember{*info};
}

// --- availability / version ---------------------------------------------

int Unreal_IsAvailable() { return UnrealEngine::Instance().EnsureBootstrapped() ? 1 : 0; }

void Unreal_EngineVersion(std::int32_t *major, std::int32_t *minor, std::int32_t *patch) {
    const EngineVersion &version = UnrealEngine::Instance().Version();
    if (major)
        *major = version.major;
    if (minor)
        *minor = version.minor;
    if (patch)
        *patch = version.patch;
}

int Unreal_UsesFieldProperties() { return UnrealEngine::Instance().Version().UsesFieldProperties() ? 1 : 0; }

// --- object lookup --------------------------------------------------------

URK_UnrealObject Unreal_FindObject(const char *name) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !engine.EnsureBootstrapped())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().Find(name);
}

URK_UnrealObject Unreal_FindObjectInOuter(const char *name, const char *outerName) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !outerName || !engine.EnsureBootstrapped())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().FindInOuter(name, outerName);
}

URK_UnrealObject Unreal_ClassOf(URK_UnrealObject object) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().ClassOf(object);
}

URK_UnrealObject Unreal_OuterOf(URK_UnrealObject object) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().OuterOf(object);
}

int Unreal_NameOf(URK_UnrealObject object, char *output, std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return 0;
    const std::optional<std::string> name = engine.Finder().NameOf(object);
    return name ? (CopyOut(*name, output, outputSize) ? 1 : 0) : 0;
}

// --- type queries -----------------------------------------------------------

int Unreal_IsChildOf(URK_UnrealObject structObject, URK_UnrealObject base) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return 0;
    return engine.Types().IsChildOf(structObject, base) ? 1 : 0;
}

int Unreal_IsA(URK_UnrealObject object, URK_UnrealObject classObject) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return 0;
    return engine.Types().IsA(object, classObject) ? 1 : 0;
}

URK_UnrealObject Unreal_DefaultObjectOf(URK_UnrealObject classObject) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Types().DefaultObjectOf(classObject);
}

std::size_t Unreal_InstancesOf(URK_UnrealObject classObject, URK_UnrealObject *output, std::size_t outputCapacity,
                               int exact) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return 0;

    TypeQueries::InstanceQuery query{};
    query.classObject = classObject;
    query.exact = exact != 0;
    const std::vector<Address> instances = engine.Types().InstancesOf(query);

    if (output && outputCapacity > 0) {
        const std::size_t count = std::min(outputCapacity, instances.size());
        for (std::size_t i = 0; i < count; ++i)
            output[i] = instances[i];
    }
    return instances.size();
}

// --- property access ---------------------------------------------------------

int Unreal_DescribeProperty(URK_UnrealObject object, const char *memberName, URK_UnrealPropertyInfo *info) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!info)
        return 0;
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;

    info->size = sizeof(URK_UnrealPropertyInfo);
    info->kind = static_cast<std::int32_t>(resolved->info.kind);
    info->element_size = resolved->info.elementSize;
    info->array_dim = resolved->info.arrayDim;
    info->inner = resolved->info.inner;
    return 1;
}

int Unreal_ReadInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<std::int64_t> value = engine.Values().ReadInteger(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value;
    return 1;
}

int Unreal_ReadFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<double> value = engine.Values().ReadFloating(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value;
    return 1;
}

int Unreal_ReadBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<bool> value = engine.Values().ReadBool(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value ? 1 : 0;
    return 1;
}

URK_UnrealObject Unreal_ReadObject(URK_UnrealObject object, const char *memberName, std::int32_t index) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return URK_UNREAL_NULL_OBJECT;
    return engine.Values().ReadObject(object, resolved->info, index);
}

int Unreal_ReadName(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                    std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    const std::optional<std::string> value = engine.Values().ReadName(object, resolved->info, index);
    return value ? (CopyOut(*value, output, outputSize) ? 1 : 0) : 0;
}

int Unreal_ReadString(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                      std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    const std::optional<std::string> value = engine.Values().ReadString(object, resolved->info, index);
    return value ? (CopyOut(*value, output, outputSize) ? 1 : 0) : 0;
}

int Unreal_WriteInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteInteger(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

int Unreal_WriteFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteFloating(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

int Unreal_WriteBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteBool(engine.Writer(), object, resolved->info, value != 0, index) ? 1 : 0;
}

int Unreal_WriteObject(URK_UnrealObject object, const char *memberName, std::int32_t index, URK_UnrealObject value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteObject(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

// --- calling ------------------------------------------------------------------

// A UFunction's outer is the class that declares it, so inherited functions
// are found by climbing. An instance stands for its class.
URK_UnrealObject Unreal_FindFunction(URK_UnrealObject ownerClass, const char *name) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !engine.Available() || ownerClass == kNullAddress)
        return URK_UNREAL_NULL_OBJECT;

    Address owner = ownerClass;
    if (!ObjectIs(engine.Finder(), engine.Structs(), owner, kCastFlagClass))
        owner = engine.Finder().ClassOf(owner);
    constexpr int kMaxDepth = 64;
    for (int depth = 0; owner != kNullAddress && depth < kMaxDepth; ++depth) {
        const Address function = engine.Finder().FindInOuter(name, owner);
        if (function != kNullAddress)
            return function;
        owner = engine.Types().SuperOf(owner);
    }
    return URK_UNREAL_NULL_OBJECT;
}

URK_UnrealCallFrame *Unreal_CallFrameCreate(URK_UnrealObject function) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available() || function == kNullAddress)
        return nullptr;

    const std::optional<FunctionInfo> info =
        DescribeFunction(engine.Chain(), engine.Values(), engine.Functions(), function);
    if (!info)
        return nullptr;

    return reinterpret_cast<URK_UnrealCallFrame *>(new CallFrame(*info));
}

void Unreal_CallFrameDestroy(URK_UnrealCallFrame *frame) { delete reinterpret_cast<CallFrame *>(frame); }

int Unreal_CallFrameSet(URK_UnrealCallFrame *frame, const char *parameterName, const void *value, std::size_t size) {
    if (!frame || !parameterName)
        return 0;
    return reinterpret_cast<CallFrame *>(frame)->Set(parameterName, value, size) ? 1 : 0;
}

int Unreal_CallFrameGet(const URK_UnrealCallFrame *frame, const char *parameterName, void *output,
                        std::size_t size) {
    if (!frame || !parameterName)
        return 0;
    return reinterpret_cast<const CallFrame *>(frame)->Get(parameterName, output, size) ? 1 : 0;
}

int Unreal_Call(URK_UnrealObject object, URK_UnrealCallFrame *frame) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!frame || object == kNullAddress || !engine.ProcessEventResolved())
        return 0;

    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (!hook.Installed())
        return 0;
    if (hook.GameThreadId() == 0 || hook.GameThreadId() != GetCurrentThreadId())
        return 0;

    CallFrame *callFrame = reinterpret_cast<CallFrame *>(frame);
    return InvokeProcessEvent(engine.ProcessEvent(), object, callFrame->Function().function, callFrame->Data()) ? 1
                                                                                                                : 0;
}

// --- hooking / dispatch ---------------------------------------------------

// The loader's game loop and mods share one hook. It comes off only when
// neither holds it, so a mod's remove cannot stop the game loop.
std::mutex g_hookMutex;
bool g_loaderHold = false;
bool g_modHold = false;

bool EnsureHookInstalled() {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.EnsureBootstrapped() || !engine.ProcessEventResolved() || !g_installer.Valid())
        return false;

    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (hook.Installed())
        return true;

    const std::vector<Address> implementations =
        ProcessEventImplementations(engine.Finder(), engine.Types(), engine.Structs(), engine.ProcessEvent());
    return hook.Install(g_installer, implementations, engine.ProcessEvent());
}

int Unreal_HookInstall() {
    std::lock_guard lock(g_hookMutex);
    if (!EnsureHookInstalled())
        return 0;
    g_modHold = true;
    return 1;
}

int Unreal_HookInstalled() { return ProcessEventHook::Instance().Installed() ? 1 : 0; }

int Unreal_HookRemove() {
    std::lock_guard lock(g_hookMutex);
    g_modHold = false;
    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (!g_loaderHold)
        return hook.Remove() ? 1 : 0;
    // What remove means to a mod: its observer stops seeing calls.
    hook.Observe(nullptr, nullptr);
    return 1;
}

// bool and int differ as return types, so the ABI observer needs a trampoline.
std::atomic<URK_UnrealProcessEventObserverFn> g_observerFn{nullptr};
std::atomic<void *> g_observerUser{nullptr};

bool ObserverTrampoline(void *, Address object, Address function, void *parms) {
    const URK_UnrealProcessEventObserverFn fn = g_observerFn.load(std::memory_order_acquire);
    if (!fn)
        return true;
    return fn(g_observerUser.load(std::memory_order_acquire), object, function, parms) != 0;
}

void Unreal_ProcessEventObserve(URK_UnrealProcessEventObserverFn observer, void *userData) {
    g_observerUser.store(userData, std::memory_order_release);
    g_observerFn.store(observer, std::memory_order_release);
    ProcessEventHook::Instance().Observe(observer ? &ObserverTrampoline : nullptr, nullptr);
}

std::uint32_t Unreal_GameThreadId() { return ProcessEventHook::Instance().GameThreadId(); }

int Unreal_PostToGameThread(URK_UnrealPostedWorkFn work, void *userData) {
    if (!work)
        return 0;
    return ProcessEventHook::Instance().Post(reinterpret_cast<ProcessEventHook::Work>(work), userData) ? 1 : 0;
}

URK_UnrealApi BuildTable() {
    URK_UnrealApi api{};
    api.version = URK_UNREAL_API_VERSION;
    api.size = sizeof(URK_UnrealApi);

    api.is_available = &Unreal_IsAvailable;
    api.engine_version = &Unreal_EngineVersion;
    api.uses_field_properties = &Unreal_UsesFieldProperties;

    api.find_object = &Unreal_FindObject;
    api.find_object_in_outer = &Unreal_FindObjectInOuter;
    api.class_of = &Unreal_ClassOf;
    api.outer_of = &Unreal_OuterOf;
    api.name_of = &Unreal_NameOf;

    api.is_child_of = &Unreal_IsChildOf;
    api.is_a = &Unreal_IsA;
    api.default_object_of = &Unreal_DefaultObjectOf;
    api.instances_of = &Unreal_InstancesOf;

    api.describe_property = &Unreal_DescribeProperty;
    api.read_integer = &Unreal_ReadInteger;
    api.read_floating = &Unreal_ReadFloating;
    api.read_bool = &Unreal_ReadBool;
    api.read_object = &Unreal_ReadObject;
    api.read_name = &Unreal_ReadName;
    api.read_string = &Unreal_ReadString;
    api.write_integer = &Unreal_WriteInteger;
    api.write_floating = &Unreal_WriteFloating;
    api.write_bool = &Unreal_WriteBool;
    api.write_object = &Unreal_WriteObject;

    api.find_function = &Unreal_FindFunction;
    api.call_frame_create = &Unreal_CallFrameCreate;
    api.call_frame_destroy = &Unreal_CallFrameDestroy;
    api.call_frame_set = &Unreal_CallFrameSet;
    api.call_frame_get = &Unreal_CallFrameGet;
    api.call = &Unreal_Call;

    api.hook_install = &Unreal_HookInstall;
    api.hook_installed = &Unreal_HookInstalled;
    api.hook_remove = &Unreal_HookRemove;
    api.process_event_observe = &Unreal_ProcessEventObserve;
    api.game_thread_id = &Unreal_GameThreadId;
    api.post_to_game_thread = &Unreal_PostToGameThread;

    return api;
}

} // namespace

UnrealEngine &UnrealEngine::Instance() {
    static UnrealEngine engine;
    return engine;
}

bool UnrealEngine::EnsureBootstrapped() {
    if (available_.load(std::memory_order_acquire))
        return true;
    if (ruledOut_.load(std::memory_order_acquire))
        return false;

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t last = lastAttemptMs_.load(std::memory_order_acquire);
    if (last != 0 && now - last < kRetryCooldownMs)
        return false;

    std::lock_guard<std::mutex> lock(bootstrapMutex_);
    // Re-checked under the lock: another thread may have climbed or failed.
    if (available_.load(std::memory_order_relaxed))
        return true;
    if (ruledOut_.load(std::memory_order_relaxed))
        return false;
    const std::uint64_t lastUnderLock = lastAttemptMs_.load(std::memory_order_relaxed);
    if (lastUnderLock != 0 && GetTickCount64() - lastUnderLock < kRetryCooldownMs)
        return false;
    const std::uint64_t attemptStart = GetTickCount64();
    lastAttemptMs_.store(attemptStart, std::memory_order_release);
    ++profile_.attempts;
    std::uint64_t mark = attemptStart;
    const auto lap = [&mark] {
        const std::uint64_t now = GetTickCount64();
        const std::uint64_t elapsed = now - mark;
        mark = now;
        return elapsed;
    };

    // Partial state is never read while available_ is false; every failure
    // restamps the cooldown.
    const auto failed = [this, attemptStart](const char *why) {
        failure_.store(why, std::memory_order_release);
        const std::uint64_t now = GetTickCount64();
        profile_.failedMs += now - attemptStart;
        lastAttemptMs_.store(now, std::memory_order_release);
        return false;
    };

    const UnrealPresence &presence = Presence();
    if (ruledOut_.load(std::memory_order_acquire))
        return false;
    version_ = presence.version;

    std::vector<ScanRegion> dataRegions;
    std::vector<ScanRegion> codeRegions;
    for (const Address module : presence.runtimeModules) {
        const std::vector<ScanRegion> moduleData = ModuleDataRegions(memory_, module);
        const std::vector<ScanRegion> moduleCode = ModuleCodeRegions(memory_, module);
        dataRegions.insert(dataRegions.end(), moduleData.begin(), moduleData.end());
        codeRegions.insert(codeRegions.end(), moduleCode.begin(), moduleCode.end());
    }
    if (dataRegions.empty())
        return failed("the engine image has no data sections");

    if (!anchors_) {
        anchors_.emplace();
        for (const Address module : presence.runtimeModules) {
            GlobalCandidates found = FindGlobalCandidates(memory_, module);
            anchors_->objectArrays.insert(anchors_->objectArrays.end(), found.objectArrays.begin(),
                                          found.objectArrays.end());
            anchors_->namePools.insert(anchors_->namePools.end(), found.namePools.begin(), found.namePools.end());
        }
        functionTable_ = FunctionTable::Read(memory_, presence.runtimeModules);
        profile_.anchoredArrays = anchors_->objectArrays.size();
        profile_.anchoredPools = anchors_->namePools.size();
        profile_.anchorMs = lap();
        const bool anchored = !anchors_->objectArrays.empty() && !anchors_->namePools.empty();
        scanAllowedAtMs_ = anchored ? GetTickCount64() + kScanFallbackMs : 0;
    }

    // Ordinary early on: the object array is built long after the loader lands.
    runtime_ = BootstrapAnchored(memory_, *anchors_);
    profile_.locatedBy = "code anchors";
    if (!runtime_ && GetTickCount64() >= scanAllowedAtMs_) {
        ++profile_.scans;
        runtime_ = BootstrapRuntime(memory_, dataRegions);
        profile_.locatedBy = "data scan";
        const bool anchored = !anchors_->objectArrays.empty() && !anchors_->namePools.empty();
        scanAllowedAtMs_ = GetTickCount64() + (anchored ? kScanFallbackMs : kScanRetryMs);
    }
    profile_.locateMs = lap();
    if (!runtime_)
        return failed("GUObjectArray and FNamePool not found yet");

    finder_ = std::make_unique<ObjectFinder>(ObjectFinder::Build(runtime_->objects, runtime_->names, runtime_->header));
    profile_.indexMs = lap();
    // Holds even when the version resource was stripped.
    if (!UsesFPropertySystem(*finder_)) {
        failure_.store("properties are UObjects, so the engine predates UE4.25", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
        return false;
    }
    structs_ = FindStructOffsets(*finder_);
    fields_ = FindFieldOffsets(*finder_, runtime_->names, structs_);
    if (!fields_.Resolved())
        return failed("the FField layout did not resolve");

    tail_ = FindPropertyTailOffsets(*finder_, structs_, fields_);
    classes_ = FindClassOffsets(*finder_, structs_);
    chain_ = std::make_unique<PropertyChain>(memory_, runtime_->names, structs_, fields_);
    values_ = std::make_unique<PropertyValues>(memory_, runtime_->names, structs_, fields_, tail_);
    types_ = std::make_unique<TypeQueries>(*finder_, structs_, classes_);
    profile_.offsetsMs = lap();
    functions_ = FindFunctionOffsets(*finder_, structs_, fields_, tail_, codeRegions);
    profile_.functionsMs = lap();
    processEvent_ = FindProcessEvent(*finder_, *types_, structs_, functions_, codeRegions, functionTable_)
                       .value_or(ProcessEventLocation{});
    profile_.processEventMs = lap();

    available_.store(true, std::memory_order_release);
    return true;
}

const UnrealPresence &UnrealEngine::Presence() {
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (presence_)
        return *presence_;

    char pathBuffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, sizeof(pathBuffer));
    std::string path(pathBuffer, length < sizeof(pathBuffer) ? length : sizeof(pathBuffer) - 1);
    std::string name = path;
    const std::size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    const ModuleCandidate self{name, path, MainModuleBase()};
    presence_ = DetectUnreal(memory_, std::span<const ModuleCandidate>(&self, 1));
    // Neither answer changes while the process lives.
    if (!presence_->WorthScanning() || presence_->runtimeModules.empty()) {
        failure_.store("not a UBT-built process", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
    } else if (presence_->version.Known() && !presence_->version.UsesFieldProperties()) {
        failure_.store("the version resource names an engine before UE4.25", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
    }
    return *presence_;
}

bool UnrealEngine::RuledOut() const { return ruledOut_.load(std::memory_order_acquire); }

bool UnrealEngine::Available() const { return available_.load(std::memory_order_acquire); }

const URK_UnrealApi *UnrealSdkApi(const HookInstaller &installer) {
    static const URK_UnrealApi table = BuildTable();
    g_installer = installer;
    return &table;
}

bool UnrealSdk_HoldProcessEventHook() {
    std::lock_guard lock(g_hookMutex);
    if (!EnsureHookInstalled())
        return false;
    g_loaderHold = true;
    return true;
}

} // namespace URK::Unreal
