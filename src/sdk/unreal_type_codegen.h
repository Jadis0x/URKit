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
    // Relative to folder: "Engine/Actor.h", or "INDEX.md".
    std::string fileName;
    std::string contents;
    // Under sdk/unreal/: "types", or "re" for the reverse-engineering exports.
    std::string folder = "types";
};

// One header per class in its package's folder, plus INDEX.md, pseudo-code and the re/ exports.
// Fails on a missing, foreign or newer dump.
bool Build(const std::filesystem::path &dumpPath, std::vector<Header> *headers, std::string *error);

} // namespace UnrealTypeCodegen
