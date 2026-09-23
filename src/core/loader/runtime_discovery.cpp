#include "runtime_discovery.h"

#include "unreal_sdk_api.h"

#include <windows.h>

#include <array>
#include <cstring>

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

// Static imports of the process image, read from its own mapped headers.
bool MainImageImports(const char *dll) {
    const auto *base = reinterpret_cast<const std::uint8_t *>(GetModuleHandleW(nullptr));
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const IMAGE_DATA_DIRECTORY &imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (imports.VirtualAddress == 0)
        return false;
    for (auto *entry = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(base + imports.VirtualAddress); entry->Name;
         ++entry) {
        if (_stricmp(reinterpret_cast<const char *>(base + entry->Name), dll) == 0)
            return true;
    }
    return false;
}

bool IsUnityImage() {
    static const bool unity = MainImageImports("UnityPlayer.dll");
    return unity;
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
        !IsUnityImage() && UnrealPresence().WorthScanning(),
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
