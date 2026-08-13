#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace ModProjectGenerator {

enum class OutputFilePolicy {
    GeneratedOverwrite,
    EditablePreserve,
    DocumentationPreserve,
};

struct OutputFileSpec {
    std::filesystem::path relativePath;
    OutputFilePolicy policy = OutputFilePolicy::GeneratedOverwrite;
    bool required = true;
    bool moduleFile = false;
    bool writtenByCommonGenerator = true;
};

std::string Identifier(const std::string &text, const char *fallback);
bool WriteText(const std::filesystem::path &path, const std::string &text, std::string *error);

struct ModuleProjectOptions {
    std::filesystem::path projectRoot;
    std::filesystem::path sdkHeaderPath;
    std::string projectName;
    std::string modId;
    std::string backendDisplayName;
    std::string backendModule;
    std::string backendNamespace;
    std::string requiredBackendConstant;
    std::string requiredCapabilityConstant;
    std::string description;
    std::string deployDirectory;
    std::vector<std::filesystem::path> backendModuleFiles;
    std::vector<std::filesystem::path> extraModuleFiles;
    std::vector<std::string> includeDirectories;
    std::vector<std::string> configExtraLines;
    std::vector<std::string> hookModuleImports;
    std::vector<std::string> hookInstallLines;
    std::vector<std::string> hookUninstallLines;
    std::vector<std::string> readmeExtraLayoutLines;
    std::vector<OutputFileSpec> extraOutputFiles;
    bool enableLocalization = false;
    bool logHookSummary = false;
    bool preserveEditableSources = true;
};

bool WriteModuleProject(const ModuleProjectOptions &options, std::string *error);

} // namespace ModProjectGenerator
