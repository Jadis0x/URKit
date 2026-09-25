#include "unreal_property_values.h"
#include "unreal_text.h"

#include <array>
#include <cstdio>
#include <vector>

namespace URK::Unreal {
namespace {

// Max distance of the tail past Offset_Internal.
constexpr std::int32_t kMinTailGap = 0x08;
constexpr std::int32_t kMaxTailGap = 0x60;

// Samples per kind: enough to rule out chance, few enough to stop early.
constexpr std::size_t kSamplesPerKind = 4;
constexpr std::int32_t kMaxObjectsWalked = 0x4000;
constexpr std::int32_t kMaxChainLength = 0x200;

// FBoolProperty::SetBoolSize invariants (4.25-5.8).
bool PlausibleBoolTail(const MemoryReader &reader, const FieldOffsets &fields, Address field, std::int32_t offset) {
    const std::optional<std::uint8_t> fieldSize = reader.ReadAs<std::uint8_t>(field + offset);
    const std::optional<std::uint8_t> byteOffset = reader.ReadAs<std::uint8_t>(field + offset + 1);
    const std::optional<std::uint8_t> byteMask = reader.ReadAs<std::uint8_t>(field + offset + 2);
    const std::optional<std::uint8_t> fieldMask = reader.ReadAs<std::uint8_t>(field + offset + 3);
    if (!fieldSize || !byteOffset || !byteMask || !fieldMask)
        return false;

    const std::optional<std::int32_t> elementSize = reader.ReadAs<std::int32_t>(field + fields.elementSize);
    if (!elementSize || *fieldSize != *elementSize ||
        (*fieldSize != 1 && *fieldSize != 2 && *fieldSize != 4 && *fieldSize != 8) || *byteOffset >= *fieldSize)
        return false;
    if (*fieldMask == 0xFF)
        return *byteOffset == 0 && *byteMask == 1;
    return *byteMask != 0 && *byteMask == *fieldMask && (*byteMask & (*byteMask - 1)) == 0;
}

// The tail must reach an object of this kind; FFields aren't in the object array.
bool PointsToObjectWithFlags(const ObjectFinder &finder, const StructOffsets &structs, Address field,
                             std::int32_t offset, std::uint64_t required) {
    const std::optional<Address> target = finder.Reader().ReadPointer(field + offset);
    if (!target || !IsLiveObject(finder, *target))
        return false;
    return ObjectIs(finder, structs, *target, required);
}

// The pointer at the tail leads to another FField that is itself a property.
bool PointsToProperty(const MemoryReader &reader, const FieldOffsets &fields, Address field, std::int32_t offset) {
    const std::optional<Address> target = reader.ReadPointer(field + offset);
    if (!target || *target == kNullAddress)
        return false;
    const std::optional<Address> fieldClass = reader.ReadPointer(*target + fields.fieldClass);
    if (!fieldClass || *fieldClass == kNullAddress)
        return false;
    const std::optional<std::uint64_t> castFlags =
        reader.ReadAs<std::uint64_t>(*fieldClass + fields.fieldClassCastFlags);
    return castFlags && (*castFlags & kCastFlagProperty) != 0;
}

// Array element property: first offset from the tail all arrays agree on.
std::int32_t FindArrayInnerOffset(const MemoryReader &reader, const FieldOffsets &fields,
                                  const std::vector<Address> &arrays, std::int32_t tail) {
    if (arrays.empty())
        return kOffsetNotFound;

    constexpr std::int32_t kMaxInnerGap = 0x20;
    for (std::int32_t offset = tail; offset <= tail + kMaxInnerGap;
         offset += static_cast<std::int32_t>(sizeof(Address))) {
        bool satisfied = true;
        for (const Address field : arrays)
            satisfied = satisfied && PointsToProperty(reader, fields, field, offset);
        if (satisfied)
            return offset;
    }
    return kOffsetNotFound;
}

// First offset from start where every sample passes.
template <typename Test>
std::int32_t FirstAgreeingOffset(const std::vector<Address> &samples, std::int32_t start, Test test) {
    if (samples.empty())
        return kOffsetNotFound;
    constexpr std::int32_t kMaxGap = 0x20;
    for (std::int32_t offset = start; offset <= start + kMaxGap; offset += static_cast<std::int32_t>(sizeof(Address))) {
        bool satisfied = true;
        for (const Address field : samples)
            satisfied = satisfied && test(field, offset);
        if (satisfied)
            return offset;
    }
    return kOffsetNotFound;
}

struct Samples {
    std::vector<Address> bools;
    std::vector<Address> objects;
    std::vector<Address> structs;
    std::vector<Address> arrays;
    std::vector<Address> sets;
    std::vector<Address> maps;
    std::vector<Address> enums;

