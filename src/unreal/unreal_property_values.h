#pragma once

// Reading and writing members. The per-kind tail offset must satisfy several
// kinds at once; an array's Inner sits past it in UE5, so it is measured apart.

#include "unreal_property_offsets.h"

#include <cstdint>
#include <optional>
#include <string>

namespace URK::Unreal {

// Cast flags for the remaining property kinds. Engine constants, used here to
// classify a property rather than to anchor an offset.
inline constexpr std::uint64_t kCastFlagInt8Property = 0x2;
inline constexpr std::uint64_t kCastFlagEnum = 0x4;
inline constexpr std::uint64_t kCastFlagScriptStruct = 0x10;
inline constexpr std::uint64_t kCastFlagFloatProperty = 0x100;
inline constexpr std::uint64_t kCastFlagUInt64Property = 0x200;
inline constexpr std::uint64_t kCastFlagClassProperty = 0x400;
inline constexpr std::uint64_t kCastFlagUInt32Property = 0x800;
inline constexpr std::uint64_t kCastFlagInterfaceProperty = 0x1000;
inline constexpr std::uint64_t kCastFlagNameProperty = 0x2000;
inline constexpr std::uint64_t kCastFlagStrProperty = 0x4000;
inline constexpr std::uint64_t kCastFlagObjectProperty = 0x10000;
inline constexpr std::uint64_t kCastFlagBoolProperty = 0x20000;
inline constexpr std::uint64_t kCastFlagUInt16Property = 0x40000;
inline constexpr std::uint64_t kCastFlagStructProperty = 0x100000;
inline constexpr std::uint64_t kCastFlagArrayProperty = 0x200000;
inline constexpr std::uint64_t kCastFlagInt64Property = 0x400000;
inline constexpr std::uint64_t kCastFlagDelegateProperty = 0x800000;
inline constexpr std::uint64_t kCastFlagMulticastDelegateProperty = 0x2000000;
inline constexpr std::uint64_t kCastFlagObjectPropertyBase = 0x4000000;
inline constexpr std::uint64_t kCastFlagWeakObjectProperty = 0x8000000;
inline constexpr std::uint64_t kCastFlagLazyObjectProperty = 0x10000000;
inline constexpr std::uint64_t kCastFlagSoftObjectProperty = 0x20000000;
inline constexpr std::uint64_t kCastFlagTextProperty = 0x40000000;
inline constexpr std::uint64_t kCastFlagInt16Property = 0x80000000;
inline constexpr std::uint64_t kCastFlagDoubleProperty = 0x100000000;
inline constexpr std::uint64_t kCastFlagSoftClassProperty = 0x200000000;
inline constexpr std::uint64_t kCastFlagMapProperty = 0x400000000000;
inline constexpr std::uint64_t kCastFlagSetProperty = 0x800000000000;
inline constexpr std::uint64_t kCastFlagEnumProperty = 0x1000000000000;
inline constexpr std::uint64_t kCastFlagMulticastInlineDelegateProperty = 0x4000000000000;
inline constexpr std::uint64_t kCastFlagMulticastSparseDelegateProperty = 0x8000000000000;
inline constexpr std::uint64_t kCastFlagUtf8StrProperty = 0x1000000000000000;
inline constexpr std::uint64_t kCastFlagAnsiStrProperty = 0x2000000000000000;

enum class PropertyKind {
    Unknown,
    Bool,
    Byte,
    Int8,
    Int16,
    Int32,
    Int64,
    UInt16,
    UInt32,
    UInt64,
    Float,
    Double,
    Enum,
    Name,
    String,
    Text,
    Object,
    Class,
    WeakObject,
    SoftObject,
    Interface,
    Struct,
    Array,
    Set,
    Map,
    Delegate,
    // In URK_UnrealPropertyKind order: the ABI casts this enum.
    MulticastDelegate,
    SparseDelegate,
    LazyObject,
    Utf8String,
    AnsiString,
};

const char *PropertyKindName(PropertyKind kind);

// Which kind the cast flags describe. Order matters: a property carries every
// flag its bases carry, so the most derived match wins.
PropertyKind ClassifyProperty(std::uint64_t castFlags);

// Where a property's kind-specific members begin. Most kinds share the tail;
// an array's element property is measured separately.
struct PropertyTailOffsets {
    std::int32_t tail = kOffsetNotFound;
    std::int32_t arrayInner = kOffsetNotFound;
    // FSetProperty::ElementProp, FMapProperty::KeyProp/ValueProp,
    // FEnumProperty::Enum. Each measured on live properties of its kind.
    std::int32_t setElement = kOffsetNotFound;
    std::int32_t mapKey = kOffsetNotFound;
    std::int32_t mapValue = kOffsetNotFound;
    std::int32_t enumPropertyEnum = kOffsetNotFound;
    // Why the tail did not resolve: the closest offset and what refused it.
    std::string failure;

    bool Resolved() const { return tail != kOffsetNotFound; }

