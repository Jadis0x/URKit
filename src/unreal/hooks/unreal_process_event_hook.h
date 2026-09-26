#pragma once

// ProcessEvent hook: every reflected call plus a game-thread foothold.
// attach/detach come from the host, so no SafetyHook dependency here.

#include "unreal/hooks/unreal_function_hooks.h"
#include "unreal/hooks/unreal_process_event.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace URK::Unreal {

using ProcessEventFn = void(__fastcall *)(void *object, void *function, void *parms);

// attach returns an opaque handle and writes the trampoline; detach frees it.
struct HookInstaller {
    void *context = nullptr;
    void *(*attach)(void *context, void *target, void *detour, void **trampoline) = nullptr;
    bool (*detach)(void *context, void *handle) = nullptr;

    bool Valid() const { return attach != nullptr && detach != nullptr; }
};

class ProcessEventHook {
  public:
    // Runs on the calling thread, before the engine. False drops the call.
    using Observer = bool (*)(void *user, Address object, Address function, void *parms);

    // Runs on the game thread.
    using Work = void (*)(void *user);

    // Game thread, once per frame.
    using FrameTick = void (*)(void *user);
    static constexpr std::uint64_t kUnclockedFrameMs = 16;

    // The loader's own calls bypass observers and ticks while alive.
    class PassThrough {
      public:
        PassThrough();
        ~PassThrough();
        PassThrough(const PassThrough &) = delete;
        PassThrough &operator=(const PassThrough &) = delete;

      private:
        FunctionHooks::Quiet quiet_;
    };

    static constexpr std::uint32_t kDefaultDrainTimeoutMs = 5000;

    // A detour carries no user pointer, hence a singleton.
    static ProcessEventHook &Instance();

    // All-or-nothing: a hook that covers some calls hides what it misses.
    bool Install(const HookInstaller &installer, std::span<const Address> implementations,
                 const ProcessEventLocation &location);

    // Waits for in-flight calls; on timeout the patch stays (leak over use-after-free).
    bool Remove(std::uint32_t timeoutMs = kDefaultDrainTimeoutMs);

    bool Installed() const;

    void Observe(Observer observer, void *user);

    // frameCounter may be null. Set before or after Install; null tick stops it.
    void SetFrameTick(FrameTick tick, void *user, const volatile std::uint64_t *frameCounter);

    // GFrameCounter write hooks; the site that alone advances it is the frame boundary.
    static constexpr std::size_t kMaxBoundarySites = 16;
    void FrameBoundary(std::size_t site);
    bool FrameBoundaryProven() const { return boundarySite_.load(std::memory_order_acquire) >= 0; }
    // The elected site, -1 before one is.
    std::int32_t ElectedBoundary() const { return boundarySite_.load(std::memory_order_acquire); }

    // Innermost ProcessEvent call on this thread, or null.
    struct Call {
        Address object = kNullAddress;
        Address function = kNullAddress;
        void *parms = nullptr;
        // Its entry into script (ProcessInternal) was already claimed.
        bool scriptClaimed = false;
    };
    static const Call *CurrentCall();

    // True once per call when script is entered for a call ProcessEvent already reported.
    static bool ClaimScriptEntry(Address object, Address function);

    // Fails when the queue is full or no game thread is known yet.
    bool Post(Work work, void *user);

    // Busiest caller wins; zero until enough calls have been seen.
    std::uint32_t GameThreadId() const;

    std::uint64_t Calls() const;
    std::uint64_t Dropped() const;
    std::size_t Pending() const;

  private:
    ProcessEventHook() = default;

    static constexpr std::size_t kMaxImplementations = 16;
    static constexpr std::size_t kMaxQueued = 64;
    static constexpr std::size_t kTrackedThreads = 8;
    // Far less than one frame's worth, but enough to rule out a stray call.
    static constexpr std::uint64_t kThreadEvidence = 64;
    // Consecutive frames the boundary must follow the counter through.
    static constexpr std::uint32_t kBoundaryEvidence = 8;

    struct Patched {
        Address target = kNullAddress;
        void *handle = nullptr;
        // Atomic: the patch is live before attach returns the trampoline.
        std::atomic<ProcessEventFn> original{nullptr};
    };

    struct ThreadTally {
        std::atomic<std::uint32_t> thread{0};
        std::atomic<std::uint64_t> calls{0};
    };

    struct Queued {
        std::atomic<Work> work{nullptr};
        void *user = nullptr;
    };

    // One detour per patch; Super::ProcessEvent must continue into the base.
    template <std::size_t Slot> static void __fastcall DetourAt(void *object, void *function, void *parms);
    static ProcessEventFn DetourFor(std::size_t slot);
    void Enter(std::size_t slot, void *object, void *function, void *parms);
    void Dispatch(ProcessEventFn original, void *object, void *function, void *parms);
    void ResetPatches();
    void Tally(std::uint32_t thread);
    void Drain();
    void TickFrame();

    // Written while nothing is installed, read-only afterwards.
    HookInstaller installer_{};
    ProcessEventLocation location_{};
    Patched patched_[kMaxImplementations]{};
    std::size_t patchedCount_ = 0;

    std::atomic<bool> installed_{false};
    // Calls inside the detour; Remove() waits for this to empty.
    std::atomic<std::int32_t> inFlight_{0};

    std::atomic<Observer> observer_{nullptr};
    std::atomic<void *> observerUser_{nullptr};

    std::atomic<FrameTick> frameTick_{nullptr};
    std::atomic<void *> frameTickUser_{nullptr};
    std::atomic<const volatile std::uint64_t *> frameCounter_{nullptr};
    // Game thread only.
    std::uint64_t lastFrame_ = ~std::uint64_t{0};

    struct BoundarySite {
        std::atomic<std::uint32_t> thread{0};
        std::atomic<std::uint64_t> frame{0};
        std::atomic<std::uint32_t> run{0};
    };
    BoundarySite sites_[kMaxBoundarySites]{};
    // The site frames tick at; -1 until one proves itself.
    std::atomic<std::int32_t> boundarySite_{-1};

    std::atomic<std::uint64_t> calls_{0};
    std::atomic<std::uint64_t> dropped_{0};

    ThreadTally threads_[kTrackedThreads]{};
    std::atomic<std::uint32_t> gameThread_{0};

    // Many posters, one drainer; separate ends so a late post survives a drain.
    Queued queue_[kMaxQueued]{};
    std::atomic<std::uint64_t> posted_{0};
    std::atomic<std::uint64_t> drained_{0};
};

} // namespace URK::Unreal
