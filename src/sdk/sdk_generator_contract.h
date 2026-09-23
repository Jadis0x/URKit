#pragma once

#include "mod_project_generator_common.h"

#include <filesystem>
#include <string>
#include <vector>

namespace SdkGenerator {

struct OutputFile {
    std::filesystem::path relativePath;
    std::filesystem::path sourcePath;
    std::string contents;
    ModProjectGenerator::OutputFilePolicy policy = ModProjectGenerator::OutputFilePolicy::GeneratedOverwrite;
    bool required = true;
    bool moduleFile = false;
    // One clang-format process per file; thousands of typed headers skip it.
    bool format = true;
};

struct OutputPlan {
    std::filesystem::path root;
    std::vector<OutputFile> files;
    bool preserveEditableFiles = true;
};

struct OutputResult {
    std::vector<std::filesystem::path> moduleFiles;
    std::vector<std::filesystem::path> requiredFiles;
};

bool WriteOutputPlan(const OutputPlan &plan, OutputResult *result, std::string *error);
bool ValidateOutputPlan(const OutputPlan &plan, std::string *error);
bool PublishOutputPlanAtomically(const OutputPlan &plan, OutputResult *result, std::string *error);

} // namespace SdkGenerator