    // Arrays don't share the tail; exclude them.
    std::size_t Kinds() const {
        return static_cast<std::size_t>(!bools.empty()) + static_cast<std::size_t>(!objects.empty()) +
               static_cast<std::size_t>(!structs.empty());
    }

    bool Full() const {
        return bools.size() >= kSamplesPerKind && objects.size() >= kSamplesPerKind &&
               structs.size() >= kSamplesPerKind && arrays.size() >= kSamplesPerKind &&
               sets.size() >= kSamplesPerKind && maps.size() >= kSamplesPerKind && enums.size() >= kSamplesPerKind;
    }
};

std::string Hex(std::uint64_t value) {
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%llX", static_cast<unsigned long long>(value));
    return buffer;
}

void Collect(std::vector<Address> &into, Address field) {
    if (into.size() < kSamplesPerKind)
        into.push_back(field);
}

// All properties, grouped by kind.
Samples CollectSamples(const ObjectFinder &finder, const StructOffsets &structs, const FieldOffsets &fields) {
    const MemoryReader &reader = finder.Reader();
    const ObjectArray &objects = finder.Objects();
    const PropertyChain chain(reader, finder.Names(), structs, fields);

    Samples samples;
    const std::int32_t total = objects.Num();
    const std::int32_t walked = total < kMaxObjectsWalked ? total : kMaxObjectsWalked;

    for (std::int32_t index = 0; index < walked; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;
        if (!ObjectIs(finder, structs, object, kCastFlagStruct))
            continue;

        Address field = chain.First(object);
        for (std::int32_t step = 0; field != kNullAddress && step < kMaxChainLength; ++step) {
            const Address fieldClass = chain.ClassOf(field);
            const std::optional<std::uint64_t> fieldFlags =
                reader.ReadAs<std::uint64_t>(fieldClass + fields.fieldClassCastFlags);
            if (fieldFlags) {
                if ((*fieldFlags & kCastFlagBoolProperty) != 0)
                    Collect(samples.bools, field);
                else if ((*fieldFlags & kCastFlagArrayProperty) != 0)
                    Collect(samples.arrays, field);
                else if ((*fieldFlags & kCastFlagSetProperty) != 0)
                    Collect(samples.sets, field);
                else if ((*fieldFlags & kCastFlagMapProperty) != 0)
                    Collect(samples.maps, field);
                else if ((*fieldFlags & kCastFlagEnumProperty) != 0)
                    Collect(samples.enums, field);
                else if ((*fieldFlags & kCastFlagStructProperty) != 0)
                    Collect(samples.structs, field);
                else if ((*fieldFlags & kCastFlagObjectProperty) != 0)
                    Collect(samples.objects, field);
            }
            if (samples.Full())
                return samples;
            field = chain.Next(field);
        }
    }

    return samples;
}

template <typename T> std::optional<std::int64_t> ReadWidened(const MemoryReader &reader, Address address) {
    const std::optional<T> value = reader.ReadAs<T>(address);
    if (!value)
        return std::nullopt;
    return static_cast<std::int64_t>(*value);
}

} // namespace

const char *PropertyKindName(PropertyKind kind) {
    switch (kind) {
    case PropertyKind::Bool:
        return "bool";
    case PropertyKind::Byte:
        return "byte";
    case PropertyKind::Int8:
        return "int8";
    case PropertyKind::Int16:
        return "int16";
    case PropertyKind::Int32:
        return "int32";
    case PropertyKind::Int64:
        return "int64";
    case PropertyKind::UInt16:
        return "uint16";
    case PropertyKind::UInt32:
        return "uint32";
    case PropertyKind::UInt64:
        return "uint64";
    case PropertyKind::Float:
        return "float";
    case PropertyKind::Double:
        return "double";
    case PropertyKind::Enum:
        return "enum";
    case PropertyKind::Name:
        return "name";
    case PropertyKind::String:
        return "string";
    case PropertyKind::Text:
        return "text";
    case PropertyKind::Object:
        return "object";
    case PropertyKind::Class:
        return "class";
    case PropertyKind::WeakObject:
        return "weak object";
    case PropertyKind::SoftObject:
        return "soft object";
    case PropertyKind::Interface:
        return "interface";
    case PropertyKind::Struct:
        return "struct";
    case PropertyKind::Array:
        return "array";
    case PropertyKind::Set:
        return "set";
    case PropertyKind::Map:
        return "map";
    case PropertyKind::Delegate:
        return "delegate";
    case PropertyKind::MulticastDelegate:
        return "multicast delegate";
    case PropertyKind::SparseDelegate:
        return "sparse delegate";
    case PropertyKind::LazyObject:
        return "lazy object";
    case PropertyKind::Utf8String:
        return "utf8 string";
    case PropertyKind::AnsiString:
        return "ansi string";
    case PropertyKind::Unknown:
        break;
    }
    return "unknown";
}

PropertyKind ClassifyProperty(std::uint64_t castFlags) {
    // Most derived first: cast flags include every base's flags.
    struct Mapping {
        std::uint64_t flag;
        PropertyKind kind;
    };
    static constexpr std::array kMappings = {
        Mapping{kCastFlagBoolProperty, PropertyKind::Bool},
        Mapping{kCastFlagEnumProperty, PropertyKind::Enum},
        Mapping{kCastFlagClassProperty, PropertyKind::Class},
        Mapping{kCastFlagSoftClassProperty, PropertyKind::SoftObject},
        Mapping{kCastFlagSoftObjectProperty, PropertyKind::SoftObject},
        Mapping{kCastFlagWeakObjectProperty, PropertyKind::WeakObject},
        Mapping{kCastFlagLazyObjectProperty, PropertyKind::LazyObject},
        Mapping{kCastFlagObjectProperty, PropertyKind::Object},
        Mapping{kCastFlagInterfaceProperty, PropertyKind::Interface},
        Mapping{kCastFlagStructProperty, PropertyKind::Struct},
        Mapping{kCastFlagArrayProperty, PropertyKind::Array},
        Mapping{kCastFlagSetProperty, PropertyKind::Set},
        Mapping{kCastFlagMapProperty, PropertyKind::Map},
        Mapping{kCastFlagNameProperty, PropertyKind::Name},
        Mapping{kCastFlagStrProperty, PropertyKind::String},
        Mapping{kCastFlagUtf8StrProperty, PropertyKind::Utf8String},
        Mapping{kCastFlagAnsiStrProperty, PropertyKind::AnsiString},
        Mapping{kCastFlagTextProperty, PropertyKind::Text},
        Mapping{kCastFlagDelegateProperty, PropertyKind::Delegate},
        Mapping{kCastFlagMulticastSparseDelegateProperty, PropertyKind::SparseDelegate},
        Mapping{kCastFlagMulticastDelegateProperty, PropertyKind::MulticastDelegate},
        Mapping{kCastFlagDoubleProperty, PropertyKind::Double},
        Mapping{kCastFlagFloatProperty, PropertyKind::Float},
        Mapping{kCastFlagInt64Property, PropertyKind::Int64},
        Mapping{kCastFlagUInt64Property, PropertyKind::UInt64},
        Mapping{kCastFlagIntProperty, PropertyKind::Int32},
        Mapping{kCastFlagUInt32Property, PropertyKind::UInt32},
        Mapping{kCastFlagInt16Property, PropertyKind::Int16},
        Mapping{kCastFlagUInt16Property, PropertyKind::UInt16},
        Mapping{kCastFlagInt8Property, PropertyKind::Int8},
        Mapping{kCastFlagByteProperty, PropertyKind::Byte},
    };

    for (const Mapping &mapping : kMappings) {
        if ((castFlags & mapping.flag) != 0)
            return mapping.kind;
    }
    return PropertyKind::Unknown;
}

PropertyTailOffsets FindPropertyTailOffsets(const ObjectFinder &finder, const StructOffsets &structs,
                                            const FieldOffsets &fields) {
    PropertyTailOffsets resolved;
    if (!fields.Resolved())
        return resolved;

    const Samples samples = CollectSamples(finder, structs, fields);
    // Needs two kinds to agree; one kind alone matches padding too easily.
    if (samples.Kinds() < 2) {
        resolved.failure = "fewer than two property kinds to agree (bools " + std::to_string(samples.bools.size()) +
                           ", objects " + std::to_string(samples.objects.size()) + ", structs " +
                           std::to_string(samples.structs.size()) + ")";
        return resolved;
    }

    const MemoryReader &reader = finder.Reader();
    const std::int32_t start = ((fields.offsetInternal + kMinTailGap) + 0x7) & ~0x7;

    // The offset refused by the fewest samples, for the failure message.
    std::size_t fewest = ~std::size_t{0};
    for (std::int32_t offset = start; offset <= fields.offsetInternal + kMaxTailGap;
         offset += static_cast<std::int32_t>(sizeof(Address))) {
        std::size_t refused = 0;
        std::string first;
        const auto check = [&](const std::vector<Address> &kind, const char *name, auto test) {
            for (const Address field : kind) {
                if (test(field))
                    continue;
                if (refused++ == 0)
                    first = std::string(name) + " property 0x" + Hex(field);
            }
        };
        check(samples.bools, "bool", [&](Address field) { return PlausibleBoolTail(reader, fields, field, offset); });
        check(samples.objects, "object", [&](Address field) {
            return PointsToObjectWithFlags(finder, structs, field, offset, kCastFlagClass);
        });
        check(samples.structs, "struct", [&](Address field) {
            return PointsToObjectWithFlags(finder, structs, field, offset, kCastFlagScriptStruct);
        });
        if (refused > 0) {
            if (refused < fewest) {
                fewest = refused;
                resolved.failure = "closest offset 0x" + Hex(static_cast<std::uint64_t>(offset)) + " was refused by " +
                                   std::to_string(refused) + " sample(s), first a " + first;
            }
            continue;
        }

        resolved.failure.clear();
        resolved.tail = offset;
        resolved.arrayInner = FindArrayInnerOffset(reader, fields, samples.arrays, offset);
        const auto property = [&](Address field, std::int32_t at) { return PointsToProperty(reader, fields, field, at); };
        resolved.setElement = FirstAgreeingOffset(samples.sets, offset, property);
        resolved.mapKey = FirstAgreeingOffset(samples.maps, offset, property);
        if (resolved.mapKey != kOffsetNotFound)
            resolved.mapValue = FirstAgreeingOffset(samples.maps, resolved.mapKey + 8, property);
        resolved.enumPropertyEnum = FirstAgreeingOffset(samples.enums, offset, [&](Address field, std::int32_t at) {
            return PointsToObjectWithFlags(finder, structs, field, at, kCastFlagEnum);
        });
        return resolved;
    }

    return resolved;
}

std::optional<PropertyInfo> PropertyValues::Describe(Address field) const {
    if (field == kNullAddress || !fields_.Resolved())
        return std::nullopt;

    const std::optional<Address> fieldClass = reader_->ReadPointer(field + fields_.fieldClass);
    if (!fieldClass || *fieldClass == kNullAddress)
        return std::nullopt;

    const std::optional<std::uint64_t> castFlags =
        reader_->ReadAs<std::uint64_t>(*fieldClass + fields_.fieldClassCastFlags);
    const std::optional<std::int32_t> offset = reader_->ReadInt32(field + fields_.offsetInternal);
    const std::optional<std::int32_t> elementSize = reader_->ReadInt32(field + fields_.elementSize);
    const std::optional<std::int32_t> arrayDim = reader_->ReadInt32(field + fields_.arrayDim);
    const std::optional<std::uint64_t> propertyFlags = reader_->ReadAs<std::uint64_t>(field + fields_.propertyFlags);
    if (!castFlags || !offset || !elementSize || !arrayDim)
        return std::nullopt;

    PropertyInfo info;
    info.field = field;
    info.castFlags = *castFlags;
    info.kind = ClassifyProperty(*castFlags);
    info.offset = *offset;
    info.elementSize = *elementSize;
    info.arrayDim = *arrayDim > 0 ? *arrayDim : 1;
    info.propertyFlags = propertyFlags ? *propertyFlags : 0;

    if (!tail_.Resolved())
        return info;

    if (info.kind == PropertyKind::Bool) {
        const std::optional<std::uint8_t> fieldSize = reader_->ReadAs<std::uint8_t>(field + tail_.boolFieldSize());
        const std::optional<std::uint8_t> byteOffset = reader_->ReadAs<std::uint8_t>(field + tail_.boolByteOffset());
        const std::optional<std::uint8_t> byteMask = reader_->ReadAs<std::uint8_t>(field + tail_.boolByteMask());
        const std::optional<std::uint8_t> fieldMask = reader_->ReadAs<std::uint8_t>(field + tail_.boolFieldMask());
        if (!fieldSize || !byteOffset || !byteMask || !fieldMask)
            return std::nullopt;
        info.boolLayout = BoolLayout{*fieldSize, *byteOffset, *byteMask, *fieldMask};
        return info;
    }

    const auto pointerAt = [&](std::int32_t offset) {
        if (offset == kOffsetNotFound)
            return kNullAddress;
        return reader_->ReadPointer(field + offset).value_or(kNullAddress);
    };
    switch (info.kind) {
    case PropertyKind::Object:
    case PropertyKind::Class:
    case PropertyKind::WeakObject:
    case PropertyKind::SoftObject:
    case PropertyKind::LazyObject:
    case PropertyKind::Interface:
    case PropertyKind::Struct:
        info.inner = pointerAt(tail_.firstPointer());
        info.typeObject = info.inner;
        break;
    case PropertyKind::Enum:
        info.inner = pointerAt(tail_.firstPointer());
        info.typeObject = pointerAt(tail_.enumPropertyEnum);
        break;
    case PropertyKind::Byte:
        // Where FEnumProperty keeps UnderlyingProp a byte keeps its UEnum; null for a plain byte.
        info.typeObject = pointerAt(tail_.firstPointer());
        break;
    case PropertyKind::Delegate:
    case PropertyKind::MulticastDelegate:
    case PropertyKind::SparseDelegate:
        info.typeObject = pointerAt(tail_.firstPointer());
        break;
    case PropertyKind::Array:
        info.inner = pointerAt(tail_.arrayInner);
        break;
    case PropertyKind::Set:
        info.inner = pointerAt(tail_.setElement);
        break;
    case PropertyKind::Map:
        info.inner = pointerAt(tail_.mapKey);
        info.valueInner = pointerAt(tail_.mapValue);
        break;
    default:
        break;
    }

    return info;
}

Address PropertyValues::ValueAddress(Address instance, const PropertyInfo &info, std::int32_t index) const {
    if (instance == kNullAddress || !info.Resolved())
        return kNullAddress;
    if (index < 0 || index >= info.arrayDim)
        return kNullAddress;
    return instance + static_cast<Address>(info.offset) +
           static_cast<Address>(index) * static_cast<Address>(info.elementSize);
}

std::int32_t PropertyValues::AlignmentOf(const PropertyInfo &info) const {
    switch (info.kind) {
    case PropertyKind::Bool:
    case PropertyKind::Byte:
    case PropertyKind::Int8:
    case PropertyKind::SparseDelegate:
        return 1;
    case PropertyKind::Int16:
    case PropertyKind::UInt16:
        return 2;
    case PropertyKind::Int32:
    case PropertyKind::UInt32:
    case PropertyKind::Float:
    case PropertyKind::Name:
    case PropertyKind::WeakObject:
    case PropertyKind::LazyObject:
    case PropertyKind::Delegate:
        return 4;
    case PropertyKind::Enum:
        return info.elementSize > 0 && info.elementSize <= 8 ? info.elementSize : 0;
    case PropertyKind::Struct: {
        if (structs_.minAlignment == kOffsetNotFound || info.inner == kNullAddress)
            return 0;
        // int16 since UE5.x (int32 before; the low half is the same value).
        const std::int32_t alignment = reader_->ReadAs<std::int16_t>(info.inner + structs_.minAlignment).value_or(0);
        return alignment > 0 && alignment <= 256 && (alignment & (alignment - 1)) == 0 ? alignment : 0;
    }
    case PropertyKind::Unknown:
        return 0;
    default:
        return 8;
    }
}

std::optional<std::int64_t> PropertyValues::ReadInteger(Address instance, const PropertyInfo &info,
                                                        std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress)
        return std::nullopt;

