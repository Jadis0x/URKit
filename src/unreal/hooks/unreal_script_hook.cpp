#include "unreal/hooks/unreal_script_hook.h"
#include "unreal/detect/unreal_code_anchors.h"
#include "unreal/values/unreal_engine_calls.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace URK::Unreal {
namespace {

// EFunctionFlags::FUNC_Native, the same from 4.25 to 5.8.
constexpr std::uint32_t kFunctionNative = 0x00000400;
// Script functions a game has at the least once its classes load.
constexpr std::int32_t kMinScriptFunctions = 8;
// FFrame's fields sit within its first words (FOutputDevice, then Node...).
constexpr std::int32_t kFrameSearch = 0x60;
// Calls from ProcessEvent that must agree before the offsets are used.
constexpr std::int32_t kAgreement = 16;
// Where a UFunction may keep its bytecode array (UStruct::Script).
constexpr std::int32_t kFunctionFieldsFrom = 0x28;
constexpr std::int32_t kFunctionFieldsTo = 0x100;
// A body's length when .pdata gives none.
constexpr std::size_t kMaxBodyBytes = 0x400;

// ProcessInternal's frame: the same frame in the local call is not a new call.
thread_local const void *g_internalFrame = nullptr;
// An observer's own script calls are not reported back to it.
thread_local bool g_inObserver = false;

const MemoryReader *g_reader = nullptr;

Address Load(const std::uint8_t *at) {
    Address value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// Only until attach returns the trampoline.
template <typename Fn> Fn Original(const std::atomic<Fn> &slot) {
    Fn original = slot.load(std::memory_order_acquire);
    while (!original) {
        YieldProcessor();
        original = slot.load(std::memory_order_acquire);
    }
    return original;
}

std::string Hex(std::int32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%X", static_cast<unsigned>(value));
    return text;
}

} // namespace

ScriptCallHook &ScriptCallHook::Instance() {
    static ScriptCallHook hook;
    return hook;
}

bool ScriptCallHook::Install(const ObjectFinder &finder, const TypeQueries &types, const FunctionOffsets &functions,
                             const FunctionTable &bounds, std::span<const ScanRegion> code,
                             const HookInstaller &installer) {
    if (attempted_)
        return Installed();
    const auto fail = [this](std::string why) {
        failure_ = std::move(why);
        return false;
    };
    if (!installer.Valid() || functions.func == kOffsetNotFound || functions.functionFlags == kOffsetNotFound ||
        functions.parmsSize == kOffsetNotFound || bounds.Empty())
        return fail("the function layout or the hook installer is unavailable");
    const MemoryReader &reader = finder.Reader();
    const Address functionClass = finder.FindInOuter("Function", "/Script/CoreUObject");
    if (functionClass == kNullAddress)
        return fail("UFunction's class was not found");

    // ProcessInternal: the Func every script function shares.
    std::map<Address, std::int32_t> funcs;
    std::int32_t scripts = 0;
    const ObjectArray &objects = finder.Objects();
    for (std::int32_t index = 0, total = objects.Num(); index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress || !types.IsA(object, functionClass))
            continue;
        const std::optional<std::uint32_t> flags = reader.ReadUInt32(object + functions.functionFlags);
        const std::optional<Address> func = reader.ReadAs<Address>(object + functions.func);
        if (!flags || !func || (*flags & kFunctionNative) != 0 || *func == kNullAddress)
            continue;
        ++scripts;
        ++funcs[*func];
    }
    Address internal = kNullAddress;
    std::int32_t shared = 0;
    for (const auto &[func, count] : funcs) {
        if (count > shared) {
            internal = func;
            shared = count;
        }
    }
    if (scripts < kMinScriptFunctions || shared * 10 < scripts * 9 ||
        !ImageCode(reinterpret_cast<const void *>(internal)))
        return fail("no Func is shared by the script functions (" + std::to_string(scripts) + " seen)");
    attempted_ = true;

