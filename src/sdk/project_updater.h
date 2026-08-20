#pragma once

#include "project_manifest.h"
#include "project_ledger.h"

#include <filesystem>
#include <string>
#include <vector>

namespace UrkProject {

struct Inspection {
    std::filesystem::path projectRoot;
    Manifest manifest;
    int detectedSdkVersion = 0;
    bool hasManifest = false;
    bool hasGeneratedFileLedger = false;
    GeneratedFileLedger generatedFileLedger;
    bool updateAvailable = false;
    std::vector<std::string> notices;
};

enum class ChangeKind {
    Added,
    Modified,
    Conflict,
};

struct PlannedChange {
    std::filesystem::path relativePath;
    ChangeKind kind = ChangeKind::Modified;
};

struct UpdatePreview {
    Inspection inspection;
    std::vector<PlannedChange> changes;
};

struct UpdateResult {
    Inspection inspection;
    std::filesystem::path backupDirectory;
    std::filesystem::path stagedUpdateDirectory;
    std::vector<PlannedChange> conflicts;
    bool updated = false;
    bool staged = false;
};

bool Inspect(const std::filesystem::path &projectRoot, Inspection *inspection, std::string *error);
bool PreviewUpdate(const std::filesystem::path &projectRoot, UpdatePreview *preview, std::string *error);
bool Update(const std::filesystem::path &projectRoot, UpdateResult *result, std::string *error);
bool StageSdkMigration(const std::filesystem::path &projectRoot, std::filesystem::path *stagedUpdateDirectory,
                       std::string *error);
std::string Describe(const Inspection &inspection);
std::string DescribeChanges(const std::vector<PlannedChange> &changes);

} // namespace UrkProject
