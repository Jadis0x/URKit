#pragma once

// UField and UStruct layout calibration, the rung above the UObject header.
//
// Each field is pinned by anchoring on engine objects whose value is fixed by
// the engine itself: FColor is always four bytes, FGuid sixteen, AActor's class
// carries exactly the Actor cast flag. An offset that satisfies every anchor at
// once is the field.

#include "unreal_object_finder.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace URK::Unreal {

// Cast flags the engine assigns to its own classes. Used as anchor values, not
// as a classification table.
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

    // From UE4.25 properties stopped being UObjects and moved to a separate
    // FField chain, which changes what Children holds.
    bool usesFProperty = false;
};

// A known object paired with the value the field must hold for it.
template <typename T> struct Anchor {
    Address object = kNullAddress;
    T value{};
};

// The lowest offset at which every anchor holds its value. Anchors whose object
// was not found make the search fail rather than silently weaken it.
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

// What an object is, read off the class it belongs to.
//
// ClassCastFlags describes a class's *instances*, not the class object holding
// it - AActor's flags say "an actor", not "a class" - and only UClass declares
// the field at all, so asking an object for its own flags asks a different
// question and asks it of memory that may not be a UClass. The flags that
// describe an object are its class's.
std::uint64_t CastFlagsOf(const ObjectFinder &finder, const StructOffsets &structs, Address object);

bool ObjectIs(const ObjectFinder &finder, const StructOffsets &structs, Address object, std::uint64_t flag);

// Properties are UObjects before UE4.25 and FFields after, so looking for a
// known property in the object array tells the two systems apart.
bool UsesFPropertySystem(const ObjectFinder &finder);

std::int32_t FindCastFlagsOffset(const ObjectFinder &finder);
std::int32_t FindSuperStructOffset(const ObjectFinder &finder);
std::int32_t FindChildrenOffset(const ObjectFinder &finder, bool usesFProperty);
std::int32_t FindPropertiesSizeOffset(const ObjectFinder &finder);
std::int32_t FindMinAlignmentOffset(const ObjectFinder &finder);
std::int32_t FindFieldNextOffset(const ObjectFinder &finder, std::int32_t childrenOffset);

StructOffsets FindStructOffsets(const ObjectFinder &finder);

} // namespace URK::Unreal
