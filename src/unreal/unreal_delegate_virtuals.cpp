#include "unreal_delegate_virtuals.h"
#include "unreal_engine_calls.h"
#include "unreal_object_finder.h"
#include "unreal_owned_values.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace URK::Unreal {
namespace {

// Past FProperty's own virtuals in every version.
constexpr std::int32_t kMaxSlots = 128;
// Get, Set, Add, Remove, Clear: declared in this order from 4.25 to 5.8.
constexpr std::int32_t kAddOffset = 2;
constexpr std::int32_t kRemoveOffset = 3;
constexpr std::int32_t kClearOffset = 4;

using GetFn = const std::uint8_t *(__fastcall *)(const void *self, const void *value);
// FScriptDelegate by value: MSVC passes a pointer to the caller's copy.
using AddFn = void(__fastcall *)(const void *self, void *delegate, void *parent, void *value);
using RemoveFn = void(__fastcall *)(const void *self, const void *delegate, void *parent, void *value);
using ClearFn = void(__fastcall *)(const void *self, void *parent, void *value);

// The first bytes of a function, through one incremental-link `jmp rel32`.
bool BodyStarts(const MemoryReader &reader, Address function, std::initializer_list<std::uint8_t> prefix) {
    std::uint8_t bytes[8]{};
    for (int hop = 0; hop < 2; ++hop) {
        if (!ImageCode(reinterpret_cast<const void *>(function)) || !reader.Read(function, bytes, sizeof(bytes)))
            return false;
        if (bytes[0] != 0xE9)
            return std::equal(prefix.begin(), prefix.end(), bytes);
        std::int32_t displacement = 0;
        std::memcpy(&displacement, bytes + 1, sizeof(displacement));
        function = function + 5 + static_cast<Address>(static_cast<std::int64_t>(displacement));
    }
    return false;
}

// The first member of kind in any of the classes: engine delegates every build has.
std::optional<PropertyInfo> FirstOfKind(const ObjectFinder &finder, const PropertyChain &chain,
                                        const PropertyValues &values, PropertyKind kind,
                                        std::initializer_list<const char *> classes) {
    for (const char *name : classes) {
        const Address owner = finder.Find(name);
        Address field = owner != kNullAddress ? chain.First(owner) : kNullAddress;
        for (int step = 0; field != kNullAddress && step < 0x400; ++step, field = chain.Next(field)) {
            const std::optional<PropertyInfo> info = values.Describe(field);
            if (info && info->kind == kind && info->arrayDim == 1)
                return info;
        }
    }
    return std::nullopt;
}

} // namespace

void **DelegateVirtuals::VtableOf(Address field) const {
    const std::optional<Address> vtable = field != kNullAddress ? finder_->Reader().ReadAs<Address>(field) : std::nullopt;
    if (!vtable || *vtable == kNullAddress)
        return nullptr;
    for (std::int32_t slot = 0; slot <= getSlot_ + kClearOffset; ++slot) {
        const std::optional<Address> entry = finder_->Reader().ReadAs<Address>(*vtable + slot * sizeof(Address));
        if (!entry || !ImageCode(reinterpret_cast<const void *>(*entry)))
            return nullptr;
    }
    return reinterpret_cast<void **>(*vtable);
}

