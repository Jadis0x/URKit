#include "mod_project_generator_common.h"
#include "embedded_mod_sdk.h"
#include "embedded_dev_test.h"
#include "mod_sdk.h"
#include "project_ledger.h"
#include "project_manifest.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

namespace ModProjectGenerator {
namespace {

namespace fs = std::filesystem;

bool IsCppKeyword(const std::string &value) {
    static const char *keywords[] = {
        "alignas",       "alignof",     "and",        "and_eq",   "asm",       "auto",
        "bitand",        "bitor",       "bool",       "break",    "case",      "catch",
        "char",          "class",       "compl",      "concept",  "const",     "consteval",
        "constexpr",     "constinit",   "const_cast", "continue", "co_await",  "co_return",
        "co_yield",      "decltype",    "default",    "delete",   "do",        "double",
        "dynamic_cast",  "else",        "enum",       "explicit", "export",    "extern",
        "false",         "float",       "for",        "friend",   "goto",      "if",
        "inline",        "int",         "long",       "mutable",  "namespace", "new",
        "noexcept",      "not",         "not_eq",     "nullptr",  "operator",  "or",
        "or_eq",         "private",     "protected",  "public",   "register",  "reinterpret_cast",
        "requires",      "return",      "short",      "signed",   "sizeof",    "static",
        "static_assert", "static_cast", "struct",     "switch",   "template",  "this",
        "thread_local",  "throw",       "true",       "try",      "typedef",   "typeid",
        "typename",      "union",       "unsigned",   "using",    "virtual",   "void",
        "volatile",      "wchar_t",     "while",      "xor",      "xor_eq"};
    for (const char *keyword : keywords)
        if (value == keyword)
            return true;
    return false;
}

std::string EscapeString(const std::string &text) {
    std::ostringstream out;
    for (char ch : text) {
        switch (ch) {
            case '\\':
                out << "\\\\";
                break;
            case '"':
                out << "\\\"";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

bool IsValidModId(const std::string &value) {
    if (value.empty())
        return false;
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char ch) { return std::isalnum(ch) || ch == '_' || ch == '-'; });
}

std::string ReadExistingModId(const fs::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    std::ostringstream source;
    source << input.rdbuf();
    const std::string text = source.str();
    const std::string marker = "mod_id";
    const size_t name = text.find(marker);
    if (name == std::string::npos)
        return {};
    const size_t equals = text.find('=', name + marker.size());
    if (equals == std::string::npos)
        return {};
    const size_t first_quote = text.find('"', equals + 1);
    if (first_quote == std::string::npos)
        return {};
    const size_t second_quote = text.find('"', first_quote + 1);
    if (second_quote == std::string::npos)
        return {};
    const std::string value = text.substr(first_quote + 1, second_quote - first_quote - 1);
    return IsValidModId(value) ? value : std::string{};
}

std::string GenerateModId(const std::string &project_name) {
    std::random_device device;
    const std::uint32_t suffix = (static_cast<std::uint32_t>(device()) << 16) ^ static_cast<std::uint32_t>(device());
    std::ostringstream out;
    out << Identifier(project_name, "GeneratedMod") << '_' << std::hex << std::nouppercase << std::setw(8)
        << std::setfill('0') << suffix;
    return out.str();
}

std::string ResolveModId(const ModuleProjectOptions &options) {
    if (const std::string existing = ReadExistingModId(options.projectRoot / "mod/config/mod_config.h");
        !existing.empty()) {
        return existing;
    }
    return IsValidModId(options.modId) ? options.modId : GenerateModId(options.projectName);
}

#include "templates/mod_project_generator_unity.inl"
#include "templates/mod_project_generator_runtime.inl"
#include "templates/mod_project_generator_ui.inl"

std::string CMakeLists(const ModuleProjectOptions &options, const std::vector<fs::path> &sourceFiles,
                       const std::vector<fs::path> &moduleFiles) {
    std::ostringstream out;
    out << "cmake_minimum_required(VERSION 3.28)\n\n"
        << "if(POLICY CMP0091)\n"
        << "    cmake_policy(SET CMP0091 NEW)\n"
        << "endif()\n\n"
        << "set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL \"Generated mods keep dependencies linked into the mod DLL when "
           "possible\" FORCE)\n\n"
        << "project(" << options.projectName << " LANGUAGES CXX)\n\n"
        << "set(CMAKE_CXX_STANDARD 20)\n"
        << "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        << "set(CMAKE_CXX_EXTENSIONS OFF)\n"
        << "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)\n"
        << "set(CMAKE_CXX_SCAN_FOR_MODULES OFF)\n\n"
        << "if(WIN32)\n"
        << "    set(CMAKE_MSVC_RUNTIME_LIBRARY \"MultiThreaded$<$<CONFIG:Debug>:Debug>\" CACHE STRING \"Use the static Microsoft C/C++ runtime\" FORCE)\n"
        << "endif()\n\n"
        << "if(NOT WIN32)\n"
        << "    message(FATAL_ERROR \"Generated URKit starter mods target Windows native Unity processes.\")\n"
        << "endif()\n"
        << "if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)\n"
        << "    message(FATAL_ERROR \"Generated URKit starter mods support x64 targets only.\")\n"
        << "endif()\n\n"
        << "set(BUILD_SHARED_LIBS OFF CACHE BOOL \"Build third-party dependencies as shared libraries\" FORCE)\n\n"
        << "function(urk_disable_vs_vcpkg_integration target_name)\n"
        << "    if(MSVC)\n"
        << "        set_target_properties(${target_name} PROPERTIES VS_GLOBAL_VcpkgEnabled \"false\")\n"
        << "    endif()\n"
        << "endfunction()\n\n"
        << "include(FetchContent)\n"
        << "FetchContent_Declare(imgui\n"
        << "    GIT_REPOSITORY https://github.com/ocornut/imgui.git\n"
        << "    GIT_TAG v1.92.9b-docking\n"
        << ")\n"
        << "FetchContent_MakeAvailable(imgui)\n"
        << "add_library(imgui STATIC\n"
        << "    ${imgui_SOURCE_DIR}/imgui.cpp\n"
        << "    ${imgui_SOURCE_DIR}/imgui_draw.cpp\n"
        << "    ${imgui_SOURCE_DIR}/imgui_tables.cpp\n"
        << "    ${imgui_SOURCE_DIR}/imgui_widgets.cpp\n"
        << "    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp\n"
        << "    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp\n"
        << "    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp\n"
        << "    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp\n"
        << ")\n"
        << "target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)\n"
        << "set_target_properties(imgui PROPERTIES CXX_SCAN_FOR_MODULES OFF)\n"
        << "if(MSVC)\n"
        << "    target_compile_options(imgui PRIVATE /W0 /permissive- /EHsc /utf-8)\n"
        << "endif()\n"
        << "urk_disable_vs_vcpkg_integration(imgui)\n\n"
        << "add_library(" << options.projectName << " SHARED)\n"
        << "set_target_properties(" << options.projectName << " PROPERTIES CXX_SCAN_FOR_MODULES OFF)\n\n";
    out << "target_include_directories(" << options.projectName
        << " PRIVATE . mod";
    for (const auto &directory : options.includeDirectories)
        out << " " << directory;
    out << ")\n\n";
    out << "set(URK_PROJECT_SOURCES\n";
    for (const auto &file : sourceFiles)
        out << "        " << file.generic_string() << "\n";
    for (const auto &file : moduleFiles)
        out << "        " << file.generic_string() << "\n";
    out << ")\n\n"
        << "# User modules under mod are picked up automatically after CMake reconfigures.\n"
        << "file(GLOB_RECURSE URK_USER_MODULE_FILES CONFIGURE_DEPENDS\n"
        << "    RELATIVE \"${CMAKE_CURRENT_SOURCE_DIR}\"\n"
        << "    \"${CMAKE_CURRENT_SOURCE_DIR}/mod/*.h\"\n"
        << "    \"${CMAKE_CURRENT_SOURCE_DIR}/mod/*.hpp\"\n"
        << "    \"${CMAKE_CURRENT_SOURCE_DIR}/mod/*.cpp\"\n"
        << "    \"${CMAKE_CURRENT_SOURCE_DIR}/mod/*.cc\"\n"
        << "    \"${CMAKE_CURRENT_SOURCE_DIR}/mod/*.cxx\"\n"
        << ")\n\n"
        << "list(REMOVE_ITEM URK_USER_MODULE_FILES ${URK_PROJECT_SOURCES})\n\n"
        << "target_sources(" << options.projectName << "\n"
        << "    PRIVATE\n"
        << "        ${URK_PROJECT_SOURCES}\n"
        << "        ${URK_USER_MODULE_FILES}\n"
        << ")\n\n"
        << "source_group(TREE \"${CMAKE_CURRENT_SOURCE_DIR}\" FILES\n"
        << "    ${URK_PROJECT_SOURCES}\n"
        << "    ${URK_USER_MODULE_FILES}\n"
        << ")\n\n"
        << "target_compile_features(" << options.projectName << " PRIVATE cxx_std_20)\n"
        << "target_compile_definitions(" << options.projectName << " PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)\n"
        << "target_link_libraries(" << options.projectName << " PRIVATE imgui d3d11 d3d12 dxgi opengl32)\n"
        << "if(MSVC)\n"
        << "    target_compile_options(" << options.projectName << " PRIVATE /W4 /permissive- /EHsc /utf-8 /bigobj)\n"
        << "    target_link_options(" << options.projectName << " PRIVATE\n"
        << "        \"$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>\"\n"
        << "        \"$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>\"\n"
        << "        \"$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>\"\n"
        << "    )\n"
        << "endif()\n"
        << "urk_disable_vs_vcpkg_integration(" << options.projectName << ")\n\n"
        << "set(URK_DEPLOY_DIR \"" << EscapeString(options.deployDirectory)
        << "\" CACHE PATH \"Directory where the built mod DLL will be copied\")\n"
        << "if(URK_DEPLOY_DIR)\n"
        << "    add_custom_command(TARGET " << options.projectName << " POST_BUILD\n"
        << "        COMMAND ${CMAKE_COMMAND} -E make_directory "
           "\"${URK_DEPLOY_DIR}\"\n"
        << "        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:" << options.projectName
        << "> \"${URK_DEPLOY_DIR}\"\n";
    if (options.enableLocalization) {
        out << "        COMMAND ${CMAKE_COMMAND} -E make_directory \"${URK_DEPLOY_DIR}/locales\"\n"
            << "        COMMAND ${CMAKE_COMMAND} -E copy_directory \"${CMAKE_CURRENT_SOURCE_DIR}/locales\" "
               "\"${URK_DEPLOY_DIR}/locales\"\n";
    }
    out << "    )\n"
        << "endif()\n";
    return out.str();
}

std::string CMakePresets() {
    return R"URK({
  "version": 3,
  "configurePresets": [
    {
      "name": "clang-debug",
      "displayName": "Clang x64 Debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_COMPILER": "clang++",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "clang-release",
      "displayName": "Clang x64 Release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_COMPILER": "clang++",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "msvc-debug",
      "displayName": "MSVC x64 Debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_CXX_COMPILER": "cl",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "msvc-release",
      "displayName": "MSVC x64 Release",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/out/build/${presetName}",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Release",
        "CMAKE_CXX_COMPILER": "cl",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    }
  ],
  "buildPresets": [
    {
      "name": "clang-debug",
      "configurePreset": "clang-debug"
    },
    {
      "name": "clang-release",
      "configurePreset": "clang-release"
    },
    {
      "name": "msvc-debug",
      "configurePreset": "msvc-debug"
    },
    {
      "name": "msvc-release",
      "configurePreset": "msvc-release"
    }
  ]
}
)URK";
}

std::string VsCodeCppProperties() {
    return R"URK({
  "version": 4,
  "configurations": [
    {
      "name": "Win32-Clang",
      "compilerPath": "C:/Program Files/LLVM/bin/clang++.exe",
      "intelliSenseMode": "windows-clang-x64",
      "cppStandard": "c++20",
      "compileCommands": "${workspaceFolder}/out/build/clang-debug/compile_commands.json",
      "mergeConfigurations": true
    },
    {
      "name": "Win32-MSVC",
      "compilerPath": "cl.exe",
      "intelliSenseMode": "windows-msvc-x64",
      "cppStandard": "c++20",
      "compileCommands": "${workspaceFolder}/out/build/msvc-debug/compile_commands.json",
      "mergeConfigurations": true
    }
  ]
}
)URK";
}

