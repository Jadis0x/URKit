#pragma once

#include "mod_sdk.h"

#include <cstdint>

struct MonoApi;
struct Il2CppApi;

void RuntimeEvents_Reset();
void RuntimeEvents_StopWorkers();
bool RuntimeEvents_WaitForMonoUnityReady(MonoApi &mono, int timeoutMs);
uint64_t RuntimeEvents_ConfigureMono(MonoApi &mono);
uint64_t RuntimeEvents_ConfigureIl2Cpp(Il2CppApi &il2cpp);
uint64_t RuntimeEvents_Capabilities();
void RuntimeEvents_AfterModsLoaded();
void RuntimeEvents_Pump();
int RuntimeEvents_CurrentScene(URK_SceneInfo *scene);
// Scene reports for backends without Unity hooks; distinct forces a reload of the same map.
void RuntimeEvents_ObserveScene(const URK_SceneInfo &scene, bool distinct);
void RuntimeEvents_SetMainThread(unsigned long threadId);

// Cursor access for a non-Unity engine; called only on its game thread.
struct RuntimeCursorProvider {
    bool (*read)(URK_CursorState *state) = nullptr;
    bool (*setVisible)(bool visible) = nullptr;
    bool (*setLockState)(int32_t lockState) = nullptr;
    // Set: the menu lease calls this instead of saving and applying state.
    bool (*setMenuOpen)(bool open) = nullptr;
};

// Menu cursor leases, save and restore work as under Unity; the engine pumps.
void RuntimeEvents_ConfigureExternal(const char *name, uint64_t capabilities, const RuntimeCursorProvider &cursor);
void RuntimeEvents_PumpExternal();
int RuntimeEvents_MenuCursorSetOpen(void *ownerModule, int open);
int RuntimeEvents_MenuMouseCaptureSet(void *ownerModule, int capture);
int RuntimeEvents_UnregisterModule(void *module);
int RuntimeEvents_CursorStateGet(URK_CursorState *state);
int RuntimeEvents_CursorStateSet(const URK_CursorState *state);
int RuntimeEvents_InputGetKey(int32_t keyCode);
int RuntimeEvents_InputGetKeyDown(int32_t keyCode);
int RuntimeEvents_InputGetKeyUp(int32_t keyCode);
int RuntimeEvents_InputGetMouseButton(int32_t button);
int RuntimeEvents_InputGetMouseButtonDown(int32_t button);
int RuntimeEvents_InputGetMouseButtonUp(int32_t button);
int32_t RuntimeEvents_GraphicsDeviceType();
int RuntimeEvents_IsMainThread();
