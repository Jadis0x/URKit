#include "project_updater.h"

#include "il2cpp_sdk_generator.h"
#include "mod_sdk.h"
#include "mono_sdk_generator.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>

namespace UrkProject {
namespace {

namespace fs = std::filesystem;

struct SnapshotEntry {
    fs::path relativePath;
    bool existed = false;
};

constexpr const char *kManagedPaths[] = {
    "sdk",
    "mod/generated",
    "mod/ui",
    "mod/hooks",
    "CMakeLists.txt",
    "CMakePresets.json",
    ".clangd",
    ".vscode/c_cpp_properties.json",
    ".urk/project.ini",
};

bool IsRegularFile(const fs::path &path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

bool PathExists(const fs::path &path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

bool ReadText(const fs::path &path, std::string *text, std::string *error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "cannot read " + path.string();
        return false;
    }
    std::ostringstream output;
    output << input.rdbuf();
    if (input.bad()) {
        if (error)
            *error = "cannot read " + path.string();
        return false;
    }
    *text = output.str();
    return true;
}

int ReadSdkVersion(const fs::path &header, std::string *error) {
    std::string text;
    if (!ReadText(header, &text, error))
        return 0;

    constexpr std::string_view marker = "#define URK_SDK_VERSION";
    const size_t markerPosition = text.find(marker);
    if (markerPosition == std::string::npos) {
        if (error)
            *error = "URK_SDK_VERSION is missing from " + header.string();
        return 0;
    }
    const size_t numberBegin = text.find_first_of("0123456789", markerPosition + marker.size());
    if (numberBegin == std::string::npos) {
        if (error)
            *error = "URK_SDK_VERSION is invalid in " + header.string();
        return 0;
    }
    size_t numberEnd = numberBegin;
    while (numberEnd < text.size() && text[numberEnd] >= '0' && text[numberEnd] <= '9')
        ++numberEnd;
    int version = 0;
    const auto [end, conversionError] =
        std::from_chars(text.data() + numberBegin, text.data() + numberEnd, version);
    if (conversionError != std::errc{} || end != text.data() + numberEnd || version <= 0) {
        if (error)
            *error = "URK_SDK_VERSION is invalid in " + header.string();
        return 0;
    }
    return version;
}

std::string ExtractQuotedValue(const std::string &text, std::string_view marker) {
    const size_t markerPosition = text.find(marker);
    if (markerPosition == std::string::npos)
        return {};
    const size_t quoteBegin = text.find('"', markerPosition + marker.size());
    if (quoteBegin == std::string::npos)
        return {};
    const size_t quoteEnd = text.find('"', quoteBegin + 1);
    if (quoteEnd == std::string::npos)
        return {};
    const std::string escaped = text.substr(quoteBegin + 1, quoteEnd - quoteBegin - 1);
    std::string value;
    value.reserve(escaped.size());
    for (size_t index = 0; index < escaped.size(); ++index) {
        const char current = escaped[index];
        if (current != '\\' || index + 1 >= escaped.size()) {
            value.push_back(current);
            continue;
        }
        const char next = escaped[++index];
        switch (next) {
            case '\\':
                value.push_back('\\');
                break;
            case '"':
                value.push_back('"');
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                value.push_back(next);
                break;
        }
    }
    return value;
}

std::string ExtractCMakeProjectName(const std::string &text) {
    constexpr std::string_view marker = "project(";
    const size_t start = text.find(marker);
    if (start == std::string::npos)
        return {};
    const size_t nameBegin = text.find_first_not_of(" \t\r\n", start + marker.size());
    if (nameBegin == std::string::npos)
        return {};
    size_t nameEnd = nameBegin;
    while (nameEnd < text.size() && text[nameEnd] != ')' && text[nameEnd] != ' ' && text[nameEnd] != '\t' &&
           text[nameEnd] != '\r' && text[nameEnd] != '\n')
        ++nameEnd;
    return text.substr(nameBegin, nameEnd - nameBegin);
}

bool ParseLocalization(const fs::path &configPath) {
    std::string config;
    std::string ignored;
    if (!ReadText(configPath, &config, &ignored))
        return false;
    constexpr std::string_view marker = "enable_localization";
    const size_t markerPosition = config.find(marker);
    if (markerPosition == std::string::npos)
        return false;
    const size_t equals = config.find('=', markerPosition + marker.size());
    if (equals == std::string::npos)
        return false;
    const size_t value = config.find_first_not_of(" \t", equals + 1);
    return value != std::string::npos && config.compare(value, 4, "true") == 0;
}

bool InspectLegacyProject(const fs::path &root, Inspection *inspection, std::string *error) {
    const bool hasMono = IsRegularFile(root / "sdk/mono/mono_runtime.h");
    const bool hasIl2Cpp = IsRegularFile(root / "sdk/il2cpp/il2cpp_runtime.h");
    if (hasMono == hasIl2Cpp) {
        if (error) {
            *error = hasMono ? "legacy project has both Mono and IL2CPP SDK folders; choose a project with one backend"
                             : "project does not contain a supported URKit Mono or IL2CPP SDK folder";
        }
        return false;
    }

    std::string cmake;
    if (!ReadText(root / "CMakeLists.txt", &cmake, error))
        return false;
    const std::string deployDirectory = ExtractQuotedValue(cmake, "set(URK_DEPLOY_DIR");
    if (deployDirectory.empty()) {
        if (error)
            *error = "legacy project does not have a generated URK_DEPLOY_DIR; updater cannot safely preserve deployment";
        return false;
    }
    const fs::path deployPath(deployDirectory);
    if (!deployPath.is_absolute() || deployPath.parent_path().empty() || deployPath.filename().empty()) {
        if (error)
            *error = "legacy project has an invalid URK_DEPLOY_DIR: " + deployDirectory;
        return false;
    }

    Manifest manifest;
    manifest.backend = hasIl2Cpp ? Backend::Il2Cpp : Backend::Mono;
    manifest.gameDirectory = deployPath.parent_path().lexically_normal();
    manifest.modsDirectory = deployPath.filename().string();
    manifest.projectName = ExtractCMakeProjectName(cmake);

    std::string config;
    std::string ignored;
    if (ReadText(root / "mod/config/mod_config.h", &config, &ignored)) {
        const std::string configuredName = ExtractQuotedValue(config, "project_name");
        if (!configuredName.empty())
            manifest.projectName = configuredName;
    }
    if (manifest.projectName.empty()) {
        if (error)
            *error = "legacy project does not declare a project name";
        return false;
    }
    manifest.enableLocalization = ParseLocalization(root / "mod/config/mod_config.h");

    std::string versionError;
    manifest.sdkVersion = ReadSdkVersion(root / "sdk/mod_sdk.h", &versionError);
    if (manifest.sdkVersion <= 0) {
        if (error)
            *error = versionError;
        return false;
    }

    inspection->manifest = std::move(manifest);
    inspection->detectedSdkVersion = inspection->manifest.sdkVersion;
    inspection->notices.push_back("No .urk/project.ini was found; updater inferred project settings from generated files.");
    return true;
}

bool CopyPath(const fs::path &source, const fs::path &destination, std::string *error) {
    std::error_code ec;
    if (fs::is_directory(source, ec) && !ec) {
        fs::create_directories(destination.parent_path(), ec);
        if (!ec)
            fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
    } else if (!ec && fs::is_regular_file(source, ec) && !ec) {
        fs::create_directories(destination.parent_path(), ec);
        if (!ec)
            fs::copy_file(source, destination, fs::copy_options::overwrite_existing, ec);
    } else {
        if (error)
            *error = "cannot snapshot unsupported path: " + source.string();
        return false;
    }
    if (ec) {
        if (error)
            *error = "cannot copy " + source.string() + " to " + destination.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool RemovePath(const fs::path &path, std::string *error) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        if (error)
            *error = "cannot remove " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool CreateBackup(const fs::path &root, int sdkVersion, fs::path *backupDirectory, std::vector<SnapshotEntry> *entries,
                  std::string *error) {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const fs::path backup = root / ".urk/backups" /
                            ("before-sdk-v" + std::to_string(sdkVersion) + "-" + std::to_string(stamp));
    std::error_code ec;
    fs::create_directories(backup, ec);
    if (ec) {
        if (error)
            *error = "cannot create update backup " + backup.string() + ": " + ec.message();
        return false;
    }

    entries->clear();
    entries->reserve(std::size(kManagedPaths));
    for (const char *managed : kManagedPaths) {
        SnapshotEntry entry{fs::path(managed), PathExists(root / managed)};
        if (entry.existed && !CopyPath(root / entry.relativePath, backup / entry.relativePath, error))
            return false;
        entries->push_back(std::move(entry));
    }

    std::ofstream metadata(backup / "snapshot.txt", std::ios::binary | std::ios::trunc);
    if (!metadata) {
        if (error)
            *error = "cannot record update backup metadata: " + backup.string();
        return false;
    }
    metadata << "URKit project update backup\n";
    metadata << "source_sdk_version=" << sdkVersion << '\n';
    for (const SnapshotEntry &entry : *entries)
        metadata << entry.relativePath.generic_string() << '=' << (entry.existed ? "present" : "absent") << '\n';
    metadata.close();
    if (!metadata) {
        if (error)
            *error = "cannot finish update backup metadata: " + backup.string();
        return false;
    }

    *backupDirectory = backup;
    return true;
}

bool RestoreBackup(const fs::path &root, const fs::path &backup, const std::vector<SnapshotEntry> &entries,
                   std::string *error) {
    for (const SnapshotEntry &entry : entries) {
        if (!RemovePath(root / entry.relativePath, error))
            return false;
        if (entry.existed && !CopyPath(backup / entry.relativePath, root / entry.relativePath, error))
            return false;
    }
    return true;
}

bool Regenerate(const fs::path &root, const Manifest &manifest, std::string *error) {
    const std::string reportDetails = "Mono and IL2CPP generated projects use runtime API helpers. "
                                      "No offline metadata or dump-generated modules are emitted.\n";
    const fs::path sdkRoot = root / "sdk" / BackendName(manifest.backend);
    const std::string gameDirectory = manifest.gameDirectory.string();

    if (manifest.backend == Backend::Il2Cpp) {
        if (!Il2CppSdkGenerator::Generate(sdkRoot.string(), reportDetails, error))
            return false;
        return Il2CppSdkGenerator::GenerateModProject(root.string(), sdkRoot.string(), {}, manifest.projectName,
                                                      gameDirectory, manifest.modsDirectory, manifest.enableLocalization,
                                                      error);
    }

    if (!MonoSdkGenerator::Generate(sdkRoot.string(), reportDetails, error))
        return false;
    return MonoSdkGenerator::GenerateModProject(root.string(), sdkRoot.string(), {}, manifest.projectName, gameDirectory,
                                                manifest.modsDirectory, manifest.enableLocalization, error);
}

class ScopedPreviewDirectory {
  public:
    explicit ScopedPreviewDirectory(fs::path path) : path_(std::move(path)) {}
    ~ScopedPreviewDirectory() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    const fs::path &path() const { return path_; }

  private:
    fs::path path_;
};

bool IsPreviewManagedFile(const fs::path &relativePath) {
    const std::string path = relativePath.generic_string();
    return path.starts_with("sdk/") || path.starts_with("mod/generated/") || path.starts_with("mod/ui/") ||
           path.starts_with("mod/hooks/") || path == "CMakeLists.txt" || path == "CMakePresets.json" ||
           path == ".clangd" || path == ".vscode/c_cpp_properties.json" || path == kProjectManifestRelativePath;
}

bool FileContentsEqual(const fs::path &left, const fs::path &right, bool *equal, std::string *error) {
    std::error_code ec;
    if (!fs::is_regular_file(left, ec) || ec || !fs::is_regular_file(right, ec) || ec) {
        if (error)
            *error = "cannot compare generated project file " + left.string();
        return false;
    }
    const auto leftSize = fs::file_size(left, ec);
    if (ec) {
        if (error)
            *error = "cannot inspect generated project file " + left.string() + ": " + ec.message();
        return false;
    }
    const auto rightSize = fs::file_size(right, ec);
    if (ec) {
        if (error)
            *error = "cannot inspect generated project file " + right.string() + ": " + ec.message();
        return false;
    }
    if (leftSize != rightSize) {
        *equal = false;
        return true;
    }

    std::ifstream leftInput(left, std::ios::binary);
    std::ifstream rightInput(right, std::ios::binary);
    if (!leftInput || !rightInput) {
        if (error)
            *error = "cannot open generated project files for comparison";
        return false;
    }
    constexpr size_t kBufferSize = 64 * 1024;
    char leftBuffer[kBufferSize];
    char rightBuffer[kBufferSize];
    while (leftInput && rightInput) {
        leftInput.read(leftBuffer, sizeof(leftBuffer));
        rightInput.read(rightBuffer, sizeof(rightBuffer));
        const std::streamsize leftCount = leftInput.gcount();
        const std::streamsize rightCount = rightInput.gcount();
        if (leftCount != rightCount || !std::equal(leftBuffer, leftBuffer + leftCount, rightBuffer)) {
            *equal = false;
            return true;
        }
    }
    if (leftInput.bad() || rightInput.bad()) {
        if (error)
            *error = "cannot read generated project files for comparison";
        return false;
    }
    *equal = true;
    return true;
}

bool BuildPreview(const Inspection &inspection, std::vector<PlannedChange> *changes, std::string *error) {
    std::error_code ec;
    const fs::path temporaryRoot = fs::temp_directory_path(ec);
    if (ec) {
        if (error)
            *error = "cannot resolve a temporary directory for update preview: " + ec.message();
        return false;
    }
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const fs::path stage = temporaryRoot / ("urk-project-preview-" + std::to_string(timestamp));
    fs::create_directories(stage, ec);
    if (ec) {
        if (error)
            *error = "cannot create update preview directory " + stage.string() + ": " + ec.message();
        return false;
    }
    ScopedPreviewDirectory cleanup(stage);

    for (const fs::path &preservedPath : {fs::path("mod"), fs::path("locales")}) {
        const fs::path source = inspection.projectRoot / preservedPath;
        if (PathExists(source) && !CopyPath(source, stage / preservedPath, error))
            return false;
    }
    if (!Regenerate(stage, inspection.manifest, error))
        return false;

    changes->clear();
    fs::recursive_directory_iterator iterator(stage, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && iterator != end) {
        const fs::directory_entry entry = *iterator;
        iterator.increment(ec);
        if (ec)
            break;
        if (!entry.is_regular_file(ec) || ec)
            continue;
        const fs::path relativePath = entry.path().lexically_relative(stage);
        if (!IsPreviewManagedFile(relativePath))
            continue;
        const fs::path existing = inspection.projectRoot / relativePath;
        if (!PathExists(existing)) {
            changes->push_back({relativePath, ChangeKind::Added});
            continue;
        }
        bool equal = false;
        if (!FileContentsEqual(entry.path(), existing, &equal, error))
            return false;
        if (!equal)
            changes->push_back({relativePath, ChangeKind::Modified});
    }
    if (ec) {
        if (error)
            *error = "cannot enumerate update preview output: " + ec.message();
        return false;
    }
    std::sort(changes->begin(), changes->end(), [](const PlannedChange &left, const PlannedChange &right) {
        return left.relativePath.generic_string() < right.relativePath.generic_string();
    });
    return true;
}

} // namespace

bool Inspect(const fs::path &projectRoot, Inspection *inspection, std::string *error) {
    if (error)
        error->clear();
    if (!inspection) {
        if (error)
            *error = "project inspection output is null";
        return false;
    }
    std::error_code ec;
    const fs::path root = fs::absolute(projectRoot, ec).lexically_normal();
    if (ec || projectRoot.empty() || !fs::is_directory(root, ec) || ec) {
        if (error)
            *error = "project directory does not exist: " + projectRoot.string();
        return false;
    }
    if (!IsRegularFile(root / "CMakeLists.txt")) {
        if (error)
            *error = "selected directory is not a URKit project (CMakeLists.txt is missing): " + root.string();
        return false;
    }

    Inspection parsed;
    parsed.projectRoot = root;
    parsed.hasManifest = PathExists(root / kProjectManifestRelativePath);
    if (parsed.hasManifest) {
        if (!ReadManifest(root, &parsed.manifest, error))
            return false;
        std::string versionError;
        parsed.detectedSdkVersion = ReadSdkVersion(root / "sdk/mod_sdk.h", &versionError);
        if (parsed.detectedSdkVersion == 0)
            parsed.notices.push_back("sdk/mod_sdk.h is missing or malformed; the update can recreate it.");
    } else if (!InspectLegacyProject(root, &parsed, error)) {
        return false;
    }

    if (parsed.manifest.sdkVersion > URK_SDK_VERSION || parsed.detectedSdkVersion > URK_SDK_VERSION) {
        if (error) {
            *error = "this project uses a newer SDK than urk-updater.exe (project=" +
                     std::to_string(std::max(parsed.manifest.sdkVersion, parsed.detectedSdkVersion)) + ", updater=" +
                     std::to_string(URK_SDK_VERSION) + ")";
        }
        return false;
    }
    parsed.updateAvailable = !parsed.hasManifest || parsed.manifest.sdkVersion < URK_SDK_VERSION ||
                             parsed.detectedSdkVersion < URK_SDK_VERSION || parsed.detectedSdkVersion == 0;
    if (!parsed.updateAvailable)
        parsed.notices.push_back("Project generated files already match this updater's SDK version.");

    *inspection = std::move(parsed);
    return true;
}

bool PreviewUpdate(const fs::path &projectRoot, UpdatePreview *preview, std::string *error) {
    if (error)
        error->clear();
    if (!preview) {
        if (error)
            *error = "update preview output is null";
        return false;
    }
    Inspection inspection;
    if (!Inspect(projectRoot, &inspection, error))
        return false;

    UpdatePreview result;
    result.inspection = inspection;
    if (inspection.updateAvailable && !BuildPreview(inspection, &result.changes, error))
        return false;
    *preview = std::move(result);
    return true;
}

bool Update(const fs::path &projectRoot, UpdateResult *result, std::string *error) {
    Inspection inspection;
    if (!Inspect(projectRoot, &inspection, error))
        return false;
    if (result)
        result->inspection = inspection;
    if (!inspection.updateAvailable)
        return true;

    fs::path backup;
    std::vector<SnapshotEntry> snapshot;
    const int sourceVersion = inspection.detectedSdkVersion > 0 ? inspection.detectedSdkVersion : inspection.manifest.sdkVersion;
    if (!CreateBackup(inspection.projectRoot, sourceVersion, &backup, &snapshot, error))
        return false;

    std::string updateError;
    if (!Regenerate(inspection.projectRoot, inspection.manifest, &updateError)) {
        std::string rollbackError;
        if (!RestoreBackup(inspection.projectRoot, backup, snapshot, &rollbackError))
            updateError += "; automatic rollback failed: " + rollbackError + "; restore from " + backup.string();
        if (error)
            *error = updateError;
        return false;
    }

    Inspection updated;
    if (!Inspect(inspection.projectRoot, &updated, &updateError)) {
        std::string rollbackError;
        if (!RestoreBackup(inspection.projectRoot, backup, snapshot, &rollbackError))
            updateError += "; automatic rollback failed: " + rollbackError + "; restore from " + backup.string();
        if (error)
            *error = updateError;
        return false;
    }
    if (result) {
        result->inspection = std::move(updated);
        result->backupDirectory = std::move(backup);
        result->updated = true;
    }
    return true;
}

std::string Describe(const Inspection &inspection) {
    std::ostringstream out;
    out << "Project: " << inspection.manifest.projectName << '\n'
        << "Path: " << inspection.projectRoot.string() << '\n'
        << "Backend: " << (inspection.manifest.backend == Backend::Il2Cpp ? "IL2CPP" : "Mono") << '\n'
        << "Project SDK: " << inspection.detectedSdkVersion << '\n'
        << "Updater SDK: " << URK_SDK_VERSION << '\n'
        << "Manifest: " << (inspection.hasManifest ? "present" : "legacy project; will be created") << '\n'
        << "Status: " << (inspection.updateAvailable ? "update available" : "up to date");
    for (const std::string &notice : inspection.notices)
        out << '\n' << "Note: " << notice;
    return out.str();
}

std::string DescribeChanges(const std::vector<PlannedChange> &changes) {
    std::ostringstream out;
    if (changes.empty())
        return "Files to change: none";
    out << "Files to change (" << changes.size() << "):";
    for (const PlannedChange &change : changes)
        out << '\n' << (change.kind == ChangeKind::Added ? "+ " : "~ ") << change.relativePath.generic_string();
    return out.str();
}

} // namespace UrkProject
