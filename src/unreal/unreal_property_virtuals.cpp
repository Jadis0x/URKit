#include "unreal_property_virtuals.h"
#include "unreal_engine_calls.h"
#include "unreal_object_finder.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace URK::Unreal {
namespace {

// CPF_HasGetValueTypeHash, the same from 4.25 to 5.8.
constexpr std::uint64_t kHasGetValueTypeHash = 0x0008000000000000ull;
// Far past FProperty's own virtuals in every version (the hash is 12-33).
constexpr std::int32_t kMaxSlots = 96;
// Enough of a function to see a small body whole.
constexpr std::size_t kBodyBytes = 32;

using HashFn = std::uint32_t(__fastcall *)(const void *self, const void *src);
using IdenticalFn = bool(__fastcall *)(const void *self, const void *a, const void *b, std::uint32_t flags);
using InitializeFn = void(__fastcall *)(const void *self, void *dest);
using CopyFn = void(__fastcall *)(const void *self, void *dest, const void *src, std::int32_t count);
// InitializeValueInternal comes this many slots past DestroyValueInternal at most
// (2 in 4.25-5.4, 3 from 5.5 on).
constexpr std::int32_t kInitializeWindow = 6;

struct Sample {
    Address field = kNullAddress;
    void **vtable = nullptr;
};

// The function's first bytes, through one `jmp rel32` (an incremental-link thunk).
std::optional<std::array<std::uint8_t, kBodyBytes>> Body(const MemoryReader &reader, const void *function) {
    std::array<std::uint8_t, kBodyBytes> bytes{};
    auto at = reinterpret_cast<Address>(function);
    for (int hop = 0; hop < 2; ++hop) {
        if (!ImageCode(reinterpret_cast<const void *>(at)) || !reader.Read(at, bytes.data(), bytes.size()))
            return std::nullopt;
        if (bytes[0] != 0xE9)
            return bytes;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
        at = at + 5 + static_cast<Address>(static_cast<std::int64_t>(displacement));
    }
    return std::nullopt;
}

bool StartsWith(const std::array<std::uint8_t, kBodyBytes> &body, std::initializer_list<std::uint8_t> prefix) {
    return std::equal(prefix.begin(), prefix.end(), body.begin());
}

// A small comparison: `sete al` before the first `ret`, and no call on the way.
bool ComparesAndReturns(const std::array<std::uint8_t, kBodyBytes> &body) {
    bool sete = false;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (body[i] == 0xE8 || body[i] == 0xCC)
            return false;
        if (body[i] == 0xC3)
            return sete;
        if (i + 2 < body.size() && body[i] == 0x0F && body[i + 1] == 0x94 && body[i + 2] == 0xC0)
            sete = true;
    }
    return false;
}

struct ValueOps {
    std::int32_t initialize = -1;
    std::int32_t copy = -1;
    std::string failure;
};

