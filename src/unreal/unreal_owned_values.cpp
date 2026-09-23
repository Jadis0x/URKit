#include "unreal_owned_values.h"

#include <algorithm>
#include <cstring>

namespace URK::Unreal {
namespace {

constexpr int kMaxDepth = 16;
constexpr std::int32_t kMaxFields = 4096;
// FString and every TArray: data pointer, Num, Max.
constexpr std::int32_t kArrayHeader = 16;
// Far above any real array; a larger Num means the header is not one.
constexpr std::int32_t kMaxElements = 1 << 26;

Ownership Worst(Ownership a, Ownership b) { return static_cast<int>(a) > static_cast<int>(b) ? a : b; }

bool AllZero(const std::uint8_t *bytes, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i])
            return false;
    }
    return true;
}

} // namespace

Ownership OwnedValues::Classify(const PropertyInfo &info, int depth) const {
    if (depth > kMaxDepth)
        return Ownership::Unreleasable;
    switch (info.kind) {
    case PropertyKind::Bool:
    case PropertyKind::Byte:
    case PropertyKind::Int8:
    case PropertyKind::Int16:
    case PropertyKind::Int32:
    case PropertyKind::Int64:
    case PropertyKind::UInt16:
    case PropertyKind::UInt32:
    case PropertyKind::UInt64:
    case PropertyKind::Float:
    case PropertyKind::Double:
    case PropertyKind::Enum:
    case PropertyKind::Name:
    case PropertyKind::Object:
    case PropertyKind::Class:
    case PropertyKind::WeakObject:
    case PropertyKind::Interface:
        return Ownership::None;
    case PropertyKind::String:
        return info.elementSize == kArrayHeader ? Ownership::Releasable
                                                                          : Ownership::Unreleasable;
    case PropertyKind::Array: {
        const std::optional<PropertyInfo> inner = values_->Describe(info.inner);
        if (!inner || info.elementSize != kArrayHeader)
            return Ownership::Unreleasable;
        return Classify(*inner, depth + 1) == Ownership::Unreleasable ? Ownership::Unreleasable
                                                                      : Ownership::Releasable;
    }
    case PropertyKind::Struct: {
        Ownership result = Ownership::None;
        int level = 0;
        for (Address owner = info.inner; owner != kNullAddress && level < kMaxDepth;
             owner = types_->SuperOf(owner), ++level) {
            Address field = chain_->First(owner);
            for (std::int32_t step = 0; field != kNullAddress && step < kMaxFields; ++step, field = chain_->Next(field)) {
                const std::optional<PropertyInfo> member = values_->Describe(field);
                if (!member || !member->Resolved())
                    return Ownership::Unreleasable;
                result = Worst(result, Classify(*member, depth + 1));
                if (result == Ownership::Unreleasable)
                    return result;
            }
        }
        return result;
    }
    default:
        return Ownership::Unreleasable;
    }
}

bool OwnedValues::Invoke() {
    return InvokeProcessEvent(processEvent_, library_, function_, parms_.data());
}

