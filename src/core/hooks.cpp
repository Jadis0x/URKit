#include "hooks.h"
#include "hook_manager.h"

bool Hook_Attach(void **ppOriginal, void *pDetour) {
    URK_HookOptions options{};
    options.size = sizeof(options);
    options.backend = URK_HOOK_BACKEND_AUTO;
    options.flags = 0;

    return HookManager_Attach(ppOriginal, pDetour, &options) != 0;
}

bool Hook_Detach(void **ppOriginal, void *pDetour) {
    return HookManager_Detach(ppOriginal, pDetour) != 0;
}

bool Hook_AttachEx(void **ppOriginal, void *pDetour, const URK_HookOptions *options) {
    return HookManager_Attach(ppOriginal, pDetour, options) != 0;
}

bool Hook_DetachEx(void **ppOriginal, void *pDetour) {
    return HookManager_Detach(ppOriginal, pDetour) != 0;
}

bool Hook_BackendAvailable(uint32_t backend) {
    return HookManager_BackendAvailable(backend) != 0;
}

bool Hook_MidAvailable() {
    return HookManager_MidHooksAvailable() != 0;
}

URK_MidHookHandle *Hook_MidAttach(void *target, URK_MidHookCallbackFn callback, const URK_MidHookOptions *options) {
    return HookManager_MidAttach(target, callback, options);
}

bool Hook_MidDetach(URK_MidHookHandle *hook) {
    return HookManager_MidDetach(hook) != 0;
}

bool Hook_MidSetEnabled(URK_MidHookHandle *hook, bool enabled) {
    return HookManager_MidSetEnabled(hook, enabled ? 1 : 0) != 0;
}