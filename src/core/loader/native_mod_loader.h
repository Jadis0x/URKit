#pragma once

#include "config.h"
#include "mod_sdk.h"

#include <cstddef>
#include <string>
#include <vector>

struct NativeModLoadPlan {
    std::vector<std::string> paths;

    bool Empty() const noexcept { return paths.empty(); }
};

constexpr bool NativeMods_RequiresRuntimeEvents(bool safeMode, std::size_t candidateCount) noexcept {
    return !safeMode && candidateCount != 0;
}

// Candidate discovery is intentionally separate from runtime hook activation.
// A process with no mods must not be modified merely because a proxy was loaded.
NativeModLoadPlan NativeMods_Discover(const Config &config);
void NativeMods_Load(const NativeModLoadPlan &plan, const URK_ModContext &context);