std::string Readme(const ModuleProjectOptions &options) {
    std::ostringstream out;
    out << "# " << options.projectName << "\n\n"
        << "Generated URKit " << options.backendDisplayName << " mod project for Windows x64. The output DLL is a "
           "URKit loader plugin, not a standalone injectable DLL.\n\n"
        << "## Build\n\n"
        << "Use Clang from PowerShell or MSVC from an x64 Visual Studio Developer PowerShell.\n\n"
        << "```powershell\n"
        << "cmake --preset clang-debug\n"
        << "cmake --build --preset clang-debug --parallel\n"
        << "# Or, from an x64 Visual Studio Developer PowerShell:\n"
        << "cmake --preset msvc-debug\n"
        << "cmake --build --preset msvc-debug --parallel\n"
        << "```\n\n"
        << "Use `clang-release` or `msvc-release` for release builds. Builds deploy to the selected game's "
           "`Mods` directory.\n\n"
        << "## Documentation\n\n"
        << "See the [URKit SDK Handbook]"
           "(https://github.com/Jadis0x/URKit/blob/main/docs/SDK_HANDBOOK.md) for the complete "
           "GameObject/component API, Unity access, threading, lifecycle, hooks, UI, highlight rendering, "
           "and unload rules. The highlight chapter explains exactly how generated overlays are "
           "submitted through DX11, DX12, or OpenGL.\n\n"
        << "Include `sdk/unity/unity.h` for Unity work. Resolve the target object and component, then access the "
           "required member. Keep Unity calls on the main thread and check `Unity::last_error()` after an "
           "unexpected empty result.\n\n"
        << "## Project files\n\n"
        << "- `mod/lifecycle/mod_runtime.cpp`: game/runtime and main-thread work.\n"
        << "- `mod/hooks/mod_hooks.cpp`: exact, validated hook installation.\n"
        << "- `mod/lifecycle/mod_network.cpp`: HTTPS setup and policy.\n"
        << "- `mod/support/mod_log.cpp`: shared logging.\n"
        << "- `mod/config/mod_config.h` and `mod/ui/theme.h`: metadata and styling.\n\n"
        << "Files under `sdk/`, `mod/generated/`, native hook support, and the build profiles are refreshed by "
           "the generator. Files under `mod/ui/`, other user-owned files, and new sources under `mod/` are "
           "preserved.\n\n"
        << "## MCP runtime tests\n\n"
        << "Generated projects include `sdk/dev_test.h`. Export `URK_DevTestCount`, `URK_DevTestDescribe`, and "
           "`URK_DevTestRun` from a user-owned source under `mod/` to make runtime tests discoverable through "
           "`URKitDevBridge.dll`. The development MCP guide is available in the URKit release documentation.\n\n"
        << "## Runtime constraints\n\n"
        << "- `sdk/mod_sdk.h` is the ABI source of truth. Check version, size, backend, capability, and function "
           "pointers before use.\n"
        << "- Include `sdk/unity/unity.h` for normal Unity work. Use exact overload helpers when required and keep "
           "Unity calls on the main thread.\n"
        << "- Resolve stable metadata once instead of repeating lookups in update/render callbacks.\n"
        << "- Detach hooks, callbacks, coroutines, workers, and UI before unload.\n"
        << "- DX11, DX12, and OpenGL overlays are supported; Vulkan is not.\n";
    for (const std::string &line : options.readmeExtraLayoutLines)
        out << "- " << line << "\n";
    if (options.enableLocalization)
        out << "- Locale JSON files under `locales/` are preserved and deployed beside the DLL.\n";
    out << "\nStart troubleshooting with `URKit_logs.log` beside the game executable.\n";
    return out.str();
}

