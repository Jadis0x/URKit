#pragma once

int MainThread_Register(void (*callback)());
int MainThread_Unregister(void (*callback)());
void MainThread_UnregisterModule(void *module);
bool MainThread_HasDispatchTarget();
void MainThread_SetDispatchTargetAvailable(bool available);
void MainThread_Drain();
