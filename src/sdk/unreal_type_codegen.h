#pragma once

// Typed headers from the loader's type dump ([Unreal] DumpTypes=1). No offsets.

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace UnrealTypeCodegen {

// Written by the loader beside the game executable.
inline constexpr const char *kDumpFileName = "URKit_unreal_types.txt";

struct Header {
    std::string fileName;
    std::string contents;
};

// One header per class. Fails on a missing, foreign or newer dump.
bool Build(const std::filesystem::path &dumpPath, std::vector<Header> *headers, std::string *error);

} // namespace UnrealTypeCodegen
