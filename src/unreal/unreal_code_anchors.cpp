#include "unreal_code_anchors.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <string_view>

namespace URK::Unreal {
namespace {

constexpr std::size_t kChunkBytes = 0x100000;
// The longest x64 instruction, so one straddling a chunk edge is still seen whole.
constexpr std::size_t kOverlap = 16;
// Enough for UObjectBaseInit with AllocateObjectPool inlined.
constexpr std::size_t kMaxFunctionBytes = 0x4000;

// Field offsets a reference may carry past the global it belongs to: before
// UE5.8 ObjObjects sat 0x10 into FUObjectArray, and its own fields reach 0x28.
constexpr std::int64_t kObjectArrayBefore = 0x28;
constexpr std::int64_t kObjectArrayAfter = 0x10;
constexpr std::int64_t kNamePoolBefore = 0x18;

constexpr std::u16string_view kGcKey = u"gc.MaxObjectsInGame";
constexpr std::string_view kFirstEngineName = "ByteProperty";

bool InRegions(std::span<const ScanRegion> regions, Address address) {
    for (const ScanRegion &region : regions) {
        if (address >= region.start && address < region.start + region.size)
            return true;
    }
    return false;
}

// visit(chunkStart, bytes, readable, owned): positions below owned belong to
// this chunk; the rest is overlap for instructions that cross into the next.
void ForEachChunk(const MemoryReader &reader, std::span<const ScanRegion> regions,
                  const std::function<void(Address, const std::uint8_t *, std::size_t, std::size_t)> &visit) {
    std::vector<std::uint8_t> buffer(kChunkBytes + kOverlap);
    for (const ScanRegion &region : regions) {
        for (std::uint64_t offset = 0; offset < region.size; offset += kChunkBytes) {
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(kChunkBytes + kOverlap, region.size - offset));
            if (!reader.Read(region.start + offset, buffer.data(), want))
                continue;
            visit(region.start + offset, buffer.data(), want, std::min(kChunkBytes, want));
        }
    }
}

std::int32_t Displacement(const std::uint8_t *at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// Whole literals only: a zero unit on both sides.
std::vector<Address> FindLiteral(const MemoryReader &reader, std::span<const ScanRegion> constants,
                                 const std::vector<std::uint8_t> &text, std::size_t unit) {
    std::vector<std::uint8_t> needle(unit, 0);
    needle.insert(needle.end(), text.begin(), text.end());
    needle.insert(needle.end(), unit, 0);

    std::vector<Address> found;
    ForEachChunk(reader, constants, [&](Address base, const std::uint8_t *bytes, std::size_t size, std::size_t owned) {
        const std::uint8_t *end = bytes + size;
        for (const std::uint8_t *at = std::search(bytes, end, needle.begin(), needle.end()); at != end;
             at = std::search(at + 1, end, needle.begin(), needle.end())) {
            const std::size_t position = static_cast<std::size_t>(at - bytes);
            if (position >= owned)
                break;
            const Address literal = base + position + unit;
            if (literal % unit == 0)
                found.push_back(literal);
        }
    });
    return found;
}

// lea r64, [rip+disp32] instructions whose operand is one of targets.
std::vector<std::pair<Address, std::size_t>> LeaReferences(const MemoryReader &reader,
                                                           std::span<const ScanRegion> code,
                                                           const std::vector<Address> &targets) {
    std::vector<std::pair<Address, std::size_t>> sites;
    if (targets.empty())
        return sites;
    ForEachChunk(reader, code, [&](Address base, const std::uint8_t *bytes, std::size_t size, std::size_t owned) {
        for (std::size_t i = 0; i < owned && i + 7 <= size; ++i) {
            if ((bytes[i] != 0x48 && bytes[i] != 0x4C) || bytes[i + 1] != 0x8D || (bytes[i + 2] & 0xC7) != 0x05)
                continue;
            const Address target = base + i + 7 + static_cast<std::int64_t>(Displacement(bytes + i + 3));
            for (std::size_t which = 0; which < targets.size(); ++which) {
                if (targets[which] == target)
                    sites.emplace_back(base + i, which);
            }
        }
    });
    return sites;
}

std::vector<Address> CallsTo(const MemoryReader &reader, std::span<const ScanRegion> code, Address target) {
    std::vector<Address> sites;
    ForEachChunk(reader, code, [&](Address base, const std::uint8_t *bytes, std::size_t size, std::size_t owned) {
        for (std::size_t i = 0; i < owned && i + 5 <= size; ++i) {
            if (bytes[i] == 0xE8 && base + i + 5 + static_cast<std::int64_t>(Displacement(bytes + i + 1)) == target)
                sites.push_back(base + i);
        }
    });
    return sites;
}

// Bytes of immediate after a rip-relative operand, or -1 if the opcode is not
// one that reads or writes a global.
int ImmediateAfter(std::uint8_t opcode) {
    switch (opcode) {
    case 0x01: case 0x03: case 0x09: case 0x0B: case 0x21: case 0x23: case 0x29: case 0x2B:
    case 0x31: case 0x33: case 0x38: case 0x39: case 0x3A: case 0x3B: case 0x85: case 0x87:
    case 0x88: case 0x89: case 0x8A: case 0x8B: case 0x8D: case 0xFF:
        return 0;
    case 0x80: case 0x83: case 0xC6:
        return 1;
    case 0x81: case 0xC7:
        return 4;
    default:
        return -1;
    }
}

// Globals a function touches through rip-relative operands. Not a decoder: a
// stray match only adds a candidate, and candidates are validated.
std::vector<Address> DataReferences(const MemoryReader &reader, const FunctionRange &range,
                                    std::span<const ScanRegion> writable) {
    std::vector<Address> targets;
    const std::size_t size = static_cast<std::size_t>(std::min<Address>(range.end - range.begin, kMaxFunctionBytes));
    std::vector<std::uint8_t> bytes(size);
    if (size < 6 || !reader.Read(range.begin, bytes.data(), size))
        return targets;

    for (std::size_t i = 0; i + 6 <= size; ++i) {
        std::size_t modrm = 0;
        int immediate = -1;
        if (bytes[i] == 0x0F && i + 7 <= size) {
            const std::uint8_t second = bytes[i + 1];
            if (second == 0xB6 || second == 0xB7 || second == 0xBE || second == 0xBF || second == 0x10 ||
                second == 0x11) {
                modrm = i + 2;
                immediate = 0;
            }
        } else {
            immediate = ImmediateAfter(bytes[i]);
            modrm = i + 1;
        }
        if (immediate < 0 || (bytes[modrm] & 0xC7) != 0x05 || modrm + 5 + immediate > size)
            continue;
        const Address end = range.begin + modrm + 5 + static_cast<Address>(immediate);
        const Address target = end + static_cast<std::int64_t>(Displacement(bytes.data() + modrm + 1));
        if (InRegions(writable, target))
            targets.push_back(target);
    }
    return targets;
}

void Vote(std::map<Address, int> &votes, std::span<const ScanRegion> writable, Address address) {
    if (InRegions(writable, address))
        ++votes[address];
}

// Globals are pointer-aligned, so a field reference names a nearby aligned base.
void VoteAround(std::map<Address, int> &votes, std::span<const ScanRegion> writable, Address target,
                std::int64_t before, std::int64_t after) {
    const Address aligned = target & ~Address{7};
    for (std::int64_t delta = -before; delta <= after; delta += 8)
        Vote(votes, writable, aligned + delta);
}

std::vector<Address> Ranked(const std::map<Address, int> &votes) {
    std::vector<std::pair<Address, int>> ordered(votes.begin(), votes.end());
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto &a, const auto &b) { return a.second > b.second; });
    std::vector<Address> ranked;
    ranked.reserve(ordered.size());
    for (const auto &[address, count] : ordered)
        ranked.push_back(address);
    return ranked;
}

