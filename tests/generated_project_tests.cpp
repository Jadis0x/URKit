// Generates Mono, IL2CPP and Unreal projects and compiles a probe against each.
// Templates are raw strings, so this is the only thing that type-checks them.

#include "src/sdk/il2cpp_sdk_generator.h"
#include "src/sdk/mono_sdk_generator.h"
#include "src/sdk/unreal_sdk_generator.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("%-72s %s\n", what.c_str(), condition ? "ok" : "FAILED");
    if (!condition)
        ++g_failures;
}

void Write(const fs::path &path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

bool CompilerIsMsvc() {
    std::string compiler = fs::path(URK_TEST_CXX_COMPILER).filename().string();
    for (char &character : compiler)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return compiler.find("clang") == std::string::npos && compiler.find("cl") != std::string::npos;
}

std::string Quote(const std::string &value) {
    return "\"" + value + "\"";
}

// Compiles without linking; only diagnostics matter.
bool SyntaxCheck(const fs::path &projectRoot, const fs::path &source, const fs::path &workingDirectory) {
    const std::string root = projectRoot.string();
    std::string command = Quote(URK_TEST_CXX_COMPILER);
    if (CompilerIsMsvc()) {
        command += " /nologo /std:c++20 /Zs /EHsc /permissive- /DWIN32_LEAN_AND_MEAN /DNOMINMAX";
        command += " /I" + Quote(root) + " /I" + Quote((projectRoot / "mod").string());
        command += " /TP " + Quote(source.string());
    } else {
        command += " -std=c++20 -fsyntax-only -DWIN32_LEAN_AND_MEAN -DNOMINMAX";
        command += " -I" + Quote(root) + " -I" + Quote((projectRoot / "mod").string());
        command += " -x c++ " + Quote(source.string());
    }
    const fs::path previous = fs::current_path();
    fs::current_path(workingDirectory);
    const int status = std::system(("\"" + command + "\"").c_str());
    fs::current_path(previous);
    return status == 0;
}

// Instantiates version-sensitive template paths; nothing runs.
constexpr std::string_view kProbeSource = R"PROBE(
#include "sdk/unity/unity.h"

#include <string>
#include <vector>

namespace {

void probe_object_finders() {
    // Renamed in Unity 2022.2; both names must instantiate.
    std::vector<URK::Unity::GameObject> byOldName = URK::Unity::Object::FindObjectsOfType<URK::Unity::GameObject>();
    std::vector<URK::Unity::GameObject> byNewName = URK::Unity::Object::FindObjectsByType<URK::Unity::GameObject>(
        URK::Unity::FindObjectsSortMode::None);
    std::vector<URK::Unity::GameObject> all = URK::Unity::Object::FindObjectsOfTypeAll<URK::Unity::GameObject>();
    (void)byOldName;
    (void)byNewName;
    (void)all;
}

void probe_unqualified_type_resolution() {
    // An empty image means "scan every loaded assembly" on both backends.
    constexpr URK::Unity::TypeRef unqualified{"", "MyGame", "PlayerController"};
    constexpr URK::Unity::TypeRef qualified{"Assembly-CSharp.dll", "MyGame", "PlayerController"};
    (void)unqualified.resolve_class();
    (void)unqualified.resolve_type_object();
    (void)qualified.resolve_class();
}

void probe_components(URK::Unity::GameObject object) {
    URK::Unity::Transform transform = object.GetComponent<URK::Unity::Transform>();
    (void)transform.position();
    transform.set_position(URK::Unity::Vector3{0.0f, 1.0f, 2.0f});
    (void)object.GetComponentInChildren<URK::Unity::Camera>(true);
    (void)object.GetComponents<URK::Unity::Object>();
    (void)object.HasComponent<URK::Unity::Renderer>();
    (void)object.name();
    (void)object.activeInHierarchy();
}

void probe_scene_traversal() {
    (void)URK::Unity::SceneManager::GetActiveScene();
    (void)URK::Unity::SceneManager::GetLoadedSceneRoots();
    (void)URK::Unity::SceneManager::FindSceneGameObjects(false);
    (void)URK::Unity::SceneManager::FindSceneGameObjects(true);
}

void probe_stripped_member_detection() {
    // Stripped members must be detectable without invoking them.
    (void)URK::Unity::has_method(URK::Unity::GameObjectType, "get_scene", 0);
    (void)URK::Unity::has_method(URK::Unity::TransformType, "SetParent", 1);
    (void)URK::Unity::has_property(URK::Unity::GameObjectType, "tag");
    (void)URK::Unity::GameObject::scene_available();
}

void probe_reflection(URK::Unity::Object target) {
    (void)target.GetField<int>("health");
    target.SetField<float>("speed", 1.0f);
    (void)target.GetProperty<bool>("enabled");
    (void)target.Call<int>("Damage", 5);
    (void)target.Call<void*>("Describe", "text");
}

void probe_statics() {
    (void)URK::Unity::Time::deltaTime();
    URK::Unity::Time::set_timeScale(1.0f);
    (void)URK::Unity::Screen::width();
    (void)URK::Unity::Input::GetKey(URK::Unity::KeyCode::A);
}

void keep_referenced(URK::Unity::GameObject object) {
    probe_object_finders();
    probe_unqualified_type_resolution();
    probe_components(object);
    probe_scene_traversal();
    probe_reflection(object);
    probe_statics();
}

} // namespace

extern "C" void urk_generated_project_probe(void *handle) {
    keep_referenced(URK::Unity::GameObject{handle});
}
)PROBE";