struct PlannedWrite {
    fs::path relativePath;
    OutputFilePolicy policy;
    std::string content;
    bool required = true;
    bool moduleFile = false;
};

OutputFileSpec OutputSpecForWrite(const PlannedWrite &write) {
    return {write.relativePath, write.policy, write.required, write.moduleFile, true};
}

bool WriteIfMissing(const fs::path &path, const std::string &text, std::string *error) {
    std::error_code ec;
    if (fs::exists(path, ec) && !ec)
        return true;
    return WriteText(path, text, error);
}

bool WriteEditablePreserve(const ModuleProjectOptions &options, const PlannedWrite &write, std::string *error) {
    const fs::path destination = options.projectRoot / write.relativePath;
    std::error_code ec;
    const bool exists = fs::exists(destination, ec);
    if (ec) {
        if (error)
            *error = "cannot inspect editable file " + destination.string() + ": " + ec.message();
        return false;
    }
    if (exists)
        return true;

    return WriteText(destination, write.content, error);
}

bool WriteByPolicy(const ModuleProjectOptions &options, const PlannedWrite &write, std::string *error) {
    const fs::path destination = options.projectRoot / write.relativePath;
    switch (write.policy) {
        case OutputFilePolicy::GeneratedOverwrite:
            return WriteText(destination, write.content, error);
        case OutputFilePolicy::EditablePreserve:
            return options.preserveEditableSources ? WriteEditablePreserve(options, write, error)
                                                   : WriteText(destination, write.content, error);
        case OutputFilePolicy::DocumentationPreserve:
            return WriteIfMissing(destination, write.content, error);
    }
    if (error)
        *error = "unknown output file policy for " + write.relativePath.generic_string();
    return false;
}

