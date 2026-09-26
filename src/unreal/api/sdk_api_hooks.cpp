// The ProcessEvent hook, observers and mod function hooks.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

// Loader and mods share one hook; it's removed only when neither holds it.
std::mutex g_hookMutex;

bool g_loaderHold = false;

static bool g_modHold = false;

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
static std::atomic<URK_UnrealProcessEventObserverFn> g_observerFn{nullptr};

static std::atomic<void *> g_observerUser{nullptr};

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

// The Blueprint call observer: bool and int differ here too.
static std::atomic<URK_UnrealScriptCallObserverFn> g_scriptObserverFn{nullptr};

static std::atomic<void *> g_scriptObserverUser{nullptr};

static std::mutex g_scriptHookMutex;

void ScriptObserverTrampoline(void *, Address object, Address function, void *locals, void *result, bool after) {
    if (const URK_UnrealScriptCallObserverFn fn = g_scriptObserverFn.load(std::memory_order_acquire))
        fn(g_scriptObserverUser.load(std::memory_order_acquire), object, function, locals, result, after ? 1 : 0);
}

// Blueprint-to-Blueprint calls reach neither observers nor hooks without it.
bool EnsureScriptHook() {
    UnrealEngine &engine = UnrealEngine::Instance();
    ScriptCallHook &hook = ScriptCallHook::Instance();
    if (hook.Installed())
        return true;
    std::lock_guard lock(g_scriptHookMutex);
    if (hook.Installed())
        return true;
    if (!engine.EnsureBootstrapped() || !g_installer.Valid())
        return false;
    std::vector<ScanRegion> code;
    for (const Address module : engine.Presence().runtimeModules) {
        const std::vector<ScanRegion> regions = ModuleCodeRegions(engine.Reader(), module);
        code.insert(code.end(), regions.begin(), regions.end());
    }
    if (!hook.Install(engine.Finder(), engine.Types(), engine.Functions(), engine.Bounds(), code, g_installer)) {
        Report(kNullAddress, "Blueprint calls cannot be observed: " + hook.Failure());
        return false;
    }
    char where[128];
    std::snprintf(where, sizeof(where), "Blueprint call hooks at ProcessInternal %p and ProcessLocalScriptFunction %p",
                  reinterpret_cast<void *>(hook.ProcessInternal()),
                  reinterpret_cast<void *>(hook.ProcessLocalScriptFunction()));
    Report(kNullAddress, where);
    return true;
}

int Unreal_ScriptCallObserve(URK_UnrealScriptCallObserverFn observer, void *userData) {
    ScriptCallHook &hook = ScriptCallHook::Instance();
    if (observer && !EnsureScriptHook())
        return 0;
    if (!observer && hook.Installed())
        Report(kNullAddress, "Blueprint call observer cleared: " + std::to_string(hook.InternalCalls()) +
                                 " calls through ProcessInternal, " + std::to_string(hook.LocalCalls()) +
                                 " through ProcessLocalScriptFunction, frame layout " +
                                 (hook.NodeOffset() >= 0 ? "measured" : "not measured: " + hook.MeasureState()));
    g_scriptObserverUser.store(userData, std::memory_order_release);
    g_scriptObserverFn.store(observer, std::memory_order_release);
    hook.Observe(observer ? &ScriptObserverTrampoline : nullptr, nullptr);
    return hook.Installed() ? 1 : 0;
}

static std::atomic<URK_UnrealObjectLifeObserverFn> g_lifeObserverFn{nullptr};

static std::atomic<void *> g_lifeObserverUser{nullptr};

void LifeObserverTrampoline(void *, Address object, bool created) {
    if (const URK_UnrealObjectLifeObserverFn fn = g_lifeObserverFn.load(std::memory_order_acquire))
        fn(g_lifeObserverUser.load(std::memory_order_acquire), object, created ? 1 : 0);
}

int Unreal_ObjectLifeObserve(URK_UnrealObjectLifeObserverFn observer, void *userData) {
    UnrealEngine &engine = UnrealEngine::Instance();
    ObjectLifeHook &hook = ObjectLifeHook::Instance();
    if (observer && !hook.Installed()) {
        std::lock_guard lock(g_scriptHookMutex);
        if (!engine.EnsureBootstrapped() || !g_installer.Valid())
            return 0;
        if (!hook.Install(engine.Reader(), engine.Presence().runtimeModules, engine.Bounds(), engine.Finder().Objects(),
                          engine.Finder().Offsets().index, g_installer)) {
            Report(kNullAddress, "objects cannot be followed: " + hook.Failure());
            return 0;
        }
        char where[128];
        std::snprintf(where, sizeof(where), "object life hooks at AllocateUObjectIndex %p and FreeUObjectIndex %p",
                      reinterpret_cast<void *>(hook.Allocate()), reinterpret_cast<void *>(hook.Free()));
        Report(kNullAddress, where);
    }
    g_lifeObserverUser.store(userData, std::memory_order_release);
    g_lifeObserverFn.store(observer, std::memory_order_release);
    hook.Observe(observer ? &LifeObserverTrampoline : nullptr, nullptr);
    return hook.Installed() ? 1 : 0;
}

