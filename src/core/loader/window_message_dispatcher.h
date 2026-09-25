#pragma once

#include "mod_sdk.h"

int WindowMessage_Register(void *window, URK_WindowMessageCallback callback);
int WindowMessage_Unregister(void *window, URK_WindowMessageCallback callback);
intptr_t WindowMessage_CallOriginal(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam);

// Removes the module's callbacks. -1 when one is still running on this thread.
int WindowMessage_UnregisterModule(void *module);
