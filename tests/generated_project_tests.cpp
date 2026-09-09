// Generates a Mono and an IL2CPP mod project straight from the generator
// library, then compiles a probe translation unit against the result.
//
// The SDK templates are raw string literals, so nothing in the URKit build
// itself type-checks them; a syntax error or a broken API reaches users only
// when they build a generated project. The probe instantiates the templates that
// callers actually reach, because a header-only parse would not look inside an
// uninstantiated template.
//
// No Unity game is required: the generator only records the game directory in
// the manifest.

#include "src/sdk/il2cpp_sdk_generator.h"
#include "src/sdk/mono_sdk_generator.h"

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

// Reaches the version-sensitive template paths: both spellings of the object
// finders, unqualified type resolution, component access, and the IL2CPP helper
// surface. Nothing here runs; instantiation is the point.
constexpr std::string_view kProbeSource = R"PROBE(
#include "sdk/unity/unity.h"

#include <string>
#include <vector>

namespace {

void probe_object_finders() {
    // Both names must instantiate: Unity renamed this API in 2022.2, and the
    // generated SDK falls back across the rename in both directions.
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
    // Managed stripping removes UnityEngine members the game never calls, so
    // presence has to be answerable without invoking anything.
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
    return true;
}

std::string ReadText(const fs::path &path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A setter whose C++ parameter is the generic `Object` wrapper cannot be
// dispatched by inferred type: inference yields "UnityEngine.Object" while the
// property's declared C# type is something narrower (AudioClip, Font, ...), so
// the exact-overload lookup never matches and the call silently never happens.
// Such setters must name the declared type through CallExact.
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
