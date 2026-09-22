#include "runtime_discovery.h"

#include "unreal_sdk_api.h"

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

// Version resource and module layout, not the bootstrap scan. Cached there.
const URK::Unreal::UnrealPresence &UnrealPresence() {
    return URK::Unreal::UnrealEngine::Instance().Presence();
}

const char *UnrealReason() {
    const URK::Unreal::UnrealPresence &presence = UnrealPresence();
    return presence.reason.empty() ? "Unreal Engine image detected" : presence.reason.c_str();
}
} // namespace

RuntimeModuleSnapshot RuntimeDiscovery_Snapshot() {
    return {
        ModuleLoaded("UnityPlayer.dll"),
        ModuleLoaded("GameAssembly.dll"),
        MonoLoaded(),
        UnrealPresence().WorthScanning(),
    };
}

const char *RuntimeDiscovery_QualificationReason(const RuntimeModuleSnapshot &snapshot) {
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
    case RuntimeModuleKind::Il2Cpp:
        return "GameAssembly.dll loaded";
    case RuntimeModuleKind::Mono:
        return "Mono runtime module loaded";
    case RuntimeModuleKind::Unreal:
        return UnrealReason();
    case RuntimeModuleKind::None:
        return snapshot.unityPlayerLoaded ? "UnityPlayer.dll loaded" : "no supported runtime in the current process";
    }
    return "no supported runtime in the current process";
}

const char *RuntimeDiscovery_RuntimeReason(const RuntimeModuleSnapshot &snapshot) {
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
    case RuntimeModuleKind::Il2Cpp:
        return "GameAssembly.dll loaded";
    case RuntimeModuleKind::Mono:
        return "Mono runtime module loaded";
    case RuntimeModuleKind::Unreal:
        return UnrealReason();
    case RuntimeModuleKind::None:
        return "no loaded scripting runtime module";
    }
    return "no loaded scripting runtime module";
}
