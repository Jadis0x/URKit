#pragma once

// UFunction parameter block. Offsets are where the engine stores numbers that
// agree with what the property chain already implies.

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

// codeRegions may be empty: only the native entry point goes unresolved then.
FunctionOffsets FindFunctionOffsets(const ObjectFinder &finder, const StructOffsets &structs,
                                    const FieldOffsets &fields, const PropertyTailOffsets &tail,
                                    std::span<const ScanRegion> codeRegions = {});

struct FunctionParameter {
    std::string name;
    PropertyInfo info;
    bool returned = false;
};

// One function, ready to be called; parameters in declaration order.
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

// Prefers the stored ParmsSize; falls back to what the parameters imply.
std::optional<FunctionInfo> DescribeFunction(const PropertyChain &chain, const PropertyValues &values,
                                             const FunctionOffsets &offsets, Address function);

// The engine reads every byte, padding included, so zero it whole.
bool ClearFrame(MemoryWriter &writer, Address frame, const FunctionInfo &info);

} // namespace URK::Unreal