void DelegateVirtuals::Measure() {
    std::lock_guard lock(mutex_);
    if (measured_)
        return;
    measured_ = true;
    const MemoryReader &reader = finder_->Reader();
    const std::optional<PropertyInfo> inlined =
        FirstOfKind(*finder_, *chain_, *values_, PropertyKind::MulticastDelegate,
                    {"AudioComponent", "AnimInstance", "Character", "GameViewportClient"});
    const std::optional<PropertyInfo> sparse =
        FirstOfKind(*finder_, *chain_, *values_, PropertyKind::SparseDelegate, {"Actor", "PrimitiveComponent"});
    delegateSize_ = owned_->DelegateElement().elementSize;
    if (!inlined || !sparse || delegateSize_ <= 0 || delegateSize_ > 32) {
        failure_ = "no engine multicast and sparse delegate properties to measure against";
        return;
    }
    const auto entry = [&](Address field, std::int32_t slot) {
        const std::optional<Address> vtable = reader.ReadAs<Address>(field);
        const std::optional<Address> at =
            vtable ? reader.ReadAs<Address>(*vtable + slot * sizeof(Address)) : std::nullopt;
        return at && ImageCode(reinterpret_cast<const void *>(*at)) ? *at : kNullAddress;
    };
    // An inline list's GetMulticastDelegate returns the value itself: `mov rax, rdx; ret`.
    std::vector<std::int32_t> candidates;
    for (std::int32_t slot = 0; slot + kClearOffset < kMaxSlots; ++slot) {
        const Address get = entry(inlined->field, slot);
        if (get == kNullAddress)
            break;
        if (!BodyStarts(reader, get, {0x48, 0x8B, 0xC2, 0xC3}))
            continue;
        bool overridden = true;
        for (std::int32_t offset = 0; offset <= kClearOffset && overridden; ++offset) {
            const Address mine = entry(inlined->field, slot + offset);
            const Address theirs = entry(sparse->field, slot + offset);
            overridden = mine != kNullAddress && theirs != kNullAddress && mine != theirs;
        }
        if (overridden)
            candidates.push_back(slot);
    }
    if (candidates.size() != 1) {
        failure_ = "no single vtable slot returns an inline delegate list (" + std::to_string(candidates.size()) +
                   " candidates)";
        return;
    }
    const std::int32_t slot = candidates.front();

    // A probe list the engine fills and empties through Add and Remove.
    const Address probeObject = finder_->Find("Default__KismetSystemLibrary");
    std::uint8_t delegate[32]{};
    const std::size_t nameSize = static_cast<std::size_t>(finder_->Names().Layout().size);
    if (probeObject == kNullAddress || !owned_->Engine().MakeWeak(probeObject, delegate) ||
        EngineCalls::kWeakSize + nameSize > static_cast<std::size_t>(delegateSize_) ||
        !reader.Read(probeObject + finder_->Offsets().name, delegate + EngineCalls::kWeakSize, nameSize)) {
        failure_ = "no probe delegate could be made";
        return;
    }
    std::vector<std::uint64_t> storage((static_cast<std::size_t>(inlined->elementSize) + 7) / 8 + 1, 0);
    auto *list = reinterpret_cast<std::uint8_t *>(storage.data());
    const auto *self = reinterpret_cast<const void *>(inlined->field);
    const auto count = [list] {
        std::int32_t num = 0;
        std::memcpy(&num, list + sizeof(Address), sizeof(num));
        return num;
    };
    void **vtable = reinterpret_cast<void **>(*reader.ReadAs<Address>(inlined->field));
    bool proven = reinterpret_cast<GetFn>(vtable[slot])(self, list) == list;
    if (proven) {
        std::uint8_t copy[32];
        std::memcpy(copy, delegate, sizeof(copy));
        reinterpret_cast<AddFn>(vtable[slot + kAddOffset])(self, copy, nullptr, list);
        Address data = kNullAddress;
        std::memcpy(&data, list, sizeof(data));
        std::uint8_t stored[32]{};
        proven = count() == 1 && data != kNullAddress && reader.Read(data, stored, delegateSize_) &&
                 std::memcmp(stored, delegate, static_cast<std::size_t>(delegateSize_)) == 0;
        if (count() > 0)
            reinterpret_cast<RemoveFn>(vtable[slot + kRemoveOffset])(self, delegate, nullptr, list);
        proven = proven && count() == 0;
    }
    PropertyInfo whole = *inlined;
    whole.offset = 0;
    owned_->Destroy(whole, list);
    if (!proven) {
        failure_ = "slot " + std::to_string(slot) + " did not add and remove a probe binding";
        return;
    }
    getSlot_ = slot;
    NoteMemory("sparse delegates bind through the engine's own delegate property virtuals: GetMulticastDelegate slot " +
               std::to_string(slot));
}

bool DelegateVirtuals::Ready() {
    Measure();
    return getSlot_ >= 0;
}

const std::uint8_t *DelegateVirtuals::List(const PropertyInfo &property, const std::uint8_t *value) {
    void **vtable = Ready() ? VtableOf(property.field) : nullptr;
    if (!vtable || !value)
        return nullptr;
    return reinterpret_cast<GetFn>(vtable[getSlot_])(reinterpret_cast<const void *>(property.field), value);
}

bool DelegateVirtuals::Add(const PropertyInfo &property, Address owner, std::uint8_t *value,
                           const std::uint8_t *delegate) {
    void **vtable = Ready() ? VtableOf(property.field) : nullptr;
    if (!vtable || !value || owner == kNullAddress)
        return false;
    std::uint8_t copy[32]{};
    std::memcpy(copy, delegate, static_cast<std::size_t>(delegateSize_));
    reinterpret_cast<AddFn>(vtable[getSlot_ + kAddOffset])(reinterpret_cast<const void *>(property.field), copy,
                                                          reinterpret_cast<void *>(owner), value);
    return true;
}

bool DelegateVirtuals::Remove(const PropertyInfo &property, Address owner, std::uint8_t *value,
                              const std::uint8_t *delegate) {
    void **vtable = Ready() ? VtableOf(property.field) : nullptr;
    if (!vtable || !value || owner == kNullAddress)
        return false;
    reinterpret_cast<RemoveFn>(vtable[getSlot_ + kRemoveOffset])(reinterpret_cast<const void *>(property.field),
                                                                delegate, reinterpret_cast<void *>(owner), value);
    return true;
}

bool DelegateVirtuals::Clear(const PropertyInfo &property, Address owner, std::uint8_t *value) {
    void **vtable = Ready() ? VtableOf(property.field) : nullptr;
    if (!vtable || !value || owner == kNullAddress)
        return false;
    reinterpret_cast<ClearFn>(vtable[getSlot_ + kClearOffset])(reinterpret_cast<const void *>(property.field),
                                                              reinterpret_cast<void *>(owner), value);
    return true;
}

} // namespace URK::Unreal
