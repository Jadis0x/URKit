// FName resolution against synthetic name tables.
//
// Both storage forms are built here and handed to the same entry point, so the
// test also pins that the pool and the older entry array are told apart by
// probing rather than by any version hint.

#include "src/unreal/unreal_names.h"
#include "tests/unreal_fake_image.h"

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("  %-64s %s\n", what.c_str(), condition ? "ok" : "FAILED");
    if (!condition)
        ++g_failures;
}

void CheckName(const std::optional<std::string> &actual, const char *expected) {
    const std::string shown = actual ? ("\"" + *actual + "\"") : std::string("<none>");
    Check(actual && *actual == expected, std::string("reads ") + shown + " for \"" + expected + "\"");
}

using UnrealTest::FakeImage;
using URK::Unreal::Address;
using URK::Unreal::NameStorage;
using URK::Unreal::NameTable;

constexpr Address kImageBase = 0x20000000;

// ---------------------------------------------------------------- FNamePool

// FNamePool declares Blocks as a fixed 8192-entry array, so the block table is
// followed by a long run of nulls before any other data. Everything else sits
// past that run, or the table scan would count unrelated bytes as blocks.
constexpr Address kPoolAddress = 0x0000;
constexpr Address kBlockZero = 0x4000;
constexpr Address kBlockOne = 0x5000;
constexpr Address kArrayAddress = 0x6000;
constexpr Address kItemsAddress = 0x6100;
constexpr Address kObjectsAddress = 0x7000;
constexpr std::size_t kPoolImageSize = 0x9000;

constexpr std::int32_t kObjectStride = 0x40;
constexpr std::int32_t kObjectNameOffset = 0x18;
constexpr std::int32_t kObjectCount = 6;

// The block the calibration must find: names live 0x10 bits deep, so the 0xE it
// starts from has to be raised twice.
constexpr std::int32_t kBlockOffsetBits = 0x10;

// Entry offsets within block zero, in bytes. A comparison index is the block
// number shifted up, plus the byte offset divided by the entry stride of 2.
constexpr std::int32_t kNoneEntry = 0x00;
constexpr std::int32_t kBytePropertyEntry = 0x06;
constexpr std::int32_t kCoreUObjectEntry = 0x14;
constexpr std::int32_t kClassEntry = 0x2A;
constexpr std::int32_t kWideEntry = 0x32;
constexpr std::int32_t kNumberedEntry = 0x3C;
constexpr std::int32_t kActorEntry = 0x00; // block one

std::uint32_t PoolIndex(std::int32_t block, std::int32_t byteOffset) {
    return (static_cast<std::uint32_t>(block) << kBlockOffsetBits) + static_cast<std::uint32_t>(byteOffset / 2);
}

void PutAnsiEntry(FakeImage &image, Address entry, const char *text) {
    const auto length = static_cast<std::uint16_t>(std::strlen(text));
    image.Put<std::uint16_t>(entry, static_cast<std::uint16_t>(length << 1));
    image.PutBytes(entry + 2, text, length);
}

void PutWideEntry(FakeImage &image, Address entry, const char *text) {
    const auto length = static_cast<std::uint16_t>(std::strlen(text));
    image.Put<std::uint16_t>(entry, static_cast<std::uint16_t>((length << 1) | 1));
    for (std::uint16_t i = 0; i < length; ++i)
        image.Put<std::uint16_t>(entry + 2 + i * 2, static_cast<std::uint16_t>(text[i]));
}

// A numbered entry carries no text: the payload references the base name and
// the number to append.
void PutNumberedEntry(FakeImage &image, Address entry, std::uint32_t baseIndex, std::int32_t number) {
    image.Put<std::uint16_t>(entry, 0);
    image.Put<std::int32_t>(entry + 2, static_cast<std::int32_t>(baseIndex));
    image.Put<std::int32_t>(entry + 6, number);
}

