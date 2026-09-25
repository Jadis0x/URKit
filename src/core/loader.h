#pragma once

enum class LoaderStartMode {
    Proxy,
    Injected,
};

enum class LoaderRunStatus {
    Succeeded,
    Skipped,
    Failed,
};

// Loader entry, on its own thread: resolve backend, wait for runtime, init APIs, load mods.
LoaderRunStatus Loader_Run(LoaderStartMode mode = LoaderStartMode::Proxy);

// Calls ModShutdown in reverse load order and releases loader state. Idempotent.
void Loader_Shutdown();