    switch (info.kind) {
    case PropertyKind::Int8:
        return ReadWidened<std::int8_t>(*reader_, value);
    case PropertyKind::Byte:
        return ReadWidened<std::uint8_t>(*reader_, value);
    case PropertyKind::Int16:
        return ReadWidened<std::int16_t>(*reader_, value);
    case PropertyKind::UInt16:
        return ReadWidened<std::uint16_t>(*reader_, value);
    case PropertyKind::Int32:
        return ReadWidened<std::int32_t>(*reader_, value);
    case PropertyKind::UInt32:
        return ReadWidened<std::uint32_t>(*reader_, value);
    case PropertyKind::Int64:
    case PropertyKind::UInt64:
        return ReadWidened<std::int64_t>(*reader_, value);
    case PropertyKind::Enum:
        // An enum's width is its underlying property's.
        switch (info.elementSize) {
        case 1:
            return ReadWidened<std::uint8_t>(*reader_, value);
        case 2:
            return ReadWidened<std::uint16_t>(*reader_, value);
        case 4:
            return ReadWidened<std::int32_t>(*reader_, value);
        case 8:
            return ReadWidened<std::int64_t>(*reader_, value);
        default:
            return std::nullopt;
        }
    default:
        return std::nullopt;
    }
}

std::optional<double> PropertyValues::ReadFloating(Address instance, const PropertyInfo &info,
                                                   std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress)
        return std::nullopt;

    if (info.kind == PropertyKind::Float) {
        const std::optional<float> single = reader_->ReadAs<float>(value);
        return single ? std::optional<double>(static_cast<double>(*single)) : std::nullopt;
    }
    if (info.kind == PropertyKind::Double)
        return reader_->ReadAs<double>(value);
    return std::nullopt;
}

