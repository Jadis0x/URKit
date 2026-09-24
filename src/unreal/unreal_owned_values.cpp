#include "unreal_owned_values.h"

#include <algorithm>
#include <cstring>

namespace URK::Unreal {
namespace {

constexpr int kMaxDepth = 16;
constexpr std::int32_t kMaxFields = 4096;
// FScriptDelegate: a weak object pointer and an FName. Builds with dynamic
// delegate payloads add a shared pointer, which is not measured here.
constexpr std::int32_t kPlainDelegateSize = 16;

Ownership Worst(Ownership a, Ownership b) {
    return static_cast<int>(a) > static_cast<int>(b) ? a : b;
}

template <typename T> T Load(const std::uint8_t *at) {
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

char16_t Folded(char16_t ch) {
    return ch >= u'A' && ch <= u'Z' ? static_cast<char16_t>(ch + 32) : ch;
}

} // namespace

bool OwnedValues::Fail(std::string why) {
    failure_ = std::move(why);
    return false;
}

// Members of a struct, supers included, each with its resolved shape.
template <typename Visit> bool OwnedValues::ForEachMember(Address structObject, Visit visit, int depth) const {
    int level = 0;
    for (Address owner = structObject; owner != kNullAddress && level < kMaxDepth;
         owner = types_->SuperOf(owner), ++level) {
        Address field = chain_->First(owner);
        for (std::int32_t step = 0; field != kNullAddress && step < kMaxFields; ++step, field = chain_->Next(field)) {
            const std::optional<PropertyInfo> member = values_->Describe(field);
            if (!member || !member->Resolved() || member->elementSize <= 0 || member->arrayDim < 1)
                return false;
            if (!visit(*member))
                return false;
        }
    }
    (void)depth;
    return true;
}

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
        case PropertyKind::LazyObject:
        case PropertyKind::Interface:
        case PropertyKind::SparseDelegate:
            return Ownership::None;
        case PropertyKind::Delegate:
            return info.elementSize == kPlainDelegateSize ? Ownership::None : Ownership::Unreleasable;
        case PropertyKind::String:
        case PropertyKind::Utf8String:
        case PropertyKind::AnsiString:
        case PropertyKind::MulticastDelegate:
            return info.elementSize == kArrayHeaderSize ? Ownership::Owned : Ownership::Unreleasable;
        case PropertyKind::Text:
        case PropertyKind::SoftObject:
            return Ownership::Owned;
        case PropertyKind::Array: {
            const std::optional<PropertyInfo> inner = values_->Describe(info.inner);
            if (!inner || info.elementSize != kArrayHeaderSize)
                return Ownership::Unreleasable;
            return Classify(*inner, depth + 1) == Ownership::Unreleasable ? Ownership::Unreleasable : Ownership::Owned;
        }
        case PropertyKind::Set:
        case PropertyKind::Map: {
            const std::optional<PropertyInfo> key = values_->Describe(info.inner);
            if (!key || Classify(*key, depth + 1) == Ownership::Unreleasable)
                return Ownership::Unreleasable;
            if (info.kind == PropertyKind::Map) {
                const std::optional<PropertyInfo> value = values_->Describe(info.valueInner);
                if (!value || Classify(*value, depth + 1) == Ownership::Unreleasable)
                    return Ownership::Unreleasable;
            }
            return Ownership::Owned;
        }
        case PropertyKind::Struct: {
            Ownership result = Ownership::None;
            const bool known = ForEachMember(
                info.inner,
                [&](const PropertyInfo &member) {
                    result = Worst(result, Classify(member, depth + 1));
                    return result != Ownership::Unreleasable;
                },
                depth);
            return known ? result : Ownership::Unreleasable;
        }
        default:
            return Ownership::Unreleasable;
    }
}

bool OwnedValues::NeedsInitialize(const PropertyInfo &info, int depth) const {
    if (depth > kMaxDepth)
        return false;
    if (info.kind == PropertyKind::Text)
        return true;
    if (info.kind != PropertyKind::Struct)
        return false;
    bool needs = false;
    ForEachMember(
        info.inner,
        [&](const PropertyInfo &member) {
            needs = needs || NeedsInitialize(member, depth + 1);
            return !needs;
        },
        depth);
    return needs;
}

