#pragma once

// FField and FProperty layout calibration.
//
// From UE4.25 properties stopped being UObjects: they live in a separate FField
// chain hanging off UStruct::ChildProperties and carry no vtable-bearing class
// object, so none of the UObject machinery applies to them.
//
// The anchors are engine structs whose field order and sizes are fixed by their
// C++ declaration: FColor is declared B, G, R, A as bytes, FGuid as four int32s.
// That pins names, offsets, sizes and flags all at once.

#include "unreal_struct_offsets.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace URK::Unreal {

// Cast flags the engine assigns to property classes.
inline constexpr std::uint64_t kCastFlagByteProperty = 0x40;
inline constexpr std::uint64_t kCastFlagIntProperty = 0x80;
inline constexpr std::uint64_t kCastFlagProperty = 0x8000;
inline constexpr std::uint64_t kCastFlagNumericProperty = 0x1000000;

// Property flags the engine assigns to a plain edit-exposed POD member.
inline constexpr std::uint64_t kPropertyFlagEdit = 0x1;
inline constexpr std::uint64_t kPropertyFlagBlueprintVisible = 0x4;
inline constexpr std::uint64_t kPropertyFlagParm = 0x80;
inline constexpr std::uint64_t kPropertyFlagOutParm = 0x100;
inline constexpr std::uint64_t kPropertyFlagZeroConstructor = 0x200;
inline constexpr std::uint64_t kPropertyFlagReturnParm = 0x400;
inline constexpr std::uint64_t kPropertyFlagSaveGame = 0x1000000;
inline constexpr std::uint64_t kPropertyFlagIsPlainOldData = 0x40000000;
inline constexpr std::uint64_t kPropertyFlagNoDestructor = 0x1000000000;
inline constexpr std::uint64_t kPropertyFlagHasGetValueTypeHash = 0x8000000000000;
inline constexpr std::uint64_t kPropertyFlagNativeAccessSpecifierPublic = 0x10000000000000;

struct FieldOffsets {
    std::int32_t childProperties = kOffsetNotFound;

    std::int32_t fieldClass = kOffsetNotFound;
    std::int32_t fieldNext = kOffsetNotFound;
    std::int32_t fieldName = kOffsetNotFound;

    std::int32_t fieldClassCastFlags = kOffsetNotFound;

    std::int32_t arrayDim = kOffsetNotFound;
    std::int32_t elementSize = kOffsetNotFound;
    std::int32_t propertyFlags = kOffsetNotFound;
    std::int32_t offsetInternal = kOffsetNotFound;

    bool Resolved() const {
        return childProperties != kOffsetNotFound && fieldClass != kOffsetNotFound && fieldNext != kOffsetNotFound &&
               fieldName != kOffsetNotFound && fieldClassCastFlags != kOffsetNotFound && arrayDim != kOffsetNotFound &&
               elementSize != kOffsetNotFound && propertyFlags != kOffsetNotFound && offsetInternal != kOffsetNotFound;
    }
};

// Walks the property chain once its offsets are known. Also the shape the rest
// of the toolkit will read members through.
class PropertyChain {
  public:
    PropertyChain(const MemoryReader &reader, const NameTable &names, const StructOffsets &structs,
                  const FieldOffsets &fields)
        : reader_(&reader), names_(&names), structs_(structs), fields_(fields) {}

    const MemoryReader &Reader() const { return *reader_; }

    Address First(Address structObject) const;
    Address Next(Address field) const;
    Address ClassOf(Address field) const;
    std::optional<std::string> NameOf(Address field) const;
    Address FindMember(Address structObject, std::string_view name) const;

    // The same, continued up the Super chain: a member is usually declared by
    // a class further up than the one an instance reports.
    Address FindMemberDeep(Address structObject, std::string_view name) const;

  private:
    const MemoryReader *reader_;
    const NameTable *names_;
    StructOffsets structs_;
    FieldOffsets fields_;
};

std::int32_t FindChildPropertiesOffset(const ObjectFinder &finder, const StructOffsets &structs);
std::int32_t FindFieldClassOffset(const ObjectFinder &finder, const StructOffsets &structs,
                                  const FieldOffsets &fields);
std::int32_t FindFieldNextOffset(const ObjectFinder &finder, const StructOffsets &structs, const FieldOffsets &fields);
std::int32_t FindFieldNameOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                 const FieldOffsets &fields);
std::int32_t FindFieldClassCastFlagsOffset(const ObjectFinder &finder, const NameTable &names,
                                           const StructOffsets &structs, const FieldOffsets &fields);
std::int32_t FindElementSizeOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                   const FieldOffsets &fields);
std::int32_t FindArrayDimOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                const FieldOffsets &fields);
std::int32_t FindOffsetInternalOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                      const FieldOffsets &fields);
std::int32_t FindPropertyFlagsOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                     const FieldOffsets &fields);

// Runs the steps in dependency order. Returns everything at kOffsetNotFound
// when the build predates the FField split, where properties are objects.
FieldOffsets FindFieldOffsets(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs);

} // namespace URK::Unreal
