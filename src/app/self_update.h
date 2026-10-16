#pragma once

#include "activity_log.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace UrkApp {

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

// Release asset that replaces the running executable.
inline constexpr const char *kReleaseAssetName = "urk.exe";

bool CheckForUpdate(UpdateCheckResult *result, AvailableUpdate *available, std::string *error);
// Downloads, verifies the published SHA-256 and starts a helper that swaps urk.exe once this process exits.
// With restart the new urk.exe is started afterwards.
bool DownloadAndRestart(const AvailableUpdate &update, const LogSink &log, bool restart, std::string *error);
bool ApplyDownloadedUpdate(const std::filesystem::path &source, const std::filesystem::path &target,
                           const std::string &expectedSha256, uint32_t waitForProcessId, bool restart,
                           std::string *error);
// Best effort: drop the helper copy a previous self-update left behind.
void RemoveStaleSelfUpdateHelper() noexcept;

} // namespace UrkApp