bool OwnedValues::HoldsText(const PropertyInfo &info, int depth) const {
    if (depth > kMaxDepth)
        return false;
    switch (info.kind) {
        case PropertyKind::Text:
            return true;
        case PropertyKind::Array:
        case PropertyKind::Set: {
            const std::optional<PropertyInfo> inner = values_->Describe(info.inner);
            return inner && HoldsText(*inner, depth + 1);
        }
        case PropertyKind::Map: {
            const std::optional<PropertyInfo> key = values_->Describe(info.inner);
            const std::optional<PropertyInfo> value = values_->Describe(info.valueInner);
            return (key && HoldsText(*key, depth + 1)) || (value && HoldsText(*value, depth + 1));
        }
        case PropertyKind::Struct: {
            bool holds = false;
            ForEachMember(
                info.inner,
                [&](const PropertyInfo &member) {
                    holds = holds || HoldsText(member, depth + 1);
                    return !holds;
                },
                depth);
            return holds;
        }
        default:
            return false;
    }
}

bool OwnedValues::FillNullTexts(const PropertyInfo &info, std::uint8_t *value, bool *made, int depth) {
    if (depth > kMaxDepth)
        return Fail("a value is nested too deeply");
    if (info.kind == PropertyKind::Text) {
        if (Load<void *>(value))
            return true;
        if (!engine_->MakeEmptyText(value))
            return Fail(engine_->Failure());
        *made = true;
        return true;
    }
    if (info.kind != PropertyKind::Struct || !NeedsInitialize(info, depth))
        return true;
    const bool done = ForEachMember(
        info.inner,
        [&](const PropertyInfo &member) {
            if (!NeedsInitialize(member, depth + 1))
                return true;
            for (std::int32_t i = 0; i < member.arrayDim; ++i) {
                if (!FillNullTexts(member, value + member.offset + static_cast<std::size_t>(i) * member.elementSize,
                                   made, depth + 1))
                    return false;
            }
            return true;
        },
        depth);
    return done ? true : (failure_.empty() ? Fail("a struct's members did not resolve") : false);
}

std::int32_t OwnedValues::DelegateSize() {
    if (const std::int32_t known = delegateSize_.load(std::memory_order_acquire); known >= 0)
        return known;
    // Any reflected single-cast delegate names FScriptDelegate's size; the timer
    // library's has existed since UE4.
    std::int32_t size = 0;
    const Address library = finder_->FindInOuter("KismetSystemLibrary", "/Script/Engine");
    const Address function =
        library != kNullAddress ? finder_->FindInOuter("K2_SetTimerDelegate", library) : kNullAddress;
    const Address field = function != kNullAddress ? chain_->FindMember(function, "Delegate") : kNullAddress;
    if (const std::optional<PropertyInfo> delegate = values_->Describe(field);
        delegate && delegate->kind == PropertyKind::Delegate)
        size = delegate->elementSize;
    delegateSize_.store(size, std::memory_order_release);
    return size;
}

PropertyInfo OwnedValues::DelegateElement() {
    PropertyInfo element;
    element.kind = PropertyKind::Delegate;
    element.elementSize = DelegateSize();
    element.offset = 0;
    element.field = kNullAddress;
    return element;
}

bool OwnedValues::Destroy(const PropertyInfo &info, std::uint8_t *value, int depth) {
    if (!Release(info, value, depth, false))
        return false;
    leaks_ = 0;
    Release(info, value, depth, true);
    if (leaks_ > 0)
        NoteMemory(std::to_string(leaks_) + " part(s) of a " + PropertyKindName(info.kind) +
                   " value were leaked, not freed: " + leakReason_);
    return true;
}

bool OwnedValues::Releasable(const PropertyInfo &info, const std::uint8_t *value) {
    return Release(info, const_cast<std::uint8_t *>(value), 0, false);
}

void OwnedValues::Leaked(const std::string &why) {
    if (leaks_++ == 0)
        leakReason_ = why;
}

