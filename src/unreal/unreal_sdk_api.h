#pragma once

// URK_UnrealApi over the live calibration. Mods get opaque handles, never
// offsets; process-global, so a singleton.

#include "mod_sdk.h"
#include "unreal_bootstrap.h"
#include "unreal_engine_detect.h"
#include "unreal_functions.h"
#include "unreal_process_event_hook.h"
#include "unreal_process_memory.h"
#include "unreal_struct_offsets.h"
#include "unreal_type_queries.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace URK::Unreal {

// Where bootstrap time went, for the load log.
struct BootstrapProfile {
    std::uint32_t attempts = 0;
    std::uint32_t scans = 0;
    std::uint64_t failedMs = 0;
    std::uint64_t anchorMs = 0;
    std::uint64_t locateMs = 0;
    std::uint64_t indexMs = 0;
    std::uint64_t offsetsMs = 0;
    std::uint64_t functionsMs = 0;
    std::uint64_t processEventMs = 0;
    std::size_t anchoredArrays = 0;
    std::size_t anchoredPools = 0;
    const char *locatedBy = "";
};

// The calibration ladder, resolved once and kept.
class UnrealEngine {
  public:
    static UnrealEngine &Instance();

    UnrealEngine(const UnrealEngine &) = delete;
    UnrealEngine &operator=(const UnrealEngine &) = delete;

    // Thread-safe and retryable: the proxy loads before the object array
    // exists. Only a non-UBT process is ruled out; a cooldown limits polling.
    bool EnsureBootstrapped();

    static constexpr std::uint64_t kRetryCooldownMs = 100;
    // The data scan costs seconds. Without anchors it is the only way in;
    // with them it is a fallback for anchors that validate nothing.
    static constexpr std::uint64_t kScanRetryMs = 2000;
    static constexpr std::uint64_t kScanFallbackMs = 10000;

    // Version resource and module layout only, no scan. Cached; the answer
    // cannot change while the process lives.
    const UnrealPresence &Presence();

    // This process can never be supported, so waiting for it is pointless.
    bool RuledOut() const;

    // Why the last bootstrap attempt failed; empty until one has.
    const char *LastFailure() const { return failure_.load(std::memory_order_acquire); }

    bool Available() const;
    const EngineVersion &Version() const { return version_; }
    // Stable once Available().
    const BootstrapProfile &Profile() const { return profile_; }

    const ProcessMemory &Reader() const { return memory_; }
    // Same memory as reads; constness is what keeps them apart.
    ProcessMemory &Writer() { return memory_; }
    const ObjectFinder &Finder() const { return *finder_; }
    const PropertyChain &Chain() const { return *chain_; }
    const PropertyValues &Values() const { return *values_; }
    const TypeQueries &Types() const { return *types_; }
    const StructOffsets &Structs() const { return structs_; }
    const FunctionOffsets &Functions() const { return functions_; }
    // Exact function bounds from the engine image's exception table.
    const FunctionTable &Bounds() const { return functionTable_; }

    const ProcessEventLocation &ProcessEvent() const { return processEvent_; }
    bool ProcessEventResolved() const { return processEvent_.Resolved(); }

  private:
    UnrealEngine() = default;

    std::atomic<bool> available_{false};
    // Only for a process that can never be Unreal, not for "not ready yet".
    std::atomic<bool> ruledOut_{false};
    std::atomic<std::uint64_t> lastAttemptMs_{0};
    std::atomic<const char *> failure_{""};
    std::mutex bootstrapMutex_;
    std::mutex presenceMutex_;
    std::optional<UnrealPresence> presence_;

    ProcessMemory memory_;
    EngineVersion version_;
    // Found once: they depend only on the image.
    std::optional<GlobalCandidates> anchors_;
    FunctionTable functionTable_;
    std::uint64_t scanAllowedAtMs_ = 0;
    std::optional<Runtime> runtime_;
    std::unique_ptr<ObjectFinder> finder_;
    StructOffsets structs_{};
    FieldOffsets fields_{};
    PropertyTailOffsets tail_{};
    ClassOffsets classes_{};
    FunctionOffsets functions_{};
    std::unique_ptr<PropertyChain> chain_;
    std::unique_ptr<PropertyValues> values_;
    std::unique_ptr<TypeQueries> types_;
    ProcessEventLocation processEvent_{};
    BootstrapProfile profile_{};
};

// Static table, same pointer every call. An invalid installer leaves the API
// read-only: hook_install/call refuse cleanly, everything else still works.
const URK_UnrealApi *UnrealSdkApi(const HookInstaller &installer);

// The loader's own claim on the ProcessEvent hook, for the game loop. Needs the
// installer UnrealSdkApi was given; mods' install/remove leave this claim alone.
bool UnrealSdk_HoldProcessEventHook();

// Where the API reports refused calls; the loader passes its log.
using LogSink = void (*)(const char *message);
void UnrealSdk_SetLog(LogSink log);

// Game thread, once per frame: releases engine memory in call frames a mod
// destroyed on another thread.
void UnrealSdk_ReleasePending();

} // namespace URK::Unreal
