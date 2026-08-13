#include "hook_manager.h"
#include "logger.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <vector>
#include <windows.h>
#include <tlhelp32.h>

#include <detours.h>

namespace {

struct HookRecord {
    uint32_t backend = URK_HOOK_BACKEND_AUTO;
    void **original_slot = nullptr;
    void *target = nullptr;
    void *attach_target = nullptr;
    void *trampoline = nullptr;
    void *detour = nullptr;
};

std::mutex g_hook_mutex;
std::vector<std::unique_ptr<HookRecord>> g_hooks;
#ifdef _WIN64
bool g_detours_address_policy_configured = false;
#endif

struct DetoursResult {
    LONG error = NO_ERROR;
    const char *stage = nullptr;
    DWORD thread_id = 0;
};

HookRecord *FindRecord(void **original, void *detour) {
    for (auto &record : g_hooks) {
        if (record && record->original_slot == original && record->detour == detour) {
            return record.get();
        }
    }

    return nullptr;
}

HMODULE ModuleForAddress(void *address) {
    HMODULE module = nullptr;
    if (!address)
        return nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCSTR>(address), &module);
    return module;
}

bool IsExecutableAddress(void *address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || !VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
        return false;
    }
    const DWORD protection = info.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

void ConfigureDetoursAddressPolicy() {
#ifdef _WIN64
    if (g_detours_address_policy_configured)
        return;

    // Mono JIT code can fall inside Detours' legacy 32-bit system range.
    // Disable that exclusion before allocating x64 trampolines.
    DetourSetSystemRegionLowerBound(nullptr);
    DetourSetSystemRegionUpperBound(nullptr);
    g_detours_address_policy_configured = true;
    Log("[hooks][Detours] x64 trampoline allocator configured for low-address "
        "Unity JIT targets.");
#endif
}

DetoursResult UpdateTransactionThreads(std::vector<HANDLE> *opened_threads) {
    if (!opened_threads)
        return {ERROR_INVALID_PARAMETER, "thread handle storage", 0};

    const LONG current_result = DetourUpdateThread(GetCurrentThread());
    if (current_result != NO_ERROR)
        return {current_result, "DetourUpdateThread", GetCurrentThreadId()};

    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_thread_id = GetCurrentThreadId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {static_cast<LONG>(GetLastError()), "CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD)", 0};

    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    BOOL have_entry = Thread32First(snapshot, &entry);
    if (!have_entry) {
        const DWORD error = GetLastError();
        CloseHandle(snapshot);
        return error == ERROR_NO_MORE_FILES ? DetoursResult{}
                                            : DetoursResult{static_cast<LONG>(error), "Thread32First", 0};
    }

    DetoursResult result{};
    do {
        if (entry.th32OwnerProcessID != process_id || entry.th32ThreadID == current_thread_id) {
            continue;
        }

        HANDLE thread =
            OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                       FALSE, entry.th32ThreadID);
        if (!thread) {
            const DWORD error = GetLastError();
            // A thread that exits after the snapshot no longer needs enlistment.
            if (error == ERROR_INVALID_PARAMETER)
                continue;
            result = {static_cast<LONG>(error), "OpenThread", entry.th32ThreadID};
            break;
        }

        const LONG update_result = DetourUpdateThread(thread);
        if (update_result != NO_ERROR) {
            CloseHandle(thread);
            result = {update_result, "DetourUpdateThread", entry.th32ThreadID};
            break;
        }
        opened_threads->push_back(thread);
    } while (Thread32Next(snapshot, &entry));

    if (result.error == NO_ERROR) {
        const DWORD enumeration_error = GetLastError();
        if (enumeration_error != ERROR_NO_MORE_FILES)
            result = {static_cast<LONG>(enumeration_error), "Thread32Next", 0};
    }

    CloseHandle(snapshot);
    return result;
}

