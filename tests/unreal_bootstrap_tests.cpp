// Live-process bootstrap against a synthetic module.
//
// Nothing here hands the calibration an address: a module image with PE headers
// is built, its globals are buried in .data among structures meant to be
// mistaken for them, and the bootstrap has to come back with the pair. The
// decoys are what the test is for - a stale object array that satisfies every
// structural invariant but whose objects no longer name themselves, a block
// table that reaches "None" without being a name pool, and copies of the live
// array in code and in a packer's RWX section, neither of which is a section an
// engine global lives in.
//
// Two module layouts are built, differing in engine version and in where in
// .data each global sits, because one layout alone would pass for an
// implementation that simply walked to a fixed place.

#include "src/unreal/unreal_bootstrap.h"
#include "tests/unreal_fake_image.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("  %-72s %s\n", what.c_str(), condition ? "ok" : "FAILED");
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
using URK::Unreal::kNullAddress;
using URK::Unreal::ScanRegion;

// The module and the allocations its globals point at are separate mappings, as
// they are in a process: a pointer that leaves one must land nowhere.
class ProcessImage : public URK::Unreal::MemoryReader {
  public:
    ProcessImage(const FakeImage &module, const FakeImage &heap) : module_(&module), heap_(&heap) {}

    bool Read(Address address, void *out, std::size_t size) const override {
        return module_->Read(address, out, size) || heap_->Read(address, out, size);
    }

    bool Readable(Address address, std::size_t size) const override {
        return module_->Readable(address, size) || heap_->Readable(address, size);
    }

  private:
    const FakeImage *module_;
    const FakeImage *heap_;
};

// ------------------------------------------------------------------ module

constexpr Address kModuleBase = 0x140000000;
constexpr Address kHeapBase = 0x00007FF000000000;

constexpr Address kNtHeaders = 0x80;
constexpr std::uint16_t kOptionalHeaderSize = 0xF0;
constexpr Address kSectionTable = kNtHeaders + 0x18 + kOptionalHeaderSize;

constexpr Address kTextAddress = 0x1000;
constexpr Address kRdataAddress = 0x2000;
constexpr Address kDataAddress = 0x3000;
constexpr Address kDataSize = 0x18000;
constexpr Address kPackedAddress = kDataAddress + kDataSize;
constexpr Address kRelocAddress = kPackedAddress + 0x1000;
constexpr std::size_t kModuleImageSize = kRelocAddress + 0x1000;

constexpr std::uint32_t kTextCharacteristics = 0x60000020;   // code, execute, read
constexpr std::uint32_t kRdataCharacteristics = 0x40000040;  // initialized data, read
constexpr std::uint32_t kDataCharacteristics = 0xC0000040;   // initialized data, read, write
constexpr std::uint32_t kPackedCharacteristics = 0xE0000040; // initialized data, read, write, execute
constexpr std::uint32_t kRelocCharacteristics = 0x42000040;  // initialized data, read, discardable

void PutSection(FakeImage &image, int index, const char *name, Address virtualAddress, std::uint32_t virtualSize,
                std::uint32_t characteristics) {
    const Address header = kSectionTable + static_cast<Address>(index) * 0x28;
    image.PutBytes(header, name, std::strlen(name));
    image.Put<std::uint32_t>(header + 0x08, virtualSize);
    image.Put<std::uint32_t>(header + 0x0C, static_cast<std::uint32_t>(virtualAddress));
    image.Put<std::uint32_t>(header + 0x10, virtualSize);
    image.Put<std::uint32_t>(header + 0x24, characteristics);
}

void PutHeaders(FakeImage &image) {
    image.Put<std::uint16_t>(0x00, 0x5A4D);
    image.Put<std::uint32_t>(0x3C, static_cast<std::uint32_t>(kNtHeaders));

    image.Put<std::uint32_t>(kNtHeaders, 0x00004550);
    image.Put<std::uint16_t>(kNtHeaders + 0x04, 0x8664);
    image.Put<std::uint16_t>(kNtHeaders + 0x06, 5);
    image.Put<std::uint16_t>(kNtHeaders + 0x14, kOptionalHeaderSize);
    image.Put<std::uint16_t>(kNtHeaders + 0x18, 0x20B);

    PutSection(image, 0, ".text", kTextAddress, 0x1000, kTextCharacteristics);
    PutSection(image, 1, ".rdata", kRdataAddress, 0x1000, kRdataCharacteristics);
    PutSection(image, 2, ".data", kDataAddress, static_cast<std::uint32_t>(kDataSize), kDataCharacteristics);
    // What a packer leaves behind: data characteristics on a section that is
    // also executable, so excluding code cannot come down to those alone.
    PutSection(image, 3, ".pack", kPackedAddress, 0x1000, kPackedCharacteristics);
    PutSection(image, 4, ".reloc", kRelocAddress, 0x1000, kRelocCharacteristics);
}