    // FBoolProperty: FieldSize, ByteOffset, ByteMask, FieldMask, one byte each.
    std::int32_t boolFieldSize() const { return tail; }
    std::int32_t boolByteOffset() const { return tail + 1; }
    std::int32_t boolByteMask() const { return tail + 2; }
    std::int32_t boolFieldMask() const { return tail + 3; }

    // The first pointer of the kinds that share the tail: PropertyClass,
    // Struct, UnderlyingProp.
    std::int32_t firstPointer() const { return tail; }
};

// Lowest offset that satisfies every kind found, plus the array element slot.
// Needs two kinds to agree, so a one-kind graph fails rather than guesses.
PropertyTailOffsets FindPropertyTailOffsets(const ObjectFinder &finder, const StructOffsets &structs,
                                            const FieldOffsets &fields);

struct BoolLayout {
    std::uint8_t fieldSize = 0;
    std::uint8_t byteOffset = 0;
    std::uint8_t byteMask = 0;
    std::uint8_t fieldMask = 0;

    // A bool of its own byte rather than a bit of a bitfield.
    bool Native() const { return fieldMask == 0xFF; }
};

// Everything needed to read or write one member, gathered once.
struct PropertyInfo {
    Address field = kNullAddress;
    PropertyKind kind = PropertyKind::Unknown;
    std::uint64_t castFlags = 0;
    std::uint64_t propertyFlags = 0;

    std::int32_t offset = kOffsetNotFound;
    std::int32_t elementSize = 0;
    std::int32_t arrayDim = 1;

    BoolLayout boolLayout{};

    // Object/Class/Weak/Soft/Lazy: the UClass. Struct: UScriptStruct. Array
    // and Set: the element property. Map: the key property. Enum: the
    // underlying numeric property.
    Address inner = kNullAddress;
    // Map: the value property.
    Address valueInner = kNullAddress;
    // The UEnum of an enum or byte, the signature UFunction of a delegate, and
    // inner for the kinds where that is an object.
    Address typeObject = kNullAddress;

    bool Resolved() const { return field != kNullAddress && offset != kOffsetNotFound; }
};

// A TArray's three fields, which FString shares.
struct ArrayView {
    Address data = kNullAddress;
    std::int32_t num = 0;
    std::int32_t max = 0;
    Address inner = kNullAddress;
    std::int32_t elementSize = 0;

    Address ElementAt(std::int32_t index) const {
        return (index < 0 || index >= num) ? kNullAddress
                                           : data + static_cast<Address>(index) * static_cast<Address>(elementSize);
    }
};

// Writing takes a MemoryWriter per call, so a read-only caller never holds one.
class PropertyValues {
  public:
    PropertyValues(const MemoryReader &reader, const NameTable &names, const StructOffsets &structs,
                   const FieldOffsets &fields, const PropertyTailOffsets &tail)
        : reader_(&reader), names_(&names), structs_(structs), fields_(fields), tail_(tail) {}

    std::optional<PropertyInfo> Describe(Address field) const;
    const PropertyTailOffsets &Tail() const { return tail_; }

    // What a value of this property must be aligned to in memory: the kind's
    // C++ alignment, or a struct's measured MinAlignment.
    std::int32_t AlignmentOf(const PropertyInfo &info) const;

    // Where a member's value sits inside an instance. Static C arrays are
    // addressed by index; anything else only has index zero.
    Address ValueAddress(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    // Whatever the kind holds, widened. Byte, enum and every integer kind.
    std::optional<std::int64_t> ReadInteger(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    std::optional<double> ReadFloating(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    std::optional<bool> ReadBool(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    Address ReadObject(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    std::optional<std::string> ReadName(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    // FString is a TArray of characters; the text is copied out as UTF-8,
    // never touched.
    std::optional<std::string> ReadString(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    // The characters of any string kind at a value address, as UTF-8.
    std::optional<std::string> ReadStringAt(Address value, PropertyKind kind) const;

    std::optional<ArrayView> ReadArray(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    // In-place only. FString/TArray/TMap/FText own engine allocations, so
    // changing one means calling into the game, not writing memory.
    bool WriteInteger(MemoryWriter &writer, Address instance, const PropertyInfo &info, std::int64_t value,
                      std::int32_t index = 0) const;
    bool WriteFloating(MemoryWriter &writer, Address instance, const PropertyInfo &info, double value,
                       std::int32_t index = 0) const;
    bool WriteBool(MemoryWriter &writer, Address instance, const PropertyInfo &info, bool value,
                   std::int32_t index = 0) const;
    bool WriteObject(MemoryWriter &writer, Address instance, const PropertyInfo &info, Address value,
                     std::int32_t index = 0) const;

  private:
    const MemoryReader *reader_;
    const NameTable *names_;
    StructOffsets structs_;
    FieldOffsets fields_;
    PropertyTailOffsets tail_;
};

} // namespace URK::Unreal
