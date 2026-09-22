#include "unreal_bootstrap.h"

#include <algorithm>
#include <array>
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

// The array outlives individual objects, so some slots are empty and some names
// belong to content the pool no longer holds; three in four is a match, not a
// coincidence.
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

// What one scanned word is worth knowing, asked once per word rather than
// once per candidate whose window covers it.
//
// Reaching "None" in one hop is a pool block table, in two an entry array
// chunk of entry pointers. The first hop's read serves both: its own bytes
// answer the one-hop question and its first word is the second hop, so the
// scan pays one read per hop rather than one per question.
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

// Both globals are pointer aligned, so a scan steps by a word and a region is
// just its words. Read in chunks: one reader call per word over a shipped
// game's data sections is where the bootstrap used to spend its life.
constexpr std::size_t kChunkWords = 0x8000;

// Words past a chunk's last candidate that the candidate still looks at: the
// name table's window of slots, or the object array's header.
constexpr std::size_t kLookaheadWords =
    std::max(kPrefilterSlots / sizeof(Address), (kObjectArrayHeaderBytes + sizeof(Address) - 1) / sizeof(Address));

// A chunk straddling the end of a committed range is halved rather than read
// a word at a time, so an unreadable tail does not cost the whole chunk.
// Unreadable words stay zero, which no prefilter accepts.
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

// Whether a candidate is worth validating, from the chunk alone. The object
// array is decided by its own header; the name table by whether any slot of
// its window reached "None", which is the one question that costs a read and
// so is answered per word rather than per candidate.
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

std::optional<Runtime> BootstrapModule(const MemoryReader &reader, Address moduleBase) {
    const std::vector<ScanRegion> regions = ModuleDataRegions(reader, moduleBase);
    return BootstrapRuntime(reader, regions);
}

} // namespace URK::Unreal
