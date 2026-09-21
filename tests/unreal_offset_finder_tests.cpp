// Unreal offset calibration against synthetic object graphs.
//
// There is no API to check the calibration against, so the proof is that it
// recovers layouts it was never told. Two incompatible graphs are used, each
// asserting the other's array order is rejected: one graph alone would pass for
// an implementation that simply hardcoded it.

#include "src/unreal/unreal_object_array.h"
#include "src/unreal/unreal_offsets.h"
#include "tests/unreal_fake_image.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("  %-70s %s\n", what.c_str(), condition ? "ok" : "FAILED");
    if (!condition)
        ++g_failures;
}

void CheckOffset(const char *what, std::int32_t actual, std::int32_t expected) {
    char detail[160];
    std::snprintf(detail, sizeof(detail), "%s resolved to 0x%02X (expected 0x%02X)", what, actual, expected);
    Check(actual == expected, detail);
}

using UnrealTest::FakeImage;
using URK::Unreal::Address;
using URK::Unreal::ChunkedObjectArrayLayout;

constexpr Address kImageBase = 0x10000000;
constexpr Address kVTableAddress = 0x100;
constexpr Address kArrayAddress = 0x200;
constexpr Address kChunkTableAddress = 0x300;
constexpr Address kItemsAddress = 0x1000;
constexpr Address kPageSize = 0x1000;

// What a graph is built to, and so the answer the calibration must reach.
struct GraphSpec {
    const char *name;

    std::int32_t flagsOffset;
    std::int32_t indexOffset;
    std::int32_t classOffset;
    std::int32_t nameOffset;
    std::int32_t outerOffset;
    std::int32_t objectStride;

    std::int32_t itemPointerOffset;
    std::int32_t itemStride;

    ChunkedObjectArrayLayout array;
    std::int32_t objectCount;
    std::int32_t elementsPerChunk;
    std::int32_t maxChunks;
};

// UE5.8 order: chunk table first, element and chunk counts swapped.
constexpr GraphSpec kUe58Graph{
    .name = "UE5.8 count order, compact header",
    .flagsOffset = 0x08,
    .indexOffset = 0x0C,
    .classOffset = 0x10,
    .nameOffset = 0x18,
    .outerOffset = 0x20,
    .objectStride = 0x40,
    .itemPointerOffset = 0x00,
    .itemStride = 0x18,
    .array = {.objectsOffset = 0x00,
              .maxElementsOffset = 0x0C,
              .numElementsOffset = 0x08,
              .maxChunksOffset = 0x14,
              .numChunksOffset = 0x10},
    .objectCount = 0x1000,
    .elementsPerChunk = 0x8000,
    .maxChunks = 8,
};

// UE4.21-UE5.7 order, padded ahead of the flags and with a wider FUObjectItem,
// as builds carrying extra bookkeeping present it.
constexpr GraphSpec kUe57Graph{
    .name = "pre-5.8 count order, padded header",
    .flagsOffset = 0x10,
    .indexOffset = 0x14,
    .classOffset = 0x18,
    .nameOffset = 0x20,
    .outerOffset = 0x28,
    .objectStride = 0x40,
    .itemPointerOffset = 0x08,
    .itemStride = 0x20,
    .array = {.objectsOffset = 0x00,
              .maxElementsOffset = 0x10,
              .numElementsOffset = 0x14,
              .maxChunksOffset = 0x18,
              .numChunksOffset = 0x1C},
    .objectCount = 0x1000,
    .elementsPerChunk = 0x8000,
    .maxChunks = 8,
};

Address ObjectsRegion(const GraphSpec &spec) {
    const Address itemsEnd = kItemsAddress + static_cast<Address>(spec.objectCount) * spec.itemStride;
    return (itemsEnd + kPageSize - 1) & ~(kPageSize - 1);
}

std::size_t ImageSize(const GraphSpec &spec) {
    return ObjectsRegion(spec) + static_cast<Address>(spec.objectCount) * spec.objectStride + kPageSize;
}

Address ObjectAddressAt(const GraphSpec &spec, std::int32_t index) {
    return kImageBase + ObjectsRegion(spec) + static_cast<Address>(index) * spec.objectStride;
}

