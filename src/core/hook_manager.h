#pragma once

#include "mod_sdk.h"

int HookManager_Attach(void **original, void *detour, const URK_HookOptions *options);

int HookManager_Detach(void **original, void *detour);

// Detached hook count, or -1 when one could not be detached (keep the module loaded).
int HookManager_DetachModule(void *module);

int HookManager_BackendAvailable(uint32_t backend);

int HookManager_MidHooksAvailable();

URK_MidHookHandle *HookManager_MidAttach(void *target, URK_MidHookCallbackFn callback,
                                         const URK_MidHookOptions *options);

int HookManager_MidDetach(URK_MidHookHandle *hook);

int HookManager_MidSetEnabled(URK_MidHookHandle *hook, int enabled);
