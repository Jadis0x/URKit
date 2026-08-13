#pragma once

#include "mod_sdk.h"
#include <windows.h>

bool Hook_Attach(void **ppOriginal, void *pDetour);
bool Hook_Detach(void **ppOriginal, void *pDetour);

bool Hook_AttachEx(void **ppOriginal, void *pDetour, const URK_HookOptions *options);

bool Hook_DetachEx(void **ppOriginal, void *pDetour);

bool Hook_BackendAvailable(uint32_t backend);
