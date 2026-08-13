#include "runtime_events.h"

#include "hooks.h"
#include "cursor_lease_registry.h"
#include "il2cpp_api.h"
#include "loader.h"
#include "logger.h"
#include "main_thread_dispatcher.h"
#include "mod_lifecycle.h"
#include "mono_api.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstring>
#include <mutex>
#include <new>
#include <string>
#include <type_traits>

namespace {
template <typename FunctionPointer>
FunctionPointer FunctionPointerFromAddress(void *address) {
    static_assert(std::is_pointer_v<FunctionPointer>);
    static_assert(std::is_function_v<std::remove_pointer_t<FunctionPointer>>);
    static_assert(sizeof(FunctionPointer) == sizeof(address));
    return std::bit_cast<FunctionPointer>(address);
}

struct MonoSceneMethods {
    MonoMethod *getActiveScene = nullptr;
    MonoMethod *internalSceneLoaded = nullptr;
    MonoMethod *internalActiveSceneChanged = nullptr;
    MonoMethod *isValidInternal = nullptr;
    MonoMethod *getNameInternal = nullptr;
    MonoMethod *getBuildIndexInternal = nullptr;

    bool ready() const {
        return getActiveScene && getNameInternal;
    }
};

struct MonoCursorMethods {
    MonoMethod *getVisible = nullptr;
    MonoMethod *setVisible = nullptr;
    MonoMethod *getLockState = nullptr;
    MonoMethod *setLockState = nullptr;

    bool ready() const {
        return getVisible && setVisible && getLockState && setLockState;
    }
};

struct MonoInputMethods {
    MonoMethod *getKey = nullptr;
    MonoMethod *getKeyDown = nullptr;
    MonoMethod *getKeyUp = nullptr;
    MonoMethod *getMouseButton = nullptr;
    MonoMethod *getMouseButtonDown = nullptr;
    MonoMethod *getMouseButtonUp = nullptr;

    bool ready() const {
        return getKey && getKeyDown && getKeyUp && getMouseButton && getMouseButtonDown && getMouseButtonUp;
    }
};

struct MonoFrameMethods {
    MonoMethod *invokeOnBeforeRender = nullptr;
    MonoMethod *getGraphicsDeviceType = nullptr;

    bool frame_ready() const {
        return invokeOnBeforeRender != nullptr;
    }
    bool graphics_ready() const {
        return getGraphicsDeviceType != nullptr;
    }
    bool ready() const {
        return frame_ready() && graphics_ready();
    }
};

struct MonoApplicationMethods {
    MonoMethod *internalApplicationQuit = nullptr;

    bool ready() const {
        return internalApplicationQuit != nullptr;
    }
};

struct MonoObjectDestroyMethods {
    MonoMethod *destroy = nullptr;
    MonoMethod *destroyImmediate = nullptr;
    MonoMethod *getInstanceId = nullptr;
    MonoMethod *getName = nullptr;

    bool ready() const {
        return destroy && destroyImmediate;
    }
};

struct Il2CppSceneMethods {
    const Il2CppMethod *getActiveScene = nullptr;
    const Il2CppMethod *internalSceneLoaded = nullptr;
    const Il2CppMethod *internalActiveSceneChanged = nullptr;
    const Il2CppMethod *isValidInternal = nullptr;
    const Il2CppMethod *getNameInternal = nullptr;
    const Il2CppMethod *getBuildIndexInternal = nullptr;

    bool ready() const {
        return getActiveScene && getNameInternal;
    }
};

struct Il2CppCursorMethods {
    const Il2CppMethod *getVisible = nullptr;
    const Il2CppMethod *setVisible = nullptr;
    const Il2CppMethod *getLockState = nullptr;
    const Il2CppMethod *setLockState = nullptr;

    bool ready() const {
        return getVisible && setVisible && getLockState && setLockState;
    }
};

struct Il2CppInputMethods {
    const Il2CppMethod *getKey = nullptr;
    const Il2CppMethod *getKeyDown = nullptr;
    const Il2CppMethod *getKeyUp = nullptr;
    const Il2CppMethod *getMouseButton = nullptr;
    const Il2CppMethod *getMouseButtonDown = nullptr;
    const Il2CppMethod *getMouseButtonUp = nullptr;

    bool ready() const {
        return getKey && getKeyDown && getKeyUp && getMouseButton && getMouseButtonDown && getMouseButtonUp;
    }
};

struct Il2CppFrameMethods {
    const Il2CppMethod *invokeOnBeforeRender = nullptr;
    const Il2CppMethod *getGraphicsDeviceType = nullptr;

    bool frame_ready() const {
        return invokeOnBeforeRender != nullptr;
    }
    bool graphics_ready() const {
        return getGraphicsDeviceType != nullptr;
    }
    bool ready() const {
        return frame_ready() && graphics_ready();
    }
};

struct Il2CppApplicationMethods {
    const Il2CppMethod *internalApplicationQuit = nullptr;

    bool ready() const {
        return internalApplicationQuit != nullptr;
    }
};

struct Il2CppObjectDestroyMethods {
    const Il2CppMethod *destroy = nullptr;
    const Il2CppMethod *destroyImmediate = nullptr;
    const Il2CppMethod *getInstanceId = nullptr;
    const Il2CppMethod *getName = nullptr;

    bool hooks_ready() const {
        return destroy && destroyImmediate;
    }
};

enum class RuntimeEventsBackend {
    None,
    Mono,
    Il2Cpp,
};

struct CursorState {
    bool visible = false;
    int32_t lockState = 0;
};

struct SceneValue {
    int32_t handle = 0;
};

using MonoSceneLoadedFn = void (*)(SceneValue scene, int32_t mode);
using MonoActiveSceneChangedFn = void (*)(SceneValue previousScene, SceneValue currentScene);
using Il2CppSceneLoadedFn = void (*)(SceneValue scene, int32_t mode, const void *methodInfo);
using Il2CppActiveSceneChangedFn = void (*)(SceneValue previousScene, SceneValue currentScene, const void *methodInfo);
using MonoInputBoolFn = bool (*)(int32_t value);
using Il2CppInputBoolFn = bool (*)(int32_t value, const void *methodInfo);
using MonoBeforeRenderFn = void (*)();
using Il2CppBeforeRenderFn = void (*)(const void *methodInfo);
using MonoApplicationQuitFn = void (*)();
using Il2CppApplicationQuitFn = void (*)(const void *methodInfo);
using MonoDestroyFn = void (*)(MonoObject *object, float delaySeconds);
using MonoDestroyImmediateFn = void (*)(MonoObject *object, bool allowDestroyingAssets);
using Il2CppDestroyFn = void (*)(Il2CppObject *object, float delaySeconds, const void *methodInfo);
using Il2CppDestroyImmediateFn = void (*)(Il2CppObject *object, bool allowDestroyingAssets, const void *methodInfo);

std::mutex g_eventsMutex;
RuntimeEventsBackend g_backend = RuntimeEventsBackend::None;
MonoApi *g_mono = nullptr;
Il2CppApi *g_il2cpp = nullptr;
uint64_t g_capabilities = URK_RUNTIME_CAP_NONE;
MonoSceneMethods g_sceneMethods{};
MonoCursorMethods g_cursorMethods{};
MonoInputMethods g_inputMethods{};
MonoFrameMethods g_frameMethods{};
MonoApplicationMethods g_applicationMethods{};
MonoObjectDestroyMethods g_objectDestroyMethods{};
Il2CppSceneMethods g_il2cppSceneMethods{};
Il2CppCursorMethods g_il2cppCursorMethods{};
Il2CppInputMethods g_il2cppInputMethods{};
Il2CppFrameMethods g_il2cppFrameMethods{};
Il2CppApplicationMethods g_il2cppApplicationMethods{};
Il2CppObjectDestroyMethods g_il2cppObjectDestroyMethods{};
MonoSceneLoadedFn g_originalMonoSceneLoaded = nullptr;
MonoActiveSceneChangedFn g_originalMonoActiveSceneChanged = nullptr;
Il2CppSceneLoadedFn g_originalIl2CppSceneLoaded = nullptr;
Il2CppActiveSceneChangedFn g_originalIl2CppActiveSceneChanged = nullptr;
MonoInputBoolFn g_originalMonoMouseButton = nullptr;
MonoInputBoolFn g_originalMonoMouseButtonDown = nullptr;
MonoInputBoolFn g_originalMonoMouseButtonUp = nullptr;
Il2CppInputBoolFn g_originalIl2CppMouseButton = nullptr;
Il2CppInputBoolFn g_originalIl2CppMouseButtonDown = nullptr;
Il2CppInputBoolFn g_originalIl2CppMouseButtonUp = nullptr;
MonoBeforeRenderFn g_originalMonoBeforeRender = nullptr;
Il2CppBeforeRenderFn g_originalIl2CppBeforeRender = nullptr;
MonoApplicationQuitFn g_originalMonoApplicationQuit = nullptr;
Il2CppApplicationQuitFn g_originalIl2CppApplicationQuit = nullptr;
MonoDestroyFn g_originalMonoDestroy = nullptr;
MonoDestroyImmediateFn g_originalMonoDestroyImmediate = nullptr;
Il2CppDestroyFn g_originalIl2CppDestroy = nullptr;
Il2CppDestroyImmediateFn g_originalIl2CppDestroyImmediate = nullptr;

URK_SceneInfo g_currentScene{};
bool g_haveScene = false;
bool g_sceneFailureLogged = false;
bool g_cursorFailureLogged = false;
bool g_inputFailureLogged = false;
bool g_pumpActiveLogged = false;
bool g_cursorOverrideActive = false;
CursorState g_savedCursorState{};
std::atomic<int> g_menuCursorDesired{-1};
CursorLeaseRegistry g_menuCursorLeases;
std::atomic_bool g_menuMouseCaptureDesired{false};
CursorLeaseRegistry g_menuMouseCaptureLeases;
std::atomic<int> g_menuCursorLastApplyResult{-1};
std::atomic<DWORD> g_unityMainThreadId{0};
bool g_mouseInputSuppressionInstalled = false;
bool g_monoFrameHookInstallFailed = false;
bool g_monoSceneHookInstallFailed = false;
bool g_monoMouseHookInstallFailed = false;
bool g_monoApplicationQuitHookInstallFailed = false;
bool g_monoObjectDestroyHookInstallFailed = false;
bool g_il2cppFrameHookInstallFailed = false;
bool g_il2cppSceneHookInstallFailed = false;
bool g_il2cppMouseHookInstallFailed = false;
bool g_il2cppApplicationQuitHookInstallFailed = false;
bool g_il2cppObjectDestroyHookInstallFailed = false;
bool g_il2cppFrameResolutionLogged = false;
bool g_il2cppSceneResolutionLogged = false;
bool g_il2cppCursorResolutionLogged = false;
bool g_il2cppInputResolutionLogged = false;
std::atomic_bool g_graphicsThreadFailureLogged{false};
std::atomic_bool g_graphicsInvokeFailureLogged{false};
std::atomic_bool g_applicationQuitObserved{false};
thread_local bool g_runtimePumpActive = false;
std::mutex g_monoActivationWorkerMutex;
std::mutex g_il2cppActivationWorkerMutex;
std::atomic<uint32_t> g_eventsGeneration{1};
HANDLE g_monoActivationWorker = nullptr;
DWORD g_monoActivationWorkerId = 0;
HANDLE g_il2cppActivationWorker = nullptr;
DWORD g_il2cppActivationWorkerId = 0;

void MonoSceneLoadedDetour(SceneValue scene, int32_t mode);
void MonoActiveSceneChangedDetour(SceneValue previousScene, SceneValue currentScene);
void Il2CppSceneLoadedDetour(SceneValue scene, int32_t mode, const void *methodInfo);
void Il2CppActiveSceneChangedDetour(SceneValue previousScene, SceneValue currentScene, const void *methodInfo);
bool MonoMouseButtonDetour(int32_t value);
bool MonoMouseButtonDownDetour(int32_t value);
bool MonoMouseButtonUpDetour(int32_t value);
bool Il2CppMouseButtonDetour(int32_t value, const void *methodInfo);
bool Il2CppMouseButtonDownDetour(int32_t value, const void *methodInfo);
bool Il2CppMouseButtonUpDetour(int32_t value, const void *methodInfo);
void MonoBeforeRenderDetour();
void Il2CppBeforeRenderDetour(const void *methodInfo);
void MonoApplicationQuitDetour();
void Il2CppApplicationQuitDetour(const void *methodInfo);
void MonoDestroyDetour(MonoObject *object, float delaySeconds);
void MonoDestroyImmediateDetour(MonoObject *object, bool allowDestroyingAssets);
void Il2CppDestroyDetour(Il2CppObject *object, float delaySeconds, const void *methodInfo);
void Il2CppDestroyImmediateDetour(Il2CppObject *object, bool allowDestroyingAssets, const void *methodInfo);
uint64_t TryActivateMonoRuntimeEvents(MonoApi &mono);
uint64_t TryActivateIl2CppRuntimeEvents(Il2CppApi &il2cpp);
void StartMonoActivationWorker(MonoApi &mono);
void StartIl2CppActivationWorker(Il2CppApi &il2cpp);
void StopMonoActivationWorker();
void StopIl2CppActivationWorker();

const char *BoolText(bool value) {
    return value ? "yes" : "no";
}

MonoThreadScope::Mode MonoScopeModeForCurrentThread() {
    const DWORD mainThreadId = g_unityMainThreadId.load(std::memory_order_acquire);
    if (mainThreadId != 0 && mainThreadId == GetCurrentThreadId())
        return MonoThreadScope::Mode::BorrowExistingThread;
    return MonoThreadScope::Mode::AttachCurrentThread;
}

template <typename SceneMethods>
void LogSceneMethodsUnavailable(const char *backend, const char *message, const SceneMethods &sceneMethods) {
    Log("[runtime][events][%s][WARNING] %s: "
        "SceneManager.GetActiveScene=%s Scene.IsValidInternal=%s "
        "Scene.GetNameInternal=%s Scene.GetBuildIndexInternal=%s "
        "SceneManager.Internal_SceneLoaded=%s SceneManager.Internal_ActiveSceneChanged=%s.",
        backend ? backend : "unknown", message ? message : "Scene events unavailable",
        BoolText(sceneMethods.getActiveScene != nullptr), BoolText(sceneMethods.isValidInternal != nullptr),
        BoolText(sceneMethods.getNameInternal != nullptr), BoolText(sceneMethods.getBuildIndexInternal != nullptr),
        BoolText(sceneMethods.internalSceneLoaded != nullptr),
        BoolText(sceneMethods.internalActiveSceneChanged != nullptr));
}

template <typename CursorMethods>
void LogCursorMethodsUnavailable(const char *backend, const char *message, const CursorMethods &cursorMethods) {
    Log("[runtime][events][%s][WARNING] %s: "
        "Cursor.get_visible=%s Cursor.set_visible=%s "
        "Cursor.get_lockState=%s Cursor.set_lockState=%s.",
        backend ? backend : "unknown", message ? message : "Cursor control unavailable",
        BoolText(cursorMethods.getVisible != nullptr), BoolText(cursorMethods.setVisible != nullptr),
        BoolText(cursorMethods.getLockState != nullptr), BoolText(cursorMethods.setLockState != nullptr));
}

template <typename InputMethods>
void LogInputMethodsUnavailable(const char *backend, const char *message, const InputMethods &inputMethods) {
    Log("[runtime][events][%s][WARNING] %s: "
        "Input.GetKey=%s Input.GetKeyDown=%s Input.GetKeyUp=%s "
        "Input.GetMouseButton=%s Input.GetMouseButtonDown=%s "
        "Input.GetMouseButtonUp=%s.",
        backend ? backend : "unknown", message ? message : "Input helpers unavailable",
        BoolText(inputMethods.getKey != nullptr), BoolText(inputMethods.getKeyDown != nullptr),
        BoolText(inputMethods.getKeyUp != nullptr), BoolText(inputMethods.getMouseButton != nullptr),
        BoolText(inputMethods.getMouseButtonDown != nullptr), BoolText(inputMethods.getMouseButtonUp != nullptr));
}

template <typename FrameMethods>
void LogFrameMethodsUnavailable(const char *backend, const char *message, const FrameMethods &frameMethods) {
    Log("[runtime][events][%s][WARNING] %s: "
        "Application.InvokeOnBeforeRender=%s "
        "SystemInfo.get_graphicsDeviceType=%s.",
        backend ? backend : "unknown", message ? message : "Frame pump unavailable",
        BoolText(frameMethods.invokeOnBeforeRender != nullptr),
        BoolText(frameMethods.getGraphicsDeviceType != nullptr));
}

const char *BackendText(RuntimeEventsBackend backend) {
    switch (backend) {
        case RuntimeEventsBackend::Mono:
            return "Mono";
        case RuntimeEventsBackend::Il2Cpp:
            return "IL2CPP";
        default:
            return "None";
    }
}

bool IsExecutableAddress(void *address) {
    if (!address)
        return false;

    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(address, &mbi, sizeof(mbi)))
        return false;
    if (mbi.State != MEM_COMMIT)
        return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS))
        return false;

    const DWORD protection = mbi.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
           protection == PAGE_EXECUTE_WRITECOPY;
}

HMODULE ModuleForAddress(void *address) {
    if (!address)
        return nullptr;
    HMODULE module = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(address), &module)) {
        return nullptr;
    }
    return module;
}

bool IsUnityPlayerExecutableAddress(void *address) {
    const HMODULE unityPlayer = GetModuleHandleA("UnityPlayer.dll");
    return unityPlayer && IsExecutableAddress(address) && ModuleForAddress(address) == unityPlayer;
}

