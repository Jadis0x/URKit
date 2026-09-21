#pragma once

// What a call needs before it can be made: which parameters a function takes,
// how large the block holding them is, and where the returned value lands in
// it.
//
// None of those numbers has to be trusted to a guessed offset, because the
// property chain already knows what they must be - the parameters are the
// properties flagged as such, the block is as large as the last of them ends,
// and the return value is the one flagged as returned. So the offsets are found
// by looking for where the engine happens to store numbers it has already
// agreed with. Two functions with different numbers are enough to pin them.
//
// Making the call itself is not here: that needs ProcessEvent, which cannot be
// located without a real game to locate it in.

#include "unreal_module.h"
#include "unreal_property_values.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace URK::Unreal {

// Function flags the engine assigns. Used to describe, not to anchor.
inline constexpr std::uint32_t kFunctionFlagFinal = 0x00000001;
inline constexpr std::uint32_t kFunctionFlagPublic = 0x00000004;
inline constexpr std::uint32_t kFunctionFlagNative = 0x00000400;
inline constexpr std::uint32_t kFunctionFlagEvent = 0x00000800;
inline constexpr std::uint32_t kFunctionFlagStatic = 0x00002000;
inline constexpr std::uint32_t kFunctionFlagBlueprintCallable = 0x04000000;

struct FunctionOffsets {
    std::int32_t functionFlags = kOffsetNotFound;
    std::int32_t numParms = kOffsetNotFound;
    std::int32_t parmsSize = kOffsetNotFound;
    std::int32_t returnValueOffset = kOffsetNotFound;
    // The native entry point, found only when code sections are supplied.
    std::int32_t func = kOffsetNotFound;

    bool Resolved() const {
        return numParms != kOffsetNotFound && parmsSize != kOffsetNotFound && returnValueOffset != kOffsetNotFound;
    }
};

// The counts the engine stores, and what they must equal. codeRegions may be
// empty, in which case the native entry point is left unresolved and everything
// else is still measured.
FunctionOffsets FindFunctionOffsets(const ObjectFinder &finder, const StructOffsets &structs,
                                    const FieldOffsets &fields, const PropertyTailOffsets &tail,
                                    std::span<const ScanRegion> codeRegions = {});

struct FunctionParameter {
    std::string name;
    PropertyInfo info;
    bool returned = false;
};

// One function, ready to be called: its parameters in declaration order, the
// block they sit in, and which of them the call answers with.
struct FunctionInfo {
    Address function = kNullAddress;
    std::uint32_t flags = 0;
    std::int32_t parmsSize = 0;
    Address nativeEntry = kNullAddress;

    std::vector<FunctionParameter> parameters;
    // Index into parameters, or -1 when the function answers with nothing.
    std::int32_t returnValue = -1;

    bool Native() const { return (flags & kFunctionFlagNative) != 0; }
    bool Static() const { return (flags & kFunctionFlagStatic) != 0; }

    const FunctionParameter *Parameter(std::string_view name) const;
    const FunctionParameter *Returned() const {
        return returnValue < 0 ? nullptr : &parameters[static_cast<std::size_t>(returnValue)];
    }
};

// Reads a function's parameters out of its chain. The size the engine stores is
// preferred, and what the parameters imply is used when it is not resolved.
std::optional<FunctionInfo> DescribeFunction(const PropertyChain &chain, const PropertyValues &values,
                                             const FunctionOffsets &offsets, Address function);

// Clears a parameter block before it is filled. The engine reads every byte of
// it, including the ones no parameter covers.
bool ClearFrame(MemoryWriter &writer, Address frame, const FunctionInfo &info);

} // namespace URK::Unreal