URK::Unreal::ObjectArrayLayout PoolObjectArrayLayout() {
    URK::Unreal::ObjectArrayLayout layout;
    layout.chunked = false;
    layout.fixed = {.objectsOffset = 0x00, .maxObjectsOffset = 0x08, .numObjectsOffset = 0x0C};
    layout.item = {.pointerOffset = 0x00, .stride = 0x08};
    return layout;
}

void BuildPoolImage(FakeImage &image) {
    image.Put<std::int32_t>(kPoolAddress + 0x00, 1); // one block past block zero
    image.Put<std::int32_t>(kPoolAddress + 0x04, 0x46);
    image.Put<Address>(kPoolAddress + 0x10, kImageBase + kBlockZero);
    image.Put<Address>(kPoolAddress + 0x18, kImageBase + kBlockOne);

    // "None" and "/Script/CoreUObject" are what identify a name block, and
    // "ByteProperty" is what calibrates the length shift.
    PutAnsiEntry(image, kBlockZero + kNoneEntry, "None");
    PutAnsiEntry(image, kBlockZero + kBytePropertyEntry, "ByteProperty");
    PutAnsiEntry(image, kBlockZero + kCoreUObjectEntry, "/Script/CoreUObject");
    PutAnsiEntry(image, kBlockZero + kClassEntry, "Class");
    PutWideEntry(image, kBlockZero + kWideEntry, "Wide");
    PutNumberedEntry(image, kBlockZero + kNumberedEntry, PoolIndex(0, kClassEntry), 3);
    PutAnsiEntry(image, kBlockOne + kActorEntry, "Actor");

    image.Put<Address>(kArrayAddress + 0x00, kImageBase + kItemsAddress);
    image.Put<std::int32_t>(kArrayAddress + 0x08, 0x10);
    image.Put<std::int32_t>(kArrayAddress + 0x0C, kObjectCount);

    const std::uint32_t names[kObjectCount] = {
        PoolIndex(0, kNoneEntry),  PoolIndex(0, kClassEntry), PoolIndex(1, kActorEntry),
        PoolIndex(0, kClassEntry), PoolIndex(0, kNumberedEntry), PoolIndex(0, kWideEntry),
    };
    const std::int32_t numbers[kObjectCount] = {0, 0, 0, 3, 0, 0};

    for (std::int32_t index = 0; index < kObjectCount; ++index) {
        const Address object = kObjectsAddress + static_cast<Address>(index) * kObjectStride;
        image.Put<Address>(kItemsAddress + static_cast<Address>(index) * 8, kImageBase + object);
        image.Put<std::uint32_t>(object + kObjectNameOffset, names[index]);
        image.Put<std::int32_t>(object + kObjectNameOffset + 4, numbers[index]);
    }
}

// --------------------------------------------------------- TNameEntryArray

constexpr Address kEntryArrayAddress = 0x0000;
constexpr Address kChunkZero = 0x1000;
constexpr Address kEntriesAddress = 0x2000;
constexpr std::size_t kEntryArrayImageSize = 0x4000;
constexpr std::int32_t kLegacyEntryStride = 0x40;
constexpr std::int32_t kLegacyStringOffset = 0x10;
constexpr std::int32_t kLegacyEntryCount = 9;

void PutLegacyEntry(FakeImage &image, std::int32_t index, const char *text, bool wide) {
    const Address entry = kEntriesAddress + static_cast<Address>(index) * kLegacyEntryStride;
    image.Put<Address>(kChunkZero + static_cast<Address>(index) * 8, kImageBase + entry);
    image.Put<std::uint32_t>(entry, (static_cast<std::uint32_t>(index) << 1) | (wide ? 1u : 0u));

    const std::size_t length = std::strlen(text);
    if (!wide) {
        image.PutBytes(entry + kLegacyStringOffset, text, length + 1);
        return;
    }
    for (std::size_t i = 0; i <= length; ++i)
        image.Put<std::uint16_t>(entry + kLegacyStringOffset + i * 2, static_cast<std::uint16_t>(text[i]));
}

