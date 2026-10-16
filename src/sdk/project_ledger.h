#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace UrkProject {

inline constexpr int kGeneratedFileLedgerSchemaVersion = 1;
inline constexpr const char *kGeneratedFileLedgerRelativePath = ".urk/generated-files.ini";
// Hashes of the template each user-owned file was created from, so an update can tell an
// untouched file (safe to refresh) from one the user edited (kept as is).
inline constexpr const char *kEditableFileLedgerRelativePath = ".urk/editable-files.ini";
// Template hashes already reported for files the user kept, so the next update stays quiet
// until the template changes again.
inline constexpr const char *kReviewedTemplateLedgerRelativePath = ".urk/reviewed-templates.ini";

struct GeneratedFileLedger {
    int schemaVersion = kGeneratedFileLedgerSchemaVersion;
    int sdkVersion = 0;
    std::unordered_map<std::string, std::string> hashes;
};

bool HashFileSha256(const std::filesystem::path &path, std::string *hash, std::string *error);
bool ReadGeneratedFileLedger(const std::filesystem::path &projectRoot, GeneratedFileLedger *ledger,
                             std::string *error);
bool WriteGeneratedFileLedger(const std::filesystem::path &projectRoot, int sdkVersion,
                              const std::vector<std::filesystem::path> &relativePaths, std::string *error);
bool ReadEditableFileLedger(const std::filesystem::path &projectRoot, GeneratedFileLedger *ledger, std::string *error);
bool WriteEditableFileLedger(const std::filesystem::path &projectRoot, int sdkVersion,
                             const std::unordered_map<std::string, std::string> &hashes, std::string *error);
bool ReadReviewedTemplateLedger(const std::filesystem::path &projectRoot, GeneratedFileLedger *ledger,
                                std::string *error);
bool WriteReviewedTemplateLedger(const std::filesystem::path &projectRoot, int sdkVersion,
                                 const std::unordered_map<std::string, std::string> &hashes, std::string *error);

} // namespace UrkProject
