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

// The NT headers of a plausible 64-bit image.
std::optional<Address> NtHeaders(const MemoryReader &reader, Address moduleBase) {
    if (moduleBase == kNullAddress)
        return std::nullopt;

    const std::optional<std::uint16_t> dosSignature = reader.ReadAs<std::uint16_t>(moduleBase);
    if (!dosSignature || *dosSignature != kDosSignature)
        return std::nullopt;

    const std::optional<std::uint32_t> lfanew = reader.ReadUInt32(moduleBase + kLfanewOffset);
    if (!lfanew || *lfanew < kLfanewOffset || *lfanew > kMaxLfanew)
        return std::nullopt;

    const Address ntHeaders = moduleBase + *lfanew;
    const std::optional<std::uint32_t> ntSignature = reader.ReadUInt32(ntHeaders);
    if (!ntSignature || *ntSignature != kNtSignature)
        return std::nullopt;

    const std::optional<std::uint16_t> magic = reader.ReadAs<std::uint16_t>(ntHeaders + kOptionalHeaderOffset);
    if (!magic || *magic != kPe32PlusMagic)
        return std::nullopt;
    return ntHeaders;
}

std::vector<ScanRegion> SectionsWhere(const MemoryReader &reader, Address moduleBase,
                                      bool (*wanted)(const ModuleSection &)) {
    std::vector<ScanRegion> regions;
    for (const ModuleSection &section : ReadModuleSections(reader, moduleBase)) {
        if (!wanted(section) || IsSkipped(section.name))
            continue;
        // A header can claim more than the loader committed.
        if (!reader.Readable(section.start, sizeof(Address)))
            continue;
        regions.push_back(ScanRegion{.start = section.start, .size = section.size, .writable = section.Writable()});
    }
    return regions;
}

// PE32+ optional header: SizeOfImage, and the exception entry of the data directory.
constexpr Address kSizeOfImageOffset = 0x38;
constexpr Address kExceptionDirectoryOffset = 0x70 + 3 * 8;
constexpr std::uint32_t kRuntimeFunctionSize = 12;
// UNWIND_INFO: flags in the top five bits of byte 0, code count in byte 2.
constexpr std::uint8_t kUnwindChainInfo = 0x4;
constexpr int kMaxChainDepth = 32;

} // namespace

bool ModuleSection::Readable() const { return (characteristics & kSectionRead) != 0; }

bool ModuleSection::Executable() const { return (characteristics & kSectionExecute) != 0; }

bool ModuleSection::Writable() const { return (characteristics & kSectionWrite) != 0; }

bool ModuleSection::HoldsData() const {
    return (characteristics & (kSectionInitializedData | kSectionUninitializedData)) != 0;
}