void CloseThreadHandles(std::vector<HANDLE> *threads) {
    if (!threads)
        return;
    for (HANDLE thread : *threads) {
        if (thread)
            CloseHandle(thread);
    }
    threads->clear();
}

DetoursResult RunDetoursTransaction(void **original, void *detour, bool attach) {
    if (!original || !*original || !detour)
        return {ERROR_INVALID_PARAMETER, "argument validation"};

    ConfigureDetoursAddressPolicy();

    const LONG begin_result = DetourTransactionBegin();
    if (begin_result != NO_ERROR)
        return {begin_result, "DetourTransactionBegin"};

    std::vector<HANDLE> opened_threads;
    DetoursResult update_result = UpdateTransactionThreads(&opened_threads);
    if (update_result.error != NO_ERROR) {
        DetourTransactionAbort();
        CloseThreadHandles(&opened_threads);
        return update_result;
    }

    const LONG operation_result = attach ? DetourAttach(original, detour) : DetourDetach(original, detour);
    if (operation_result != NO_ERROR) {
        DetourTransactionAbort();
        CloseThreadHandles(&opened_threads);
        return {operation_result, attach ? "DetourAttach" : "DetourDetach"};
    }

    const LONG commit_result = DetourTransactionCommit();
    CloseThreadHandles(&opened_threads);
    if (commit_result != NO_ERROR) {
        return {commit_result, "DetourTransactionCommit"};
    }

    return {};
}

DetoursResult RunDetoursBatch(const std::vector<HookRecord *> &records, bool attach) {
    if (records.empty())
        return {};

    ConfigureDetoursAddressPolicy();
    const LONG begin_result = DetourTransactionBegin();
    if (begin_result != NO_ERROR)
        return {begin_result, "DetourTransactionBegin"};

    std::vector<HANDLE> opened_threads;
    DetoursResult update_result = UpdateTransactionThreads(&opened_threads);
    if (update_result.error != NO_ERROR) {
        DetourTransactionAbort();
        CloseThreadHandles(&opened_threads);
        return update_result;
    }

    LONG operation_result = NO_ERROR;
    if (attach) {
        for (HookRecord *record : records) {
            if (!record || !record->original_slot || !*record->original_slot || !record->detour) {
                operation_result = ERROR_INVALID_PARAMETER;
                break;
            }
            operation_result = DetourAttach(record->original_slot, record->detour);
            if (operation_result != NO_ERROR)
                break;
        }
    } else {
        for (auto it = records.rbegin(); it != records.rend(); ++it) {
            HookRecord *record = *it;
            if (!record || !record->original_slot || !*record->original_slot || !record->detour) {
                operation_result = ERROR_INVALID_PARAMETER;
                break;
            }
            operation_result = DetourDetach(record->original_slot, record->detour);
            if (operation_result != NO_ERROR)
                break;
        }
    }

    if (operation_result != NO_ERROR) {
        DetourTransactionAbort();
        CloseThreadHandles(&opened_threads);
        return {operation_result, attach ? "DetourAttach(batch)" : "DetourDetach(batch)"};
    }

    const LONG commit_result = DetourTransactionCommit();
    CloseThreadHandles(&opened_threads);
    if (commit_result != NO_ERROR)
        return {commit_result, "DetourTransactionCommit(batch)"};
    return {};
}

std::vector<HookRecord *> RecordsForTarget(void *target) {
    std::vector<HookRecord *> records;
    for (const auto &record : g_hooks) {
        if (record && record->target == target)
            records.push_back(record.get());
    }
    return records;
}

void PrepareHookChain(const std::vector<HookRecord *> &records, void *target) {
    void *attach_target = target;
    for (HookRecord *record : records) {
        record->attach_target = attach_target;
        *record->original_slot = attach_target;
        attach_target = record->detour;
    }
}

void RefreshTrampolines(const std::vector<HookRecord *> &records) {
    for (HookRecord *record : records)
        record->trampoline = record->original_slot ? *record->original_slot : nullptr;
}

