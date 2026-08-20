#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace UrkProject {

inline constexpr int kGeneratedFileLedgerSchemaVersion = 1;
inline constexpr const char *kGeneratedFileLedgerRelativePath = ".urk/generated-files.ini";

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

} // namespace UrkProject
