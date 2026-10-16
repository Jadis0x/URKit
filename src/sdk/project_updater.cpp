#include "project_updater.h"

#include "il2cpp_sdk_generator.h"
#include "mod_sdk.h"
#include "mono_sdk_generator.h"
#include "unreal_sdk_generator.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_set>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace UrkProject {

class CandidateTree {
  public:
    explicit CandidateTree(std::filesystem::path root) : root_(std::move(root)) {}
    ~CandidateTree() {
        std::error_code ignored;
        std::filesystem::remove_all(root_, ignored);
    }
    CandidateTree(const CandidateTree &) = delete;
    CandidateTree &operator=(const CandidateTree &) = delete;

    const std::filesystem::path &root() const { return root_; }

  private:
    std::filesystem::path root_;
};

namespace {

namespace fs = std::filesystem;

constexpr const char *kJournalRelativePath = ".urk/update-journal.ini";
constexpr const char *kLockRelativePath = ".urk/update.lock";
constexpr const char *kBackupIndexName = "backup.ini";
// Same-sized lists stay readable; past this the apply log reports in batches.
constexpr std::size_t kPerFileLogLimit = 200;

void Emit(const ProgressSink &progress, const std::string &line) {
    if (progress)
        progress(line);
}

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

std::string LocalStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
}

fs::path UniqueDirectory(const fs::path &parent, const std::string &name) {
    fs::path candidate = parent / name;
    for (int suffix = 2; PathExists(candidate); ++suffix)
        candidate = parent / (name + "-" + std::to_string(suffix));
    return candidate;
}

std::string ElapsedText(std::chrono::steady_clock::time_point started) {
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(1);
    out << static_cast<double>(elapsed) / 1000.0 << " s";
    return out.str();
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
    fs::path deployPath(deployDirectory);
    const bool literal = !deployDirectory.empty() && deployDirectory.find("${") == std::string::npos &&
                         deployPath.is_absolute() && !deployPath.parent_path().empty() && !deployPath.filename().empty();
    std::string deployNotice;
    if (!literal) {
        // The path is computed in CMake (for example "${URK_DEFAULT_DEPLOY_DIR}") or missing. Projects made by URKit
        // sit in <game>/urk-sdk-output/<name>/project, so the game folder follows from the location.
        const fs::path outputRoot = root.parent_path().parent_path();
        if (outputRoot.filename() != "urk-sdk-output" || outputRoot.parent_path().empty()) {
            if (error)
                *error = deployDirectory.empty()
                             ? "CMakeLists.txt does not set URK_DEPLOY_DIR and the project is not inside "
                               "<game>\\urk-sdk-output, so the game folder is unknown"
                             : "URK_DEPLOY_DIR is computed in CMakeLists.txt (" + deployDirectory +
                                   ") and the project is not inside <game>\\urk-sdk-output, so the game folder is "
                                   "unknown";
            return false;
        }
        // Keep the folder name when the expression ends in a plain one, such as "${...}/Mods".
        std::string modsName = "Mods";
        const size_t slash = deployDirectory.find_last_of("/\\");
        if (slash != std::string::npos && slash + 1 < deployDirectory.size()) {
            const std::string tail = deployDirectory.substr(slash + 1);
            if (tail.find('$') == std::string::npos && tail.find('{') == std::string::npos)
                modsName = tail;
        }
        deployPath = outputRoot.parent_path() / modsName;
        deployNotice = "The deploy folder is computed in CMakeLists.txt; assuming " + deployPath.string() + ".";
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
    inspection->notices.push_back("No .urk/project.ini; settings were read from CMakeLists.txt and sdk/mod_sdk.h.");
    if (!deployNotice.empty())
        inspection->notices.push_back(deployNotice);
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
            *error = "cannot copy unsupported path: " + source.string();
        return false;
    }
    if (ec) {
        if (error)
            *error = "cannot copy " + source.string() + " to " + destination.string() + ": " + ec.message();
        return false;
    }
    return true;
}

