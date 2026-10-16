#pragma once

#include <optional>
#include <string>
#include <vector>

namespace UrkApp {

// Runs a command-line verb. Returns nullopt when the arguments ask for the window.
std::optional<int> RunCommandLine(const std::vector<std::wstring> &arguments);

} // namespace UrkApp
