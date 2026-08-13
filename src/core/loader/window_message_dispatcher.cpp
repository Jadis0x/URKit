#include "window_message_dispatcher.h"

#include "logger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
struct CallbackRecord {
    URK_WindowMessageCallback callback = nullptr;
    HMODULE owner = nullptr;
    std::atomic_uint active{0};
    std::mutex waitMutex;
    std::condition_variable waitCondition;
};

struct WindowRecord {
    WNDPROC original = nullptr;
    std::vector<std::shared_ptr<CallbackRecord>> callbacks;
    bool installed = false;
};

std::mutex g_mutex;
std::unordered_map<HWND, WindowRecord> g_windows;
thread_local CallbackRecord *g_currentCallback = nullptr;
thread_local std::vector<CallbackRecord *> g_dispatchStack;

HMODULE ModuleForAddress(const void *address) {
    HMODULE module = nullptr;
    if (!address || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                       reinterpret_cast<LPCSTR>(address), &module)) {
        return nullptr;
    }
    return module;
}

bool SameProcessWindow(HWND window) {
    DWORD processId = 0;
    return window && GetWindowThreadProcessId(window, &processId) != 0 && processId == GetCurrentProcessId();
}

__declspec(noinline) bool InvokeWindowMessageCallback(URK_WindowMessageCallback callback, HWND window, UINT message,
                                                      WPARAM wparam, LPARAM lparam, intptr_t *result, int *handled,
                                                      DWORD *exceptionCode) {
    __try {
        *result = callback(window, message, wparam, lparam, handled);
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

LRESULT CALLBACK DispatchWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    std::vector<std::shared_ptr<CallbackRecord>> callbacks;
    WNDPROC original = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_windows.find(window);
        if (found == g_windows.end())
            return DefWindowProc(window, message, wparam, lparam);
        original = found->second.original;
        callbacks.assign(found->second.callbacks.rbegin(), found->second.callbacks.rend());
        for (const auto &record : callbacks)
            record->active.fetch_add(1, std::memory_order_acq_rel);
    }

    bool handled = false;
    LRESULT handledResult = 0;
    const size_t dispatchStackBase = g_dispatchStack.size();
    for (const auto &record : callbacks)
        g_dispatchStack.push_back(record.get());
    for (const auto &record : callbacks) {
        int callbackHandled = 0;
        intptr_t callbackResult = 0;
        DWORD exceptionCode = 0;
        CallbackRecord *previousCallback = g_currentCallback;
        g_currentCallback = record.get();
        if (!InvokeWindowMessageCallback(record->callback, window, message, wparam, lparam, &callbackResult,
                                         &callbackHandled, &exceptionCode)) {
            Log("[window-messages][ERROR] callback=%p owner=%p crashed with exception 0x%08lX.",
                reinterpret_cast<void *>(record->callback), record->owner, exceptionCode);
        }
        g_currentCallback = previousCallback;
        if (callbackHandled && !handled) {
            handled = true;
            handledResult = static_cast<LRESULT>(callbackResult);
        }
        if (record->active.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            std::lock_guard<std::mutex> waitLock(record->waitMutex);
            record->waitCondition.notify_all();
        }
    }
    g_dispatchStack.resize(dispatchStackBase);

    if (handled)
        return handledResult;
    return original ? CallWindowProc(original, window, message, wparam, lparam)
                    : DefWindowProc(window, message, wparam, lparam);
}

void WaitUntilIdle(const std::shared_ptr<CallbackRecord> &record) {
    std::unique_lock<std::mutex> lock(record->waitMutex);
    record->waitCondition.wait(lock, [&record] { return record->active.load(std::memory_order_acquire) == 0; });
}

bool RestoreIfUnused(HWND window, WindowRecord &record) {
    if (!record.callbacks.empty())
        return false;
    if (IsWindow(window) && GetWindowLongPtr(window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(&DispatchWindowMessage)) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(record.original));
        if (!previous && GetLastError() != ERROR_SUCCESS) {
            Log("[window-messages][ERROR] failed to restore window=%p error=%lu.", window, GetLastError());
            return false;
        }
        record.installed = false;
        return false;
    }
    return !IsWindow(window);
}
} // namespace

