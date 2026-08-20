#include "mod_context.h"
#include "hooks.h"
#include "logger.h"
#include "main_thread_dispatcher.h"
#include "mod_api.h"
#include "network_http.h"
#include "runtime_events.h"
#include "steam_identity.h"
#include "window_message_dispatcher.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <intrin.h>
#include <string>

namespace {
URK_ModContext g_modContext{};
void Ctx_Log(const char *fmt, ...) {
    char b[2048];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof(b), fmt, a);
    va_end(a);
    Log("[mod] %s", b);
}

int Ctx_HookAttach(void **o, void *d) {
    return Hook_Attach(o, d) ? 1 : 0;
}
int Ctx_HookDetach(void **o, void *d) {
    return Hook_Detach(o, d) ? 1 : 0;
}
int Ctx_HookAttachEx(void **o, void *d, const URK_HookOptions *options) {
    return Hook_AttachEx(o, d, options) ? 1 : 0;
}
int Ctx_HookDetachEx(void **o, void *d) {
    return Hook_DetachEx(o, d) ? 1 : 0;
}
int Ctx_HookBackendAvailable(uint32_t backend) {
    return Hook_BackendAvailable(backend) ? 1 : 0;
}
int Ctx_MainThreadUnregister(void (*callback)()) {
    return MainThread_Unregister(callback);
}
int Ctx_NetworkJsonRequest(const URK_NetworkRequest *request, URK_NetworkResponse *response) {
    return NetworkHttp_JsonRequest(request, response) ? 1 : 0;
}
uint32_t Runtime_Backend() {
    return g_modContext.runtimeBackend;
}

uint64_t Runtime_Capabilities() {
    return g_modContext.runtimeCapabilities | RuntimeEvents_Capabilities();
}

uintptr_t Runtime_ModuleBase(uint32_t kind) {
    switch (kind) {
        case URK_RUNTIME_MODULE_BACKEND_PRIMARY:
            return g_modContext.runtimeBackendModuleBase;
        case URK_RUNTIME_MODULE_MONO:
            return g_modContext.runtimeBackend == URK_RUNTIME_BACKEND_MONO ? g_modContext.runtimeBackendModuleBase : 0;
        case URK_RUNTIME_MODULE_GAME_ASSEMBLY:
            return g_modContext.gameAssemblyModuleBase;
        case URK_RUNTIME_MODULE_UNITY_PLAYER:
            return g_modContext.unityPlayerModuleBase;
        default:
            return 0;
    }
}

int Runtime_SceneCurrent(URK_SceneInfo *scene) {
    return RuntimeEvents_CurrentScene(scene);
}

HMODULE Runtime_OwnerModule(const void *ownerAddress) {
    HMODULE owner = nullptr;
    if (!ownerAddress || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                            reinterpret_cast<LPCSTR>(ownerAddress), &owner))
        return nullptr;
    return owner;
}

int Runtime_MenuCursorSetOpenOwned(const void *ownerAddress, int open) {
    return RuntimeEvents_MenuCursorSetOpen(Runtime_OwnerModule(ownerAddress), open);
}

int Runtime_MenuMouseCaptureSetOwned(const void *ownerAddress, int capture) {
    return RuntimeEvents_MenuMouseCaptureSet(Runtime_OwnerModule(ownerAddress), capture);
}

__declspec(noinline) int Runtime_MenuCursorSetOpen(int open) {
    return Runtime_MenuCursorSetOpenOwned(_ReturnAddress(), open);
}

int Runtime_CursorStateGet(URK_CursorState *state) {
    return RuntimeEvents_CursorStateGet(state);
}

int Runtime_CursorStateSet(const URK_CursorState *state) {
    return RuntimeEvents_CursorStateSet(state);
}

int Runtime_InputGetKey(int32_t keyCode) {
    return RuntimeEvents_InputGetKey(keyCode);
}

int Runtime_InputGetKeyDown(int32_t keyCode) {
    return RuntimeEvents_InputGetKeyDown(keyCode);
}

int Runtime_InputGetKeyUp(int32_t keyCode) {
    return RuntimeEvents_InputGetKeyUp(keyCode);
}

int Runtime_InputGetMouseButton(int32_t button) {
    return RuntimeEvents_InputGetMouseButton(button);
}

int Runtime_InputGetMouseButtonDown(int32_t button) {
    return RuntimeEvents_InputGetMouseButtonDown(button);
}

int Runtime_InputGetMouseButtonUp(int32_t button) {
    return RuntimeEvents_InputGetMouseButtonUp(button);
}

int32_t Runtime_GraphicsDeviceType() {
    return RuntimeEvents_GraphicsDeviceType();
}

