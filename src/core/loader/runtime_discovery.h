#pragma once

enum class RuntimeModuleKind {
    None,
    Il2Cpp,
    Mono,
};

struct RuntimeModuleSnapshot {
    bool unityPlayerLoaded = false;
    bool il2cppLoaded = false;
    bool monoLoaded = false;

    constexpr bool IsUnityProcess() const noexcept {
        return unityPlayerLoaded || il2cppLoaded || monoLoaded;
    }
};

constexpr RuntimeModuleKind RuntimeDiscovery_SelectRuntime(const RuntimeModuleSnapshot &snapshot) noexcept {
    if (snapshot.il2cppLoaded)
        return RuntimeModuleKind::Il2Cpp;
    if (snapshot.monoLoaded)
        return RuntimeModuleKind::Mono;
    return RuntimeModuleKind::None;
}

RuntimeModuleSnapshot RuntimeDiscovery_Snapshot();
const char *RuntimeDiscovery_UnityReason(const RuntimeModuleSnapshot &snapshot) noexcept;
const char *RuntimeDiscovery_RuntimeReason(const RuntimeModuleSnapshot &snapshot) noexcept;
