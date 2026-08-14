#include "project_manifest.h"

#include <charconv>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>

#ifdef _WIN32
#include <windows.h>
#endif

namespace UrkProject {
namespace {

namespace fs = std::filesystem;

bool ParseInt(const std::string &text, int *value) {
    if (!value || text.empty())
        return false;
    int parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size())
        return false;
    *value = parsed;
    return true;
}

bool ParseBool(const std::string &text, bool *value) {
    if (!value)
        return false;
    if (text == "0" || text == "false") {
        *value = false;
        return true;
    }
    if (text == "1" || text == "true") {
        *value = true;
        return true;
    }
    return false;
}

bool IsSafeValue(const std::string &value) {
    return value.find_first_of("\r\n") == std::string::npos;
}

bool ReadFields(const fs::path &path, std::unordered_map<std::string, std::string> *fields, std::string *error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "cannot open project manifest: " + path.string();
        return false;
    }

    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        const size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            if (error)
                *error = "invalid project manifest line " + std::to_string(lineNumber) + ": " + path.string();
            return false;
        }
        const std::string key = line.substr(0, equals);
        if (!fields->emplace(key, line.substr(equals + 1)).second) {
            if (error)
                *error = "duplicate project manifest field '" + key + "': " + path.string();
            return false;
        }
    }
    if (!input.eof()) {
        if (error)
            *error = "cannot read project manifest: " + path.string();
        return false;
    }
    return true;
}

bool Require(const std::unordered_map<std::string, std::string> &fields, const char *key, std::string *value,
             std::string *error) {
    const auto found = fields.find(key);
    if (found == fields.end() || found->second.empty()) {
        if (error)
            *error = std::string("project manifest is missing '") + key + "'";
        return false;
    }
    *value = found->second;
    return true;
}

} // namespace

const char *BackendName(Backend backend) {
    return backend == Backend::Il2Cpp ? "il2cpp" : "mono";
}

bool ParseBackend(const std::string &value, Backend *backend) {
    if (!backend)
        return false;
    if (value == "mono") {
        *backend = Backend::Mono;
        return true;
    }
    if (value == "il2cpp") {
        *backend = Backend::Il2Cpp;
        return true;
    }
    return false;
}

bool ReadManifest(const fs::path &projectRoot, Manifest *manifest, std::string *error) {
    if (error)
        error->clear();
    if (!manifest) {
        if (error)
            *error = "project manifest output is null";
        return false;
    }

    const fs::path path = projectRoot / kProjectManifestRelativePath;
    std::unordered_map<std::string, std::string> fields;
    if (!ReadFields(path, &fields, error))
        return false;

    Manifest parsed;
    std::string value;
    if (!Require(fields, "schema_version", &value, error))
        return false;
    if (!ParseInt(value, &parsed.schemaVersion)) {
        if (error)
            *error = "project manifest has an invalid schema_version";
        return false;
    }
    if (parsed.schemaVersion != kProjectManifestSchemaVersion) {
        if (error)
            *error = "unsupported project manifest schema version: " + std::to_string(parsed.schemaVersion);
        return false;
    }
    if (!Require(fields, "sdk_version", &value, error))
        return false;
    if (!ParseInt(value, &parsed.sdkVersion) || parsed.sdkVersion <= 0) {
        if (error)
            *error = "project manifest has an invalid sdk_version";
        return false;
    }
    if (!Require(fields, "backend", &value, error))
        return false;
    if (!ParseBackend(value, &parsed.backend)) {
        if (error)
            *error = "project manifest has an invalid backend";
        return false;
    }
    if (!Require(fields, "project_name", &parsed.projectName, error))
        return false;
    if (!IsSafeValue(parsed.projectName)) {
        if (error)
            *error = "project manifest has an invalid project_name";
        return false;
    }
    if (!Require(fields, "game_directory", &value, error))
        return false;
    if (!IsSafeValue(value)) {
        if (error)
            *error = "project manifest has an invalid game_directory";
        return false;
    }
    parsed.gameDirectory = fs::path(value).lexically_normal();
    if (!Require(fields, "mods_directory", &parsed.modsDirectory, error))
        return false;
    if (!IsSafeValue(parsed.modsDirectory) || parsed.modsDirectory.empty()) {
        if (error)
            *error = "project manifest has an invalid mods_directory";
        return false;
    }
    if (!Require(fields, "enable_localization", &value, error))
        return false;
    if (!ParseBool(value, &parsed.enableLocalization)) {
        if (error)
            *error = "project manifest has an invalid enable_localization";
        return false;
    }

    *manifest = std::move(parsed);
    return true;
}

bool WriteManifest(const fs::path &projectRoot, const Manifest &manifest, std::string *error) {
    if (projectRoot.empty()) {
        if (error)
            *error = "project root is empty";
        return false;
    }
    if (manifest.schemaVersion != kProjectManifestSchemaVersion || manifest.sdkVersion <= 0 || manifest.projectName.empty() ||
        manifest.gameDirectory.empty() || manifest.modsDirectory.empty() || !IsSafeValue(manifest.projectName) ||
        !IsSafeValue(manifest.gameDirectory.string()) || !IsSafeValue(manifest.modsDirectory)) {
        if (error)
            *error = "refusing to write an invalid project manifest";
        return false;
    }

    const fs::path path = projectRoot / kProjectManifestRelativePath;
    const fs::path temporary = path.parent_path() / "project.ini.tmp";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error)
            *error = "cannot create project metadata directory " + path.parent_path().string() + ": " + ec.message();
        return false;
    }

    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error)
            *error = "cannot write temporary project manifest: " + temporary.string();
        return false;
    }
    output << "# Managed by URKit. Project-specific update metadata.\n"
           << "schema_version=" << manifest.schemaVersion << '\n'
           << "sdk_version=" << manifest.sdkVersion << '\n'
           << "backend=" << BackendName(manifest.backend) << '\n'
           << "project_name=" << manifest.projectName << '\n'
           << "game_directory=" << manifest.gameDirectory.lexically_normal().generic_string() << '\n'
           << "mods_directory=" << manifest.modsDirectory << '\n'
           << "enable_localization=" << (manifest.enableLocalization ? "1" : "0") << '\n';
    output.close();
    if (!output) {
        std::error_code cleanup;
        fs::remove(temporary, cleanup);
        if (error)
            *error = "cannot finish project manifest: " + temporary.string();
        return false;
    }

    const bool destinationExists = fs::exists(path, ec) && !ec;
    if (ec) {
        std::error_code cleanup;
        fs::remove(temporary, cleanup);
        if (error)
            *error = "cannot inspect existing project manifest " + path.string() + ": " + ec.message();
        return false;
    }
#ifdef _WIN32
    const bool published = destinationExists
                               ? ReplaceFileW(path.c_str(), temporary.c_str(), nullptr, REPLACEFILE_WRITE_THROUGH,
                                              nullptr, nullptr) != FALSE
                               : MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!published) {
        const DWORD code = GetLastError();
        std::error_code cleanup;
        fs::remove(temporary, cleanup);
        if (error)
            *error = "cannot publish project manifest " + path.string() + ": Windows error " + std::to_string(code);
        return false;
    }
    return true;
#else
    (void)destinationExists;
    fs::rename(temporary, path, ec);
    if (ec) {
        std::error_code cleanup;
        fs::remove(temporary, cleanup);
        if (error)
            *error = "cannot publish project manifest " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
#endif
}

} // namespace UrkProject