constexpr std::string_view kIl2CppHelperProbeSource = R"PROBE(
#include "sdk/il2cpp/il2cpp_helpers.h"

extern "C" int urk_il2cpp_helper_probe(void *target) {
    // Guards a resolved icall target before it is hooked.
    return URK::il2cpp::helpers::is_valid_icall_target(target) ? 1 : 0;
}
)PROBE";

struct GeneratedProject {
    fs::path root;
    std::string label;
};

// Type dump with codegen edge cases: reserved names, duplicates, untyped kinds, skipped functions.
std::string UnrealTypeDump() {
    const auto row = [](std::initializer_list<std::string_view> fields) {
        std::string line;
        for (const std::string_view field : fields)
            (line += field) += '\t';
        line.back() = '\n';
        return line;
    };
    std::string dump = row({"URKIT-UNREAL-TYPES", "2"}) + row({"IMAGE", "1", "2"}) + row({"ENGINE", "5.8.0"});
    dump += row({"C", "Object", "/Script/CoreUObject", "", ""});
    dump += row({"C", "Actor", "/Script/Engine", "Object", "/Script/CoreUObject"});
    dump += row({"P", "Owner", "object", "8", "1", "0", "Actor", "/Script/Engine"});
    dump += row({"P", "Instigator", "object", "8", "1", "0", "Pawn", "/Script/Engine"});
    dump += row({"P", "class", "int32", "4", "1", "0", "", ""});
    dump += row({"P", "Actor", "float", "4", "1", "0", "", ""});
    dump += row({"P", "name", "bool", "1", "1", "0", "", ""});
    dump += row({"P", "My Var", "double", "8", "1", "0", "", ""});
    dump += row({"P", "My_Var", "byte", "1", "1", "0", "", ""});
    dump += row({"P", "Weights", "float", "4", "3", "0", "", ""});
    dump += row({"P", "Location", "struct", "24", "1", "0", "Missing", "/Script/CoreUObject"});
    dump += row({"P", "Spot", "struct", "24", "1", "0", "Vector", "/Script/CoreUObject"});
    dump += row({"P", "Tag", "name", "8", "1", "0", "", ""});
    dump += row({"P", "Label", "string", "16", "1", "0", "", ""});
    dump += row({"C", "Pawn", "/Script/Engine", "Actor", "/Script/Engine"});
    dump += row({"F", "GetController", "400", ""});
    dump += row({"A", "ReturnValue", "object", "8", "1", "580", "Actor", "/Script/Engine"});
    dump += row({"F", "GetBounds", "400"});
    dump += row({"A", "Radius", "float", "4", "1", "180", "", ""});
    dump += row({"A", "ReturnValue", "bool", "1", "1", "580", "", ""});
    dump += row({"F", "MakeOne", "2400"});
    dump += row({"A", "Mode", "enum", "1", "1", "80", "", ""});
    dump += row({"A", "Target", "object", "8", "1", "80", "Actor", "/Script/Engine"});
    dump += row({"A", "ReturnValue", "int32", "4", "1", "580", "", ""});
    dump += row({"F", "Teleport", "400"});
    dump += row({"A", "Where", "struct", "24", "1", "80", "Vector", "/Script/CoreUObject"});
    dump += row({"F", "GetLabel", "400"});
    dump += row({"A", "ReturnValue", "struct", "24", "1", "580", "Labelled", "/Script/Engine"});
    dump += row({"F", "Blocked", "400"});
    dump += row({"A", "Where", "struct", "24", "1", "80", "Missing", "/Script/CoreUObject"});
    dump += row({"F", "GetSpot", "400"});
    dump += row({"A", "ReturnValue", "struct", "24", "1", "580", "Vector", "/Script/CoreUObject"});
    dump += row({"F", "Sweep", "400"});
    dump += row({"A", "Hit", "struct", "40", "1", "180", "HitLike", "/Script/Engine"});
    dump += row({"A", "ReturnValue", "bool", "1", "1", "580", "", ""});
    dump += row({"F", "OnHit__DelegateSignature", "130000"});
    dump += row({"F", "ExecuteUbergraph_Pawn", "0"});
    dump += row({"A", "EntryPoint", "int32", "4", "1", "80", "", ""});
    dump += row({"C", "Settings", "/Script/PluginA", "Object", "/Script/CoreUObject"});
    dump += row({"C", "Settings", "/Script/PluginB", "Object", "/Script/CoreUObject"});
    dump += row({"C", "BP Door_C", "/Game/Doors/BP Door", "Actor", "/Script/Engine"});
    dump += row({"P", "Open", "bool", "1", "1", "0", "", ""});
    dump += row({"F", "Slam", "400"});
    dump += row({"F", "InpActEvt_Jump_K2Node_InputActionEvent_0", "400"});
    // Struct cases: offsets, bitfields, nesting, objects, byte members, inherit-only.
    const auto field = [&](std::string_view name, std::string_view kind, std::string_view size,
                           std::string_view offset, std::string_view inner = "", std::string_view innerPackage = "",
                           std::string_view mask = "0", std::string_view fieldMask = "255") {
        return row({"M", name, kind, size, "1", "0", inner, innerPackage, offset, "0", mask, fieldMask});
    };
    dump += row({"S", "Vector", "/Script/CoreUObject", "", "", "24", "8"});
    dump += field("X", "double", "8", "0") + field("Y", "double", "8", "8") + field("Z", "double", "8", "16");
    dump += row({"S", "VectorNet", "/Script/Engine", "Vector", "/Script/CoreUObject", "24", "8"});
    dump += row({"S", "HitLike", "/Script/Engine", "", "", "40", "8"});
    dump += field("bHit", "bool", "1", "0", "", "", "1", "1") + field("bStart", "bool", "1", "0", "", "", "2", "2");
    dump += field("Location", "struct", "24", "8", "Vector", "/Script/CoreUObject");
    dump += field("Actor", "object", "8", "32", "Actor", "/Script/Engine");
    dump += row({"S", "Tagged", "/Script/Engine", "", "", "32", "8"});
    dump += field("Tag", "name", "8", "0") + field("Count", "int32", "4", "8") + field("Items", "array", "16", "16");
    dump += row({"S", "Labelled", "/Script/Engine", "", "", "24", "8"});
    dump += field("Label", "text", "24", "0");
    return dump;
}