void RenameWithRetry(const fs::path &from, const fs::path &to, std::error_code &ec) {
    // Antivirus and indexers hold freshly written files for a moment.
    for (int attempt = 0;; ++attempt) {
        ec.clear();
        fs::rename(from, to, ec);
        if (!ec || attempt == 10)
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// Copies next to the destination first, so a reader never sees a half-written file.
bool InstallFile(const fs::path &source, const fs::path &destination, std::string *error) {
    std::error_code ec;
    fs::create_directories(destination.parent_path(), ec);
    if (ec) {
        if (error)
            *error = "cannot create " + destination.parent_path().string() + ": " + ec.message();
        return false;
    }
    fs::path staged = destination;
    staged += ".urk-new";
    fs::copy_file(source, staged, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error)
            *error = "cannot stage " + destination.string() + ": " + ec.message();
        return false;
    }
    RenameWithRetry(staged, destination, ec);
    if (ec) {
        std::error_code cleanup;
        fs::remove(staged, cleanup);
        if (error)
            *error = "cannot replace " + destination.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool RemoveFile(const fs::path &path, std::string *error) {
    std::error_code ec;
    fs::remove(path, ec);
    if (ec) {
        if (error)
            *error = "cannot remove " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

const char *BackendDisplayName(Backend backend) {
    switch (backend) {
        case Backend::Il2Cpp:
            return "IL2CPP";
        case Backend::Unreal:
            return "Unreal";
        case Backend::Mono:
            break;
    }
    return "Mono";
}

// One SDK folder plus the project around it, for whichever backend.
bool GenerateBackend(Backend backend, const fs::path &root, const fs::path &sdkRoot, const std::string &projectName,
                     const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                     const std::string &reportDetails, std::string *error) {
    switch (backend) {
        case Backend::Il2Cpp:
            return Il2CppSdkGenerator::Generate(sdkRoot.string(), reportDetails, error) &&
                   Il2CppSdkGenerator::GenerateModProject(root.string(), sdkRoot.string(), {}, projectName,
                                                          gameDirectory, modsDirectory, enableLocalization, error);
        case Backend::Unreal:
            return UnrealSdkGenerator::Generate(sdkRoot.string(), reportDetails,
                                                UnrealSdkGenerator::TypeDumpPath(gameDirectory), error) &&
                   UnrealSdkGenerator::GenerateModProject(root.string(), sdkRoot.string(), {}, projectName,
                                                          gameDirectory, modsDirectory, enableLocalization, error);
        case Backend::Mono:
            break;
    }
    return MonoSdkGenerator::Generate(sdkRoot.string(), reportDetails, error) &&
           MonoSdkGenerator::GenerateModProject(root.string(), sdkRoot.string(), {}, projectName, gameDirectory,
                                                modsDirectory, enableLocalization, error);
}

bool Regenerate(const fs::path &root, const Manifest &manifest, std::string *error) {
    const std::string reportDetails = std::string(kSdkReportDetails) + "\n";
    return GenerateBackend(manifest.backend, root, root / "sdk" / BackendName(manifest.backend), manifest.projectName,
                           manifest.gameDirectory.string(), manifest.modsDirectory, manifest.enableLocalization,
                           reportDetails, error);
}

// The backend SDK folder is published as a whole directory by the generator, so every file
// below it belongs to the generator, recorded in the ledger or not.
bool IsGeneratorOwnedTree(const std::string &generic) {
    return generic.starts_with("sdk/mono/") || generic.starts_with("sdk/il2cpp/") ||
           generic.starts_with("sdk/unreal/");
}

bool IsEditableArea(const std::string &generic) {
    return generic.starts_with("mod/") || generic.starts_with("locales/");
}

// The one user file copied into the candidate: it carries the mod id generated code embeds.
constexpr std::string_view kModConfigPath = "mod/config/mod_config.h";

bool HashOrEmpty(const fs::path &path, std::string *hash, std::string *error) {
    hash->clear();
    if (!PathExists(path))
        return true;
    return HashFileSha256(path, hash, error);
}

bool CollectFiles(const fs::path &root, const fs::path &subtree, std::vector<std::string> *files, std::string *error) {
    const fs::path start = root / subtree;
    if (!PathExists(start))
        return true;
    std::error_code ec;
    fs::recursive_directory_iterator iterator(start, ec);
    const fs::recursive_directory_iterator end;
    while (!ec && iterator != end) {
        const fs::directory_entry entry = *iterator;
        iterator.increment(ec);
        if (ec)
            break;
        std::error_code typeError;
        if (!entry.is_regular_file(typeError) || typeError)
            continue;
        files->push_back(entry.path().lexically_relative(root).generic_string());
    }
    if (ec) {
        if (error)
            *error = "cannot enumerate " + start.string() + ": " + ec.message();
        return false;
    }
    return true;
}

// Candidates normally go away with their preview; a killed process leaves its folder behind.
void RemoveOrphanedCandidates(const fs::path &temporaryRoot) {
#ifdef _WIN32
    std::error_code ec;
    for (fs::directory_iterator it(temporaryRoot, ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!name.starts_with("urk-update-"))
            continue;
        const size_t dash = name.find('-', 11);
        DWORD owner = 0;
        const auto [last, parseError] = std::from_chars(name.data() + 11, name.data() + (dash == std::string::npos ? name.size() : dash), owner);
        (void)last;
        if (parseError != std::errc{} || owner == GetCurrentProcessId())
            continue;
        if (HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, owner)) {
            const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
            CloseHandle(process);
            if (alive)
                continue;
        } else if (GetLastError() == ERROR_ACCESS_DENIED) {
            continue;
        }
        std::error_code ignored;
        fs::remove_all(it->path(), ignored);
    }
#else
    (void)temporaryRoot;
#endif
}

bool BuildCandidate(const Inspection &inspection, std::shared_ptr<const CandidateTree> *candidate, std::string *error,
                    const ProgressSink &progress) {
    std::error_code ec;
    const fs::path temporaryRoot = fs::temp_directory_path(ec);
    if (ec) {
        if (error)
            *error = "cannot resolve the temporary directory: " + ec.message();
        return false;
    }
    RemoveOrphanedCandidates(temporaryRoot);
#ifdef _WIN32
    const std::string owner = std::to_string(GetCurrentProcessId());
#else
    const std::string owner = "0";
#endif
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
    auto tree = std::make_shared<CandidateTree>(temporaryRoot / ("urk-update-" + owner + "-" + std::to_string(stamp)));
    fs::create_directories(tree->root(), ec);
    if (ec) {
        if (error)
            *error = "cannot create candidate directory " + tree->root().string() + ": " + ec.message();
        return false;
    }
    const fs::path config = inspection.projectRoot / kModConfigPath;
    if (IsRegularFile(config) && !CopyPath(config, tree->root() / kModConfigPath, error))
        return false;

    Emit(progress, "Generating " + std::string(BackendDisplayName(inspection.manifest.backend)) + " SDK " +
                       std::to_string(URK_SDK_VERSION) + " candidate in " + tree->root().string());
    const auto started = std::chrono::steady_clock::now();
    if (!Regenerate(tree->root(), inspection.manifest, error)) {
        if (error)
            *error = "candidate generation failed: " + *error;
        return false;
    }
    Emit(progress, "Candidate generated in " + ElapsedText(started));
    *candidate = std::move(tree);
    return true;
}

void SortChanges(std::vector<PlannedChange> *changes) {
    std::sort(changes->begin(), changes->end(), [](const PlannedChange &left, const PlannedChange &right) {
        return left.relativePath.generic_string() < right.relativePath.generic_string();
    });
}

bool BuildPlan(const Inspection &inspection, const CandidateTree &candidate, std::vector<PlannedChange> *changes,
               std::unordered_map<std::string, std::string> *baselines,
               std::unordered_map<std::string, std::string> *reviewed, std::string *error,
               const ProgressSink &progress) {
    GeneratedFileLedger candidateLedger;
    if (!ReadGeneratedFileLedger(candidate.root(), &candidateLedger, error)) {
        if (error)
            *error = "candidate has no generated-file ledger: " + *error;
        return false;
    }

    std::vector<std::string> candidateFiles;
    for (const char *subtree : {"sdk", "mod", "locales", "third_party", ".vscode"})
        if (!CollectFiles(candidate.root(), subtree, &candidateFiles, error))
            return false;
    for (const char *rootFile : {"CMakeLists.txt", "CMakePresets.json", ".clangd"})
        if (IsRegularFile(candidate.root() / rootFile))
            candidateFiles.emplace_back(rootFile);
    for (const auto &[path, hash] : candidateLedger.hashes)
        if (std::find(candidateFiles.begin(), candidateFiles.end(), path) == candidateFiles.end())
            candidateFiles.push_back(path);
    std::sort(candidateFiles.begin(), candidateFiles.end());
    candidateFiles.erase(std::unique(candidateFiles.begin(), candidateFiles.end()), candidateFiles.end());

    changes->clear();
    baselines->clear();
    reviewed->clear();
    std::unordered_set<std::string> candidateSet(candidateFiles.begin(), candidateFiles.end());
    std::size_t generatedCount = 0;
    std::size_t templateCount = 0;

    for (const std::string &path : candidateFiles) {
        const fs::path source = candidate.root() / path;
        const fs::path existing = inspection.projectRoot / path;
        const bool generated =
            candidateLedger.hashes.contains(path) || IsGeneratorOwnedTree(path) || path.starts_with("sdk/");
        if (!generated && (!IsEditableArea(path) || path == kModConfigPath))
            continue;

        PlannedChange change;
        change.relativePath = fs::path(path);
        change.userOwned = !generated;
        if (!HashFileSha256(source, &change.candidateHash, error) || !HashOrEmpty(existing, &change.currentHash, error))
            return false;

        if (generated) {
            ++generatedCount;
            if (change.currentHash == change.candidateHash)
                continue;
            if (change.currentHash.empty()) {
                change.kind = ChangeKind::Added;
            } else if (IsGeneratorOwnedTree(path)) {
                change.kind = ChangeKind::Modified;
            } else if (!inspection.hasGeneratedFileLedger) {
                change.kind = ChangeKind::Conflict;
                change.reason = "no generated-file ledger; cannot tell whether it was edited";
            } else if (const auto baseline = inspection.generatedFileLedger.hashes.find(path);
                       baseline == inspection.generatedFileLedger.hashes.end()) {
                change.kind = ChangeKind::Conflict;
                change.reason = "exists but was not created by the generator";
            } else if (baseline->second == change.currentHash) {
                change.kind = ChangeKind::Modified;
            } else {
                change.kind = ChangeKind::Conflict;
                change.reason = "edited since it was generated";
            }
            changes->push_back(std::move(change));
            continue;
        }

        ++templateCount;
        const auto recorded = inspection.hasEditableFileLedger ? inspection.editableFileLedger.hashes.find(path)
                                                               : inspection.editableFileLedger.hashes.end();
        const bool hasBaseline = recorded != inspection.editableFileLedger.hashes.end();
        if (change.currentHash.empty()) {
            change.kind = ChangeKind::Added;
            (*baselines)[path] = change.candidateHash;
            changes->push_back(std::move(change));
            continue;
        }
        if (change.currentHash == change.candidateHash) {
            (*baselines)[path] = change.candidateHash;
            continue;
        }
        if (hasBaseline && recorded->second == change.currentHash) {
            change.kind = ChangeKind::Modified;
            change.reason = "unchanged since it was generated; refreshed from the new template";
            (*baselines)[path] = change.candidateHash;
            changes->push_back(std::move(change));
            continue;
        }
        if (hasBaseline) {
            (*baselines)[path] = recorded->second;
            if (recorded->second == change.candidateHash)
                continue; // the user edited it, the template did not move
        }
        (*reviewed)[path] = change.candidateHash;
        if (const auto seen = inspection.reviewedTemplates.hashes.find(path);
            seen != inspection.reviewedTemplates.hashes.end() && seen->second == change.candidateHash)
            continue; // this template change was already reported
        change.kind = ChangeKind::Review;
        change.reason = hasBaseline ? "edited by you and its template changed; your copy is kept"
                                    : "differs from the current template; your copy is kept";
        changes->push_back(std::move(change));
    }

    // Generated files the candidate no longer produces.
    std::vector<std::string> staleCandidates;
    if (inspection.hasGeneratedFileLedger)
        for (const auto &[path, hash] : inspection.generatedFileLedger.hashes)
            if (!candidateSet.contains(path))
                staleCandidates.push_back(path);
    std::vector<std::string> ownedTrees;
    if (!CollectFiles(inspection.projectRoot, fs::path("sdk") / BackendName(inspection.manifest.backend), &ownedTrees,
                      error))
        return false;
    for (const std::string &path : ownedTrees)
        if (!candidateSet.contains(path))
            staleCandidates.push_back(path);
    std::sort(staleCandidates.begin(), staleCandidates.end());
    staleCandidates.erase(std::unique(staleCandidates.begin(), staleCandidates.end()), staleCandidates.end());
    for (const std::string &path : staleCandidates) {
        PlannedChange change;
        change.relativePath = fs::path(path);
        if (!HashOrEmpty(inspection.projectRoot / path, &change.currentHash, error))
            return false;
        if (change.currentHash.empty())
            continue;
        const auto baseline = inspection.generatedFileLedger.hashes.find(path);
        if (IsGeneratorOwnedTree(path) ||
            (baseline != inspection.generatedFileLedger.hashes.end() && baseline->second == change.currentHash)) {
            change.kind = ChangeKind::Removed;
        } else {
            change.kind = ChangeKind::Review;
            change.reason = "no longer generated; kept because it was edited";
        }
        changes->push_back(std::move(change));
    }

    // Keep the recorded baseline of user files the templates do not cover.
    if (inspection.hasEditableFileLedger)
        for (const auto &[path, hash] : inspection.editableFileLedger.hashes)
            if (!baselines->contains(path) && PathExists(inspection.projectRoot / path))
                (*baselines)[path] = hash;

    SortChanges(changes);
    Emit(progress, "Compared " + std::to_string(generatedCount) + " generated files and " +
                       std::to_string(templateCount) + " user-file templates");
    Emit(progress, "Plan: " + std::to_string(CountChanges(*changes, ChangeKind::Modified)) + " to replace, " +
                       std::to_string(CountChanges(*changes, ChangeKind::Added)) + " to add, " +
                       std::to_string(CountChanges(*changes, ChangeKind::Removed)) + " to remove, " +
                       std::to_string(CountChanges(*changes, ChangeKind::Conflict)) + " in conflict, " +
                       std::to_string(CountChanges(*changes, ChangeKind::Review)) + " to review");
    return true;
}

bool IsApplied(const PlannedChange &change, const ApplyOptions &options) {
    switch (change.kind) {
        case ChangeKind::Added:
        case ChangeKind::Modified:
        case ChangeKind::Removed:
            return true;
        case ChangeKind::Conflict:
            return options.overwriteConflicts;
        case ChangeKind::Review:
            return false;
    }
    return false;
}

#ifdef _WIN32
class UpdateLock {
  public:
    UpdateLock() = default;
    UpdateLock(const UpdateLock &) = delete;
    UpdateLock &operator=(const UpdateLock &) = delete;
    ~UpdateLock() {
        if (handle_ != INVALID_HANDLE_VALUE)
            CloseHandle(handle_);
    }

    // Delete-on-close: a crashed update cannot leave the project locked.
    bool Acquire(const fs::path &projectRoot, std::string *error) {
        const fs::path path = projectRoot / kLockRelativePath;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        handle_ = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (handle_ != INVALID_HANDLE_VALUE)
            return true;
        const DWORD code = GetLastError();
        if (error) {
            *error = code == ERROR_SHARING_VIOLATION
                         ? "another URKit update is running on this project"
                         : "cannot lock the project for update: " + std::system_category().message(static_cast<int>(code));
        }
        return false;
    }

  private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#else
class UpdateLock {
  public:
    bool Acquire(const fs::path &, std::string *) { return true; }
};
#endif

struct BackupEntry {
    std::string relativePath;
    bool existed = false;
};

bool WriteBackupIndex(const fs::path &backup, int fromSdk, const std::vector<BackupEntry> &entries,
                      std::string *error) {
    std::ofstream index(backup / kBackupIndexName, std::ios::binary | std::ios::trunc);
    if (!index) {
        if (error)
            *error = "cannot write " + (backup / kBackupIndexName).string();
        return false;
    }
    index << "# URKit update backup. Restored automatically if the update is interrupted.\n"
          << "from_sdk=" << fromSdk << '\n'
          << "to_sdk=" << URK_SDK_VERSION << '\n';
    for (const BackupEntry &entry : entries)
        index << "entry=" << (entry.existed ? "present" : "absent") << '\t' << entry.relativePath << '\n';
    index.flush();
    index.close();
    if (!index) {
        if (error)
            *error = "cannot finish " + (backup / kBackupIndexName).string();
        return false;
    }
    return true;
}

bool ReadBackupIndex(const fs::path &backup, std::vector<BackupEntry> *entries, std::string *error) {
    std::ifstream index(backup / kBackupIndexName, std::ios::binary);
    if (!index) {
        if (error)
            *error = "backup index is missing: " + (backup / kBackupIndexName).string();
        return false;
    }
    entries->clear();
    std::string line;
    while (std::getline(index, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.starts_with("entry="))
            continue;
        const std::string value = line.substr(6);
        const size_t tab = value.find('\t');
        if (tab == std::string::npos) {
            if (error)
                *error = "backup index has an invalid entry";
            return false;
        }
        const std::string state = value.substr(0, tab);
        const fs::path relative = fs::path(value.substr(tab + 1)).lexically_normal();
        if ((state != "present" && state != "absent") || relative.empty() || relative.is_absolute() ||
            relative.generic_string().starts_with("..")) {
            if (error)
                *error = "backup index has an invalid entry";
            return false;
        }
        entries->push_back({relative.generic_string(), state == "present"});
    }
    return true;
}

bool RestoreBackup(const fs::path &root, const fs::path &backup, std::string *error) {
    std::vector<BackupEntry> entries;
    if (!ReadBackupIndex(backup, &entries, error))
        return false;
    std::string firstError;
    for (const BackupEntry &entry : entries) {
        std::string stepError;
        const bool restored = entry.existed
                                  ? InstallFile(backup / "files" / entry.relativePath, root / entry.relativePath,
                                                &stepError)
                                  : RemoveFile(root / entry.relativePath, &stepError);
        if (!restored && firstError.empty())
            firstError = stepError;
    }
    if (!firstError.empty()) {
        if (error)
            *error = firstError;
        return false;
    }
    return true;
}

bool WriteJournal(const fs::path &root, const fs::path &backup, std::string *error) {
    const fs::path path = root / kJournalRelativePath;
    std::ofstream journal(path, std::ios::binary | std::ios::trunc);
    if (!journal) {
        if (error)
            *error = "cannot write " + path.string();
        return false;
    }
    journal << "# URKit update in progress. Removed when the update finishes or is rolled back.\n"
            << "backup=" << backup.lexically_relative(root).generic_string() << '\n';
    journal.flush();
    journal.close();
    if (!journal) {
        if (error)
            *error = "cannot finish " + path.string();
        return false;
    }
    return true;
}

bool ReadJournal(const fs::path &root, fs::path *backup, std::string *error) {
    std::string text;
    if (!ReadText(root / kJournalRelativePath, &text, error))
        return false;
    std::istringstream lines(text);
    std::string line;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!line.starts_with("backup="))
            continue;
        const fs::path relative = fs::path(line.substr(7)).lexically_normal();
        if (relative.empty() || relative.is_absolute() || !relative.generic_string().starts_with(".urk/backups/")) {
            if (error)
                *error = "update journal points outside .urk/backups";
            return false;
        }
        *backup = root / relative;
        return true;
    }
    if (error)
        *error = "update journal does not name a backup";
    return false;
}

bool StageConflictedCandidate(const UpdatePreview &preview, fs::path *stagedDirectory, std::string *error,
                              const ProgressSink &progress) {
    const Inspection &inspection = preview.inspection;
    const fs::path updates = inspection.projectRoot / ".urk" / "updates";
    std::error_code ec;
    fs::create_directories(updates, ec);
    const fs::path stage = UniqueDirectory(updates, "sdk-v" + std::to_string(URK_SDK_VERSION) + "-" + LocalStamp());
    if (!CopyPath(preview.candidate->root(), stage / "candidate", error))
        return false;
    std::error_code cleanup;
    fs::remove_all(stage / "candidate" / ".urk", cleanup);

    std::ofstream report(stage / "CONFLICTS.txt", std::ios::binary | std::ios::trunc);
    if (!report) {
        if (error)
            *error = "cannot write staged update report " + stage.string();
        return false;
    }
    report << "URKit SDK " << URK_SDK_VERSION << " update, staged\n\n"
           << "Nothing in the project was changed. candidate/ is the project as SDK " << URK_SDK_VERSION
           << " generates it; mod/ there holds the plain templates.\n"
           << "Merge the files below by hand, then run the update again, or apply it with the option that\n"
           << "overwrites conflicting generated files (they are backed up first).\n\n";
    for (const PlannedChange &change : preview.changes) {
        if (change.kind != ChangeKind::Conflict && change.kind != ChangeKind::Review)
            continue;
        report << (change.kind == ChangeKind::Conflict ? "CONFLICT " : "REVIEW   ")
               << change.relativePath.generic_string();
        if (!change.reason.empty())
            report << "  (" << change.reason << ')';
        report << '\n';
    }
    report.close();
    if (!report) {
        if (error)
            *error = "cannot finish staged update report " + stage.string();
        return false;
    }
    Emit(progress, "Staged candidate: " + stage.string());
    *stagedDirectory = stage;
    return true;
}

bool StageBackendSdkCandidate(const fs::path &projectRoot, const fs::path &stage, Backend backend,
                              const std::string &projectName, std::ostream &report, std::string *error) {
    const char *backendName = BackendName(backend);
    const fs::path candidateRoot = stage / backendName / "candidate";
    const fs::path candidateSdk = candidateRoot / "sdk" / backendName;
    const std::string reportDetails = "Staged SDK migration candidate.\n";
    if (!GenerateBackend(backend, candidateRoot, candidateSdk, projectName, projectRoot.string(), "Mods", false,
                         reportDetails, error))
        return false;

    std::vector<std::string> files;
    if (!CollectFiles(candidateRoot, "sdk", &files, error))
        return false;
    for (const std::string &path : files) {
        const fs::path relative = fs::path(path).lexically_relative("sdk");
        const fs::path existing = projectRoot / path;
        if (!PathExists(existing)) {
            report << "ADD " << backendName << '/' << path << '\n';
            continue;
        }
        std::string candidateHash;
        std::string existingHash;
        if (!HashFileSha256(candidateRoot / path, &candidateHash, error) ||
            !HashFileSha256(existing, &existingHash, error))
            return false;
        if (candidateHash != existingHash)
            report << "CHANGE " << backendName << '/' << path << '\n';
    }
    return true;
}

} // namespace

const char *ChangeKindName(ChangeKind kind) {
    switch (kind) {
        case ChangeKind::Added:
            return "add";
        case ChangeKind::Modified:
            return "replace";
        case ChangeKind::Removed:
            return "remove";
        case ChangeKind::Conflict:
            return "conflict";
        case ChangeKind::Review:
            return "review";
    }
    return "change";
}

std::size_t CountChanges(const std::vector<PlannedChange> &changes, ChangeKind kind) {
    return static_cast<std::size_t>(std::count_if(changes.begin(), changes.end(),
                                                  [kind](const PlannedChange &change) { return change.kind == kind; }));
}

std::vector<fs::path> ListEditableFiles(const fs::path &projectRoot) {
    GeneratedFileLedger ledger;
    std::string ignored;
    const bool hasLedger = ReadGeneratedFileLedger(projectRoot, &ledger, &ignored);
    std::vector<std::string> files;
    CollectFiles(projectRoot, "mod", &files, &ignored);
    CollectFiles(projectRoot, "locales", &files, &ignored);
    std::vector<fs::path> editable;
    for (const std::string &path : files)
        if (!hasLedger || !ledger.hashes.contains(path))
            editable.emplace_back(path);
    return editable;
}

bool RecordEditableBaselines(const fs::path &projectRoot, const std::vector<fs::path> &preExisting,
                             std::string *error) {
    GeneratedFileLedger recorded;
    std::string ignored;
    ReadEditableFileLedger(projectRoot, &recorded, &ignored);
    std::unordered_set<std::string> kept;
    for (const fs::path &path : preExisting)
        kept.insert(path.generic_string());

    std::unordered_map<std::string, std::string> baselines;
    for (const fs::path &path : ListEditableFiles(projectRoot)) {
        const std::string generic = path.generic_string();
        if (kept.contains(generic)) {
            if (const auto found = recorded.hashes.find(generic); found != recorded.hashes.end())
                baselines.emplace(generic, found->second);
            continue;
        }
        std::string hash;
        if (!HashFileSha256(projectRoot / path, &hash, error))
            return false;
        baselines.emplace(generic, std::move(hash));
    }
    return WriteEditableFileLedger(projectRoot, URK_SDK_VERSION, baselines, error);
}

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
            *error = "not a URKit project, CMakeLists.txt is missing: " + root.string();
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
            parsed.notices.push_back("sdk/mod_sdk.h is missing or malformed; the update recreates it.");
    } else if (!InspectLegacyProject(root, &parsed, error)) {
        return false;
    }

    if (PathExists(root / kGeneratedFileLedgerRelativePath)) {
        std::string ledgerError;
        if (ReadGeneratedFileLedger(root, &parsed.generatedFileLedger, &ledgerError)) {
            parsed.hasGeneratedFileLedger = true;
        } else {
            parsed.notices.push_back("Generated-file ledger is unreadable (" + ledgerError +
                                     "); changed generated files are treated as conflicts.");
        }
    } else {
        parsed.notices.push_back("No generated-file ledger; changed generated files are treated as conflicts.");
    }
    if (PathExists(root / kEditableFileLedgerRelativePath)) {
        std::string ledgerError;
        if (ReadEditableFileLedger(root, &parsed.editableFileLedger, &ledgerError))
            parsed.hasEditableFileLedger = true;
        else
            parsed.notices.push_back("Editable-file ledger is unreadable (" + ledgerError + ").");
    } else {
        parsed.notices.push_back("No editable-file ledger; files under mod/ are never replaced, only reported.");
    }
    if (PathExists(root / kReviewedTemplateLedgerRelativePath)) {
        std::string ignored;
        ReadReviewedTemplateLedger(root, &parsed.reviewedTemplates, &ignored);
    }
    parsed.interruptedUpdate = PathExists(root / kJournalRelativePath);
    if (parsed.interruptedUpdate)
        parsed.notices.push_back("A previous update did not finish. Its backup is restored before the next update.");

    const int projectSdk = std::max(parsed.manifest.sdkVersion, parsed.detectedSdkVersion);
    if (projectSdk > URK_SDK_VERSION) {
        if (error) {
            *error = "the project uses SDK " + std::to_string(projectSdk) + ", newer than this urk.exe (SDK " +
                     std::to_string(URK_SDK_VERSION) + "); use a newer URKit release";
        }
        return false;
    }
    parsed.updateAvailable = !parsed.hasManifest || parsed.manifest.sdkVersion < URK_SDK_VERSION ||
                             parsed.detectedSdkVersion < URK_SDK_VERSION || parsed.detectedSdkVersion == 0;

    *inspection = std::move(parsed);
    return true;
}

