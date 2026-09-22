#pragma once

#include <string>

struct ProcessQualification {
    bool isSupportedRuntime = false;
    bool unityPlayerLoaded = false;
    bool il2cppLoaded = false;
    bool monoLoaded = false;
    bool unrealDetected = false;
    std::string reason;
};

// Uses modules loaded in the current process, plus the Unreal image check.
// Files beside the executable are deliberately ignored so launchers and helper
// processes cannot qualify merely because they share the game directory.
ProcessQualification ProcessQualification_WaitForRuntime(unsigned timeoutMs);
