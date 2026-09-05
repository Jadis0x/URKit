// Covers the assembly-image and managed-type name matching that both the Mono
// and IL2CPP bindings depend on. These are the rules that decide whether a class
// or method lookup succeeds, and the shapes Unity reports change between engine
// versions, so every accepted spelling is pinned here.

#include "src/unity/unity_name_matching.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("%-72s %s\n", what.c_str(), condition ? "ok" : "FAILED");
    if (!condition)
        ++g_failures;
}

void CheckImage(const char *requested, const char *actual, bool expected) {
    const bool matched = URK::UnityNames::ImageNameMatches(requested, actual);
    Check(matched == expected, std::string("image ") + (expected ? "match  " : "reject ") + "\"" +
                                   (requested ? requested : "<null>") + "\" vs \"" + (actual ? actual : "<null>") +
                                   "\"");
}

void CheckType(const char *requested, const char *actual, bool expected) {
    const bool matched = URK::UnityNames::TypeNameMatches(actual, requested);
    Check(matched == expected, std::string("type  ") + (expected ? "match  " : "reject ") + "\"" +
                                   (requested ? requested : "<null>") + "\" vs \"" + (actual ? actual : "<null>") +
                                   "\"");
}

} // namespace

int main() {
    using namespace URK::UnityNames;

    // Unity reports image names with and without the extension depending on
    // version and on which API the name came from.
    CheckImage("UnityEngine.CoreModule.dll", "UnityEngine.CoreModule.dll", true);
    CheckImage("UnityEngine.CoreModule", "UnityEngine.CoreModule.dll", true);
    CheckImage("UnityEngine.CoreModule.dll", "UnityEngine.CoreModule", true);
    CheckImage("UnityEngine.CoreModule", "UnityEngine.CoreModule", true);

    // mono_image_get_filename returns a full path; mono_image_get_name does not.
    CheckImage("Assembly-CSharp.dll", "C:\\Game\\Managed\\Assembly-CSharp.dll", true);
    CheckImage("Assembly-CSharp", "C:/Game/Managed/Assembly-CSharp.dll", true);
    CheckImage("C:/Game/Managed/Assembly-CSharp.dll", "Assembly-CSharp", true);

    // Assembly names are case-insensitive on Windows.
    CheckImage("unityengine.coremodule.dll", "UnityEngine.CoreModule.dll", true);

    // Distinct modules must not collide, and an empty request matches nothing.
    CheckImage("UnityEngine.CoreModule.dll", "UnityEngine.UIModule.dll", false);
    CheckImage("UnityEngine.dll", "UnityEngine.CoreModule.dll", false);
    CheckImage("", "UnityEngine.CoreModule.dll", false);
    CheckImage(nullptr, "UnityEngine.CoreModule.dll", false);
    CheckImage("UnityEngine.CoreModule.dll", nullptr, false);

    // ".dll" is only an extension at the end of the name.
    CheckImage("My.dll.Library.dll", "My.dll.Library", true);
    Check(StripDll("Plain") == "Plain", "StripDll leaves a name without an extension alone");
    Check(StripDll("A.dll") == "A", "StripDll removes a trailing extension");
    Check(StripDll(".dll") == ".dll", "StripDll keeps a name that is only an extension");

    // Every variant is produced once, so a lookup key stays stable.
    Check(ImageNameVariants("UnityEngine.CoreModule.dll").size() == 2, "a plain name yields two variants");
    Check(ImageNameVariants("Same").size() == 1, "an extensionless basename yields one variant");
    Check(ImageNameVariants("").empty(), "an empty name yields no variants");

    // C# aliases, CLR names and the generated SDK's spellings must all agree.
    CheckType("int", "System.Int32", true);
    CheckType("Int32", "System.Int32", true);
    CheckType("System.Int32", "int", true);
    CheckType("bool", "System.Boolean", true);
    CheckType("boolean", "System.Boolean", true);
    CheckType("float", "System.Single", true);
    CheckType("single", "System.Single", true);
    CheckType("double", "System.Double", true);
    CheckType("string", "System.String", true);
    CheckType("object", "System.Object", true);
    CheckType("void", "System.Void", true);
    CheckType("byte", "System.Byte", true);
    CheckType("sbyte", "System.SByte", true);
    CheckType("char", "System.Char", true);
    CheckType("short", "System.Int16", true);
    CheckType("ushort", "System.UInt16", true);
    CheckType("uint", "System.UInt32", true);
    CheckType("long", "System.Int64", true);
    CheckType("ulong", "System.UInt64", true);

    // TypeObject arguments are emitted as System.Type by the generated SDK; the
    // Mono binding used to normalize this differently from the IL2CPP one.
    CheckType("Type", "System.Type", true);
    CheckType("System.Type", "System.Type", true);

    // Similar-looking primitives are still distinct types.
    CheckType("int", "System.Int64", false);
    CheckType("float", "System.Double", false);
    CheckType("uint", "System.Int32", false);
    CheckType("string", "System.Object", false);

    // C++-side decorations the runtimes add around a type name.
    CheckType("UnityEngine.Vector3", "struct UnityEngine.Vector3", true);
    CheckType("UnityEngine.GameObject", "class UnityEngine.GameObject", true);

    // By-ref and pointer suffixes change the type and must survive normalization.
    CheckType("System.Int32&", "int&", true);
    CheckType("System.Int32", "System.Int32&", false);
    CheckType("System.Int32*", "System.Int32&", false);

    // Game-defined types have no alias and compare case-insensitively.
    CheckType("MyGame.PlayerController", "MyGame.PlayerController", true);
    CheckType("mygame.playercontroller", "MyGame.PlayerController", true);
    CheckType("MyGame.PlayerController", "MyGame.EnemyController", false);
    CheckType(nullptr, "System.Int32", false);
    CheckType("System.Int32", nullptr, false);

    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL PASS (0 failures)\n");
    return 0;
}
