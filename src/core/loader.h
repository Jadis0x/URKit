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

// Main loader entry. Runs on its own thread:
//   1. resolves the selected Unity scripting backend
//   2. waits for runtime/domain readiness
//   3. initializes runtime API, hooks, main-thread callbacks, and SDK tooling
//   4. scans the configured Mods directory using the active build policy
LoaderRunStatus Loader_Run(LoaderStartMode mode = LoaderStartMode::Proxy);

// Process detach lifecycle. Calls optional native ModShutdown exports in
// reverse load order, clears per-module runtime callbacks, and releases loader
// state. Safe to call more than once.
void Loader_Shutdown();
