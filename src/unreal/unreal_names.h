#pragma once

// FName resolution. Every remaining calibration step looks objects up by name,
// so this has to work before CastFlags, Children or the property chain can be
// measured. Two storage forms exist - FNamePool from UE4.23 and the older
// TNameEntryArray - and both are probed rather than picked by version.

#include "unreal_object_array.h"

#include <cstdint>
#include <optional>
#include <string>

namespace URK::Unreal {

enum class NameStorage { Pool, EntryArray };

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

// TNameEntryArray: chunks of FNameEntry pointers, with the counts stored after
// the chunk table. The chunk size is fixed at 0x4000 in every version using it.
struct NameEntryArrayLayout {
    std::int32_t numElementsOffset = kOffsetNotFound;
    std::int32_t blockCountOffset = kOffsetNotFound;
    std::int32_t entryStringOffset = kOffsetNotFound;
    std::int32_t entryIndexOffset = kOffsetNotFound;
};

struct NameLayout {
    NameStorage storage = NameStorage::Pool;
    NamePoolLayout pool{};
    NameEntryArrayLayout entries{};
};

class NameTable {
  public:
    // Identifies the storage form at address and probes its entry layout.
    static std::optional<NameTable> Resolve(const MemoryReader &reader, Address address);

    const NameLayout &Layout() const { return layout_; }
    Address BaseAddress() const { return address_; }

    // Raises blockOffsetBits until every object's name lands in a real block.
    // Pool form only; the entry array uses a fixed chunk size.
    void CalibrateBlockOffsetBits(const ObjectArray &objects, std::int32_t nameOffset);

    std::optional<std::string> Read(std::uint32_t comparisonIndex) const;

    // The object's name with FName's number suffix applied.
    std::optional<std::string> ObjectName(const ObjectArray &objects, std::int32_t nameOffset,
                                          std::int32_t objectIndex) const;

  private:
    NameTable(const MemoryReader &reader, Address address, const NameLayout &layout)
        : reader_(&reader), address_(address), layout_(layout) {}

    std::optional<std::string> ReadFromPool(std::uint32_t comparisonIndex, int depth) const;
    std::optional<std::string> ReadFromEntryArray(std::uint32_t comparisonIndex) const;

    const MemoryReader *reader_;
    Address address_;
    NameLayout layout_;
};

} // namespace URK::Unreal