// InitializeValueInternal and CopyValuesInternal, around the hash slot.
ValueOps MeasureValueOps(const Sample &integer, const Sample &byte, std::int32_t hashSlot,
                         const std::vector<std::array<std::uint8_t, kBodyBytes>> &ints,
                         const std::vector<std::array<std::uint8_t, kBodyBytes>> &bytes,
                         const std::vector<Address> &intFunctions, const std::vector<Address> &byteFunctions) {
    ValueOps ops;
    const auto slots = static_cast<std::int32_t>(ints.size());
    // ClearValueInternal: `*(T*)Data = T()`.
    std::vector<std::int32_t> clears;
    for (std::int32_t slot = hashSlot + 1; slot < slots; ++slot) {
        const auto s = static_cast<std::size_t>(slot);
        if (StartsWith(ints[s], {0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0xC3}) &&
            StartsWith(bytes[s], {0xC6, 0x02, 0x00, 0xC3}))
            clears.push_back(slot);
    }
    if (clears.size() != 1) {
        ops.failure =
            "no single vtable slot clears an int32 and a byte (" + std::to_string(clears.size()) + " candidates)";
        return ops;
    }
    // DestroyValueInternal: one empty function shared by every number.
    const std::int32_t destroy = clears.front() + 1;
    const auto d = static_cast<std::size_t>(destroy);
    if (destroy >= slots || intFunctions[d] != byteFunctions[d] ||
        !(ints[d][0] == 0xC3 || StartsWith(ints[d], {0xC2, 0x00, 0x00}))) {
        ops.failure = "the slot after ClearValue is not an empty destructor for numbers";
        return ops;
    }
    // InitializeValueInternal: zeroes its ArrayDim values; a probe proves it.
    std::vector<std::int32_t> initialize;
    for (std::int32_t slot = destroy + 1; slot < std::min(slots, destroy + 1 + kInitializeWindow); ++slot) {
        const auto s = static_cast<std::size_t>(slot);
        if (intFunctions[s] == byteFunctions[s] ||
            std::find(ints[s].begin(), ints[s].begin() + 16, 0xE8) != ints[s].begin() + 16)
            continue;
        std::uint32_t wide = 0xDEADBEEFu;
        std::uint32_t narrow = 0xFFFFFFFFu;
        reinterpret_cast<InitializeFn>(integer.vtable[slot])(reinterpret_cast<const void *>(integer.field), &wide);
        reinterpret_cast<InitializeFn>(byte.vtable[slot])(reinterpret_cast<const void *>(byte.field), &narrow);
        if (wide == 0 && narrow == 0xFFFFFF00u)
            initialize.push_back(slot);
    }
    if (initialize.size() != 1) {
        ops.failure = "no single vtable slot initializes an int32 and a byte (" + std::to_string(initialize.size()) +
                      " candidates)";
        return ops;
    }
    // CopyValuesInternal: just before the hash in every version; probes prove it.
    const std::int32_t copy = hashSlot - 1;
    if (copy < 0 || intFunctions[static_cast<std::size_t>(copy)] == byteFunctions[static_cast<std::size_t>(copy)]) {
        ops.failure = "the slot before the hash does not copy per type";
        return ops;
    }
    std::uint32_t wideFrom[2] = {11, 22};
    std::uint32_t wideTo[3] = {0, 0, 33};
    std::uint8_t narrowFrom[4] = {7, 8, 9, 10};
    std::uint8_t narrowTo[4] = {0, 0, 0, 0};
    reinterpret_cast<CopyFn>(integer.vtable[copy])(reinterpret_cast<const void *>(integer.field), wideTo, wideFrom, 2);
    reinterpret_cast<CopyFn>(byte.vtable[copy])(reinterpret_cast<const void *>(byte.field), narrowTo, narrowFrom, 2);
    if (wideTo[0] != 11 || wideTo[1] != 22 || wideTo[2] != 33 || narrowTo[0] != 7 || narrowTo[1] != 8 ||
        narrowTo[2] != 0) {
        ops.failure = "the slot before the hash did not copy its probes";
        return ops;
    }
    ops.initialize = initialize.front();
    ops.copy = copy;
    return ops;
}

} // namespace

void **PropertyVirtuals::VtableOf(Address field) {
    if (field == kNullAddress)
        return nullptr;
    const std::optional<Address> vtable = finder_->Reader().ReadAs<Address>(field);
    if (!vtable || *vtable == kNullAddress)
        return nullptr;
    {
        std::lock_guard lock(mutex_);
        if (vtables_.count(*vtable))
            return reinterpret_cast<void **>(*vtable);
    }
    // A vtable lives in the image's read-only data; its entries are image code.
    auto **table = reinterpret_cast<void **>(*vtable);
    const std::int32_t last = std::max({hashSlot_, identicalSlot_, initializeSlot_, copySlot_});
    for (std::int32_t slot = 0; slot <= last; ++slot) {
        const std::optional<Address> entry = finder_->Reader().ReadAs<Address>(*vtable + slot * sizeof(Address));
        if (!entry || !ImageCode(reinterpret_cast<const void *>(*entry)))
            return nullptr;
    }
    std::lock_guard lock(mutex_);
    vtables_.insert(*vtable);
    return table;
}

