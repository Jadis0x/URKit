#include "unreal_sdk_generator.h"
#include "mod_project_generator_common.h"
#include "mod_project_generator_profiles.h"
#include "sdk_generator_contract.h"
#include "unreal_type_codegen.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace mpg = ModProjectGenerator;
namespace sdk = SdkGenerator;

#include "templates/unreal/runtime.inl"

std::string UnrealSdkReadme(const std::string &details) {
    std::ostringstream out;
    out << "# URKit - Unreal SDK\n\n"
        << "`unreal_runtime.h` wraps `URK_UnrealApi`: objects, members and functions are resolved by name at "
           "runtime through the engine's own reflection.\n\n"
        << "## Typed headers\n\n"
        << "Set `DumpTypes=1` under `[Unreal]` in the game's `URKit_config.ini` and play: every map that loads "
           "adds its classes to `" << UnrealTypeCodegen::kDumpFileName << "` beside the game. Generating or updating "
           "the project then writes `types/<Name>.h`, one per class and struct.\n\n"
           "- Classes are handles and hold names and signatures, never offsets: each access resolves on the live "
           "class, so a game update needs no rebuild. A member the update removed fails at runtime and `get()` "
           "comes back empty.\n"
           "- Structs are values copied into the mod, so their header carries a layout. Before any copy it is "
           "checked against the running game; if an update changed the struct, accesses to it fail and the log "
           "says to regenerate. Members that own engine memory (names, strings, arrays) are kept as bytes, and "
           "the loader refuses a write that changes them.\n\n"
           "`types/` is rewritten from the dump; do not edit it.\n\n"
        << details;
    return out.str();
}

} // namespace

namespace UnrealSdkGenerator {

std::string SanitizeProjectName(const std::string &projectName, const std::string &fallback) {
    return mpg::Identifier(projectName, fallback.c_str());
}

bool HasUsableOutput(const std::string &directory) {
    std::error_code ec;
    const fs::path root(directory);
    for (const fs::path &file : {root / "unreal_runtime.h", root / "README.md"}) {
        if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) == 0 || ec)
            return false;
    }
    return true;
}

std::string TypeDumpPath(const std::string &gameDirectory) {
    return gameDirectory.empty() ? std::string() : (fs::path(gameDirectory) / UnrealTypeCodegen::kDumpFileName).string();
}

bool Generate(const std::string &directory, const std::string &reportDetails, const std::string &typeDumpPath,
              std::string *error) {
    sdk::OutputPlan plan;
    plan.root = fs::path(directory);
    plan.files = {
        {"unreal_runtime.h", {}, UnrealRuntimeModule(), mpg::OutputFilePolicy::GeneratedOverwrite, true, true},
        {"README.md", {}, UnrealSdkReadme(reportDetails), mpg::OutputFilePolicy::GeneratedOverwrite, true, false},
    };
    std::error_code ec;
    if (!typeDumpPath.empty() && fs::is_regular_file(typeDumpPath, ec)) {
        std::vector<UnrealTypeCodegen::Header> headers;
        if (!UnrealTypeCodegen::Build(typeDumpPath, &headers, error))
            return false;
        for (UnrealTypeCodegen::Header &header : headers)
            plan.files.push_back({fs::path("types") / header.fileName, {}, std::move(header.contents),
                                  mpg::OutputFilePolicy::GeneratedOverwrite, true, false, false});
    }
    sdk::OutputResult output;
    return sdk::PublishOutputPlanAtomically(plan, &output, error);
}

bool GenerateModProject(const std::string &projectRoot, const std::string &unrealSdkRoot,
                        const std::string &commonIncludeRoot, const std::string &rawProjectName,
                        const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                        std::string *error) {
    const fs::path root(projectRoot);
    const fs::path sdkRoot(unrealSdkRoot);
    const auto profile = mpg::UnrealBackendProfile();
    fs::path sdkHeaderPath;
    if (!commonIncludeRoot.empty())
        sdkHeaderPath = fs::path(commonIncludeRoot) / "mod_sdk.h";

    if (!HasUsableOutput(sdkRoot.string())) {
        if (error)
            *error = "Unreal SDK is missing or incomplete; generate Unreal SDK before project";
        return false;
    }

    sdk::OutputPlan backendFiles;
    backendFiles.root = root;
    backendFiles.files = {
        {profile.sdkSubdirectory / "unreal_runtime.h",
         sdkRoot / "unreal_runtime.h",
         {},
         mpg::OutputFilePolicy::GeneratedOverwrite,
         true,
         true},
    };
    // types/ is copied whole when the SDK was staged elsewhere; stale headers go.
    std::error_code ec;
    const fs::path projectTypes = root / profile.sdkSubdirectory / "types";
    if (!fs::equivalent(sdkRoot, root / profile.sdkSubdirectory, ec)) {
        fs::remove_all(projectTypes, ec);
        for (fs::directory_iterator it(sdkRoot / "types", ec), end; !ec && it != end; it.increment(ec))
            backendFiles.files.push_back({profile.sdkSubdirectory / "types" / it->path().filename(),
                                          it->path(),
                                          {},
                                          mpg::OutputFilePolicy::GeneratedOverwrite,
                                          true,
                                          false});
    }
    if (!sdk::WriteOutputPlan(backendFiles, nullptr, error))
        return false;

    auto options = mpg::MakeModuleProjectOptions(profile, root, SanitizeProjectName(rawProjectName), sdkHeaderPath);
    options.deployDirectory = (fs::path(gameDirectory) / modsDirectory).string();
    options.enableLocalization = enableLocalization;
    return mpg::WriteModuleProject(options, error);
}

} // namespace UnrealSdkGenerator