bool PreviewUpdate(const fs::path &projectRoot, UpdatePreview *preview, std::string *error,
                   const ProgressSink &progress) {
    if (error)
        error->clear();
    if (!preview) {
        if (error)
            *error = "update preview output is null";
        return false;
    }
    Emit(progress, "Inspecting " + projectRoot.string());
    UpdatePreview result;
    if (!Inspect(projectRoot, &result.inspection, error))
        return false;
    const Inspection &inspection = result.inspection;
    Emit(progress, std::string("Manifest: ") +
                       (inspection.hasManifest ? "SDK " + std::to_string(inspection.manifest.sdkVersion)
                                               : std::string("none, settings inferred")) +
                       ", backend " + BackendDisplayName(inspection.manifest.backend) + ", sdk/mod_sdk.h " +
                       (inspection.detectedSdkVersion > 0 ? "SDK " + std::to_string(inspection.detectedSdkVersion)
                                                          : std::string("missing")));
    Emit(progress, "Ledgers: generated " +
                       (inspection.hasGeneratedFileLedger
                            ? std::to_string(inspection.generatedFileLedger.hashes.size()) + " files"
                            : std::string("missing")) +
                       ", editable " +
                       (inspection.hasEditableFileLedger
                            ? std::to_string(inspection.editableFileLedger.hashes.size()) + " files"
                            : std::string("missing")));

    if (!BuildCandidate(inspection, &result.candidate, error, progress))
        return false;
    if (!BuildPlan(inspection, *result.candidate, &result.changes, &result.editableBaselines,
                   &result.reviewedTemplates, error, progress))
        return false;

    const bool hasWork = std::any_of(result.changes.begin(), result.changes.end(), [](const PlannedChange &change) {
        return change.kind != ChangeKind::Review;
    });
    if (hasWork && !result.inspection.updateAvailable)
        result.inspection.notices.push_back("Same SDK version, but generated files differ from this release.");
    result.inspection.updateAvailable = result.inspection.updateAvailable || hasWork;
    *preview = std::move(result);
    return true;
}

