// FField and FProperty offset calibration, and the member walk built on it.
//
// Runs against both FFieldVariant shapes: the one carrying a trailing bool, and
// the narrowed one from UE5.1.1. Those put FField::Next at different offsets,
// so recovering both is what shows Next is measured rather than assumed.

#include "tests/unreal_fake_world.h"

#include <cstdio>
#include <optional>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("  %-62s %s\n", what.c_str(), condition ? "ok" : "FAILED");
    if (!condition)
        ++g_failures;
}

void CheckOffset(const char *what, std::int32_t actual, std::int32_t expected) {
    char detail[160];
    std::snprintf(detail, sizeof(detail), "%s resolved to 0x%02X (expected 0x%02X)", what, actual, expected);
    Check(actual == expected, detail);
}

void Run(bool wideFieldOwner, const char *label) {
    using namespace URK::Unreal;
    using namespace UnrealTest;
    std::printf("\n%s\n", label);

    const World world(WorldConfig{.wideFieldOwner = wideFieldOwner});
    const FieldLayout &expected = world.Fields();

    std::optional<NameTable> names = NameTable::Resolve(world.Image(), world.PoolAddress());
    Check(names.has_value(), "name pool resolves");
    if (!names)
        return;

    const ObjectArray objects(world.Image(), world.ArrayAddress(), World::ArrayLayout());
    const ObjectFinder finder = ObjectFinder::Build(objects, *names, World::HeaderOffsets());
    const StructOffsets structs = FindStructOffsets(finder);
    Check(structs.usesFProperty, "world is recognized as using the FField system");

    const FieldOffsets fields = FindFieldOffsets(finder, *names, structs);
    Check(fields.Resolved(), "every FField and FProperty offset is resolved");

    CheckOffset("UStruct::ChildProperties", fields.childProperties, kChildPropertiesOffset);
    CheckOffset("FField::ClassPrivate", fields.fieldClass, kFieldClassOffset);
    CheckOffset("FField::Next", fields.fieldNext, expected.next);
    CheckOffset("FField::NamePrivate", fields.fieldName, expected.name);
    CheckOffset("FFieldClass::CastFlags", fields.fieldClassCastFlags, kFieldClassCastFlagsOffset);
    CheckOffset("FProperty::ArrayDim", fields.arrayDim, expected.arrayDim);
    CheckOffset("FProperty::ElementSize", fields.elementSize, expected.elementSize);
    CheckOffset("FProperty::PropertyFlags", fields.propertyFlags, expected.propertyFlags);
    CheckOffset("FProperty::Offset_Internal", fields.offsetInternal, expected.offsetInternal);

    // With the chain calibrated, a struct's members can be walked and read.
    const PropertyChain chain(world.Image(), *names, structs, fields);
    const Address guid = finder.Find("Guid");
    const Address color = finder.Find("Color");

    const Address guidC = chain.FindMember(guid, "C");
    Check(guidC != kNullAddress, "member lookup walks the chain past the first entry");
    Check(chain.FindMember(guid, "Z") == kNullAddress, "member lookup rejects a name the struct lacks");

    const std::optional<std::string> firstName = chain.NameOf(chain.First(color));
    Check(firstName && *firstName == "B", "FColor's first member reads back as B, matching its declaration");

    // Reading a member's placement through the calibrated offsets is the point
    // of the whole ladder, so it is checked end to end here.
    const std::optional<std::int32_t> guidCOffset = world.Image().ReadInt32(guidC + fields.offsetInternal);
    Check(guidCOffset && *guidCOffset == 0x08, "FGuid::C reports offset 8 through the calibrated field");

    const Address colorG = chain.FindMember(color, "G");
    const std::optional<std::int32_t> colorGSize = world.Image().ReadInt32(colorG + fields.elementSize);
    Check(colorGSize && *colorGSize == 1, "FColor::G reports element size 1");

    const std::optional<std::uint64_t> colorClassFlags =
        world.Image().ReadAs<std::uint64_t>(chain.ClassOf(colorG) + fields.fieldClassCastFlags);
    Check(colorClassFlags && (*colorClassFlags & kCastFlagByteProperty) != 0,
          "FColor::G is classified as a byte property");

    const std::optional<std::uint64_t> guidClassFlags =
        world.Image().ReadAs<std::uint64_t>(chain.ClassOf(guidC) + fields.fieldClassCastFlags);
    Check(guidClassFlags && (*guidClassFlags & kCastFlagIntProperty) != 0, "FGuid::C is classified as an int property");
}

void RunLegacy() {
    using namespace URK::Unreal;
    using namespace UnrealTest;
    std::printf("\nUField system (before UE4.25)\n");

    const World world(WorldConfig{.legacyProperties = true});
    std::optional<NameTable> names = NameTable::Resolve(world.Image(), world.PoolAddress());
    if (!names)
        return;

    const ObjectArray objects(world.Image(), world.ArrayAddress(), World::ArrayLayout());
    const ObjectFinder finder = ObjectFinder::Build(objects, *names, World::HeaderOffsets());
    const StructOffsets structs = FindStructOffsets(finder);

    // There is no FField chain to calibrate before UE4.25, and reporting
    // offsets anyway would hand the caller numbers pointing at nothing.
    const FieldOffsets fields = FindFieldOffsets(finder, *names, structs);
    Check(!fields.Resolved(), "no FField offsets are invented for a build without them");
    Check(fields.childProperties == kOffsetNotFound, "ChildProperties stays unresolved");
}

} // namespace

int main() {
    Run(false, "FFieldVariant without the trailing bool (UE5.1.1 and later)");
    Run(true, "FFieldVariant with the trailing bool (before UE5.1.1)");
    RunLegacy();

    if (g_failures == 0) {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