// The pool is constructed in place: `lea rcx, [rip+NamePoolData]` sits just
// ahead of every call to the constructor.
std::optional<Address> ThisArgumentBefore(const MemoryReader &reader, Address call) {
    constexpr std::size_t kLookBack = 24;
    constexpr std::size_t kLeaLength = 7;
    std::uint8_t bytes[kLookBack]{};
    if (!reader.Read(call - kLookBack, bytes, kLookBack))
        return std::nullopt;
    for (std::size_t at = kLookBack - kLeaLength + 1; at-- > 0;) {
        if (bytes[at] == 0x48 && bytes[at + 1] == 0x8D && bytes[at + 2] == 0x0D) {
            const Address next = call - kLookBack + at + kLeaLength;
            return next + static_cast<std::int64_t>(Displacement(bytes + at + 3));
        }
    }
    return std::nullopt;
}

} // namespace

GlobalCandidates FindGlobalCandidates(const MemoryReader &reader, Address moduleBase) {
    GlobalCandidates candidates;
    const std::vector<ScanRegion> code = ModuleCodeRegions(reader, moduleBase);
    const std::vector<ScanRegion> constants = ModuleConstantRegions(reader, moduleBase);
    const std::vector<ScanRegion> writable = ModuleWritableRegions(reader, moduleBase);
    if (code.empty() || constants.empty() || writable.empty())
        return candidates;

    const Address modules[] = {moduleBase};
    const FunctionTable functions = FunctionTable::Read(reader, modules);
    if (functions.Empty())
        return candidates;

    std::vector<std::uint8_t> gcKey(kGcKey.size() * 2);
    std::memcpy(gcKey.data(), kGcKey.data(), gcKey.size());
    const std::vector<std::uint8_t> engineName(kFirstEngineName.begin(), kFirstEngineName.end());

    const std::vector<Address> gcKeys = FindLiteral(reader, constants, gcKey, 2);
    const std::vector<Address> engineNames = FindLiteral(reader, constants, engineName, 1);

    std::vector<Address> literals = gcKeys;
    literals.insert(literals.end(), engineNames.begin(), engineNames.end());
    const auto sites = LeaReferences(reader, code, literals);

    std::map<Address, int> arrayVotes;
    std::map<Address, int> poolVotes;
    std::vector<Address> constructors;
    for (const auto &[site, which] : sites) {
        const std::optional<FunctionRange> range = functions.Containing(site);
        if (!range)
            continue;
        if (which < gcKeys.size()) {
            for (const Address target : DataReferences(reader, *range, writable))
                VoteAround(arrayVotes, writable, target, kObjectArrayBefore, kObjectArrayAfter);
            continue;
        }
        const Address constructor = functions.PrimaryBegin(site);
        if (constructor != kNullAddress &&
            std::find(constructors.begin(), constructors.end(), constructor) == constructors.end())
            constructors.push_back(constructor);
        // Inlined into its caller, the constructor writes the pool's fields directly.
        for (const Address target : DataReferences(reader, *range, writable))
            VoteAround(poolVotes, writable, target, kNamePoolBefore, 0);
    }

    // A pool argument at a call site outweighs a field write nearby.
    constexpr int kCallSiteWeight = 4;
    for (const Address constructor : constructors) {
        for (const Address call : CallsTo(reader, code, constructor)) {
            const std::optional<Address> pool = ThisArgumentBefore(reader, call);
            if (pool && InRegions(writable, *pool))
                poolVotes[*pool] += kCallSiteWeight;
        }
    }

    candidates.objectArrays = Ranked(arrayVotes);
    candidates.namePools = Ranked(poolVotes);
    return candidates;
}