bool IsRegularNonEmpty(const fs::path &path, std::string *error, const char *prefix) {
    std::error_code ec;
    const bool regular = fs::is_regular_file(path, ec);
    const auto size = regular && !ec ? fs::file_size(path, ec) : 0;
    if (!regular || ec || size == 0) {
        if (error)
            *error = std::string(prefix) + ": " + path.string();
        return false;
    }
    return true;
}

void AppendExternalOutputSpecs(const ModuleProjectOptions &options, std::vector<OutputFileSpec> &specs) {
    for (const auto &moduleFile : options.backendModuleFiles) {
        specs.push_back({moduleFile, OutputFilePolicy::GeneratedOverwrite, true, true, false});
    }
    for (const auto &moduleFile : options.extraModuleFiles) {
        specs.push_back({moduleFile, OutputFilePolicy::EditablePreserve, true, true, false});
    }
    for (const auto &extra : options.extraOutputFiles) {
        if (!extra.writtenByCommonGenerator)
            specs.push_back(extra);
    }
}

std::vector<fs::path> CollectModuleFiles(const std::vector<OutputFileSpec> &specs) {
    std::vector<fs::path> moduleFiles;
    for (const auto &file : specs) {
        if (file.moduleFile)
            moduleFiles.push_back(file.relativePath);
    }
    return moduleFiles;
}