void CopyText(char *output, size_t outputSize, const char *text) {
    if (!output || outputSize == 0)
        return;
    output[0] = '\0';
    if (!text)
        return;
    const size_t count = (std::min)(std::strlen(text), outputSize - 1);
    std::memcpy(output, text, count);
    output[count] = '\0';
}

class Il2CppRuntimeThreadScope {
  public:
    explicit Il2CppRuntimeThreadScope(Il2CppApi &il2cpp) : il2cpp_(il2cpp) {
        if (!il2cpp_.MetadataAccessReady() || !il2cpp_.thread_attach_available())
            return;

        Il2CppThread *current = il2cpp_.il2cpp_thread_current();
        if (current) {
            thread_ = current;
            attached_ = true;
            ownsAttach_ = false;
            return;
        }

        Il2CppDomain *domain = il2cpp_.Domain();
        if (!domain || !il2cpp_.il2cpp_thread_attach)
            return;

        thread_ = il2cpp_.il2cpp_thread_attach(domain);
        attached_ = thread_ != nullptr;
        ownsAttach_ = attached_;
    }

    ~Il2CppRuntimeThreadScope() {
        if (ownsAttach_ && thread_ && il2cpp_.il2cpp_thread_detach)
            il2cpp_.il2cpp_thread_detach(thread_);
    }

    bool IsAttached() const {
        return attached_;
    }

  private:
    Il2CppApi &il2cpp_;
    Il2CppThread *thread_ = nullptr;
    bool attached_ = false;
    bool ownsAttach_ = false;
};

MonoMethod *FindUnityMethod(MonoApi &mono, const char *namespc, const char *klass, const char *method, int argc,
                            const char **resolvedImage = nullptr) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll", nullptr};
    for (const char *image : images) {
        MonoMethod *resolved = mono.FindMethod(image, namespc, klass, method, argc);
        if (resolved) {
            if (resolvedImage)
                *resolvedImage = image ? image : "<any loaded Unity image>";
            return resolved;
        }
    }
    return nullptr;
}

MonoMethod *FindUnityMethodExact(MonoApi &mono, const char *namespc, const char *klass, const char *method,
                                 const char *const *parameterTypes, int parameterCount) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll", nullptr};
    for (const char *image : images) {
        MonoMethod *resolved = mono.FindMethodExact(image, namespc, klass, method, parameterTypes, parameterCount);
        if (resolved)
            return resolved;
    }
    return nullptr;
}

MonoMethod *FindUnityInputMethodExact(MonoApi &mono, const char *method, const char *const *parameterTypes,
                                      int parameterCount) {
    const char *images[] = {"UnityEngine.InputLegacyModule.dll", "UnityEngine.CoreModule.dll", "UnityEngine.dll",
                            nullptr};
    for (const char *image : images) {
        MonoMethod *resolved =
            mono.FindMethodExact(image, "UnityEngine", "Input", method, parameterTypes, parameterCount);
        if (resolved)
            return resolved;
    }
    return nullptr;
}

const Il2CppMethod *FindIl2CppUnityMethod(Il2CppApi &il2cpp, const char *namespc, const char *klass,
                                          const char *method, int argc) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        Il2CppClass *resolvedClass = il2cpp.FindClass(image, namespc, klass);
        if (!resolvedClass)
            continue;
        if (const Il2CppMethod *resolved = il2cpp.FindMethod(resolvedClass, method, argc))
            return resolved;
    }
    return nullptr;
}

const Il2CppMethod *FindIl2CppUnityMethodExact(Il2CppApi &il2cpp, const char *namespc, const char *klass,
                                                const char *method, const char *const *parameterTypes,
                                                int parameterCount) {
    const char *images[] = {"UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        Il2CppClass *resolvedClass = il2cpp.FindClass(image, namespc, klass);
        if (!resolvedClass)
            continue;
        const Il2CppMethod *resolved = il2cpp.FindMethodExact(resolvedClass, method, parameterTypes, parameterCount);
        if (resolved)
            return resolved;
    }
    return nullptr;
}

const Il2CppMethod *FindIl2CppUnityInputMethodExact(Il2CppApi &il2cpp, const char *method,
                                                     const char *const *parameterTypes, int parameterCount) {
    const char *images[] = {"UnityEngine.InputLegacyModule.dll", "UnityEngine.CoreModule.dll", "UnityEngine.dll"};
    for (const char *image : images) {
        Il2CppClass *inputClass = il2cpp.FindClass(image, "UnityEngine", "Input");
        if (!inputClass)
            continue;
        if (const Il2CppMethod *resolved =
                il2cpp.FindMethodExact(inputClass, method, parameterTypes, parameterCount)) {
            return resolved;
        }
    }
    return nullptr;
}

Il2CppSceneMethods ResolveIl2CppSceneMethods(Il2CppApi &il2cpp) {
    Il2CppSceneMethods methods{};

    methods.getActiveScene = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine.SceneManagement", "SceneManager",
                                                         "GetActiveScene", nullptr, 0);
    methods.internalSceneLoaded = FindIl2CppUnityMethod(il2cpp, "UnityEngine.SceneManagement", "SceneManager",
                                                        "Internal_SceneLoaded", 2);
    methods.internalActiveSceneChanged = FindIl2CppUnityMethod(
        il2cpp, "UnityEngine.SceneManagement", "SceneManager", "Internal_ActiveSceneChanged", 2);
    const char *int32[] = {"System.Int32"};
    methods.isValidInternal = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine.SceneManagement", "Scene",
                                                          "IsValidInternal", int32, 1);
    methods.getNameInternal = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine.SceneManagement", "Scene",
                                                          "GetNameInternal", int32, 1);
    methods.getBuildIndexInternal = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine.SceneManagement", "Scene",
                                                                "GetBuildIndexInternal", int32, 1);

    return methods;
}

Il2CppCursorMethods ResolveIl2CppCursorMethods(Il2CppApi &il2cpp) {
    Il2CppCursorMethods methods{};
    const char *boolean[] = {"System.Boolean"};
    const char *cursorLockMode[] = {"UnityEngine.CursorLockMode"};
    methods.getVisible = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "get_visible", nullptr, 0);
    if (!methods.getVisible)
        methods.getVisible = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "GetVisible", nullptr, 0);
    methods.setVisible = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "set_visible", boolean, 1);
    if (!methods.setVisible)
        methods.setVisible = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "SetVisible", boolean, 1);
    methods.getLockState = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "get_lockState", nullptr, 0);
    if (!methods.getLockState)
        methods.getLockState = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "GetLockState", nullptr, 0);
    methods.setLockState = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "set_lockState", cursorLockMode, 1);
    if (!methods.setLockState)
        methods.setLockState = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Cursor", "SetLockState",
                                                          cursorLockMode, 1);
    return methods;
}

Il2CppInputMethods ResolveIl2CppInputMethods(Il2CppApi &il2cpp) {
    Il2CppInputMethods methods{};
    const char *keyCode[] = {"UnityEngine.KeyCode"};
    const char *int32[] = {"System.Int32"};
    methods.getKey = FindIl2CppUnityInputMethodExact(il2cpp, "GetKey", keyCode, 1);
    methods.getKeyDown = FindIl2CppUnityInputMethodExact(il2cpp, "GetKeyDown", keyCode, 1);
    methods.getKeyUp = FindIl2CppUnityInputMethodExact(il2cpp, "GetKeyUp", keyCode, 1);
    methods.getMouseButton = FindIl2CppUnityInputMethodExact(il2cpp, "GetMouseButton", int32, 1);
    methods.getMouseButtonDown = FindIl2CppUnityInputMethodExact(il2cpp, "GetMouseButtonDown", int32, 1);
    methods.getMouseButtonUp = FindIl2CppUnityInputMethodExact(il2cpp, "GetMouseButtonUp", int32, 1);
    return methods;
}

Il2CppFrameMethods ResolveIl2CppFrameMethods(Il2CppApi &il2cpp) {
    Il2CppFrameMethods methods{};
    methods.invokeOnBeforeRender = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Application",
                                                               "InvokeOnBeforeRender", nullptr, 0);
    methods.getGraphicsDeviceType = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "SystemInfo",
                                                                "get_graphicsDeviceType", nullptr, 0);
    return methods;
}

Il2CppApplicationMethods ResolveIl2CppApplicationMethods(Il2CppApi &il2cpp) {
    Il2CppApplicationMethods methods{};
    methods.internalApplicationQuit = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Application",
                                                                  "Internal_ApplicationQuit", nullptr, 0);
    return methods;
}

Il2CppObjectDestroyMethods ResolveIl2CppObjectDestroyMethods(Il2CppApi &il2cpp) {
    Il2CppObjectDestroyMethods methods{};
    const char *objectAndSingle[] = {"UnityEngine.Object", "System.Single"};
    const char *objectAndBoolean[] = {"UnityEngine.Object", "System.Boolean"};
    methods.destroy =
        FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "Destroy", objectAndSingle, 2);
    methods.destroyImmediate = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "DestroyImmediate",
                                                           objectAndBoolean, 2);
    methods.getInstanceId =
        FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "GetInstanceID", nullptr, 0);
    methods.getName = FindIl2CppUnityMethodExact(il2cpp, "UnityEngine", "Object", "get_name", nullptr, 0);
    return methods;
}

MonoObject *InvokeMono(MonoMethod *method, void *object, void **params, const char *label, bool *ok) {
    if (ok)
        *ok = false;
    if (!g_mono || !method || !g_mono->runtime_invoke)
        return nullptr;

    MonoObject *managedException = nullptr;
    uint32_t nativeException = 0;
    MonoObject *result = g_mono->RuntimeInvokeSafe(method, object, params, &managedException, &nativeException);
    if (nativeException || managedException) {
        Log("[runtime][events][Mono] %s failed: nativeException=0x%08X "
            "managedException=%p.",
            label ? label : "managed invoke", nativeException, managedException);
        return nullptr;
    }

    if (ok)
        *ok = true;
    return result;
}

Il2CppObject *InvokeIl2Cpp(const Il2CppMethod *method, void *object, void **params, const char *label, bool *ok) {
    if (ok)
        *ok = false;
    if (!g_il2cpp || !method || !g_il2cpp->il2cpp_runtime_invoke)
        return nullptr;

    Il2CppObject *managedException = nullptr;
    Il2CppObject *result = g_il2cpp->il2cpp_runtime_invoke(method, object, params, &managedException);

    if (managedException) {
        if (g_il2cpp->il2cpp_format_exception) {
            char message[1024]{};
            g_il2cpp->il2cpp_format_exception(managedException, message, static_cast<int>(sizeof(message)));
            Log("[runtime][events][IL2CPP] %s failed: managedException=%p message=%s.",
                label ? label : "managed invoke", managedException, message);
        } else {
            Log("[runtime][events][IL2CPP] %s failed: managedException=%p.", label ? label : "managed invoke",
                managedException);
        }
        return nullptr;
    }

    if (ok)
        *ok = true;
    return result;
}

bool ReadIl2CppBoxedInt32(Il2CppObject *object, int32_t *value) {
    if (!g_il2cpp || !g_il2cpp->il2cpp_object_unbox || !object || !value)
        return false;
    void *slot = g_il2cpp->il2cpp_object_unbox(object);
    if (!slot)
        return false;
    *value = *static_cast<int32_t *>(slot);
    return true;
}

bool ReadIl2CppBoxedBool(Il2CppObject *object, bool *value) {
    if (!g_il2cpp || !g_il2cpp->il2cpp_object_unbox || !object || !value)
        return false;
    void *slot = g_il2cpp->il2cpp_object_unbox(object);
    if (!slot)
        return false;
    *value = *static_cast<unsigned char *>(slot) != 0;
    return true;
}

std::string Il2CppStringToUtf8(Il2CppObject *object) {
    if (!g_il2cpp || !object || !g_il2cpp->il2cpp_string_length || !g_il2cpp->il2cpp_string_chars)
        return {};

    auto *string = static_cast<Il2CppString *>(object);
    const int32_t length = g_il2cpp->il2cpp_string_length(string);
    const uint16_t *chars = g_il2cpp->il2cpp_string_chars(string);
    if (!chars || length <= 0)
        return {};

    const int required =
        WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(chars), length, nullptr, 0, nullptr, nullptr);
    if (required <= 0)
        return {};

    std::string text(static_cast<size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<LPCWCH>(chars), length, text.data(), required, nullptr, nullptr);
    return text;
}

bool ReadBoxedInt32(MonoObject *object, int32_t *value) {
    if (!g_mono || !g_mono->object_unbox || !object || !value)
        return false;
    void *slot = g_mono->object_unbox(object);
    if (!slot)
        return false;
    *value = *static_cast<int32_t *>(slot);
    return true;
}

bool ReadBoxedBool(MonoObject *object, bool *value) {
    if (!g_mono || !g_mono->object_unbox || !object || !value)
        return false;
    void *slot = g_mono->object_unbox(object);
    if (!slot)
        return false;
    *value = *static_cast<unsigned char *>(slot) != 0;
    return true;
}

std::string MonoStringToUtf8(MonoObject *object) {
    if (!g_mono || !g_mono->string_to_utf8 || !object)
        return {};
    if (!g_mono->free_) {
        static std::atomic_bool loggedMissingFree{false};
        if (!loggedMissingFree.exchange(true, std::memory_order_acq_rel)) {
            Log("[runtime][events][Mono][WARNING] mono_string_to_utf8 "
                "requires mono_free; string conversion disabled to avoid "
                "leaking runtime-allocated memory.");
        }
        return {};
    }
    char *raw = g_mono->string_to_utf8(static_cast<MonoString *>(object));
    if (!raw)
        return {};
    std::string text(raw);
    g_mono->free_(raw);
    return text;
}

void FillObjectDestroyRequest(URK_ObjectDestroyRequest *request, uintptr_t object, int32_t instanceId,
                              float delaySeconds, uint32_t flags, const std::string &name,
                              const std::string &typeName) {
    *request = {};
    request->size = sizeof(URK_ObjectDestroyRequest);
    request->flags = flags;
    request->objectAddress = object;
    request->instanceId = instanceId;
    request->delaySeconds = delaySeconds;
    CopyText(request->name, sizeof(request->name), name.c_str());
    CopyText(request->typeName, sizeof(request->typeName), typeName.c_str());
}

URK_ObjectDestroyRequest CaptureMonoObjectDestroyRequest(MonoObject *object, float delaySeconds, uint32_t flags) {
    int32_t instanceId = 0;
    std::string name;
    std::string typeName;

    if (object && g_mono) {
        if (g_mono->object_get_class && g_mono->class_get_name) {
            MonoClass *klass = g_mono->object_get_class(object);
            const char *className = klass ? g_mono->class_get_name(klass) : nullptr;
            if (className)
                typeName = className;
        }
        bool ok = false;
        if (g_objectDestroyMethods.getInstanceId) {
            MonoObject *result = InvokeMono(g_objectDestroyMethods.getInstanceId, object, nullptr,
                                            "Object.GetInstanceID", &ok);
            if (!ok || !ReadBoxedInt32(result, &instanceId))
                instanceId = 0;
        }
        if (g_objectDestroyMethods.getName) {
            MonoObject *result = InvokeMono(g_objectDestroyMethods.getName, object, nullptr, "Object.get_name", &ok);
            if (ok)
                name = MonoStringToUtf8(result);
        }
    }

    URK_ObjectDestroyRequest request{};
    FillObjectDestroyRequest(&request, reinterpret_cast<uintptr_t>(object), instanceId, delaySeconds, flags, name,
                             typeName);
    return request;
}

URK_ObjectDestroyRequest CaptureIl2CppObjectDestroyRequest(Il2CppObject *object, float delaySeconds, uint32_t flags) {
    int32_t instanceId = 0;
    std::string name;
    std::string typeName;

    if (object && g_il2cpp) {
        if (g_il2cpp->il2cpp_object_get_class && g_il2cpp->il2cpp_class_get_name) {
            Il2CppClass *klass = g_il2cpp->il2cpp_object_get_class(object);
            const char *className = klass ? g_il2cpp->il2cpp_class_get_name(klass) : nullptr;
            if (className)
                typeName = className;
        }
        bool ok = false;
        if (g_il2cppObjectDestroyMethods.getInstanceId) {
            Il2CppObject *result = InvokeIl2Cpp(g_il2cppObjectDestroyMethods.getInstanceId, object, nullptr,
                                                "Object.GetInstanceID", &ok);
            if (!ok || !ReadIl2CppBoxedInt32(result, &instanceId))
                instanceId = 0;
        }
        if (g_il2cppObjectDestroyMethods.getName) {
            Il2CppObject *result = InvokeIl2Cpp(g_il2cppObjectDestroyMethods.getName, object, nullptr,
                                                "Object.get_name", &ok);
            if (ok)
                name = Il2CppStringToUtf8(result);
        }
    }

    URK_ObjectDestroyRequest request{};
    FillObjectDestroyRequest(&request, reinterpret_cast<uintptr_t>(object), instanceId, delaySeconds, flags, name,
                             typeName);
    return request;
}

MonoSceneMethods ResolveSceneMethods(MonoApi &mono) {
    MonoSceneMethods methods{};
    methods.getActiveScene = FindUnityMethodExact(mono, "UnityEngine.SceneManagement", "SceneManager", "GetActiveScene",
                                                  nullptr, 0);
    methods.internalSceneLoaded = FindUnityMethod(mono, "UnityEngine.SceneManagement", "SceneManager",
                                                  "Internal_SceneLoaded", 2);
    methods.internalActiveSceneChanged = FindUnityMethod(mono, "UnityEngine.SceneManagement", "SceneManager",
                                                         "Internal_ActiveSceneChanged", 2);
    const char *int32[] = {"System.Int32"};
    methods.isValidInternal = FindUnityMethodExact(mono, "UnityEngine.SceneManagement", "Scene", "IsValidInternal", int32, 1);
    methods.getNameInternal = FindUnityMethodExact(mono, "UnityEngine.SceneManagement", "Scene", "GetNameInternal", int32, 1);
    methods.getBuildIndexInternal =
        FindUnityMethodExact(mono, "UnityEngine.SceneManagement", "Scene", "GetBuildIndexInternal", int32, 1);

    return methods;
}

