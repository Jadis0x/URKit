#include "runtime_discovery.h"

#include <windows.h>

#include <array>

namespace {
bool ModuleLoaded(const char *name) {
    return name && GetModuleHandleA(name) != nullptr;
}

bool MonoLoaded() {
    constexpr std::array<const char *, 4> candidates{
        "mono-2.0-bdwgc.dll",
        "mono-2.0-sgen.dll",
        "mono-2.0.dll",
        "mono.dll",
    };
    for (const char *candidate : candidates) {
        if (ModuleLoaded(candidate))
            return true;
    }
    return false;
}
} // namespace

RuntimeModuleSnapshot RuntimeDiscovery_Snapshot() {
    return {
        ModuleLoaded("UnityPlayer.dll"),
        ModuleLoaded("GameAssembly.dll"),
        MonoLoaded(),
    };
}

const char *RuntimeDiscovery_UnityReason(const RuntimeModuleSnapshot &snapshot) noexcept {
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
    case RuntimeModuleKind::Il2Cpp:
        return "GameAssembly.dll loaded";
    case RuntimeModuleKind::Mono:
        return "Mono runtime module loaded";
    case RuntimeModuleKind::None:
        return snapshot.unityPlayerLoaded ? "UnityPlayer.dll loaded" : "no Unity runtime module loaded";
    }
    return "no Unity runtime module loaded";
}

const char *RuntimeDiscovery_RuntimeReason(const RuntimeModuleSnapshot &snapshot) noexcept {
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
    case RuntimeModuleKind::Il2Cpp:
        return "GameAssembly.dll loaded";
    case RuntimeModuleKind::Mono:
        return "Mono runtime module loaded";
    case RuntimeModuleKind::None:
        return "no loaded scripting runtime module";
    }
    return "no loaded scripting runtime module";
}
