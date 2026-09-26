#pragma once

// TArray/TSet/TMap edits through the engine allocator. Game thread; all or nothing.

#include "unreal/values/unreal_engine_calls.h"
#include "unreal/values/unreal_property_virtuals.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace URK::Unreal {

class OwnedValues;

// FScriptSet: TSparseArray (data, 4-word TBitArray, free list), then the hash and its size.
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
    Containers(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
               EngineCalls &engine, OwnedValues &owned)
        : finder_(&finder), values_(&values), engine_(&engine), owned_(&owned), virtuals_(finder, chain, values) {}

    const std::string &Failure() const { return failure_; }

    // --- TArray (and a multicast delegate's invocation list) ---
    // count default elements before index; all made, or none.
    bool ArrayInsert(const PropertyInfo &inner, std::uint8_t *array, std::int32_t index, std::int32_t count);
    // Releases then removes count elements; refused if any can't be released.
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
    // Adds key, taking ownership when *consumed is set; an equal key returns its slot.
    std::int32_t Add(const SetLayout &layout, std::uint8_t *set, std::uint8_t *key, bool *consumed);
    // Refused, with the set unchanged, if the element could not be released.
    bool Remove(const SetLayout &layout, std::uint8_t *set, std::int32_t slot);
    // Empties a set whose elements are already released and frees its storage.
    void FreeStorage(std::uint8_t *set);
    static bool HoldsStorage(const std::uint8_t *set);

    PropertyVirtuals &Virtuals() { return virtuals_; }

  private:
    struct KeyRecord {
        // A live container stored a bucket the engine's hash does not give (for good).
        bool contradicted = false;
        bool collapsedNoted = false;
    };
    struct CachedLayout {
        // Layout source; a property reallocated at the same address must not reuse it.
        Address inner = kNullAddress;
        Address valueInner = kNullAddress;
        std::int32_t elementSize = 0;
        std::optional<SetLayout> layout;
        std::string failure;
    };
    // Every validated container checks the engine's hash against the buckets it stored.
    void Check(const SetLayout &layout, std::uint8_t *set);
    // Engine hash of key; none when unavailable, then everything links into one bucket.
    std::optional<std::uint32_t> KeyHash(const SetLayout &layout, const std::uint8_t *key);
    // The engine's Identical where measured, else the loader's own equality.
    bool KeysEqual(const SetLayout &layout, const std::uint8_t *a, const std::uint8_t *b);
    bool Fail(std::string why);
    std::uint8_t *Bits(std::uint8_t *set);
    std::int32_t *Buckets(std::uint8_t *set);
    std::int32_t AllocateSlot(const SetLayout &layout, std::uint8_t *set);
    void FreeSlot(const SetLayout &layout, std::uint8_t *set, std::int32_t slot);
    void LinkInto(const SetLayout &layout, std::uint8_t *set, std::int32_t slot, std::int32_t bucket);
    void Collapse(const SetLayout &layout, std::uint8_t *set);
    void Relink(const SetLayout &layout, std::uint8_t *set, std::uint8_t *table, std::int32_t buckets,
                const std::vector<std::pair<std::int32_t, std::uint32_t>> &hashes);
    std::string KeySignature(const PropertyInfo &key) const;

    const ObjectFinder *finder_;
    const PropertyValues *values_;
    EngineCalls *engine_;
    OwnedValues *owned_;
    PropertyVirtuals virtuals_;
    // Per thread: reads run off the game thread too.
    inline static thread_local std::string failure_;
    std::mutex layoutMutex_;
    std::map<Address, CachedLayout> layouts_;
    std::mutex hashMutex_;
    std::map<std::string, KeyRecord> keys_;
};

} // namespace URK::Unreal
