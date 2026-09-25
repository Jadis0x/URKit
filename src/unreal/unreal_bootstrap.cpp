#include "unreal_bootstrap.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

namespace URK::Unreal {
namespace {

// Room for a stale array or a second pool without letting a noisy section turn
// the pairing into a long search.
constexpr std::size_t kMaxCandidates = 0x20;

// "None", the first entry of every name table, sits at the head of the first
// block behind its own short header.
constexpr std::uint32_t kNoneBytes = 0x656E6F4E;
constexpr std::size_t kEntryWindow = 0x10;
constexpr Address kPrefilterSlots = 0x40;

// Names CoreUObject registers before any game content, so a pair that cannot
// produce one of them is not the pair however well it reads otherwise.
constexpr std::array kAnchorNames = {"Object", "Class", "Package", "Function", "Field", "Struct"};

constexpr std::int32_t kNameSampleCount = 0x100;
constexpr std::size_t kMaxNameLength = 0x80;

// Empty slots and unloaded names are normal; three in four is a match.
constexpr std::int32_t kConfirmedNumerator = 3;
constexpr std::int32_t kConfirmedDenominator = 4;

// The entry is within the first bytes of a block, so one read covers it.
bool HoldsNoneEntry(std::span<const std::uint8_t> bytes) {
    for (std::size_t offset = 0; offset + sizeof(kNoneBytes) <= bytes.size(); offset += 2) {
        std::uint32_t word = 0;
        std::memcpy(&word, bytes.data() + offset, sizeof(word));
        if (word == kNoneBytes)
            return true;
    }
    return false;
}

// Whether a word reaches "None" in one hop (pool block) or two (entry chunk).
// One read serves both, and it is asked per word, not per candidate.
bool ReachesNoneEntry(const MemoryReader &reader, Address value) {
    if (!MemoryReader::PlausiblePointer(value))
        return false;

    std::array<std::uint8_t, kEntryWindow> bytes{};
    if (!reader.Read(value, bytes.data(), bytes.size()))
        return false;
    if (HoldsNoneEntry(bytes))
        return true;

    Address entry = 0;
    std::memcpy(&entry, bytes.data(), sizeof(entry));
    if (!MemoryReader::PlausiblePointer(entry))
        return false;

    std::array<std::uint8_t, kEntryWindow> hop{};
    return reader.Read(entry, hop.data(), hop.size()) && HoldsNoneEntry(hop);
}

bool PlausibleName(const std::string &name) {
    if (name.empty() || name.size() > kMaxNameLength)
        return false;
    for (const char character : name) {
        const auto byte = static_cast<std::uint8_t>(character);
        if (byte <= 0x20 || byte >= 0x7F)
            return false;
    }
    return true;
}

bool IsAnchorName(const std::string &name) {
    for (const char *anchor : kAnchorNames) {
        if (name == anchor)
            return true;
    }
    return false;
}

// How many sampled objects name themselves through this table, or zero if the
// sample says the two are unrelated.
std::int32_t ConfirmNames(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &header) {
    const std::int32_t total = objects.Num();
    const std::int32_t sampleCount = total < kNameSampleCount ? total : kNameSampleCount;

    std::int32_t sampled = 0;
    std::int32_t plausible = 0;
    bool anchored = false;

    for (std::int32_t index = 0; index < sampleCount; ++index) {
        if (objects.ObjectAt(index) == kNullAddress)
            continue;
        ++sampled;

        const std::optional<std::string> name = names.ObjectName(objects, header.name, index);
        if (!name || !PlausibleName(*name))
            continue;
        ++plausible;
        anchored = anchored || IsAnchorName(*name);
    }

    if (sampled == 0 || !anchored)
        return 0;
    if (plausible * kConfirmedDenominator < sampled * kConfirmedNumerator)
        return 0;
    return plausible;
}

// A scan reaches a global's aliases first, so ties are settled by taking the
// last address that still confirms.
bool Better(const Runtime &candidate, const Runtime &incumbent) {
    if (candidate.confirmedNames != incumbent.confirmedNames)
        return candidate.confirmedNames > incumbent.confirmedNames;
    if (candidate.nameTableAddress != incumbent.nameTableAddress)
        return candidate.nameTableAddress > incumbent.nameTableAddress;
    return candidate.objectArrayAddress > incumbent.objectArrayAddress;
}

enum class Looking { ObjectArray, NameTable };

// Globals are word aligned; read in chunks, since one call per word was the
// old bottleneck.
constexpr std::size_t kChunkWords = 0x8000;

// Words past a chunk's last candidate that the candidate still looks at: the
// name table's window of slots, or the object array's header.
constexpr std::size_t kLookaheadWords =
    std::max(kPrefilterSlots / sizeof(Address), (kObjectArrayHeaderBytes + sizeof(Address) - 1) / sizeof(Address));

// Halve a failing chunk so an unreadable tail costs only itself; unread words
// stay zero, which no prefilter accepts.
void ReadWords(const MemoryReader &reader, Address at, std::span<Address> words) {
    if (words.empty() || reader.Read(at, words.data(), words.size() * sizeof(Address)))
        return;
    if (words.size() == 1) {
        words[0] = 0;
        return;
    }
    const std::size_t half = words.size() / 2;
    ReadWords(reader, at, words.first(half));
    ReadWords(reader, at + half * sizeof(Address), words.subspan(half));
}

// Worth validating? The object array by its header; the name table by whether
// any slot of its window reached "None".
bool Worth(std::span<const Address> words, std::span<const std::uint8_t> facts, std::size_t at, Looking looking) {
    if (looking == Looking::ObjectArray) {
        const auto *bytes = reinterpret_cast<const std::uint8_t *>(words.data() + at);
        return HeaderMightBeObjectArray({bytes, kObjectArrayHeaderBytes});
    }
    for (std::size_t slot = 0; slot < kPrefilterSlots / sizeof(Address); ++slot) {
        if (facts[at + slot])
            return true;
    }
    return false;
}

bool Resolves(const MemoryReader &reader, Address address, Looking looking) {
    return looking == Looking::NameTable ? NameTable::Resolve(reader, address).has_value()
                                         : ResolveObjectArrayLayout(reader, address).has_value();
}

std::vector<Address> Scan(const MemoryReader &reader, std::span<const ScanRegion> regions, Looking looking) {
    std::vector<Address> found;
    std::vector<Address> words;
    std::vector<std::uint8_t> facts;

    for (const ScanRegion &region : regions) {
        const std::size_t regionWords = static_cast<std::size_t>(region.size / sizeof(Address));
        for (std::size_t first = 0; first < regionWords; first += kChunkWords) {
            const std::size_t candidates = std::min(kChunkWords, regionWords - first);
            const Address base = region.start + first * sizeof(Address);

            words.assign(candidates + kLookaheadWords, 0);
            ReadWords(reader, base, std::span(words).first(candidates));
            ReadWords(reader, base + candidates * sizeof(Address), std::span(words).subspan(candidates));

            facts.assign(looking == Looking::NameTable ? words.size() : 0, 0);
            for (std::size_t i = 0; i < facts.size(); ++i)
                facts[i] = ReachesNoneEntry(reader, words[i]) ? 1 : 0;

            for (std::size_t i = 0; i < candidates; ++i) {
                if (!Worth(words, facts, i, looking))
                    continue;
                const Address address = base + i * sizeof(Address);
                if (!Resolves(reader, address, looking))
                    continue;
                found.push_back(address);
                if (found.size() >= kMaxCandidates)
                    return found;
            }
        }
    }
    return found;
}

} // namespace

std::vector<Address> FindObjectArrayCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions) {
    return Scan(reader, regions, Looking::ObjectArray);
}

