#pragma once

// Blueprint calls via UObject::ProcessInternal and ProcessLocalScriptFunction
// (self calls, ubergraphs). FFrame offsets are measured before reporting.

#include "unreal_functions.h"
#include "unreal_module.h"
#include "unreal_process_event_hook.h"
#include "unreal_type_queries.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>

namespace URK::Unreal {

class ScriptCallHook {
  public:
    using Observer = void (*)(void *user, Address object, Address function, void *locals, void *result, bool after);

    static ScriptCallHook &Instance();

    // Once; later calls return the first answer.
    bool Install(const ObjectFinder &finder, const TypeQueries &types, const FunctionOffsets &functions,
                 const FunctionTable &bounds, std::span<const ScanRegion> code, const HookInstaller &installer);
    bool Installed() const { return installed_.load(std::memory_order_acquire); }
    const std::string &Failure() const { return failure_; }

    void Observe(Observer observer, void *user);

    // FFrame offsets once measured, -1 before.
    std::int32_t NodeOffset() const { return nodeOffset_.load(std::memory_order_acquire); }
    std::int32_t LocalsOffset() const { return localsOffset_.load(std::memory_order_acquire); }
    Address ProcessInternal() const { return processInternal_; }
    Address ProcessLocalScriptFunction() const { return processLocal_; }
    // Calls through each entry point so far.
    std::uint64_t InternalCalls() const { return internalCalls_.load(std::memory_order_relaxed); }
    std::uint64_t LocalCalls() const { return localCalls_.load(std::memory_order_relaxed); }
    // Where measuring stands, for a log line.
    std::string MeasureState() const;

  private:
    using ScriptFn = void(__fastcall *)(void *context, void *stack, void *result);

    ScriptCallHook() = default;

    static void __fastcall InternalDetour(void *context, void *stack, void *result);
    static void __fastcall LocalDetour(void *context, void *stack, void *result);
    void Measure(void *context, const std::uint8_t *stack);
    void Report(const std::uint8_t *stack, void *result, bool after);

    std::atomic<bool> installed_{false};
    bool attempted_ = false;
    std::string failure_;
    Address processInternal_ = kNullAddress;
    Address processLocal_ = kNullAddress;
    std::atomic<ScriptFn> internalOriginal_{nullptr};
    std::atomic<ScriptFn> localOriginal_{nullptr};
    std::int32_t parmsSizeOffset_ = kOffsetNotFound;

    std::atomic<Observer> observer_{nullptr};
    std::atomic<void *> observerUser_{nullptr};

    // Measured: calls that agreed so far, and the offsets they agreed on.
    std::atomic<std::int32_t> nodeOffset_{-1};
    std::atomic<std::int32_t> objectOffset_{-1};
    std::atomic<std::int32_t> localsOffset_{-1};
    std::atomic<std::int32_t> agreed_{0};
    // Worker threads run script too.
    std::mutex measureMutex_;
    bool contradicted_ = false;
    std::int32_t candidateNode_ = -1;
    std::int32_t candidateScript_ = -1;
    std::atomic<std::uint64_t> sampled_{0};
    std::atomic<std::uint64_t> parmsChecked_{0};
    std::atomic<std::int32_t> lastNode_{-1};
    std::atomic<std::int32_t> lastObject_{-1};
    std::atomic<std::uint64_t> internalCalls_{0};
    std::atomic<std::uint64_t> localCalls_{0};
};

} // namespace URK::Unreal
