#pragma once

// GUObjectArray discovery; field order varies, so candidates are validated.

#include "unreal/memory/unreal_memory.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace URK::Unreal {

inline constexpr std::int32_t kOffsetNotFound = -1;

// One flat FUObjectItem allocation: UE4.20 and older, and later titles that kept it (Grounded).
struct FixedObjectArrayLayout {
    std::int32_t objectsOffset = 0;
    std::int32_t maxObjectsOffset = 0;
    std::int32_t numObjectsOffset = 0;
};

// Chunk pointer table (UE4.21+); per-chunk count = maxElements / maxChunks.
struct ChunkedObjectArrayLayout {
    std::int32_t objectsOffset = 0;
    std::int32_t maxElementsOffset = 0;
    std::int32_t numElementsOffset = 0;
    std::int32_t maxChunksOffset = 0;
    std::int32_t numChunksOffset = 0;
};

// Layouts seen across engine versions and shipped titles, tried in order.
std::span<const ChunkedObjectArrayLayout> KnownChunkedLayouts();

// Bytes of a candidate that the header-only prefilter looks at.
inline constexpr std::size_t kObjectArrayHeaderBytes = 0x24;

// Header-only prefilter before full validation.
bool HeaderMightBeObjectArray(std::span<const std::uint8_t> header);

// Probed, not assumed: FUObjectItem gained fields over successive versions.
struct ObjectItemLayout {
    std::int32_t pointerOffset = kOffsetNotFound;
    std::int32_t stride = kOffsetNotFound;

    bool Resolved() const { return pointerOffset != kOffsetNotFound && stride != kOffsetNotFound; }
};

struct ObjectArrayLayout {
    bool chunked = true;
    FixedObjectArrayLayout fixed{};
    ChunkedObjectArrayLayout chunks{};
    ObjectItemLayout item{};
    std::int32_t elementsPerChunk = 0;
};

// Whether the candidate address holds an object array in the given layout.
bool ValidateLayout(const MemoryReader &reader, Address address, const ChunkedObjectArrayLayout &layout);
bool ValidateLayout(const MemoryReader &reader, Address address, const FixedObjectArrayLayout &layout);

std::optional<ObjectItemLayout> ProbeObjectItemLayout(const MemoryReader &reader, Address firstItem);

// Picks the matching array layout, then probes the item layout behind it; why says what failed.
std::optional<ObjectArrayLayout> ResolveObjectArrayLayout(const MemoryReader &reader, Address address,
                                                          std::string *why = nullptr);

// Reads objects out of a resolved array. The reader must outlive it.
class ObjectArray {
  public:
    ObjectArray(const MemoryReader &reader, Address address, const ObjectArrayLayout &layout)
        : reader_(&reader), address_(address), layout_(layout) {}

    const MemoryReader &Reader() const { return *reader_; }
    const ObjectArrayLayout &Layout() const { return layout_; }
    // GUObjectArray's ObjObjects.
    Address BaseAddress() const { return address_; }

    std::int32_t Num() const;

    // kNullAddress when out of range or any link in the chain is unreadable.
    Address ObjectAt(std::int32_t index) const;
    // The FUObjectItem holding it.
    Address ItemAt(std::int32_t index) const;

    // Every slot in index order, a block per read; stops when visit returns false.
    template <typename Visit> void ForEach(Visit &&visit) const {
        const std::int32_t total = Num();
        std::vector<Address> block;
        std::vector<std::uint8_t> bytes;
        for (std::int32_t first = 0; first < total;) {
            const std::int32_t count = ReadBlock(first, total, block, bytes);
            if (count <= 0)
                return;
            for (std::int32_t i = 0; i < count; ++i) {
                if (!visit(first + i, block[i]))
                    return;
            }
            first += count;
        }
    }

  private:
    // Object pointers from first on, within one chunk; null where unreadable.
    std::int32_t ReadBlock(std::int32_t first, std::int32_t total, std::vector<Address> &objects,
                           std::vector<std::uint8_t> &bytes) const;

    const MemoryReader *reader_;
    Address address_;
    ObjectArrayLayout layout_;
};

} // namespace URK::Unreal
