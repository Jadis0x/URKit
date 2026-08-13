#pragma once

#include <string>

namespace MonoSdkGenerator {

std::string SanitizeProjectName(const std::string &projectName, const std::string &fallback = "MonoMod");

bool Generate(const std::string &outputDirectory, const std::string &reportDetails, std::string *error);

bool GenerateModProject(const std::string &projectRoot, const std::string &monoSdkRoot,
                        const std::string &commonIncludeRoot, const std::string &rawProjectName,
                        const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                        std::string *error);

bool HasUsableOutput(const std::string &outputDirectory);

} // namespace MonoSdkGenerator
