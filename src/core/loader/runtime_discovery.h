#pragma once

enum class RuntimeModuleKind {
    None,
    Il2Cpp,
    Mono,
    Unreal,
};

struct RuntimeModuleSnapshot {
    bool unityPlayerLoaded = false;
    bool il2cppLoaded = false;
    bool monoLoaded = false;
    bool unrealDetected = false;

    constexpr bool IsUnityProcess() const noexcept {
        return unityPlayerLoaded || il2cppLoaded || monoLoaded;
    }

    constexpr bool IsSupportedProcess() const noexcept {
        return IsUnityProcess() || unrealDetected;
    }
};

// A loaded Unity module outranks Unreal: the module is proof, detection a claim.
constexpr RuntimeModuleKind RuntimeDiscovery_SelectRuntime(const RuntimeModuleSnapshot &snapshot) noexcept {
    if (snapshot.il2cppLoaded)
        return RuntimeModuleKind::Il2Cpp;
    if (snapshot.monoLoaded)
        return RuntimeModuleKind::Mono;
    if (snapshot.unrealDetected)
        return RuntimeModuleKind::Unreal;
    return RuntimeModuleKind::None;
}

RuntimeModuleSnapshot RuntimeDiscovery_Snapshot();
const char *RuntimeDiscovery_QualificationReason(const RuntimeModuleSnapshot &snapshot);
const char *RuntimeDiscovery_RuntimeReason(const RuntimeModuleSnapshot &snapshot);