// -------------------------------------------------------------------- world

// What a module is built to, and so the answer the bootstrap must reach.
struct ModuleSpec {
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

    // Where in .data each global lands, so no run can pass by position.
    Address poolOffset;
    Address arrayOffset;
    Address staleArrayOffset;
    Address decoyPoolOffset;
};

constexpr std::int32_t kObjectCount = 0x1000;
constexpr std::int32_t kElementsPerChunk = 0x8000;
constexpr std::int32_t kMaxChunks = 8;

constexpr ModuleSpec kUe58Module{
    .name = "UE5.8 count order, globals early in .data",
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
    .poolOffset = 0x00000,
    .arrayOffset = 0x11000,
    .staleArrayOffset = 0x11800,
    .decoyPoolOffset = 0x12000,
};

constexpr ModuleSpec kUe57Module{
    .name = "pre-5.8 count order, globals late in .data",
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
    .poolOffset = 0x06000,
    .arrayOffset = 0x00800,
    .staleArrayOffset = 0x00100,
    .decoyPoolOffset = 0x17000,
};

// Heap regions. The two graphs are kept apart so the stale one cannot be
// reached by walking off the end of the live one.
constexpr Address kChunkTable = 0x000000;
constexpr Address kItems = 0x001000;
constexpr Address kObjects = 0x022000;
constexpr Address kBlockZero = 0x063000;
constexpr Address kBlockOne = 0x064000;
constexpr Address kStaleChunkTable = 0x066000;
constexpr Address kStaleItems = 0x067000;
constexpr Address kStaleObjects = 0x080000;
constexpr std::size_t kHeapImageSize = 0x0C1000;

constexpr Address kVTable = kBlockZero - 0x100;

// Entries pack at the pool two-byte stride, and a comparison index is the block
// number shifted up plus the byte offset halved.
constexpr std::int32_t kBlockOffsetBits = 0x10;

// Where the stale graph points: real entry headers, in a part of block one that
// holds no names.
constexpr Address kGarbageEntries = 0x800;
constexpr std::uint16_t kGarbageHeader = 0xBEEF;

// The names the bootstrap has to read back. The anchors CoreUObject registers
// come first, as they do in a process.
constexpr const char *kObjectNames[] = {
    "Object",      "Class",     "Package",  "Function",   "Field",        "Struct",
    "Actor",       "Pawn",      "Level",    "World",      "Controller",   "PlayerController",
    "Transform",   "Guid",      "Color",    "Vector",     "GameInstance", "KismetSystemLibrary",
};
constexpr std::size_t kObjectNameCount = sizeof(kObjectNames) / sizeof(kObjectNames[0]);

class NameBlocks {
  public:
    explicit NameBlocks(FakeImage &image) : image_(&image) {
        // What the pool calibration keys on, in the order it expects.
        Put(kBlockZero, 0x00, "None");
        Put(kBlockZero, 0x06, "ByteProperty");
        Put(kBlockZero, 0x14, "/Script/CoreUObject");

        for (std::size_t i = 0; i < kObjectNameCount; ++i)
            names_.push_back(Append(kObjectNames[i]));

        for (Address offset = kGarbageEntries; offset < 0x1000; offset += 8)
            image_->Put<std::uint16_t>(kBlockOne + offset, kGarbageHeader);
    }

    std::uint32_t IndexOf(std::size_t index) const { return names_[index % names_.size()]; }

    // An index into the part of block one that holds headers but no names.
    static std::uint32_t GarbageIndex(std::int32_t index) {
        const Address offset = kGarbageEntries + static_cast<Address>(index % 0x100) * 8;
        return (1u << kBlockOffsetBits) + static_cast<std::uint32_t>(offset / 2);
    }

  private:
    void Put(Address block, Address offset, const char *text) {
        const auto length = static_cast<std::uint16_t>(std::strlen(text));
        image_->Put<std::uint16_t>(block + offset, static_cast<std::uint16_t>(length << 1));
        image_->PutBytes(block + offset + 2, text, length);
    }

    // Object names live in block one, which is what forces the block width to
    // be calibrated rather than left at its default.
    std::uint32_t Append(const char *text) {
        const Address offset = cursor_;
        Put(kBlockOne, offset, text);
        cursor_ = (cursor_ + 2 + static_cast<Address>(std::strlen(text)) + 1) & ~Address{1};
        return (1u << kBlockOffsetBits) + static_cast<std::uint32_t>(offset / 2);
    }

