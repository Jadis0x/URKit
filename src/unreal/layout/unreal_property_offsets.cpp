#include "unreal/layout/unreal_property_offsets.h"

namespace URK::Unreal {
namespace {

constexpr std::int32_t kMaxChildPropertiesOffset = 0x80;
constexpr std::int32_t kMaxFieldOffset = 0x48;
constexpr std::int32_t kMaxPropertyOffset = 0x100;

// Owner had a trailing bool before UE5.1.1; start past the narrow layout.
constexpr std::int32_t kFieldClassMinOffset = 0x08;
constexpr std::int32_t kFieldOwnerOffset = 0x10;

// Flags every edit-exposed POD member of an engine struct carries.
constexpr std::uint64_t kPodMemberFlags = kPropertyFlagEdit | kPropertyFlagZeroConstructor | kPropertyFlagSaveGame |
                                          kPropertyFlagIsPlainOldData | kPropertyFlagNoDestructor |
                                          kPropertyFlagHasGetValueTypeHash;

// First offset where both fields are mapped pointers; FFieldClass has no vtable.
std::int32_t FindSharedReadablePointer(const MemoryReader &reader, Address fieldA, Address fieldB, std::int32_t start,
                                       std::int32_t limit) {
    for (std::int32_t offset = start; offset <= limit; offset += sizeof(Address)) {
        if (reader.PointsToReadable(fieldA + offset) && reader.PointsToReadable(fieldB + offset))
            return offset;
    }
    return kOffsetNotFound;
}

std::int32_t FindSharedFieldPointer(const MemoryReader &reader, Address fieldA, Address fieldB, std::int32_t start,
                                    std::int32_t limit) {
    for (std::int32_t offset = start; offset <= limit; offset += sizeof(Address)) {
        if (reader.PointsToObject(fieldA + offset) && reader.PointsToObject(fieldB + offset))
            return offset;
    }
    return kOffsetNotFound;
}

Address FirstProperty(const MemoryReader &reader, const ObjectFinder &finder, const FieldOffsets &fields,
                      const char *structName) {
    if (fields.childProperties == kOffsetNotFound)
        return kNullAddress;
    const Address structure = finder.Find(structName);
    if (structure == kNullAddress)
        return kNullAddress;
    const std::optional<Address> first = reader.ReadPointer(structure + fields.childProperties);
    return first ? *first : kNullAddress;
}

// Members of the anchor structs, by the order their C++ declaration fixes.
Address Member(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
               const FieldOffsets &fields, const char *structName, std::string_view member) {
    const PropertyChain chain(finder.Reader(), names, structs, fields);
    const Address structure = finder.Find(structName);
    if (structure == kNullAddress)
        return kNullAddress;
    return chain.FindMember(structure, member);
}

} // namespace

Address PropertyChain::First(Address structObject) const {
    if (structObject == kNullAddress || fields_.childProperties == kOffsetNotFound)
        return kNullAddress;
    const std::optional<Address> first = reader_->ReadPointer(structObject + fields_.childProperties);
    return first ? *first : kNullAddress;
}

Address PropertyChain::Next(Address field) const {
    if (field == kNullAddress || fields_.fieldNext == kOffsetNotFound)
        return kNullAddress;
    const std::optional<Address> next = reader_->ReadPointer(field + fields_.fieldNext);
    return next ? *next : kNullAddress;
}

Address PropertyChain::ClassOf(Address field) const {
    if (field == kNullAddress || fields_.fieldClass == kOffsetNotFound)
        return kNullAddress;
    const std::optional<Address> fieldClass = reader_->ReadPointer(field + fields_.fieldClass);
    return fieldClass ? *fieldClass : kNullAddress;
}

std::optional<std::string> PropertyChain::NameOf(Address field) const {
    if (field == kNullAddress || fields_.fieldName == kOffsetNotFound)
        return std::nullopt;
    // With its number: Blueprint compilers name members Node, Node_1, Node_2...
    return names_->ReadFName(field + fields_.fieldName);
}

Address PropertyChain::FindMember(Address structObject, std::string_view name) const {
    // Longer chains mean wrong offsets.
    constexpr int kMaxChainLength = 0x1000;

    Address field = First(structObject);
    for (int step = 0; field != kNullAddress && step < kMaxChainLength; ++step) {
        const std::optional<std::string> fieldName = NameOf(field);
        if (fieldName && SameName(*fieldName, name))
            return field;
        field = Next(field);
    }
    return kNullAddress;
}

Address PropertyChain::FindMemberDeep(Address structObject, std::string_view name) const {
    // Bounds the walk so a corrupt chain can't loop.
    constexpr int kMaxDepth = 0x40;

    Address current = structObject;
    for (int depth = 0; depth < kMaxDepth && current != kNullAddress; ++depth) {
        if (const Address member = FindMember(current, name); member != kNullAddress)
            return member;
        if (structs_.superStruct == kOffsetNotFound)
            break;
        const std::optional<Address> super = reader_->ReadPointer(current + structs_.superStruct);
        if (!super || *super == current)
            break;
        current = *super;
    }
    return kNullAddress;
}

std::int32_t FindChildPropertiesOffset(const ObjectFinder &finder, const StructOffsets &structs) {
    if (structs.children == kOffsetNotFound)
        return kOffsetNotFound;

    const Address color = finder.Find("Color");
    const Address guid = finder.Find("Guid");
    if (color == kNullAddress || guid == kNullAddress)
        return kOffsetNotFound;

    return FindSharedFieldPointer(finder.Reader(), color, guid, structs.children + sizeof(Address),
                                  kMaxChildPropertiesOffset);
}

std::int32_t FindFieldClassOffset(const ObjectFinder &finder, const StructOffsets &, const FieldOffsets &fields) {
    const Address guidField = FirstProperty(finder.Reader(), finder, fields, "Guid");
    const Address vectorField = FirstProperty(finder.Reader(), finder, fields, "Vector");
    if (guidField == kNullAddress || vectorField == kNullAddress)
        return kOffsetNotFound;

    return FindSharedReadablePointer(finder.Reader(), guidField, vectorField, kFieldClassMinOffset, 0x30);
}

std::int32_t FindFieldNextOffset(const ObjectFinder &finder, const StructOffsets &, const FieldOffsets &fields) {
    const Address guidField = FirstProperty(finder.Reader(), finder, fields, "Guid");
    const Address vectorField = FirstProperty(finder.Reader(), finder, fields, "Vector");
    if (guidField == kNullAddress || vectorField == kNullAddress)
        return kOffsetNotFound;

    // The wide form's trailing bool isn't a pointer, so the scan reaches the real Next.
    return FindSharedFieldPointer(finder.Reader(), guidField, vectorField, kFieldOwnerOffset + sizeof(Address),
                                  kMaxFieldOffset);
}

std::int32_t FindFieldNameOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &,
                                 const FieldOffsets &fields) {
    // First member names are fixed by declaration.
    struct KnownFirstMember {
        const char *structName;
        const char *memberName;
    };
    constexpr KnownFirstMember kKnown[] = {{"Guid", "A"}, {"Color", "B"}, {"Vector", "X"}};

    const MemoryReader &reader = finder.Reader();

    for (std::int32_t offset = kFieldOwnerOffset; offset <= kMaxFieldOffset; offset += 4) {
        if (offset == fields.fieldClass || offset == fields.fieldNext)
            continue;

        bool satisfied = true;
        int checked = 0;
        for (const KnownFirstMember &known : kKnown) {
            const Address field = FirstProperty(reader, finder, fields, known.structName);
            if (field == kNullAddress)
                continue;
            ++checked;
            const std::optional<std::uint32_t> comparisonIndex = reader.ReadUInt32(field + offset);
            if (!comparisonIndex) {
                satisfied = false;
                break;
            }
            const std::optional<std::string> resolved = names.Read(*comparisonIndex);
            if (!resolved || *resolved != known.memberName) {
                satisfied = false;
                break;
            }
        }
        if (satisfied && checked > 0)
            return offset;
    }
    return kOffsetNotFound;
}

