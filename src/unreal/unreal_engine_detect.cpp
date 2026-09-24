#include "unreal_engine_detect.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <optional>
#include <vector>

namespace URK::Unreal {
namespace {

// Spelled out: the image may belong to another process, or to none.
constexpr std::uint16_t kDosSignature = 0x5A4D;
constexpr std::uint32_t kNtSignature = 0x00004550;
constexpr std::uint16_t kPe32PlusMagic = 0x20B;

constexpr Address kLfanewOffset = 0x3C;
constexpr Address kOptionalHeaderOffset = 0x18;
constexpr Address kPe32PlusDataDirectoryOffset = 0x70;
constexpr std::uint32_t kResourceDirectoryIndex = 2;

constexpr Address kResourceDirectoryHeaderSize = 0x10;
constexpr Address kResourceNamedCountOffset = 0x0C;
constexpr Address kResourceIdCountOffset = 0x0E;
constexpr Address kResourceEntrySize = 0x08;
constexpr std::uint32_t kResourceSubdirectoryFlag = 0x80000000;
constexpr std::uint32_t kResourceTypeVersion = 16;

constexpr std::uint32_t kMaxLfanew = 0x1000;
constexpr std::uint32_t kMaxResourceEntries = 1024;
// Room to spare for a few hundred bytes, and a bound on a wrong guess.
constexpr std::uint32_t kMaxVersionBytes = 0x4000;

// ProductVersion is the engine's; FileVersion usually agrees.
constexpr const char *kVersionKeys[] = {"ProductVersion", "FileVersion"};

struct ResourceEntry {
    std::uint32_t id = 0;
    std::uint32_t offset = 0;
    bool directory = false;
};

std::vector<ResourceEntry> ReadResourceEntries(const MemoryReader &reader, Address directory) {
    std::vector<ResourceEntry> entries;

    const std::optional<std::uint16_t> named = reader.ReadAs<std::uint16_t>(directory + kResourceNamedCountOffset);
    const std::optional<std::uint16_t> ids = reader.ReadAs<std::uint16_t>(directory + kResourceIdCountOffset);
    if (!named || !ids)
        return entries;

    const std::uint32_t total = static_cast<std::uint32_t>(*named) + *ids;
    if (total == 0 || total > kMaxResourceEntries)
        return entries;

    for (std::uint32_t i = 0; i < total; ++i) {
        const Address at = directory + kResourceDirectoryHeaderSize + i * kResourceEntrySize;
        const std::optional<std::uint32_t> name = reader.ReadUInt32(at);
        const std::optional<std::uint32_t> offset = reader.ReadUInt32(at + 4);
        if (!name || !offset)
            break;
        entries.push_back({*name, *offset & ~kResourceSubdirectoryFlag,
                           (*offset & kResourceSubdirectoryFlag) != 0});
    }
    return entries;
}

// The first child, or the one carrying this id when one is asked for.
bool DescendResource(const MemoryReader &reader, Address resourceBase, Address directory, const std::uint32_t *wantId,
                     Address &into) {
    for (const ResourceEntry &entry : ReadResourceEntries(reader, directory)) {
        if (wantId && entry.id != *wantId)
            continue;
        into = resourceBase + entry.offset;
        return true;
    }
    return false;
}

// UTF-16 in the resource, compared against ASCII without pulling in a locale.
bool WideEquals(const std::vector<std::uint8_t> &blob, std::size_t at, const char *text) {
    for (std::size_t i = 0;; ++i) {
        const std::size_t byte = at + i * 2;
        if (byte + 1 >= blob.size())
            return false;
        const std::uint16_t unit = static_cast<std::uint16_t>(blob[byte] | (blob[byte + 1] << 8));
        if (text[i] == '\0')
            return unit == 0;
        if (unit != static_cast<std::uint16_t>(text[i]))
            return false;
    }
}

std::string ReadWide(const std::vector<std::uint8_t> &blob, std::size_t at) {
    std::string text;
    for (std::size_t byte = at; byte + 1 < blob.size(); byte += 2) {
        const std::uint16_t unit = static_cast<std::uint16_t>(blob[byte] | (blob[byte + 1] << 8));
        if (unit == 0)
            break;
        // UBT writes ASCII here; anything else is not a version string.
        if (unit > 0x7F)
            return {};
        text.push_back(static_cast<char>(unit));
        if (text.size() > 256)
            break;
    }
    return text;
}

// Searched inside the located blob rather than by walking String structures:
// a few hundred bytes, already the right ones.
std::string ValueForKey(const std::vector<std::uint8_t> &blob, const char *key) {
    const std::size_t keyUnits = std::strlen(key);
    for (std::size_t at = 0; at + (keyUnits + 1) * 2 < blob.size(); at += 2) {
        if (!WideEquals(blob, at, key))
            continue;

        // The value follows the key's terminator, padded to a 4-byte boundary.
        std::size_t value = at + (keyUnits + 1) * 2;
        value = (value + 3) & ~std::size_t{3};
        const std::string text = ReadWide(blob, value);
        if (!text.empty())
            return text;
    }
    return {};
}

bool StartsWith(const std::string &text, const char *prefix) { return text.rfind(prefix, 0) == 0; }

std::string Lowered(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool Contains(const std::string &text, const char *needle) { return text.find(needle) != std::string::npos; }

// Digits at a position, stopping at the first thing that is not one.
std::int64_t ReadNumber(const std::string &text, std::size_t &at) {
    std::int64_t value = -1;
    while (at < text.size() && text[at] >= '0' && text[at] <= '9') {
        value = (value < 0 ? 0 : value) * 10 + (text[at] - '0');
        ++at;
        if (value > 0xFFFFFFFFLL)
            break;
    }
    return value;
}

// Engine module names in a split build; editor and modular games both prefix them.
bool LooksLikeEngineModule(const std::string &lowered, const char *suffix) {
    if (!Contains(lowered, suffix))
        return false;
    return StartsWith(lowered, "unrealeditor-") || StartsWith(lowered, "ue4editor-") ||
           StartsWith(lowered, "ue5editor-") || StartsWith(lowered, "unrealgame-") ||
           Contains(lowered, "-core.dll") || Contains(lowered, "-coreuobject.dll");
}

} // namespace

std::string ReadModuleVersionString(const MemoryReader &reader, Address moduleBase) {
    if (moduleBase == kNullAddress)
        return {};

    const std::optional<std::uint16_t> dos = reader.ReadAs<std::uint16_t>(moduleBase);
    if (!dos || *dos != kDosSignature)
        return {};

    const std::optional<std::uint32_t> lfanew = reader.ReadUInt32(moduleBase + kLfanewOffset);
    if (!lfanew || *lfanew < kLfanewOffset || *lfanew > kMaxLfanew)
        return {};

    const Address ntHeaders = moduleBase + *lfanew;
    const std::optional<std::uint32_t> pe = reader.ReadUInt32(ntHeaders);
    if (!pe || *pe != kNtSignature)
        return {};

    const Address optionalHeader = ntHeaders + kOptionalHeaderOffset;
    const std::optional<std::uint16_t> magic = reader.ReadAs<std::uint16_t>(optionalHeader);
    if (!magic || *magic != kPe32PlusMagic)
        return {};

    const Address directoryEntry =
        optionalHeader + kPe32PlusDataDirectoryOffset + kResourceDirectoryIndex * 8;
    const std::optional<std::uint32_t> resourceRva = reader.ReadUInt32(directoryEntry);
    const std::optional<std::uint32_t> resourceSize = reader.ReadUInt32(directoryEntry + 4);
    if (!resourceRva || *resourceRva == 0 || !resourceSize || *resourceSize == 0)
        return {};

    const Address resourceBase = moduleBase + *resourceRva;

    // Three levels: type, name, language. Only the type is asked for by id.
    constexpr std::uint32_t versionType = kResourceTypeVersion;
    Address byName = kNullAddress;
    if (!DescendResource(reader, resourceBase, resourceBase, &versionType, byName))
        return {};
    Address byLanguage = kNullAddress;
    if (!DescendResource(reader, resourceBase, byName, nullptr, byLanguage))
        return {};
    Address leaf = kNullAddress;
    if (!DescendResource(reader, resourceBase, byLanguage, nullptr, leaf))
        return {};

    // The leaf points at the data with an RVA, not with a resource offset.
    const std::optional<std::uint32_t> dataRva = reader.ReadUInt32(leaf);
    const std::optional<std::uint32_t> dataSize = reader.ReadUInt32(leaf + 4);
    if (!dataRva || !dataSize || *dataSize == 0)
        return {};

    const std::uint32_t size = std::min(*dataSize, kMaxVersionBytes);
    std::vector<std::uint8_t> blob(size, 0);
    if (!reader.Read(moduleBase + *dataRva, blob.data(), size))
        return {};

    for (const char *key : kVersionKeys) {
        const std::string value = ValueForKey(blob, key);
        if (!value.empty())
            return value;
    }
    return {};
}

EngineVersion ParseEngineVersion(const std::string &text) {
    EngineVersion version;
    version.branch = text;
    version.source = "the version resource";
    if (text.empty())
        return version;

    // "...+Release-5.8-CL-56702186": what the engine's own branch says.
    const std::size_t release = text.find("Release-");
    if (release != std::string::npos) {
        std::size_t at = release + 8;
        const std::int64_t major = ReadNumber(text, at);
        if (major > 0 && at < text.size() && text[at] == '.') {
            ++at;
            const std::int64_t minor = ReadNumber(text, at);
            if (minor >= 0) {
                version.major = static_cast<std::int32_t>(major);
                version.minor = static_cast<std::int32_t>(minor);
            }
        }
    }

    // "5.3.2-29314046+++UE5...": carries the patch the branch form does not.
    if (!version.Known() || version.patch == 0) {
        std::size_t at = 0;
        const std::int64_t major = ReadNumber(text, at);
        if (major > 0 && at < text.size() && text[at] == '.') {
            ++at;
            const std::int64_t minor = ReadNumber(text, at);
            std::int64_t patch = 0;
            if (at < text.size() && text[at] == '.') {
                ++at;
                patch = ReadNumber(text, at);
            }
            if (minor >= 0) {
                if (!version.Known()) {
                    version.major = static_cast<std::int32_t>(major);
                    version.minor = static_cast<std::int32_t>(minor);
                }
                if (patch > 0)
                    version.patch = static_cast<std::int32_t>(patch);
            }
        }
    }

    const std::size_t changelist = text.find("CL-");
    if (changelist != std::string::npos) {
        std::size_t at = changelist + 3;
        const std::int64_t value = ReadNumber(text, at);
        if (value > 0)
            version.changelist = static_cast<std::uint64_t>(value);
    }
    return version;
}

namespace {

// UBT's on-disk layout, which a packed title keeps even when its version
// resource is stripped - the loader still needs the paths.
struct LayoutEvidence {
    bool engineBinaries = false;
    bool targetBinaries = false;
    bool buildToolName = false;

