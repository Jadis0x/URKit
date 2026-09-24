#pragma once

// FName resolution through FNamePool (4.23+), needed before any later rung.

#include "unreal_object_array.h"

#include <cstdint>
#include <optional>
#include <string>

namespace URK::Unreal {

// FNamePool: a block table preceded by the current block index and write
// cursor. A comparison index splits into a block and a stride-scaled offset.
struct NamePoolLayout {
    std::int32_t blockCountOffset = kOffsetNotFound;
    std::int32_t byteCursorOffset = kOffsetNotFound;
    std::int32_t blocksOffset = kOffsetNotFound;
    std::int32_t entryHeaderOffset = kOffsetNotFound;
    std::int32_t entryStringOffset = kOffsetNotFound;
    std::int32_t entryStride = kOffsetNotFound;
    std::int32_t lengthShift = kOffsetNotFound;
    std::int32_t blockOffsetBits = 0xE;
};

// FName: ComparisonIndex, [DisplayIndex], [Number].
struct NameLayout {
    NamePoolLayout pool{};
    std::int32_t displayIndexOffset = kOffsetNotFound;
    std::int32_t numberOffset = 4;
    std::int32_t size = 8;
};

class NameTable {
  public:
    // Probes the pool's header and entry layout at address.
    static std::optional<NameTable> Resolve(const MemoryReader &reader, Address address);

    const NameLayout &Layout() const { return layout_; }
    Address BaseAddress() const { return address_; }

    // Raises blockOffsetBits until every object's name lands in a real block.
    void CalibrateBlockOffsetBits(const ObjectArray &objects, std::int32_t nameOffset);

    // Detects DisplayIndex and Number from object names.
    void CalibrateNameLayout(const ObjectArray &objects, std::int32_t nameOffset, std::int32_t outerOffset);

    std::optional<std::string> Read(std::uint32_t comparisonIndex) const;

    // The FName stored at this address - comparison index and number - as text.
    std::optional<std::string> ReadFName(Address fname) const;

    // The object's name with FName's number suffix applied.
    std::optional<std::string> ObjectName(const ObjectArray &objects, std::int32_t nameOffset,
                                          std::int32_t objectIndex) const;

  private:
    NameTable(const MemoryReader &reader, Address address, const NameLayout &layout)
        : reader_(&reader), address_(address), layout_(layout) {}

    std::optional<std::string> ReadFromPool(std::uint32_t comparisonIndex, int depth) const;

    const MemoryReader *reader_;
    Address address_;
    NameLayout layout_;
};

} // namespace URK::Unreal