std::vector<Address> FindNameTableCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions) {
    return Scan(reader, regions, Looking::NameTable);
}

std::optional<Runtime> BootstrapRuntime(const MemoryReader &reader, std::span<const ScanRegion> regions) {
    const std::vector<Address> arrays = FindObjectArrayCandidates(reader, regions);
    const std::vector<Address> tables = FindNameTableCandidates(reader, regions);
    return PairCandidates(reader, arrays, tables);
}

std::optional<Runtime> BootstrapAnchored(const MemoryReader &reader, const GlobalCandidates &candidates) {
    const auto resolving = [&reader](std::span<const Address> addresses, Looking looking) {
        std::vector<Address> kept;
        for (const Address address : addresses) {
            if (kept.size() >= kMaxCandidates)
                break;
            if (Resolves(reader, address, looking))
                kept.push_back(address);
        }
        return kept;
    };
    const std::vector<Address> arrays = resolving(candidates.objectArrays, Looking::ObjectArray);
    if (arrays.empty())
        return std::nullopt;
    return PairCandidates(reader, arrays, resolving(candidates.namePools, Looking::NameTable));
}

std::optional<Runtime> PairCandidates(const MemoryReader &reader, std::span<const Address> arrays,
                                      std::span<const Address> tables) {
    std::optional<Runtime> best;
    for (const Address arrayAddress : arrays) {
        const std::optional<ObjectArrayLayout> layout = ResolveObjectArrayLayout(reader, arrayAddress);
        if (!layout)
            continue;

        const ObjectArray objects(reader, arrayAddress, *layout);
        const ObjectOffsets header = FindObjectOffsets(objects);
        if (!header.Resolved())
            continue;

        for (const Address tableAddress : tables) {
            // Resolved per pair: the block width is calibrated against the
            // array being tried, and a wrong array would leave it raised.
            std::optional<NameTable> names = NameTable::Resolve(reader, tableAddress);
            if (!names)
                continue;
            names->CalibrateBlockOffsetBits(objects, header.name);
            names->CalibrateNameLayout(objects, header.name, header.outer);

            const std::int32_t confirmed = ConfirmNames(objects, *names, header);
            if (confirmed == 0)
                continue;

            const Runtime candidate{.objectArrayAddress = arrayAddress,
                                    .nameTableAddress = tableAddress,
                                    .objects = objects,
                                    .names = *names,
                                    .header = header,
                                    .confirmedNames = confirmed};
            if (!best || Better(candidate, *best))
                best = candidate;
        }
    }

    return best;
}