bool RebuildTargetWithout(void *target, const std::vector<HookRecord *> &removed) {
    std::vector<HookRecord *> current = RecordsForTarget(target);
    if (current.empty())
        return false;

    const DetoursResult detach_result = RunDetoursBatch(current, false);
    if (detach_result.error != NO_ERROR) {
        Log("[ERROR] Detours target-chain detach failed: stage=%s error=%ld target=%p hooks=%zu.",
            detach_result.stage ? detach_result.stage : "unknown", detach_result.error, target, current.size());
        return false;
    }

    std::vector<HookRecord *> remaining;
    for (HookRecord *record : current) {
        if (std::find(removed.begin(), removed.end(), record) == removed.end())
            remaining.push_back(record);
    }

    PrepareHookChain(remaining, target);
    const DetoursResult attach_result = RunDetoursBatch(remaining, true);
    if (attach_result.error != NO_ERROR) {
        Log("[ERROR] Detours target-chain rebuild failed: stage=%s error=%ld target=%p remaining=%zu; "
            "restoring the previous chain.",
            attach_result.stage ? attach_result.stage : "unknown", attach_result.error, target, remaining.size());
        PrepareHookChain(current, target);
        const DetoursResult rollback_result = RunDetoursBatch(current, true);
        if (rollback_result.error == NO_ERROR) {
            RefreshTrampolines(current);
            return false;
        }

        Log("[hooks][FATAL] target-chain rollback failed: stage=%s error=%ld target=%p; disabling all hooks "
            "for this target to prevent stale module calls.",
            rollback_result.stage ? rollback_result.stage : "unknown", rollback_result.error, target);
        for (HookRecord *record : current) {
            if (record->original_slot)
                *record->original_slot = target;
        }
        g_hooks.erase(std::remove_if(g_hooks.begin(), g_hooks.end(),
                                     [target](const auto &record) { return record && record->target == target; }),
                      g_hooks.end());
        return true;
    }

    RefreshTrampolines(remaining);
    for (HookRecord *record : removed) {
        if (record && record->original_slot)
            *record->original_slot = target;
    }
    g_hooks.erase(std::remove_if(g_hooks.begin(), g_hooks.end(), [&removed](const auto &record) {
                      return record && std::find(removed.begin(), removed.end(), record.get()) != removed.end();
                  }),
                  g_hooks.end());
    Log("[hooks] rebuilt target=%p chain: removed=%zu remaining=%zu.", target, removed.size(), remaining.size());
    return true;
}

int AttachDetours(void **original, void *detour) {
    if (!original || !*original || !detour)
        return 0;

    void *target = *original;
    const DetoursResult result = RunDetoursTransaction(original, detour, true);

    if (result.error != NO_ERROR) {
        Log("[ERROR] Detours hook attach failed: stage=%s error=%ld thread=%lu "
            "target=%p detour=%p.",
            result.stage ? result.stage : "unknown", result.error, result.thread_id, target, detour);
        return 0;
    }

    return 1;
}

int AttachWithBackend(void **original, void *detour, uint32_t backend) {
    if (backend == URK_HOOK_BACKEND_DETOURS) {
        void *target = original ? *original : nullptr;

        const std::vector<HookRecord *> existing = RecordsForTarget(target);
        void *attach_target = existing.empty() ? target : existing.back()->detour;
        *original = attach_target;

        if (!AttachDetours(original, detour)) {
            *original = target;
            return 0;
        }

        auto record = std::make_unique<HookRecord>();
        record->backend = URK_HOOK_BACKEND_DETOURS;
        record->original_slot = original;
        record->target = target;
        record->attach_target = attach_target;
        record->trampoline = original ? *original : nullptr;
        record->detour = detour;

        g_hooks.emplace_back(std::move(record));
        return 1;
    }

    if (backend == URK_HOOK_BACKEND_SAFETYHOOK) {
        Log("[ERROR] SafetyHook backend requested but URKit now exposes "
            "Detours as the only hook backend.");
        return 0;
    }

    Log("[ERROR] Unsupported hook backend requested (%u).", backend);
    return 0;
}

} // namespace

