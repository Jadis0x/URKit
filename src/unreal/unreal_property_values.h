#pragma once

// Reading and writing a member's value once the field layout is calibrated.
//
// What a property means lives in two places. Its FFieldClass cast flags say
// which kind it is, and everything past FProperty belongs to that kind: the
// mask of a bool, the class an object reference points at, the struct a struct
// member holds. Those begin where FProperty ends, and that offset is measured
// the way the rungs below measure everything else - by demanding that one
// offset satisfy what several different kinds each know about their own first
// member. Padding and the chain pointers FProperty ends with cannot satisfy all
// of them at once.
//
// An array's element property is not among them: in a shipped UE5 build it sits
// further in than the tail, so it is measured on its own. Assuming it shared
// the tail is what a real game disproved.

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
};

const char *PropertyKindName(PropertyKind kind);

// Which kind the cast flags describe. Order matters: a property carries every
// flag its bases carry, so the most derived match wins.
PropertyKind ClassifyProperty(std::uint64_t castFlags);

// Where a property's kind-specific members begin, and what each kind keeps
// there.
//
// Most kinds put their first member at the end of FProperty and are measured
// together. An array does not: shipped UE5 builds keep something ahead of the
// element property, so where that element property sits is measured separately
// rather than assumed to be the same place.
struct PropertyTailOffsets {
    std::int32_t tail = kOffsetNotFound;
    std::int32_t arrayInner = kOffsetNotFound;

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

// Walks the graph for properties of several kinds and returns the lowest offset
// every one of them is satisfied at, then looks for where an array keeps its
// element property. Needs at least two kinds to agree on the tail, so a graph
// holding only one kind of property fails rather than guessing.
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

    // Object and Class: the UClass a reference must be. Struct: the
    // UScriptStruct. Array: the element property. Enum: the numeric property
    // underneath it. A set or a map keeps more than one and is left alone.
    Address inner = kNullAddress;

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

// Reads and writes members of one running game. Reading needs the layout only;
// writing needs somewhere to write, which is asked for per call so a caller
// that only reads cannot accidentally hold the means to change the game.
class PropertyValues {
  public:
    PropertyValues(const MemoryReader &reader, const NameTable &names, const StructOffsets &structs,
                   const FieldOffsets &fields, const PropertyTailOffsets &tail)
        : reader_(&reader), names_(&names), structs_(structs), fields_(fields), tail_(tail) {}

    std::optional<PropertyInfo> Describe(Address field) const;

    // Where a member's value sits inside an instance. Static C arrays are
    // addressed by index; anything else only has index zero.
    Address ValueAddress(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    // Whatever the kind holds, widened. Byte, enum and every integer kind.
    std::optional<std::int64_t> ReadInteger(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    std::optional<double> ReadFloating(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    std::optional<bool> ReadBool(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    Address ReadObject(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;
    std::optional<std::string> ReadName(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    // FString is a TArray of characters; the text is copied out of whatever it
    // points at, which belongs to the game and is not touched.
    std::optional<std::string> ReadString(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    std::optional<ArrayView> ReadArray(Address instance, const PropertyInfo &info, std::int32_t index = 0) const;

    // Writes are for values that fit where they already are. FString, TArray,
    // TMap and FText own allocations the engine's allocator made, so changing
    // one means calling into the game rather than writing memory.
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