    // ProcessLocalScriptFunction: ProcessInternal's callee whose address is taken.
    const std::optional<FunctionRange> body = bounds.Containing(internal);
    const Address end = body && bounds.PrimaryBegin(internal) == internal ? body->end : internal + kMaxBodyBytes;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end - internal));
    if (bytes.size() < 5 || !reader.Read(internal, bytes.data(), bytes.size()))
        return fail("ProcessInternal's body is unreadable");
    std::vector<Address> callees;
    for (std::size_t i = 0; i + 5 <= bytes.size(); ++i) {
        if (bytes[i] != 0xE8)
            continue;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, bytes.data() + i + 1, sizeof(displacement));
        const Address target = internal + i + 5 + static_cast<Address>(static_cast<std::int64_t>(displacement));
        if (bounds.PrimaryBegin(target) == target &&
            std::find(callees.begin(), callees.end(), target) == callees.end())
            callees.push_back(target);
    }
    const std::vector<std::size_t> taken = AddressTakenCounts(reader, code, callees);
    Address local = kNullAddress;
    for (std::size_t i = 0; i < callees.size(); ++i) {
        if (taken[i] == 0)
            continue;
        if (local != kNullAddress)
            return fail("more than one of ProcessInternal's callees has its address taken");
        local = callees[i];
    }
    if (local == kNullAddress)
        return fail("ProcessLocalScriptFunction was not found among ProcessInternal's " +
                    std::to_string(callees.size()) + " callees");

    // Both or neither: a hook on one misses the calls the other carries.
    void *internalTrampoline = nullptr;
    void *localTrampoline = nullptr;
    void *internalHandle = installer.attach(installer.context, reinterpret_cast<void *>(internal),
                                            reinterpret_cast<void *>(&InternalDetour), &internalTrampoline);
    if (!internalHandle || !internalTrampoline) {
        if (internalHandle)
            installer.detach(installer.context, internalHandle);
        return fail("ProcessInternal could not be hooked");
    }
    internalOriginal_.store(reinterpret_cast<ScriptFn>(internalTrampoline), std::memory_order_release);
    void *localHandle = installer.attach(installer.context, reinterpret_cast<void *>(local),
                                         reinterpret_cast<void *>(&LocalDetour), &localTrampoline);
    if (!localHandle || !localTrampoline) {
        if (localHandle)
            installer.detach(installer.context, localHandle);
        // Leave the first patch: a call may be inside its trampoline.
        return fail("ProcessLocalScriptFunction could not be hooked");
    }
    localOriginal_.store(reinterpret_cast<ScriptFn>(localTrampoline), std::memory_order_release);
    processInternal_ = internal;
    processLocal_ = local;
    parmsSizeOffset_ = functions.parmsSize;
    g_reader = &reader;
    installed_.store(true, std::memory_order_release);
    return true;
}

void ScriptCallHook::Observe(Observer observer, void *user) {
    observerUser_.store(user, std::memory_order_release);
    observer_.store(observer, std::memory_order_release);
}