MonoCursorMethods ResolveCursorMethods(MonoApi &mono) {
    MonoCursorMethods methods{};
    const char *boolean[] = {"System.Boolean"};
    const char *cursorLockMode[] = {"UnityEngine.CursorLockMode"};
    methods.getVisible = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "get_visible", nullptr, 0);
    if (!methods.getVisible)
        methods.getVisible = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "GetVisible", nullptr, 0);
    methods.setVisible = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "set_visible", boolean, 1);
    if (!methods.setVisible)
        methods.setVisible = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "SetVisible", boolean, 1);
    methods.getLockState = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "get_lockState", nullptr, 0);
    if (!methods.getLockState)
        methods.getLockState = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "GetLockState", nullptr, 0);
    methods.setLockState = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "set_lockState", cursorLockMode, 1);
    if (!methods.setLockState)
        methods.setLockState = FindUnityMethodExact(mono, "UnityEngine", "Cursor", "SetLockState", cursorLockMode, 1);
    return methods;
}

MonoInputMethods ResolveInputMethods(MonoApi &mono) {
    MonoInputMethods methods{};
    const char *keyCode[] = {"UnityEngine.KeyCode"};
    const char *int32[] = {"System.Int32"};
    methods.getKey = FindUnityInputMethodExact(mono, "GetKey", keyCode, 1);
    methods.getKeyDown = FindUnityInputMethodExact(mono, "GetKeyDown", keyCode, 1);
    methods.getKeyUp = FindUnityInputMethodExact(mono, "GetKeyUp", keyCode, 1);
    methods.getMouseButton = FindUnityInputMethodExact(mono, "GetMouseButton", int32, 1);
    methods.getMouseButtonDown = FindUnityInputMethodExact(mono, "GetMouseButtonDown", int32, 1);
    methods.getMouseButtonUp = FindUnityInputMethodExact(mono, "GetMouseButtonUp", int32, 1);
    return methods;
}

MonoFrameMethods ResolveFrameMethods(MonoApi &mono) {
    MonoFrameMethods methods{};
    methods.invokeOnBeforeRender =
        FindUnityMethodExact(mono, "UnityEngine", "Application", "InvokeOnBeforeRender", nullptr, 0);
    methods.getGraphicsDeviceType =
        FindUnityMethodExact(mono, "UnityEngine", "SystemInfo", "get_graphicsDeviceType", nullptr, 0);
    return methods;
}

MonoApplicationMethods ResolveApplicationMethods(MonoApi &mono) {
    MonoApplicationMethods methods{};
    methods.internalApplicationQuit =
        FindUnityMethodExact(mono, "UnityEngine", "Application", "Internal_ApplicationQuit", nullptr, 0);
    return methods;
}

MonoObjectDestroyMethods ResolveObjectDestroyMethods(MonoApi &mono) {
    MonoObjectDestroyMethods methods{};
    const char *objectAndSingle[] = {"UnityEngine.Object", "System.Single"};
    const char *objectAndBoolean[] = {"UnityEngine.Object", "System.Boolean"};
    methods.destroy =
        FindUnityMethodExact(mono, "UnityEngine", "Object", "Destroy", objectAndSingle, 2);
    methods.destroyImmediate = FindUnityMethodExact(mono, "UnityEngine", "Object", "DestroyImmediate",
                                                     objectAndBoolean, 2);
    methods.getInstanceId = FindUnityMethodExact(mono, "UnityEngine", "Object", "GetInstanceID", nullptr, 0);
    methods.getName = FindUnityMethodExact(mono, "UnityEngine", "Object", "get_name", nullptr, 0);
    return methods;
}

void FillSceneInfo(URK_SceneInfo *scene, int32_t handle, int32_t buildIndex, const std::string &name) {
    *scene = {};
    scene->size = sizeof(URK_SceneInfo);
    scene->buildIndex = buildIndex;
    scene->handle = handle;
    CopyText(scene->name, sizeof(scene->name), name.c_str());
}

bool PopulateMonoSceneFromHandle(int32_t handle, URK_SceneInfo *scene, std::string *reason,
                                 const char *invalidHandleReason, const char *invalidSceneReason) {
    if (!scene || !g_mono || !g_sceneMethods.getNameInternal) {
        if (reason)
            *reason = "Mono scene methods are incomplete";
        return false;
    }
    if (handle == 0) {
        if (reason)
            *reason = invalidHandleReason ? invalidHandleReason : "scene handle is invalid";
        return false;
    }

    bool ok = false;
    if (g_sceneMethods.isValidInternal) {
        void *validParams[] = {&handle};
        MonoObject *validObject =
            InvokeMono(g_sceneMethods.isValidInternal, nullptr, validParams, "Scene.IsValidInternal", &ok);
        bool valid = false;
        if (!ok || !ReadBoxedBool(validObject, &valid) || !valid) {
            if (reason)
                *reason = invalidSceneReason ? invalidSceneReason : "Scene.IsValidInternal rejected the scene handle";
            return false;
        }
    }

    void *params[] = {&handle};

    MonoObject *nameObject = InvokeMono(g_sceneMethods.getNameInternal, nullptr, params, "Scene.GetNameInternal", &ok);
    if (!ok) {
        if (reason)
            *reason = "Scene.GetNameInternal failed";
        return false;
    }

    int32_t buildIndex = -1;
    if (g_sceneMethods.getBuildIndexInternal) {
        MonoObject *buildIndexObject =
            InvokeMono(g_sceneMethods.getBuildIndexInternal, nullptr, params, "Scene.GetBuildIndexInternal", &ok);
        if (!ok || !ReadBoxedInt32(buildIndexObject, &buildIndex)) {
            if (reason)
                *reason = "Scene.GetBuildIndexInternal failed";
            return false;
        }
    }

    FillSceneInfo(scene, handle, buildIndex, MonoStringToUtf8(nameObject));
    return true;
}

bool ReadMonoActiveScene(URK_SceneInfo *scene, std::string *reason) {
    if (!scene || !g_mono || !g_sceneMethods.ready()) {
        if (reason)
            *reason = "Mono scene methods are incomplete";
        return false;
    }

    bool ok = false;
    MonoObject *sceneObject = InvokeMono(g_sceneMethods.getActiveScene, nullptr, nullptr, "SceneManager.GetActiveScene", &ok);
    if (!ok || !sceneObject) {
        if (reason)
            *reason = "SceneManager.GetActiveScene failed or returned null";
        return false;
    }
    if (!g_mono->object_unbox) {
        if (reason)
            *reason = "mono_object_unbox is unavailable for the Scene value returned by SceneManager.GetActiveScene";
        return false;
    }
    void *sceneValue = g_mono->object_unbox(sceneObject);
    if (!sceneValue) {
        if (reason)
            *reason = "mono_object_unbox failed for the Scene value returned by SceneManager.GetActiveScene";
        return false;
    }
    int32_t handle = 0;
    std::memcpy(&handle, sceneValue, sizeof(handle));

    if (handle == 0) {
        if (reason)
            *reason = "SceneManager.GetActiveScene returned an invalid scene handle";
        return false;
    }

    return PopulateMonoSceneFromHandle(handle, scene, reason,
                                       "SceneManager.GetActiveScene returned an invalid scene handle",
                                       "Scene.IsValidInternal rejected the active scene handle");
}

bool PopulateIl2CppSceneFromHandle(int32_t handle, URK_SceneInfo *scene, std::string *reason,
                                   const char *invalidHandleReason, const char *invalidSceneReason) {
    if (!scene || !g_il2cpp || !g_il2cppSceneMethods.getNameInternal) {
        if (reason)
            *reason = "IL2CPP scene methods are incomplete";
        return false;
    }
    if (handle == 0) {
        if (reason)
            *reason = invalidHandleReason ? invalidHandleReason : "scene handle is invalid";
        return false;
    }

    bool ok = false;
    if (g_il2cppSceneMethods.isValidInternal) {
        void *validParams[] = {&handle};
        Il2CppObject *validObject =
            InvokeIl2Cpp(g_il2cppSceneMethods.isValidInternal, nullptr, validParams, "Scene.IsValidInternal", &ok);

        bool valid = false;
        if (!ok || !ReadIl2CppBoxedBool(validObject, &valid) || !valid) {
            if (reason)
                *reason = invalidSceneReason ? invalidSceneReason : "Scene.IsValidInternal rejected the scene handle";
            return false;
        }
    }

    void *params[] = {&handle};

    Il2CppObject *nameObject =
        InvokeIl2Cpp(g_il2cppSceneMethods.getNameInternal, nullptr, params, "Scene.GetNameInternal", &ok);
    if (!ok) {
        if (reason)
            *reason = "Scene.GetNameInternal failed";
        return false;
    }

    int32_t buildIndex = -1;
    if (g_il2cppSceneMethods.getBuildIndexInternal) {
        Il2CppObject *buildIndexObject = InvokeIl2Cpp(g_il2cppSceneMethods.getBuildIndexInternal, nullptr, params,
                                                      "Scene.GetBuildIndexInternal", &ok);
        if (!ok || !ReadIl2CppBoxedInt32(buildIndexObject, &buildIndex)) {
            if (reason)
                *reason = "Scene.GetBuildIndexInternal failed";
            return false;
        }
    }

    FillSceneInfo(scene, handle, buildIndex, Il2CppStringToUtf8(nameObject));
    return true;
}

bool ReadIl2CppActiveScene(URK_SceneInfo *scene, std::string *reason) {
    if (!scene || !g_il2cpp || !g_il2cppSceneMethods.ready()) {
        if (reason)
            *reason = "IL2CPP scene methods are incomplete";
        return false;
    }

    bool ok = false;
    Il2CppObject *sceneObject =
        InvokeIl2Cpp(g_il2cppSceneMethods.getActiveScene, nullptr, nullptr, "SceneManager.GetActiveScene", &ok);
    if (!ok || !sceneObject) {
        if (reason)
            *reason = "SceneManager.GetActiveScene failed or returned null";
        return false;
    }
    if (!g_il2cpp->il2cpp_object_unbox) {
        if (reason)
            *reason = "il2cpp_object_unbox is unavailable for the Scene value returned by SceneManager.GetActiveScene";
        return false;
    }
    void *sceneValue = g_il2cpp->il2cpp_object_unbox(sceneObject);
    if (!sceneValue) {
        if (reason)
            *reason = "il2cpp_object_unbox failed for the Scene value returned by SceneManager.GetActiveScene";
        return false;
    }
    int32_t handle = 0;
    std::memcpy(&handle, sceneValue, sizeof(handle));

    if (handle == 0) {
        if (reason)
            *reason = "SceneManager.GetActiveScene returned an invalid scene handle";
        return false;
    }

    return PopulateIl2CppSceneFromHandle(handle, scene, reason,
                                         "SceneManager.GetActiveScene returned an invalid scene handle",
                                         "Scene.IsValidInternal rejected the active scene handle");
}

bool ReadActiveScene(URK_SceneInfo *scene, std::string *reason) {
    switch (g_backend) {
        case RuntimeEventsBackend::Mono:
            return ReadMonoActiveScene(scene, reason);
        case RuntimeEventsBackend::Il2Cpp:
            return ReadIl2CppActiveScene(scene, reason);
        default:
            if (reason)
                *reason = "runtime events backend is not configured";
            return false;
    }
}

bool ReadSceneByHandle(int32_t handle, URK_SceneInfo *scene, std::string *reason) {
    switch (g_backend) {
        case RuntimeEventsBackend::Mono:
            return PopulateMonoSceneFromHandle(handle, scene, reason, "scene handle is invalid",
                                               "Scene.IsValidInternal rejected the scene handle");
        case RuntimeEventsBackend::Il2Cpp:
            return PopulateIl2CppSceneFromHandle(handle, scene, reason, "scene handle is invalid",
                                                 "Scene.IsValidInternal rejected the scene handle");
        default:
            if (reason)
                *reason = "runtime events backend is not configured";
            return false;
    }
}

bool SameScene(const URK_SceneInfo &left, const URK_SceneInfo &right) {
    return left.buildIndex == right.buildIndex && left.handle == right.handle &&
           std::strncmp(left.name, right.name, sizeof(left.name)) == 0;
}

struct SceneTransition {
    URK_SceneInfo previous{};
    bool firstScene = false;
    bool changed = false;
    bool duplicate = false;
};

SceneTransition UpdateObservedScene(const URK_SceneInfo &current, bool markDuplicate = false) {
    SceneTransition transition{};
    std::lock_guard lock(g_eventsMutex);
    if (!g_haveScene) {
        g_currentScene = current;
        g_haveScene = true;
        transition.firstScene = true;
    } else if (!SameScene(g_currentScene, current)) {
        transition.previous = g_currentScene;
        g_currentScene = current;
        transition.changed = true;
    } else {
        transition.duplicate = markDuplicate;
    }
    return transition;
}

void PumpSceneEvents() {
    URK_SceneInfo current{};
    std::string reason;
    if (!ReadActiveScene(&current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] Scene event pump could not read "
                "UnityEngine.SceneManagement.SceneManager.GetActiveScene: %s.",
                BackendText(g_backend), reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }

    const SceneTransition transition = UpdateObservedScene(current);

    if (transition.firstScene) {
        Log("[runtime][events] scene loaded: name='%s' buildIndex=%d handle=%d.", current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneLoaded(current);
    } else if (transition.changed) {
        Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", transition.previous.name,
            transition.previous.buildIndex, transition.previous.handle, current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneChanged(transition.previous, current);
        ModLifecycle_DispatchSceneLoaded(current);
    }
}

void DispatchSceneLoadedFromHandle(int32_t handle, const char *source) {
    if (ModLifecycle_ShutdownStarted())
        return;

    URK_SceneInfo current{};
    std::string reason;
    if (!ReadSceneByHandle(handle, &current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] %s could not read scene handle=%d: %s.", BackendText(g_backend),
                source ? source : "scene hook", handle, reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }

    const SceneTransition transition = UpdateObservedScene(current, true);
    if (transition.duplicate)
        return;

    if (transition.changed) {
        Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", transition.previous.name,
            transition.previous.buildIndex, transition.previous.handle, current.name, current.buildIndex, current.handle);
        ModLifecycle_DispatchSceneChanged(transition.previous, current);
    }
    Log("[runtime][events] scene loaded: name='%s' buildIndex=%d handle=%d.", current.name, current.buildIndex,
        current.handle);
    ModLifecycle_DispatchSceneLoaded(current);
}

void DispatchActiveSceneChangedFromHandles(int32_t previousHandle, int32_t currentHandle, const char *source) {
    if (ModLifecycle_ShutdownStarted())
        return;

    URK_SceneInfo previous{};
    URK_SceneInfo current{};
    std::string reason;
    if (!ReadSceneByHandle(currentHandle, &current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] %s could not read current scene handle=%d: %s.",
                BackendText(g_backend), source ? source : "scene hook", currentHandle,
                reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }
    const bool havePrevious = ReadSceneByHandle(previousHandle, &previous, &reason);
    bool changed = false;
    {
        std::lock_guard lock(g_eventsMutex);
        if (!g_haveScene) {
            g_currentScene = current;
            g_haveScene = true;
            changed = havePrevious && !SameScene(previous, current);
        } else if (!SameScene(g_currentScene, current)) {
            if (!havePrevious)
                previous = g_currentScene;
            g_currentScene = current;
            changed = true;
        }
    }
    if (!changed)
        return;
    Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", previous.name, previous.buildIndex,
        previous.handle, current.name, current.buildIndex, current.handle);
    ModLifecycle_DispatchSceneChanged(previous, current);
}

void DispatchActiveSceneChangedFromActiveScene(const char *source) {
    if (ModLifecycle_ShutdownStarted())
        return;

    URK_SceneInfo current{};
    std::string reason;
    if (!ReadActiveScene(&current, &reason)) {
        if (!g_sceneFailureLogged) {
            Log("[runtime][events][%s][WARNING] %s could not read current "
                "active scene: %s.",
                BackendText(g_backend), source ? source : "scene hook",
                reason.empty() ? "unknown failure" : reason.c_str());
            g_sceneFailureLogged = true;
        }
        return;
    }

    const SceneTransition transition = UpdateObservedScene(current);

    if (transition.firstScene) {
        Log("[runtime][events] scene loaded: name='%s' buildIndex=%d handle=%d.", current.name, current.buildIndex,
            current.handle);
        ModLifecycle_DispatchSceneLoaded(current);
        return;
    }

    if (!transition.changed)
        return;

    Log("[runtime][events] scene changed: '%s'(%d/%d) -> '%s'(%d/%d).", transition.previous.name,
        transition.previous.buildIndex, transition.previous.handle, current.name, current.buildIndex, current.handle);
    ModLifecycle_DispatchSceneChanged(transition.previous, current);
    ModLifecycle_DispatchSceneLoaded(current);
}

void MonoSceneLoadedDetour(SceneValue scene, int32_t mode) {
    if (!g_originalMonoSceneLoaded)
        return;
    g_originalMonoSceneLoaded(scene, mode);
    DispatchSceneLoadedFromHandle(scene.handle, "SceneManager.Internal_SceneLoaded");
}

