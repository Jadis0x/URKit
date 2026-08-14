#pragma once

#include <filesystem>
#include <string>

namespace UrkProject {

inline constexpr int kProjectManifestSchemaVersion = 1;
inline constexpr const char *kProjectManifestRelativePath = ".urk/project.ini";

enum class Backend {
    Mono,
    Il2Cpp,
};

struct Manifest {
    int schemaVersion = kProjectManifestSchemaVersion;
    int sdkVersion = 0;
    Backend backend = Backend::Mono;
    std::string projectName;
    std::filesystem::path gameDirectory;
    std::string modsDirectory = "Mods";
    bool enableLocalization = false;
};

const char *BackendName(Backend backend);
bool ParseBackend(const std::string &value, Backend *backend);

bool ReadManifest(const std::filesystem::path &projectRoot, Manifest *manifest, std::string *error);
bool WriteManifest(const std::filesystem::path &projectRoot, const Manifest &manifest, std::string *error);

} // namespace UrkProject
