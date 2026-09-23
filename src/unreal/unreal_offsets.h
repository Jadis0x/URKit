#pragma once

// UObject header offsets, each found by a property only the right field has.
// Order matters: later steps exclude earlier claims.

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
