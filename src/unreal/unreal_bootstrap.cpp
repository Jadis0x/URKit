#include "unreal_bootstrap.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string>

namespace URK::Unreal {
namespace {

// Both globals are pointer aligned, so nothing in between is worth probing.
constexpr Address kScanStep = sizeof(Address);

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

// Every known array layout keeps its object pointer at one of these, so a
// candidate holding no readable pointer at any of them cannot be one.
const std::vector<std::int32_t> &ObjectsOffsets() {
    static const std::vector<std::int32_t> offsets = [] {
        std::vector<std::int32_t> distinct;
        for (const FixedObjectArrayLayout &layout : KnownFixedLayouts())
            distinct.push_back(layout.objectsOffset);
        for (const ChunkedObjectArrayLayout &layout : KnownChunkedLayouts())
            distinct.push_back(layout.objectsOffset);
        std::sort(distinct.begin(), distinct.end());
        distinct.erase(std::unique(distinct.begin(), distinct.end()), distinct.end());
        return distinct;
    }();
    return offsets;
}

bool MightHoldObjectArray(const MemoryReader &reader, Address address) {
    for (const std::int32_t offset : ObjectsOffsets()) {
        if (reader.PointsToReadable(address + offset))
            return true;
    }
    return false;
}

// One read rather than a walk: the entry is within the first bytes of a block.
bool HoldsNoneEntry(const MemoryReader &reader, Address block) {
    std::array<std::uint8_t, kEntryWindow> bytes{};
    if (!reader.Read(block, bytes.data(), bytes.size()))
        return false;
    for (std::size_t offset = 0; offset + sizeof(kNoneBytes) <= bytes.size(); offset += 2) {
        std::uint32_t word = 0;
        std::memcpy(&word, bytes.data() + offset, sizeof(word));
        if (word == kNoneBytes)
            return true;
    }
    return false;
}

// Running the full name-table probe everywhere would be far too slow, so a
// candidate first has to reach "None": one hop for the pool block table, two
// for the entry array chunk of entry pointers.
bool MightHoldNameTable(const MemoryReader &reader, Address address) {
    for (Address slot = 0; slot < kPrefilterSlots; slot += sizeof(Address)) {
        const std::optional<Address> pointer = reader.ReadPointer(address + slot);
        if (!pointer || *pointer == kNullAddress || !reader.Readable(*pointer, kEntryWindow))
            continue;
        if (HoldsNoneEntry(reader, *pointer))
            return true;
        const std::optional<Address> entry = reader.ReadPointer(*pointer);
        if (entry && *entry != kNullAddress && HoldsNoneEntry(reader, *entry))
            return true;
    }
    return false;
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

// A scan reaches a global's aliases before the global itself: a few bytes
// ahead of it a different header offset describes the same block table and
// reads back the same names, so equally confirmed pairs are settled by taking
// the last address that still confirms.
bool Better(const Runtime &candidate, const Runtime &incumbent) {
    if (candidate.confirmedNames != incumbent.confirmedNames)
        return candidate.confirmedNames > incumbent.confirmedNames;
    if (candidate.nameTableAddress != incumbent.nameTableAddress)
        return candidate.nameTableAddress > incumbent.nameTableAddress;
    return candidate.objectArrayAddress > incumbent.objectArrayAddress;
}

std::vector<Address> Scan(const MemoryReader &reader, std::span<const ScanRegion> regions,
                          bool (*accept)(const MemoryReader &, Address)) {
    std::vector<Address> found;
    for (const ScanRegion &region : regions) {
        if (region.size < sizeof(Address))
            continue;
        const Address end = region.start + region.size - sizeof(Address);
        for (Address address = region.start; address <= end; address += kScanStep) {
            if (!accept(reader, address))
                continue;
            found.push_back(address);
            if (found.size() >= kMaxCandidates)
                return found;
        }
    }
    return found;
}

bool AcceptObjectArray(const MemoryReader &reader, Address address) {
    return MightHoldObjectArray(reader, address) && ResolveObjectArrayLayout(reader, address).has_value();
}

bool AcceptNameTable(const MemoryReader &reader, Address address) {
    return MightHoldNameTable(reader, address) && NameTable::Resolve(reader, address).has_value();
}

} // namespace

std::vector<Address> FindObjectArrayCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions) {
    return Scan(reader, regions, AcceptObjectArray);
}

std::vector<Address> FindNameTableCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions) {
    return Scan(reader, regions, AcceptNameTable);
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
