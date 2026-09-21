#include "unreal_module.h"

#include <array>
#include <cstring>

namespace URK::Unreal {
namespace {

// PE fields are spelled out rather than taken from windows.h: the reader may be
// describing another process's image, and the tests describe no process at all.
constexpr std::uint16_t kDosSignature = 0x5A4D;      // "MZ"
constexpr std::uint32_t kNtSignature = 0x00004550;   // "PE\0\0"
constexpr std::uint16_t kPe32PlusMagic = 0x20B;

constexpr Address kLfanewOffset = 0x3C;
constexpr Address kFileHeaderOffset = 0x04;
constexpr Address kSectionCountOffset = kFileHeaderOffset + 0x02;
constexpr Address kOptionalHeaderSizeOffset = kFileHeaderOffset + 0x10;
constexpr Address kOptionalHeaderOffset = 0x18;

constexpr Address kSectionHeaderSize = 0x28;
constexpr Address kSectionNameSize = 8;
constexpr Address kSectionVirtualSizeOffset = 0x08;
constexpr Address kSectionVirtualAddressOffset = 0x0C;
constexpr Address kSectionRawSizeOffset = 0x10;
constexpr Address kSectionCharacteristicsOffset = 0x24;

constexpr std::uint32_t kSectionInitializedData = 0x00000040;
constexpr std::uint32_t kSectionUninitializedData = 0x00000080;
constexpr std::uint32_t kSectionExecute = 0x20000000;
constexpr std::uint32_t kSectionRead = 0x40000000;
constexpr std::uint32_t kSectionWrite = 0x80000000;

// Bounds that reject a header the scan wandered into rather than pin a linker.
constexpr std::uint16_t kMaxSections = 96;
constexpr std::uint32_t kMaxLfanew = 0x1000;
constexpr std::uint64_t kMaxSectionSize = 0x20000000;

// Sections the linker emits for its own use; an engine global is never in one.
constexpr std::array kSkippedSections = {".reloc", ".rsrc", ".pdata", ".idata", ".didat", ".edata"};

bool IsSkipped(const std::string &name) {
    for (const char *skipped : kSkippedSections) {
        if (name == skipped)
            return true;
    }
    return false;
}

std::string ReadSectionName(const MemoryReader &reader, Address header) {
    std::array<char, kSectionNameSize + 1> raw{};
    if (!reader.Read(header, raw.data(), kSectionNameSize))
        return {};
    return std::string(raw.data(), std::strlen(raw.data()));
}

} // namespace

bool ModuleSection::Readable() const { return (characteristics & kSectionRead) != 0; }

bool ModuleSection::Executable() const { return (characteristics & kSectionExecute) != 0; }

bool ModuleSection::Writable() const { return (characteristics & kSectionWrite) != 0; }

bool ModuleSection::HoldsData() const {
    return (characteristics & (kSectionInitializedData | kSectionUninitializedData)) != 0;
}

std::vector<ModuleSection> ReadModuleSections(const MemoryReader &reader, Address moduleBase) {
    std::vector<ModuleSection> sections;
    if (moduleBase == kNullAddress)
        return sections;

    const std::optional<std::uint16_t> dosSignature = reader.ReadAs<std::uint16_t>(moduleBase);
    if (!dosSignature || *dosSignature != kDosSignature)
        return sections;

    const std::optional<std::uint32_t> lfanew = reader.ReadUInt32(moduleBase + kLfanewOffset);
    if (!lfanew || *lfanew < kLfanewOffset || *lfanew > kMaxLfanew)
        return sections;

    const Address ntHeaders = moduleBase + *lfanew;
    const std::optional<std::uint32_t> ntSignature = reader.ReadUInt32(ntHeaders);
    if (!ntSignature || *ntSignature != kNtSignature)
        return sections;

    const std::optional<std::uint16_t> magic = reader.ReadAs<std::uint16_t>(ntHeaders + kOptionalHeaderOffset);
    if (!magic || *magic != kPe32PlusMagic)
        return sections;

    const std::optional<std::uint16_t> count = reader.ReadAs<std::uint16_t>(ntHeaders + kSectionCountOffset);
    const std::optional<std::uint16_t> optionalSize =
        reader.ReadAs<std::uint16_t>(ntHeaders + kOptionalHeaderSizeOffset);
    if (!count || !optionalSize || *count == 0 || *count > kMaxSections)
        return sections;

    const Address table = ntHeaders + kOptionalHeaderOffset + *optionalSize;
    for (std::uint16_t index = 0; index < *count; ++index) {
        const Address header = table + static_cast<Address>(index) * kSectionHeaderSize;

        const std::optional<std::uint32_t> virtualSize = reader.ReadUInt32(header + kSectionVirtualSizeOffset);
        const std::optional<std::uint32_t> virtualAddress = reader.ReadUInt32(header + kSectionVirtualAddressOffset);
        const std::optional<std::uint32_t> rawSize = reader.ReadUInt32(header + kSectionRawSizeOffset);
        const std::optional<std::uint32_t> characteristics =
            reader.ReadUInt32(header + kSectionCharacteristicsOffset);
        if (!virtualSize || !virtualAddress || !rawSize || !characteristics)
            break;

        // Mapped, so the virtual size rules; .bss has no raw bytes at all.
        const std::uint64_t size = *virtualSize != 0 ? *virtualSize : *rawSize;
        if (size == 0 || size > kMaxSectionSize || *virtualAddress == 0)
            continue;

        sections.push_back(ModuleSection{.name = ReadSectionName(reader, header),
                                         .start = moduleBase + *virtualAddress,
                                         .size = size,
                                         .characteristics = *characteristics});
    }

    return sections;
}

std::vector<ScanRegion> ModuleDataRegions(const MemoryReader &reader, Address moduleBase) {
    std::vector<ScanRegion> regions;
    for (const ModuleSection &section : ReadModuleSections(reader, moduleBase)) {
        if (!section.Readable() || section.Executable() || !section.HoldsData())
            continue;
        if (IsSkipped(section.name))
            continue;
        // A section header can claim more than the loader committed, and the
        // first bytes are what every probe starts from.
        if (!reader.Readable(section.start, sizeof(Address)))
            continue;
        regions.push_back(ScanRegion{.start = section.start, .size = section.size});
    }
    return regions;
}

} // namespace URK::Unreal
