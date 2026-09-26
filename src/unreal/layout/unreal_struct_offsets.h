#pragma once

// UField/UStruct layout: the offset satisfying every anchor (FColor, FGuid, AActor).

#include "unreal/reflection/unreal_object_finder.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace URK::Unreal {

// Engine class cast flags, used as anchor values.
inline constexpr std::uint64_t kCastFlagField = 0x1;
inline constexpr std::uint64_t kCastFlagStruct = 0x8;
inline constexpr std::uint64_t kCastFlagClass = 0x20;
inline constexpr std::uint64_t kCastFlagFunction = 0x80000;
inline constexpr std::uint64_t kCastFlagActor = 0x1000000000;

struct StructOffsets {
    std::int32_t castFlags = kOffsetNotFound;
    std::int32_t superStruct = kOffsetNotFound;
    std::int32_t children = kOffsetNotFound;
    std::int32_t propertiesSize = kOffsetNotFound;
    std::int32_t minAlignment = kOffsetNotFound;
    std::int32_t fieldNext = kOffsetNotFound;
};

// A known object paired with the value the field must hold for it.
template <typename T> struct Anchor {
    Address object = kNullAddress;
    T value{};
};

// Lowest offset where every anchor holds; a missing anchor fails the search.
template <typename T>
std::int32_t FindAnchoredOffset(const MemoryReader &reader, const std::vector<Anchor<T>> &anchors,
                                std::int32_t minOffset, std::int32_t maxOffset, std::int32_t step = 4) {
    if (anchors.empty())
        return kOffsetNotFound;
    for (const Anchor<T> &anchor : anchors) {
        if (anchor.object == kNullAddress)
            return kOffsetNotFound;
    }

    for (std::int32_t offset = minOffset; offset <= maxOffset; offset += step) {
        bool satisfied = true;
        for (const Anchor<T> &anchor : anchors) {
            const std::optional<T> value = reader.ReadAs<T>(anchor.object + offset);
            if (!value || *value != anchor.value) {
                satisfied = false;
                break;
            }
        }
        if (satisfied)
            return offset;
    }
    return kOffsetNotFound;
}

// First offset past the UObject header, which is where UField begins.
std::int32_t FirstOffsetPastHeader(const ObjectOffsets &offsets);

// Read off the object's class; only UClass declares it.
std::uint64_t CastFlagsOf(const ObjectFinder &finder, const StructOffsets &structs, Address object);

bool ObjectIs(const ObjectFinder &finder, const StructOffsets &structs, Address object, std::uint64_t flag);

// Pre-4.25 properties are UObjects; finding one means unsupported.
bool UsesFPropertySystem(const ObjectFinder &finder);

std::int32_t FindCastFlagsOffset(const ObjectFinder &finder);
std::int32_t FindSuperStructOffset(const ObjectFinder &finder);
std::int32_t FindChildrenOffset(const ObjectFinder &finder);
std::int32_t FindPropertiesSizeOffset(const ObjectFinder &finder);
std::int32_t FindMinAlignmentOffset(const ObjectFinder &finder);
std::int32_t FindFieldNextOffset(const ObjectFinder &finder, std::int32_t childrenOffset);

StructOffsets FindStructOffsets(const ObjectFinder &finder);

} // namespace URK::Unreal