std::vector<std::size_t> AddressTakenCounts(const MemoryReader &reader, std::span<const ScanRegion> code,
                                            const std::vector<Address> &targets) {
    std::vector<std::size_t> counts(targets.size(), 0);
    for (const auto &[site, which] : LeaReferences(reader, code, targets))
        ++counts[which];
    return counts;
}

std::vector<Address> FunctionsReferencingText(const MemoryReader &reader, Address moduleBase,
                                              const FunctionTable &functions, std::string_view text) {
    const std::vector<ScanRegion> code = ModuleCodeRegions(reader, moduleBase);
    const std::vector<ScanRegion> constants = ModuleConstantRegions(reader, moduleBase);
    if (code.empty() || constants.empty() || functions.Empty() || text.empty())
        return {};
    std::vector<std::uint8_t> ascii(text.begin(), text.end());
    std::vector<std::uint8_t> wide;
    for (const char c : text) {
        wide.push_back(static_cast<std::uint8_t>(c));
        wide.push_back(0);
    }
    std::vector<Address> targets = FindLiteral(reader, constants, wide, 2);
    const std::vector<Address> narrow = FindLiteral(reader, constants, ascii, 1);
    targets.insert(targets.end(), narrow.begin(), narrow.end());
    if (targets.empty())
        return {};
    // A record holding the literal's address: the code takes the record's.
    std::vector<ScanRegion> records = constants;
    const std::vector<ScanRegion> data = ModuleDataRegions(reader, moduleBase);
    records.insert(records.end(), data.begin(), data.end());
    const std::vector<Address> literals = targets;
    ForEachChunk(reader, records, [&](Address base, const std::uint8_t *bytes, std::size_t size, std::size_t owned) {
        for (std::size_t i = (8 - base % 8) % 8; i < owned && i + 8 <= size; i += 8) {
            Address value = 0;
            std::memcpy(&value, bytes + i, sizeof(value));
            if (std::find(literals.begin(), literals.end(), value) != literals.end())
                targets.push_back(base + i);
        }
    });
    std::vector<Address> owners;
    for (const auto &[site, which] : LeaReferences(reader, code, targets)) {
        const Address owner = functions.PrimaryBegin(site);
        if (owner != kNullAddress && std::find(owners.begin(), owners.end(), owner) == owners.end())
            owners.push_back(owner);
    }
    return owners;
}

