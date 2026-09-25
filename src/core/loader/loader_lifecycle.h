#pragma once

#include "loader.h"

#include <windows.h>

// Elects one runtime owner per process; other proxies only forward.
bool LoaderLifecycle_TryStart(HMODULE module, LoaderStartMode mode);

// DllMain-safe: never waits or runs callbacks under the loader lock.
void LoaderLifecycle_RequestStopFromDllMain();
void LoaderLifecycle_ReleaseProcessResourcesFromDllMain();

bool LoaderLifecycle_StopRequested();
HANDLE LoaderLifecycle_StopEvent();