std::string ReadText(const fs::path &path);

void CheckUnrealTypes(const GeneratedProject &project) {
    const fs::path types = project.root / "sdk/unreal/types";
    for (const char *file : {"CoreUObject/Object.h", "Engine/Actor.h", "Engine/Pawn.h", "PluginA/Settings.h",
                             "PluginB/Settings_PluginB.h", "Game/Doors/BP_Door_C.h", "INDEX.md"})
        Check(fs::is_regular_file(types / file), project.label + ": types/" + file + " is generated");
    const std::string door = ReadText(types / "Game/Doors/BP_Door_C.h");
    const std::size_t compiled = door.find("// Blueprint compiler output");
    Check(door.find("#include \"../../Engine/Actor.h\"") != std::string::npos &&
              door.find("#include \"../../../unreal_runtime.h\"") == std::string::npos,
          project.label + ": BP_Door_C.h includes its super across folders");
    Check(compiled != std::string::npos && door.find("InpActEvt_Jump") > compiled && door.find("Slam()") < compiled,
          project.label + ": BP_Door_C.h puts compiler-made functions after the authored ones");
    const std::string index = ReadText(types / "INDEX.md");
    Check(index.find("### Game/Doors") < index.find("## Engine and plugins") &&
              index.find("### Engine") != std::string::npos && index.find("BP_Door_C") != std::string::npos,
          project.label + ": INDEX.md lists the game's folders before the engine's");
    const std::string actor = ReadText(types / "Engine/Actor.h");
    for (const char *expected : {"class_()", "Actor_()", "name_()", "My_Var()", "My_Var_2()",
                                 "Weights(std::int32_t index)", "No typed form yet: Location (struct).",
                                 "::URK::unreal::StructMember<::URK::unreal::types::Vector> Spot()",
                                 "class Pawn;", "\"Actor\", \"/Script/Engine\""})
        Check(actor.find(expected) != std::string::npos, project.label + ": Actor.h has " + expected);
    const std::string pawn = ReadText(types / "Engine/Pawn.h");
    for (const char *expected : {"template <typename UrkR = ::URK::unreal::types::Actor> UrkR GetController()",
                                 "std::optional<bool> GetBounds(float *Radius)",
                                 "static std::optional<std::int32_t> MakeOne(std::uint8_t Mode",
                                 "bool Teleport(const ::URK::unreal::types::Vector &Where)",
                                 "std::optional<::URK::unreal::types::Vector> GetSpot()",
                                 "std::optional<bool> Sweep(::URK::unreal::types::HitLike *Hit)",
                                 "std::optional<::URK::unreal::types::Labelled> GetLabel()",
                                 "No typed form yet: Blocked()."})
        Check(pawn.find(expected) != std::string::npos, project.label + ": Pawn.h has " + expected);
    Check(pawn.find("OnHit") == std::string::npos && pawn.find("ExecuteUbergraph") == std::string::npos,
          project.label + ": Pawn.h leaves out delegate signatures and the ubergraph");
    const std::string hit = ReadText(types / "Engine/HitLike.h");
    for (const char *expected : {"struct alignas(8) HitLike", "std::uint8_t urk_bits_0;", "bool bHit() const",
                                 "void set_bStart(bool value)", "::URK::unreal::types::Vector Location;",
                                 "::URK::unreal::StructObject<::URK::unreal::types::Actor> Actor;",
                                 "static_assert(sizeof(HitLike) == HitLike::kSize);",
                                 "static_assert(offsetof(HitLike, Actor) == 32);", "{\"bStart\", 0, 1, 1, 1, 0, 2, 2}"})
        Check(hit.find(expected) != std::string::npos, project.label + ": HitLike.h has " + expected);
    const std::string tagged = ReadText(types / "Engine/Tagged.h");
    Check(tagged.find("std::uint8_t urk_opaque_Tag[8];") != std::string::npos &&
              tagged.find("std::uint8_t urk_opaque_Items[16];") != std::string::npos &&
              tagged.find("std::int32_t Count;") != std::string::npos,
          project.label + ": Tagged.h keeps names and arrays as bytes");
    Check(ReadText(types / "Engine/VectorNet.h").find("double Z;") != std::string::npos,
          project.label + ": VectorNet.h carries its super's members");
}

