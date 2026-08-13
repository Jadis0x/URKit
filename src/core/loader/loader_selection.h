#pragma once

#include <string>
#include <vector>

struct LoaderSelection {
    std::string configPath;
    std::vector<std::string> modPaths;
};

// Shows the injected-loader path selection UI. Returns false when the user
// cancels or when no valid selection was made.
bool Loader_SelectPaths(LoaderSelection *selection);
