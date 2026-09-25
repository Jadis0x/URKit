#include "unreal_struct_offsets.h"

namespace URK::Unreal {
namespace {

// UStruct fields sit well inside this; UClass::ClassCastFlags is the deepest.
constexpr std::int32_t kMaxStructOffset = 0x1B0;
constexpr std::int32_t kMaxFieldNextOffset = 0x60;

// Fixed engine sizes: FColor 4 bytes, FGuid 4 uint32s, FTransform SIMD aligned.
constexpr std::int32_t kColorSize = 0x04;
constexpr std::int32_t kGuidSize = 0x10;
constexpr std::int16_t kTransformAlignment = 0x10;
constexpr std::int16_t kPlayerControllerAlignment = 0x08;

std::int32_t AlignUp(std::int32_t value, std::int32_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// Members that exist as objects only before UE4.25, with their owner struct.
constexpr struct {
    const char *member;
    const char *owner;
} kLegacyPropertyProbes[] = {
    {"X", "Vector"},
    {"A", "Guid"},
    {"R", "Color"},
};

} // namespace

std::int32_t FirstOffsetPastHeader(const ObjectOffsets &offsets) {
    std::int32_t highest = 0;
    for (const std::int32_t field : {offsets.flags, offsets.index, offsets.classPointer, offsets.name, offsets.outer}) {
        if (field != kOffsetNotFound && field > highest)
            highest = field;
    }
    return AlignUp(highest + static_cast<std::int32_t>(sizeof(std::int32_t)), static_cast<std::int32_t>(sizeof(Address)));
}

std::uint64_t CastFlagsOf(const ObjectFinder &finder, const StructOffsets &structs, Address object) {
    if (object == kNullAddress || structs.castFlags == kOffsetNotFound)
        return 0;
    const Address classObject = finder.ClassOf(object);
    if (classObject == kNullAddress)
        return 0;
    const std::optional<std::uint64_t> flags =
        finder.Reader().ReadAs<std::uint64_t>(classObject + structs.castFlags);
    return flags ? *flags : 0;
}

bool ObjectIs(const ObjectFinder &finder, const StructOffsets &structs, Address object, std::uint64_t flag) {
    return (CastFlagsOf(finder, structs, object) & flag) == flag;
}

bool UsesFPropertySystem(const ObjectFinder &finder) {
    for (const auto &probe : kLegacyPropertyProbes) {
        if (finder.FindInOuter(probe.member, probe.owner) != kNullAddress)
            return false;
    }
    return true;
}

std::int32_t FindCastFlagsOffset(const ObjectFinder &finder) {
    const std::vector<Anchor<std::uint64_t>> anchors{
        {finder.Find("Actor"), kCastFlagActor},
        {finder.Find("Class"), kCastFlagField | kCastFlagStruct | kCastFlagClass},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, FirstOffsetPastHeader(finder.Offsets()), kMaxStructOffset);
}

std::int32_t FindSuperStructOffset(const ObjectFinder &finder) {
    // Some builds spell UStruct's own object "struct"; both are tried.
    Address structClass = finder.Find("Struct");
    if (structClass == kNullAddress)
        structClass = finder.Find("struct");

    const std::vector<Anchor<Address>> anchors{
        {structClass, finder.Find("Field")},
        {finder.Find("Class"), structClass},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, FirstOffsetPastHeader(finder.Offsets()), kMaxStructOffset);
}

std::int32_t FindChildrenOffset(const ObjectFinder &finder) {
    // Children is a function once properties moved to FField.
    const std::vector<Anchor<Address>> anchors{
        {finder.Find("PlayerController"), finder.FindInOuter("WasInputKeyJustReleased", "PlayerController")},
        {finder.Find("Controller"), finder.FindInOuter("UnPossess", "Controller")},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, FirstOffsetPastHeader(finder.Offsets()), kMaxStructOffset);
}

std::int32_t FindPropertiesSizeOffset(const ObjectFinder &finder) {
    const std::vector<Anchor<std::int32_t>> anchors{
        {finder.Find("Color"), kColorSize},
        {finder.Find("Guid"), kGuidSize},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, FirstOffsetPastHeader(finder.Offsets()), kMaxStructOffset);
}

std::int32_t FindMinAlignmentOffset(const ObjectFinder &finder) {
    const std::vector<Anchor<std::int16_t>> anchors{
        {finder.Find("Transform"), kTransformAlignment},
        {finder.Find("PlayerController"), kPlayerControllerAlignment},
    };
    return FindAnchoredOffset(finder.Reader(), anchors, FirstOffsetPastHeader(finder.Offsets()), kMaxStructOffset);
}

std::int32_t FindFieldNextOffset(const ObjectFinder &finder, std::int32_t childrenOffset) {
    if (childrenOffset == kOffsetNotFound)
        return kOffsetNotFound;

    const MemoryReader &reader = finder.Reader();

    // First child is a function with a successor, so the shared offset is Next.
    const auto firstChild = [&](const char *owner) -> Address {
        const Address structure = finder.Find(owner);
        if (structure == kNullAddress)
            return kNullAddress;
        const std::optional<Address> child = reader.ReadPointer(structure + childrenOffset);
        return child ? *child : kNullAddress;
    };

    const Address childA = firstChild("KismetSystemLibrary");
    const Address childB = firstChild("KismetStringLibrary");
    if (childA == kNullAddress || childB == kNullAddress)
        return kNullAddress;

    for (std::int32_t offset = FirstOffsetPastHeader(finder.Offsets()); offset <= kMaxFieldNextOffset;
         offset += sizeof(Address)) {
        if (reader.PointsToObject(childA + offset) && reader.PointsToObject(childB + offset))
            return offset;
    }
    return kOffsetNotFound;
}

StructOffsets FindStructOffsets(const ObjectFinder &finder) {
    StructOffsets offsets;
    offsets.castFlags = FindCastFlagsOffset(finder);
    offsets.superStruct = FindSuperStructOffset(finder);
    offsets.children = FindChildrenOffset(finder);
    offsets.propertiesSize = FindPropertiesSizeOffset(finder);
    offsets.minAlignment = FindMinAlignmentOffset(finder);
    offsets.fieldNext = FindFieldNextOffset(finder, offsets.children);
    return offsets;
}

} // namespace URK::Unreal
