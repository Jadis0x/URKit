#pragma once

#include "mod_project_generator_common.h"

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ModProjectGenerator {

struct BackendProjectProfile {
    std::string displayName;
    std::string moduleName;
    std::string namespaceName;
    std::string description;
    std::filesystem::path sdkSubdirectory;
    std::vector<std::filesystem::path> moduleFiles;
    std::vector<std::string> includeDirectories;
    std::vector<std::string> readmeExtraLayoutLines;
};

inline void ApplyBackendProfile(ModuleProjectOptions &options, const BackendProjectProfile &profile) {
    options.backendDisplayName = profile.displayName;
    options.backendModule = profile.moduleName;
    options.backendNamespace = profile.namespaceName;
    if (profile.namespaceName == "URK::mono") {
        options.requiredBackendConstant = "URK::runtime_backend_mono";
        options.requiredCapabilityConstant = "URK::runtime_cap_mono_api";
    } else if (profile.namespaceName == "URK::il2cpp") {
        options.requiredBackendConstant = "URK::runtime_backend_il2cpp";
        options.requiredCapabilityConstant = "URK::runtime_cap_il2cpp_api";
    }
    options.description = profile.description;
    options.backendModuleFiles = profile.moduleFiles;
    options.includeDirectories = profile.includeDirectories;
    options.readmeExtraLayoutLines = profile.readmeExtraLayoutLines;
}

inline ModuleProjectOptions MakeModuleProjectOptions(const BackendProjectProfile &profile,
                                                     std::filesystem::path projectRoot, std::string projectName,
                                                     std::filesystem::path sdkHeaderPath = {}) {
    ModuleProjectOptions options;
    options.projectRoot = std::move(projectRoot);
    options.sdkHeaderPath = std::move(sdkHeaderPath);
    options.projectName = std::move(projectName);
    ApplyBackendProfile(options, profile);
    return options;
}

inline BackendProjectProfile MonoBackendProfile() {
    BackendProjectProfile profile;
    profile.displayName = "Mono";
    profile.moduleName = "urk.mono.runtime";
    profile.namespaceName = "URK::mono";
    profile.description = "Generated URKit Mono native mod";
    profile.sdkSubdirectory = "sdk/mono";
    profile.moduleFiles = {
        "sdk/mono/mono_runtime.h",
        "sdk/mono/mono_helpers.h",
    };
    profile.readmeExtraLayoutLines = {
        "`sdk/mono/mono_runtime.h`, `sdk/mono/mono_helpers.h`: generated Mono "
        "runtime adapter headers.",
    };
    return profile;
}

inline BackendProjectProfile Il2CppBackendProfile() {
    BackendProjectProfile profile;
    profile.displayName = "IL2CPP";
    profile.moduleName = "urk.il2cpp.runtime";
    profile.namespaceName = "URK::il2cpp";
    profile.description = "URKit IL2CPP native mod profile";
    profile.sdkSubdirectory = "sdk/il2cpp";
    profile.moduleFiles = {
        "sdk/il2cpp/il2cpp_runtime.h",
        "sdk/il2cpp/il2cpp_helpers.h",
    };
    profile.readmeExtraLayoutLines = {
        "`sdk/il2cpp/il2cpp_runtime.h`, `sdk/il2cpp/il2cpp_helpers.h`: "
        "generated IL2CPP runtime adapter headers.",
    };
    return profile;
}

} // namespace ModProjectGenerator