std::optional<bool> PropertyValues::ReadBool(Address instance, const PropertyInfo &info, std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress || info.kind != PropertyKind::Bool || info.boolLayout.fieldMask == 0)
        return std::nullopt;

    const std::optional<std::uint8_t> byte =
        reader_->ReadAs<std::uint8_t>(value + static_cast<Address>(info.boolLayout.byteOffset));
    if (!byte)
        return std::nullopt;
    return (*byte & info.boolLayout.fieldMask) != 0;
}

Address PropertyValues::ReadObject(Address instance, const PropertyInfo &info, std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress)
        return kNullAddress;
    if (info.kind != PropertyKind::Object && info.kind != PropertyKind::Class)
        return kNullAddress;

    const std::optional<Address> object = reader_->ReadPointer(value);
    return object ? *object : kNullAddress;
}

std::optional<std::string> PropertyValues::ReadName(Address instance, const PropertyInfo &info,
                                                    std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress || info.kind != PropertyKind::Name)
        return std::nullopt;
    return names_->ReadFName(value);
}

std::optional<std::string> PropertyValues::ReadString(Address instance, const PropertyInfo &info,
                                                      std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress)
        return std::nullopt;
    return ReadStringAt(value, info.kind);
}

std::optional<std::string> PropertyValues::ReadStringAt(Address value, PropertyKind kind) const {
    if (kind != PropertyKind::String && kind != PropertyKind::Utf8String && kind != PropertyKind::AnsiString)
        return std::nullopt;
    const std::optional<Address> data = reader_->ReadPointer(value);
    const std::optional<std::int32_t> num = reader_->ReadInt32(value + sizeof(Address));
    const std::optional<std::int32_t> max = reader_->ReadInt32(value + sizeof(Address) + sizeof(std::int32_t));
    if (!data || !num || !max || *num < 0 || *num > *max)
        return std::nullopt;
    if (*data == kNullAddress || *num <= 0)
        return std::string();

    // The count includes the terminator the engine always stores.
    const std::size_t count = static_cast<std::size_t>(*num - 1);
    if (kind == PropertyKind::String) {
        std::u16string units(count, u'\0');
        if (count && !reader_->Read(*data, units.data(), count * sizeof(char16_t)))
            return std::nullopt;
        return Utf16ToUtf8(units);
    }
    std::string bytes(count, '\0');
    if (count && !reader_->Read(*data, bytes.data(), count))
        return std::nullopt;
    if (kind == PropertyKind::AnsiString) {
        // ANSICHAR text is Latin-1 to the engine.
        std::u16string widened(count, u'\0');
        for (std::size_t i = 0; i < count; ++i)
            widened[i] = static_cast<char16_t>(static_cast<unsigned char>(bytes[i]));
        return Utf16ToUtf8(widened);
    }
    return bytes;
}