void BuildImage(FakeImage &image, const GraphSpec &spec) {
    const std::int32_t maxElements = spec.elementsPerChunk * spec.maxChunks;
    const std::int32_t numChunks = (spec.objectCount / spec.elementsPerChunk) + 1;

    // The calibration tells an object pointer from an unrelated mapped value by
    // the vtable every object starts with.
    image.Put<Address>(kVTableAddress, kImageBase);

    image.Put<Address>(kArrayAddress + spec.array.objectsOffset, kImageBase + kChunkTableAddress);
    image.Put<std::int32_t>(kArrayAddress + spec.array.numElementsOffset, spec.objectCount);
    image.Put<std::int32_t>(kArrayAddress + spec.array.maxElementsOffset, maxElements);
    image.Put<std::int32_t>(kArrayAddress + spec.array.numChunksOffset, numChunks);
    image.Put<std::int32_t>(kArrayAddress + spec.array.maxChunksOffset, spec.maxChunks);

    image.Put<Address>(kChunkTableAddress, kImageBase + kItemsAddress);

    for (std::int32_t index = 0; index < spec.objectCount; ++index) {
        const Address item = kItemsAddress + static_cast<Address>(index) * spec.itemStride;
        image.Put<Address>(item + spec.itemPointerOffset, ObjectAddressAt(spec, index));

        const Address object = ObjectsRegion(spec) + static_cast<Address>(index) * spec.objectStride;
        image.Put<Address>(object, kImageBase + kVTableAddress);

        // Common enough to stand out, but not universal.
        image.Put<std::uint32_t>(object + spec.flagsOffset, (index % 8 == 0) ? 0x41u : 0x43u);
        image.Put<std::int32_t>(object + spec.indexOffset, index);

        // Most objects point at a class, which points at the class of classes,
        // which points at itself.
        image.Put<Address>(object + spec.classOffset, ObjectAddressAt(spec, index <= 3 ? 2 : 3));

        // Real spread in the comparison index and a zero instance number, so a
        // wrong offset cannot pass the distribution test.
        image.Put<std::uint32_t>(object + spec.nameOffset, 0x300u + static_cast<std::uint32_t>(index) * 7u);
        image.Put<std::uint32_t>(object + spec.nameOffset + 4, 0u);

        // The first two stand in for packages, which have no outer.
        image.Put<Address>(object + spec.outerOffset, index < 2 ? 0 : ObjectAddressAt(spec, 1));
    }
}

void RunGraph(const GraphSpec &spec, const ChunkedObjectArrayLayout &rejectedOrder, const char *rejectedName) {
    using namespace URK::Unreal;

    std::printf("\n%s\n", spec.name);

    FakeImage image(kImageBase, ImageSize(spec));
    BuildImage(image, spec);

    const Address arrayAddress = kImageBase + kArrayAddress;

    const std::optional<ObjectArrayLayout> layout = ResolveObjectArrayLayout(image, arrayAddress);
    Check(layout.has_value(), "GUObjectArray layout resolves from a bare address");
    if (!layout)
        return;

    Check(layout->chunked, "array is recognized as chunked");
    CheckOffset("chunk table", layout->chunks.objectsOffset, spec.array.objectsOffset);
    CheckOffset("num elements", layout->chunks.numElementsOffset, spec.array.numElementsOffset);
    CheckOffset("max elements", layout->chunks.maxElementsOffset, spec.array.maxElementsOffset);
    CheckOffset("num chunks", layout->chunks.numChunksOffset, spec.array.numChunksOffset);
    CheckOffset("max chunks", layout->chunks.maxChunksOffset, spec.array.maxChunksOffset);
    Check(layout->elementsPerChunk == spec.elementsPerChunk, "elements per chunk derived from max elements and chunks");

    CheckOffset("FUObjectItem pointer", layout->item.pointerOffset, spec.itemPointerOffset);
    CheckOffset("FUObjectItem stride", layout->item.stride, spec.itemStride);

    // If the other count order matched, every later offset would be measured
    // against garbage.
    Check(!ValidateLayout(image, arrayAddress, rejectedOrder), std::string("the ") + rejectedName + " is rejected");

    const ObjectArray objects(image, arrayAddress, *layout);
    Check(objects.Num() == spec.objectCount, "object count reads back from the array");
    Check(objects.ObjectAt(0x55) == ObjectAddressAt(spec, 0x55), "object lookup walks chunk and item layout");
    Check(objects.ObjectAt(spec.objectCount) == kNullAddress, "out-of-range lookup yields no object");

    CheckOffset("UObject::Flags", FindFlagsOffset(objects), spec.flagsOffset);
    CheckOffset("UObject::InternalIndex", FindIndexOffset(objects), spec.indexOffset);
    CheckOffset("UObject::ClassPrivate", FindClassOffset(objects), spec.classOffset);

    const ObjectOffsets offsets = FindObjectOffsets(objects);
    Check(offsets.Resolved(), "every UObject header offset is resolved");
    CheckOffset("ladder Flags", offsets.flags, spec.flagsOffset);
    CheckOffset("ladder InternalIndex", offsets.index, spec.indexOffset);
    CheckOffset("ladder ClassPrivate", offsets.classPointer, spec.classOffset);
    CheckOffset("ladder OuterPrivate", offsets.outer, spec.outerOffset);
    CheckOffset("ladder NamePrivate", offsets.name, spec.nameOffset);
}

} // namespace

int main() {
    RunGraph(kUe58Graph, kUe57Graph.array, "pre-5.8 count order");
    RunGraph(kUe57Graph, kUe58Graph.array, "UE5.8 count order");

    if (g_failures == 0) {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