    bool Any() const { return engineBinaries || targetBinaries || buildToolName; }
};

LayoutEvidence ReadLayout(std::span<const ModuleCandidate> modules) {
    LayoutEvidence evidence;
    for (const ModuleCandidate &module : modules) {
        const std::string path = Lowered(module.path);
        if (Contains(path, "\\engine\\binaries\\"))
            evidence.engineBinaries = true;
        if (Contains(path, "\\binaries\\win64\\") || Contains(path, "\\binaries\\wingdk\\"))
            evidence.targetBinaries = true;
    }

    const std::string name = Lowered(modules.front().name);
    evidence.buildToolName = Contains(name, "-win64-shipping") || Contains(name, "-win64-test") ||
                             Contains(name, "-win64-development") || Contains(name, "-wingdk-shipping");
    return evidence;
}

} // namespace

UnrealPresence DetectUnreal(const MemoryReader &reader, std::span<const ModuleCandidate> modules) {
    UnrealPresence presence;
    if (modules.empty()) {
        presence.reason = "no modules to look at";
        return presence;
    }

    // Split build: the object array is CoreUObject's, the name pool Core's,
    // so both have to be scanned.
    Address core = kNullAddress;
    Address coreUObject = kNullAddress;
    for (const ModuleCandidate &module : modules) {
        const std::string lowered = Lowered(module.name);
        if (coreUObject == kNullAddress && LooksLikeEngineModule(lowered, "coreuobject"))
            coreUObject = module.base;
        else if (core == kNullAddress && LooksLikeEngineModule(lowered, "core"))
            core = module.base;
    }

    if (coreUObject != kNullAddress) {
        presence.confidence = DetectionConfidence::Confirmed;
        presence.layout = UnrealLayout::Modular;
        presence.runtimeModules.push_back(coreUObject);
        if (core != kNullAddress)
            presence.runtimeModules.push_back(core);
        presence.version = ParseEngineVersion(ReadModuleVersionString(reader, coreUObject));
        presence.reason = "engine modules loaded";
        return presence;
    }

    // Otherwise the whole engine is in one image, and the first module is it.
    const ModuleCandidate &main = modules.front();
    const std::string versionText = ReadModuleVersionString(reader, main.base);
    presence.version = ParseEngineVersion(versionText);

    const std::string loweredVersion = Lowered(versionText);

    // A branch string is the strong signal: only UBT stamps one.
    const bool branded = Contains(loweredVersion, "++ue4") || Contains(loweredVersion, "++ue5") ||
                         Contains(loweredVersion, "+ue4") || Contains(loweredVersion, "+ue5");

    const LayoutEvidence layout = ReadLayout(modules);

    if (branded) {
        presence.confidence = DetectionConfidence::Confirmed;
        presence.layout = UnrealLayout::Monolithic;
        presence.runtimeModules.push_back(main.base);
        presence.reason = layout.buildToolName ? "engine branch in the version resource, and a target name to match"
                                               : "engine branch in the version resource";
        return presence;
    }

    // No branch string: a packed title and a non-Unreal process look alike, so
    // the layout only earns a maybe and the bootstrap settles it.
    if (layout.Any()) {
        presence.confidence = DetectionConfidence::Possible;
        presence.layout = UnrealLayout::Monolithic;
        presence.runtimeModules.push_back(main.base);
        presence.reason = layout.engineBinaries && layout.targetBinaries
                              ? "no engine branch, but modules loaded from an Engine\\Binaries and a "
                                "target Binaries\\Win64"
                          : layout.buildToolName ? "no engine branch, but a build-tool target name"
                                                 : "no engine branch, but the build-tool directory layout";
        return presence;
    }

    presence.reason = versionText.empty() ? "no version resource and no build-tool layout"
                                          : "version resource carries no engine branch";
    return presence;
}

namespace {

// A ModRM/SIB memory operand at code[at].
struct Operand {
    int base = -1; // -1: none, 16: rip
    int index = -1;
    int scale = 0;
    std::int64_t displacement = 0;
    std::size_t end = 0;
    int reg = 0;
};

constexpr int kRip = 16;

std::optional<Operand> MemoryOperand(const std::uint8_t *code, std::size_t size, std::size_t at, std::uint8_t rex) {
    if (at >= size)
        return std::nullopt;
    const std::uint8_t modrm = code[at];
    const int mod = modrm >> 6;
    const int rm = modrm & 7;
    if (mod == 3)
        return std::nullopt;
    Operand operand;
    operand.reg = (modrm >> 3) & 7;
    operand.base = rm | ((rex & 1) << 3);
    std::size_t next = at + 1;
    if (rm == 4) {
        if (next >= size)
            return std::nullopt;
        const std::uint8_t sib = code[next++];
        const int index = ((sib >> 3) & 7) | ((rex & 2) << 2);
        operand.index = index == 4 ? -1 : index;
        operand.scale = sib >> 6;
        operand.base = (sib & 7) | ((rex & 1) << 3);
        if ((sib & 7) == 5 && mod == 0)
            operand.base = -1;
    } else if (rm == 5 && mod == 0) {
        operand.base = kRip;
    }
    const std::size_t bytes = mod == 1 ? 1 : (mod == 2 || operand.base == kRip || operand.base == -1) ? 4 : 0;
    if (next + bytes > size)
        return std::nullopt;
    if (bytes == 1) {
        operand.displacement = static_cast<std::int8_t>(code[next]);
    } else if (bytes == 4) {
        std::int32_t displacement = 0;
        std::memcpy(&displacement, code + next, sizeof(displacement));
        operand.displacement = displacement;
    }
    operand.end = next + bytes;
    return operand;
}

// `mov r/m32, imm32` or `mov r/m32, r32` (after 0x66: 16 bits) at code[at].
struct Store {
    Operand target;
    std::optional<std::uint32_t> immediate;
    std::size_t end = 0;

