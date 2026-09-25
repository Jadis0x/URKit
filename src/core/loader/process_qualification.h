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

// Uses loaded modules only, so launchers in the game folder don't qualify.
ProcessQualification ProcessQualification_WaitForRuntime(unsigned timeoutMs);
