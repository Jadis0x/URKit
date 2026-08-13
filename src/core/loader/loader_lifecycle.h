#pragma once

#include "loader.h"

#include <windows.h>

// Elects one URKit runtime owner in the current process and starts its
// bootstrap worker. Other proxy DLLs remain available for forwarding only.
bool LoaderLifecycle_TryStart(HMODULE module, LoaderStartMode mode);

// DllMain-safe stop notification. This never waits, invokes callbacks, or
// performs loader cleanup while the Windows loader lock is held.
void LoaderLifecycle_RequestStopFromDllMain();
void LoaderLifecycle_ReleaseProcessResourcesFromDllMain();

bool LoaderLifecycle_StopRequested();
HANDLE LoaderLifecycle_StopEvent();
