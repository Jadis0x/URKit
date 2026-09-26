#include "unreal/hooks/unreal_native_call_hook.h"

#include "unreal/hooks/unreal_function_hooks.h"
#include "unreal/hooks/unreal_process_event_hook.h"
#include "unreal/values/unreal_owned_values.h"
#include "unreal/values/unreal_property_virtuals.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

namespace URK::Unreal {
namespace {

// EX_Max: 0x100 through 5.0, 0xFF by 5.4 (Abiotic's PDB: GNatives[255]). Only opcodes below it are looked up.
constexpr std::size_t kNativeCount = 255;
constexpr std::uint8_t kExNothing = 0x0B;
constexpr std::uint8_t kExEndFunctionParms = 0x16;
constexpr std::uint32_t kFunctionFlagNet = 0x00000040;
constexpr std::uint64_t kPropertyOutParm = 0x100;
constexpr std::uint64_t kPropertyConstParm = 0x2;
// FFrame: Object, Code, Locals, MostRecentProperty, MostRecentPropertyAddress in a row (4.25 to 5.x).
constexpr std::int32_t kObjectFromCode = -8;
constexpr std::int32_t kRecentAddressFromCode = 0x18;
constexpr std::size_t kStubSize = 32;
constexpr std::size_t kStubPage = 0x10000;

// Their thunks read arguments by hand (wildcards, CustomThunk); evaluating them as declared would be wrong.
constexpr const char *kCustomThunkOwners[] = {"KismetArrayLibrary", "BlueprintSetLibrary", "BlueprintMapLibrary",
                                              "DataTableFunctionLibrary", "BlueprintInstancedStructLibrary"};

bool InCode(std::span<const ScanRegion> code, Address address) {
    return std::any_of(code.begin(), code.end(), [address](const ScanRegion &region) {
        return address >= region.start && address < region.start + region.size;
    });
}

// execEndFunctionParms is "Stack.Code--": dec qword [rdx+Code]; ret (or add ..., -1). Returns Code's offset.
std::int32_t CodeOffsetOf(const MemoryReader &reader, Address function) {
    std::uint8_t bytes[6]{};
    if (!reader.Read(function, bytes, sizeof(bytes)))
        return -1;
    if (bytes[0] == 0x48 && bytes[1] == 0xFF && bytes[2] == 0x4A && bytes[4] == 0xC3)
        return bytes[3];
    if (bytes[0] == 0x48 && bytes[1] == 0x83 && bytes[2] == 0x42 && bytes[4] == 0xFF && bytes[5] == 0xC3)
        return bytes[3];
    return -1;
}

// execNothing has no body.
bool Returns(const MemoryReader &reader, Address function) {
    std::uint8_t bytes[2]{};
    return reader.Read(function, bytes, sizeof(bytes)) &&
           (bytes[0] == 0xC3 || (bytes[0] == 0xF3 && bytes[1] == 0xC3) || bytes[0] == 0xC2);
}

bool Dominated(const Address *table) {
    std::map<Address, int> counts;
    for (std::size_t i = 0; i < kNativeCount; ++i)
        ++counts[table[i]];
    const auto top = std::max_element(counts.begin(), counts.end(),
                                      [](const auto &a, const auto &b) { return a.second < b.second; });
    // Unused opcodes all point at execUndefined.
    return top->second >= 32 && counts.size() >= 64;
}

} // namespace

NativeCallHook &NativeCallHook::Instance() {
    static NativeCallHook hook;
    return hook;
}

bool NativeCallHook::Prepare(const MemoryReader &reader, std::span<const ScanRegion> data,
                             std::span<const ScanRegion> code, Address objectProcessEvent, std::int32_t funcOffset,
                             OwnedValues &owned, PropertyVirtuals &virtuals) {
    const std::lock_guard lock(mutex_);
    if (attempted_)
        return ready_;
    attempted_ = true;
    if (objectProcessEvent == kNullAddress || funcOffset < 0) {
        failure_ = "UObject::ProcessEvent or UFunction::Func is not known";
        return false;
    }
    std::vector<Address> found;
    std::int32_t codeOffset = -1;
    // Windows overlapping by one table: a page .data can't read costs its window, not the section.
    constexpr std::size_t kWindow = 0x2000;
    std::size_t runs = 0, ended = 0, empty = 0, unreadable = 0;
    std::vector<Address> chunk(kWindow + kNativeCount);
    for (const ScanRegion &region : data) {
        if (!region.writable)
            continue;
        const std::size_t slots = static_cast<std::size_t>(region.size / sizeof(Address));
        for (std::size_t first = 0; first + kNativeCount <= slots; first += kWindow) {
            const std::size_t count = std::min(kWindow + kNativeCount - 1, slots - first);
            if (!reader.Read(region.start + first * sizeof(Address), chunk.data(), count * sizeof(Address))) {
                ++unreadable;
                continue;
            }
            std::size_t run = 0;
            for (std::size_t i = 0; i < count; ++i) {
                run = InCode(code, chunk[i]) ? run + 1 : 0;
                const std::size_t start = i + 1 - kNativeCount;
                if (run < kNativeCount || start >= kWindow)
                    continue;
                const Address *table = &chunk[start];
                ++runs;
                const std::int32_t offset = CodeOffsetOf(reader, table[kExEndFunctionParms]);
                if (offset < 0x18 || offset > 0x40)
                    continue;
                ++ended;
                if (!Returns(reader, table[kExNothing]))
                    continue;
                ++empty;
                if (!Dominated(table))
                    continue;
                found.push_back(region.start + (first + start) * sizeof(Address));
                codeOffset = offset;
            }
        }
    }
    if (found.size() != 1) {
        failure_ = (found.empty() ? "GNatives was not found" : "more than one table looks like GNatives") +
                   std::string(" (") + std::to_string(runs) + " runs of " + std::to_string(kNativeCount) +
                   " code pointers, " + std::to_string(ended) + " with execEndFunctionParms, " + std::to_string(empty) + " with execNothing, " +
                   std::to_string(unreadable) + " unreadable windows)";
        return false;
    }
    natives_ = reinterpret_cast<const NativeFn *>(found.front());
    codeOffset_ = codeOffset;
    funcOffset_ = funcOffset;
    processEvent_ = objectProcessEvent;
    owned_ = &owned;
    virtuals_ = &virtuals;
    ready_ = true;
    return true;
}

std::string NativeCallHook::Refusal(const FunctionInfo &info, const std::string &ownerName) {
    if (!info.Native())
        return "not native";
    if (info.flags & kFunctionFlagNet)
        return "a network function";
    for (const char *owner : kCustomThunkOwners) {
        if (ownerName == owner)
            return "its thunk reads its own arguments";
    }
    for (const FunctionParameter &parameter : info.parameters) {
        if (!parameter.info.Resolved() || parameter.info.kind == PropertyKind::Unknown)
            return "parameter " + parameter.name + " has no known type";
    }
    return {};
}

std::uint8_t *NativeCallHook::MakeStub(Entry *entry) {
    if (!stubPage_ || stubsUsed_ + kStubSize > kStubPage) {
        stubPage_ = static_cast<std::uint8_t *>(
            VirtualAlloc(nullptr, kStubPage, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        stubsUsed_ = 0;
        if (!stubPage_)
            return nullptr;
    }
    std::uint8_t *stub = stubPage_ + stubsUsed_;
    stubsUsed_ += kStubSize;
    // mov r9, entry; mov rax, Thunk; jmp rax: the native's three arguments stay, the entry is the fourth.
    const auto thunk = reinterpret_cast<std::uintptr_t>(&NativeCallHook::Thunk);
    const auto self = reinterpret_cast<std::uintptr_t>(entry);
    std::uint8_t *at = stub;
    *at++ = 0x49;
    *at++ = 0xB9;
    std::memcpy(at, &self, sizeof(self));
    at += sizeof(self);
    *at++ = 0x48;
    *at++ = 0xB8;
    std::memcpy(at, &thunk, sizeof(thunk));
    at += sizeof(thunk);
    *at++ = 0xFF;
    *at++ = 0xE0;
    FlushInstructionCache(GetCurrentProcess(), stub, kStubSize);
    return stub;
}

bool NativeCallHook::Attach(std::shared_ptr<const FunctionInfo> info) {
    const std::lock_guard lock(mutex_);
    if (!ready_ || !info)
        return false;
    const Address function = info->function;
    auto &slot = *reinterpret_cast<std::atomic<void *> *>(function + static_cast<Address>(funcOffset_));
    std::unique_ptr<Entry> &entry = entries_[function];
    if (!entry) {
        entry = std::make_unique<Entry>();
        entry->stub = MakeStub(entry.get());
        if (!entry->stub) {
            entries_.erase(function);
            return false;
        }
    }
    if (entry->active.load(std::memory_order_acquire))
        return true;
    entry->info = std::move(info);
    entry->original = slot.load(std::memory_order_acquire);
    entry->active.store(true, std::memory_order_release);
    slot.store(entry->stub, std::memory_order_release);
    return true;
}

void NativeCallHook::Detach(Address function) {
    const std::lock_guard lock(mutex_);
    const auto found = entries_.find(function);
    if (found == entries_.end() || !found->second->active.load(std::memory_order_acquire))
        return;
    auto &slot = *reinterpret_cast<std::atomic<void *> *>(function + static_cast<Address>(funcOffset_));
    // Calls already in the stub still find their original through the entry, which stays.
    slot.store(found->second->original, std::memory_order_release);
    found->second->active.store(false, std::memory_order_release);
}

void NativeCallHook::Step(void *object, std::uint8_t *stack, void *result) const {
    auto &code = *reinterpret_cast<std::uint8_t **>(stack + codeOffset_);
    const std::uint8_t opcode = *code++;
    natives_[opcode](object, stack, result);
}

void __fastcall NativeCallHook::Thunk(void *context, std::uint8_t *stack, void *result, Entry *entry) {
    const auto original = reinterpret_cast<NativeFn>(entry->original);
    NativeCallHook &self = Instance();
    // No bytecode: ProcessEvent's own frame (ours included), where the hook already ran.
    const std::uint8_t *code = *reinterpret_cast<std::uint8_t **>(stack + self.codeOffset_);
    if (!code || !entry->active.load(std::memory_order_acquire) || FunctionHooks::QuietHere() ||
        ProcessEventHook::Instance().GameThreadId() != GetCurrentThreadId() || !self.virtuals_->ValueOpsReady()) {
        original(context, stack, result);
        return;
    }
    self.Route(context, stack, result, *entry);
}

void NativeCallHook::Route(void *context, std::uint8_t *stack, void *result, Entry &entry) {
    const FunctionInfo &info = *entry.info;
    auto &code = *reinterpret_cast<std::uint8_t **>(stack + codeOffset_);
    void *object = *reinterpret_cast<void **>(stack + codeOffset_ + kObjectFromCode);
    auto &recent = *reinterpret_cast<std::uint8_t **>(stack + codeOffset_ + kRecentAddressFromCode);
    std::uint8_t *const start = code;

    const std::size_t size = static_cast<std::size_t>(std::max<std::int32_t>(info.parmsSize, 1));
    auto *parms = static_cast<std::uint8_t *>(_aligned_malloc(size, 16));
    std::memset(parms, 0, size);
    const auto each = [&](const FunctionParameter &parameter, auto &&visit) {
        for (std::int32_t i = 0; i < parameter.info.arrayDim; ++i)
            visit(parms + parameter.info.offset + static_cast<std::size_t>(i) * parameter.info.elementSize, i);
    };
    for (const FunctionParameter &parameter : info.parameters)
        each(parameter, [&](std::uint8_t *value, std::int32_t) { owned_->Initialize(parameter.info, value); });

    // As the VM would for its own call: each argument in order, references kept to write back.
    struct Out {
        const FunctionParameter *parameter;
        std::uint8_t *home;
    };
    std::vector<Out> outs;
    bool matched = true;
    for (const FunctionParameter &parameter : info.parameters) {
        if (parameter.returned)
            continue;
        if (*code == kExEndFunctionParms) {
            matched = false;
            break;
        }
        const std::uint64_t flags = parameter.info.propertyFlags;
        const bool reference = (flags & kPropertyOutParm) && !(flags & kPropertyConstParm);
        recent = nullptr;
        Step(object, stack, parms + parameter.info.offset);
        if (reference)
            outs.push_back({&parameter, recent});
    }
    matched = matched && *code == kExEndFunctionParms;
    const auto release = [&] {
        for (const FunctionParameter &parameter : info.parameters)
            each(parameter, [&](std::uint8_t *value, std::int32_t) { owned_->Destroy(parameter.info, value); });
        _aligned_free(parms);
    };
    if (!matched) {
        // The bytecode disagrees with reflection: rewind and let the native read it, from now on unrouted.
        code = start;
        release();
        entry.active.store(false, std::memory_order_release);
        reinterpret_cast<NativeFn>(entry.original)(context, stack, result);
        return;
    }
    ++code;

    using ProcessEventFn = void(__fastcall *)(void *object, void *function, void *parms);
    reinterpret_cast<ProcessEventFn>(processEvent_)(context, reinterpret_cast<void *>(info.function), parms);

    for (const Out &out : outs) {
        if (!out.home)
            continue;
        each(*out.parameter, [&](std::uint8_t *value, std::int32_t i) {
            virtuals_->Copy(out.parameter->info, out.home + static_cast<std::size_t>(i) * out.parameter->info.elementSize,
                            value);
        });
    }
    if (const FunctionParameter *returned = info.Returned(); returned && result)
        virtuals_->Copy(returned->info, static_cast<std::uint8_t *>(result), parms + returned->info.offset);
    release();
}

} // namespace URK::Unreal
