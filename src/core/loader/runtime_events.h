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
