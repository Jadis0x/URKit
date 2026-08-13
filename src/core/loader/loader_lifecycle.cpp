#include "loader_lifecycle.h"

#include "intro.h"
#include "logger.h"

#include <atomic>
#include <cstdio>
#include <exception>

namespace {
enum class LifecycleState : unsigned char {
    Dormant,
    Starting,
    Running,
    Finished,
    StopRequested,
};

std::atomic<LifecycleState> g_state{LifecycleState::Dormant};
std::atomic_bool g_stopRequested{false};
HANDLE g_stopEvent = nullptr;
HANDLE g_ownerMarker = nullptr;
HANDLE g_bootstrapThread = nullptr;
LoaderStartMode g_mode = LoaderStartMode::Proxy;

void DebugLifecycleMessage(const char *message) {
    OutputDebugStringA(message ? message : "[URKit][lifecycle] unknown diagnostic\n");
}

DWORD RunLoaderCppBoundary() noexcept {
    try {
        g_state.store(LifecycleState::Running, std::memory_order_release);
        const LoaderRunStatus status = Loader_Run(g_mode);
        g_state.store(LifecycleState::Finished, std::memory_order_release);
        switch (status) {
        case LoaderRunStatus::Succeeded:
            return ERROR_SUCCESS;
        case LoaderRunStatus::Skipped:
            return ERROR_CANCELLED;
        case LoaderRunStatus::Failed:
            return ERROR_DLL_INIT_FAILED;
        }
    } catch (const std::exception &exception) {
        Log("[loader][FATAL] Unhandled C++ exception escaped loader startup: %s", exception.what());
        Intro::Close();
        g_state.store(LifecycleState::Finished, std::memory_order_release);
        return ERROR_UNHANDLED_EXCEPTION;
    } catch (...) {
        Log("[loader][FATAL] Unknown C++ exception escaped loader startup.");
        Intro::Close();
        g_state.store(LifecycleState::Finished, std::memory_order_release);
        return ERROR_UNHANDLED_EXCEPTION;
    }
    return ERROR_DLL_INIT_FAILED;
}

DWORD RunLoaderNativeBoundary() {
    __try {
        return RunLoaderCppBoundary();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char diagnostic[192]{};
        std::snprintf(diagnostic, sizeof(diagnostic),
                      "[URKit][loader][FATAL] Native exception 0x%08lX escaped loader startup.\n",
                      GetExceptionCode());
        DebugLifecycleMessage(diagnostic);
        g_state.store(LifecycleState::Finished, std::memory_order_release);
        return GetExceptionCode();
    }
}

DWORD WINAPI BootstrapThread(void *) {
    return RunLoaderNativeBoundary();
}

bool AcquireProcessOwnerMarker() {
    wchar_t name[96]{};
    swprintf_s(name, L"Local\\URKit.RuntimeOwner.%lu", GetCurrentProcessId());
    HANDLE marker = CreateMutexW(nullptr, FALSE, name);
    if (!marker) {
        DebugLifecycleMessage("[URKit][lifecycle][ERROR] Process owner marker creation failed.\n");
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(marker);
        DebugLifecycleMessage("[URKit][lifecycle] Another URKit proxy owns this process; forwarding-only mode.\n");
        return false;
    }
    g_ownerMarker = marker;
    return true;
}
} // namespace

bool LoaderLifecycle_TryStart(HMODULE module, LoaderStartMode mode) {
    if (!module || !AcquireProcessOwnerMarker())
        return false;

    HMODULE pinnedModule = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(module), &pinnedModule) ||
        pinnedModule == nullptr) {
        DebugLifecycleMessage("[URKit][lifecycle][ERROR] Loader module could not be pinned for process lifetime.\n");
        CloseHandle(g_ownerMarker);
        g_ownerMarker = nullptr;
        return false;
    }

    g_mode = mode;
    g_stopRequested.store(false, std::memory_order_release);
    g_state.store(LifecycleState::Starting, std::memory_order_release);
    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_stopEvent) {
        DebugLifecycleMessage("[URKit][lifecycle][ERROR] Stop event creation failed.\n");
        CloseHandle(g_ownerMarker);
        g_ownerMarker = nullptr;
        g_state.store(LifecycleState::Dormant, std::memory_order_release);
        return false;
    }

    g_bootstrapThread = CreateThread(nullptr, 0, &BootstrapThread, nullptr, 0, nullptr);
    if (!g_bootstrapThread) {
        DebugLifecycleMessage("[URKit][lifecycle][ERROR] Bootstrap thread creation failed.\n");
        CloseHandle(g_stopEvent);
        CloseHandle(g_ownerMarker);
        g_stopEvent = nullptr;
        g_ownerMarker = nullptr;
        g_state.store(LifecycleState::Dormant, std::memory_order_release);
        return false;
    }
    return true;
}

void LoaderLifecycle_RequestStopFromDllMain() {
    g_stopRequested.store(true, std::memory_order_release);
    g_state.store(LifecycleState::StopRequested, std::memory_order_release);
    if (g_stopEvent)
        SetEvent(g_stopEvent);
}

void LoaderLifecycle_ReleaseProcessResourcesFromDllMain() {
    if (g_bootstrapThread) {
        CloseHandle(g_bootstrapThread);
        g_bootstrapThread = nullptr;
    }
    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    if (g_ownerMarker) {
        CloseHandle(g_ownerMarker);
        g_ownerMarker = nullptr;
    }
}

bool LoaderLifecycle_StopRequested() {
    return g_stopRequested.load(std::memory_order_acquire);
}

HANDLE LoaderLifecycle_StopEvent() {
    return g_stopEvent;
}