bool RecoverInterruptedUpdate(const fs::path &projectRoot, bool *recovered, fs::path *backupDirectory,
                              std::string *error) {
    if (recovered)
        *recovered = false;
    std::error_code ec;
    const fs::path root = fs::absolute(projectRoot, ec).lexically_normal();
    if (!PathExists(root / kJournalRelativePath))
        return true;
    UpdateLock lock;
    if (!lock.Acquire(root, error))
        return false;
    fs::path backup;
    if (!ReadJournal(root, &backup, error))
        return false;
    if (!RestoreBackup(root, backup, error)) {
        if (error)
            *error = "cannot restore the interrupted update from " + backup.string() + ": " + *error;
        return false;
    }
    if (!RemoveFile(root / kJournalRelativePath, error))
        return false;
    if (recovered)
        *recovered = true;
    if (backupDirectory)
        *backupDirectory = backup;
    return true;
}

bool ApplyUpdate(const UpdatePreview &preview, const ApplyOptions &options, UpdateResult *result, std::string *error,
                 const ProgressSink &progress) {
    if (error)
        error->clear();
    if (!result) {
        if (error)
            *error = "update result output is null";
        return false;
    }
    if (!preview.candidate) {
        if (error)
            *error = "the update preview has no candidate; check the project again";
        return false;
    }
    const auto started = std::chrono::steady_clock::now();
    const Inspection &inspection = preview.inspection;
    const fs::path &root = inspection.projectRoot;
    *result = {};
    result->inspection = inspection;

    UpdateLock lock;
    if (!lock.Acquire(root, error))
        return false;
    Emit(progress, std::string("Locked ") + kLockRelativePath);
    if (PathExists(root / kJournalRelativePath)) {
        if (error)
            *error = "a previous update did not finish; check the project again to restore it first";
        return false;
    }

    std::vector<const PlannedChange *> applied;
    std::vector<PlannedChange> conflicts;
    for (const PlannedChange &change : preview.changes) {
        if (change.kind == ChangeKind::Conflict)
            conflicts.push_back(change);
        if (IsApplied(change, options))
            applied.push_back(&change);
    }
    result->conflicts = conflicts;

    // Every planned file must still be what the preview saw.
    Emit(progress, "Verifying " + std::to_string(preview.changes.size()) + " planned files against the preview");
    for (const PlannedChange &change : preview.changes) {
        std::string currentHash;
        if (!HashOrEmpty(root / change.relativePath, &currentHash, error))
            return false;
        if (currentHash != change.currentHash) {
            if (error)
                *error = change.relativePath.generic_string() + " changed after the check; check the project again";
            return false;
        }
    }

    if (!conflicts.empty() && !options.overwriteConflicts) {
        Emit(progress, std::to_string(conflicts.size()) + " conflicting files; staging the update instead");
        if (!StageConflictedCandidate(preview, &result->stagedUpdateDirectory, error, progress))
            return false;
        result->staged = true;
        Emit(progress, "Project left unchanged (" + ElapsedText(started) + ")");
        return true;
    }

    const bool metadataChanges = !inspection.hasManifest || inspection.manifest.sdkVersion != URK_SDK_VERSION ||
                                 !inspection.hasEditableFileLedger || !inspection.hasGeneratedFileLedger ||
                                 CountChanges(preview.changes, ChangeKind::Review) > 0;
    if (applied.empty() && !metadataChanges) {
        Emit(progress, "Nothing to apply");
        return true;
    }

    // Backup: every file about to change plus the metadata.
    std::vector<BackupEntry> entries;
    for (const PlannedChange *change : applied)
        entries.push_back({change->relativePath.generic_string(), !change->currentHash.empty()});
    for (const char *metadata : {kProjectManifestRelativePath, kGeneratedFileLedgerRelativePath,
                                 kEditableFileLedgerRelativePath, kReviewedTemplateLedgerRelativePath})
        entries.push_back({metadata, PathExists(root / metadata)});

    const int fromSdk = inspection.detectedSdkVersion > 0 ? inspection.detectedSdkVersion : inspection.manifest.sdkVersion;
    std::error_code ec;
    const fs::path backups = root / ".urk" / "backups";
    fs::create_directories(backups, ec);
    const fs::path backup =
        UniqueDirectory(backups, LocalStamp() + "-sdk" + std::to_string(fromSdk) + "-to-" +
                                                   std::to_string(URK_SDK_VERSION));
    fs::create_directories(backup / "files", ec);
    if (ec) {
        if (error)
            *error = "cannot create backup directory " + backup.string() + ": " + ec.message();
        return false;
    }
    std::size_t backedUp = 0;
    for (const BackupEntry &entry : entries) {
        if (!entry.existed)
            continue;
        if (!CopyPath(root / entry.relativePath, backup / "files" / entry.relativePath, error))
            return false;
        ++backedUp;
    }
    if (!WriteBackupIndex(backup, fromSdk, entries, error))
        return false;
    Emit(progress, "Backup: " + backup.lexically_relative(root).generic_string() + " (" + std::to_string(backedUp) +
                       " files)");
    if (!WriteJournal(root, backup, error))
        return false;
    result->backupDirectory = backup;

    auto rollback = [&](const std::string &failure) {
        std::string restoreError;
        if (RestoreBackup(root, backup, &restoreError)) {
            std::string ignored;
            RemoveFile(root / kJournalRelativePath, &ignored);
            Emit(progress, "Rolled back from " + backup.lexically_relative(root).generic_string());
            if (error)
                *error = failure + "; the project was restored from the backup";
        } else {
            Emit(progress, "Rollback failed: " + restoreError);
            if (error)
                *error = failure + "; rollback failed (" + restoreError + "); restore by hand from " + backup.string();
        }
        result->backupDirectory.clear();
        return false;
    };

    const bool perFile = applied.size() <= kPerFileLogLimit;
    std::size_t done = 0;
    for (const PlannedChange *change : applied) {
        const fs::path destination = root / change->relativePath;
        std::string stepError;
        const bool ok = change->kind == ChangeKind::Removed
                            ? RemoveFile(destination, &stepError)
                            : InstallFile(preview.candidate->root() / change->relativePath, destination, &stepError);
        if (!ok)
            return rollback(stepError);
        if (change->kind == ChangeKind::Removed)
            ++result->filesRemoved;
        else
            ++result->filesWritten;
        ++done;
        if (perFile)
            Emit(progress, std::string(ChangeKindName(change->kind)) + "  " + change->relativePath.generic_string());
        else if (done % 100 == 0 || done == applied.size())
            Emit(progress, "Applied " + std::to_string(done) + " of " + std::to_string(applied.size()) + " files");
    }

    for (const char *metadata : {kProjectManifestRelativePath, kGeneratedFileLedgerRelativePath}) {
        std::string stepError;
        if (!InstallFile(preview.candidate->root() / metadata, root / metadata, &stepError))
            return rollback(stepError);
    }
    {
        std::string stepError;
        if (!WriteEditableFileLedger(root, URK_SDK_VERSION, preview.editableBaselines, &stepError))
            return rollback(stepError);
        if (!WriteReviewedTemplateLedger(root, URK_SDK_VERSION, preview.reviewedTemplates, &stepError))
            return rollback(stepError);
    }
    Emit(progress, "Metadata: .urk/project.ini, .urk/generated-files.ini, .urk/editable-files.ini");

    for (const PlannedChange *change : applied) {
        std::string actual;
        std::string stepError;
        if (!HashOrEmpty(root / change->relativePath, &actual, &stepError))
            return rollback(stepError);
        const std::string &expected = change->kind == ChangeKind::Removed ? std::string() : change->candidateHash;
        if (actual != expected)
            return rollback(change->relativePath.generic_string() + " does not match the candidate after writing");
    }
    Emit(progress, "Verified " + std::to_string(applied.size()) + " files against the candidate");

    Inspection updated;
    {
        std::string stepError;
        if (!Inspect(root, &updated, &stepError))
            return rollback("the updated project does not inspect cleanly: " + stepError);
    }
    {
        std::string stepError;
        if (!RemoveFile(root / kJournalRelativePath, &stepError))
            Emit(progress, "Warning: " + stepError);
    }

    // Reference copies of the templates behind files that were kept.
    if (CountChanges(preview.changes, ChangeKind::Review) > 0) {
        const fs::path templates = root / ".urk" / "templates" / ("sdk-v" + std::to_string(URK_SDK_VERSION));
        std::size_t copied = 0;
        for (const PlannedChange &change : preview.changes) {
            if (change.kind != ChangeKind::Review || change.candidateHash.empty())
                continue;
            std::string ignored;
            if (CopyPath(preview.candidate->root() / change.relativePath, templates / change.relativePath, &ignored))
                ++copied;
        }
        if (copied > 0) {
            result->templateDirectory = templates;
            Emit(progress, "Current templates of kept files: " + templates.lexically_relative(root).generic_string());
        }
    }

    result->inspection = std::move(updated);
    result->updated = true;
    Emit(progress, "Updated to SDK " + std::to_string(URK_SDK_VERSION) + " in " + ElapsedText(started));
    return true;
}

