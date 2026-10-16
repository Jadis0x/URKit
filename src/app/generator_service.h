#pragma once

#include "activity_log.h"

#include <filesystem>
#include <optional>
#include <string>

namespace UrkApp {

struct BackendDetection {
    std::string backend; // mono, il2cpp, unreal
    std::string evidence;
};

struct ProjectOptions {
    std::string backendSelection = "auto";
    std::string backend = "mono";
    std::string projectName = "GeneratedMod";
    std::string gameExePath;
    std::string gameDir;
    std::string exportRoot;
    std::string projectOut;
    std::string sdkOut;
    std::string modsDir = "Mods";
    std::string detectionEvidence;
    bool enableLocalization = false;
};

std::string Lower(std::string value);
std::string TrimPath(std::string value);
const char *BackendDisplayName(const std::string &backend);
bool IsValidBackendSelection(const std::string &value);

std::optional<BackendDetection> DetectBackend(const std::filesystem::path &gameDir);
// A packaged Unreal game runs from <Root>/<Project>/Binaries/Win64.
bool IsUnrealBinariesDir(const std::filesystem::path &gameDir);
// Machine type from the PE header: "x64", "x86", "arm64" or empty when the file is not a PE image.
std::string ExecutableMachine(const std::filesystem::path &exe);

// Validates the inputs and fills the derived paths. Returns false with a user-facing reason.
bool ResolveProjectOptions(ProjectOptions &options, std::string &error);
bool GenerateProject(const ProjectOptions &options, const LogSink &log, std::string &error);

} // namespace UrkApp
