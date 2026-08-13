#pragma once

#include <windows.h>

#include <chrono>
#include <span>
#include <string_view>

struct RuntimeModuleCandidate {
    std::string_view moduleName;
    std::string_view readinessExport;
};

struct RuntimeModule {
    HMODULE handle = nullptr;
    std::string_view moduleName;
    std::string_view readinessExport;
    FARPROC readinessAddress = nullptr;

    explicit operator bool() const noexcept {
        return handle != nullptr && readinessAddress != nullptr;
    }
};

RuntimeModule WaitForRuntime(std::string_view runtimeName, std::span<const RuntimeModuleCandidate> candidates,
                             std::chrono::milliseconds timeout,
                             std::chrono::milliseconds pollInterval = std::chrono::milliseconds{25});