bool IsCppSourceFile(const fs::path &path) {
    const std::string extension = path.extension().string();
    return extension == ".cpp" || extension == ".cxx" || extension == ".cc";
}

std::vector<fs::path> CollectSourceFiles(const std::vector<OutputFileSpec> &specs) {
    std::vector<fs::path> sourceFiles;
    for (const auto &file : specs) {
        if (!file.moduleFile && IsCppSourceFile(file.relativePath))
            sourceFiles.push_back(file.relativePath);
    }
    return sourceFiles;
}

bool ValidateOutputSpecs(const fs::path &root, const std::vector<OutputFileSpec> &specs, std::string *error) {
    for (const auto &file : specs) {
        if (!file.required)
            continue;
        if (!IsRegularNonEmpty(root / file.relativePath, error, "missing generated project file"))
            return false;
    }
    return true;
}

bool ReadTextFile(const fs::path &path, std::string &text, std::string *error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "cannot read " + path.string();
        return false;
    }
    std::ostringstream out;
    out << input.rdbuf();
    text = out.str();
    if (text.empty()) {
        if (error)
            *error = "empty file: " + path.string();
        return false;
    }
    return true;
}

bool ReadSdkHeader(const ModuleProjectOptions &options, std::string &text, std::string *error) {
    if (!options.sdkHeaderPath.empty())
        return ReadTextFile(options.sdkHeaderPath, text, error);

    text.assign(kEmbeddedModSdkHeader.begin(), kEmbeddedModSdkHeader.end());
    if (text.empty()) {
        if (error)
            *error = "embedded canonical sdk/mod_sdk.h is empty";
        return false;
    }
    return true;
}

} // namespace