bool Update(const fs::path &projectRoot, const ApplyOptions &options, UpdateResult *result, std::string *error,
            const ProgressSink &progress) {
    bool recovered = false;
    fs::path recoveredFrom;
    if (!RecoverInterruptedUpdate(projectRoot, &recovered, &recoveredFrom, error))
        return false;
    if (recovered)
        Emit(progress, "Restored an interrupted update from " + recoveredFrom.string());
    UpdatePreview preview;
    if (!PreviewUpdate(projectRoot, &preview, error, progress))
        return false;
    if (!ApplyUpdate(preview, options, result, error, progress))
        return false;
    result->recovered = recovered;
    return true;
}

bool StageSdkMigration(const fs::path &projectRoot, fs::path *stagedUpdateDirectory, std::string *error) {
    if (error)
        error->clear();
    if (!stagedUpdateDirectory) {
        if (error)
            *error = "staged SDK migration output is null";
        return false;
    }
    std::error_code ec;
    const fs::path root = fs::absolute(projectRoot, ec).lexically_normal();
    if (ec || projectRoot.empty() || !fs::is_directory(root, ec) || ec) {
        if (error)
            *error = "project directory does not exist: " + projectRoot.string();
        return false;
    }

    const bool hasMono = IsRegularFile(root / "sdk/mono/mono_runtime.h");
    const bool hasIl2Cpp = IsRegularFile(root / "sdk/il2cpp/il2cpp_runtime.h");
    const bool hasUnreal = IsRegularFile(root / "sdk/unreal/unreal_runtime.h");
    if (!hasMono && !hasIl2Cpp && !hasUnreal) {
        if (error)
            *error = "project does not contain a Mono, IL2CPP or Unreal SDK folder to stage";
        return false;
    }

    std::string cmake;
    std::string readError;
    const std::string projectName = ReadText(root / "CMakeLists.txt", &cmake, &readError)
                                        ? ExtractCMakeProjectName(cmake)
                                        : root.filename().string();
    if (projectName.empty()) {
        if (error)
            *error = "cannot infer a project name for staged SDK migration";
        return false;
    }

    fs::create_directories(root / ".urk" / "updates", ec);
    const fs::path stage =
        UniqueDirectory(root / ".urk" / "updates", "sdk-v" + std::to_string(URK_SDK_VERSION) + "-" + LocalStamp());
    fs::create_directories(stage, ec);
    if (ec) {
        if (error)
            *error = "cannot create staged SDK migration directory " + stage.string() + ": " + ec.message();
        return false;
    }

    std::ofstream report(stage / "MIGRATION.txt", std::ios::binary | std::ios::trunc);
    if (!report) {
        if (error)
            *error = "cannot write staged SDK migration report";
        return false;
    }
    report << "URKit staged SDK migration\n\n"
           << "Nothing in the project was changed. Each backend candidate is stored below this directory.\n"
           << "Compare candidate/sdk with the project's sdk directory and merge custom changes deliberately.\n\n";
    if (hasMono && !StageBackendSdkCandidate(root, stage, Backend::Mono, projectName, report, error))
        return false;
    if (hasIl2Cpp && !StageBackendSdkCandidate(root, stage, Backend::Il2Cpp, projectName, report, error))
        return false;
    if (hasUnreal && !StageBackendSdkCandidate(root, stage, Backend::Unreal, projectName, report, error))
        return false;
    report.close();
    if (!report) {
        if (error)
            *error = "cannot finish staged SDK migration report";
        return false;
    }
    *stagedUpdateDirectory = stage;
    return true;
}

