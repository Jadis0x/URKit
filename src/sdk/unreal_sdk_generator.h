#pragma once

#include <string>

namespace UnrealSdkGenerator {

std::string SanitizeProjectName(const std::string &projectName, const std::string &fallback = "UnrealMod");

bool Generate(const std::string &outputDirectory, const std::string &reportDetails, std::string *error);

bool GenerateModProject(const std::string &projectRoot, const std::string &unrealSdkRoot,
                        const std::string &commonIncludeRoot, const std::string &rawProjectName,
                        const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                        std::string *error);

bool HasUsableOutput(const std::string &outputDirectory);

} // namespace UnrealSdkGenerator