std::optional<ArrayView> PropertyValues::ReadArray(Address instance, const PropertyInfo &info,
                                                   std::int32_t index) const {
    const Address value = ValueAddress(instance, info, index);
    if (value == kNullAddress || info.kind != PropertyKind::Array)
        return std::nullopt;

    const std::optional<Address> data = reader_->ReadPointer(value);
    const std::optional<std::int32_t> num = reader_->ReadInt32(value + sizeof(Address));
    const std::optional<std::int32_t> max = reader_->ReadInt32(value + sizeof(Address) + sizeof(std::int32_t));
    if (!data || !num || !max || *num < 0 || *num > *max)
        return std::nullopt;

    ArrayView view;
    view.data = *data;
    view.num = *num;
    view.max = *max;
    view.inner = info.inner;

    if (info.inner != kNullAddress) {
        if (const std::optional<std::int32_t> elementSize = reader_->ReadInt32(info.inner + fields_.elementSize))
            view.elementSize = *elementSize;
    }
    return view;
}

bool PropertyValues::WriteInteger(MemoryWriter &writer, Address instance, const PropertyInfo &info,
                                  std::int64_t value, std::int32_t index) const {
    const Address at = ValueAddress(instance, info, index);
    if (at == kNullAddress)
        return false;

    switch (info.kind) {
    case PropertyKind::Int8:
        return writer.WriteAs<std::int8_t>(at, static_cast<std::int8_t>(value));
    case PropertyKind::Byte:
        return writer.WriteAs<std::uint8_t>(at, static_cast<std::uint8_t>(value));
    case PropertyKind::Int16:
        return writer.WriteAs<std::int16_t>(at, static_cast<std::int16_t>(value));
    case PropertyKind::UInt16:
        return writer.WriteAs<std::uint16_t>(at, static_cast<std::uint16_t>(value));
    case PropertyKind::Int32:
        return writer.WriteAs<std::int32_t>(at, static_cast<std::int32_t>(value));
    case PropertyKind::UInt32:
        return writer.WriteAs<std::uint32_t>(at, static_cast<std::uint32_t>(value));
    case PropertyKind::Int64:
    case PropertyKind::UInt64:
        return writer.WriteAs<std::int64_t>(at, value);
    case PropertyKind::Enum:
        switch (info.elementSize) {
        case 1:
            return writer.WriteAs<std::uint8_t>(at, static_cast<std::uint8_t>(value));
        case 2:
            return writer.WriteAs<std::uint16_t>(at, static_cast<std::uint16_t>(value));
        case 4:
            return writer.WriteAs<std::int32_t>(at, static_cast<std::int32_t>(value));
        case 8:
            return writer.WriteAs<std::int64_t>(at, value);
        default:
            return false;
        }
    default:
        return false;
    }
}