std::string Describe(const Inspection &inspection) {
    std::ostringstream out;
    out << "Project      " << inspection.manifest.projectName << '\n'
        << "Path         " << inspection.projectRoot.string() << '\n'
        << "Backend      " << BackendDisplayName(inspection.manifest.backend) << '\n'
        << "Project SDK  " << (inspection.detectedSdkVersion > 0 ? std::to_string(inspection.detectedSdkVersion) : "?")
        << '\n'
        << "Release SDK  " << URK_SDK_VERSION << '\n'
        << "Manifest     " << (inspection.hasManifest ? "present" : "missing, created by the update") << '\n'
        << "Ledgers      generated " << (inspection.hasGeneratedFileLedger ? "present" : "missing") << ", editable "
        << (inspection.hasEditableFileLedger ? "present" : "missing") << '\n'
        << "Status       " << (inspection.updateAvailable ? "update available" : "up to date");
    for (const std::string &notice : inspection.notices)
        out << "\nNote         " << notice;
    return out.str();
}

std::string DescribeChanges(const std::vector<PlannedChange> &changes) {
    if (changes.empty())
        return "No file changes.";
    std::ostringstream out;
    out << changes.size() << (changes.size() == 1 ? " file:" : " files:");
    for (const PlannedChange &change : changes) {
        out << '\n' << ChangeKindName(change.kind);
        for (size_t pad = std::string_view(ChangeKindName(change.kind)).size(); pad < 9; ++pad)
            out << ' ';
        out << change.relativePath.generic_string();
        if (!change.reason.empty())
            out << "  (" << change.reason << ')';
    }
    return out.str();
}

} // namespace UrkProject
