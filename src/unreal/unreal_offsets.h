#pragma once

// UObject header calibration. There are no exports and no symbols, and Epic
// moves these fields between versions, so each offset is derived from a property
// only the right field satisfies: Flags recurs at one value across most objects,
// Index stores its own slot number, Class reaches a fixed point when followed,
// Outer is a shared pointer member past Class, Name is what is left with
// FName-like spread. Order matters - later steps exclude earlier claims.

#include "unreal_object_array.h"

#include <cstdint>

namespace URK::Unreal {

struct ObjectOffsets {
    std::int32_t flags = kOffsetNotFound;
    std::int32_t index = kOffsetNotFound;
    std::int32_t classPointer = kOffsetNotFound;
    std::int32_t outer = kOffsetNotFound;
    std::int32_t name = kOffsetNotFound;

    bool Resolved() const {
        return flags != kOffsetNotFound && index != kOffsetNotFound && classPointer != kOffsetNotFound &&
               outer != kOffsetNotFound && name != kOffsetNotFound;
    }
};

std::int32_t FindFlagsOffset(const ObjectArray &objects);
std::int32_t FindIndexOffset(const ObjectArray &objects);
std::int32_t FindClassOffset(const ObjectArray &objects);
std::int32_t FindOuterOffset(const ObjectArray &objects, const ObjectOffsets &known);
std::int32_t FindNameOffset(const ObjectArray &objects, const ObjectOffsets &known);

// Runs the steps in order, falling back to the declared layout where a field
// could not be measured.
ObjectOffsets FindObjectOffsets(const ObjectArray &objects);

} // namespace URK::Unreal