// FFrame is Node, Object, Code, Locals (4.25-5.8), matched against ProcessEvent's call.
void ScriptCallHook::Measure(void *context, const std::uint8_t *stack) {
    const ProcessEventHook::Call *call = ProcessEventHook::CurrentCall();
    if (!call || !g_reader)
        return;
    const std::unique_lock lock(measureMutex_, std::try_to_lock);
    if (!lock.owns_lock() || nodeOffset_.load(std::memory_order_acquire) >= 0 || contradicted_)
        return;
    sampled_.fetch_add(1, std::memory_order_relaxed);
    std::int32_t node = -1;
    std::int32_t object = -1;
    for (std::int32_t at = 0; at + 8 <= kFrameSearch; at += 8) {
        const Address value = Load(stack + at);
        if (value == call->function)
            node = node == -1 ? at : -2;
        else if (value == reinterpret_cast<Address>(context))
            object = object == -1 ? at : -2;
    }
    lastNode_.store(node, std::memory_order_relaxed);
    lastObject_.store(object, std::memory_order_relaxed);
    // Not ProcessEvent's own call into script (a nested one), or ambiguous.
    if (node < 0 || object != node + 8 || object + 24 > kFrameSearch)
        return;
    const std::int32_t code = object + 8;
    const std::int32_t locals = object + 16;
    const Address bytecode = Load(stack + code);
    std::int32_t script = -1;
    for (std::int32_t at = kFunctionFieldsFrom; bytecode != kNullAddress && at < kFunctionFieldsTo; at += 8) {
        if (g_reader->ReadAs<Address>(call->function + at) == bytecode) {
            script = at;
            break;
        }
    }
    if (script < 0)
        return;
    const std::optional<std::uint16_t> parmsSize = g_reader->ReadAs<std::uint16_t>(call->function + parmsSizeOffset_);
    if (parmsSize && *parmsSize >= 4 && call->parms) {
        std::vector<std::uint8_t> parms(*parmsSize);
        std::memcpy(parms.data(), call->parms, parms.size());
        if (!std::all_of(parms.begin(), parms.end(), [](std::uint8_t b) { return b == 0; })) {
            std::vector<std::uint8_t> copy(parms.size());
            const Address frameLocals = Load(stack + locals);
            if (frameLocals == kNullAddress || !g_reader->Read(frameLocals, copy.data(), copy.size()) ||
                copy != parms) {
                contradicted_ = true;
                NoteMemory("Blueprint calls cannot be observed: a call's parameters are not where FFrame's Locals "
                           "should hold them");
                return;
            }
            parmsChecked_.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (node != candidateNode_ || script != candidateScript_) {
        candidateNode_ = node;
        candidateScript_ = script;
        agreed_.store(1, std::memory_order_relaxed);
        return;
    }
    if (agreed_.fetch_add(1, std::memory_order_relaxed) + 1 < kAgreement)
        return;
    objectOffset_.store(object, std::memory_order_release);
    localsOffset_.store(locals, std::memory_order_release);
    nodeOffset_.store(node, std::memory_order_release);
    NoteMemory("Blueprint calls are observable: FFrame Node +" + Hex(node) + ", Object +" + Hex(object) +
               ", Code +" + Hex(code) + " (the function's bytecode at +" + Hex(script) + "), Locals +" + Hex(locals) +
               " (parameters seen there in " + std::to_string(parmsChecked_.load()) + " calls)");
}

std::string ScriptCallHook::MeasureState() const {
    return std::to_string(sampled_.load()) + " calls from ProcessEvent sampled, " + std::to_string(agreed_.load()) +
           " agreeing; last sample node " + std::to_string(lastNode_.load()) + ", object " +
           std::to_string(lastObject_.load());
}

void ScriptCallHook::Report(const std::uint8_t *stack, void *result, bool after) {
    const Observer observer = observer_.load(std::memory_order_acquire);
    const std::int32_t node = nodeOffset_.load(std::memory_order_acquire);
    if (!observer || node < 0 || g_inObserver)
        return;
    const Address function = Load(stack + node);
    const Address object = Load(stack + objectOffset_.load(std::memory_order_acquire));
    void *locals = reinterpret_cast<void *>(Load(stack + localsOffset_.load(std::memory_order_acquire)));
    g_inObserver = true;
    observer(observerUser_.load(std::memory_order_acquire), object, function, locals, result, after);
    g_inObserver = false;
}

bool ScriptCallHook::HooksBefore(FunctionHooks::Pending &pending, const std::uint8_t *stack, void *result,
                                 bool internal) {
    const std::int32_t node = nodeOffset_.load(std::memory_order_acquire);
    if (node < 0)
        return true;
    const Address function = Load(stack + node);
    const Address object = Load(stack + objectOffset_.load(std::memory_order_acquire));
    if (internal && ProcessEventHook::ClaimScriptEntry(object, function))
        return true;
    void *locals = reinterpret_cast<void *>(Load(stack + localsOffset_.load(std::memory_order_acquire)));
    return FunctionHooks::Instance().Before(pending, object, function, locals, result);
}

void __fastcall ScriptCallHook::InternalDetour(void *context, void *stack, void *result) {
    ScriptCallHook &hook = Instance();
    hook.internalCalls_.fetch_add(1, std::memory_order_relaxed);
    const auto *frame = static_cast<const std::uint8_t *>(stack);
    if (hook.nodeOffset_.load(std::memory_order_acquire) < 0)
        hook.Measure(context, frame);
    hook.Report(frame, result, false);
    FunctionHooks::Pending pending;
    if (hook.HooksBefore(pending, frame, result, true)) {
        const ScriptFn original = Original(hook.internalOriginal_);
        const void *outer = g_internalFrame;
        g_internalFrame = stack;
        original(context, stack, result);
        g_internalFrame = outer;
    }
    FunctionHooks::Instance().After(pending);
    hook.Report(frame, result, true);
}

void __fastcall ScriptCallHook::LocalDetour(void *context, void *stack, void *result) {
    ScriptCallHook &hook = Instance();
    hook.localCalls_.fetch_add(1, std::memory_order_relaxed);
    const auto *frame = static_cast<const std::uint8_t *>(stack);
    // ProcessInternal's own call: already reported and hooked there.
    const bool reported = g_internalFrame == stack;
    if (reported) {
        g_internalFrame = nullptr;
        Original(hook.localOriginal_)(context, stack, result);
        return;
    }
    hook.Report(frame, result, false);
    FunctionHooks::Pending pending;
    if (hook.HooksBefore(pending, frame, result, false))
        Original(hook.localOriginal_)(context, stack, result);
    FunctionHooks::Instance().After(pending);
    hook.Report(frame, result, true);
}

} // namespace URK::Unreal
