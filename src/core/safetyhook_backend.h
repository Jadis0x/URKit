#pragma once

#include "mod_sdk.h"

#include <cstddef>
#include <cstdint>

// Keeps SafetyHook headers (C++23) out of the rest of the runtime.

bool SafetyHookBackend_Available();

// Inline hook; returns an opaque handle or nullptr.
void *SafetyHookBackend_CreateInline(void *target, void *detour, void **trampoline);

bool SafetyHookBackend_DestroyInline(void *handle);

// Mid hooks carry no user pointer, so a fixed thunk pool identifies the slot.
constexpr unsigned SafetyHookBackend_MidSlotCount = 128;

using SafetyHookBackend_MidDispatchFn = void (*)(unsigned slot, URK_HookRegisters *registers);

void SafetyHookBackend_SetMidDispatch(SafetyHookBackend_MidDispatchFn dispatch);

void *SafetyHookBackend_CreateMid(void *target, unsigned slot);

bool SafetyHookBackend_DestroyMid(void *handle);

bool SafetyHookBackend_SetMidEnabled(void *handle, bool enabled);

// Length of the instruction at code (Zydis), or 0.
std::size_t SafetyHookBackend_InstructionLength(const std::uint8_t *code, std::size_t available);
