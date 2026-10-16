#pragma once

#include "project_manifest.h"
#include "project_ledger.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace UrkProject {

// Text the generators put in sdk/<backend>/README.md. New projects and updates must pass the same
// string, or every update would report that file as changed.
inline constexpr const char *kSdkReportDetails =
    "Generated projects use runtime API helpers. No offline metadata or dump-generated modules are emitted.";

struct Inspection {
    std::filesystem::path projectRoot;
    Manifest manifest;
    int detectedSdkVersion = 0;
    bool hasManifest = false;
    bool hasGeneratedFileLedger = false;
    GeneratedFileLedger generatedFileLedger;
    bool hasEditableFileLedger = false;
    GeneratedFileLedger editableFileLedger;
    GeneratedFileLedger reviewedTemplates;
    // A previous update stopped between its backup and its last write.
    bool interruptedUpdate = false;
    bool updateAvailable = false;
    std::vector<std::string> notices;
};

enum class ChangeKind {
    Added,    // the generator now produces a file the project does not have
    Modified, // the file is unchanged since it was generated, so it can be replaced
    Removed,  // no longer generated and unchanged since it was generated
    Conflict, // generator-owned, but edited or without a recorded baseline
    Review,   // user-owned and kept as is; the template it came from has changed
};

struct PlannedChange {
    std::filesystem::path relativePath;
    ChangeKind kind = ChangeKind::Modified;
    // SHA-256 of the project file when the plan was made; empty when it did not exist.
    std::string currentHash;
    // SHA-256 of the candidate file; empty for Removed.
    std::string candidateHash;
    std::string reason;
    bool userOwned = false;
};

// Generated project held in a temporary directory between preview and apply.
class CandidateTree;

struct UpdatePreview {
    Inspection inspection;
    std::vector<PlannedChange> changes;
    std::shared_ptr<const CandidateTree> candidate;
    // Template hashes recorded for user-owned files once the update is applied.
    std::unordered_map<std::string, std::string> editableBaselines;
    // Template hashes of kept files, recorded so the same change is reported once.
    std::unordered_map<std::string, std::string> reviewedTemplates;
};

struct ApplyOptions {
    // Replace edited generator-owned files instead of staging the update. Each one is backed up first.
    bool overwriteConflicts = false;
};

struct UpdateResult {
    Inspection inspection;
    std::filesystem::path backupDirectory;
    std::filesystem::path stagedUpdateDirectory;
    std::filesystem::path templateDirectory;
    std::vector<PlannedChange> conflicts;
    std::size_t filesWritten = 0;
    std::size_t filesRemoved = 0;
    bool updated = false;
    bool staged = false;
    bool recovered = false;
};

// Receives one line per step. Called on the thread that runs the update.
using ProgressSink = std::function<void(const std::string &line)>;

bool Inspect(const std::filesystem::path &projectRoot, Inspection *inspection, std::string *error);
bool PreviewUpdate(const std::filesystem::path &projectRoot, UpdatePreview *preview, std::string *error,
                   const ProgressSink &progress = {});
// Applies exactly the previewed plan. Refuses when a planned file changed after the preview.
bool ApplyUpdate(const UpdatePreview &preview, const ApplyOptions &options, UpdateResult *result, std::string *error,
                 const ProgressSink &progress = {});
// Recovery, preview and apply in one call.
bool Update(const std::filesystem::path &projectRoot, const ApplyOptions &options, UpdateResult *result,
            std::string *error, const ProgressSink &progress = {});
// Restores the files of an update that was interrupted after its backup was taken.
bool RecoverInterruptedUpdate(const std::filesystem::path &projectRoot, bool *recovered,
                              std::filesystem::path *backupDirectory, std::string *error);
bool StageSdkMigration(const std::filesystem::path &projectRoot, std::filesystem::path *stagedUpdateDirectory,
                       std::string *error);
// Hashes the user-owned files a fresh generation created; files in preExisting keep their recorded baseline.
bool RecordEditableBaselines(const std::filesystem::path &projectRoot,
                             const std::vector<std::filesystem::path> &preExisting, std::string *error);
std::vector<std::filesystem::path> ListEditableFiles(const std::filesystem::path &projectRoot);

const char *ChangeKindName(ChangeKind kind);
std::size_t CountChanges(const std::vector<PlannedChange> &changes, ChangeKind kind);
std::string Describe(const Inspection &inspection);
std::string DescribeChanges(const std::vector<PlannedChange> &changes);

} // namespace UrkProject