bool GenerateBoth(const fs::path &workspace, std::vector<GeneratedProject> *projects) {
    const fs::path gameDirectory = workspace / "game";
    fs::create_directories(gameDirectory);
    std::string error;

    const fs::path monoSdk = workspace / "staged" / "mono";
    if (!MonoSdkGenerator::Generate(monoSdk.string(), "", &error)) {
        std::printf("FAILED: Mono SDK staging: %s\n", error.c_str());
        return false;
    }
    const fs::path monoProject = workspace / "mono" / "project";
    if (!MonoSdkGenerator::GenerateModProject(monoProject.string(), monoSdk.string(), URK_TEST_SDK_DIR, "SmokeMono",
                                              gameDirectory.string(), "Mods", true, &error)) {
        std::printf("FAILED: Mono project generation: %s\n", error.c_str());
        return false;
    }
    projects->push_back({monoProject, "mono"});

    const fs::path il2cppSdk = workspace / "staged" / "il2cpp";
    if (!Il2CppSdkGenerator::Generate(il2cppSdk.string(), "", &error)) {
        std::printf("FAILED: IL2CPP SDK staging: %s\n", error.c_str());
        return false;
    }
    const fs::path il2cppProject = workspace / "il2cpp" / "project";
    if (!Il2CppSdkGenerator::GenerateModProject(il2cppProject.string(), il2cppSdk.string(), URK_TEST_SDK_DIR,
                                                "SmokeIl2Cpp", gameDirectory.string(), "Mods", true, &error)) {
        std::printf("FAILED: IL2CPP project generation: %s\n", error.c_str());
        return false;
    }
    projects->push_back({il2cppProject, "il2cpp"});

    const std::string typeDump = UnrealSdkGenerator::TypeDumpPath(gameDirectory.string());
    fs::create_directories(gameDirectory);
    Write(typeDump, UnrealTypeDump());
    const fs::path unrealSdk = workspace / "staged" / "unreal";
    if (!UnrealSdkGenerator::Generate(unrealSdk.string(), "", typeDump, &error)) {
        std::printf("FAILED: Unreal SDK staging: %s\n", error.c_str());
        return false;
    }
    const fs::path unrealProject = workspace / "unreal" / "project";
    if (!UnrealSdkGenerator::GenerateModProject(unrealProject.string(), unrealSdk.string(), URK_TEST_SDK_DIR,
                                                "SmokeUnreal", gameDirectory.string(), "Mods", true, &error)) {
        std::printf("FAILED: Unreal project generation: %s\n", error.c_str());
        return false;
    }
    projects->push_back({unrealProject, "unreal"});
    return true;
}