bool PropertyValues::WriteFloating(MemoryWriter &writer, Address instance, const PropertyInfo &info, double value,
                                   std::int32_t index) const {
    const Address at = ValueAddress(instance, info, index);
    if (at == kNullAddress)
        return false;

    if (info.kind == PropertyKind::Float)
        return writer.WriteAs<float>(at, static_cast<float>(value));
    if (info.kind == PropertyKind::Double)
        return writer.WriteAs<double>(at, value);
    return false;
}

bool PropertyValues::WriteBool(MemoryWriter &writer, Address instance, const PropertyInfo &info, bool value,
                               std::int32_t index) const {
    const Address at = ValueAddress(instance, info, index);
    if (at == kNullAddress || info.kind != PropertyKind::Bool || info.boolLayout.fieldMask == 0)
        return false;

    // Bitfield: read-modify-write only this property's bits.
    const Address byteAddress = at + static_cast<Address>(info.boolLayout.byteOffset);
    const std::optional<std::uint8_t> current = reader_->ReadAs<std::uint8_t>(byteAddress);
    if (!current)
        return false;

    // FBoolProperty::SetPropertyValue: clear FieldMask, set ByteMask (1 for native bool).
    const auto cleared = static_cast<std::uint8_t>(*current & ~info.boolLayout.fieldMask);
    const auto updated = static_cast<std::uint8_t>(value ? (cleared | info.boolLayout.byteMask) : cleared);
    return writer.WriteAs<std::uint8_t>(byteAddress, updated);
}

bool PropertyValues::WriteObject(MemoryWriter &writer, Address instance, const PropertyInfo &info, Address value,
                                 std::int32_t index) const {
    const Address at = ValueAddress(instance, info, index);
    if (at == kNullAddress)
        return false;
    if (info.kind != PropertyKind::Object && info.kind != PropertyKind::Class)
        return false;
    return writer.WriteAs<Address>(at, value);
}

} // namespace URK::Unreal
