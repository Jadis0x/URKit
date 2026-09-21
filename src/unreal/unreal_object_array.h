#pragma once

// GUObjectArray discovery, the first rung of offset calibration: every later
// step needs real UObjects to measure against. The array's field order is not
// stable - UE5.8 reordered it and shipped titles shuffle it further - so
// candidates are validated against the array's own invariants rather than
// pinned per engine version.

#include "unreal_memory.h"

#include <cstdint>
#include <optional>
#include <span>

namespace URK::Unreal {

inline constexpr std::int32_t kOffsetNotFound = -1;

// UE4.11 - UE4.20: one flat allocation of FUObjectItem.
struct FixedObjectArrayLayout {
    std::int32_t objectsOffset = 0;
    std::int32_t maxObjectsOffset = 0;
    std::int32_t numObjectsOffset = 0;
};

// UE4.21+: a table of chunk pointers. The per-chunk count is not stored and is
// derived as maxElements / maxChunks.
struct ChunkedObjectArrayLayout {
    std::int32_t objectsOffset = 0;
    std::int32_t maxElementsOffset = 0;
    std::int32_t numElementsOffset = 0;
    std::int32_t maxChunksOffset = 0;
    std::int32_t numChunksOffset = 0;
};

// Layouts seen across engine versions and shipped titles, tried in order.
std::span<const FixedObjectArrayLayout> KnownFixedLayouts();
std::span<const ChunkedObjectArrayLayout> KnownChunkedLayouts();

// Probed, not assumed: FUObjectItem gained fields over successive versions.
struct ObjectItemLayout {
    std::int32_t pointerOffset = kOffsetNotFound;
    std::int32_t stride = kOffsetNotFound;

    bool Resolved() const { return pointerOffset != kOffsetNotFound && stride != kOffsetNotFound; }
};

struct ObjectArrayLayout {
    bool chunked = false;
    FixedObjectArrayLayout fixed{};
    ChunkedObjectArrayLayout chunks{};
    ObjectItemLayout item{};
    std::int32_t elementsPerChunk = 0;
};

// Whether the candidate address holds an object array in the given layout.
bool ValidateLayout(const MemoryReader &reader, Address address, const FixedObjectArrayLayout &layout);
bool ValidateLayout(const MemoryReader &reader, Address address, const ChunkedObjectArrayLayout &layout);

std::optional<ObjectItemLayout> ProbeObjectItemLayout(const MemoryReader &reader, Address firstItem);

// Picks the matching array layout, then probes the item layout behind it.
std::optional<ObjectArrayLayout> ResolveObjectArrayLayout(const MemoryReader &reader, Address address);

// Reads objects out of a resolved array. The reader must outlive it.
class ObjectArray {
  public:
    ObjectArray(const MemoryReader &reader, Address address, const ObjectArrayLayout &layout)
        : reader_(&reader), address_(address), layout_(layout) {}

    const MemoryReader &Reader() const { return *reader_; }
    const ObjectArrayLayout &Layout() const { return layout_; }

    std::int32_t Num() const;

    // kNullAddress when out of range or any link in the chain is unreadable.
    Address ObjectAt(std::int32_t index) const;

  private:
    const MemoryReader *reader_;
    Address address_;
    ObjectArrayLayout layout_;
};

} // namespace URK::Unreal
