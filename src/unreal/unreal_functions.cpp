#include "unreal_functions.h"

#include <algorithm>
#include <array>
#include <vector>

namespace URK::Unreal {
namespace {

// UFunction's counts sit past UStruct in a fixed order, but the order is not
// trusted: each is checked against what the parameters say it must be.
constexpr std::int32_t kNumParmsToParmsSize = 2;
constexpr std::int32_t kNumParmsToReturnValueOffset = 4;
constexpr std::int32_t kFunctionFlagsToNumParms = 4;

// The engine pads a parameter block out to the alignment of what is in it, so
// what the properties imply is a floor rather than the number itself.
constexpr std::int32_t kParmsSizeSlack = 0x10;

constexpr std::int32_t kMaxFunctionOffset = 0x200;
constexpr std::int32_t kMaxFuncPointerOffset = 0x240;
constexpr std::int32_t kMaxChainLength = 0x100;
constexpr std::size_t kFunctionSamples = 6;
constexpr std::int32_t kMaxObjectsWalked = 0x4000;

// What a function's own chain says about it, which is what the stored numbers
// have to agree with.
struct Expected {
    Address function = kNullAddress;
    std::int32_t numParms = 0;
    std::int32_t parmsSize = 0;
    std::int32_t returnValueOffset = 0;
    bool hasReturnValue = false;
};

bool IsParameter(const PropertyInfo &info) { return (info.propertyFlags & kPropertyFlagParm) != 0; }

bool IsReturnValue(const PropertyInfo &info) { return (info.propertyFlags & kPropertyFlagReturnParm) != 0; }

// Walks one function's properties and works out what the engine must have
// stored for it.
std::optional<Expected> ExpectedOf(const PropertyChain &chain, const PropertyValues &values, Address function) {
    Expected expected;
    expected.function = function;

    Address field = chain.First(function);
    for (std::int32_t step = 0; field != kNullAddress && step < kMaxChainLength; ++step) {
        const std::optional<PropertyInfo> info = values.Describe(field);
        if (!info)
            return std::nullopt;

        if (IsParameter(*info)) {
            ++expected.numParms;
            const std::int32_t end = info->offset + info->elementSize * info->arrayDim;
            expected.parmsSize = std::max(expected.parmsSize, end);
            if (IsReturnValue(*info)) {
                expected.returnValueOffset = info->offset;
                expected.hasReturnValue = true;
            }
        }
        field = chain.Next(field);
    }

    if (expected.numParms == 0)
        return std::nullopt;
    return expected;
}

// Which functions are sampled matters more than how many: a game's first
// hundred are delegate signatures with identical shapes, which pin nothing.
// So duplicate shapes are skipped and ones with a return value preferred.
std::vector<Expected> CollectFunctions(const ObjectFinder &finder, const StructOffsets &structs,
                                       const PropertyChain &chain, const PropertyValues &values) {
    std::vector<Expected> samples;
    const ObjectArray &objects = finder.Objects();

    const auto sameShape = [](const Expected &left, const Expected &right) {
        return left.numParms == right.numParms && left.parmsSize == right.parmsSize &&
               left.hasReturnValue == right.hasReturnValue;
    };

    const std::int32_t total = objects.Num();
    const std::int32_t walked = total < kMaxObjectsWalked ? total : kMaxObjectsWalked;
    std::size_t withReturnValue = 0;
    for (std::int32_t index = 0; index < walked && samples.size() < kFunctionSamples; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;
        if (!ObjectIs(finder, structs, object, kCastFlagFunction))
            continue;

        const std::optional<Expected> expected = ExpectedOf(chain, values, object);
        if (!expected)
            continue;
        if (std::any_of(samples.begin(), samples.end(),
                        [&](const Expected &other) { return sameShape(other, *expected); })) {
            continue;
        }

        samples.push_back(*expected);
        withReturnValue += expected->hasReturnValue ? 1 : 0;
    }

    // A set that answers with nothing leaves ReturnValueOffset unanchored, so
    // one that does is worth looking further for.
    if (withReturnValue == 0) {
        for (std::int32_t index = walked; index < total && withReturnValue == 0; ++index) {
            const Address object = objects.ObjectAt(index);
            if (object == kNullAddress || !ObjectIs(finder, structs, object, kCastFlagFunction))
                continue;
            const std::optional<Expected> expected = ExpectedOf(chain, values, object);
            if (expected && expected->hasReturnValue) {
                samples.push_back(*expected);
                ++withReturnValue;
            }
        }
    }

    return samples;
}

bool PointsIntoRegions(Address pointer, std::span<const ScanRegion> regions) {
    for (const ScanRegion &region : regions) {
        if (pointer >= region.start && pointer < region.start + region.size)
            return true;
    }
    return false;
}

// The one pointer a UFunction keeps into code. Script functions have one too
// (the interpreter), so flags cannot identify it; everything else past the
// counts points at the heap.
std::int32_t FindFuncOffset(const MemoryReader &reader, const std::vector<Expected> &samples,
                            std::span<const ScanRegion> codeRegions, std::int32_t start) {
    if (codeRegions.empty() || samples.empty())
        return kOffsetNotFound;

    for (std::int32_t offset = start; offset <= kMaxFuncPointerOffset;
         offset += static_cast<std::int32_t>(sizeof(Address))) {
        bool satisfied = true;
        for (const Expected &sample : samples) {
            const std::optional<Address> pointer = reader.ReadPointer(sample.function + offset);
            if (!pointer || *pointer == kNullAddress || !PointsIntoRegions(*pointer, codeRegions)) {
                satisfied = false;
                break;
            }
        }
        if (satisfied)
            return offset;
    }

    return kOffsetNotFound;
}

} // namespace

FunctionOffsets FindFunctionOffsets(const ObjectFinder &finder, const StructOffsets &structs,
                                    const FieldOffsets &fields, const PropertyTailOffsets &tail,
                                    std::span<const ScanRegion> codeRegions) {
    FunctionOffsets resolved;
    if (!fields.Resolved() || structs.castFlags == kOffsetNotFound)
        return resolved;

    const MemoryReader &reader = finder.Reader();
    const PropertyChain chain(reader, finder.Names(), structs, fields);
    const PropertyValues values(reader, finder.Names(), structs, fields, tail);

    const std::vector<Expected> samples = CollectFunctions(finder, structs, chain, values);
    // Defensive: a function's numbers are small and a real UStruct is full of
    // small numbers, so one sample could match an earlier offset by chance.
    const bool differ = std::any_of(samples.begin(), samples.end(), [&](const Expected &other) {
        return other.numParms != samples.front().numParms || other.parmsSize != samples.front().parmsSize;
    });
    if (samples.size() < 2 || !differ)
        return resolved;

    const std::int32_t start = FirstOffsetPastHeader(finder.Offsets());
    for (std::int32_t offset = start; offset <= kMaxFunctionOffset; ++offset) {
        bool satisfied = true;

        for (const Expected &sample : samples) {
            const std::optional<std::uint8_t> numParms = reader.ReadAs<std::uint8_t>(sample.function + offset);
            const std::optional<std::uint16_t> parmsSize =
                reader.ReadAs<std::uint16_t>(sample.function + offset + kNumParmsToParmsSize);
            const std::optional<std::uint16_t> returnValueOffset =
                reader.ReadAs<std::uint16_t>(sample.function + offset + kNumParmsToReturnValueOffset);
            if (!numParms || !parmsSize || !returnValueOffset) {
                satisfied = false;
                break;
            }

            if (*numParms != sample.numParms) {
                satisfied = false;
                break;
            }
            if (*parmsSize < sample.parmsSize || *parmsSize > sample.parmsSize + kParmsSizeSlack) {
                satisfied = false;
                break;
            }
            // Only evidence when there is a return value to point at.
            if (sample.hasReturnValue && *returnValueOffset != sample.returnValueOffset) {
                satisfied = false;
                break;
            }
        }

        if (!satisfied)
            continue;

        resolved.numParms = offset;
        resolved.parmsSize = offset + kNumParmsToParmsSize;
        resolved.returnValueOffset = offset + kNumParmsToReturnValueOffset;
        resolved.functionFlags = offset - kFunctionFlagsToNumParms;
        resolved.func = FindFuncOffset(reader, samples, codeRegions, offset + kNumParmsToReturnValueOffset);
        return resolved;
    }

    return resolved;
}

const FunctionParameter *FunctionInfo::Parameter(std::string_view name) const {
    const auto found = std::find_if(parameters.begin(), parameters.end(),
                                    [name](const FunctionParameter &parameter) { return parameter.name == name; });
    return found == parameters.end() ? nullptr : &*found;
}

std::optional<FunctionInfo> DescribeFunction(const PropertyChain &chain, const PropertyValues &values,
                                             const FunctionOffsets &offsets, Address function) {
    if (function == kNullAddress)
        return std::nullopt;

    const std::optional<Expected> expected = ExpectedOf(chain, values, function);

    FunctionInfo info;
    info.function = function;

    Address field = chain.First(function);
    for (std::int32_t step = 0; field != kNullAddress && step < kMaxChainLength; ++step) {
        const std::optional<PropertyInfo> property = values.Describe(field);
        if (!property)
            return std::nullopt;

        // Only parameters belong to the block; locals do not.
        if (IsParameter(*property)) {
            FunctionParameter parameter;
            parameter.info = *property;
            parameter.returned = IsReturnValue(*property);
            if (const std::optional<std::string> name = chain.NameOf(field))
                parameter.name = *name;
            if (parameter.returned)
                info.returnValue = static_cast<std::int32_t>(info.parameters.size());
            info.parameters.push_back(std::move(parameter));
        }
        field = chain.Next(field);
    }

    if (offsets.Resolved()) {
        if (const std::optional<std::uint16_t> parmsSize = chain.Reader().ReadAs<std::uint16_t>(
                function + offsets.parmsSize))
            info.parmsSize = *parmsSize;
        if (const std::optional<std::uint32_t> flags = chain.Reader().ReadUInt32(function + offsets.functionFlags))
            info.flags = *flags;
        if (offsets.func != kOffsetNotFound) {
            if (const std::optional<Address> entry = chain.Reader().ReadPointer(function + offsets.func))
                info.nativeEntry = *entry;
        }
    }

    // What the parameters imply, when the engine's own number is not available.
    if (info.parmsSize == 0 && expected)
        info.parmsSize = expected->parmsSize;

    return info;
}

bool ClearFrame(MemoryWriter &writer, Address frame, const FunctionInfo &info) {
    if (frame == kNullAddress || info.parmsSize <= 0)
        return false;

    const std::vector<std::uint8_t> zeroes(static_cast<std::size_t>(info.parmsSize), 0);
    return writer.Write(frame, zeroes.data(), zeroes.size());
}

} // namespace URK::Unreal