    FakeImage *image_;
    Address cursor_ = 0;
    std::vector<std::uint32_t> names_;
};

Address ObjectAddressAt(const ModuleSpec &spec, Address region, std::int32_t index) {
    return kHeapBase + region + static_cast<Address>(index) * spec.objectStride;
}

// One object graph: items, objects and the header fields the calibration is to
// recover. Only the names differ between the live graph and the stale one.
void BuildGraph(FakeImage &heap, const ModuleSpec &spec, const NameBlocks &names, Address itemsRegion,
                Address objectsRegion, bool stale) {
    for (std::int32_t index = 0; index < kObjectCount; ++index) {
        const Address item = itemsRegion + static_cast<Address>(index) * spec.itemStride;
        heap.Put<Address>(item + spec.itemPointerOffset, ObjectAddressAt(spec, objectsRegion, index));

        const Address object = objectsRegion + static_cast<Address>(index) * spec.objectStride;
        heap.Put<Address>(object, kHeapBase + kVTable);
        heap.Put<std::uint32_t>(object + spec.flagsOffset, (index % 8 == 0) ? 0x41u : 0x43u);
        heap.Put<std::int32_t>(object + spec.indexOffset, index);
        heap.Put<Address>(object + spec.classOffset,
                          ObjectAddressAt(spec, objectsRegion, index <= 3 ? 2 : 3));
        heap.Put<Address>(object + spec.outerOffset,
                          index < 2 ? 0 : ObjectAddressAt(spec, objectsRegion, 1));

        const std::uint32_t name =
            stale ? NameBlocks::GarbageIndex(index) : names.IndexOf(static_cast<std::size_t>(index));
        heap.Put<std::uint32_t>(object + spec.nameOffset, name);
        heap.Put<std::uint32_t>(object + spec.nameOffset + 4, 0u);
    }
}

void PutObjectArray(FakeImage &image, Address at, const ModuleSpec &spec, Address chunkTable) {
    image.Put<Address>(at + spec.array.objectsOffset, kHeapBase + chunkTable);
    image.Put<std::int32_t>(at + spec.array.numElementsOffset, kObjectCount);
    image.Put<std::int32_t>(at + spec.array.maxElementsOffset, kElementsPerChunk * kMaxChunks);
    image.Put<std::int32_t>(at + spec.array.numChunksOffset, (kObjectCount / kElementsPerChunk) + 1);
    image.Put<std::int32_t>(at + spec.array.maxChunksOffset, kMaxChunks);
}

void BuildHeap(FakeImage &heap, const ModuleSpec &spec) {
    // Every object starts with a vtable; that is what tells one from unrelated
    // mapped data, so the graphs share one.
    heap.Put<Address>(kVTable, kHeapBase);

    const NameBlocks names(heap);

    heap.Put<Address>(kChunkTable, kHeapBase + kItems);
    heap.Put<Address>(kStaleChunkTable, kHeapBase + kStaleItems);

    BuildGraph(heap, spec, names, kItems, kObjects, false);
    BuildGraph(heap, spec, names, kStaleItems, kStaleObjects, true);
}

void BuildModule(FakeImage &module, const ModuleSpec &spec) {
    PutHeaders(module);

    const Address pool = kDataAddress + spec.poolOffset;
    module.Put<std::int32_t>(pool + 0x00, 1); // one block past block zero
    module.Put<std::int32_t>(pool + 0x04, 0x400);
    module.Put<Address>(pool + 0x10, kHeapBase + kBlockZero);
    module.Put<Address>(pool + 0x18, kHeapBase + kBlockOne);

    PutObjectArray(module, kDataAddress + spec.arrayOffset, spec, kChunkTable);
    PutObjectArray(module, kDataAddress + spec.staleArrayOffset, spec, kStaleChunkTable);

    // A block table that reaches "None" without being a name pool: its count
    // does not match the pointers behind it.
    const Address decoy = kDataAddress + spec.decoyPoolOffset;
    module.Put<std::int32_t>(decoy + 0x00, 7);
    module.Put<Address>(decoy + 0x10, kHeapBase + kBlockZero);
    module.Put<Address>(decoy + 0x18, kHeapBase + kBlockOne);

    // Copies of the live array where the engine keeps no globals.
    PutObjectArray(module, kTextAddress + 0x100, spec, kChunkTable);
    PutObjectArray(module, kPackedAddress + 0x100, spec, kChunkTable);
}

