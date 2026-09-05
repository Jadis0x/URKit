#pragma once

#include "mod_sdk.h"

// Isolates every SafetyHook header from the rest of the runtime: those headers
// pull in std::expected and therefore require C++23 from their translation unit.

bool SafetyHookBackend_Available();

// Installs an inline hook and reports the trampoline that reaches the original
// code. Returns an opaque handle, or nullptr on failure.
void *SafetyHookBackend_CreateInline(void *target, void *detour, void **trampoline);

bool SafetyHookBackend_DestroyInline(void *handle);

// A mid hook callback carries no user pointer, so the loader owns a fixed pool
// of dispatch thunks and passes the slot that identifies this hook.
constexpr unsigned SafetyHookBackend_MidSlotCount = 128;

using SafetyHookBackend_MidDispatchFn = void (*)(unsigned slot, URK_HookRegisters *registers);

void SafetyHookBackend_SetMidDispatch(SafetyHookBackend_MidDispatchFn dispatch);

void *SafetyHookBackend_CreateMid(void *target, unsigned slot);

bool SafetyHookBackend_DestroyMid(void *handle);

bool SafetyHookBackend_SetMidEnabled(void *handle, bool enabled);