std::vector<ModuleSection> ReadModuleSections(const MemoryReader &reader, Address moduleBase) {
    std::vector<ModuleSection> sections;
    const std::optional<Address> nt = NtHeaders(reader, moduleBase);
    if (!nt)
        return sections;
    const Address ntHeaders = *nt;

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

std::vector<ScanRegion> ModuleCodeRegions(const MemoryReader &reader, Address moduleBase) {
    std::vector<ScanRegion> regions;
    for (const ModuleSection &section : ReadModuleSections(reader, moduleBase)) {
        if (!section.Executable())
            continue;
        regions.push_back(ScanRegion{.start = section.start, .size = section.size});
    }
    return regions;
}

std::vector<ScanRegion> ModuleDataRegions(const MemoryReader &reader, Address moduleBase) {
    return SectionsWhere(reader, moduleBase, [](const ModuleSection &section) {
        return section.Readable() && section.HoldsData() && !section.Executable();
    });
}

std::vector<ScanRegion> ModuleConstantRegions(const MemoryReader &reader, Address moduleBase) {
    return SectionsWhere(reader, moduleBase, [](const ModuleSection &section) {
        return section.Readable() && section.HoldsData() && !section.Executable() && !section.Writable();
    });
}

std::vector<ScanRegion> ModuleWritableRegions(const MemoryReader &reader, Address moduleBase) {
    return SectionsWhere(reader, moduleBase, [](const ModuleSection &section) {
        return section.Readable() && section.HoldsData() && !section.Executable() && section.Writable();
    });
}

Address FindModuleExport(const MemoryReader &reader, Address moduleBase, std::string_view name) {
    constexpr Address kExportDirectoryOffset = 0x70;
    constexpr std::uint32_t kMaxExports = 0x100000;
    const std::optional<Address> nt = NtHeaders(reader, moduleBase);
    if (!nt)
        return kNullAddress;
    const std::uint32_t directory = reader.ReadUInt32(*nt + kOptionalHeaderOffset + kExportDirectoryOffset).value_or(0);
    if (directory == 0)
        return kNullAddress;
    const Address table = moduleBase + directory;
    const std::uint32_t count = reader.ReadUInt32(table + 0x18).value_or(0);
    const Address functions = moduleBase + reader.ReadUInt32(table + 0x1C).value_or(0);
    const Address names = moduleBase + reader.ReadUInt32(table + 0x20).value_or(0);
    const Address ordinals = moduleBase + reader.ReadUInt32(table + 0x24).value_or(0);
    if (count == 0 || count > kMaxExports)
        return kNullAddress;
    std::vector<char> text(name.size() + 1);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint32_t at = reader.ReadUInt32(names + i * 4ull).value_or(0);
        if (at == 0 || !reader.Read(moduleBase + at, text.data(), text.size()) || text.back() != '\0' ||
            name != std::string_view(text.data(), name.size()))
            continue;
        const std::optional<std::uint16_t> ordinal = reader.ReadAs<std::uint16_t>(ordinals + i * 2ull);
        const std::uint32_t rva = ordinal ? reader.ReadUInt32(functions + *ordinal * 4ull).value_or(0) : 0;
        return rva != 0 ? moduleBase + rva : kNullAddress;
    }
    return kNullAddress;
}

FunctionTable FunctionTable::Read(const MemoryReader &reader, std::span<const Address> modules) {
    FunctionTable table;
    table.reader_ = &reader;
    for (const Address base : modules) {
        const std::optional<Address> nt = NtHeaders(reader, base);
        if (!nt)
            continue;
        const Address optional = *nt + kOptionalHeaderOffset;
        const std::optional<std::uint32_t> imageSize = reader.ReadUInt32(optional + kSizeOfImageOffset);
        const std::optional<std::uint32_t> rva = reader.ReadUInt32(optional + kExceptionDirectoryOffset);
        const std::optional<std::uint32_t> size = reader.ReadUInt32(optional + kExceptionDirectoryOffset + 4);
        if (!imageSize || !rva || !size || *rva == 0 || *size < kRuntimeFunctionSize)
            continue;
        if (static_cast<std::uint64_t>(*rva) + *size > *imageSize)
            continue;
        table.modules_.push_back(Module{.base = base,
                                        .imageEnd = base + *imageSize,
                                        .table = base + *rva,
                                        .count = *size / kRuntimeFunctionSize});
    }
    return table;
}

const FunctionTable::Module *FunctionTable::ModuleOf(Address address) const {
    for (const Module &module : modules_) {
        if (address >= module.base && address < module.imageEnd)
            return &module;
    }
    return nullptr;
}

std::optional<FunctionTable::Entry> FunctionTable::EntryAt(const Module &module, std::uint32_t index) const {
    return reader_->ReadAs<Entry>(module.table + static_cast<Address>(index) * kRuntimeFunctionSize);
}

// The table is sorted by begin address, as the unwinder requires.
std::optional<std::uint32_t> FunctionTable::LookupIndex(const Module &module, Address address) const {
    const std::uint64_t rva = address - module.base;
    std::uint32_t low = 0;
    std::uint32_t high = module.count;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2;
        const std::optional<Entry> entry = EntryAt(module, middle);
        if (!entry)
            return std::nullopt;
        if (rva < entry->begin)
            high = middle;
        else if (rva >= entry->end)
            low = middle + 1;
        else
            return middle;
    }
    return std::nullopt;
}

