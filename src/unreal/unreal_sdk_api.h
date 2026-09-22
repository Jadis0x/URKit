#pragma once

// URK_UnrealApi bound to the live calibration. Mods get opaque handles, never
// offsets or raw pointers; reads go through PropertyValues and calls through
// the ProcessEvent hook. Process-global, so a singleton.

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

// The calibration ladder, resolved once and kept.
class UnrealEngine {
  public:
    static UnrealEngine &Instance();

    UnrealEngine(const UnrealEngine &) = delete;
    UnrealEngine &operator=(const UnrealEngine &) = delete;

    // Thread-safe; only one caller pays for the climb. Failure is retryable:
    // a proxy DLL loads before the engine builds its object array, so "not
    // yet" is the common case. Only a non-UBT process is ruled out for good.
    // A cooldown keeps a polling caller from burning the frame budget.
    bool EnsureBootstrapped();

    static constexpr std::uint64_t kRetryCooldownMs = 2000;

    bool Available() const;
    const EngineVersion &Version() const { return version_; }

    const ProcessMemory &Reader() const { return memory_; }
    // Same memory as reads; constness is what keeps them apart.
    ProcessMemory &Writer() { return memory_; }
    const ObjectFinder &Finder() const { return *finder_; }
    const PropertyChain &Chain() const { return *chain_; }
    const PropertyValues &Values() const { return *values_; }
    const TypeQueries &Types() const { return *types_; }
    const StructOffsets &Structs() const { return structs_; }
    const FunctionOffsets &Functions() const { return functions_; }

    const ProcessEventLocation &ProcessEvent() const { return processEvent_; }
    bool ProcessEventResolved() const { return processEvent_.Resolved(); }

  private:
    UnrealEngine() = default;

    std::atomic<bool> available_{false};
    // Only for a process that can never be Unreal, not for "not ready yet".
    std::atomic<bool> ruledOut_{false};
    std::atomic<std::uint64_t> lastAttemptMs_{0};
    std::mutex bootstrapMutex_;

    ProcessMemory memory_;
    EngineVersion version_;
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
};

// Static table, same pointer every call. An invalid installer leaves the API
// read-only: hook_install/call refuse cleanly, everything else still works.
const URK_UnrealApi *UnrealSdkApi(const HookInstaller &installer);

} // namespace URK::Unreal