void MonoActiveSceneChangedDetour(SceneValue previousScene, SceneValue currentScene) {
    if (!g_originalMonoActiveSceneChanged)
        return;
    g_originalMonoActiveSceneChanged(previousScene, currentScene);
    DispatchActiveSceneChangedFromHandles(previousScene.handle, currentScene.handle,
                                          "SceneManager.Internal_ActiveSceneChanged");
}

void Il2CppSceneLoadedDetour(SceneValue scene, int32_t mode, const void *methodInfo) {
    if (!g_originalIl2CppSceneLoaded)
        return;
    g_originalIl2CppSceneLoaded(scene, mode, methodInfo);
    Il2CppRuntimeThreadScope scope(*g_il2cpp);
    if (scope.IsAttached())
        DispatchSceneLoadedFromHandle(scene.handle, "SceneManager.Internal_SceneLoaded");
}

void Il2CppActiveSceneChangedDetour(SceneValue previousScene, SceneValue currentScene, const void *methodInfo) {
    if (!g_originalIl2CppActiveSceneChanged)
        return;
    g_originalIl2CppActiveSceneChanged(previousScene, currentScene, methodInfo);
    Il2CppRuntimeThreadScope scope(*g_il2cpp);
    if (scope.IsAttached())
        DispatchActiveSceneChangedFromHandles(previousScene.handle, currentScene.handle,
                                              "SceneManager.Internal_ActiveSceneChanged");
}

void MonoBeforeRenderDetour() {
    g_unityMainThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    if (!g_originalMonoBeforeRender)
        return;

    g_originalMonoBeforeRender();
    RuntimeEvents_Pump();
}

void Il2CppBeforeRenderDetour(const void *methodInfo) {
    g_unityMainThreadId.store(GetCurrentThreadId(), std::memory_order_release);
    if (!g_originalIl2CppBeforeRender)
        return;

    g_originalIl2CppBeforeRender(methodInfo);
    RuntimeEvents_Pump();
}

void DispatchApplicationQuit(const char *backend) {
    if (!g_applicationQuitObserved.exchange(true, std::memory_order_acq_rel)) {
        Log("[runtime][events][%s] Application quit observed; starting loader shutdown.",
            backend ? backend : "unknown");
        Loader_Shutdown();
    }
}

void MonoApplicationQuitDetour() {
    DispatchApplicationQuit("Mono");
    if (g_originalMonoApplicationQuit)
        g_originalMonoApplicationQuit();
}

void Il2CppApplicationQuitDetour(const void *methodInfo) {
    DispatchApplicationQuit("IL2CPP");
    if (g_originalIl2CppApplicationQuit)
        g_originalIl2CppApplicationQuit(methodInfo);
}

void MonoDestroyDetour(MonoObject *object, float delaySeconds) {
    const URK_ObjectDestroyRequest request =
        CaptureMonoObjectDestroyRequest(object, delaySeconds, URK_OBJECT_DESTROY_REQUEST_NONE);
    if (g_originalMonoDestroy)
        g_originalMonoDestroy(object, delaySeconds);
    ModLifecycle_DispatchObjectDestroyRequested(request);
}

void MonoDestroyImmediateDetour(MonoObject *object, bool allowDestroyingAssets) {
    const uint32_t flags = URK_OBJECT_DESTROY_REQUEST_IMMEDIATE |
                           (allowDestroyingAssets ? URK_OBJECT_DESTROY_REQUEST_ALLOW_DESTROYING_ASSETS : 0u);
    const URK_ObjectDestroyRequest request =
        CaptureMonoObjectDestroyRequest(object, 0.0f, flags);
    if (g_originalMonoDestroyImmediate)
        g_originalMonoDestroyImmediate(object, allowDestroyingAssets);
    ModLifecycle_DispatchObjectDestroyRequested(request);
}

void Il2CppDestroyDetour(Il2CppObject *object, float delaySeconds, const void *methodInfo) {
    const URK_ObjectDestroyRequest request =
        CaptureIl2CppObjectDestroyRequest(object, delaySeconds, URK_OBJECT_DESTROY_REQUEST_NONE);
    if (g_originalIl2CppDestroy)
        g_originalIl2CppDestroy(object, delaySeconds, methodInfo);
    ModLifecycle_DispatchObjectDestroyRequested(request);
}

void Il2CppDestroyImmediateDetour(Il2CppObject *object, bool allowDestroyingAssets, const void *methodInfo) {
    const uint32_t flags = URK_OBJECT_DESTROY_REQUEST_IMMEDIATE |
                           (allowDestroyingAssets ? URK_OBJECT_DESTROY_REQUEST_ALLOW_DESTROYING_ASSETS : 0u);
    const URK_ObjectDestroyRequest request =
        CaptureIl2CppObjectDestroyRequest(object, 0.0f, flags);
    if (g_originalIl2CppDestroyImmediate)
        g_originalIl2CppDestroyImmediate(object, allowDestroyingAssets, methodInfo);
    ModLifecycle_DispatchObjectDestroyRequested(request);
}

bool ReadMonoCursorState(CursorState *state) {
    if (!state || !g_mono || !g_cursorMethods.ready())
        return false;

    bool ok = false;
    MonoObject *visibleObject = InvokeMono(g_cursorMethods.getVisible, nullptr, nullptr, "Cursor.get_visible", &ok);
    if (!ok || !ReadBoxedBool(visibleObject, &state->visible))
        return false;

    MonoObject *lockObject = InvokeMono(g_cursorMethods.getLockState, nullptr, nullptr, "Cursor.get_lockState", &ok);
    return ok && ReadBoxedInt32(lockObject, &state->lockState);
}

bool SetMonoCursorVisible(bool visible) {
    bool value = visible;
    void *params[] = {&value};
    bool ok = false;
    InvokeMono(g_cursorMethods.setVisible, nullptr, params, "Cursor.set_visible", &ok);
    return ok;
}

bool SetMonoCursorLockState(int32_t lockState) {
    void *params[] = {&lockState};
    bool ok = false;
    InvokeMono(g_cursorMethods.setLockState, nullptr, params, "Cursor.set_lockState", &ok);
    return ok;
}

bool ReadIl2CppCursorState(CursorState *state) {
    if (!state || !g_il2cpp || !g_il2cppCursorMethods.ready())
        return false;

    bool ok = false;
    Il2CppObject *visibleObject =
        InvokeIl2Cpp(g_il2cppCursorMethods.getVisible, nullptr, nullptr, "Cursor.get_visible", &ok);
    if (!ok || !ReadIl2CppBoxedBool(visibleObject, &state->visible))
        return false;

    Il2CppObject *lockObject =
        InvokeIl2Cpp(g_il2cppCursorMethods.getLockState, nullptr, nullptr, "Cursor.get_lockState", &ok);
    return ok && ReadIl2CppBoxedInt32(lockObject, &state->lockState);
}

bool SetIl2CppCursorVisible(bool visible) {
    bool value = visible;
    void *params[] = {&value};
    bool ok = false;
    InvokeIl2Cpp(g_il2cppCursorMethods.setVisible, nullptr, params, "Cursor.set_visible", &ok);
    return ok;
}

bool SetIl2CppCursorLockState(int32_t lockState) {
    void *params[] = {&lockState};
    bool ok = false;
    InvokeIl2Cpp(g_il2cppCursorMethods.setLockState, nullptr, params, "Cursor.set_lockState", &ok);
    return ok;
}

bool InvokeMonoInputBool(MonoMethod *method, int32_t value) {
    if (!method)
        return false;
    void *params[] = {&value};
    bool ok = false;
    MonoObject *result = InvokeMono(method, nullptr, params, "Input managed invoke", &ok);
    bool state = false;
    return ok && ReadBoxedBool(result, &state) && state;
}

bool InvokeIl2CppInputBool(const Il2CppMethod *method, int32_t value) {
    if (!method)
        return false;
    void *params[] = {&value};
    bool ok = false;
    Il2CppObject *result = InvokeIl2Cpp(method, nullptr, params, "Input managed invoke", &ok);
    bool state = false;
    return ok && ReadIl2CppBoxedBool(result, &state) && state;
}

bool ReadMonoGraphicsDeviceType(int32_t *value) {
    if (!value || !g_frameMethods.getGraphicsDeviceType)
        return false;
    bool ok = false;
    MonoObject *result =
        InvokeMono(g_frameMethods.getGraphicsDeviceType, nullptr, nullptr, "SystemInfo.get_graphicsDeviceType", &ok);
    return ok && ReadBoxedInt32(result, value);
}

bool ReadIl2CppGraphicsDeviceType(int32_t *value) {
    if (!value || !g_il2cppFrameMethods.getGraphicsDeviceType)
        return false;
    bool ok = false;
    Il2CppObject *result = InvokeIl2Cpp(g_il2cppFrameMethods.getGraphicsDeviceType, nullptr, nullptr,
                                        "SystemInfo.get_graphicsDeviceType", &ok);
    return ok && ReadIl2CppBoxedInt32(result, value);
}

bool MouseInputBlocked() {
    // Showing the cursor does not block mouse input by itself.
    return g_menuMouseCaptureDesired.load(std::memory_order_acquire);
}

bool MonoMouseButtonDetour(int32_t value) {
    return MouseInputBlocked() || !g_originalMonoMouseButton ? false : g_originalMonoMouseButton(value);
}
bool MonoMouseButtonDownDetour(int32_t value) {
    return MouseInputBlocked() || !g_originalMonoMouseButtonDown ? false : g_originalMonoMouseButtonDown(value);
}
bool MonoMouseButtonUpDetour(int32_t value) {
    return MouseInputBlocked() || !g_originalMonoMouseButtonUp ? false : g_originalMonoMouseButtonUp(value);
}
bool Il2CppMouseButtonDetour(int32_t value, const void *methodInfo) {
    return MouseInputBlocked() || !g_originalIl2CppMouseButton ? false : g_originalIl2CppMouseButton(value, methodInfo);
}
bool Il2CppMouseButtonDownDetour(int32_t value, const void *methodInfo) {
    return MouseInputBlocked() || !g_originalIl2CppMouseButtonDown ? false
                                                                   : g_originalIl2CppMouseButtonDown(value, methodInfo);
}
bool Il2CppMouseButtonUpDetour(int32_t value, const void *methodInfo) {
    return MouseInputBlocked() || !g_originalIl2CppMouseButtonUp ? false
                                                                 : g_originalIl2CppMouseButtonUp(value, methodInfo);
}

bool ReadInputStateByKind(int32_t value, int kind) {
    switch (g_backend) {
        case RuntimeEventsBackend::Mono:
            switch (kind) {
                case 0:
                    return InvokeMonoInputBool(g_inputMethods.getKey, value);
                case 1:
                    return InvokeMonoInputBool(g_inputMethods.getKeyDown, value);
                case 2:
                    return InvokeMonoInputBool(g_inputMethods.getKeyUp, value);
                case 3:
                    return InvokeMonoInputBool(g_inputMethods.getMouseButton, value);
                case 4:
                    return InvokeMonoInputBool(g_inputMethods.getMouseButtonDown, value);
                case 5:
                    return InvokeMonoInputBool(g_inputMethods.getMouseButtonUp, value);
                default:
                    return false;
            }
        case RuntimeEventsBackend::Il2Cpp:
            switch (kind) {
                case 0:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getKey, value);
                case 1:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getKeyDown, value);
                case 2:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getKeyUp, value);
                case 3:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getMouseButton, value);
                case 4:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getMouseButtonDown, value);
                case 5:
                    return InvokeIl2CppInputBool(g_il2cppInputMethods.getMouseButtonUp, value);
                default:
                    return false;
            }
        default:
            return false;
    }
}

bool ReadCursorState(CursorState *state) {
    switch (g_backend) {
        case RuntimeEventsBackend::Mono:
            return ReadMonoCursorState(state);
        case RuntimeEventsBackend::Il2Cpp:
            return ReadIl2CppCursorState(state);
        default:
            return false;
    }
}

bool SetCursorVisible(bool visible) {
    switch (g_backend) {
        case RuntimeEventsBackend::Mono:
            return SetMonoCursorVisible(visible);
        case RuntimeEventsBackend::Il2Cpp:
            return SetIl2CppCursorVisible(visible);
        default:
            return false;
    }
}

bool SetCursorLockState(int32_t lockState) {
    switch (g_backend) {
        case RuntimeEventsBackend::Mono:
            return SetMonoCursorLockState(lockState);
        case RuntimeEventsBackend::Il2Cpp:
            return SetIl2CppCursorLockState(lockState);
        default:
            return false;
    }
}

bool ApplyCursorState(const CursorState &state) {
    const bool visibleOk = SetCursorVisible(state.visible);
    const bool lockOk = SetCursorLockState(state.lockState);
    return visibleOk && lockOk;
}

void RestoreCursorOverrideForResetLocked() {
    if (!g_cursorOverrideActive)
        return;

    bool restored = false;
    switch (g_backend) {
        case RuntimeEventsBackend::Mono: {
            if (!g_mono)
                break;
            MonoThreadScope scope(*g_mono, Mono_PublishedDomain(), "runtime cursor override reset",
                                  MonoScopeModeForCurrentThread());
            restored = scope.IsAttached() && ApplyCursorState(g_savedCursorState);
            break;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!g_il2cpp)
                break;
            Il2CppRuntimeThreadScope scope(*g_il2cpp);
            restored = scope.IsAttached() && ApplyCursorState(g_savedCursorState);
            break;
        }
        default:
            break;
    }

    if (restored) {
        Log("[runtime][events] Unity cursor override reset; restored visible=%s "
            "lockState=%d.",
            BoolText(g_savedCursorState.visible), g_savedCursorState.lockState);
    } else {
        Log("[runtime][events][%s][WARNING] Unity cursor override reset failed; "
            "saved Cursor.visible/lockState could not be restored.",
            BackendText(g_backend));
    }
}

void ResetStateLocked() {
    RestoreCursorOverrideForResetLocked();

    if (g_originalMonoBeforeRender)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoBeforeRender),
                    reinterpret_cast<void *>(&MonoBeforeRenderDetour));
    if (g_originalIl2CppBeforeRender)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppBeforeRender),
                    reinterpret_cast<void *>(&Il2CppBeforeRenderDetour));
    if (g_originalMonoApplicationQuit)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoApplicationQuit),
                    reinterpret_cast<void *>(&MonoApplicationQuitDetour));
    if (g_originalIl2CppApplicationQuit)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppApplicationQuit),
                    reinterpret_cast<void *>(&Il2CppApplicationQuitDetour));
    if (g_originalMonoDestroy)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoDestroy), reinterpret_cast<void *>(&MonoDestroyDetour));
    if (g_originalMonoDestroyImmediate)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoDestroyImmediate),
                    reinterpret_cast<void *>(&MonoDestroyImmediateDetour));
    if (g_originalIl2CppDestroy)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppDestroy),
                    reinterpret_cast<void *>(&Il2CppDestroyDetour));
    if (g_originalIl2CppDestroyImmediate)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppDestroyImmediate),
                    reinterpret_cast<void *>(&Il2CppDestroyImmediateDetour));
    if (g_originalMonoSceneLoaded)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoSceneLoaded),
                    reinterpret_cast<void *>(&MonoSceneLoadedDetour));
    if (g_originalMonoActiveSceneChanged)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoActiveSceneChanged),
                    reinterpret_cast<void *>(&MonoActiveSceneChangedDetour));
    if (g_originalIl2CppSceneLoaded)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppSceneLoaded),
                    reinterpret_cast<void *>(&Il2CppSceneLoadedDetour));
    if (g_originalIl2CppActiveSceneChanged)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppActiveSceneChanged),
                    reinterpret_cast<void *>(&Il2CppActiveSceneChangedDetour));
    if (g_originalMonoMouseButton)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoMouseButton),
                    reinterpret_cast<void *>(&MonoMouseButtonDetour));
    if (g_originalMonoMouseButtonDown)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoMouseButtonDown),
                    reinterpret_cast<void *>(&MonoMouseButtonDownDetour));
    if (g_originalMonoMouseButtonUp)
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoMouseButtonUp),
                    reinterpret_cast<void *>(&MonoMouseButtonUpDetour));
    if (g_originalIl2CppMouseButton)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppMouseButton),
                    reinterpret_cast<void *>(&Il2CppMouseButtonDetour));
    if (g_originalIl2CppMouseButtonDown)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppMouseButtonDown),
                    reinterpret_cast<void *>(&Il2CppMouseButtonDownDetour));
    if (g_originalIl2CppMouseButtonUp)
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppMouseButtonUp),
                    reinterpret_cast<void *>(&Il2CppMouseButtonUpDetour));

    g_backend = RuntimeEventsBackend::None;
    g_mono = nullptr;
    g_il2cpp = nullptr;
    g_capabilities = URK_RUNTIME_CAP_NONE;
    g_sceneMethods = {};
    g_cursorMethods = {};
    g_inputMethods = {};
    g_frameMethods = {};
    g_applicationMethods = {};
    g_objectDestroyMethods = {};
    g_il2cppSceneMethods = {};
    g_il2cppCursorMethods = {};
    g_il2cppInputMethods = {};
    g_il2cppFrameMethods = {};
    g_il2cppApplicationMethods = {};
    g_il2cppObjectDestroyMethods = {};
    g_originalMonoSceneLoaded = nullptr;
    g_originalMonoActiveSceneChanged = nullptr;
    g_originalIl2CppSceneLoaded = nullptr;
    g_originalIl2CppActiveSceneChanged = nullptr;
    g_originalMonoMouseButton = nullptr;
    g_originalMonoMouseButtonDown = nullptr;
    g_originalMonoMouseButtonUp = nullptr;
    g_originalIl2CppMouseButton = nullptr;
    g_originalIl2CppMouseButtonDown = nullptr;
    g_originalIl2CppMouseButtonUp = nullptr;
    g_originalMonoBeforeRender = nullptr;
    g_originalIl2CppBeforeRender = nullptr;
    g_originalMonoApplicationQuit = nullptr;
    g_originalIl2CppApplicationQuit = nullptr;
    g_originalMonoDestroy = nullptr;
    g_originalMonoDestroyImmediate = nullptr;
    g_originalIl2CppDestroy = nullptr;
    g_originalIl2CppDestroyImmediate = nullptr;

    g_currentScene = {};
    g_haveScene = false;
    g_sceneFailureLogged = false;
    g_cursorFailureLogged = false;
    g_inputFailureLogged = false;
    g_pumpActiveLogged = false;
    g_cursorOverrideActive = false;
    g_savedCursorState = {};
    g_menuCursorDesired.store(-1, std::memory_order_release);
    g_menuCursorLeases.Clear();
    g_menuMouseCaptureDesired.store(false, std::memory_order_release);
    g_menuMouseCaptureLeases.Clear();
    g_menuCursorLastApplyResult.store(-1, std::memory_order_release);
    g_unityMainThreadId.store(0, std::memory_order_release);
    g_mouseInputSuppressionInstalled = false;
    g_monoFrameHookInstallFailed = false;
    g_monoSceneHookInstallFailed = false;
    g_monoMouseHookInstallFailed = false;
    g_monoApplicationQuitHookInstallFailed = false;
    g_monoObjectDestroyHookInstallFailed = false;
    g_il2cppFrameHookInstallFailed = false;
    g_il2cppSceneHookInstallFailed = false;
    g_il2cppMouseHookInstallFailed = false;
    g_il2cppApplicationQuitHookInstallFailed = false;
    g_il2cppObjectDestroyHookInstallFailed = false;
    g_il2cppFrameResolutionLogged = false;
    g_il2cppSceneResolutionLogged = false;
    g_il2cppCursorResolutionLogged = false;
    g_il2cppInputResolutionLogged = false;
    g_graphicsThreadFailureLogged.store(false, std::memory_order_release);
    g_graphicsInvokeFailureLogged.store(false, std::memory_order_release);
    g_applicationQuitObserved.store(false, std::memory_order_release);
}

