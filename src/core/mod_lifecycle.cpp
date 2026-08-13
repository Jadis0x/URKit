#include "mod_lifecycle.h"
#include "callback_guard.h"
#include "hook_manager.h"
#include "loader.h"
#include "loader/main_thread_dispatcher.h"
#include "loader/window_message_dispatcher.h"
#include "loader/runtime_events.h"
#include "logger.h"
#include "network_http.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace {
using ModShutdownFn = void (*)();

struct LoadedNativeMod {
    HMODULE module = nullptr;
    ModShutdownFn shutdown = nullptr;
    URK_OnSceneLoadedFn onSceneLoaded = nullptr;
    URK_OnSceneChangedFn onSceneChanged = nullptr;
    URK_OnObjectDestroyRequestedFn onObjectDestroyRequested = nullptr;
    std::string path;
};

std::mutex g_loadedModsMutex;
std::vector<LoadedNativeMod> g_loadedMods;
std::atomic_bool g_shutdownStarted{false};

std::string ModulePath(HMODULE module, const char *fallback) {
    char path[MAX_PATH]{};
    if (module && GetModuleFileNameA(module, path, MAX_PATH) && path[0])
        return path;
    return fallback && fallback[0] ? fallback : "<unknown mod>";
}

void RegisterLoadedNativeMod(HMODULE module, const char *path) {
    if (!module || g_shutdownStarted.load(std::memory_order_acquire))
        return;

    const auto shutdown = reinterpret_cast<ModShutdownFn>(GetProcAddress(module, "ModShutdown"));
    const auto onSceneLoaded = reinterpret_cast<URK_OnSceneLoadedFn>(GetProcAddress(module, "OnSceneLoaded"));
    const auto onSceneChanged = reinterpret_cast<URK_OnSceneChangedFn>(GetProcAddress(module, "OnSceneChanged"));
    const auto onObjectDestroyRequested =
        reinterpret_cast<URK_OnObjectDestroyRequestedFn>(GetProcAddress(module, "OnObjectDestroyRequested"));
    const std::string modulePath = ModulePath(module, path);

    std::lock_guard<std::mutex> lock(g_loadedModsMutex);
    auto existing = std::find_if(g_loadedMods.begin(), g_loadedMods.end(),
                                 [module](const LoadedNativeMod &mod) { return mod.module == module; });

    if (existing != g_loadedMods.end()) {
        existing->shutdown = shutdown;
        existing->onSceneLoaded = onSceneLoaded;
        existing->onSceneChanged = onSceneChanged;
        existing->onObjectDestroyRequested = onObjectDestroyRequested;
        existing->path = modulePath;
        return;
    }

    g_loadedMods.push_back(
        {module, shutdown, onSceneLoaded, onSceneChanged, onObjectDestroyRequested, modulePath});

        Log(shutdown ? "  [SUCCESS][lifecycle] %s: optional ModShutdown resolved." : "  [INFO][lifecycle] %s: no ModShutdown export.",
        modulePath.c_str());
    if (onSceneLoaded || onSceneChanged) {
        Log("  [SUCCESS][lifecycle] %s: scene callbacks resolved (OnSceneLoaded=%s, "
            "OnSceneChanged=%s).",
            modulePath.c_str(), onSceneLoaded ? "yes" : "no", onSceneChanged ? "yes" : "no");
    }
    if (onObjectDestroyRequested) {
        Log("  [SUCCESS][lifecycle] %s: object destroy request callback resolved.", modulePath.c_str());
    }
}

void UnregisterLoadedNativeMod(HMODULE module) {
    if (!module)
        return;

    std::lock_guard<std::mutex> lock(g_loadedModsMutex);
    g_loadedMods.erase(std::remove_if(g_loadedMods.begin(), g_loadedMods.end(),
                                      [module](const LoadedNativeMod &mod) { return mod.module == module; }),
                       g_loadedMods.end());
}

