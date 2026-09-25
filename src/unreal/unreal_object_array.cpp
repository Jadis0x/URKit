#include "unreal_object_array.h"

#include <array>
#include <cstdio>
#include <cstring>

namespace URK::Unreal {
namespace {

constexpr std::array kChunkedLayouts = {
    // Default up to UE5.7.
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

constexpr FixedObjectArrayLayout kFixedLayout{.objectsOffset = 0x00, .maxObjectsOffset = 0x08, .numObjectsOffset = 0x0C};

// Generous bounds: they exclude noise, not pin a particular game's numbers.
constexpr std::int32_t kMaxPlausibleElements = 0x400000;
constexpr std::int32_t kMinFixedElements = 0x1000;
// Fixed-form items are three pointers wide and InternalIndex follows vtable and
// flags, so "object five reports five" confirms the array cheaply.
constexpr std::int32_t kFixedItemStride = sizeof(Address) * 3;
constexpr std::int32_t kFixedProbeIndex = 5;
constexpr std::int32_t kFixedInternalIndexOffset = sizeof(Address) + sizeof(std::int32_t);
constexpr std::int32_t kMinChunkedElements = 0x800;
constexpr std::int32_t kMinChunkedMaxElements = 0x10000;
constexpr std::int32_t kMinChunks = 0x1;
constexpr std::int32_t kMaxChunks = 0x14;
constexpr std::int32_t kMinMaxChunks = 0x6;
constexpr std::int32_t kMaxMaxChunks = 0x5FF;
constexpr std::int32_t kMinElementsPerChunk = 0x8000;
constexpr std::int32_t kMaxElementsPerChunk = 0x80000;

template <typename T> T At(std::span<const std::uint8_t> header, std::int32_t offset) {
    T value{};
    std::memcpy(&value, header.data() + offset, sizeof(T));
    return value;
}

bool HeaderFits(std::span<const std::uint8_t> header, const ChunkedObjectArrayLayout &layout) {
    const Address objects = At<Address>(header, layout.objectsOffset);
    const std::int32_t maxElements = At<std::int32_t>(header, layout.maxElementsOffset);
    const std::int32_t numElements = At<std::int32_t>(header, layout.numElementsOffset);
    const std::int32_t maxChunks = At<std::int32_t>(header, layout.maxChunksOffset);
    const std::int32_t numChunks = At<std::int32_t>(header, layout.numChunksOffset);

    if (numChunks > kMaxChunks || numChunks < kMinChunks)
        return false;
    if (maxChunks > kMaxMaxChunks || maxChunks < kMinMaxChunks)
        return false;
    if (numElements <= kMinChunkedElements || maxElements <= kMinChunkedMaxElements)
        return false;
    if (numElements > maxElements || numChunks > maxChunks)
        return false;
    if ((maxElements % 0x10) != 0)
        return false;

    const std::int32_t elementsPerChunk = maxElements / maxChunks;
    if ((elementsPerChunk % 0x10) != 0)
        return false;
    if (elementsPerChunk < kMinElementsPerChunk || elementsPerChunk > kMaxElementsPerChunk)
        return false;
    if (((numElements / elementsPerChunk) + 1) != numChunks)
        return false;
    if ((maxElements / elementsPerChunk) != maxChunks)
        return false;
    return MemoryReader::PlausiblePointer(objects);
}

bool HeaderFits(std::span<const std::uint8_t> header, const FixedObjectArrayLayout &layout) {
    const Address objects = At<Address>(header, layout.objectsOffset);
    const std::int32_t maxElements = At<std::int32_t>(header, layout.maxObjectsOffset);
    const std::int32_t numElements = At<std::int32_t>(header, layout.numObjectsOffset);
    if (numElements > maxElements || maxElements > kMaxPlausibleElements || numElements < kMinFixedElements)
        return false;
    return MemoryReader::PlausiblePointer(objects);
}

} // namespace

std::span<const ChunkedObjectArrayLayout> KnownChunkedLayouts() { return kChunkedLayouts; }

bool HeaderMightBeObjectArray(std::span<const std::uint8_t> header) {
    if (header.size() < kObjectArrayHeaderBytes)
        return false;
    for (const ChunkedObjectArrayLayout &layout : kChunkedLayouts) {
        if (HeaderFits(header, layout))
            return true;
    }
    return HeaderFits(header, kFixedLayout);
}

bool ValidateLayout(const MemoryReader &reader, Address address, const FixedObjectArrayLayout &layout) {
    const std::optional<Address> objects = reader.ReadPointer(address + layout.objectsOffset);
    const std::optional<std::int32_t> maxElements = reader.ReadInt32(address + layout.maxObjectsOffset);
    const std::optional<std::int32_t> numElements = reader.ReadInt32(address + layout.numObjectsOffset);
    if (!objects || !maxElements || !numElements || *objects == kNullAddress)
        return false;
    if (*numElements > *maxElements || *maxElements > kMaxPlausibleElements || *numElements < kMinFixedElements)
        return false;

    const std::optional<Address> probeObject =
        reader.ReadPointer(*objects + static_cast<Address>(kFixedProbeIndex) * kFixedItemStride);
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

std::optional<ObjectArrayLayout> ResolveObjectArrayLayout(const MemoryReader &reader, Address address,
                                                          std::string *why) {
    const auto fail = [why](std::string reason) -> std::optional<ObjectArrayLayout> {
        if (why)
            *why = std::move(reason);
        return std::nullopt;
    };
    ObjectArrayLayout resolved;
    Address firstItem = kNullAddress;

    for (const ChunkedObjectArrayLayout &candidate : kChunkedLayouts) {
        if (!ValidateLayout(reader, address, candidate))
            continue;
        const std::optional<Address> chunkTable = reader.ReadPointer(address + candidate.objectsOffset);
        const std::optional<std::int32_t> maxElements = reader.ReadInt32(address + candidate.maxElementsOffset);
        const std::optional<std::int32_t> maxChunks = reader.ReadInt32(address + candidate.maxChunksOffset);
        if (!chunkTable || !maxElements || !maxChunks)
            return fail("its header became unreadable");
        const std::optional<Address> firstChunk = reader.ReadPointer(*chunkTable);
        if (!firstChunk)
            return fail("the chunk table is unreadable");
        resolved.chunks = candidate;
        resolved.elementsPerChunk = *maxElements / *maxChunks;
        firstItem = *firstChunk;
        break;
    }

    if (firstItem == kNullAddress && ValidateLayout(reader, address, kFixedLayout)) {
        resolved.chunked = false;
        resolved.fixed = kFixedLayout;
        firstItem = reader.ReadPointer(address + kFixedLayout.objectsOffset).value_or(kNullAddress);
    }

    if (firstItem == kNullAddress) {
        std::string words = "no known layout fits its header (int32s:";
        for (Address at = 0; at < kObjectArrayHeaderBytes; at += 4) {
            char word[16];
            const std::optional<std::int32_t> value = reader.ReadInt32(address + at);
            std::snprintf(word, sizeof(word), value ? " %X" : " ?", value ? static_cast<unsigned>(*value) : 0u);
            words += word;
        }
        return fail(words + ")");
    }

    const std::optional<ObjectItemLayout> item = ProbeObjectItemLayout(reader, firstItem);
    if (!item) {
        bool pointer = false;
        for (std::int32_t offset = 0; offset < 0x20 && !pointer; offset += 4)
            pointer = reader.PointsToObject(firstItem + offset);
        return fail(std::string(resolved.chunked ? "a chunked" : "a flat") + " layout fits, but " +
                    (pointer ? "no item spacing up to 0x38 puts objects in the next two items"
                             : "the first item holds no object pointer in its first 0x20 bytes"));
    }

    resolved.item = *item;
    return resolved;
}

std::int32_t ObjectArray::Num() const {
    const std::int32_t offset = layout_.chunked ? layout_.chunks.numElementsOffset : layout_.fixed.numObjectsOffset;
    const std::optional<std::int32_t> count = reader_->ReadInt32(address_ + offset);
    return count ? *count : 0;
}

Address ObjectArray::ItemAt(std::int32_t index) const {
    if (index < 0 || index >= Num())
        return kNullAddress;

    const std::int32_t objectsOffset = layout_.chunked ? layout_.chunks.objectsOffset : layout_.fixed.objectsOffset;
    const std::optional<Address> objects = reader_->ReadPointer(address_ + objectsOffset);
    if (!objects || *objects == kNullAddress)
        return kNullAddress;
    if (!layout_.chunked)
        return *objects + static_cast<Address>(index) * layout_.item.stride;
    if (layout_.elementsPerChunk <= 0)
        return kNullAddress;

    const std::int32_t chunk = index / layout_.elementsPerChunk;
    const std::int32_t indexInChunk = index % layout_.elementsPerChunk;
    const std::optional<Address> chunkAddress =
        reader_->ReadPointer(*objects + static_cast<Address>(chunk) * sizeof(Address));
    if (!chunkAddress || *chunkAddress == kNullAddress)
        return kNullAddress;
    return *chunkAddress + static_cast<Address>(indexInChunk) * layout_.item.stride;
}

Address ObjectArray::ObjectAt(std::int32_t index) const {
    const Address itemBase = ItemAt(index);
    if (itemBase == kNullAddress)
        return kNullAddress;
    const std::optional<Address> object = reader_->ReadPointer(itemBase + layout_.item.pointerOffset);
    return object ? *object : kNullAddress;
}

} // namespace URK::Unreal