void BuildEntryArrayImage(FakeImage &image) {
    image.Put<Address>(kEntryArrayAddress + 0x00, kImageBase + kChunkZero);
    // The counts sit past the gap that ends the chunk table.
    image.Put<std::int32_t>(kEntryArrayAddress + 0x18, kLegacyEntryCount);
    image.Put<std::int32_t>(kEntryArrayAddress + 0x1C, 1);

    const char *names[kLegacyEntryCount] = {"None",  "Class", "Actor", "Object", "Pawn",
                                            "Level", "World", "Guid",  "Eight"};
    for (std::int32_t index = 0; index < kLegacyEntryCount; ++index)
        PutLegacyEntry(image, index, names[index], index == 6);
}

void RunPool() {
    using namespace URK::Unreal;
    std::printf("\nFNamePool\n");

    FakeImage image(kImageBase, kPoolImageSize);
    BuildPoolImage(image);

    std::optional<NameTable> table = NameTable::Resolve(image, kImageBase + kPoolAddress);
    Check(table.has_value(), "pool layout resolves from a bare address");
    if (!table)
        return;

    Check(table->Layout().storage == NameStorage::Pool, "storage is identified as a pool");
    const NamePoolLayout &pool = table->Layout().pool;
    Check(pool.blockCountOffset == 0x00, "block count offset found");
    Check(pool.blocksOffset == 0x10, "block table offset found");
    Check(pool.entryStringOffset == 2, "entry header size found");
    Check(pool.entryStride == 2, "entry stride derived from header size");
    Check(pool.lengthShift == 1, "length shift calibrated from ByteProperty");

    const ObjectArray objects(image, kImageBase + kArrayAddress, PoolObjectArrayLayout());
    Check(objects.Num() == kObjectCount, "object array reads back");

    Check(pool.blockOffsetBits == 0xE, "block offset bits start at the default");
    table->CalibrateBlockOffsetBits(objects, kObjectNameOffset);
    Check(table->Layout().pool.blockOffsetBits == kBlockOffsetBits, "block offset bits raised to fit the second block");

    CheckName(table->Read(PoolIndex(0, kNoneEntry)), "None");
    CheckName(table->Read(PoolIndex(0, kBytePropertyEntry)), "ByteProperty");
    CheckName(table->Read(PoolIndex(0, kCoreUObjectEntry)), "/Script/CoreUObject");
    CheckName(table->Read(PoolIndex(0, kWideEntry)), "Wide");
    CheckName(table->Read(PoolIndex(1, kActorEntry)), "Actor");
    CheckName(table->Read(PoolIndex(0, kNumberedEntry)), "Class_2");

    CheckName(table->ObjectName(objects, kObjectNameOffset, 0), "None");
    CheckName(table->ObjectName(objects, kObjectNameOffset, 2), "Actor");
    // The suffix can come from the object's own FName number or from a numbered
    // pool entry; both must land on the same text.
    CheckName(table->ObjectName(objects, kObjectNameOffset, 3), "Class_2");
    CheckName(table->ObjectName(objects, kObjectNameOffset, 4), "Class_2");
    CheckName(table->ObjectName(objects, kObjectNameOffset, 5), "Wide");
}

void RunEntryArray() {
    using namespace URK::Unreal;
    std::printf("\nTNameEntryArray\n");

    FakeImage image(kImageBase, kEntryArrayImageSize);
    BuildEntryArrayImage(image);

    std::optional<NameTable> table = NameTable::Resolve(image, kImageBase + kEntryArrayAddress);
    Check(table.has_value(), "entry array layout resolves from a bare address");
    if (!table)
        return;

    Check(table->Layout().storage == NameStorage::EntryArray, "storage is identified as an entry array");
    Check(table->Layout().entries.numElementsOffset == 0x18, "element count offset found");
    Check(table->Layout().entries.entryStringOffset == kLegacyStringOffset, "entry string offset found");
    Check(table->Layout().entries.entryIndexOffset == 0x00, "entry index offset found");

    CheckName(table->Read(0), "None");
    CheckName(table->Read(1), "Class");
    CheckName(table->Read(8), "Eight");
    CheckName(table->Read(6), "World");
}

} // namespace

int main() {
    RunPool();
    RunEntryArray();

    if (g_failures == 0) {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