int WindowMessage_Register(void *windowValue, URK_WindowMessageCallback callback) {
    HWND window = static_cast<HWND>(windowValue);
    HMODULE owner = ModuleForAddress(reinterpret_cast<const void *>(callback));
    if (!SameProcessWindow(window) || !callback || !owner)
        return 0;

    std::lock_guard<std::mutex> lock(g_mutex);
    auto found = g_windows.find(window);
    if (found == g_windows.end()) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DispatchWindowMessage));
        if (!previous && GetLastError() != ERROR_SUCCESS) {
            Log("[window-messages][ERROR] failed to install dispatcher for window=%p error=%lu.", window,
                GetLastError());
            return 0;
        }
        found = g_windows.emplace(window, WindowRecord{reinterpret_cast<WNDPROC>(previous), {}, true}).first;
    } else if (!found->second.installed) {
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous = SetWindowLongPtr(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&DispatchWindowMessage));
        if (!previous && GetLastError() != ERROR_SUCCESS) {
            Log("[window-messages][ERROR] failed to reinstall dispatcher for window=%p error=%lu.", window,
                GetLastError());
            return 0;
        }
        found->second.original = reinterpret_cast<WNDPROC>(previous);
        found->second.installed = true;
    }

    const auto duplicate = std::find_if(found->second.callbacks.begin(), found->second.callbacks.end(),
                                        [callback](const auto &record) { return record->callback == callback; });
    if (duplicate != found->second.callbacks.end())
        return 1;

    auto record = std::make_shared<CallbackRecord>();
    record->callback = callback;
    record->owner = owner;
    found->second.callbacks.push_back(std::move(record));
    return 1;
}

int WindowMessage_Unregister(void *windowValue, URK_WindowMessageCallback callback) {
    HWND window = static_cast<HWND>(windowValue);
    if (!window || !callback)
        return 0;

    std::shared_ptr<CallbackRecord> removed;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_windows.find(window);
        if (found == g_windows.end())
            return 0;
        const auto callbackIt = std::find_if(found->second.callbacks.begin(), found->second.callbacks.end(),
                                             [callback](const auto &record) { return record->callback == callback; });
        if (callbackIt == found->second.callbacks.end())
            return 0;
        if (std::find(g_dispatchStack.begin(), g_dispatchStack.end(), callbackIt->get()) != g_dispatchStack.end())
            return 0;
        removed = *callbackIt;
        found->second.callbacks.erase(callbackIt);
        if (RestoreIfUnused(window, found->second))
            g_windows.erase(found);
    }
    WaitUntilIdle(removed);
    return 1;
}

intptr_t WindowMessage_CallOriginal(void *windowValue, uint32_t message, uintptr_t wparam, intptr_t lparam) {
    HWND window = static_cast<HWND>(windowValue);
    WNDPROC original = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const auto found = g_windows.find(window);
        if (found != g_windows.end())
            original = found->second.original;
    }
    return original ? CallWindowProc(original, window, message, wparam, lparam)
                    : DefWindowProc(window, message, wparam, lparam);
}

int WindowMessage_UnregisterModule(void *moduleValue) {
    HMODULE module = static_cast<HMODULE>(moduleValue);
    if (!module)
        return 0;

    std::vector<std::shared_ptr<CallbackRecord>> removed;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (const auto &[window, record] : g_windows) {
            (void)window;
            for (const auto &callback : record.callbacks) {
                if (callback->owner == module &&
                    std::find(g_dispatchStack.begin(), g_dispatchStack.end(), callback.get()) != g_dispatchStack.end())
                    return -1;
            }
        }
        for (auto windowIt = g_windows.begin(); windowIt != g_windows.end();) {
            WindowRecord &windowRecord = windowIt->second;
            auto callbackIt = windowRecord.callbacks.begin();
            while (callbackIt != windowRecord.callbacks.end()) {
                if ((*callbackIt)->owner == module) {
                    removed.push_back(*callbackIt);
                    callbackIt = windowRecord.callbacks.erase(callbackIt);
                } else {
                    ++callbackIt;
                }
            }
            if (RestoreIfUnused(windowIt->first, windowRecord))
                windowIt = g_windows.erase(windowIt);
            else
                ++windowIt;
        }
    }
    for (const auto &record : removed)
        WaitUntilIdle(record);
    return static_cast<int>(removed.size());
}
