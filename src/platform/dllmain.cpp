#include "loader.h"
#include "loader/loader_lifecycle.h"
#include <cwchar>
#include <windows.h>

static bool ShouldSkipProcess() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    const wchar_t *name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;

    if (_wcsicmp(name, L"UnityCrashHandler64.exe") == 0)
        return true;
    if (_wcsicmp(name, L"UnityCrashHandler32.exe") == 0)
        return true;
    if (_wcsicmp(name, L"crashpad_handler.exe") == 0)
        return true;

    return false;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    (void)reserved;
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(hModule);

            if (ShouldSkipProcess())
                return TRUE;

#ifdef URK_INJECTED_LOADER
            LoaderLifecycle_TryStart(hModule, LoaderStartMode::Injected);
#else
            LoaderLifecycle_TryStart(hModule, LoaderStartMode::Proxy);
#endif
            break;

        case DLL_PROCESS_DETACH:
            // Avoid cleanup under the loader lock; normal shutdown owns it.
            LoaderLifecycle_RequestStopFromDllMain();
            LoaderLifecycle_ReleaseProcessResourcesFromDllMain();
            break;
    }

    return TRUE;
}