// One walk for both passes, so the check covers exactly what the release does.
// Checking (apply false) touches nothing and refuses on anything that could
// fail. Releasing (apply true) runs only after the check passed; an engine call
// failing then is leaked and counted, the walk goes on, and the value ends zeroed.
bool OwnedValues::Release(const PropertyInfo &info, std::uint8_t *value, int depth, bool apply) {
    if (depth > kMaxDepth)
        return Fail("a value is nested too deeply");
    const Ownership ownership = Classify(info, depth);
    if (ownership == Ownership::Unreleasable)
        return Fail(std::string("a ") + PropertyKindName(info.kind) + " value cannot be released here");
    // An engine step: refused when checking, leaked when releasing.
    const auto step = [&](bool done, const std::string &why) {
        if (done)
            return true;
        if (!apply)
            return Fail(why);
        Leaked(why);
        return true;
    };
    const auto freeable = [&](const std::uint8_t *header) {
        return apply ? engine_->EmptyArray(const_cast<std::uint8_t *>(header))
                     : (!Load<void *>(header) || engine_->FreeReady());
    };
    const auto nested = [&](const PropertyInfo &inner, std::uint8_t *at) {
        return Release(inner, at, depth + 1, apply) || step(false, failure_);
    };
    if (ownership == Ownership::Owned) {
        switch (info.kind) {
            case PropertyKind::String:
            case PropertyKind::Utf8String:
            case PropertyKind::AnsiString:
                if (!step(freeable(value), engine_->Failure()))
                    return false;
                break;
            case PropertyKind::MulticastDelegate:
                // Its bindings own nothing; only the list's buffer goes back.
                if (DelegateSize() != kPlainDelegateSize)
                    return Fail("delegates here are not the plain weak-object-and-name layout");
                if (!step(freeable(value), engine_->Failure()))
                    return false;
                break;
            case PropertyKind::Text:
                if (!step(apply ? engine_->ReleaseText(value) : engine_->CanReleaseText(value), engine_->Failure()))
                    return false;
                break;
            case PropertyKind::SoftObject: {
                const std::int32_t at = engine_->SoftPathOffset();
                const Address path = engine_->SoftPathStruct();
                if (at == kOffsetNotFound || path == kNullAddress)
                    return Fail(engine_->Failure());
                PropertyInfo pathInfo;
                pathInfo.kind = PropertyKind::Struct;
                pathInfo.inner = path;
                pathInfo.elementSize = info.elementSize - at;
                if (!nested(pathInfo, value + at))
                    return false;
                break;
            }
            case PropertyKind::Array: {
                const std::optional<PropertyInfo> inner = values_->Describe(info.inner);
                auto *data = Load<std::uint8_t *>(value);
                const std::int32_t num = Load<std::int32_t>(value + 8);
                const std::int32_t max = Load<std::int32_t>(value + 12);
                if (!inner || inner->elementSize <= 0 || num < 0 || max < num || num > kMaxContainerElements ||
                    (num > 0 && !data))
                    return Fail("an array is not in a state it can be released from");
                if (Classify(*inner, depth + 1) == Ownership::Owned) {
                    for (std::int32_t i = 0; i < num; ++i) {
                        if (!nested(*inner, data + static_cast<std::size_t>(i) * inner->elementSize))
                            return false;
                    }
                }
                if (!step(freeable(value), engine_->Failure()))
                    return false;
                break;
            }
            case PropertyKind::Set:
            case PropertyKind::Map: {
                const std::optional<SetLayout> layout = containers_.LayoutOf(info);
                if (!layout)
                    return Fail(containers_.Failure());
                if (!containers_.Validate(*layout, value))
                    return Fail(containers_.Failure());
                const bool keys = Classify(layout->key, depth + 1) == Ownership::Owned;
                const bool values = layout->isMap && Classify(layout->value, depth + 1) == Ownership::Owned;
                for (const std::int32_t slot : Containers::Slots(value)) {
                    std::uint8_t *element = Containers::Element(*layout, value, slot);
                    if ((keys && !nested(layout->key, element)) ||
                        (values && !nested(layout->value, element + layout->valueOffset)))
                        return false;
                }
                if (apply)
                    containers_.FreeStorage(value);
                else if (Containers::HoldsStorage(value) && !engine_->FreeReady())
                    return Fail(engine_->Failure());
                break;
            }
            case PropertyKind::Struct: {
                const bool done = ForEachMember(
                    info.inner,
                    [&](const PropertyInfo &member) {
                        if (Classify(member, depth + 1) != Ownership::Owned)
                            return true;
                        for (std::int32_t i = 0; i < member.arrayDim; ++i) {
                            const std::size_t at = static_cast<std::size_t>(member.offset) +
                                                   static_cast<std::size_t>(i) * member.elementSize;
                            if (at + member.elementSize > static_cast<std::size_t>(info.elementSize) &&
                                info.elementSize > 0)
                                return Fail("a struct member lies outside its value");
                            if (!nested(member, value + at))
                                return false;
                        }
                        return true;
                    },
                    depth);
                if (!done)
                    return failure_.empty() ? Fail("a struct's members did not resolve") : false;
                break;
            }
            default:
                return Fail(std::string("a ") + PropertyKindName(info.kind) + " value cannot be released here");
        }
    }
    if (apply && info.elementSize > 0)
        std::memset(value, 0, static_cast<std::size_t>(info.elementSize));
    return true;
}

