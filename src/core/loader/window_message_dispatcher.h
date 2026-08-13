#pragma once

#include "mod_sdk.h"

int WindowMessage_Register(void *window, URK_WindowMessageCallback callback);
int WindowMessage_Unregister(void *window, URK_WindowMessageCallback callback);
intptr_t WindowMessage_CallOriginal(void *window, uint32_t message, uintptr_t wparam, intptr_t lparam);

// Removes all callbacks whose function address belongs to module. Returns the
// number removed, or -1 when a callback is still executing on the caller.
int WindowMessage_UnregisterModule(void *module);
