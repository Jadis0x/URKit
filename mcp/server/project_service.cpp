#include "project_service.h"

#include "process_runner.h"
#include "project_manifest.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <unordered_set>

namespace URK::DevMcp {
namespace {

using Json = nlohmann::json;
namespace fs = std::filesystem;

std::string ReadText(const fs::path &path, std::string *error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "cannot open " + path.string();
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0 || size > 4 * 1024 * 1024) {
        if (error)
            *error = "file size is outside the allowed range: " + path.string();
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(size), '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!input && !input.eof()) {
        if (error)
            *error = "cannot read " + path.string();
        return {};
    }
    text.resize(static_cast<std::size_t>(input.gcount()));
    return text;
}

ServiceResult ParseProject(const fs::path &root, UrkProject::Manifest *manifest, Json *presets) {
    std::string error;
    if (!UrkProject::ReadManifest(root, manifest, &error))
        return ServiceFailure("invalid_project", std::move(error));
    if (!fs::is_regular_file(root / "CMakeLists.txt"))
        return ServiceFailure("invalid_project", "CMakeLists.txt is missing from the project root");
    const std::string presetText = ReadText(root / "CMakePresets.json", &error);
    if (!error.empty())
        return ServiceFailure("invalid_project", std::move(error));
    try {
        const Json parsed = Json::parse(presetText);
        if (!parsed.is_object() || !parsed.contains("configurePresets") || !parsed["configurePresets"].is_array() ||
            !parsed.contains("buildPresets") || !parsed["buildPresets"].is_array())
            return ServiceFailure("invalid_project", "CMakePresets.json does not contain configure and build presets");
        if (presets)
            *presets = parsed;
    } catch (const Json::exception &) {
        return ServiceFailure("invalid_project", "CMakePresets.json is not valid JSON");
    }
    return ServiceSuccess(Json::object());
}

Json ManifestJson(const fs::path &root, const UrkProject::Manifest &manifest, const Json &presets) {
    Json presetNames = Json::array();
    for (const Json &preset : presets["buildPresets"])
        if (preset.is_object() && preset.contains("name") && preset["name"].is_string())
            presetNames.push_back(preset["name"]);
    const fs::path modsDirectory = manifest.gameDirectory / manifest.modsDirectory;
    const fs::path deployedDll = modsDirectory / (manifest.projectName + ".dll");
    return {{"project_root", root.string()},
            {"project_name", manifest.projectName},
            {"sdk_version", manifest.sdkVersion},
            {"backend", UrkProject::BackendName(manifest.backend)},
            {"game_directory", manifest.gameDirectory.string()},
            {"mods_directory", modsDirectory.string()},
            {"deployed_dll", deployedDll.string()},
            {"deployed", fs::is_regular_file(deployedDll)},
            {"presets", std::move(presetNames)}};
}

}

ServiceResult ServiceFailure(std::string code, std::string message) {
    return {false, nlohmann::json::object(), std::move(code), std::move(message)};
}

ServiceResult ServiceSuccess(nlohmann::json value) {
    return {true, std::move(value), {}, {}};
}

ProjectService::ProjectService(std::filesystem::path projectRoot) {
    std::error_code error;
    projectRoot_ = fs::absolute(std::move(projectRoot), error).lexically_normal();
    if (error)
        projectRoot_.clear();
}

ServiceResult ProjectService::LoadProject(nlohmann::json *presets) const {
    if (projectRoot_.empty())
        return ServiceFailure("invalid_project", "project root could not be resolved");
    UrkProject::Manifest manifest;
    return ParseProject(projectRoot_, &manifest, presets);
}

bool ProjectService::IsPresetAllowed(const std::string &preset, nlohmann::json *presets, std::string *error) const {
    UrkProject::Manifest manifest;
    const ServiceResult loaded = ParseProject(projectRoot_, &manifest, presets);
    if (!loaded.ok) {
        if (error)
            *error = loaded.message;
        return false;
    }
    std::unordered_set<std::string> configureNames;
    for (const Json &entry : (*presets)["configurePresets"])
        if (entry.is_object() && entry.contains("name") && entry["name"].is_string())
            configureNames.insert(entry["name"].get<std::string>());
    for (const Json &entry : (*presets)["buildPresets"]) {
        if (!entry.is_object() || entry.value("name", std::string{}) != preset)
            continue;
        const std::string configure = entry.value("configurePreset", std::string{});
        if (configureNames.contains(configure))
            return true;
    }
    if (error)
        *error = "preset is not declared by both configurePresets and buildPresets";
    return false;
}

ServiceResult ProjectService::ProjectInfo() const {
    UrkProject::Manifest manifest;
    Json presets;
    const ServiceResult loaded = ParseProject(projectRoot_, &manifest, &presets);
    if (!loaded.ok)
        return loaded;
    return ServiceSuccess(ManifestJson(projectRoot_, manifest, presets));
}