std::string Identifier(const std::string &text, const char *fallback) {
    std::string out;
    for (unsigned char ch : text) {
        if (std::isalnum(ch) || ch == '_')
            out.push_back(static_cast<char>(ch));
        else if (!out.empty() && out.back() != '_')
            out.push_back('_');
    }
    while (!out.empty() && out.front() == '_')
        out.erase(out.begin());
    while (!out.empty() && out.back() == '_')
        out.pop_back();
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out.front())) || IsCppKeyword(out))
        out = fallback && *fallback ? fallback : "GeneratedMod";
    return out;
}

bool WriteText(const fs::path &path, const std::string &text, std::string *error) {
    std::error_code ec;
    if (const auto parent = path.parent_path(); !parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            if (error)
                *error = "cannot create " + parent.string() + ": " + ec.message();
            return false;
        }
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    if (!output) {
        if (error)
            *error = "cannot write " + path.string();
        return false;
    }
    return true;
}

bool WriteModuleProject(const ModuleProjectOptions &options, std::string *error) {
    ModuleProjectOptions resolved_options = options;
    resolved_options.modId = ResolveModId(options);
    const ModuleProjectOptions &project = resolved_options;
    std::string sdkHeader;
    if (!ReadSdkHeader(project, sdkHeader, error))
        return false;
    std::string devTestHeader(kEmbeddedDevTestHeader.begin(), kEmbeddedDevTestHeader.end());
    if (devTestHeader.empty()) {
        if (error)
            *error = "embedded canonical sdk/dev_test.h is empty";
        return false;
    }
    const UnityModuleSet unityModules = BuildUnityModuleSet(project);
    std::vector<PlannedWrite> writes = {
        {"sdk/mod_sdk.h", OutputFilePolicy::GeneratedOverwrite, sdkHeader, true, false},
        {"sdk/dev_test.h", OutputFilePolicy::GeneratedOverwrite, devTestHeader, true, true},
        {"sdk/runtime_api.h", OutputFilePolicy::GeneratedOverwrite, ModSdkModule(), true, true},
        {"sdk/runtime_bootstrap.h", OutputFilePolicy::GeneratedOverwrite, RuntimeBootstrapModule(project), true, true},
        {"sdk/hook_api.h", OutputFilePolicy::GeneratedOverwrite, HooksRuntimeModule(), true, true},
        {"sdk/network_api.h", OutputFilePolicy::GeneratedOverwrite, NetworkRuntimeModule(), true, true},
        {"sdk/events.h", OutputFilePolicy::GeneratedOverwrite, EventsModule(), true, true},
        {"sdk/coroutines.h", OutputFilePolicy::GeneratedOverwrite, CoroutinesModule(), true, true},
        {"sdk/mod_async.h", OutputFilePolicy::GeneratedOverwrite, ModAsyncModule(), true, true},
        {"sdk/unity/unity.h", OutputFilePolicy::GeneratedOverwrite, unityModules.publicHeader, true, true},
        {"sdk/unity/unity_types.h", OutputFilePolicy::GeneratedOverwrite, unityModules.types, true, true},
        {"sdk/unity/unity_invoke.h", OutputFilePolicy::GeneratedOverwrite, unityModules.invoke, true, true},
        {"sdk/unity/unity_components.h", OutputFilePolicy::GeneratedOverwrite, unityModules.components, true, true},
        {"sdk/unity/unity_inspect.h", OutputFilePolicy::GeneratedOverwrite, unityModules.inspect, true, true},
        {"sdk/unity/unity_shortcuts.h", OutputFilePolicy::GeneratedOverwrite, unityModules.shortcuts, true, true},
        {"mod/config/mod_config.h", OutputFilePolicy::EditablePreserve, ConfigModule(project), true, false},
        {"mod/support/mod_log.h", OutputFilePolicy::EditablePreserve, ModLogHeader(), true, false},
        {"mod/support/mod_log.cpp", OutputFilePolicy::EditablePreserve, ModLogSource(), true, false},
        {"mod/hooks/mod_hooks.h", OutputFilePolicy::EditablePreserve, ModHooksHeader(), true, false},
        {"mod/hooks/mod_hooks.cpp", OutputFilePolicy::EditablePreserve, ModHooksSource(project), true, false},
        {"mod/lifecycle/mod_network.h", OutputFilePolicy::EditablePreserve, NetworkInitHeader(), true, false},
        {"mod/lifecycle/mod_network.cpp", OutputFilePolicy::EditablePreserve, NetworkInitSource(), true, false},
        {"mod/lifecycle/mod_runtime.h", OutputFilePolicy::EditablePreserve, GameRuntimeHeader(), true, false},
        {"mod/lifecycle/mod_runtime.cpp", OutputFilePolicy::EditablePreserve, GameRuntimeSource(project), true, false},
        {"mod/ui/theme.h", OutputFilePolicy::EditablePreserve, ThemeModule(), true, true},
        {"mod/ui/localization.h", OutputFilePolicy::EditablePreserve, LocalizationModule(), true, true},
        {"mod/ui/widgets.h", OutputFilePolicy::EditablePreserve, WidgetsModule(), true, true},
        {"mod/ui/tabs/about_tab.h", OutputFilePolicy::EditablePreserve, AboutTabModule(), true, true},
        {"mod/ui/tabs/config_tab.h", OutputFilePolicy::EditablePreserve, ConfigTabModule(), true, true},
        {"mod/ui/menu.h", OutputFilePolicy::EditablePreserve, UiModule(), true, true},
        {"mod/ui/highlight.h", OutputFilePolicy::EditablePreserve, HighlightModule(), true, true},
        {"mod/hooks/dx11_viewport_swap_chain.h", OutputFilePolicy::GeneratedOverwrite,
         Dx11ViewportSwapChainHeaderModule(), true, true},
        {"mod/hooks/dx11_viewport_swap_chain.cpp", OutputFilePolicy::GeneratedOverwrite,
         Dx11ViewportSwapChainSourceModule(), true, false},
        {"mod/hooks/dx11_state_guard.h", OutputFilePolicy::GeneratedOverwrite, Dx11StateGuardHeaderModule(), true,
         true},
        {"mod/hooks/dx11_state_guard.cpp", OutputFilePolicy::GeneratedOverwrite, Dx11StateGuardSourceModule(), true,
         false},
        {"mod/hooks/dx12_overlay_resources.h", OutputFilePolicy::GeneratedOverwrite,
         Dx12OverlayResourcesHeaderModule(), true, true},
        {"mod/hooks/dx12_overlay_resources.cpp", OutputFilePolicy::GeneratedOverwrite,
         Dx12OverlayResourcesSourceModule(), true, false},
        {"mod/hooks/dxgi_hook_discovery.h", OutputFilePolicy::GeneratedOverwrite,
         DxgiHookDiscoveryHeaderModule(), true, true},
        {"mod/hooks/dxgi_hook_discovery.cpp", OutputFilePolicy::GeneratedOverwrite,
         DxgiHookDiscoverySourceModule(), true, false},
        {"mod/hooks/win32_input_coordinates.h", OutputFilePolicy::GeneratedOverwrite,
         Win32InputCoordinatesHeaderModule(), true, true},
        {"mod/hooks/win32_input_coordinates.cpp", OutputFilePolicy::GeneratedOverwrite,
         Win32InputCoordinatesSourceModule(), true, false},
        {"mod/hooks/win32_message_pump.h", OutputFilePolicy::GeneratedOverwrite,
         Win32MessagePumpHeaderModule(), true, true},
        {"mod/hooks/win32_message_pump.cpp", OutputFilePolicy::GeneratedOverwrite,
         Win32MessagePumpSourceModule(), true, false},
        {"mod/hooks/win32_viewport_policy.h", OutputFilePolicy::GeneratedOverwrite,
         Win32ViewportPolicyHeaderModule(), true, true},
        {"mod/hooks/win32_viewport_policy.cpp", OutputFilePolicy::GeneratedOverwrite,
         Win32ViewportPolicySourceModule(), true, false},
        {"mod/hooks/render_imgui_hook.h", OutputFilePolicy::GeneratedOverwrite, RenderHookHeaderModule(), true, true},
        {"mod/hooks/render_imgui_hook.cpp", OutputFilePolicy::GeneratedOverwrite, RenderHookSourceModule(), true, false},
        {"mod/hooks/unity_log_hook.h", OutputFilePolicy::EditablePreserve, UnityLogHookModule(project), true, true},
        {"mod/generated/mod_lifecycle.h", OutputFilePolicy::GeneratedOverwrite, ModLifecycleHeader(), true, true},
        {"mod/generated/mod_lifecycle.cpp", OutputFilePolicy::GeneratedOverwrite, ModLifecycleSource(project), true,
         false},
        {"mod/generated/mod_entry.cpp", OutputFilePolicy::GeneratedOverwrite, ModEntrySource(), true, false},
        {"README.md", OutputFilePolicy::DocumentationPreserve, Readme(project), true, false},
        {".clangd", OutputFilePolicy::GeneratedOverwrite,
         "CompileFlags:\n  CompilationDatabase: out/build/clang-debug\n  Compiler: clang++\n\nIndex:\n  Background: Build\n",
         true, false},
        {"CMakePresets.json", OutputFilePolicy::GeneratedOverwrite, CMakePresets(), true, false},
        {".vscode/c_cpp_properties.json", OutputFilePolicy::GeneratedOverwrite, VsCodeCppProperties(), true, false},
    };
    if (project.enableLocalization) {
        writes.push_back({"locales/" + project.modId + "/en.json", OutputFilePolicy::EditablePreserve,
                          EnglishLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/tr.json", OutputFilePolicy::EditablePreserve,
                          TurkishLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/ja.json", OutputFilePolicy::EditablePreserve,
                          JapaneseLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/zh.json", OutputFilePolicy::EditablePreserve,
                          ChineseLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/ru.json", OutputFilePolicy::EditablePreserve,
                          RussianLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/uk.json", OutputFilePolicy::EditablePreserve,
                          UkrainianLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/es.json", OutputFilePolicy::EditablePreserve,
                          SpanishLocaleModule(), true, false});
        writes.push_back({"locales/" + project.modId + "/fr.json", OutputFilePolicy::EditablePreserve,
                          FrenchLocaleModule(), true, false});
    }
    for (const auto &extra : project.extraOutputFiles) {
        if (extra.writtenByCommonGenerator) {
            writes.push_back({extra.relativePath, extra.policy, {}, extra.required, extra.moduleFile});
        }
    }

    std::vector<OutputFileSpec> outputSpecs;
    outputSpecs.reserve(writes.size() + project.backendModuleFiles.size() + project.extraModuleFiles.size() +
                        project.extraOutputFiles.size() + 1);
    for (const auto &write : writes)
        outputSpecs.push_back(OutputSpecForWrite(write));
    AppendExternalOutputSpecs(project, outputSpecs);

    const std::vector<fs::path> sourceFiles = CollectSourceFiles(outputSpecs);
    const std::vector<fs::path> moduleFiles = CollectModuleFiles(outputSpecs);
    writes.push_back({"CMakeLists.txt", OutputFilePolicy::GeneratedOverwrite,
                      CMakeLists(project, sourceFiles, moduleFiles), true, false});
    outputSpecs.push_back(OutputSpecForWrite(writes.back()));

    for (const auto &write : writes)
        if (!WriteByPolicy(project, write, error))
            return false;

    if (!ValidateOutputSpecs(project.projectRoot, outputSpecs, error))
        return false;

    if (project.deployDirectory.empty())
        return true;

    UrkProject::Manifest manifest;
    manifest.sdkVersion = URK_SDK_VERSION;
    manifest.backend = project.backendNamespace == "URK::il2cpp" ? UrkProject::Backend::Il2Cpp : UrkProject::Backend::Mono;
    manifest.projectName = project.projectName;
    manifest.gameDirectory = std::filesystem::path(project.deployDirectory).parent_path();
    manifest.modsDirectory = std::filesystem::path(project.deployDirectory).filename().string();
    manifest.enableLocalization = project.enableLocalization;
    if (!UrkProject::WriteManifest(project.projectRoot, manifest, error))
        return false;

    std::vector<fs::path> generatedPaths;
    generatedPaths.reserve(outputSpecs.size());
    for (const OutputFileSpec &spec : outputSpecs)
        if (spec.policy == OutputFilePolicy::GeneratedOverwrite)
            generatedPaths.push_back(spec.relativePath);
    return UrkProject::WriteGeneratedFileLedger(project.projectRoot, URK_SDK_VERSION, generatedPaths, error);
}

} // namespace ModProjectGenerator
