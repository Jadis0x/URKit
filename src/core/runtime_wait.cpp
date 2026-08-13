#include "runtime_wait.h"

#include "logger.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace {
struct CandidateState {
    bool moduleReported = false;
    bool exportMissingReported = false;
};

std::string ModulePath(HMODULE module) {
    char path[32768]{};
    const DWORD length = GetModuleFileNameA(module, path, static_cast<DWORD>(sizeof(path)));
    return length ? std::string(path, length) : std::string("<path unavailable>");
}
} // namespace

RuntimeModule WaitForRuntime(const std::string_view runtimeName,
                             const std::span<const RuntimeModuleCandidate> candidates,
                             const std::chrono::milliseconds timeout, const std::chrono::milliseconds pollInterval) {
    if (candidates.empty()) {
        Log("[runtime] %.*s has no module candidates configured.", static_cast<int>(runtimeName.size()),
            runtimeName.data());
        return {};
    }

    const auto boundedTimeout = std::max(timeout, std::chrono::milliseconds::zero());
    const auto boundedPoll = std::max(pollInterval, std::chrono::milliseconds{1});
    const auto started = std::chrono::steady_clock::now();
    const auto deadline = started + boundedTimeout;
    std::vector<CandidateState> states(candidates.size());

    Log("[runtime] Waiting for %.*s runtime module and readiness export.", static_cast<int>(runtimeName.size()),
        runtimeName.data());
    for (const auto &candidate : candidates) {
        Log("[runtime] Probe configured: module=%.*s export=%.*s", static_cast<int>(candidate.moduleName.size()),
            candidate.moduleName.data(), static_cast<int>(candidate.readinessExport.size()),
            candidate.readinessExport.data());
    }

    for (;;) {
        for (size_t index = 0; index < candidates.size(); ++index) {
            const auto &candidate = candidates[index];
            auto &state = states[index];
            const std::string moduleName(candidate.moduleName);
            HMODULE module = GetModuleHandleA(moduleName.c_str());
            if (!module)
                continue;

            if (!state.moduleReported) {
                state.moduleReported = true;
                Log("[runtime] Found module %s at %p (%s); checking export %.*s.", moduleName.c_str(),
                    static_cast<void *>(module), ModulePath(module).c_str(),
                    static_cast<int>(candidate.readinessExport.size()), candidate.readinessExport.data());
            }

            const std::string exportName(candidate.readinessExport);
            FARPROC address = GetProcAddress(module, exportName.c_str());
            if (address) {
                const auto elapsed =
                    std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
                Log("[runtime] Export available: %s!%s resolved at %p after %lld ms.", moduleName.c_str(),
                    exportName.c_str(), reinterpret_cast<void *>(address), static_cast<long long>(elapsed.count()));
                return {module, candidate.moduleName, candidate.readinessExport, address};
            }

            if (!state.exportMissingReported) {
                state.exportMissingReported = true;
                Log("[runtime] Module %s is loaded, but export %s is not available "
                    "yet; waiting.",
                    moduleName.c_str(), exportName.c_str());
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline)
            break;
        std::this_thread::sleep_for(
            std::min(boundedPoll, std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)));
    }

    for (size_t index = 0; index < candidates.size(); ++index) {
        const auto &candidate = candidates[index];
        const auto &state = states[index];
        Log("[runtime] Timeout detail: module=%.*s moduleFound=%s export=%.*s "
            "exportFound=%s",
            static_cast<int>(candidate.moduleName.size()), candidate.moduleName.data(),
            state.moduleReported ? "yes" : "no", static_cast<int>(candidate.readinessExport.size()),
            candidate.readinessExport.data(), state.moduleReported && !state.exportMissingReported ? "yes" : "no");
    }
    Log("[ERROR] Timed out waiting for %.*s runtime readiness.", static_cast<int>(runtimeName.size()),
        runtimeName.data());
    return {};
}
