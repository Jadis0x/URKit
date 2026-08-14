#pragma once

#include "../project_version.h"

namespace UrkUpdater {

// Keep this in sync with the release tag that publishes urk-updater.exe.
inline constexpr std::string_view kVersion = UrkVersion::kReleaseVersion;

} // namespace UrkUpdater
