#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace UrkUpdater {

struct AvailableUpdate {
    std::string currentVersion;
    std::string availableVersion;
    std::string releaseUrl;
    std::string downloadUrl;
    std::string sha256;
};

struct UpdateCheckResult {
    std::string currentVersion;
    std::string latestVersion;
    std::string releaseUrl;
    bool updateAvailable = false;
};

bool CheckForUpdate(UpdateCheckResult *result, AvailableUpdate *available, std::string *error);
bool DownloadAndRestart(const AvailableUpdate &update, std::string *error);
bool ApplyDownloadedUpdate(const std::filesystem::path &source, const std::filesystem::path &target,
                           const std::string &expectedSha256, uint32_t waitForProcessId, std::string *error);

} // namespace UrkUpdater