bool OwnedValues::Ready() {
    if (state_ != 0)
        return state_ == 1;
    state_ = 2;

    // FString UKismetStringLibrary::Left(const FString& SourceString, int32 Count):
    // for an empty source it returns FString(), which owns nothing.
    const Address klass = finder_->FindInOuter("KismetStringLibrary", "/Script/Engine");
    function_ = klass != kNullAddress ? finder_->FindInOuter("Left", klass) : kNullAddress;
    library_ = klass != kNullAddress ? types_->DefaultObjectOf(klass) : kNullAddress;
    const std::optional<FunctionInfo> info =
        function_ != kNullAddress ? DescribeFunction(*chain_, *values_, *functions_, function_) : std::nullopt;
    if (!info || library_ == kNullAddress || !processEvent_.Resolved()) {
        failure_ = "KismetStringLibrary::Left was not found";
        return false;
    }
    const FunctionParameter *source = info->Parameter("SourceString");
    const FunctionParameter *count = info->Parameter("Count");
    const FunctionParameter *returned = info->Returned();
    const auto fits = [&](const FunctionParameter *parameter, PropertyKind kind, std::int32_t size) {
        return parameter && parameter->info.kind == kind && parameter->info.elementSize == size &&
               parameter->info.offset >= 0 && parameter->info.offset + size <= info->parmsSize;
    };
    if (info->parameters.size() != 3 || !fits(source, PropertyKind::String, kArrayHeader) ||
        !fits(count, PropertyKind::Int32, 4) || !fits(returned, PropertyKind::String, kArrayHeader)) {
        failure_ = "KismetStringLibrary::Left has an unexpected signature";
        return false;
    }
    returnOffset_ = returned->info.offset;
    parms_.assign(static_cast<std::size_t>(info->parmsSize), 0);

    // Measured, not assumed: an empty source must come back as an empty slot.
    if (!Invoke() || !AllZero(parms_.data(), parms_.size())) {
        failure_ = "KismetStringLibrary::Left(\"\", 0) did not return an unallocated FString";
        return false;
    }
    state_ = 1;
    return true;
}

// The engine move-assigns its empty result over the array, which frees the old
// buffer through FMemory. Element types do not matter to that free.
bool OwnedValues::Empty(std::uint8_t *array) {
    if (AllZero(array, static_cast<std::size_t>(kArrayHeader)))
        return true;
    std::fill(parms_.begin(), parms_.end(), std::uint8_t{0});
    std::memcpy(parms_.data() + returnOffset_, array, static_cast<std::size_t>(kArrayHeader));
    if (!Invoke() || !AllZero(parms_.data(), parms_.size())) {
        // The buffer may now be gone or not: stop rather than free twice.
        state_ = 2;
        failure_ = "emptying a value through KismetStringLibrary::Left left memory behind";
        return false;
    }
    std::memset(array, 0, static_cast<std::size_t>(kArrayHeader));
    return true;
}

bool OwnedValues::Release(const PropertyInfo &info, std::uint8_t *value, int depth) {
    if (state_ != 1 || depth > kMaxDepth)
        return false;
    switch (Classify(info, depth)) {
    case Ownership::None:
        return true;
    case Ownership::Unreleasable:
        return false;
    case Ownership::Releasable:
        break;
    }

    if (info.kind == PropertyKind::String)
        return Empty(value);

    if (info.kind == PropertyKind::Array) {
        const std::optional<PropertyInfo> inner = values_->Describe(info.inner);
        if (!inner || inner->elementSize <= 0)
            return false;
        std::uint8_t *data = nullptr;
        std::int32_t num = 0;
        std::int32_t max = 0;
        std::memcpy(&data, value, sizeof(data));
        std::memcpy(&num, value + 8, sizeof(num));
        std::memcpy(&max, value + 12, sizeof(max));
        if (num < 0 || max < num || num > kMaxElements || (num > 0 && !data))
            return false;
        if (Classify(*inner, depth + 1) == Ownership::Releasable) {
            for (std::int32_t i = 0; i < num; ++i) {
                if (!Release(*inner, data + static_cast<std::size_t>(i) * inner->elementSize, depth + 1))
                    return false;
            }
        }
        return Empty(value);
    }

    // A struct: every member that owns memory, supers included.
    int level = 0;
    for (Address owner = info.inner; owner != kNullAddress && level < kMaxDepth; owner = types_->SuperOf(owner), ++level) {
        Address field = chain_->First(owner);
        for (std::int32_t step = 0; field != kNullAddress && step < kMaxFields; ++step, field = chain_->Next(field)) {
            const std::optional<PropertyInfo> member = values_->Describe(field);
            if (!member || !member->Resolved())
                return false;
            if (Classify(*member, depth + 1) != Ownership::Releasable)
                continue;
            for (std::int32_t i = 0; i < member->arrayDim; ++i) {
                std::uint8_t *at = value + member->offset + static_cast<std::size_t>(i) * member->elementSize;
                if (!Release(*member, at, depth + 1))
                    return false;
            }
        }
    }
    return true;
}

} // namespace URK::Unreal
