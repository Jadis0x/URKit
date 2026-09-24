#pragma once

// TArray, TSet and TMap storage changed the way the engine's own templates
// change it, every buffer taken from and returned to the engine's allocator.
// A sparse set's element layout is computed with the engine's own rules
// (FStructBuilder, TScriptSparseSet::GetScriptLayout) and must be found verbatim
// in the layout the engine stored in the property; a container is checked link
// by link before it is changed. Game thread only.
//
// Every change is a transaction: whatever can fail (allocating, making or
// checking values) happens before the container changes, and a failure leaves
// it as it was. Past that point nothing fails; a replaced buffer is detached
// first and freed last, so an engine failure there leaks it (noted), never
// leaves it reachable.

#include "unreal_engine_calls.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace URK::Unreal {

class OwnedValues;

// FScriptSet: TSparseArray (TArray data, TBitArray with 4 inline words, free
// list), then the hash (one inline bucket, else a heap array) and its size.
namespace SetFields {
inline constexpr std::int32_t kData = 0;
inline constexpr std::int32_t kNum = 8;
inline constexpr std::int32_t kMax = 12;
inline constexpr std::int32_t kBitsInline = 16;
inline constexpr std::int32_t kBitsInlineWords = 4;
inline constexpr std::int32_t kBitsSecondary = 32;
inline constexpr std::int32_t kNumBits = 40;
inline constexpr std::int32_t kMaxBits = 44;
inline constexpr std::int32_t kFirstFree = 48;
inline constexpr std::int32_t kNumFree = 52;
inline constexpr std::int32_t kHashInline = 56;
inline constexpr std::int32_t kHashSecondary = 64;
inline constexpr std::int32_t kHashSize = 72;
inline constexpr std::int32_t kSize = 80;
} // namespace SetFields

struct SetLayout {
    bool isMap = false;
    PropertyInfo key;   // the set's element, or the map's key
    PropertyInfo value; // map only
    // The key as the engine hashes it: an enum by its underlying number
    // (FEnumProperty::GetValueTypeHashInternal).
    PropertyInfo hashKey;
    std::int32_t valueOffset = 0;
    // TSetElement: the element (or pair), then HashNextId and HashIndex.
    std::int32_t hashNextIdOffset = 0;
    std::int32_t hashIndexOffset = 0;
    // One sparse array slot: the set element or a free-list link.
    std::int32_t stride = 0;
    std::int32_t alignment = 0;
};

class Containers {
  public:
    Containers(const ObjectFinder &finder, const PropertyValues &values, EngineCalls &engine, OwnedValues &owned)
        : finder_(&finder), values_(&values), engine_(&engine), owned_(&owned) {}

    const std::string &Failure() const { return failure_; }

    // --- TArray (and a multicast delegate's invocation list) ---
    // count default elements before index; all made, or none.
    bool ArrayInsert(const PropertyInfo &inner, std::uint8_t *array, std::int32_t index, std::int32_t count);
    // count elements from index, released first; refused whole if any of them
    // could not be released.
    bool ArrayRemove(const PropertyInfo &inner, std::uint8_t *array, std::int32_t index, std::int32_t count);

    // --- TSet / TMap ---
    // Reflection only, cached per property; safe off the game thread.
    std::optional<SetLayout> LayoutOf(const PropertyInfo &container);
    // The set's structure, every link followed; false (with Failure) if any disagrees.
    bool Validate(const SetLayout &layout, const std::uint8_t *set);
    static std::int32_t Count(const std::uint8_t *set);
    // Occupied slots in iteration order.
    static std::vector<std::int32_t> Slots(const std::uint8_t *set);
    // The slot's element, or null when it is not occupied.
    static std::uint8_t *Element(const SetLayout &layout, std::uint8_t *set, std::int32_t slot);
    std::int32_t Find(const SetLayout &layout, std::uint8_t *set, const std::uint8_t *key);
    // Adds key (a constructed value whose ownership moves in when *consumed is
    // set); an existing equal key returns its slot and consumes nothing.
    std::int32_t Add(const SetLayout &layout, std::uint8_t *set, std::uint8_t *key, bool *consumed);
    // Refused, with the set unchanged, if the element could not be released.
    bool Remove(const SetLayout &layout, std::uint8_t *set, std::int32_t slot);
    // Empties a set whose elements are already released and frees its storage.
    void FreeStorage(std::uint8_t *set);
    static bool HoldsStorage(const std::uint8_t *set);

  private:
    struct HashEvidence {
        // Per rule: 0 open, 1 trusted, -1 contradicted (for good).
        std::vector<int> state;
        bool exhaustedNoted = false;
        bool collapsedNoted = false;
    };
    struct CachedLayout {
        // What the layout was computed from; a property freed and made again
        // at the same address must not reuse it.
        Address inner = kNullAddress;
        Address valueInner = kNullAddress;
        std::int32_t elementSize = 0;
        std::optional<SetLayout> layout;
        std::string failure;
    };
    // Every validated container is evidence, before and after a rule is trusted.
    void Learn(const SetLayout &layout, std::uint8_t *set);
    // The key's hash if a rule is trusted and every rule not contradicted agrees
    // on it; otherwise no hash is sure, and the caller links into one bucket.
    std::optional<std::uint32_t> AgreedHash(const SetLayout &layout, const std::uint8_t *key);
    bool Fail(std::string why);
    std::uint8_t *Bits(std::uint8_t *set);
    std::int32_t *Buckets(std::uint8_t *set);
    std::int32_t AllocateSlot(const SetLayout &layout, std::uint8_t *set);
    void FreeSlot(const SetLayout &layout, std::uint8_t *set, std::int32_t slot);
    void LinkInto(const SetLayout &layout, std::uint8_t *set, std::int32_t slot, std::int32_t bucket);
    void Collapse(const SetLayout &layout, std::uint8_t *set);
    void Relink(const SetLayout &layout, std::uint8_t *set, std::uint8_t *table, std::int32_t buckets,
                const std::vector<std::pair<std::int32_t, std::uint32_t>> &hashes);
    std::optional<std::uint32_t> Hash(int rule, const PropertyInfo &key, const std::uint8_t *value) const;
    std::string KeySignature(const PropertyInfo &key) const;

    const ObjectFinder *finder_;
    const PropertyValues *values_;
    EngineCalls *engine_;
    OwnedValues *owned_;
    // Per thread: reads run off the game thread too.
    inline static thread_local std::string failure_;
    std::mutex layoutMutex_;
    std::map<Address, CachedLayout> layouts_;
    std::mutex hashMutex_;
    std::map<std::string, HashEvidence> hashes_;
};

} // namespace URK::Unreal
