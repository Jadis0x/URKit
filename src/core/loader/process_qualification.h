#pragma once

#include <string>

struct ProcessQualification {
    bool isUnityProcess = false;
    bool unityPlayerLoaded = false;
    bool il2cppLoaded = false;
    bool monoLoaded = false;
    std::string reason;
};

// Uses modules loaded in the current process. Files beside the executable are
// deliberately ignored so launchers and helper processes cannot qualify merely
// because they share the game directory.
ProcessQualification ProcessQualification_WaitForUnity(unsigned timeoutMs);