int HookManager_BackendAvailable(uint32_t backend) {
    if (backend == URK_HOOK_BACKEND_AUTO)
        return 1;

    if (backend == URK_HOOK_BACKEND_DETOURS)
        return 1;

    if (backend == URK_HOOK_BACKEND_SAFETYHOOK)
        return 0;

    return 0;
}

int HookManager_Attach(void **original, void *detour, const URK_HookOptions *options) {
    if (!original || !*original || !detour) {
        Log("[ERROR] Hook attach rejected: original slot, target, and detour must be non-null.");
        return 0;
    }
    if (!IsExecutableAddress(*original) || !IsExecutableAddress(detour)) {
        Log("[ERROR] Hook attach rejected: target=%p executable=%d detour=%p executable=%d.", *original,
            IsExecutableAddress(*original) ? 1 : 0, detour, IsExecutableAddress(detour) ? 1 : 0);
        return 0;
    }

    std::scoped_lock lock(g_hook_mutex);

    if (FindRecord(original, detour))
        return 1;

    uint32_t backend = URK_HOOK_BACKEND_AUTO;

    if (options && options->size < sizeof(URK_HookOptions)) {
        Log("[ERROR] Hook attach rejected: HookOptions size=%u is smaller than %zu.", options->size,
            sizeof(URK_HookOptions));
        return 0;
    }
    if (options && options->flags != 0) {
        Log("[ERROR] Hook attach rejected: unsupported HookOptions flags=0x%08X.", options->flags);
        return 0;
    }
    if (options)
        backend = options->backend;

    if (backend == URK_HOOK_BACKEND_AUTO) {
        return AttachWithBackend(original, detour, URK_HOOK_BACKEND_DETOURS);
    }

    return AttachWithBackend(original, detour, backend);
}

int HookManager_Detach(void **original, void *detour) {
    if (!original || !detour)
        return 0;

    std::scoped_lock lock(g_hook_mutex);

    auto it = std::find_if(g_hooks.begin(), g_hooks.end(), [&](const std::unique_ptr<HookRecord> &record) {
        return record && record->original_slot == original && record->detour == detour;
    });

    if (it == g_hooks.end())
        return 0;

    HookRecord *record = it->get();
    if (record->backend != URK_HOOK_BACKEND_DETOURS)
        return 0;
    return RebuildTargetWithout(record->target, {record}) ? 1 : 0;
}

int HookManager_DetachModule(void *module) {
    if (!module)
        return 0;

    const HMODULE targetModule = static_cast<HMODULE>(module);
    std::scoped_lock lock(g_hook_mutex);

    int detached = 0;
    int failed = 0;
    std::vector<void *> targets;
    for (const auto &record : g_hooks) {
        if (record && ModuleForAddress(record->detour) == targetModule &&
            std::find(targets.begin(), targets.end(), record->target) == targets.end()) {
            targets.push_back(record->target);
        }
    }

    for (void *target : targets) {
        std::vector<HookRecord *> removed;
        for (const auto &record : g_hooks) {
            if (record && record->target == target && ModuleForAddress(record->detour) == targetModule)
                removed.push_back(record.get());
        }
        if (removed.empty())
            continue;
        const bool supported = std::all_of(removed.begin(), removed.end(), [](HookRecord *record) {
            return record->backend == URK_HOOK_BACKEND_DETOURS;
        });
        if (!supported || !RebuildTargetWithout(target, removed)) {
            ++failed;
            continue;
        }
        detached += static_cast<int>(removed.size());
    }

    if (detached || failed) {
        Log("[hooks] module=%p detach complete: detached=%d failed=%d.", module, detached, failed);
    }
    return failed == 0 ? detached : -1;
}