std::vector<std::string> ExplainNoPair(const MemoryReader &reader, std::span<const Address> arrays,
                                       std::span<const Address> tables) {
    constexpr std::size_t kShown = 3;
    constexpr std::int32_t kShownNames = 4;
    std::vector<std::string> lines;
    const auto hex = [](Address address) {
        char text[24];
        std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(address));
        return std::string(text);
    };

    std::vector<std::pair<Address, ObjectArrayLayout>> resolvedArrays;
    std::vector<std::string> arrayReasons;
    for (const Address address : arrays) {
        std::string why;
        if (const std::optional<ObjectArrayLayout> layout = ResolveObjectArrayLayout(reader, address, &why))
            resolvedArrays.emplace_back(address, *layout);
        else if (arrayReasons.size() < kShown)
            arrayReasons.push_back("object array candidate " + hex(address) + ": " + why);
    }
    std::vector<Address> resolvedTables;
    std::vector<std::string> tableReasons;
    for (const Address address : tables) {
        std::string why;
        if (NameTable::Resolve(reader, address, &why))
            resolvedTables.push_back(address);
        else if (tableReasons.size() < kShown)
            tableReasons.push_back("name pool candidate " + hex(address) + ": " + why);
    }
    lines.push_back(std::to_string(resolvedArrays.size()) + " of " + std::to_string(arrays.size()) +
                    " object array candidates and " + std::to_string(resolvedTables.size()) + " of " +
                    std::to_string(tables.size()) + " name pool candidates resolve");
    if (resolvedArrays.empty())
        lines.insert(lines.end(), arrayReasons.begin(), arrayReasons.end());
    if (resolvedTables.empty())
        lines.insert(lines.end(), tableReasons.begin(), tableReasons.end());
    if (resolvedArrays.empty() || resolvedTables.empty())
        return lines;

    // Both sides resolve, so the pairing is what fails: say what the names read as.
    for (std::size_t a = 0; a < resolvedArrays.size() && a < kShown; ++a) {
        const ObjectArray objects(reader, resolvedArrays[a].first, resolvedArrays[a].second);
        const ObjectOffsets header = FindObjectOffsets(objects);
        const std::string array = "object array " + hex(resolvedArrays[a].first) + " (" +
                                  std::to_string(objects.Num()) + " objects)";
        if (!header.Resolved()) {
            char offsets[128];
            std::snprintf(offsets, sizeof(offsets), "flags=%d index=%d class=%d outer=%d name=%d", header.flags,
                          header.index, header.classPointer, header.outer, header.name);
            lines.push_back(array + ": the UObject header did not resolve (" + offsets + ")");
            continue;
        }
        for (std::size_t t = 0; t < resolvedTables.size() && t < kShown; ++t) {
            std::optional<NameTable> names = NameTable::Resolve(reader, resolvedTables[t]);
            names->CalibrateBlockOffsetBits(objects, header.name);
            names->CalibrateNameLayout(objects, header.name, header.outer);
            std::int32_t sampled = 0;
            std::int32_t plausible = 0;
            bool anchored = false;
            std::string first;
            const std::int32_t count = std::min(objects.Num(), kNameSampleCount);
            for (std::int32_t index = 0; index < count; ++index) {
                if (objects.ObjectAt(index) == kNullAddress)
                    continue;
                ++sampled;
                const std::optional<std::string> name = names->ObjectName(objects, header.name, index);
                if (sampled <= kShownNames)
                    first += (first.empty() ? "'" : ", '") + name.value_or("?").substr(0, 40) + "'";
                if (!name || !PlausibleName(*name))
                    continue;
                ++plausible;
                anchored = anchored || IsAnchorName(*name);
            }
            lines.push_back(array + " with name pool " + hex(resolvedTables[t]) + ": " + std::to_string(plausible) +
                            " of " + std::to_string(sampled) + " sampled names plausible, core names " +
                            (anchored ? "seen" : "not seen") + "; first: " + first);
        }
    }
    return lines;
}

std::optional<Runtime> BootstrapModule(const MemoryReader &reader, Address moduleBase) {
    const std::vector<ScanRegion> regions = ModuleDataRegions(reader, moduleBase);
    return BootstrapRuntime(reader, regions);
}

} // namespace URK::Unreal