ServiceResult ProjectService::Build(std::string preset, ProcessCancellation *cancellation) {
    std::lock_guard lock(mutationMutex_);
    Json presets;
    std::string error;
    if (!IsPresetAllowed(preset, &presets, &error))
        return ServiceFailure("invalid_preset", std::move(error));
    const ProcessResult configure =
        RunProcess(projectRoot_, L"cmake", {L"--preset", std::wstring(preset.begin(), preset.end())},
                   std::chrono::minutes(10), cancellation);
    if (configure.cancelled)
        return ServiceFailure("cancelled", "build was cancelled during CMake configuration");
    if (!configure.started)
        return ServiceFailure("configure_start_failed", configure.error);
    if (configure.timedOut)
        return ServiceFailure("configure_timeout", configure.output);
    if (configure.exitCode != 0)
        return ServiceFailure("configure_failed", configure.output);
    const ProcessResult build = RunProcess(
        projectRoot_, L"cmake", {L"--build", L"--preset", std::wstring(preset.begin(), preset.end()), L"--parallel"},
        std::chrono::minutes(15), cancellation);
    if (build.cancelled)
        return ServiceFailure("cancelled", "build was cancelled during compilation");
    if (!build.started)
        return ServiceFailure("build_start_failed", build.error);
    if (build.timedOut)
        return ServiceFailure("build_timeout", build.output);
    if (build.exitCode != 0)
        return ServiceFailure("build_failed", build.output);
    UrkProject::Manifest manifest;
    const ServiceResult loaded = ParseProject(projectRoot_, &manifest, nullptr);
    if (!loaded.ok)
        return loaded;
    const fs::path deployedDll = manifest.gameDirectory / manifest.modsDirectory / (manifest.projectName + ".dll");
    if (!fs::is_regular_file(deployedDll))
        return ServiceFailure("deployment_missing",
                              "build succeeded but the deployed mod DLL was not found at " + deployedDll.string());
    return ServiceSuccess({{"preset", preset},
                           {"configured", true},
                           {"built", true},
                           {"deployed", true},
                           {"deployed_dll", deployedDll.string()},
                           {"output", configure.output + build.output}});
}

ServiceResult ProjectService::Deploy(std::string preset) {
    std::lock_guard lock(mutationMutex_);
    Json presets;
    std::string error;
    if (!IsPresetAllowed(preset, &presets, &error))
        return ServiceFailure("invalid_preset", std::move(error));
    UrkProject::Manifest manifest;
    const ServiceResult loaded = ParseProject(projectRoot_, &manifest, nullptr);
    if (!loaded.ok)
        return loaded;
    const fs::path artifact = projectRoot_ / "out" / "build" / preset / (manifest.projectName + ".dll");
    if (!fs::is_regular_file(artifact))
        return ServiceFailure("artifact_missing", "built mod DLL was not found at " + artifact.string());
    const fs::path destinationDirectory = manifest.gameDirectory / manifest.modsDirectory;
    const fs::path destination = destinationDirectory / artifact.filename();
    std::error_code filesystemError;
    fs::create_directories(destinationDirectory, filesystemError);
    if (filesystemError)
        return ServiceFailure("deploy_failed", "cannot create Mods directory: " + filesystemError.message());
    fs::copy_file(artifact, destination, fs::copy_options::overwrite_existing, filesystemError);
    if (filesystemError)
        return ServiceFailure("deploy_failed", "cannot deploy mod DLL: " + filesystemError.message());
    return ServiceSuccess(
        {{"preset", preset}, {"artifact", artifact.string()}, {"deployed_dll", destination.string()}});
}

ServiceResult ProjectService::ReadLogs(std::size_t maximumLines) const {
    UrkProject::Manifest manifest;
    const ServiceResult loaded = ParseProject(projectRoot_, &manifest, nullptr);
    if (!loaded.ok)
        return loaded;
    const fs::path logPath = manifest.gameDirectory / "URKit_logs.log";
    std::ifstream input(logPath, std::ios::binary);
    if (!input)
        return ServiceFailure("log_unavailable", "URKit log file was not found at " + logPath.string());
    std::deque<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(std::move(line));
        if (lines.size() > maximumLines)
            lines.pop_front();
    }
    if (!input.eof())
        return ServiceFailure("log_read_failed", "URKit log file could not be read completely");
    std::string text;
    for (const std::string &entry : lines) {
        text += entry;
        text.push_back('\n');
    }
    return ServiceSuccess({{"path", logPath.string()}, {"line_count", lines.size()}, {"text", std::move(text)}});
}

const std::filesystem::path &ProjectService::ProjectRoot() const {
    return projectRoot_;
}

}