Address FindEngineLoopTick(const MemoryReader &reader, Address moduleBase, const FunctionTable &functions) {
    std::vector<Address> tick = FunctionsReferencingText(reader, moduleBase, functions, "r.OneFrameThreadLag");
    const std::vector<Address> benchmarking =
        FunctionsReferencingText(reader, moduleBase, functions, "FEngineLoop::Tick.Benchmarking");
    tick.insert(tick.end(), benchmarking.begin(), benchmarking.end());
    Address found = kNullAddress;
    for (const Address owner : FunctionsReferencingText(reader, moduleBase, functions, "t.IdleWhenNotForeground")) {
        if (std::find(tick.begin(), tick.end(), owner) == tick.end())
            continue;
        if (found != kNullAddress && found != owner)
            return kNullAddress;
        found = owner;
    }
    return found;
}

std::vector<Address> GlobalReferences(const MemoryReader &reader, const FunctionRange &range,
                                      std::span<const ScanRegion> writable) {
    return DataReferences(reader, range, writable);
}

std::optional<Address> FirstBranchTarget(const MemoryReader &reader, const FunctionRange &range) {
    constexpr std::size_t kMaxThunkBytes = 0x80;
    const std::size_t size = static_cast<std::size_t>(std::min<Address>(range.end - range.begin, kMaxThunkBytes));
    std::uint8_t bytes[kMaxThunkBytes]{};
    if (size < 5 || !reader.Read(range.begin, bytes, size))
        return std::nullopt;
    for (std::size_t i = 0; i + 5 <= size; ++i) {
        if (bytes[i] == 0xE8 || bytes[i] == 0xE9)
            return range.begin + i + 5 + static_cast<std::int64_t>(Displacement(bytes + i + 1));
    }
    return std::nullopt;
}

} // namespace URK::Unreal