std::string ReadText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Setters taking a generic Object must use CallExact with the declared type.
void CheckObjectSettersAreExact(const GeneratedProject &project) {
    const std::string text = ReadText(project.root / "sdk" / "unity" / "unity_components.h");
    Check(!text.empty(), project.label + ": unity_components.h is readable");

    std::size_t offset = 0;
    int checked = 0;
    bool clean = true;
    const std::string_view needle = "(Object value) const {";
    while ((offset = text.find(needle, offset)) != std::string::npos) {
        const std::size_t lineStart = text.rfind('\n', offset) + 1;
        const std::size_t bodyEnd = text.find('}', offset);
        const std::string declaration = text.substr(lineStart, offset - lineStart);
        if (declaration.find("void set_") == std::string::npos) {
            offset += needle.size();
            continue;
        }
        ++checked;
        const std::string body = text.substr(offset, bodyEnd - offset);
        if (body.find("CallExact<void>") == std::string::npos) {
            std::printf("  generic-Object setter without CallExact: %s\n", declaration.c_str());
            clean = false;
        }
        offset += needle.size();
    }
    Check(checked > 0, project.label + ": generic-Object property setters are present");
    Check(clean, project.label + ": generic-Object property setters dispatch via CallExact");
}

void CheckLayout(const GeneratedProject &project) {
    const char *const required[] = {
        "CMakeLists.txt",
        "CMakePresets.json",
        ".urk/project.ini",
        ".urk/generated-files.ini",
        "sdk/mod_sdk.h",
        "sdk/runtime_api.h",
        "sdk/unity/unity.h",
        "sdk/unity/unity_types.h",
        "sdk/unity/unity_invoke.h",
        "sdk/unity/unity_components.h",
        "sdk/unity/unity_shortcuts.h",
        "mod/config/mod_config.h",
        "mod/hooks/render_imgui_hook.cpp",
        "mod/hooks/win32_viewport_policy.cpp",
    };
    for (const char *relative : required) {
        std::error_code code;
        const fs::path path = project.root / relative;
        const bool present = fs::is_regular_file(path, code) && fs::file_size(path, code) > 0 && !code;
        Check(present, project.label + ": " + relative + " is generated and non-empty");
    }
}

// Every Unreal adapter entry plus the runtime source that includes it.
constexpr std::string_view kUnrealProbeSource = R"PROBE(
#include "sdk/unreal/unreal_runtime.h"
#include "sdk/unreal/types/Game/Doors/BP_Door_C.h"
#include "sdk/unreal/types/Engine/Pawn.h"
#include "sdk/unreal/types/PluginB/Settings_PluginB.h"
#include "sdk/unreal/types/Engine/VectorNet.h"
#include "mod/config/mod_config.h"
#include "mod/lifecycle/mod_runtime.cpp"

