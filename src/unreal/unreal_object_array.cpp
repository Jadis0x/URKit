#include "unreal_object_array.h"

#include <array>

namespace URK::Unreal {
namespace {

constexpr std::array kFixedLayouts = {
    // Default UE4.11 - UE4.20.
    FixedObjectArrayLayout{.objectsOffset = 0x00, .maxObjectsOffset = 0x08, .numObjectsOffset = 0x0C},
};

constexpr std::array kChunkedLayouts = {
    // Default UE4.21 - UE5.7.
    ChunkedObjectArrayLayout{.objectsOffset = 0x00,
                             .maxElementsOffset = 0x10,
                             .numElementsOffset = 0x14,
                             .maxChunksOffset = 0x18,
                             .numChunksOffset = 0x1C},
    // UE5.8: counts reordered, chunk table still first.
    ChunkedObjectArrayLayout{.objectsOffset = 0x00,
                             .maxElementsOffset = 0x0C,
                             .numElementsOffset = 0x08,
                             .maxChunksOffset = 0x14,
                             .numChunksOffset = 0x10},
    // Shipped titles that moved the chunk table behind the counts.
    ChunkedObjectArrayLayout{.objectsOffset = 0x10,
                             .maxElementsOffset = 0x00,
                             .numElementsOffset = 0x04,
                             .maxChunksOffset = 0x08,
                             .numChunksOffset = 0x0C},
    ChunkedObjectArrayLayout{.objectsOffset = 0x18,
                             .maxElementsOffset = 0x10,
                             .numElementsOffset = 0x00,
                             .maxChunksOffset = 0x14,
                             .numChunksOffset = 0x20},
    ChunkedObjectArrayLayout{.objectsOffset = 0x18,
                             .maxElementsOffset = 0x00,
                             .numElementsOffset = 0x14,
                             .maxChunksOffset = 0x10,
                             .numChunksOffset = 0x04},
};

// Generous bounds: they exclude noise, not pin a particular game's numbers.
constexpr std::int32_t kMaxPlausibleElements = 0x400000;
constexpr std::int32_t kMinFixedElements = 0x1000;
constexpr std::int32_t kMinChunkedElements = 0x800;
constexpr std::int32_t kMinChunkedMaxElements = 0x10000;
constexpr std::int32_t kMinChunks = 0x1;
constexpr std::int32_t kMaxChunks = 0x14;
constexpr std::int32_t kMinMaxChunks = 0x6;
constexpr std::int32_t kMaxMaxChunks = 0x5FF;
constexpr std::int32_t kMinElementsPerChunk = 0x8000;
constexpr std::int32_t kMaxElementsPerChunk = 0x80000;

// Items are three pointers wide in every version using the fixed form, so an
// object can be located before the item layout is probed. InternalIndex sits
// behind the vtable and flags in those builds, making "object five reports five"
// a cheap confirmation that this really is GUObjectArray.
constexpr std::int32_t kFixedItemStride = sizeof(Address) * 3;
constexpr std::int32_t kFixedProbeIndex = 5;
constexpr std::int32_t kFixedInternalIndexOffset = sizeof(Address) + sizeof(std::int32_t);

} // namespace

std::span<const FixedObjectArrayLayout> KnownFixedLayouts() { return kFixedLayouts; }

std::span<const ChunkedObjectArrayLayout> KnownChunkedLayouts() { return kChunkedLayouts; }

bool ValidateLayout(const MemoryReader &reader, Address address, const FixedObjectArrayLayout &layout) {
    const std::optional<Address> objects = reader.ReadPointer(address + layout.objectsOffset);
    const std::optional<std::int32_t> maxElements = reader.ReadInt32(address + layout.maxObjectsOffset);
    const std::optional<std::int32_t> numElements = reader.ReadInt32(address + layout.numObjectsOffset);
    if (!objects || !maxElements || !numElements)
        return false;

    if (*numElements > *maxElements)
        return false;
    if (*maxElements > kMaxPlausibleElements)
        return false;
    if (*numElements < kMinFixedElements)
        return false;
    if (*objects == kNullAddress)
        return false;

    const Address probeItem = *objects + static_cast<Address>(kFixedProbeIndex) * kFixedItemStride;
    const std::optional<Address> probeObject = reader.ReadPointer(probeItem);
    if (!probeObject || *probeObject == kNullAddress)
        return false;

    const std::optional<std::int32_t> reportedIndex = reader.ReadInt32(*probeObject + kFixedInternalIndexOffset);
    return reportedIndex && *reportedIndex == kFixedProbeIndex;
}

bool ValidateLayout(const MemoryReader &reader, Address address, const ChunkedObjectArrayLayout &layout) {
    const std::optional<Address> objects = reader.ReadPointer(address + layout.objectsOffset);
    const std::optional<std::int32_t> maxElements = reader.ReadInt32(address + layout.maxElementsOffset);
    const std::optional<std::int32_t> numElements = reader.ReadInt32(address + layout.numElementsOffset);
    const std::optional<std::int32_t> maxChunks = reader.ReadInt32(address + layout.maxChunksOffset);
    const std::optional<std::int32_t> numChunks = reader.ReadInt32(address + layout.numChunksOffset);
    if (!objects || !maxElements || !numElements || !maxChunks || !numChunks)
        return false;

    if (*numChunks > kMaxChunks || *numChunks < kMinChunks)
        return false;
    if (*maxChunks > kMaxMaxChunks || *maxChunks < kMinMaxChunks)
        return false;
    if (*numElements <= kMinChunkedElements || *maxElements <= kMinChunkedMaxElements)
        return false;
    if (*numElements > *maxElements || *numChunks > *maxChunks)
        return false;
    if ((*maxElements % 0x10) != 0)
        return false;

    const std::int32_t elementsPerChunk = *maxElements / *maxChunks;
    if ((elementsPerChunk % 0x10) != 0)
        return false;
    if (elementsPerChunk < kMinElementsPerChunk || elementsPerChunk > kMaxElementsPerChunk)
        return false;

    // Mutually consistent counts are what make the match trustworthy without a
    // signature; passing both by accident is unlikely.
    if (((*numElements / elementsPerChunk) + 1) != *numChunks)
        return false;
    if ((*maxElements / elementsPerChunk) != *maxChunks)
        return false;

    if (*objects == kNullAddress || !reader.Readable(*objects, sizeof(Address) * static_cast<std::size_t>(*numChunks)))
        return false;

    for (std::int32_t chunk = 0; chunk < *numChunks; ++chunk) {
        const std::optional<Address> chunkAddress =
            reader.ReadPointer(*objects + static_cast<Address>(chunk) * sizeof(Address));
        if (!chunkAddress || *chunkAddress == kNullAddress || !reader.Readable(*chunkAddress, sizeof(Address)))
            return false;
    }

    return true;
}

std::optional<ObjectItemLayout> ProbeObjectItemLayout(const MemoryReader &reader, Address firstItem) {
    ObjectItemLayout layout;

    // The UObject pointer is the first field pointing at an object; anything
    // ahead of it is engine bookkeeping.
    for (std::int32_t offset = 0; offset < 0x20; offset += 4) {
        if (reader.PointsToObject(firstItem + offset)) {
            layout.pointerOffset = offset;
            break;
        }
    }
    if (layout.pointerOffset == kOffsetNotFound)
        return std::nullopt;

    // First spacing at which the second and third items also hold objects.
    // Checking two rejects a spacing landing on padding in the first.
    const Address base = firstItem + static_cast<Address>(layout.pointerOffset);
    for (std::int32_t stride = sizeof(Address); stride <= 0x38; stride += 4) {
        const Address second = base + static_cast<Address>(stride);
        const Address third = base + static_cast<Address>(stride) * 2;
        if (reader.PointsToObject(second) && reader.PointsToObject(third)) {
            layout.stride = stride;
            break;
        }
    }
    if (layout.stride == kOffsetNotFound)
        return std::nullopt;

    return layout;
}

std::optional<ObjectArrayLayout> ResolveObjectArrayLayout(const MemoryReader &reader, Address address) {
    ObjectArrayLayout resolved;
    Address firstItem = kNullAddress;

    for (const FixedObjectArrayLayout &candidate : kFixedLayouts) {
        if (!ValidateLayout(reader, address, candidate))
            continue;
        const std::optional<Address> objects = reader.ReadPointer(address + candidate.objectsOffset);
        if (!objects)
            return std::nullopt;
        resolved.chunked = false;
        resolved.fixed = candidate;
        firstItem = *objects;
        break;
    }

    if (firstItem == kNullAddress) {
        for (const ChunkedObjectArrayLayout &candidate : kChunkedLayouts) {
            if (!ValidateLayout(reader, address, candidate))
                continue;
            const std::optional<Address> chunkTable = reader.ReadPointer(address + candidate.objectsOffset);
            const std::optional<std::int32_t> maxElements = reader.ReadInt32(address + candidate.maxElementsOffset);
            const std::optional<std::int32_t> maxChunks = reader.ReadInt32(address + candidate.maxChunksOffset);
            if (!chunkTable || !maxElements || !maxChunks)
                return std::nullopt;
            const std::optional<Address> firstChunk = reader.ReadPointer(*chunkTable);
            if (!firstChunk)
                return std::nullopt;
            resolved.chunked = true;
            resolved.chunks = candidate;
            resolved.elementsPerChunk = *maxElements / *maxChunks;
            firstItem = *firstChunk;
            break;
        }
    }

    if (firstItem == kNullAddress)
        return std::nullopt;

    const std::optional<ObjectItemLayout> item = ProbeObjectItemLayout(reader, firstItem);
    if (!item)
        return std::nullopt;

    resolved.item = *item;
    return resolved;
}

std::int32_t ObjectArray::Num() const {
    const std::int32_t offset = layout_.chunked ? layout_.chunks.numElementsOffset : layout_.fixed.numObjectsOffset;
    const std::optional<std::int32_t> count = reader_->ReadInt32(address_ + offset);
    return count ? *count : 0;
}

Address ObjectArray::ObjectAt(std::int32_t index) const {
    if (index < 0 || index >= Num())
        return kNullAddress;

    const std::int32_t objectsOffset = layout_.chunked ? layout_.chunks.objectsOffset : layout_.fixed.objectsOffset;
    const std::optional<Address> objects = reader_->ReadPointer(address_ + objectsOffset);
    if (!objects)
        return kNullAddress;

    Address itemBase = kNullAddress;
    if (layout_.chunked) {
        if (layout_.elementsPerChunk <= 0)
            return kNullAddress;
        const std::int32_t chunk = index / layout_.elementsPerChunk;
        const std::int32_t indexInChunk = index % layout_.elementsPerChunk;
        const std::optional<Address> chunkAddress =
            reader_->ReadPointer(*objects + static_cast<Address>(chunk) * sizeof(Address));
        if (!chunkAddress || *chunkAddress == kNullAddress)
            return kNullAddress;
        itemBase = *chunkAddress + static_cast<Address>(indexInChunk) * layout_.item.stride;
    } else {
        itemBase = *objects + static_cast<Address>(index) * layout_.item.stride;
    }

    const std::optional<Address> object = reader_->ReadPointer(itemBase + layout_.item.pointerOffset);
    return object ? *object : kNullAddress;
}

} // namespace URK::Unreal