std::int32_t FindFieldClassCastFlagsOffset(const ObjectFinder &finder, const NameTable &names,
                                           const StructOffsets &structs, const FieldOffsets &fields) {
    const PropertyChain chain(finder.Reader(), names, structs, fields);

    const Address guidField = FirstProperty(finder.Reader(), finder, fields, "Guid");
    const Address colorField = FirstProperty(finder.Reader(), finder, fields, "Color");

    // FGuid (int32s) and FColor (bytes) property classes differ in one flag.
    const std::vector<Anchor<std::uint64_t>> anchors{
        {chain.ClassOf(guidField),
         kCastFlagField | kCastFlagProperty | kCastFlagNumericProperty | kCastFlagIntProperty},
        {chain.ClassOf(colorField),
         kCastFlagField | kCastFlagProperty | kCastFlagNumericProperty | kCastFlagByteProperty},
    };

    const std::int32_t offset = FindAnchoredOffset(finder.Reader(), anchors, sizeof(Address), 0x30);
    return offset;
}

std::int32_t FindElementSizeOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                   const FieldOffsets &fields) {
    if (fields.fieldName == kOffsetNotFound)
        return kOffsetNotFound;

    const std::vector<Anchor<std::int32_t>> anchors{
        {Member(finder, names, structs, fields, "Guid", "A"), 0x04},
        {Member(finder, names, structs, fields, "Guid", "C"), 0x04},
        {Member(finder, names, structs, fields, "Guid", "D"), 0x04},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, fields.fieldName + 8, kMaxPropertyOffset);
}