bool SnapshotCursorRuntime(RuntimeEventsBackend *backend, MonoApi **mono, Il2CppApi **il2cpp, uint64_t *capabilities) {
    if (!backend || !mono || !il2cpp || !capabilities)
        return false;

    std::lock_guard lock(g_eventsMutex);
    *backend = g_backend;
    *mono = g_mono;
    *il2cpp = g_il2cpp;
    *capabilities = g_capabilities;
    return (*capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
}

bool SnapshotRuntime(RuntimeEventsBackend *backend, MonoApi **mono, Il2CppApi **il2cpp, uint64_t *capabilities) {
    if (!backend || !mono || !il2cpp || !capabilities)
        return false;

    std::lock_guard lock(g_eventsMutex);
    *backend = g_backend;
    *mono = g_mono;
    *il2cpp = g_il2cpp;
    *capabilities = g_capabilities;
    return g_backend != RuntimeEventsBackend::None;
}

void *Il2CppMethodPointerForHook(Il2CppApi &il2cpp, const Il2CppMethod *method) {
    void *target = il2cpp.MethodPointer(method);
    if (!target) {
        const URK_Il2CppApi *publicApi = ModApi_Il2Cpp(&il2cpp);
        const char *reason = publicApi && publicApi->last_error ? publicApi->last_error() : nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Managed native target resolution failed: method=%p reason=%s.",
            method, reason && reason[0] ? reason : "IL2CPP runtime did not provide a diagnostic");
    }
    return target;
}

bool InstallMonoFrameHook(MonoApi &mono, const MonoFrameMethods &methods, bool logFailures = true) {
    if (!methods.invokeOnBeforeRender || !mono.compile_method) {
        if (logFailures) {
            Log("[runtime][events][Mono][WARNING] Main-thread frame hook "
                "unavailable: Application.InvokeOnBeforeRender=%s "
                "mono_compile_method=%s.",
                BoolText(methods.invokeOnBeforeRender != nullptr), BoolText(mono.compile_method != nullptr));
        }
        return false;
    }

    void *target = mono.CompileMethodSafe(methods.invokeOnBeforeRender);
    if (!IsExecutableAddress(target)) {
        if (logFailures) {
            Log("[runtime][events][Mono][WARNING] Refusing to hook "
                "Application.InvokeOnBeforeRender: target=%p is not executable.",
                target);
        }
        return false;
    }

    g_originalMonoBeforeRender = reinterpret_cast<MonoBeforeRenderFn>(target);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalMonoBeforeRender),
                     reinterpret_cast<void *>(&MonoBeforeRenderDetour))) {
        g_originalMonoBeforeRender = nullptr;
        if (logFailures) {
            Log("[runtime][events][Mono][WARNING] Failed to hook "
                "Application.InvokeOnBeforeRender target=%p.",
                target);
        }
        return false;
    }

    Log("[SUCCESS][runtime][events][Mono] hooked Application.InvokeOnBeforeRender "
        "target=%p for once-per-frame main-thread dispatch.",
        target);
    return true;
}

bool InstallIl2CppFrameHook(Il2CppApi &il2cpp, const Il2CppFrameMethods &methods) {
    if (!methods.invokeOnBeforeRender) {
        Log("[runtime][events][IL2CPP][WARNING] Main-thread frame hook "
            "unavailable: Application.InvokeOnBeforeRender was not resolved.");
        return false;
    }

    void *target = Il2CppMethodPointerForHook(il2cpp, methods.invokeOnBeforeRender);
    if (!IsExecutableAddress(target)) {
        Log("[runtime][events][IL2CPP][WARNING] Refusing to hook "
            "Application.InvokeOnBeforeRender: target=%p is not executable.",
            target);
        return false;
    }

    g_originalIl2CppBeforeRender = reinterpret_cast<Il2CppBeforeRenderFn>(target);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppBeforeRender),
                     reinterpret_cast<void *>(&Il2CppBeforeRenderDetour))) {
        g_originalIl2CppBeforeRender = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook "
            "Application.InvokeOnBeforeRender target=%p.",
            target);
        return false;
    }

    Log("[SUCCESS][runtime][events][IL2CPP] hooked Application.InvokeOnBeforeRender "
        "target=%p for once-per-frame main-thread dispatch.",
        target);
    return true;
}

bool InstallMonoApplicationQuitHook(MonoApi &mono, const MonoApplicationMethods &methods) {
    if (!methods.internalApplicationQuit || !mono.compile_method) {
        Log("[runtime][events][Mono][WARNING] Application quit hook unavailable: "
            "Application.Internal_ApplicationQuit=%s mono_compile_method=%s.",
            BoolText(methods.internalApplicationQuit != nullptr), BoolText(mono.compile_method != nullptr));
        return false;
    }

    void *target = mono.CompileMethodSafe(methods.internalApplicationQuit);
    if (!IsExecutableAddress(target)) {
        Log("[runtime][events][Mono][WARNING] Refusing to hook "
            "Application.Internal_ApplicationQuit: target=%p is not executable.",
            target);
        return false;
    }

    g_originalMonoApplicationQuit = reinterpret_cast<MonoApplicationQuitFn>(target);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalMonoApplicationQuit),
                     reinterpret_cast<void *>(&MonoApplicationQuitDetour))) {
        g_originalMonoApplicationQuit = nullptr;
        Log("[runtime][events][Mono][WARNING] Failed to hook "
            "Application.Internal_ApplicationQuit target=%p.",
            target);
        return false;
    }

    Log("[SUCCESS][runtime][events][Mono] hooked Application.Internal_ApplicationQuit "
        "target=%p for loader shutdown.",
        target);
    return true;
}

bool InstallIl2CppApplicationQuitHook(Il2CppApi &il2cpp, const Il2CppApplicationMethods &methods) {
    if (!methods.internalApplicationQuit) {
        Log("[runtime][events][IL2CPP][WARNING] Application quit hook unavailable: "
            "Application.Internal_ApplicationQuit was not resolved.");
        return false;
    }

    void *target = Il2CppMethodPointerForHook(il2cpp, methods.internalApplicationQuit);
    if (!IsExecutableAddress(target)) {
        Log("[runtime][events][IL2CPP][WARNING] Refusing to hook "
            "Application.Internal_ApplicationQuit: target=%p is not executable.",
            target);
        return false;
    }

    g_originalIl2CppApplicationQuit = reinterpret_cast<Il2CppApplicationQuitFn>(target);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppApplicationQuit),
                     reinterpret_cast<void *>(&Il2CppApplicationQuitDetour))) {
        g_originalIl2CppApplicationQuit = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook "
            "Application.Internal_ApplicationQuit target=%p.",
            target);
        return false;
    }

    Log("[SUCCESS][runtime][events][IL2CPP] hooked Application.Internal_ApplicationQuit "
        "target=%p for loader shutdown.",
        target);
    return true;
}

bool InstallMonoObjectDestroyHooks(MonoApi &mono, const MonoObjectDestroyMethods &methods) {
    if (!methods.ready() || !mono.lookup_internal_call) {
        Log("[runtime][events][Mono][WARNING] Object destroy request hooks unavailable: "
            "Object.Destroy(Object,Single)=%s Object.DestroyImmediate(Object,Boolean)=%s "
            "mono_lookup_internal_call=%s.",
            BoolText(methods.destroy != nullptr), BoolText(methods.destroyImmediate != nullptr),
            BoolText(mono.lookup_internal_call != nullptr));
        return false;
    }

    void *destroyTarget = mono.lookup_internal_call(methods.destroy);
    void *destroyImmediateTarget = mono.lookup_internal_call(methods.destroyImmediate);
    if (!IsUnityPlayerExecutableAddress(destroyTarget) || !IsUnityPlayerExecutableAddress(destroyImmediateTarget)) {
        Log("[runtime][events][Mono][WARNING] Refusing object destroy request hooks: "
            "Destroy target=%p owner=%p UnityPlayer=%p valid=%s; "
            "DestroyImmediate target=%p owner=%p valid=%s.",
            destroyTarget, ModuleForAddress(destroyTarget), GetModuleHandleA("UnityPlayer.dll"),
            BoolText(IsUnityPlayerExecutableAddress(destroyTarget)), destroyImmediateTarget,
            ModuleForAddress(destroyImmediateTarget), BoolText(IsUnityPlayerExecutableAddress(destroyImmediateTarget)));
        return false;
    }

    g_originalMonoDestroy = reinterpret_cast<MonoDestroyFn>(destroyTarget);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalMonoDestroy), reinterpret_cast<void *>(&MonoDestroyDetour))) {
        g_originalMonoDestroy = nullptr;
        Log("[runtime][events][Mono][WARNING] Failed to hook Object.Destroy target=%p.", destroyTarget);
        return false;
    }

    g_originalMonoDestroyImmediate = reinterpret_cast<MonoDestroyImmediateFn>(destroyImmediateTarget);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalMonoDestroyImmediate),
                     reinterpret_cast<void *>(&MonoDestroyImmediateDetour))) {
        Hook_Detach(reinterpret_cast<void **>(&g_originalMonoDestroy), reinterpret_cast<void *>(&MonoDestroyDetour));
        g_originalMonoDestroy = nullptr;
        g_originalMonoDestroyImmediate = nullptr;
        Log("[runtime][events][Mono][WARNING] Failed to hook Object.DestroyImmediate target=%p; "
            "Object.Destroy hook was rolled back.",
            destroyImmediateTarget);
        return false;
    }

    Log("[SUCCESS][runtime][events][Mono] object destroy request hooks active: Destroy=%p DestroyImmediate=%p.",
        destroyTarget, destroyImmediateTarget);
    return true;
}

bool InstallIl2CppObjectDestroyHooks(Il2CppApi &il2cpp, const Il2CppObjectDestroyMethods &methods) {
    if (!methods.hooks_ready()) {
        Log("[runtime][events][IL2CPP][WARNING] Object destroy request hooks unavailable: "
            "Object.Destroy(Object,Single)=%s Object.DestroyImmediate(Object,Boolean)=%s.",
            BoolText(methods.destroy != nullptr), BoolText(methods.destroyImmediate != nullptr));
        return false;
    }

    void *destroyTarget = Il2CppMethodPointerForHook(il2cpp, methods.destroy);
    void *destroyImmediateTarget = Il2CppMethodPointerForHook(il2cpp, methods.destroyImmediate);
    if (!IsExecutableAddress(destroyTarget) || !IsExecutableAddress(destroyImmediateTarget)) {
        Log("[runtime][events][IL2CPP][WARNING] Refusing object destroy request hooks: "
            "Destroy target=%p owner=%p executable=%s; "
            "DestroyImmediate target=%p owner=%p executable=%s.",
            destroyTarget, ModuleForAddress(destroyTarget), BoolText(IsExecutableAddress(destroyTarget)),
            destroyImmediateTarget, ModuleForAddress(destroyImmediateTarget),
            BoolText(IsExecutableAddress(destroyImmediateTarget)));
        return false;
    }

    g_originalIl2CppDestroy = reinterpret_cast<Il2CppDestroyFn>(destroyTarget);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppDestroy),
                     reinterpret_cast<void *>(&Il2CppDestroyDetour))) {
        g_originalIl2CppDestroy = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook Object.Destroy target=%p.", destroyTarget);
        return false;
    }

    g_originalIl2CppDestroyImmediate = reinterpret_cast<Il2CppDestroyImmediateFn>(destroyImmediateTarget);
    if (!Hook_Attach(reinterpret_cast<void **>(&g_originalIl2CppDestroyImmediate),
                     reinterpret_cast<void *>(&Il2CppDestroyImmediateDetour))) {
        Hook_Detach(reinterpret_cast<void **>(&g_originalIl2CppDestroy),
                    reinterpret_cast<void *>(&Il2CppDestroyDetour));
        g_originalIl2CppDestroy = nullptr;
        g_originalIl2CppDestroyImmediate = nullptr;
        Log("[runtime][events][IL2CPP][WARNING] Failed to hook Object.DestroyImmediate target=%p; "
            "Object.Destroy hook was rolled back.", destroyImmediateTarget);
        return false;
    }

    Log("[SUCCESS][runtime][events][IL2CPP] object destroy request hooks active: Destroy=%p DestroyImmediate=%p.",
        destroyTarget, destroyImmediateTarget);
    return true;
}

bool InstallMonoMouseHooks(MonoApi &mono, const MonoInputMethods &methods) {
    if (!mono.compile_method)
        return false;
    MonoMethod *managedMethods[] = {methods.getMouseButton, methods.getMouseButtonDown, methods.getMouseButtonUp};
    void *detours[] = {reinterpret_cast<void *>(&MonoMouseButtonDetour),
                       reinterpret_cast<void *>(&MonoMouseButtonDownDetour),
                       reinterpret_cast<void *>(&MonoMouseButtonUpDetour)};
    MonoInputBoolFn *originals[] = {&g_originalMonoMouseButton, &g_originalMonoMouseButtonDown,
                                    &g_originalMonoMouseButtonUp};
    auto rollback = [&](int count) {
        for (int j = 0; j < count; ++j)
            Hook_Detach(reinterpret_cast<void **>(originals[j]), detours[j]);
        for (auto *original : originals)
            *original = nullptr;
    };
    for (int i = 0; i < 3; ++i) {
        void *target = mono.CompileMethodSafe(managedMethods[i]);
        if (!IsExecutableAddress(target)) {
            rollback(i);
            return false;
        }
        *originals[i] = reinterpret_cast<MonoInputBoolFn>(target);
        if (!Hook_Attach(reinterpret_cast<void **>(originals[i]), detours[i])) {
            rollback(i);
            return false;
        }
    }
    return true;
}

bool InstallIl2CppMouseHooks(Il2CppApi &il2cpp, const Il2CppInputMethods &methods) {
    const Il2CppMethod *managedMethods[] = {methods.getMouseButton, methods.getMouseButtonDown,
                                            methods.getMouseButtonUp};
    void *detours[] = {reinterpret_cast<void *>(&Il2CppMouseButtonDetour),
                       reinterpret_cast<void *>(&Il2CppMouseButtonDownDetour),
                       reinterpret_cast<void *>(&Il2CppMouseButtonUpDetour)};
    Il2CppInputBoolFn *originals[] = {&g_originalIl2CppMouseButton, &g_originalIl2CppMouseButtonDown,
                                      &g_originalIl2CppMouseButtonUp};
    auto rollback = [&](int count) {
        for (int j = 0; j < count; ++j)
            Hook_Detach(reinterpret_cast<void **>(originals[j]), detours[j]);
        for (auto *original : originals)
            *original = nullptr;
    };
    for (int i = 0; i < 3; ++i) {
        void *target = Il2CppMethodPointerForHook(il2cpp, managedMethods[i]);
        if (!IsExecutableAddress(target)) {
            rollback(i);
            return false;
        }
        *originals[i] = reinterpret_cast<Il2CppInputBoolFn>(target);
        if (!Hook_Attach(reinterpret_cast<void **>(originals[i]), detours[i])) {
            rollback(i);
            return false;
        }
    }
    return true;
}

