#include "main_thread_dispatcher.h"
#include "logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <vector>

namespace {
struct MainThreadCallback {
    void (*callback)() = nullptr;
    HMODULE module = nullptr;
    std::uint32_t consecutiveFaults = 0;
};

std::mutex g_mainThreadMutex;
std::vector<MainThreadCallback> g_callbacks;
bool g_dispatchTargetAvailable = false;
bool g_unavailableLogged = false;
constexpr std::uint32_t kMaxConsecutiveCallbackFaults = 3;

HMODULE ModuleForAddress(void *address) {
    HMODULE module = nullptr;
    if (!address)
        return nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(address), &module);
    return module;
}

bool InvokeCallbackSafely(void (*callback)(), DWORD *exceptionCode) {
    __try {
        callback();
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

std::uint32_t RecordCallbackResult(void (*callback)(), bool succeeded) {
    std::lock_guard lock(g_mainThreadMutex);
    const auto found = std::find_if(g_callbacks.begin(), g_callbacks.end(),
                                    [callback](const MainThreadCallback &entry) {
                                        return entry.callback == callback;
                                    });
    if (found == g_callbacks.end())
        return 0;

    if (succeeded) {
        found->consecutiveFaults = 0;
        return 0;
    }

    ++found->consecutiveFaults;
    const std::uint32_t faults = found->consecutiveFaults;
    if (faults >= kMaxConsecutiveCallbackFaults)
        g_callbacks.erase(found);
    return faults;
}
} // namespace

int MainThread_Register(void (*callback)()) {
    if (!callback)
        return 0;

    const HMODULE module = ModuleForAddress(reinterpret_cast<void *>(callback));
    if (!module) {
        Log("[mod][ERROR] MainThreadRegister failed: callback=%p does not resolve "
            "to a loaded module.",
            reinterpret_cast<void *>(callback));
        return 0;
    }

    std::lock_guard lock(g_mainThreadMutex);

    const auto found = std::find_if(g_callbacks.begin(), g_callbacks.end(),
                                    [callback](const MainThreadCallback &entry) { return entry.callback == callback; });
    if (found != g_callbacks.end())
        return 1;

    g_callbacks.push_back({callback, module});

    if (!g_dispatchTargetAvailable && !g_unavailableLogged) {
        Log("[mod][WARNING] MainThreadRegister queued: URKit runtime dispatch "
            "target is not active yet; callbacks will start automatically when "
            "runtime scene dispatch becomes available.");
        g_unavailableLogged = true;
    }

    return 1;
}

int MainThread_Unregister(void (*callback)()) {
    if (!callback)
        return 0;

    std::lock_guard lock(g_mainThreadMutex);
    const auto before = g_callbacks.size();
    g_callbacks.erase(
        std::remove_if(g_callbacks.begin(), g_callbacks.end(),
                       [callback](const MainThreadCallback &entry) { return entry.callback == callback; }),
        g_callbacks.end());
    return g_callbacks.size() != before ? 1 : 0;
}

void MainThread_UnregisterModule(void *module) {
    if (!module)
        return;

    const HMODULE targetModule = static_cast<HMODULE>(module);
    std::lock_guard lock(g_mainThreadMutex);
    g_callbacks.erase(
        std::remove_if(g_callbacks.begin(), g_callbacks.end(),
                       [targetModule](const MainThreadCallback &entry) { return entry.module == targetModule; }),
        g_callbacks.end());
}

bool MainThread_HasDispatchTarget() {
    std::lock_guard lock(g_mainThreadMutex);
    return g_dispatchTargetAvailable;
}

void MainThread_SetDispatchTargetAvailable(bool available) {
    std::lock_guard lock(g_mainThreadMutex);
    const bool becameAvailable = available && !g_dispatchTargetAvailable;
    g_dispatchTargetAvailable = available;

    if (!available) {
        g_callbacks.clear();
        g_unavailableLogged = false;
        return;
    }

    if (becameAvailable && !g_callbacks.empty()) {
        Log("[SUCCESS][runtime][events] main-thread dispatch target activated; %zu queued "
            "callback(s) will run on the next runtime pump.",
            g_callbacks.size());
    }
}

void MainThread_Drain() {
    std::vector<MainThreadCallback> callbacks;
    {
        std::lock_guard lock(g_mainThreadMutex);
        if (!g_dispatchTargetAvailable || g_callbacks.empty())
            return;
        callbacks = g_callbacks;
    }

    for (const MainThreadCallback &entry : callbacks) {
        if (!entry.callback)
            continue;

        DWORD exceptionCode = 0;
        if (InvokeCallbackSafely(entry.callback, &exceptionCode)) {
            RecordCallbackResult(entry.callback, true);
        } else {
            const std::uint32_t faults = RecordCallbackResult(entry.callback, false);
            if (faults >= kMaxConsecutiveCallbackFaults) {
                Log("[mod][ERROR] MainThread callback=%p crashed with exception "
                    "0x%08lX for %u consecutive dispatches; unregistering it.",
                    reinterpret_cast<void *>(entry.callback), exceptionCode, faults);
            } else if (faults != 0) {
                Log("[mod][ERROR] MainThread callback=%p crashed with exception "
                    "0x%08lX; keeping it registered for recovery attempt %u/%u.",
                    reinterpret_cast<void *>(entry.callback), exceptionCode, faults,
                    kMaxConsecutiveCallbackFaults);
            }
        }
    }
}