void DeactivateLoadedNativeMod(HMODULE module) {
    if (!module)
        return;

    std::lock_guard<std::mutex> lock(g_loadedModsMutex);
    auto mod = std::find_if(g_loadedMods.begin(), g_loadedMods.end(),
                            [module](const LoadedNativeMod &candidate) { return candidate.module == module; });
    if (mod == g_loadedMods.end())
        return;
    mod->shutdown = nullptr;
    mod->onSceneLoaded = nullptr;
    mod->onSceneChanged = nullptr;
    mod->onObjectDestroyRequested = nullptr;
}

bool InvokeShutdownSafely(const LoadedNativeMod &mod, DWORD *exceptionCode, std::string *cppError) {
    __try {
        return urk::guard::InvokeCpp([&] { mod.shutdown(); }, cppError);
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

bool InvokeSceneLoadedSafely(const LoadedNativeMod &mod, const URK_SceneInfo &scene, DWORD *exceptionCode,
                             std::string *cppError) {
    __try {
        return urk::guard::InvokeCpp([&] { mod.onSceneLoaded(&scene); }, cppError);
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

bool InvokeSceneChangedSafely(const LoadedNativeMod &mod, const URK_SceneInfo &previousScene,
                              const URK_SceneInfo &currentScene, DWORD *exceptionCode, std::string *cppError) {
    __try {
        return urk::guard::InvokeCpp([&] { mod.onSceneChanged(&previousScene, &currentScene); }, cppError);
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

bool InvokeObjectDestroyRequestedSafely(const LoadedNativeMod &mod, const URK_ObjectDestroyRequest &request,
                                         DWORD *exceptionCode, std::string *cppError) {
    __try {
        return urk::guard::InvokeCpp([&] { mod.onObjectDestroyRequested(&request); }, cppError);
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

std::vector<LoadedNativeMod> SnapshotLoadedMods() {
    std::lock_guard<std::mutex> lock(g_loadedModsMutex);
    return g_loadedMods;
}

void ShutdownNativeMods(const std::vector<LoadedNativeMod> &mods) {
    if (mods.empty()) {
        Log("[mods] no native mods registered for shutdown.");
        return;
    }

    Log("[mods] shutting down %zu native mods in reverse load order.", mods.size());

    int called = 0;
    int skipped = 0;
    int failed = 0;

    for (auto it = mods.rbegin(); it != mods.rend(); ++it) {
        if (!it->shutdown) {
            ++skipped;
            MainThread_UnregisterModule(it->module);
            WindowMessage_UnregisterModule(it->module);
            RuntimeEvents_UnregisterModule(it->module);
            if (HookManager_DetachModule(it->module) < 0) {
                ++failed;
                Log("  [FAILED] %s: one or more hooks could not be detached; module "
                    "will remain mapped until process exit.",
                    it->path.c_str());
            }
            continue;
        }

        DWORD exceptionCode = 0;
        std::string cppError;
        if (InvokeShutdownSafely(*it, &exceptionCode, &cppError)) {
            ++called;
            Log("  [SHUTDOWN] %s", it->path.c_str());
        } else {
            ++failed;
            if (!cppError.empty())
                Log("  [FAILED] %s: ModShutdown threw a C++ exception: %s.", it->path.c_str(), cppError.c_str());
            else
                Log("  [FAILED] %s: ModShutdown crashed with native exception 0x%08lX.", it->path.c_str(),
                    exceptionCode);
        }
        MainThread_UnregisterModule(it->module);
        WindowMessage_UnregisterModule(it->module);
        RuntimeEvents_UnregisterModule(it->module);
        if (HookManager_DetachModule(it->module) < 0) {
            ++failed;
            Log("  [FAILED] %s: one or more hooks could not be detached; module "
                "will remain mapped until process exit.",
                it->path.c_str());
        }
    }

    Log("[mods] shutdown complete: %d called, %d skipped, %d failed.", called, skipped, failed);
}

bool ShutdownOneNativeMod(const LoadedNativeMod &mod, const char *reason) {
    Log("  [UNLOAD] %s: %s", mod.path.c_str(), reason && reason[0] ? reason : "requested");

    if (mod.shutdown) {
        DWORD exceptionCode = 0;
        std::string cppError;
        if (InvokeShutdownSafely(mod, &exceptionCode, &cppError)) {
            Log("  [SHUTDOWN] %s", mod.path.c_str());
        } else {
            if (!cppError.empty())
                Log("  [FAILED] %s: ModShutdown threw a C++ exception: %s.", mod.path.c_str(), cppError.c_str());
            else
                Log("  [FAILED] %s: ModShutdown crashed with native exception 0x%08lX.", mod.path.c_str(),
                    exceptionCode);
        }
    } else {
        Log("  [lifecycle] %s: no ModShutdown export before unload.", mod.path.c_str());
    }

    MainThread_UnregisterModule(mod.module);
    RuntimeEvents_UnregisterModule(mod.module);
    if (WindowMessage_UnregisterModule(mod.module) < 0) {
        Log("  [FAILED] %s: unload refused because a window-message callback is executing on the caller.",
            mod.path.c_str());
        return false;
    }
    if (HookManager_DetachModule(mod.module) < 0) {
        Log("  [FAILED] %s: unload refused because one or more hooks still point "
            "into the module.",
            mod.path.c_str());
        return false;
    }

    if (::FreeLibrary(mod.module) == FALSE) {
        Log("  [FAILED] %s: FreeLibrary failed with error=%lu.", mod.path.c_str(), GetLastError());
        return false;
    }
    return true;
}
} // namespace

extern "C" HMODULE WINAPI URK_LoadLibraryExA(LPCSTR fileName, HANDLE file, DWORD flags) {
    HMODULE module = ::LoadLibraryExA(fileName, file, flags);
    if (module)
        RegisterLoadedNativeMod(module, fileName);
    return module;
}

extern "C" BOOL WINAPI URK_FreeLibrary(HMODULE module) {
    if (!module) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }

    MainThread_UnregisterModule(module);
    RuntimeEvents_UnregisterModule(module);
    if (WindowMessage_UnregisterModule(module) < 0) {
        Log("[mods][ERROR] FreeLibrary refused for module=%p: a window-message callback is executing on the caller.",
            module);
        DeactivateLoadedNativeMod(module);
        SetLastError(ERROR_BUSY);
        return FALSE;
    }
    if (HookManager_DetachModule(module) < 0) {
        Log("[mods][ERROR] FreeLibrary refused for module=%p: one or more hooks "
            "could not be detached and still target the module.",
            module);
        DeactivateLoadedNativeMod(module);
        SetLastError(ERROR_BUSY);
        return FALSE;
    }

    const BOOL freed = ::FreeLibrary(module);
    if (!freed) {
        const DWORD error = GetLastError();
        Log("[mods][ERROR] FreeLibrary failed for module=%p error=%lu.", module, error);
        DeactivateLoadedNativeMod(module);
        SetLastError(error);
        return FALSE;
    }
    UnregisterLoadedNativeMod(module);
    return TRUE;
}

void Loader_Shutdown() {
    if (g_shutdownStarted.exchange(true, std::memory_order_acq_rel))
        return;

    RuntimeEvents_StopWorkers();

    std::vector<LoadedNativeMod> mods;
    {
        std::lock_guard<std::mutex> lock(g_loadedModsMutex);
        mods.swap(g_loadedMods);
    }

    ShutdownNativeMods(mods);
    NetworkHttp_Shutdown();
}

bool ModLifecycle_UnloadModule(HMODULE module, const char *reason) {
    if (!module || g_shutdownStarted.load(std::memory_order_acquire))
        return false;

    LoadedNativeMod mod{};
    {
        std::lock_guard<std::mutex> lock(g_loadedModsMutex);
        auto it = std::find_if(g_loadedMods.begin(), g_loadedMods.end(),
                               [module](const LoadedNativeMod &candidate) { return candidate.module == module; });
        if (it == g_loadedMods.end())
            return false;
        mod = *it;
    }

    const bool unloaded = ShutdownOneNativeMod(mod, reason);
    std::lock_guard<std::mutex> lock(g_loadedModsMutex);
    auto it = std::find_if(g_loadedMods.begin(), g_loadedMods.end(),
                           [module](const LoadedNativeMod &candidate) { return candidate.module == module; });
    if (it == g_loadedMods.end())
        return unloaded;
    if (unloaded) {
        g_loadedMods.erase(it);
    } else {
        // Shutdown already ran. Keep the still-mapped module tracked for a later
        // detach/unload retry, but prevent callbacks or a second shutdown call.
        it->shutdown = nullptr;
        it->onSceneLoaded = nullptr;
        it->onSceneChanged = nullptr;
        it->onObjectDestroyRequested = nullptr;
    }
    return unloaded;
}

void ModLifecycle_DispatchSceneLoaded(const URK_SceneInfo &scene) {
    if (g_shutdownStarted.load(std::memory_order_acquire))
        return;

    for (const LoadedNativeMod &mod : SnapshotLoadedMods()) {
        if (!mod.onSceneLoaded)
            continue;

        DWORD exceptionCode = 0;
        std::string cppError;
        if (!InvokeSceneLoadedSafely(mod, scene, &exceptionCode, &cppError)) {
            if (!cppError.empty())
                Log("  [FAILED] %s: OnSceneLoaded threw a C++ exception: %s.", mod.path.c_str(), cppError.c_str());
            else
                Log("  [FAILED] %s: OnSceneLoaded crashed with native exception 0x%08lX.", mod.path.c_str(),
                    exceptionCode);
        }
    }
}

void ModLifecycle_DispatchSceneChanged(const URK_SceneInfo &previousScene, const URK_SceneInfo &currentScene) {
    if (g_shutdownStarted.load(std::memory_order_acquire))
        return;

    for (const LoadedNativeMod &mod : SnapshotLoadedMods()) {
        if (!mod.onSceneChanged)
            continue;

        DWORD exceptionCode = 0;
        std::string cppError;
        if (!InvokeSceneChangedSafely(mod, previousScene, currentScene, &exceptionCode, &cppError)) {
            if (!cppError.empty())
                Log("  [FAILED] %s: OnSceneChanged threw a C++ exception: %s.", mod.path.c_str(), cppError.c_str());
            else
                Log("  [FAILED] %s: OnSceneChanged crashed with native exception 0x%08lX.", mod.path.c_str(),
                    exceptionCode);
        }
    }
}

void ModLifecycle_DispatchObjectDestroyRequested(const URK_ObjectDestroyRequest &request) {
    if (g_shutdownStarted.load(std::memory_order_acquire))
        return;

    for (const LoadedNativeMod &mod : SnapshotLoadedMods()) {
        if (!mod.onObjectDestroyRequested)
            continue;

        DWORD exceptionCode = 0;
        std::string cppError;
        if (!InvokeObjectDestroyRequestedSafely(mod, request, &exceptionCode, &cppError)) {
            if (!cppError.empty())
                Log("  [FAILED] %s: OnObjectDestroyRequested threw a C++ exception: %s.", mod.path.c_str(),
                    cppError.c_str());
            else
                Log("  [FAILED] %s: OnObjectDestroyRequested crashed with native exception 0x%08lX.",
                    mod.path.c_str(), exceptionCode);
        }
    }
}

bool ModLifecycle_ShutdownStarted() {
    return g_shutdownStarted.load(std::memory_order_acquire);
}
