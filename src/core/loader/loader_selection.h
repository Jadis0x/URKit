#pragma once

#include <string>
#include <vector>

struct LoaderSelection {
    std::string configPath;
    std::vector<std::string> modPaths;
};

// Loader path picker. False when cancelled or invalid.
bool Loader_SelectPaths(LoaderSelection *selection);
