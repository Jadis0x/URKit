#pragma once

#include "mod_sdk.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

void ModLifecycle_DispatchSceneLoaded(const URK_SceneInfo &scene);
void ModLifecycle_DispatchSceneChanged(const URK_SceneInfo &previousScene, const URK_SceneInfo &currentScene);
void ModLifecycle_DispatchObjectDestroyRequested(const URK_ObjectDestroyRequest &request);
bool ModLifecycle_UnloadModule(HMODULE module, const char *reason);
bool ModLifecycle_ShutdownStarted();
