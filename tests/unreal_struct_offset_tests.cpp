// UField and UStruct offset calibration against a synthetic engine world.
//
// The world carries the objects the calibration anchors on - Field, Struct,
// Class, Actor, Color, Guid, Transform, the controller classes and the Kismet
// libraries - laid out at offsets chosen in the world header. Object header
// calibration and array discovery are proven by their own tests, so both are
// supplied directly and this test is only about the rung above them.
//
// It runs twice: once with properties as objects, as before UE4.25, and once
// without, which is what makes the two Children anchor sets meaningful.

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

void Run(bool legacyProperties, const char *label) {
    using namespace URK::Unreal;
    using namespace UnrealTest;
    std::printf("\n%s\n", label);

    const World world(WorldConfig{.legacyProperties = legacyProperties});

    std::optional<NameTable> names = NameTable::Resolve(world.Image(), world.PoolAddress());
    Check(names.has_value(), "name pool resolves");
    if (!names)
        return;

    const ObjectArray objects(world.Image(), world.ArrayAddress(), World::ArrayLayout());
    const ObjectFinder finder = ObjectFinder::Build(objects, *names, World::HeaderOffsets());
    Check(finder.IndexedCount() == static_cast<std::size_t>(objects.Num()), "every object is indexed by name");

    Check(finder.Find("Actor") != kNullAddress, "lookup by name finds a class");
    Check(finder.Find("NotAThing") == kNullAddress, "lookup rejects an unknown name");
    Check(finder.FindInOuter("UnPossess", "Controller") != kNullAddress, "lookup finds a member through its outer");
    Check(finder.FindInOuter("UnPossess", "PlayerController") == kNullAddress,
          "lookup rejects a member under the wrong outer");

    CheckOffset("first offset past the header", FirstOffsetPastHeader(World::HeaderOffsets()), kNextOffset);

    const StructOffsets offsets = FindStructOffsets(finder);
    Check(offsets.usesFProperty == !legacyProperties, "property system is told apart by probing for property objects");
    CheckOffset("UClass::ClassCastFlags", offsets.castFlags, kCastFlagsOffset);
    CheckOffset("UStruct::SuperStruct", offsets.superStruct, kSuperOffset);
    CheckOffset("UStruct::Children", offsets.children, kChildrenOffset);
    CheckOffset("UStruct::PropertiesSize", offsets.propertiesSize, kPropertiesSizeOffset);
    CheckOffset("UStruct::MinAlignment", offsets.minAlignment, kMinAlignmentOffset);
    CheckOffset("UField::Next", offsets.fieldNext, kNextOffset);

    Check(offsets.children != kChildPropertiesOffset, "Children is not confused with ChildProperties");
}

} // namespace

int main() {
    Run(false, "FProperty system (UE4.25 and later)");
    Run(true, "UField system (before UE4.25)");

    if (g_failures == 0) {
        std::printf("\nall checks passed\n");
        return 0;
    }
    std::printf("\n%d check(s) failed\n", g_failures);
    return 1;
}