std::int32_t FindArrayDimOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                const FieldOffsets &fields) {
    if (fields.elementSize == kOffsetNotFound)
        return kOffsetNotFound;

    const std::vector<Anchor<std::int32_t>> anchors{
        {Member(finder, names, structs, fields, "Guid", "A"), 0x01},
        {Member(finder, names, structs, fields, "Guid", "C"), 0x01},
        {Member(finder, names, structs, fields, "Guid", "D"), 0x01},
    };
    // ArrayDim 1 is too common to search alone; look next to the element size.
    return FindAnchoredOffset(finder.Reader(), anchors, fields.elementSize - 0x10, fields.elementSize + 0x10);
}

std::int32_t FindOffsetInternalOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                      const FieldOffsets &fields) {
    if (fields.fieldName == kOffsetNotFound)
        return kOffsetNotFound;

    // FColor is B, G, R, A: B at 0, G at 1.
    const std::vector<Anchor<std::int32_t>> anchors{
        {Member(finder, names, structs, fields, "Color", "B"), 0x00},
        {Member(finder, names, structs, fields, "Color", "G"), 0x01},
        {Member(finder, names, structs, fields, "Guid", "C"), 0x08},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, fields.fieldName + 8, kMaxPropertyOffset);
}

std::int32_t FindPropertyFlagsOffset(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs,
                                     const FieldOffsets &fields) {
    if (fields.fieldName == kOffsetNotFound)
        return kOffsetNotFound;

    const Address guidA = Member(finder, names, structs, fields, "Guid", "A");
    Address colorR = Member(finder, names, structs, fields, "Color", "R");
    if (colorR == kNullAddress)
        colorR = Member(finder, names, structs, fields, "Color", "r");

    // FColor's members are blueprint-visible where FGuid's are not.
    std::vector<Anchor<std::uint64_t>> anchors{
        {guidA, kPodMemberFlags},
        {colorR, kPodMemberFlags | kPropertyFlagBlueprintVisible},
    };

    const std::int32_t offset =
        FindAnchoredOffset(finder.Reader(), anchors, fields.fieldName + 8, kMaxPropertyOffset);
    if (offset != kOffsetNotFound)
        return offset;

    // Builds that record an access specifier carry it in the same word.
    for (Anchor<std::uint64_t> &anchor : anchors)
        anchor.value |= kPropertyFlagNativeAccessSpecifierPublic;
    return FindAnchoredOffset(finder.Reader(), anchors, fields.fieldName + 8, kMaxPropertyOffset);
}

FieldOffsets FindFieldOffsets(const ObjectFinder &finder, const NameTable &names, const StructOffsets &structs) {
    FieldOffsets fields;
    fields.childProperties = FindChildPropertiesOffset(finder, structs);
    fields.fieldClass = FindFieldClassOffset(finder, structs, fields);
    fields.fieldNext = FindFieldNextOffset(finder, structs, fields);
    fields.fieldName = FindFieldNameOffset(finder, names, structs, fields);
    fields.fieldClassCastFlags = FindFieldClassCastFlagsOffset(finder, names, structs, fields);

    // Name lookups below need the chain above.
    fields.elementSize = FindElementSizeOffset(finder, names, structs, fields);
    fields.arrayDim = FindArrayDimOffset(finder, names, structs, fields);
    fields.offsetInternal = FindOffsetInternalOffset(finder, names, structs, fields);
    fields.propertyFlags = FindPropertyFlagsOffset(finder, names, structs, fields);
    return fields;
}

} // namespace URK::Unreal