int Runtime_SteamId64(char *output, size_t outputSize) {
    if (!output || outputSize < URK_STEAM_ID64_MAX)
        return 0;
    output[0] = '\0';
    std::string steamId;
    if (!SteamIdentity_TryReadSteamId64(&steamId) || steamId.size() + 1 > outputSize)
        return 0;
    std::memcpy(output, steamId.c_str(), steamId.size() + 1);
    return 1;
}

int Runtime_IsMainThread() {
    return RuntimeEvents_IsMainThread();
}

const URK_RuntimeApi g_runtimeApi = {
    URK_RUNTIME_API_VERSION,
    sizeof(URK_RuntimeApi),
    &Runtime_Backend,
    &Runtime_Capabilities,
    &Runtime_ModuleBase,
    &Runtime_SceneCurrent,
    &Runtime_MenuCursorSetOpen,
    &Runtime_CursorStateGet,
    &Runtime_CursorStateSet,
    &Runtime_InputGetKey,
    &Runtime_InputGetKeyDown,
    &Runtime_InputGetKeyUp,
    &Runtime_InputGetMouseButton,
    &Runtime_InputGetMouseButtonDown,
    &Runtime_InputGetMouseButtonUp,
    &Runtime_GraphicsDeviceType,
    &Runtime_SteamId64,
    &WindowMessage_Register,
    &WindowMessage_Unregister,
    &WindowMessage_CallOriginal,
    &Runtime_MenuCursorSetOpenOwned,
    &Runtime_MenuMouseCaptureSetOwned,
    &Runtime_IsMainThread,
};

const URK_NetworkApi g_networkApi = {
    URK_NETWORK_API_VERSION,
    sizeof(URK_NetworkApi),
    &Ctx_NetworkJsonRequest,
};

bool HasCompatibleIl2CppApi(uint32_t runtimeBackend, const URK_Il2CppApi *api) {
    const size_t requiredSize = offsetof(URK_Il2CppApi, last_error) + sizeof(api->last_error);
    return runtimeBackend == URK_RUNTIME_BACKEND_IL2CPP && api && api->version >= URK_IL2CPP_API_VERSION &&
           api->size >= requiredSize && api->is_available && api->is_available() && api->domain_get && api->find_image &&
           api->find_class && api->find_method && api->find_method_exact && api->find_field && api->field_offset &&
           api->runtime_invoke && api->last_error;
}

} // namespace

URK_ModContext &ModContext_Build(const Config &, const ModContextBuildOptions &options) {
    uint64_t capabilities = (options.backendCapabilities & ~URK_RUNTIME_CAP_MAIN_THREAD) | URK_RUNTIME_CAP_HOOKS |
                            URK_RUNTIME_CAP_STEAM_IDENTITY;
    if (options.mainThreadDispatcherAvailable) {
        capabilities |= URK_RUNTIME_CAP_MAIN_THREAD;
    }
    if (options.apis.mono)
        capabilities |= URK_RUNTIME_CAP_MONO_API;
    if (HasCompatibleIl2CppApi(options.runtimeBackend, options.apis.il2cpp)) {
        capabilities |= URK_RUNTIME_CAP_IL2CPP_API;
    }
    if (NetworkHttp_Available())
        capabilities |= URK_RUNTIME_CAP_NETWORK;
    g_modContext.version = URK_SDK_VERSION;
    g_modContext.Log = &Ctx_Log;
    g_modContext.HookAttach = &Ctx_HookAttach;
    g_modContext.HookDetach = &Ctx_HookDetach;
    g_modContext.HookAttachEx = &Ctx_HookAttachEx;
    g_modContext.HookDetachEx = &Ctx_HookDetachEx;
    g_modContext.HookBackendAvailable = &Ctx_HookBackendAvailable;
    g_modContext.runtimeModuleBase = options.modules.backendModuleBase;
    g_modContext.mono = options.apis.mono;
    g_modContext.MainThreadRegister = &MainThread_Register;
    g_modContext.MainThreadUnregister = &Ctx_MainThreadUnregister;
    g_modContext.size = sizeof(URK_ModContext);
    g_modContext.runtimeBackend = options.runtimeBackend;
    g_modContext.runtimeCapabilities = capabilities;
    g_modContext.runtime = &g_runtimeApi;
    g_modContext.il2cpp = options.apis.il2cpp;
    g_modContext.runtimeBackendModuleBase = options.modules.backendModuleBase;
    g_modContext.unityPlayerModuleBase = options.modules.unityPlayerModuleBase;
    g_modContext.gameAssemblyModuleBase = options.modules.gameAssemblyModuleBase;
    g_modContext.network = NetworkHttp_Available() ? &g_networkApi : nullptr;

    return g_modContext;
}
