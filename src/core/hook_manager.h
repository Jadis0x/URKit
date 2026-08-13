#pragma once

#include "mod_sdk.h"

int HookManager_Attach(void **original, void *detour, const URK_HookOptions *options);

int HookManager_Detach(void **original, void *detour);

// Returns the number of detached hooks, or -1 if any owned hook could not be
// detached and the module must remain loaded.
int HookManager_DetachModule(void *module);

int HookManager_BackendAvailable(uint32_t backend);