bool OwnedValues::Initialize(const PropertyInfo &info, std::uint8_t *value, int depth) {
    if (depth > kMaxDepth)
        return Fail("a value is nested too deeply");
    if (info.kind == PropertyKind::Text)
        return engine_->MakeEmptyText(value) ? true : Fail(engine_->Failure());
    // A struct as the engine makes one: its C++ constructor (a vtable, native
    // defaults) and every member, texts included.
    if (info.kind == PropertyKind::Struct && containers_.Virtuals().Initialize(info, value))
        return true;
    if (info.kind != PropertyKind::Struct || !NeedsInitialize(info, depth))
        return true;
    const bool done = ForEachMember(
        info.inner,
        [&](const PropertyInfo &member) {
            if (!NeedsInitialize(member, depth + 1))
                return true;
            for (std::int32_t i = 0; i < member.arrayDim; ++i) {
                if (!Initialize(member, value + member.offset + static_cast<std::size_t>(i) * member.elementSize,
                                depth + 1))
                    return false;
            }
            return true;
        },
        depth);
    return done ? true : (failure_.empty() ? Fail("a struct's members did not resolve") : false);
}

bool OwnedValues::Equal(const PropertyInfo &info, const std::uint8_t *a, const std::uint8_t *b, int depth) const {
    if (depth > kMaxDepth || info.elementSize <= 0)
        return false;
    const std::size_t size = static_cast<std::size_t>(info.elementSize);
    switch (info.kind) {
        case PropertyKind::Bool: {
            const std::uint8_t mask = info.boolLayout.fieldMask;
            const std::uint8_t at = info.boolLayout.byteOffset;
            return ((a[at] & mask) != 0) == ((b[at] & mask) != 0);
        }
        case PropertyKind::Float:
            return Load<float>(a) == Load<float>(b);
        case PropertyKind::Double:
            return Load<double>(a) == Load<double>(b);
        case PropertyKind::String: {
            // FString's operator== ignores ASCII case.
            const auto *left = Load<const char16_t *>(a);
            const auto *right = Load<const char16_t *>(b);
            const std::int32_t leftLength = left ? std::max(Load<std::int32_t>(a + 8) - 1, 0) : 0;
            const std::int32_t rightLength = right ? std::max(Load<std::int32_t>(b + 8) - 1, 0) : 0;
            if (leftLength != rightLength)
                return false;
            for (std::int32_t i = 0; i < leftLength; ++i) {
                if (Folded(left[i]) != Folded(right[i]))
                    return false;
            }
            return true;
        }
        case PropertyKind::Struct: {
            bool same = true;
            const bool known = ForEachMember(
                info.inner,
                [&](const PropertyInfo &member) {
                    for (std::int32_t i = 0; i < member.arrayDim && same; ++i) {
                        const std::size_t at =
                            static_cast<std::size_t>(member.offset) + static_cast<std::size_t>(i) * member.elementSize;
                        same = Equal(member, a + at, b + at, depth + 1);
                    }
                    return same;
                },
                depth);
            return known && same;
        }
        default:
            return std::memcmp(a, b, size) == 0;
    }
}

} // namespace URK::Unreal