bool InstallMonoSceneHooks(MonoApi &mono, const MonoSceneMethods &methods, bool logFailures = true) {
    bool installed = false;
    if (!mono.compile_method) {
        if (logFailures)
            Log("[runtime][events][Mono][WARNING] Scene hooks unavailable: mono_compile_method is missing.");
        return false;
    }

    const auto install = [&](MonoMethod *method, auto *&original, void *detour, const char *name) {
        if (!method)
            return;
        void *target = mono.CompileMethodSafe(method);
        if (!IsExecutableAddress(target)) {
            if (logFailures)
                Log("[runtime][events][Mono][WARNING] Refusing to hook %s: target=%p is not executable.", name, target);
            return;
        }
        original = FunctionPointerFromAddress<std::remove_reference_t<decltype(original)>>(target);
        if (Hook_Attach(reinterpret_cast<void **>(&original), detour)) {
            installed = true;
            Log("[SUCCESS][runtime][events][Mono] hooked %s target=%p.", name, target);
        } else {
            original = nullptr;
            if (logFailures)
                Log("[runtime][events][Mono][WARNING] Failed to hook %s.", name);
        }
    };
    install(methods.internalSceneLoaded, g_originalMonoSceneLoaded, reinterpret_cast<void *>(&MonoSceneLoadedDetour),
            "SceneManager.Internal_SceneLoaded");
    install(methods.internalActiveSceneChanged, g_originalMonoActiveSceneChanged,
            reinterpret_cast<void *>(&MonoActiveSceneChangedDetour), "SceneManager.Internal_ActiveSceneChanged");
    return installed;
}

bool InstallIl2CppSceneHooks(Il2CppApi &il2cpp, const Il2CppSceneMethods &methods) {
    bool installed = false;
    const auto install = [&](const Il2CppMethod *method, auto *&original, void *detour, const char *name) {
        if (!method)
            return;
        void *target = Il2CppMethodPointerForHook(il2cpp, method);
        if (!IsExecutableAddress(target)) {
            Log("[runtime][events][IL2CPP][WARNING] Refusing to hook %s: target=%p is not executable.", name, target);
            return;
        }
        original = FunctionPointerFromAddress<std::remove_reference_t<decltype(original)>>(target);
        if (Hook_Attach(reinterpret_cast<void **>(&original), detour)) {
            installed = true;
            Log("[SUCCESS][runtime][events][IL2CPP] hooked %s target=%p.", name, target);
        } else {
            original = nullptr;
            Log("[runtime][events][IL2CPP][WARNING] Failed to hook %s.", name);
        }
    };
    install(methods.internalSceneLoaded, g_originalIl2CppSceneLoaded, reinterpret_cast<void *>(&Il2CppSceneLoadedDetour),
            "SceneManager.Internal_SceneLoaded");
    install(methods.internalActiveSceneChanged, g_originalIl2CppActiveSceneChanged,
            reinterpret_cast<void *>(&Il2CppActiveSceneChangedDetour), "SceneManager.Internal_ActiveSceneChanged");
    return installed;
}

