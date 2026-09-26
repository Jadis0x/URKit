#pragma once

// URK_UnrealApi over the live calibration; opaque handles, process-wide singleton.

#include "mod_sdk.h"
#include "unreal/detect/unreal_bootstrap.h"
#include "unreal/detect/unreal_engine_detect.h"
#include "unreal/hooks/unreal_process_event_hook.h"
#include "unreal/layout/unreal_struct_offsets.h"
#include "unreal/memory/unreal_process_memory.h"
#include "unreal/reflection/unreal_functions.h"
#include "unreal/reflection/unreal_type_queries.h"

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

    // Thread-safe and retryable; a cooldown limits polling.
    bool EnsureBootstrapped();

    static constexpr std::uint64_t kRetryCooldownMs = 100;
    // Data scan takes seconds; fallback when anchors fail.
    static constexpr std::uint64_t kScanRetryMs = 2000;
    static constexpr std::uint64_t kScanFallbackMs = 10000;
    // Too early in startup ProcessEvent may not pin down yet; retried this long.
    static constexpr std::uint64_t kProcessEventGraceMs = 10000;

    // Version resource and module layout only. Cached.
    const UnrealPresence &Presence();

    // This process can never be supported, so waiting for it is pointless.
    bool RuledOut() const;

    // Why the last bootstrap attempt failed; empty until one has.
    const char *LastFailure() const { return failure_.load(std::memory_order_acquire); }
    // The failed step in detail, one finding per line. Costs a data scan: call once, on give-up.
    std::vector<std::string> ExplainFailure();
    // Why the code anchors made no pair; cheap, for a bootstrap the data scan rescued.
    std::vector<std::string> ExplainAnchors();

    bool Available() const;
    const EngineVersion &Version() const { return version_; }
    // Fills a version the resource lacks from engine code.
    void ResolveVersionFromCode(InstructionLength length);
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
    const FieldOffsets &Fields() const { return fields_; }
    const ClassOffsets &Classes() const { return classes_; }
    const FunctionOffsets &Functions() const { return functions_; }
    // Exact function bounds from the engine image's exception table.
    const FunctionTable &Bounds() const { return functionTable_; }

    const ProcessEventLocation &ProcessEvent() const { return processEvent_; }
    bool ProcessEventResolved() const { return processEvent_.Resolved(); }
    const std::string &ProcessEventFailure() const { return processEventFailure_; }

  private:
    UnrealEngine() = default;
    std::vector<std::string> ExplainAnchorsLocked();

    std::atomic<bool> available_{false};
    // Only for a process that can never be Unreal, not for "not ready yet".
    std::atomic<bool> ruledOut_{false};
    std::atomic<std::uint64_t> lastAttemptMs_{0};
    // First attempt that found everything but ProcessEvent.
    std::uint64_t processEventMissingSinceMs_ = 0;
    std::atomic<const char *> failure_{""};
    std::mutex bootstrapMutex_;
    std::mutex presenceMutex_;
    std::optional<UnrealPresence> presence_;

    ProcessMemory memory_;
    EngineVersion version_;
    // Found once: they depend only on the image.
    std::optional<GlobalCandidates> anchors_;
    std::vector<std::pair<Address, GlobalCandidates>> anchorModules_;
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
    std::string processEventFailure_;
    BootstrapProfile profile_{};
};

// Static table. Without an installer the API is read-only.
const URK_UnrealApi *UnrealSdkApi(const HookInstaller &installer);

// Loader's own claim on the ProcessEvent hook; mods' install/remove don't touch it.
bool UnrealSdk_HoldProcessEventHook();

// Where the API reports refused calls; the loader passes its log.
using LogSink = void (*)(const char *message);
void UnrealSdk_SetLog(LogSink log);

// Game thread, per frame: releases frames other threads destroyed.
void UnrealSdk_ReleasePending();

class EnumNames;
// Enum names once measured (the game thread measures them), else null.
const EnumNames *UnrealSdk_Enums();

} // namespace URK::Unreal
