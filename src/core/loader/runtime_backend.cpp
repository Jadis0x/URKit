#include "runtime_backend.h"

#include "loader_lifecycle.h"
#include "loader_paths.h"
#include "logger.h"
#include "runtime_discovery.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <string>

namespace {
constexpr DWORD kBackendDiscoveryTimeoutMs = 30000;

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

struct BackendResolution {
    const RuntimeBackendDescriptor &backend;
    const char *reason;
};

bool RunUnavailable(Config &) {
    Log("[runtime][ERROR] No Unity scripting runtime was loaded in this process.");
    return false;
}

const RuntimeBackendDescriptor kUnavailableBackend{"Unavailable", false, RunUnavailable};

BackendResolution ResolveLoadedBackend() {
    const RuntimeModuleSnapshot snapshot = RuntimeDiscovery_Snapshot();
    switch (RuntimeDiscovery_SelectRuntime(snapshot)) {
    case RuntimeModuleKind::Il2Cpp:
        return {RuntimeBackend_Il2Cpp(), RuntimeDiscovery_RuntimeReason(snapshot)};
    case RuntimeModuleKind::Mono:
        return {RuntimeBackend_Mono(), RuntimeDiscovery_RuntimeReason(snapshot)};
    case RuntimeModuleKind::None:
        return {kUnavailableBackend, RuntimeDiscovery_RuntimeReason(snapshot)};
    }
    return {kUnavailableBackend, "unknown runtime discovery state"};
}

BackendResolution ResolveBackend(const Config &config) {
    const std::string configured = Lower(config.runtime);
    if (configured == "il2cpp")
        return {RuntimeBackend_Il2Cpp(), "explicit config"};
    if (configured == "mono")
        return {RuntimeBackend_Mono(), "explicit config"};

    const ULONGLONG started = GetTickCount64();
    const ULONGLONG deadline = started + kBackendDiscoveryTimeoutMs;
    for (;;) {
        BackendResolution resolution = ResolveLoadedBackend();
        if (resolution.backend.implemented)
            return resolution;
        if (LoaderLifecycle_StopRequested() || GetTickCount64() >= deadline)
            return resolution;

        HANDLE stopEvent = LoaderLifecycle_StopEvent();
        if (stopEvent) {
            if (WaitForSingleObject(stopEvent, 25) == WAIT_OBJECT_0)
                return resolution;
        } else {
            Sleep(25);
        }
    }
}
} // namespace

const RuntimeBackendDescriptor &RuntimeBackend_Select(const Config &config) {
    const BackendResolution resolution = ResolveBackend(config);
    Log("[runtime] configuredBackend=%s resolvedBackend=%s reason=%s", config.runtime.c_str(), resolution.backend.name,
        resolution.reason);
    return resolution.backend;
}

bool RuntimeBackend_Run(const RuntimeBackendDescriptor &backend, Config &config) {
    if (!backend.implemented) {
        Log("[runtime][ERROR] backend=%s selected but unavailable.", backend.name);
    }
    return backend.run(config);
}