void PropertyVirtuals::Measure() {
    std::string note;
    {
        std::lock_guard lock(mutex_);
        if (measured_)
            return;
        MeasureLocked();
        note = hashSlot_ < 0 ? "no engine key hash: " + failure_
                             : "keys hash and compare through the property's own virtuals: GetValueTypeHash slot " +
                                   std::to_string(hashSlot_) +
                                   (identicalSlot_ >= 0 ? ", Identical slot " + std::to_string(identicalSlot_)
                                                        : ", Identical not found (" + failure_ + ")") +
                                   (initializeSlot_ >= 0 ? ", InitializeValue slot " + std::to_string(initializeSlot_) +
                                                               ", CopyValues slot " + std::to_string(copySlot_)
                                                         : ", no value construction (" + valueFailure_ + ")");
    }
    NoteMemory(note);
}

void PropertyVirtuals::MeasureLocked() {
    measured_ = true;
    const MemoryReader &reader = finder_->Reader();
    // Parameters of functions every build has: the property objects the engine made.
    const auto sample = [&](const char *function, PropertyKind kind) -> std::optional<Sample> {
        const Address owner = finder_->FindInOuter(function, "KismetMathLibrary");
        const Address field = owner != kNullAddress ? chain_->FindMember(owner, "A") : kNullAddress;
        const std::optional<PropertyInfo> info = values_->Describe(field);
        const std::optional<Address> vtable = reader.ReadAs<Address>(field);
        if (!info || info->kind != kind || !vtable || *vtable == kNullAddress)
            return std::nullopt;
        return Sample{field, reinterpret_cast<void **>(*vtable)};
    };
    const std::optional<Sample> integer = sample("Add_IntInt", PropertyKind::Int32);
    const std::optional<Sample> byte = sample("Add_ByteByte", PropertyKind::Byte);
    if (!integer || !byte) {
        failure_ = "the int32 and byte parameters of KismetMathLibrary::Add_IntInt/Add_ByteByte did not resolve";
        return;
    }

    std::vector<std::array<std::uint8_t, kBodyBytes>> ints;
    std::vector<std::array<std::uint8_t, kBodyBytes>> bytes;
    std::vector<Address> intFunctions;
    std::vector<Address> byteFunctions;
    for (std::int32_t slot = 0; slot < kMaxSlots; ++slot) {
        const std::optional<Address> a = reader.ReadAs<Address>(reinterpret_cast<Address>(integer->vtable + slot));
        const std::optional<Address> b = reader.ReadAs<Address>(reinterpret_cast<Address>(byte->vtable + slot));
        const auto intBody = a ? Body(reader, reinterpret_cast<const void *>(*a)) : std::nullopt;
        const auto byteBody = b ? Body(reader, reinterpret_cast<const void *>(*b)) : std::nullopt;
        // The table ends where its entries stop being code.
        if (!intBody || !byteBody)
            break;
        ints.push_back(*intBody);
        bytes.push_back(*byteBody);
        intFunctions.push_back(*a);
        byteFunctions.push_back(*b);
    }

    // GetValueTypeHashInternal: GetTypeHash(int32) and GetTypeHash(uint8).
    std::vector<std::int32_t> hashes;
    for (std::size_t slot = 0; slot < ints.size(); ++slot) {
        if (StartsWith(ints[slot], {0x8B, 0x02, 0xC3}) && StartsWith(bytes[slot], {0x0F, 0xB6, 0x02, 0xC3}))
            hashes.push_back(static_cast<std::int32_t>(slot));
    }
    if (hashes.size() != 1) {
        failure_ = "no single vtable slot hashes an int32 and a byte as the engine's GetTypeHash does (" +
                   std::to_string(hashes.size()) + " candidates in " + std::to_string(ints.size()) + " slots)";
        return;
    }
    const auto hashAt = [&](const Sample &s, std::uint32_t value) {
        return reinterpret_cast<HashFn>(s.vtable[hashes.front()])(reinterpret_cast<const void *>(s.field), &value);
    };
    if (hashAt(*integer, 0x12345678u) != 0x12345678u || hashAt(*byte, 0xABu) != 0xABu) {
        failure_ = "the hash slot " + std::to_string(hashes.front()) + " did not hash a probe value to itself";
        return;
    }
    hashSlot_ = hashes.front();

    // Identical: before the hash in every version. Only small comparing bodies
    // are called, and a slot counts only if it answers every probe right.
    std::vector<std::int32_t> identical;
    for (std::int32_t slot = 0; slot < hashSlot_; ++slot) {
        const auto s = static_cast<std::size_t>(slot);
        if (intFunctions[s] == byteFunctions[s] || !ComparesAndReturns(ints[s]) || !ComparesAndReturns(bytes[s]))
            continue;
        const auto same = [&](const Sample &sampled, std::uint32_t a, const std::uint32_t *b) {
            return reinterpret_cast<IdenticalFn>(sampled.vtable[slot])(reinterpret_cast<const void *>(sampled.field),
                                                                       &a, b, 0);
        };
        const std::uint32_t five = 5, six = 6, seven = 7, eight = 8;
        if (same(*integer, 5, &five) && !same(*integer, 5, &six) && same(*integer, 0, nullptr) &&
            !same(*integer, 3, nullptr) && same(*byte, 7, &seven) && !same(*byte, 7, &eight))
            identical.push_back(slot);
    }
    if (identical.size() == 1)
        identicalSlot_ = identical.front();
    else
        failure_ =
            "no single vtable slot compares as Identical does (" + std::to_string(identical.size()) + " candidates)";
    const ValueOps ops = MeasureValueOps(*integer, *byte, hashSlot_, ints, bytes, intFunctions, byteFunctions);
    initializeSlot_ = ops.initialize;
    copySlot_ = ops.copy;
    valueFailure_ = ops.failure;
}