bool Contains(const std::vector<Address> &addresses, Address address) {
    return std::find(addresses.begin(), addresses.end(), address) != addresses.end();
}

void Run(const ModuleSpec &spec) {
    using namespace URK::Unreal;

    std::printf("\n%s\n", spec.name);

    FakeImage module(kModuleBase, kModuleImageSize);
    FakeImage heap(kHeapBase, kHeapImageSize);
    BuildModule(module, spec);
    BuildHeap(heap, spec);
    const ProcessImage process(module, heap);

    const Address poolAddress = kModuleBase + kDataAddress + spec.poolOffset;
    const Address arrayAddress = kModuleBase + kDataAddress + spec.arrayOffset;
    const Address staleAddress = kModuleBase + kDataAddress + spec.staleArrayOffset;

    const std::vector<ModuleSection> sections = ReadModuleSections(process, kModuleBase);
    Check(sections.size() == 5, "every section header is read from the mapped image");

    const std::vector<ScanRegion> regions = ModuleDataRegions(process, kModuleBase);
    Check(regions.size() == 2, "only the two data sections are handed to the scan");
    const bool dataCovered =
        std::any_of(regions.begin(), regions.end(), [](const ScanRegion &region) {
            return region.start == kModuleBase + kDataAddress && region.size == kDataSize;
        });
    Check(dataCovered, ".data is scanned whole");
    Check(std::none_of(regions.begin(), regions.end(),
                       [](const ScanRegion &region) {
                           return region.start == kModuleBase + kTextAddress ||
                                  region.start == kModuleBase + kPackedAddress;
                       }),
          "no section that can be executed is scanned");
    Check(std::none_of(regions.begin(), regions.end(),
                       [](const ScanRegion &region) { return region.start == kModuleBase + kRelocAddress; }),
          "link-support sections are not scanned");

    const std::vector<Address> arrays = FindObjectArrayCandidates(process, regions);
    Check(Contains(arrays, arrayAddress), "the live array is found by scanning alone");
    Check(Contains(arrays, staleAddress), "the stale array passes the structural invariants too");
    Check(!Contains(arrays, kModuleBase + kTextAddress + 0x100), "the copy in code is never reached");
    Check(!Contains(arrays, kModuleBase + kPackedAddress + 0x100),
          "nor the copy in a section a packer left executable");

    const std::vector<Address> tables = FindNameTableCandidates(process, regions);
    Check(Contains(tables, poolAddress), "the name pool is found by scanning alone");
    Check(!Contains(tables, kModuleBase + kDataAddress + spec.decoyPoolOffset),
          "a block table whose count does not match its pointers is rejected");

    const std::optional<Runtime> runtime = BootstrapModule(process, kModuleBase);
    Check(runtime.has_value(), "the bootstrap reaches a calibrated pair from the module base alone");
    if (!runtime)
        return;

    Check(runtime->objectArrayAddress == arrayAddress, "the live array is the one paired with the pool");
    Check(runtime->objectArrayAddress != staleAddress, "the stale array is rejected by its names, not its shape");
    Check(runtime->nameTableAddress == poolAddress, "the pool is the table the pair settled on");

    CheckOffset("UObject::Flags", runtime->header.flags, spec.flagsOffset);
    CheckOffset("UObject::InternalIndex", runtime->header.index, spec.indexOffset);
    CheckOffset("UObject::ClassPrivate", runtime->header.classPointer, spec.classOffset);
    CheckOffset("UObject::OuterPrivate", runtime->header.outer, spec.outerOffset);
    CheckOffset("UObject::NamePrivate", runtime->header.name, spec.nameOffset);

    Check(runtime->names.Layout().pool.blockOffsetBits == kBlockOffsetBits,
          "the block width was calibrated against the array it was paired with");
    Check(runtime->objects.Num() == kObjectCount, "the object count reads back through the recovered layout");

    // The point of the pairing: objects name themselves without anything having
    // been told where either global is.
    const std::optional<std::string> first = runtime->names.ObjectName(runtime->objects, runtime->header.name, 0);
    Check(first && *first == kObjectNames[0], "object zero names itself through the recovered pair");
    const std::optional<std::string> last =
        runtime->names.ObjectName(runtime->objects, runtime->header.name, kObjectNameCount - 1);
    Check(last && *last == kObjectNames[kObjectNameCount - 1], "the last distinct name reads back as well");
    Check(runtime->confirmedNames > 0xC0, "nearly every sampled object resolved to a name");
}

} // namespace

int main() {
    Run(kUe58Module);
    Run(kUe57Module);

    if (g_failures == 0) {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
