#include "unreal_offsets.h"

#include <vector>

namespace URK::Unreal {
namespace {

// Nothing calibrated here lives past the UObject header.
constexpr std::int32_t kMaxHeaderOffset = 0x40;

// RF_ClassDefaultObject | RF_Public | RF_Standalone, common among the objects
// the engine registers first.
constexpr std::uint32_t kCommonFlagValue = 0x43;
constexpr std::int32_t kFlagSampleCount = 0x100;
constexpr std::int32_t kMinObjectsCarryingCommonFlag = 0xA0;

// Far enough apart that no unrelated field holds both, low enough to exist.
constexpr std::int32_t kIndexProbeA = 0x55;
constexpr std::int32_t kIndexProbeB = 0x123;

// A wrong Class offset either leaves mapped memory or never settles, so a short
// walk decides.
constexpr int kClassWalkLimit = 0x10;

// What an FName comparison index looks like process-wide. A counter has too low
// an average, a flag word almost no spread, a pointer's low half overflows.
constexpr std::uint32_t kMaxComparisonIndex = 0x4000000;
constexpr std::uint64_t kMaxAverageComparisonIndex = kMaxComparisonIndex / 2;
constexpr std::uint64_t kMinAverageComparisonIndex = 0x280;
constexpr std::uint32_t kLowComparisonIndexCap = 0x10;
constexpr std::int32_t kMaxNamesWithLowComparisonIndex = 0x40;

// Objects near a page end may have fewer readable header bytes; skip them.
constexpr Address kPageSize = 0x1000;

bool HasRoomForHeader(Address object) { return (object & (kPageSize - 1)) <= (kPageSize - kMaxHeaderOffset); }

// First offset at or after start where both objects hold an object pointer.
std::int32_t FindSharedPointerOffset(const MemoryReader &reader, Address objectA, Address objectB, std::int32_t start,
                                     std::int32_t limit) {
    for (std::int32_t offset = start; offset <= limit; offset += sizeof(Address)) {
        if (reader.PointsToObject(objectA + offset) && reader.PointsToObject(objectB + offset))
            return offset;
    }
    return kOffsetNotFound;
}

} // namespace

std::int32_t FindFlagsOffset(const ObjectArray &objects) {
    const MemoryReader &reader = objects.Reader();
    const std::int32_t total = objects.Num();
    const std::int32_t sampleCount = total < kFlagSampleCount ? total : kFlagSampleCount;
    if (sampleCount <= 0)
        return kOffsetNotFound;

    // Scaled to the sample so a short array is judged by the same proportion.
    const std::int32_t required =
        static_cast<std::int32_t>(static_cast<std::int64_t>(sampleCount) * kMinObjectsCarryingCommonFlag /
                                  kFlagSampleCount);

    for (std::int32_t offset = sizeof(Address); offset < kMaxHeaderOffset; offset += 4) {
        std::int32_t carrying = 0;
        for (std::int32_t index = 0; index < sampleCount; ++index) {
            const Address object = objects.ObjectAt(index);
            if (object == kNullAddress || !HasRoomForHeader(object))
                continue;
            const std::optional<std::uint32_t> value = reader.ReadUInt32(object + offset);
            if (value && *value == kCommonFlagValue)
                ++carrying;
        }
        if (carrying > required)
            return offset;
    }

    return kOffsetNotFound;
}

std::int32_t FindIndexOffset(const ObjectArray &objects) {
    const MemoryReader &reader = objects.Reader();
    if (objects.Num() <= kIndexProbeB)
        return kOffsetNotFound;

    const Address objectA = objects.ObjectAt(kIndexProbeA);
    const Address objectB = objects.ObjectAt(kIndexProbeB);
    if (objectA == kNullAddress || objectB == kNullAddress)
        return kOffsetNotFound;

    for (std::int32_t offset = sizeof(Address); offset < kMaxHeaderOffset; offset += 4) {
        const std::optional<std::int32_t> valueA = reader.ReadInt32(objectA + offset);
        const std::optional<std::int32_t> valueB = reader.ReadInt32(objectB + offset);
        if (valueA && valueB && *valueA == kIndexProbeA && *valueB == kIndexProbeB)
            return offset;
    }

    return kOffsetNotFound;
}

std::int32_t FindClassOffset(const ObjectArray &objects) {
    const MemoryReader &reader = objects.Reader();
    if (objects.Num() <= kIndexProbeB)
        return kOffsetNotFound;

    const Address objectA = objects.ObjectAt(kIndexProbeA);
    const Address objectB = objects.ObjectAt(kIndexProbeB);
    if (objectA == kNullAddress || objectB == kNullAddress)
        return kOffsetNotFound;

    // Two unrelated objects, so an offset settling for one chain by chance is
    // not mistaken for the class pointer.
    auto ReachesFixedPoint = [&reader](Address startA, Address startB, std::int32_t offset) {
        Address nextA = startA;
        Address nextB = startB;
        for (int step = 0; step < kClassWalkLimit; ++step) {
            const Address currentA = nextA;
            const Address currentB = nextB;
            const std::optional<Address> followedA = reader.ReadPointer(currentA + offset);
            const std::optional<Address> followedB = reader.ReadPointer(currentB + offset);
            if (!followedA || !followedB)
                return false;
            nextA = *followedA;
            nextB = *followedB;
            if (nextA == kNullAddress || nextB == kNullAddress)
                return false;
            if (!reader.Readable(nextA, sizeof(Address)) || !reader.Readable(nextB, sizeof(Address)))
                return false;
            if (currentA == nextA && currentB == nextB)
                return true;
        }
        return false;
    };

    std::int32_t offset = sizeof(Address);
    while (offset != kOffsetNotFound) {
        offset = FindSharedPointerOffset(reader, objectA, objectB, offset, kMaxHeaderOffset);
        if (offset == kOffsetNotFound)
            break;
        if (ReachesFixedPoint(objectA, objectB, offset))
            return offset;
        offset += sizeof(Address);
    }

    return kOffsetNotFound;
}

std::int32_t FindOuterOffset(const ObjectArray &objects, const ObjectOffsets &known) {
    const MemoryReader &reader = objects.Reader();
    const std::int32_t total = objects.Num();
    if (total < 2)
        return kOffsetNotFound;

    // Packages have no Outer, so several pairs are sampled and the lowest
    // offset that holds for any wins.
    constexpr std::int32_t kPairCount = 0x10;
    const std::int32_t span = total < 0x400 ? total : 0x400;

    std::int32_t lowest = kOffsetNotFound;
    for (std::int32_t pair = 0; pair < kPairCount; ++pair) {
        const std::int32_t indexA = (pair * 37) % span;
        const std::int32_t indexB = (indexA + 1 + (pair * 11)) % span;
        if (indexA == indexB)
            continue;

        const Address objectA = objects.ObjectAt(indexA);
        const Address objectB = objects.ObjectAt(indexB);
        if (objectA == kNullAddress || objectB == kNullAddress)
            continue;
        if (!HasRoomForHeader(objectA) || !HasRoomForHeader(objectB))
            continue;

        std::int32_t offset = sizeof(Address);
        while (offset != kOffsetNotFound) {
            offset = FindSharedPointerOffset(reader, objectA, objectB, offset, kMaxHeaderOffset);
            if (offset == kOffsetNotFound)
                break;
            // Class is claimed, and Index can look like a pointer by chance.
            if (offset != known.classPointer && offset != known.index)
                break;
            offset += sizeof(Address);
        }

        if (offset != kOffsetNotFound && (lowest == kOffsetNotFound || offset < lowest))
            lowest = offset;
    }

    return lowest;
}

std::int32_t FindNameOffset(const ObjectArray &objects, const ObjectOffsets &known) {
    const MemoryReader &reader = objects.Reader();
    const std::int32_t total = objects.Num();
    if (total <= 0)
        return kOffsetNotFound;

    struct Candidate {
        std::int32_t offset = 0;
        std::int32_t lowValueCount = 0;
        std::uint64_t total = 0;
        bool inRange = true;
    };

    // Claimed offsets are excluded, as is each claimed pointer's upper half: a
    // 4-aligned read there samples the middle of that pointer.
    auto IsCandidate = [&known](std::int32_t offset) {
        constexpr std::int32_t kPointerHalf = sizeof(Address) / 2;
        for (const std::int32_t claimed : {known.classPointer, known.outer}) {
            if (claimed == kOffsetNotFound)
                continue;
            if (offset == claimed || offset == claimed + kPointerHalf)
                return false;
        }
        return offset != known.flags && offset != known.index;
    };

    std::vector<Candidate> candidates;
    for (std::int32_t offset = sizeof(Address); offset <= kMaxHeaderOffset; offset += 4) {
        if (IsCandidate(offset))
            candidates.push_back(Candidate{offset});
    }
    if (candidates.empty())
        return kOffsetNotFound;

    std::int32_t considered = 0;
    for (std::int32_t index = 0; index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress || !HasRoomForHeader(object))
            continue;
        ++considered;

        for (Candidate &candidate : candidates) {
            const std::optional<std::uint32_t> value = reader.ReadUInt32(object + candidate.offset);
            if (!value) {
                candidate.inRange = false;
                continue;
            }
            candidate.total += *value;
            candidate.inRange = candidate.inRange && *value < kMaxComparisonIndex;
            candidate.lowValueCount += (*value <= kLowComparisonIndexCap);
        }
    }
    if (considered == 0)
        return kOffsetNotFound;