std::optional<FunctionTable::Entry> FunctionTable::Lookup(const Module &module, Address address) const {
    const std::optional<std::uint32_t> index = LookupIndex(module, address);
    return index ? EntryAt(module, *index) : std::nullopt;
}

std::optional<FunctionRange> FunctionTable::Containing(Address address) const {
    const Module *module = ModuleOf(address);
    if (!module)
        return std::nullopt;
    const std::optional<Entry> entry = Lookup(*module, address);
    if (!entry)
        return std::nullopt;
    return FunctionRange{.begin = module->base + entry->begin, .end = module->base + entry->end};
}

Address FunctionTable::NextBegin(Address address) const {
    const Module *module = ModuleOf(address);
    if (!module)
        return kNullAddress;
    const std::uint64_t rva = address - module->base;
    std::uint32_t low = 0;
    std::uint32_t high = module->count;
    while (low < high) {
        const std::uint32_t middle = low + (high - low) / 2;
        const std::optional<Entry> entry = EntryAt(*module, middle);
        if (!entry)
            return kNullAddress;
        if (entry->begin <= rva)
            low = middle + 1;
        else
            high = middle;
    }
    if (low == module->count)
        return kNullAddress;
    const std::optional<Entry> next = EntryAt(*module, low);
    return next ? module->base + next->begin : kNullAddress;
}

std::optional<std::uint32_t> FunctionTable::PrimaryRva(const Module &module, std::optional<Entry> entry) const {
    for (int depth = 0; entry && depth < kMaxChainDepth; ++depth) {
        // An odd unwind address points straight at the parent entry.
        if (entry->unwind & 1) {
            entry = reader_->ReadAs<Entry>(module.base + (entry->unwind & ~1u));
            continue;
        }
        const Address info = module.base + entry->unwind;
        const std::optional<std::uint8_t> flags = reader_->ReadAs<std::uint8_t>(info);
        const std::optional<std::uint8_t> codes = reader_->ReadAs<std::uint8_t>(info + 2);
        if (!flags || !codes)
            return std::nullopt;
        if (((*flags >> 3) & kUnwindChainInfo) == 0)
            return entry->begin;
        const Address parent = info + 4 + static_cast<Address>((*codes + 1) & ~1) * 2;
        entry = reader_->ReadAs<Entry>(parent);
    }
    return std::nullopt;
}

Address FunctionTable::PrimaryBegin(Address address) const {
    const Module *module = ModuleOf(address);
    if (!module)
        return kNullAddress;
    const std::optional<std::uint32_t> primary = PrimaryRva(*module, Lookup(*module, address));
    return primary ? module->base + *primary : kNullAddress;
}

std::vector<FunctionRange> FunctionTable::Pieces(Address address) const {
    // Far-off cold pieces are not searched for: that would walk the whole table.
    constexpr std::uint32_t kMaxPieces = 16;
    std::vector<FunctionRange> pieces;
    const Module *module = ModuleOf(address);
    if (!module)
        return pieces;
    const std::optional<std::uint32_t> primary = PrimaryRva(*module, Lookup(*module, address));
    const std::optional<std::uint32_t> first = primary ? LookupIndex(*module, module->base + *primary) : std::nullopt;
    if (!first)
        return pieces;
    for (std::uint32_t index = *first; index < module->count && index - *first < kMaxPieces; ++index) {
        const std::optional<Entry> entry = EntryAt(*module, index);
        if (!entry || (index != *first && PrimaryRva(*module, entry) != primary))
            break;
        pieces.push_back(FunctionRange{.begin = module->base + entry->begin, .end = module->base + entry->end});
    }
    return pieces;
}

} // namespace URK::Unreal