void urk_probe_unreal_types() {
    namespace t = URK::unreal::types;
    const t::Pawn pawn = t::Pawn::cast(URK::unreal::find("Pawn_0"));
    const t::Actor owner = pawn.Owner().get();
    (void)pawn.Owner().set(owner);
    (void)pawn.Owner().set(pawn);
    (void)pawn.Owner().set(nullptr);
    const t::Pawn instigator = pawn.Instigator().get();
    (void)instigator.class_().set(pawn.class_().get().value_or(0) + 1);
    (void)pawn.Weights(2).get();
    (void)pawn.Tag().get();
    (void)pawn.Label().get();
    float radius = 0;
    (void)pawn.GetBounds(&radius);
    const t::Actor controller = pawn.GetController();
    (void)t::Pawn::MakeOne(1, controller);
    for (const t::BP_Door_C &door : t::BP_Door_C::instances())
        (void)door.Open().set(true);
    (void)t::Settings_PluginB::class_default().valid();
    (void)t::BP_Door_C::load_class().valid();
    (void)URK::unreal::load_class("/Game/Doors/BP_Door");
    (void)URK::unreal::load_object(std::string("/Game/Items/DA_Stick.DA_Stick"));
    const std::optional<t::Vector> spot = pawn.Spot().get();
    (void)pawn.Spot().set(t::Vector{1, 2, 3});
    (void)pawn.Teleport(spot.value_or(t::Vector{}));
    (void)pawn.GetSpot();
    t::HitLike hit{};
    if (pawn.Sweep(&hit).value_or(false) && hit.bHit()) {
        hit.set_bStart(true);
        const t::Actor struck = hit.Actor.get();
        hit.Actor.set(struck);
        (void)hit.Location.X;
    }
    (void)t::VectorNet{}.Z;
}

namespace {
struct Vec {
    double x, y, z;
};
int Observer(void *, URK_UnrealObject, URK_UnrealObject, void *) { return 1; }
void Work(void *) {}
} // namespace

void urk_probe_unreal() {
    using namespace URK::unreal;
    if (!available())
        return;
    const EngineVersion version = engine_version();
    (void)version.major;
    const Object klass = find("PlayerController", "/Script/Engine");
    const Object player = find("PlayerController_0");
    for (const Object &actor : instances_of(klass, true))
        (void)actor.name();
    (void)player.is_a(klass);
    (void)player.klass().is_child_of(klass);
    (void)player.outer().default_object();
    if (const auto info = player.describe("bShowMouseCursor"))
        (void)info->kind;
    (void)player.get_int("Count").value_or(0);
    (void)player.get_float("InputYawScale").value_or(0.0);
    (void)player.get_bool("bShowMouseCursor").value_or(false);
    (void)player.get_object("Pawn");
    (void)player.get_name("StateName");
    (void)player.get_string("PlayerName");
    (void)player.set_int("Count", 1);
    (void)player.set_float("InputYawScale", 2.5);
    (void)player.set_bool("bShowMouseCursor", true);
    (void)player.set_object("Pawn", Object());
    CallFrame frame(klass.function("ClientMessage"));
    (void)frame.set("Location", Vec{1, 2, 3});
    (void)frame.get<Vec>("ReturnValue");
    (void)call(player, frame);
    (void)install_process_event_hook();
    (void)process_event_hook_installed();
    (void)remove_process_event_hook();
    observe_process_event(&Observer);
    (void)game_thread_id();
    (void)post_to_game_thread(&Work);
}
)PROBE";

void CheckUnrealLayout(const GeneratedProject &project) {
    const char *const required[] = {
        "CMakeLists.txt",
        "sdk/mod_sdk.h",
        "sdk/runtime_api.h",
        "sdk/unreal/unreal_runtime.h",
        "mod/config/mod_config.h",
        "mod/ui/highlight.h",
        "mod/hooks/render_imgui_hook.cpp",
    };
    for (const char *relative : required) {
        std::error_code code;
        const fs::path path = project.root / relative;
        const bool present = fs::is_regular_file(path, code) && fs::file_size(path, code) > 0 && !code;
        Check(present, project.label + ": " + relative + " is generated and non-empty");
    }
    std::error_code code;
    Check(!fs::exists(project.root / "sdk/unity", code), project.label + ": no Unity SDK folder");
    Check(!fs::exists(project.root / "mod/hooks/unity_log_hook.h", code), project.label + ": no Unity log hook");
    Check(ReadText(project.root / "mod/hooks/mod_hooks.cpp").find("Unity") == std::string::npos,
          project.label + ": mod_hooks.cpp does not reference Unity");
    Check(ReadText(project.root / ".urk/project.ini").find("unreal") != std::string::npos,
          project.label + ": manifest records the Unreal backend");
    // Wrong version macro compiles but fails at load.
    const std::string lifecycle = ReadText(project.root / "mod/generated/mod_lifecycle.cpp");
    Check(lifecycle.find("URK_UNREAL_API_VERSION") != std::string::npos &&
              lifecycle.find("IL2CPP") == std::string::npos && lifecycle.find("MONO") == std::string::npos,
          project.label + ": mod_lifecycle.cpp validates the Unreal API table");
}

