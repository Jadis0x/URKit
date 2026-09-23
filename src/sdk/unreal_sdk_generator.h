#pragma once

#include <string>

namespace UnrealSdkGenerator {

std::string SanitizeProjectName(const std::string &projectName, const std::string &fallback = "UnrealMod");

// Where the loader leaves its type dump for this game ([Unreal] DumpTypes=1).
std::string TypeDumpPath(const std::string &gameDirectory);

// With a dump at typeDumpPath, also writes one typed header per class to types/.
bool Generate(const std::string &outputDirectory, const std::string &reportDetails, const std::string &typeDumpPath,
              std::string *error);

bool GenerateModProject(const std::string &projectRoot, const std::string &unrealSdkRoot,
                        const std::string &commonIncludeRoot, const std::string &rawProjectName,
                        const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                        std::string *error);

bool HasUsableOutput(const std::string &outputDirectory);

} // namespace UnrealSdkGenerator