bool PropertyVirtuals::HashReady() {
    Measure();
    return hashSlot_ >= 0;
}

bool PropertyVirtuals::IdenticalReady() {
    Measure();
    return identicalSlot_ >= 0;
}

std::string PropertyVirtuals::Failure() {
    Measure();
    std::lock_guard lock(mutex_);
    return failure_;
}

std::int32_t PropertyVirtuals::HashSlot() {
    Measure();
    return hashSlot_;
}

std::int32_t PropertyVirtuals::IdenticalSlot() {
    Measure();
    return identicalSlot_;
}

bool PropertyVirtuals::ValueOpsReady() {
    Measure();
    return initializeSlot_ >= 0 && copySlot_ >= 0;
}

bool PropertyVirtuals::Initialize(const PropertyInfo &property, std::uint8_t *value) {
    if (!value || property.arrayDim != 1 || !ValueOpsReady())
        return false;
    void **vtable = VtableOf(property.field);
    if (!vtable)
        return false;
    reinterpret_cast<InitializeFn>(vtable[initializeSlot_])(reinterpret_cast<const void *>(property.field), value);
    return true;
}

bool PropertyVirtuals::Copy(const PropertyInfo &property, std::uint8_t *dest, const std::uint8_t *src) {
    if (!dest || !src || !ValueOpsReady())
        return false;
    void **vtable = VtableOf(property.field);
    if (!vtable)
        return false;
    reinterpret_cast<CopyFn>(vtable[copySlot_])(reinterpret_cast<const void *>(property.field), dest, src, 1);
    return true;
}

std::optional<std::uint32_t> PropertyVirtuals::Hash(const PropertyInfo &property, const std::uint8_t *value) {
    if (!value || !HashReady() || (property.propertyFlags & kHasGetValueTypeHash) == 0)
        return std::nullopt;
    void **vtable = VtableOf(property.field);
    if (!vtable)
        return std::nullopt;
    return reinterpret_cast<HashFn>(vtable[hashSlot_])(reinterpret_cast<const void *>(property.field), value);
}

std::optional<bool> PropertyVirtuals::Identical(const PropertyInfo &property, const std::uint8_t *a,
                                                const std::uint8_t *b) {
    if (!a || !b || !IdenticalReady())
        return std::nullopt;
    void **vtable = VtableOf(property.field);
    if (!vtable)
        return std::nullopt;
    return reinterpret_cast<IdenticalFn>(vtable[identicalSlot_])(reinterpret_cast<const void *>(property.field), a, b,
                                                                 0);
}

} // namespace URK::Unreal