    // Absolute for rip, else the raw displacement.
    std::int64_t Where(Address codeBase) const {
        return target.base == kRip ? static_cast<std::int64_t>(codeBase + end) + target.displacement
                                   : target.displacement;
    }
    bool SameBase(const Store &other) const {
        return target.base == other.target.base && target.index == other.target.index &&
               target.scale == other.target.scale;
    }
};

std::optional<Store> DecodeStore(const std::uint8_t *code, std::size_t size, std::size_t at, bool sixteen) {
    std::size_t next = at;
    if (sixteen) {
        if (next >= size || code[next] != 0x66)
            return std::nullopt;
        ++next;
    }
    std::uint8_t rex = 0;
    if (next < size && (code[next] & 0xF0) == 0x40)
        rex = code[next++];
    if (next >= size || (code[next] != 0xC7 && code[next] != 0x89))
        return std::nullopt;
    const bool immediate = code[next++] == 0xC7;
    const std::optional<Operand> operand = MemoryOperand(code, size, next, rex);
    if (!operand || (immediate && operand->reg != 0))
        return std::nullopt;
    Store store;
    store.target = *operand;
    store.end = operand->end;
    if (immediate) {
        const std::size_t width = sixteen ? 2 : 4;
        if (store.end + width > size)
            return std::nullopt;
        std::uint32_t value = 0;
        std::memcpy(&value, code + store.end, width);
        store.immediate = value;
        store.end += width;
    }
    return store;
}

constexpr std::uint8_t kMaxMinor = 30;
// REX + opcode + ModRM + SIB + disp32 + imm32.
constexpr std::size_t kLongestStore = 12;
constexpr int kChangelistWithin = 4;
constexpr std::size_t kChunk = std::size_t{1} << 20;

} // namespace

EngineVersion FindVersionInCode(const MemoryReader &reader, std::span<const ScanRegion> code,
                                InstructionLength length) {
    EngineVersion found;
    std::vector<std::uint8_t> buffer;
    // Chunks overlap by a few instructions, so a store is never cut in two.
    constexpr std::size_t kOverlap = kLongestStore * (kChangelistWithin + 3);
    for (const ScanRegion &region : code) {
        for (std::uint64_t offset = 0; offset < region.size; offset += kChunk) {
            const auto take =
                static_cast<std::size_t>(std::min<std::uint64_t>(kChunk + kOverlap, region.size - offset));
            const Address base = region.start + offset;
            buffer.resize(take);
            if (!reader.Read(base, buffer.data(), take))
                continue;
            const std::uint8_t *bytes = buffer.data();
            const std::size_t scanEnd = std::min(take, kChunk);
            for (std::size_t p = 2; p + 4 <= scanEnd; ++p) {
                // Major (4 or 5) and a minor, 16 bits each.
                if ((bytes[p] != 4 && bytes[p] != 5) || bytes[p + 1] != 0 || bytes[p + 2] == 0 ||
                    bytes[p + 2] > kMaxMinor || bytes[p + 3] != 0)
                    continue;
                for (std::size_t start = p > kLongestStore ? p - kLongestStore : 0; start + 2 <= p; ++start) {
                    const std::optional<Store> version = DecodeStore(bytes, take, start, false);
                    if (!version || !version->immediate || version->end != p + 4)
                        continue;
                    const std::int64_t at = version->Where(base);
                    // FEngineVersionBase is 4-byte aligned.
                    if (version->target.base != kRip && at % 4 != 0)
                        continue;
                    const std::optional<Store> patch = DecodeStore(bytes, take, version->end, true);
                    if (!patch || !patch->SameBase(*version) || patch->Where(base) != at + 4 ||
                        (patch->immediate && *patch->immediate > kMaxMinor))
                        continue;
                    bool changelist = false;
                    std::size_t next = patch->end;
                    for (int step = 0; step < kChangelistWithin && next < take; ++step) {
                        const std::optional<Store> store = DecodeStore(bytes, take, next, false);
                        if (store && store->SameBase(*version) && store->Where(base) == at + 8) {
                            changelist = true;
                            break;
                        }
                        const std::size_t size = length ? length(bytes + next, take - next) : 0;
                        if (size == 0)
                            break;
                        next += size;
                    }
                    if (!changelist)
                        continue;
                    const std::int32_t major = bytes[p];
                    const std::int32_t minor = bytes[p + 2];
                    if (found.Known() && (found.major != major || found.minor != minor))
                        return {};
                    found.major = major;
                    found.minor = minor;
                    if (patch->immediate)
                        found.patch = std::max(found.patch, static_cast<std::int32_t>(*patch->immediate));
                    found.source = "FEngineVersion's constructor in code";
                    break;
                }
            }
        }
    }
    return found;
}

} // namespace URK::Unreal
