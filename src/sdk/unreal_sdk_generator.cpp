#include "unreal_sdk_generator.h"
#include "mod_project_generator_common.h"
#include "mod_project_generator_profiles.h"
#include "sdk_generator_contract.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

namespace {

namespace fs = std::filesystem;
namespace mpg = ModProjectGenerator;
namespace sdk = SdkGenerator;

#include "templates/unreal/runtime.inl"

std::string UnrealSdkReadme(const std::string &details) {
    std::ostringstream out;
    out << "# URKit - Unreal SDK\n\n"
        << "`unreal_runtime.h` wraps `URK_UnrealApi`. Nothing here is generated from the game: objects, members "
           "and functions are resolved by name at runtime through the engine's own reflection.\n\n"
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

bool Generate(const std::string &directory, const std::string &reportDetails, std::string *error) {
    sdk::OutputPlan plan;
    plan.root = fs::path(directory);
    plan.files = {
        {"unreal_runtime.h", {}, UnrealRuntimeModule(), mpg::OutputFilePolicy::GeneratedOverwrite, true, true},
        {"README.md", {}, UnrealSdkReadme(reportDetails), mpg::OutputFilePolicy::GeneratedOverwrite, true, false},
    };
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
    if (!sdk::WriteOutputPlan(backendFiles, nullptr, error))
        return false;

    auto options = mpg::MakeModuleProjectOptions(profile, root, SanitizeProjectName(rawProjectName), sdkHeaderPath);
    options.deployDirectory = (fs::path(gameDirectory) / modsDirectory).string();
    options.enableLocalization = enableLocalization;
    return mpg::WriteModuleProject(options, error);
}

} // namespace UnrealSdkGenerator