bool MonoActivationSettledLocked() {
    const bool frameSettled = (g_capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0 || g_monoFrameHookInstallFailed;
    const bool graphicsSettled =
        (g_capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0 || g_monoFrameHookInstallFailed;
    const bool sceneSettled = (g_capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0 || g_monoSceneHookInstallFailed;
    const bool cursorSettled = (g_capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
    const bool inputSettled = (g_capabilities & URK_RUNTIME_CAP_INPUT) != 0;
    const bool mouseSettled = g_mouseInputSuppressionInstalled || g_monoMouseHookInstallFailed;
    const bool objectDestroySettled =
        (g_capabilities & URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS) != 0 ||
        g_monoObjectDestroyHookInstallFailed;
    return frameSettled && graphicsSettled && sceneSettled && cursorSettled && inputSettled && mouseSettled &&
           objectDestroySettled;
}

bool Il2CppActivationSettledLocked() {
    const bool frameSettled = (g_capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0 || g_il2cppFrameHookInstallFailed;
    const bool graphicsSettled =
        (g_capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0 || g_il2cppFrameHookInstallFailed;
    const bool sceneSettled = (g_capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0 || g_il2cppSceneHookInstallFailed;
    const bool cursorSettled = (g_capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
    const bool inputSettled = (g_capabilities & URK_RUNTIME_CAP_INPUT) != 0;
    const bool mouseSettled = g_mouseInputSuppressionInstalled || g_il2cppMouseHookInstallFailed;
    return frameSettled && graphicsSettled && sceneSettled && cursorSettled && inputSettled && mouseSettled;
}

uint64_t TryActivateMonoRuntimeEvents(MonoApi &mono) {
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    bool needFrame = false;
    bool needGraphics = false;
    bool needScene = false;
    bool needCursor = false;
    bool needInput = false;
    bool needMouseSuppression = false;
    bool needApplicationQuit = false;
    bool needObjectDestroy = false;

    {
        std::lock_guard lock(g_eventsMutex);
        if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
            return g_capabilities;
        capabilities = g_capabilities;
        needFrame = (capabilities & URK_RUNTIME_CAP_MAIN_THREAD) == 0 && !g_monoFrameHookInstallFailed;
        needGraphics = (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0 && !g_monoFrameHookInstallFailed;
        needScene = (capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) == 0 && !g_monoSceneHookInstallFailed;
        needCursor = (capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) == 0;
        needInput = (capabilities & URK_RUNTIME_CAP_INPUT) == 0;
        needMouseSuppression = !g_mouseInputSuppressionInstalled && !g_monoMouseHookInstallFailed;
        needApplicationQuit = g_originalMonoApplicationQuit == nullptr && !g_monoApplicationQuitHookInstallFailed;
        needObjectDestroy = (capabilities & URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS) == 0 &&
                            !g_monoObjectDestroyHookInstallFailed;
        if (!needFrame && !needGraphics && !needScene && !needCursor && !needInput && !needMouseSuppression &&
            !needApplicationQuit && !needObjectDestroy) {
            return capabilities;
        }
    }

    if (needObjectDestroy) {
        const MonoObjectDestroyMethods objectDestroyMethods = ResolveObjectDestroyMethods(mono);
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
                return g_capabilities;
            g_objectDestroyMethods = objectDestroyMethods;
        }

        const bool installed = objectDestroyMethods.ready() && InstallMonoObjectDestroyHooks(mono, objectDestroyMethods);
        {
            std::lock_guard lock(g_eventsMutex);
            if (installed)
                g_capabilities |= URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS;
            else if (objectDestroyMethods.ready())
                g_monoObjectDestroyHookInstallFailed = true;
            capabilities = g_capabilities;
        }
        if (installed)
            Log("[SUCCESS][runtime][events][Mono] object destroy request events activated after UnityEngine methods became available.");
    }

    if (needFrame || needGraphics) {
        const MonoFrameMethods frameMethods = ResolveFrameMethods(mono);
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
                return g_capabilities;
            g_frameMethods = frameMethods;
        }

        const bool frameInstalled = needFrame && frameMethods.frame_ready() && InstallMonoFrameHook(mono, frameMethods);
        bool graphicsActivated = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (frameInstalled) {
                g_capabilities |= URK_RUNTIME_CAP_MAIN_THREAD;
            } else if (needFrame && frameMethods.frame_ready()) {
                g_monoFrameHookInstallFailed = true;
            }
            if ((g_capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0 && frameMethods.graphics_ready() &&
                (g_capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0) {
                g_capabilities |= URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE;
                graphicsActivated = true;
            }
            capabilities = g_capabilities;
        }
        if (frameInstalled) {
            MainThread_SetDispatchTargetAvailable(true);
            Log("[SUCCESS][runtime][events][Mono] main-thread frame pump activated after "
                "Application.InvokeOnBeforeRender became available.");
        }
        if (graphicsActivated) {
            Log("[SUCCESS][runtime][events][Mono] SystemInfo.graphicsDeviceType helper "
                "activated after UnityEngine methods became available.");
        }
    }

    if (needScene) {
        const MonoSceneMethods sceneMethods = ResolveSceneMethods(mono);
        if (sceneMethods.ready()) {
            {
                std::lock_guard lock(g_eventsMutex);
                if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
                    return g_capabilities;
                g_sceneMethods = sceneMethods;
            }

            const bool sceneInstalled = InstallMonoSceneHooks(mono, sceneMethods);
            {
                std::lock_guard lock(g_eventsMutex);
                g_capabilities |= URK_RUNTIME_CAP_SCENE_EVENTS;
                g_monoSceneHookInstallFailed = !sceneInstalled;
                capabilities = g_capabilities;
            }
            if (sceneInstalled) {
                Log("[SUCCESS][runtime][events][Mono] scene events activated after "
                    "UnityEngine methods became available.");
                DispatchActiveSceneChangedFromActiveScene("Mono runtime event activation");
            } else {
                Log("[runtime][events][Mono][WARNING] Native scene hooks are unavailable; "
                    "active-scene polling fallback is enabled. Additive scene loads that do not change the active "
                    "scene cannot be observed by the fallback.");
            }
        }
    }

    if (needApplicationQuit) {
        const MonoApplicationMethods applicationMethods = ResolveApplicationMethods(mono);
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
                return g_capabilities;
            g_applicationMethods = applicationMethods;
        }

        const bool applicationInstalled =
            applicationMethods.ready() && InstallMonoApplicationQuitHook(mono, applicationMethods);
        {
            std::lock_guard lock(g_eventsMutex);
            if (!applicationInstalled && applicationMethods.ready())
                g_monoApplicationQuitHookInstallFailed = true;
        }
        if (applicationInstalled) {
            Log("[SUCCESS][runtime][events][Mono] Application quit hook activated after "
                "Application.Internal_ApplicationQuit became available.");
        }
    }

    if (needCursor) {
        const MonoCursorMethods cursorMethods = ResolveCursorMethods(mono);
        if (cursorMethods.ready()) {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
                return g_capabilities;
            g_cursorMethods = cursorMethods;
            g_capabilities |= URK_RUNTIME_CAP_CURSOR_CONTROL;
            capabilities = g_capabilities;
            Log("[SUCCESS][runtime][events][Mono] cursor control activated after "
                "UnityEngine methods became available.");
        }
    }

    if (needInput || needMouseSuppression) {
        const MonoInputMethods inputMethods = ResolveInputMethods(mono);
        if (inputMethods.ready()) {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Mono || g_mono != &mono)
                return g_capabilities;
            g_inputMethods = inputMethods;
            if (needInput)
                g_capabilities |= URK_RUNTIME_CAP_INPUT;
            capabilities = g_capabilities;
        }
        const bool suppressionInstalled =
            inputMethods.ready() && needMouseSuppression && InstallMonoMouseHooks(mono, inputMethods);
        if (suppressionInstalled) {
            std::lock_guard lock(g_eventsMutex);
            g_mouseInputSuppressionInstalled = true;
            Log("[SUCCESS][runtime][events][Mono] mouse input suppression activated after "
                "UnityEngine methods became available.");
        } else if (inputMethods.ready() && needMouseSuppression) {
            std::lock_guard lock(g_eventsMutex);
            g_monoMouseHookInstallFailed = true;
            Log("[runtime][events][Mono][WARNING] Mouse input suppression hooks "
                "could not be installed; the same targets will not be retried.");
        }
    }

    return capabilities;
}

uint64_t TryActivateIl2CppRuntimeEvents(Il2CppApi &il2cpp) {
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    bool needFrame = false;
    bool needGraphics = false;
    bool needScene = false;
    bool needCursor = false;
    bool needInput = false;
    bool needMouseSuppression = false;
    bool needApplicationQuit = false;

    {
        std::lock_guard lock(g_eventsMutex);
        if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
            return g_capabilities;
        capabilities = g_capabilities;
        needFrame = (capabilities & URK_RUNTIME_CAP_MAIN_THREAD) == 0 && !g_il2cppFrameHookInstallFailed;
        needGraphics = (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0 && !g_il2cppFrameHookInstallFailed;
        needScene = (capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) == 0 && !g_il2cppSceneHookInstallFailed;
        needCursor = (capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) == 0;
        needInput = (capabilities & URK_RUNTIME_CAP_INPUT) == 0;
        needMouseSuppression = !g_mouseInputSuppressionInstalled && !g_il2cppMouseHookInstallFailed;
        needApplicationQuit =
            g_originalIl2CppApplicationQuit == nullptr && !g_il2cppApplicationQuitHookInstallFailed;
        if (!needFrame && !needGraphics && !needScene && !needCursor && !needInput && !needMouseSuppression &&
            !needApplicationQuit) {
            return capabilities;
        }
    }

    if (needFrame || needGraphics) {
        const Il2CppFrameMethods frameMethods = ResolveIl2CppFrameMethods(il2cpp);
        bool logIncompleteFrameMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppFrameMethods = frameMethods;
            if (!frameMethods.ready() && !g_il2cppFrameResolutionLogged) {
                g_il2cppFrameResolutionLogged = true;
                logIncompleteFrameMethods = true;
            }
        }
        if (logIncompleteFrameMethods) {
            LogFrameMethodsUnavailable(
                "IL2CPP", "Main-thread frame pump or graphics query not ready yet; late activation will retry",
                frameMethods);
        }

        const bool frameInstalled =
            needFrame && frameMethods.frame_ready() && InstallIl2CppFrameHook(il2cpp, frameMethods);
        bool graphicsActivated = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (frameInstalled) {
                g_capabilities |= URK_RUNTIME_CAP_MAIN_THREAD;
            } else if (needFrame && frameMethods.frame_ready()) {
                g_il2cppFrameHookInstallFailed = true;
            }
            if ((g_capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0 && frameMethods.graphics_ready() &&
                (g_capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0) {
                g_capabilities |= URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE;
                graphicsActivated = true;
            }
            capabilities = g_capabilities;
        }
        if (frameInstalled) {
            MainThread_SetDispatchTargetAvailable(true);
            Log("[SUCCESS][runtime][events][IL2CPP] main-thread frame pump activated after "
                "Application.InvokeOnBeforeRender became available.");
        }
        if (graphicsActivated) {
            Log("[runtime][events][IL2CPP] SystemInfo.graphicsDeviceType helper "
                "activated after UnityEngine methods became available.");
        }
    }

    if (needScene) {
        const Il2CppSceneMethods sceneMethods = ResolveIl2CppSceneMethods(il2cpp);
        bool logIncompleteSceneMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (!sceneMethods.ready() && !g_il2cppSceneResolutionLogged) {
                g_il2cppSceneResolutionLogged = true;
                logIncompleteSceneMethods = true;
            }
        }
        if (logIncompleteSceneMethods) {
            LogSceneMethodsUnavailable("IL2CPP", "Scene queries not ready yet; late activation will retry",
                                       sceneMethods);
        }
        if (sceneMethods.ready()) {
            {
                std::lock_guard lock(g_eventsMutex);
                if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                    return g_capabilities;
                g_il2cppSceneMethods = sceneMethods;
            }

            const bool sceneInstalled = InstallIl2CppSceneHooks(il2cpp, sceneMethods);
            {
                std::lock_guard lock(g_eventsMutex);
                g_capabilities |= URK_RUNTIME_CAP_SCENE_EVENTS;
                g_il2cppSceneHookInstallFailed = !sceneInstalled;
                capabilities = g_capabilities;
            }
            if (sceneInstalled) {
                Log("[SUCCESS][runtime][events][IL2CPP] scene hooks activated after UnityEngine methods became available.");
            } else {
                Log("[runtime][events][IL2CPP][WARNING] Native scene hooks are unavailable; "
                    "active-scene polling fallback is enabled. Additive scene loads that do not change the active "
                    "scene cannot be observed by the fallback.");
            }
        }
    }

    if (needApplicationQuit) {
        const Il2CppApplicationMethods applicationMethods = ResolveIl2CppApplicationMethods(il2cpp);
        {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppApplicationMethods = applicationMethods;
        }

        const bool applicationInstalled =
            applicationMethods.ready() && InstallIl2CppApplicationQuitHook(il2cpp, applicationMethods);
        {
            std::lock_guard lock(g_eventsMutex);
            if (!applicationInstalled && applicationMethods.ready())
                g_il2cppApplicationQuitHookInstallFailed = true;
        }
        if (applicationInstalled) {
            Log("[SUCCESS][runtime][events][IL2CPP] Application quit hook activated after "
                "Application.Internal_ApplicationQuit became available.");
        }
    }

    if (needCursor) {
        const Il2CppCursorMethods cursorMethods = ResolveIl2CppCursorMethods(il2cpp);
        bool logIncompleteCursorMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (!cursorMethods.ready() && !g_il2cppCursorResolutionLogged) {
                g_il2cppCursorResolutionLogged = true;
                logIncompleteCursorMethods = true;
            }
        }
        if (logIncompleteCursorMethods) {
            LogCursorMethodsUnavailable("IL2CPP", "Cursor control not ready yet; late activation will retry",
                                        cursorMethods);
        }
        if (cursorMethods.ready()) {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppCursorMethods = cursorMethods;
            g_capabilities |= URK_RUNTIME_CAP_CURSOR_CONTROL;
            capabilities = g_capabilities;
            Log("[SUCCESS][runtime][events][IL2CPP] cursor control activated after "
                "UnityEngine methods became available.");
        }
    }

    if (needInput || needMouseSuppression) {
        const Il2CppInputMethods inputMethods = ResolveIl2CppInputMethods(il2cpp);
        bool logIncompleteInputMethods = false;
        {
            std::lock_guard lock(g_eventsMutex);
            if (!inputMethods.ready() && !g_il2cppInputResolutionLogged) {
                g_il2cppInputResolutionLogged = true;
                logIncompleteInputMethods = true;
            }
        }
        if (logIncompleteInputMethods) {
            LogInputMethodsUnavailable(
                "IL2CPP",
                "Legacy input helpers not ready yet; searched InputLegacyModule, CoreModule, and UnityEngine",
                inputMethods);
        }
        if (inputMethods.ready()) {
            std::lock_guard lock(g_eventsMutex);
            if (g_backend != RuntimeEventsBackend::Il2Cpp || g_il2cpp != &il2cpp)
                return g_capabilities;
            g_il2cppInputMethods = inputMethods;
            if (needInput)
                g_capabilities |= URK_RUNTIME_CAP_INPUT;
            capabilities = g_capabilities;
        }
        const bool suppressionInstalled =
            inputMethods.ready() && needMouseSuppression && InstallIl2CppMouseHooks(il2cpp, inputMethods);
        if (suppressionInstalled) {
            std::lock_guard lock(g_eventsMutex);
            g_mouseInputSuppressionInstalled = true;
            Log("[SUCCESS][runtime][events][IL2CPP] mouse input suppression activated after "
                "UnityEngine methods became available.");
        } else if (inputMethods.ready() && needMouseSuppression) {
            std::lock_guard lock(g_eventsMutex);
            g_il2cppMouseHookInstallFailed = true;
            Log("[runtime][events][IL2CPP][WARNING] Mouse input suppression hooks "
                "could not be installed; the same targets will not be retried.");
        }
    }

    return capabilities;
}

struct MonoActivationWorkerArgs {
    MonoApi *mono = nullptr;
    uint32_t generation = 0;
};

struct Il2CppActivationWorkerArgs {
    Il2CppApi *il2cpp = nullptr;
    uint32_t generation = 0;
};

bool RuntimeEventsGenerationMatches(uint32_t generation) {
    return g_eventsGeneration.load(std::memory_order_acquire) == generation;
}

DWORD WINAPI MonoActivationWorkerProc(void *user) {
    auto *args = static_cast<MonoActivationWorkerArgs *>(user);
    MonoApi *mono = args ? args->mono : nullptr;
    const uint32_t generation = args ? args->generation : 0;
    delete args;

    constexpr DWORD kInitialDelayMs = 250;
    constexpr DWORD kRetryDelayMs = 500;
    constexpr int kMaxAttempts = 120;

    for (int attempt = 0; mono && attempt < kMaxAttempts; ++attempt) {
        if (!RuntimeEventsGenerationMatches(generation) || ModLifecycle_ShutdownStarted()) {
            break;
        }

        Sleep(attempt == 0 ? kInitialDelayMs : kRetryDelayMs);

        if (!RuntimeEventsGenerationMatches(generation) || ModLifecycle_ShutdownStarted()) {
            break;
        }

        MonoThreadScope scope(*mono, Mono_PublishedDomain(), "Mono runtime event late activation",
                              MonoScopeModeForCurrentThread());
        if (!scope.IsAttached())
            continue;

        TryActivateMonoRuntimeEvents(*mono);
        {
            std::lock_guard lock(g_eventsMutex);
            if (MonoActivationSettledLocked())
                break;
        }
    }

    if (mono && RuntimeEventsGenerationMatches(generation) && !ModLifecycle_ShutdownStarted()) {
        uint64_t capabilities = URK_RUNTIME_CAP_NONE;
        bool mouseSuppressionReady = false;
        {
            std::lock_guard lock(g_eventsMutex);
            capabilities = g_capabilities;
            mouseSuppressionReady = g_mouseInputSuppressionInstalled;
        }
        const bool mainThreadReady = (capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0;
        const bool graphicsReady = (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0;
        const bool sceneReady = (capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0;
        const bool cursorReady = (capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
        const bool inputReady = (capabilities & URK_RUNTIME_CAP_INPUT) != 0;
        if (!mainThreadReady || !graphicsReady || !sceneReady || !cursorReady || !inputReady ||
            !mouseSuppressionReady) {
            Log("[runtime][events][Mono][WARNING] Late UnityEngine event "
                "activation stopped: main thread=%s graphics device=%s "
                "scene events=%s cursor control=%s input=%s mouse suppression=%s.",
                BoolText(mainThreadReady), BoolText(graphicsReady), BoolText(sceneReady), BoolText(cursorReady),
                BoolText(inputReady), BoolText(mouseSuppressionReady));
        }
    }

    {
        std::lock_guard lock(g_monoActivationWorkerMutex);
        if (g_monoActivationWorkerId == GetCurrentThreadId()) {
            g_monoActivationWorkerId = 0;
        }
    }

    return 0;
}

void StartMonoActivationWorker(MonoApi &mono) {
    std::lock_guard lock(g_monoActivationWorkerMutex);
    if (g_monoActivationWorker) {
        if (WaitForSingleObject(g_monoActivationWorker, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_monoActivationWorker);
            g_monoActivationWorker = nullptr;
            g_monoActivationWorkerId = 0;
        } else {
            return;
        }
    }

    const uint32_t generation = g_eventsGeneration.load(std::memory_order_acquire);
    auto *args = new (std::nothrow) MonoActivationWorkerArgs{&mono, generation};
    if (!args) {
        Log("[runtime][events][Mono][WARNING] Late UnityEngine event "
            "activation worker could not allocate its startup context.");
        return;
    }
    DWORD threadId = 0;
    HANDLE thread = CreateThread(nullptr, 0, &MonoActivationWorkerProc, args, CREATE_SUSPENDED, &threadId);
    if (!thread) {
        delete args;
        Log("[runtime][events][Mono][WARNING] Late UnityEngine event "
            "activation worker could not be created: error=%lu.",
            GetLastError());
        return;
    }

    g_monoActivationWorker = thread;
    g_monoActivationWorkerId = threadId;
    ResumeThread(thread);
    Log("[runtime][events][Mono] Late UnityEngine event activation worker "
        "started.");
}

void StopMonoActivationWorker() {
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    {
        std::lock_guard lock(g_monoActivationWorkerMutex);
        g_eventsGeneration.fetch_add(1, std::memory_order_acq_rel);
        thread = g_monoActivationWorker;
        threadId = g_monoActivationWorkerId;
    }

    if (thread && threadId != GetCurrentThreadId()) {
        const DWORD waitResult = WaitForSingleObject(thread, 2000);
        if (waitResult == WAIT_OBJECT_0) {
            std::lock_guard lock(g_monoActivationWorkerMutex);
            if (g_monoActivationWorker == thread) {
                CloseHandle(thread);
                g_monoActivationWorker = nullptr;
                g_monoActivationWorkerId = 0;
            }
        } else {
            Log("[runtime][events][Mono][WARNING] Activation worker did not stop within 2000 ms; "
                "the loader remains pinned and the worker handle is retained.");
        }
    }
}

DWORD WINAPI Il2CppActivationWorkerProc(void *user) {
    auto *args = static_cast<Il2CppActivationWorkerArgs *>(user);
    Il2CppApi *il2cpp = args ? args->il2cpp : nullptr;
    const uint32_t generation = args ? args->generation : 0;
    delete args;

    constexpr DWORD kInitialDelayMs = 250;
    constexpr DWORD kRetryDelayMs = 500;
    constexpr int kMaxAttempts = 120;

    for (int attempt = 0; il2cpp && attempt < kMaxAttempts; ++attempt) {
        if (!RuntimeEventsGenerationMatches(generation) || ModLifecycle_ShutdownStarted()) {
            break;
        }

        Sleep(attempt == 0 ? kInitialDelayMs : kRetryDelayMs);

        if (!RuntimeEventsGenerationMatches(generation) || ModLifecycle_ShutdownStarted()) {
            break;
        }

        Il2CppRuntimeThreadScope scope(*il2cpp);
        if (!scope.IsAttached())
            continue;

        TryActivateIl2CppRuntimeEvents(*il2cpp);
        {
            std::lock_guard lock(g_eventsMutex);
            if (Il2CppActivationSettledLocked())
                break;
        }
    }

    if (il2cpp && RuntimeEventsGenerationMatches(generation) && !ModLifecycle_ShutdownStarted()) {
        uint64_t capabilities = URK_RUNTIME_CAP_NONE;
        bool mouseSuppressionReady = false;
        {
            std::lock_guard lock(g_eventsMutex);
            capabilities = g_capabilities;
            mouseSuppressionReady = g_mouseInputSuppressionInstalled;
        }
        const bool mainThreadReady = (capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0;
        const bool graphicsReady = (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0;
        const bool sceneReady = (capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0;
        const bool cursorReady = (capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0;
        const bool inputReady = (capabilities & URK_RUNTIME_CAP_INPUT) != 0;
        if (!mainThreadReady || !graphicsReady || !sceneReady || !cursorReady || !inputReady ||
            !mouseSuppressionReady) {
            Log("[runtime][events][IL2CPP][WARNING] Late UnityEngine event "
                "activation stopped: main thread=%s graphics device=%s "
                "scene events=%s cursor control=%s input=%s mouse suppression=%s.",
                BoolText(mainThreadReady), BoolText(graphicsReady), BoolText(sceneReady), BoolText(cursorReady),
                BoolText(inputReady), BoolText(mouseSuppressionReady));
        }
    }

    {
        std::lock_guard lock(g_il2cppActivationWorkerMutex);
        if (g_il2cppActivationWorkerId == GetCurrentThreadId()) {
            g_il2cppActivationWorkerId = 0;
        }
    }

    return 0;
}

void StartIl2CppActivationWorker(Il2CppApi &il2cpp) {
    std::lock_guard lock(g_il2cppActivationWorkerMutex);
    if (g_il2cppActivationWorker) {
        if (WaitForSingleObject(g_il2cppActivationWorker, 0) == WAIT_OBJECT_0) {
            CloseHandle(g_il2cppActivationWorker);
            g_il2cppActivationWorker = nullptr;
            g_il2cppActivationWorkerId = 0;
        } else {
            return;
        }
    }

    const uint32_t generation = g_eventsGeneration.load(std::memory_order_acquire);
    auto *args = new (std::nothrow) Il2CppActivationWorkerArgs{&il2cpp, generation};
    if (!args) {
        Log("[runtime][events][IL2CPP][WARNING] Late UnityEngine event "
            "activation worker could not allocate its startup context.");
        return;
    }
    DWORD threadId = 0;
    HANDLE thread = CreateThread(nullptr, 0, &Il2CppActivationWorkerProc, args, CREATE_SUSPENDED, &threadId);
    if (!thread) {
        delete args;
        Log("[runtime][events][IL2CPP][WARNING] Late UnityEngine event "
            "activation worker could not be created: error=%lu.",
            GetLastError());
        return;
    }

    g_il2cppActivationWorker = thread;
    g_il2cppActivationWorkerId = threadId;
    ResumeThread(thread);
    Log("[runtime][events][IL2CPP] Late UnityEngine event activation worker "
        "started.");
}

void StopIl2CppActivationWorker() {
    HANDLE thread = nullptr;
    DWORD threadId = 0;
    {
        std::lock_guard lock(g_il2cppActivationWorkerMutex);
        g_eventsGeneration.fetch_add(1, std::memory_order_acq_rel);
        thread = g_il2cppActivationWorker;
        threadId = g_il2cppActivationWorkerId;
    }

    if (thread && threadId != GetCurrentThreadId()) {
        const DWORD waitResult = WaitForSingleObject(thread, 2000);
        if (waitResult == WAIT_OBJECT_0) {
            std::lock_guard lock(g_il2cppActivationWorkerMutex);
            if (g_il2cppActivationWorker == thread) {
                CloseHandle(thread);
                g_il2cppActivationWorker = nullptr;
                g_il2cppActivationWorkerId = 0;
            }
        } else {
            Log("[runtime][events][IL2CPP][WARNING] Activation worker did not stop within 2000 ms; "
                "the loader remains pinned and the worker handle is retained.");
        }
    }
}

void PumpCursorControl() {
    std::lock_guard lock(g_eventsMutex);

    const int desired = g_menuCursorDesired.load(std::memory_order_acquire);
    if (desired < 0)
        return;

    if (desired != 0) {
        if (g_cursorOverrideActive) {
            if (!SetCursorLockState(URK_CURSOR_LOCK_NONE) || !SetCursorVisible(true)) {
                if (!g_cursorFailureLogged) {
                    Log("[runtime][events][%s][WARNING] Unity cursor menu "
                        "override reapply failed.",
                        BackendText(g_backend));
                    g_cursorFailureLogged = true;
                }
                g_cursorOverrideActive = false;
                g_menuCursorLastApplyResult.store(0, std::memory_order_release);
            } else {
                g_menuCursorLastApplyResult.store(1, std::memory_order_release);
            }
            return;
        }

        CursorState current{};
        if (!ReadCursorState(&current) || !SetCursorLockState(URK_CURSOR_LOCK_NONE) || !SetCursorVisible(true)) {
            if (!g_cursorFailureLogged) {
                Log("[runtime][events][%s][WARNING] Unity cursor menu override failed; "
                    "Cursor.visible/lockState could not be applied.",
                    BackendText(g_backend));
                g_cursorFailureLogged = true;
            }

            g_cursorOverrideActive = false;
            g_menuCursorLastApplyResult.store(0, std::memory_order_release);
            return;
        }

        g_savedCursorState = current;
        g_cursorOverrideActive = true;
        g_menuCursorLastApplyResult.store(1, std::memory_order_release);
        Log("[runtime][events] Unity cursor override enabled for native menu "
            "(saved visible=%s lockState=%d).",
            BoolText(current.visible), current.lockState);
        return;
    }

    if (!g_cursorOverrideActive) {
        g_menuCursorLastApplyResult.store(1, std::memory_order_release);
        return;
    }

    if (!ApplyCursorState(g_savedCursorState)) {
        if (!g_cursorFailureLogged) {
            Log("[runtime][events][%s][WARNING] Unity cursor restore failed; "
                "saved Cursor.visible/lockState could not be restored.",
                BackendText(g_backend));
            g_cursorFailureLogged = true;
        }

        g_cursorOverrideActive = false;
        g_menuCursorLastApplyResult.store(0, std::memory_order_release);
        return;
    }

    Log("[runtime][events] Unity cursor override disabled; restored visible=%s "
        "lockState=%d.",
        BoolText(g_savedCursorState.visible), g_savedCursorState.lockState);
    g_cursorOverrideActive = false;
    g_menuCursorLastApplyResult.store(1, std::memory_order_release);
}
} // namespace

void RuntimeEvents_Reset() {
    StopMonoActivationWorker();
    StopIl2CppActivationWorker();
    MainThread_SetDispatchTargetAvailable(false);
    std::lock_guard lock(g_eventsMutex);
    ResetStateLocked();
}

void RuntimeEvents_StopWorkers() {
    StopMonoActivationWorker();
    StopIl2CppActivationWorker();
}

uint64_t RuntimeEvents_Capabilities() {
    std::lock_guard lock(g_eventsMutex);
    return g_capabilities;
}

bool RuntimeEvents_WaitForMonoUnityReady(MonoApi &mono, int timeoutMs) {
    const ULONGLONG start = GetTickCount64();
    const ULONGLONG deadline = start + static_cast<ULONGLONG>((std::max)(timeoutMs, 0));
    ULONGLONG nextLog = start;
    MonoSceneMethods sceneMethods{};
    MonoCursorMethods cursorMethods{};
    MonoInputMethods inputMethods{};
    MonoFrameMethods frameMethods{};

    for (;;) {
        MonoThreadScope scope(mono, Mono_PublishedDomain(), "Mono UnityEngine readiness",
                              MonoScopeModeForCurrentThread());
        if (!scope.IsAttached()) {
            Log("[runtime][events][Mono][ERROR] UnityEngine readiness check failed: "
                "could not attach current thread to Mono.");
            return false;
        }

        sceneMethods = ResolveSceneMethods(mono);
        cursorMethods = ResolveCursorMethods(mono);
        inputMethods = ResolveInputMethods(mono);
        frameMethods = ResolveFrameMethods(mono);
        if (sceneMethods.ready() && cursorMethods.ready() && inputMethods.ready() && frameMethods.ready()) {
            Log("[SUCCESS][runtime][events][Mono] UnityEngine metadata ready: frame=yes "
                "graphics=yes scene=yes cursor=yes input=yes elapsed=%llums.",
                GetTickCount64() - start);
            return true;
        }

        const ULONGLONG now = GetTickCount64();
        if (now >= deadline)
            break;
        if (now >= nextLog) {
            Log("[runtime][events][Mono] Waiting for UnityEngine metadata: "
                "frame=%s graphics=%s scene=%s cursor=%s input=%s elapsed=%llums.",
                BoolText(frameMethods.frame_ready()), BoolText(frameMethods.graphics_ready()),
                BoolText(sceneMethods.ready()), BoolText(cursorMethods.ready()), BoolText(inputMethods.ready()),
                now - start);
            nextLog = now + 1000;
        }
        Sleep(50);
    }

    Log("[runtime][events][Mono][ERROR] Timed out after %d ms waiting for "
        "UnityEngine metadata: frame=%s graphics=%s scene=%s cursor=%s input=%s.",
        timeoutMs, BoolText(frameMethods.frame_ready()), BoolText(frameMethods.graphics_ready()),
        BoolText(sceneMethods.ready()), BoolText(cursorMethods.ready()), BoolText(inputMethods.ready()));
    return false;
}

void RuntimeEvents_AfterModsLoaded() {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    bool mouseSuppressionInstalled = false;
    {
        std::lock_guard lock(g_eventsMutex);
        backend = g_backend;
        mono = g_mono;
        il2cpp = g_il2cpp;
        capabilities = g_capabilities;
        mouseSuppressionInstalled = g_mouseInputSuppressionInstalled;
    }

    if (backend == RuntimeEventsBackend::None)
        return;

    const uint64_t required = URK_RUNTIME_CAP_MAIN_THREAD | URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE |
                              URK_RUNTIME_CAP_SCENE_EVENTS | URK_RUNTIME_CAP_CURSOR_CONTROL | URK_RUNTIME_CAP_INPUT;
    if ((capabilities & required) == required && mouseSuppressionInstalled)
        return;

    bool settled = false;
    bool frameHookFailed = false;
    bool sceneHookFailed = false;
    bool mouseHookFailed = false;
    {
        std::lock_guard lock(g_eventsMutex);
        if (backend == RuntimeEventsBackend::Mono) {
            settled = MonoActivationSettledLocked();
            frameHookFailed = g_monoFrameHookInstallFailed;
            sceneHookFailed = g_monoSceneHookInstallFailed;
            mouseHookFailed = g_monoMouseHookInstallFailed;
        } else if (backend == RuntimeEventsBackend::Il2Cpp) {
            settled = Il2CppActivationSettledLocked();
            frameHookFailed = g_il2cppFrameHookInstallFailed;
            sceneHookFailed = g_il2cppSceneHookInstallFailed;
            mouseHookFailed = g_il2cppMouseHookInstallFailed;
        }
    }
    if (settled) {
        Log("[runtime][events][%s][WARNING] Late UnityEngine event activation "
            "not started: unresolved services have deterministic hook failures "
            "(frame=%s scene=%s mouse suppression=%s); the same targets will not "
            "be retried.",
            BackendText(backend), BoolText(frameHookFailed), BoolText(sceneHookFailed), BoolText(mouseHookFailed));
        return;
    }

    if (backend == RuntimeEventsBackend::Mono && mono) {
        StartMonoActivationWorker(*mono);
    } else if (backend == RuntimeEventsBackend::Il2Cpp && il2cpp) {
        StartIl2CppActivationWorker(*il2cpp);
    }
}

uint64_t RuntimeEvents_ConfigureMono(MonoApi &mono) {
    RuntimeEvents_Reset();

    MonoThreadScope scope(mono, Mono_PublishedDomain(), "runtime events configuration",
                          MonoScopeModeForCurrentThread());
    if (!scope.IsAttached()) {
        Log("[runtime][events][Mono][WARNING] Runtime event configuration skipped: "
            "failed to attach current thread to Mono.");
        return URK_RUNTIME_CAP_NONE;
    }

    MonoSceneMethods sceneMethods = ResolveSceneMethods(mono);
    MonoCursorMethods cursorMethods = ResolveCursorMethods(mono);
    MonoInputMethods inputMethods = ResolveInputMethods(mono);
    MonoFrameMethods frameMethods = ResolveFrameMethods(mono);
    MonoApplicationMethods applicationMethods = ResolveApplicationMethods(mono);
    MonoObjectDestroyMethods objectDestroyMethods = ResolveObjectDestroyMethods(mono);

    if (!sceneMethods.ready()) {
        LogSceneMethodsUnavailable(
            "Mono", "Scene events not ready yet; late activation will retry after native mods load", sceneMethods);
    }
    if (!cursorMethods.ready()) {
        LogCursorMethodsUnavailable(
            "Mono", "Cursor control not ready yet; late activation will retry after native mods load", cursorMethods);
    }
    if (!inputMethods.ready()) {
        LogInputMethodsUnavailable(
            "Mono",
            "Legacy input helpers not ready yet; searched InputLegacyModule, CoreModule, UnityEngine, and loaded "
            "Unity images; late activation will retry after native mods load",
            inputMethods);
    }
    if (!frameMethods.ready()) {
        LogFrameMethodsUnavailable("Mono",
                                   "Main-thread frame pump or graphics device query not ready yet; late activation "
                                   "will retry after native mods load",
                                   frameMethods);
    }
    if (!applicationMethods.ready()) {
        Log("[runtime][events][Mono][WARNING] Application quit hook not ready yet; "
            "late activation will retry after native mods load.");
    }
    if (!objectDestroyMethods.ready()) {
        Log("[runtime][events][Mono][WARNING] Object destroy request hooks not ready yet: "
            "Object.Destroy(Object,Single)=%s Object.DestroyImmediate(Object,Boolean)=%s; "
            "late activation will retry after native mods load.",
            BoolText(objectDestroyMethods.destroy != nullptr),
            BoolText(objectDestroyMethods.destroyImmediate != nullptr));
    }
    {
        std::lock_guard lock(g_eventsMutex);
        g_backend = RuntimeEventsBackend::Mono;
        g_mono = &mono;
        g_sceneMethods = sceneMethods;
        g_cursorMethods = cursorMethods;
        g_inputMethods = inputMethods;
        g_frameMethods = frameMethods;
        g_applicationMethods = applicationMethods;
        g_objectDestroyMethods = objectDestroyMethods;
        g_capabilities = URK_RUNTIME_CAP_NONE;
    }

    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    const bool frameInstalled = frameMethods.frame_ready() && InstallMonoFrameHook(mono, frameMethods);
    if (frameInstalled) {
        capabilities |= URK_RUNTIME_CAP_MAIN_THREAD;
        if (frameMethods.graphics_ready())
            capabilities |= URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE;
    }
    const bool sceneInstalled = sceneMethods.ready() && InstallMonoSceneHooks(mono, sceneMethods);
    if (sceneMethods.ready())
        capabilities |= URK_RUNTIME_CAP_SCENE_EVENTS;
    if (sceneMethods.ready() && !sceneInstalled) {
        Log("[runtime][events][Mono][WARNING] Native scene hooks are unavailable; active-scene polling fallback is "
            "enabled. Additive scene loads that do not change the active scene cannot be observed by the fallback.");
    }
    const bool applicationQuitInstalled =
        applicationMethods.ready() && InstallMonoApplicationQuitHook(mono, applicationMethods);
    const bool objectDestroyInstalled =
        objectDestroyMethods.ready() && InstallMonoObjectDestroyHooks(mono, objectDestroyMethods);
    if (objectDestroyInstalled)
        capabilities |= URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS;
    if (cursorMethods.ready())
        capabilities |= URK_RUNTIME_CAP_CURSOR_CONTROL;
    bool mouseSuppressionInstalled = false;
    if (inputMethods.ready()) {
        capabilities |= URK_RUNTIME_CAP_INPUT;
        mouseSuppressionInstalled = InstallMonoMouseHooks(mono, inputMethods);
        if (!mouseSuppressionInstalled)
            Log("[runtime][events][Mono][WARNING] Mouse input suppression hooks could not be installed; Unity input "
                "helpers remain available.");
    }

    {
        std::lock_guard lock(g_eventsMutex);
        g_capabilities = capabilities;
        g_mouseInputSuppressionInstalled = mouseSuppressionInstalled;
        g_monoFrameHookInstallFailed = frameMethods.frame_ready() && !frameInstalled;
        g_monoSceneHookInstallFailed = sceneMethods.ready() && !sceneInstalled;
        g_monoMouseHookInstallFailed = inputMethods.ready() && !mouseSuppressionInstalled;
        g_monoApplicationQuitHookInstallFailed = applicationMethods.ready() && !applicationQuitInstalled;
        g_monoObjectDestroyHookInstallFailed = objectDestroyMethods.ready() && !objectDestroyInstalled;
    }
    MainThread_SetDispatchTargetAvailable((capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0);

    Log("[runtime][events][Mono] main thread=%s graphics device=%s scene events=%s "
        "cursor control=%s input=%s object destroy requests=%s.",
        BoolText((capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_INPUT) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS) != 0));
    return capabilities;
}

uint64_t RuntimeEvents_ConfigureIl2Cpp(Il2CppApi &il2cpp) {
    RuntimeEvents_Reset();

    Il2CppRuntimeThreadScope scope(il2cpp);
    if (!scope.IsAttached()) {
        Log("[runtime][events][IL2CPP][WARNING] Object destroy request hook configuration skipped: "
            "failed to attach current thread to the IL2CPP domain.");
        return URK_RUNTIME_CAP_NONE;
    }

    const Il2CppObjectDestroyMethods objectDestroyMethods = ResolveIl2CppObjectDestroyMethods(il2cpp);
    {
        std::lock_guard lock(g_eventsMutex);
        g_backend = RuntimeEventsBackend::Il2Cpp;
        g_il2cpp = &il2cpp;
        g_il2cppObjectDestroyMethods = objectDestroyMethods;
    }
    const bool objectDestroyInstalled = InstallIl2CppObjectDestroyHooks(il2cpp, objectDestroyMethods);
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (objectDestroyInstalled)
        capabilities |= URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS;

    {
        std::lock_guard lock(g_eventsMutex);
        g_capabilities = capabilities;
        g_il2cppObjectDestroyHookInstallFailed = !objectDestroyInstalled;
    }

    capabilities = TryActivateIl2CppRuntimeEvents(il2cpp);
    Log("[runtime][events][IL2CPP] main thread=%s graphics device=%s scene events=%s "
        "cursor control=%s input=%s object destroy requests=%s.",
        BoolText((capabilities & URK_RUNTIME_CAP_MAIN_THREAD) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0),
        BoolText((capabilities & URK_RUNTIME_CAP_INPUT) != 0), BoolText(objectDestroyInstalled));
    return capabilities;
}

void RuntimeEvents_Pump() {
    if (ModLifecycle_ShutdownStarted())
        return;
    if (g_runtimePumpActive)
        return;

    g_runtimePumpActive = true;
    struct PumpScope {
        ~PumpScope() {
            g_runtimePumpActive = false;
        }
    } pumpScope;

    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;

    {
        std::lock_guard lock(g_eventsMutex);
        backend = g_backend;
        mono = g_mono;
        il2cpp = g_il2cpp;
        capabilities = g_capabilities;
    }

    auto pump = [&]() {
        if (capabilities == URK_RUNTIME_CAP_NONE)
            return;

        {
            std::lock_guard lock(g_eventsMutex);
            if (!g_pumpActiveLogged) {
                Log("[runtime][events][%s] pump active "
                    "(capabilities=0x%llX).",
                    BackendText(backend), static_cast<unsigned long long>(capabilities));
                g_pumpActiveLogged = true;
            }
        }

        if ((capabilities & URK_RUNTIME_CAP_SCENE_EVENTS) != 0)
            PumpSceneEvents();

        if ((capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) != 0)
            PumpCursorControl();

        MainThread_Drain();
    };

    if (backend == RuntimeEventsBackend::Mono) {
        if (!mono)
            return;
        const DWORD mainThreadId = g_unityMainThreadId.load(std::memory_order_acquire);
        if (mainThreadId == 0 || mainThreadId != GetCurrentThreadId())
            return;

        MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime event pump", MonoScopeModeForCurrentThread());
        if (!scope.IsAttached())
            return;

        pump();
        return;
    }

    if (backend == RuntimeEventsBackend::Il2Cpp) {
        if (!il2cpp)
            return;
        if (capabilities == URK_RUNTIME_CAP_NONE)
            return;

        Il2CppRuntimeThreadScope scope(*il2cpp);
        if (!scope.IsAttached())
            return;

        pump();
        return;
    }
}

int RuntimeEvents_CurrentScene(URK_SceneInfo *scene) {
    if (!scene || scene->size < sizeof(URK_SceneInfo))
        return 0;

    std::lock_guard lock(g_eventsMutex);
    if (!g_haveScene)
        return 0;
    *scene = g_currentScene;
    scene->size = sizeof(URK_SceneInfo);
    return 1;
}

int RuntimeEvents_MenuCursorSetOpen(void *ownerModule, int open) {
    // References are counted per module so an unmatched close from one mod can
    // never release another mod's cursor ownership.
    if (!ownerModule)
        return 0;
    {
        std::lock_guard lock(g_eventsMutex);
        if ((g_capabilities & URK_RUNTIME_CAP_CURSOR_CONTROL) == 0)
            return 0;

        if (open) {
            g_menuCursorLeases.Acquire(ownerModule);
        } else {
            g_menuCursorLeases.Release(ownerModule);
        }
        g_menuCursorDesired.store(g_menuCursorLeases.AnyOpen() ? 1 : 0,
                                  std::memory_order_release);
    }
    // The caller is commonly an ImGui render/WndProc thread. Queue the state
    // change; Unity/Mono cursor calls are applied by the runtime event pump on
    // the Unity thread instead of synchronously on the render thread.
    if (g_unityMainThreadId.load(std::memory_order_acquire) == GetCurrentThreadId())
        RuntimeEvents_Pump();
    g_menuCursorLastApplyResult.store(1, std::memory_order_release);
    return 1;
}

int RuntimeEvents_MenuMouseCaptureSet(void *ownerModule, int capture) {
    if (!ownerModule)
        return 0;
    std::lock_guard lock(g_eventsMutex);
    if (!g_menuMouseCaptureLeases.Set(ownerModule, capture != 0))
        return 0;
    g_menuMouseCaptureDesired.store(g_menuMouseCaptureLeases.AnyOpen(), std::memory_order_release);
    return 1;
}

int RuntimeEvents_UnregisterModule(void *module) {
    if (!module)
        return 0;
    std::size_t released = 0;
    std::size_t releasedMouseCapture = 0;
    {
        std::lock_guard lock(g_eventsMutex);
        released = g_menuCursorLeases.ReleaseOwner(module);
        releasedMouseCapture = g_menuMouseCaptureLeases.ReleaseOwner(module);
        if (released != 0)
            g_menuCursorDesired.store(g_menuCursorLeases.AnyOpen() ? 1 : 0, std::memory_order_release);
        if (releasedMouseCapture != 0)
            g_menuMouseCaptureDesired.store(g_menuMouseCaptureLeases.AnyOpen(), std::memory_order_release);
    }
    if (released != 0) {
        Log("[runtime][events] released %zu cursor lease(s) for unloading module=%p.", released, module);
        if (g_unityMainThreadId.load(std::memory_order_acquire) == GetCurrentThreadId())
            RuntimeEvents_Pump();
    }
    return static_cast<int>(released + releasedMouseCapture);
}

int RuntimeEvents_CursorStateGet(URK_CursorState *state) {
    if (!state || state->size < sizeof(URK_CursorState))
        return 0;

    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotCursorRuntime(&backend, &mono, &il2cpp, &capabilities))
        return 0;

    CursorState current{};
    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime cursor state get",
                                  MonoScopeModeForCurrentThread());
            if (!scope.IsAttached() || !ReadCursorState(&current))
                return 0;
            break;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            if (!scope.IsAttached() || !ReadCursorState(&current))
                return 0;
            break;
        }
        default:
            return 0;
    }

    state->size = sizeof(URK_CursorState);
    state->visible = current.visible ? 1 : 0;
    state->lockState = current.lockState;
    return 1;
}

int RuntimeEvents_CursorStateSet(const URK_CursorState *state) {
    if (!state || state->size < sizeof(URK_CursorState))
        return 0;

    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotCursorRuntime(&backend, &mono, &il2cpp, &capabilities))
        return 0;

    CursorState desired{};
    desired.visible = state->visible != 0;
    desired.lockState = state->lockState;
    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime cursor state set",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ApplyCursorState(desired) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ApplyCursorState(desired) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetKey(int32_t keyCode) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime input get_key",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 0) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 0) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetKeyDown(int32_t keyCode) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime input get_key_down",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 1) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 1) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetKeyUp(int32_t keyCode) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime input get_key_up",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 2) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(keyCode, 2) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetMouseButton(int32_t button) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime input get_mouse_button",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadInputStateByKind(button, 3) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(button, 3) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetMouseButtonDown(int32_t button) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime input get_mouse_button_down",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadInputStateByKind(button, 4) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(button, 4) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int RuntimeEvents_InputGetMouseButtonUp(int32_t button) {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) || (capabilities & URK_RUNTIME_CAP_INPUT) == 0) {
        return 0;
    }

    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return 0;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime input get_mouse_button_up",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadInputStateByKind(button, 5) ? 1 : 0;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return 0;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadInputStateByKind(button, 5) ? 1 : 0;
        }
        default:
            return 0;
    }
}

int32_t RuntimeEvents_GraphicsDeviceType() {
    RuntimeEventsBackend backend = RuntimeEventsBackend::None;
    MonoApi *mono = nullptr;
    Il2CppApi *il2cpp = nullptr;
    uint64_t capabilities = URK_RUNTIME_CAP_NONE;
    if (!SnapshotRuntime(&backend, &mono, &il2cpp, &capabilities) ||
        (capabilities & URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE) == 0) {
        return URK_GRAPHICS_DEVICE_UNKNOWN;
    }

    const DWORD mainThreadId = g_unityMainThreadId.load(std::memory_order_acquire);
    if (mainThreadId == 0 || mainThreadId != GetCurrentThreadId())
        return URK_GRAPHICS_DEVICE_UNKNOWN;

    int32_t value = URK_GRAPHICS_DEVICE_UNKNOWN;
    switch (backend) {
        case RuntimeEventsBackend::Mono: {
            if (!mono)
                return URK_GRAPHICS_DEVICE_UNKNOWN;
            MonoThreadScope scope(*mono, Mono_PublishedDomain(), "runtime graphics device type",
                                  MonoScopeModeForCurrentThread());
            return scope.IsAttached() && ReadMonoGraphicsDeviceType(&value) ? value : URK_GRAPHICS_DEVICE_UNKNOWN;
        }
        case RuntimeEventsBackend::Il2Cpp: {
            if (!il2cpp)
                return URK_GRAPHICS_DEVICE_UNKNOWN;
            Il2CppRuntimeThreadScope scope(*il2cpp);
            return scope.IsAttached() && ReadIl2CppGraphicsDeviceType(&value) ? value : URK_GRAPHICS_DEVICE_UNKNOWN;
        }
        default:
            return URK_GRAPHICS_DEVICE_UNKNOWN;
    }
}