// Name and ParmsSize: a function freed and another made at its address differs.
std::uint64_t FunctionStamp(const UnrealEngine &engine, Address function) {
    std::uint32_t name = 0;
    std::uint16_t parmsSize = 0;
    std::memcpy(&name, reinterpret_cast<const void *>(function + engine.Finder().Offsets().name), sizeof(name));
    std::memcpy(&parmsSize, reinterpret_cast<const void *>(function + engine.Functions().parmsSize), sizeof(parmsSize));
    return static_cast<std::uint64_t>(name) << 16 | parmsSize;
}

std::shared_ptr<const FunctionInfo> InfoFor(ModFunctionHook &hook, Address called) {
    if (called == hook.function)
        return hook.info;
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::uint64_t stamp = FunctionStamp(engine, called);
    const std::lock_guard lock(hook.overridesMutex);
    ModFunctionHook::Override &known = hook.overrides[called];
    if (!known.info || known.stamp != stamp) {
        std::optional<FunctionInfo> info = DescribeFunction(engine.Chain(), engine.Values(), engine.Functions(), called);
        if (!info)
            return nullptr;
        known = {stamp, std::make_shared<const FunctionInfo>(std::move(*info))};
    }
    return known.info;
}

bool ModHookCallback(void *user, const HookedCall &call) {
    auto *hook = static_cast<ModFunctionHook *>(user);
    const URK_UnrealFunctionHookFn fn = call.after ? hook->after : hook->before;
    if (!fn)
        return true;
    std::shared_ptr<const FunctionInfo> info = InfoFor(*hook, call.function);
    if (!info)
        return true;
    LoaderFrame view(std::move(info), call.parms, call.result);
    const URK_UnrealHookedCall raw{call.object, call.function, reinterpret_cast<URK_UnrealCallFrame *>(&view),
                                   call.after ? 1 : 0, call.skipped ? 1 : 0};
    return fn(hook->user, &raw) != 0;
}

std::mutex g_modHooksMutex;

std::unordered_map<std::uint64_t, std::unique_ptr<ModFunctionHook>> g_modHooks;

std::uint64_t Unreal_FunctionHookAdd(URK_UnrealObject function, URK_UnrealFunctionHookFn before,
                                     URK_UnrealFunctionHookFn after, void *userData) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if ((!before && !after) || !Live(engine, function) ||
        !ObjectIs(engine.Finder(), engine.Structs(), function, kCastFlagFunction))
        return 0;
    if (!ProcessEventHook::Instance().Installed()) {
        Report(function, "hook refused: ProcessEvent is not hooked");
        return 0;
    }
    std::optional<FunctionInfo> info = DescribeFunction(engine.Chain(), engine.Values(), engine.Functions(), function);
    if (!info)
        return 0;
    // Script functions also run straight from other Blueprints.
    if (!info->Native() && !EnsureScriptHook())
        Report(function, "hooked, but only its calls through ProcessEvent: Blueprint-to-Blueprint calls are not seen");
    auto hook = std::make_unique<ModFunctionHook>();
    hook->before = before;
    hook->after = after;
    hook->user = userData;
    hook->function = function;
    hook->info = std::make_shared<const FunctionInfo>(std::move(*info));
    FunctionHooks::Instance().SetSuperOffset(engine.Structs().superStruct);
    const std::lock_guard lock(g_modHooksMutex);
    const std::uint64_t id = FunctionHooks::Instance().Add(function, before ? &ModHookCallback : nullptr,
                                                           after ? &ModHookCallback : nullptr, hook.get());
    if (id != 0)
        g_modHooks.emplace(id, std::move(hook));
    return id;
}

int Unreal_FunctionHookRemove(std::uint64_t id) {
    std::unique_ptr<ModFunctionHook> hook;
    {
        const std::lock_guard lock(g_modHooksMutex);
        const auto found = g_modHooks.find(id);
        if (found == g_modHooks.end())
            return 0;
        hook = std::move(found->second);
        g_modHooks.erase(found);
    }
    if (!FunctionHooks::Instance().Remove(id)) {
        // Still running elsewhere: its record must outlive that call.
        Report(kNullAddress, "a function hook's callback did not finish in time; the mod must stay loaded");
        hook.release();
        return 0;
    }
    return 1;
}

std::uint32_t Unreal_GameThreadId() { return ProcessEventHook::Instance().GameThreadId(); }

int Unreal_PostToGameThread(URK_UnrealPostedWorkFn work, void *userData) {
    if (!work)
        return 0;
    return ProcessEventHook::Instance().Post(reinterpret_cast<ProcessEventHook::Work>(work), userData) ? 1 : 0;
}

} // namespace URK::Unreal::SdkApi