// Each backend's project must not expose the other's helpers.
void CheckBackendSurface(const GeneratedProject &project, bool unreal) {
    std::vector<std::string_view> forbidden = {"//@unity", "//@unreal"};
    if (unreal) {
        forbidden.insert(forbidden.end(), {"input_get_", "graphics_device_type", "cursor_state_set", "has_input",
                                           "OnObjectDestroyRequested", "on_object_destroy_requested",
                                           "has_mono_api", "has_il2cpp_api", "runtime_backend_mono"});
    } else {
        forbidden.insert(forbidden.end(), {"has_unreal_api", "runtime_cap_unreal_api", "runtime_backend_unreal"});
    }
    std::error_code code;
    for (fs::recursive_directory_iterator it(project.root, code), end; it != end; it.increment(code)) {
        const fs::path relative = fs::relative(it->path(), project.root, code);
        const std::string name = relative.generic_string();
        if (!it->is_regular_file(code) || name == "sdk/mod_sdk.h" || name.starts_with("third_party/") ||
            name.starts_with("out/"))
            continue;
        const std::string extension = it->path().extension().string();
        if (extension != ".h" && extension != ".cpp")
            continue;
        const std::string text = ReadText(it->path());
        for (const std::string_view token : forbidden)
            Check(text.find(token) == std::string::npos,
                  project.label + ": " + name + " does not contain " + std::string(token));
    }
}

} // namespace

int main(int argc, char **argv) {
    fs::path workspace;
    bool keep = false;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--keep" && index + 1 < argc) {
            workspace = fs::path(argv[++index]);
            keep = true;
        }
    }
    std::error_code cleanup;
    if (workspace.empty())
        workspace = fs::temp_directory_path() / "urk_generated_project_tests";
    fs::remove_all(workspace, cleanup);
    fs::create_directories(workspace);

    std::vector<GeneratedProject> projects;
    if (!GenerateBoth(workspace, &projects))
        return 1;

    for (const GeneratedProject &project : projects) {
        CheckBackendSurface(project, project.label == "unreal");
        if (project.label == "unreal") {
            CheckUnrealLayout(project);
            CheckUnrealTypes(project);
            const fs::path probe = project.root / "urk_probe_unreal.cpp";
            Write(probe, kUnrealProbeSource);
            Check(SyntaxCheck(project.root, probe, project.root),
                  project.label + ": generated Unreal SDK and mod runtime compile");
            fs::remove(probe, cleanup);
            continue;
        }

        CheckLayout(project);
        CheckObjectSettersAreExact(project);

        const fs::path probe = project.root / "urk_probe_unity.cpp";
        Write(probe, kProbeSource);
        Check(SyntaxCheck(project.root, probe, project.root),
              project.label + ": generated Unity SDK compiles against real call sites");
        fs::remove(probe, cleanup);

        if (project.label == "il2cpp") {
            const fs::path helperProbe = project.root / "urk_probe_il2cpp_helpers.cpp";
            Write(helperProbe, kIl2CppHelperProbeSource);
            Check(SyntaxCheck(project.root, helperProbe, project.root),
                  project.label + ": generated IL2CPP helpers compile");
            fs::remove(helperProbe, cleanup);
        }
    }

    if (!keep)
        fs::remove_all(workspace, cleanup);
    else
        std::printf("\nGenerated projects kept in %s\n", workspace.string().c_str());

    if (g_failures) {
        std::printf("\n%d FAILURE(S)\n", g_failures);
        return 1;
    }
    std::printf("\nALL PASS (0 failures)\n");
    return 0;
}