    // Scaled to the sample so a short array is not rejected for its size.
    const std::int32_t lowValueAllowance =
        considered < kMaxNamesWithLowComparisonIndex ? considered / 2 : kMaxNamesWithLowComparisonIndex;

    for (const Candidate &candidate : candidates) {
        if (!candidate.inRange)
            continue;
        if (candidate.lowValueCount > lowValueAllowance)
            continue;
        const std::uint64_t average = candidate.total / static_cast<std::uint64_t>(considered);
        if (average < kMinAverageComparisonIndex || average > kMaxAverageComparisonIndex)
            continue;
        return candidate.offset;
    }

    return kOffsetNotFound;
}

ObjectOffsets FindObjectOffsets(const ObjectArray &objects) {
    ObjectOffsets offsets;

    // Fallbacks place each field behind its predecessor, as the engine declares
    // them, and apply only when measurement failed outright.
    offsets.flags = FindFlagsOffset(objects);
    if (offsets.flags == kOffsetNotFound)
        offsets.flags = sizeof(Address);

    offsets.index = FindIndexOffset(objects);
    if (offsets.index == kOffsetNotFound)
        offsets.index = offsets.flags + sizeof(std::int32_t);

    offsets.classPointer = FindClassOffset(objects);
    if (offsets.classPointer == kOffsetNotFound)
        offsets.classPointer = offsets.index + sizeof(std::int32_t);

    offsets.outer = FindOuterOffset(objects, offsets);

    offsets.name = FindNameOffset(objects, offsets);
    if (offsets.name == kOffsetNotFound)
        offsets.name = offsets.classPointer + sizeof(Address);

    // Outer's fallback is last: it sits behind FName, two int32s here.
    if (offsets.outer == kOffsetNotFound)
        offsets.outer = offsets.name + sizeof(std::int32_t) * 2;

    return offsets;
}

} // namespace URK::Unreal
