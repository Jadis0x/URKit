#pragma once

// Hooking ProcessEvent buys two things: every reflected call, and a foothold on
// the game thread to run work the engine would otherwise reject.
//
// The hook engine stays the host's: it passes attach/detach in, so nothing
// below this rung depends on SafetyHook.

#include "unreal_process_event.h"

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

    static constexpr std::uint32_t kDefaultDrainTimeoutMs = 5000;

    // A detour carries no user pointer, hence a singleton.
    static ProcessEventHook &Instance();

    // All-or-nothing: a hook that covers some calls hides what it misses.
    bool Install(const HookInstaller &installer, std::span<const Address> implementations,
                 const ProcessEventLocation &location);

    // Drains in-flight calls first: unpatching frees the trampolines a call
    // inside the detour still has to return through. On timeout nothing is
    // unpatched and the hook stays in place, passive - leaking beats freeing
    // memory a thread is standing on.
    bool Remove(std::uint32_t timeoutMs = kDefaultDrainTimeoutMs);

    bool Installed() const;

    void Observe(Observer observer, void *user);

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

    struct Patched {
        Address target = kNullAddress;
        void *handle = nullptr;
        ProcessEventFn original = nullptr;
    };

    struct ThreadTally {
        std::atomic<std::uint32_t> thread{0};
        std::atomic<std::uint64_t> calls{0};
    };

    struct Queued {
        std::atomic<Work> work{nullptr};
        void *user = nullptr;
    };

    static void __fastcall Detour(void *object, void *function, void *parms);
    void Dispatch(void *object, void *function, void *parms);
    ProcessEventFn OriginalFor(void *object) const;
    void Tally(std::uint32_t thread);
    void Drain();

    // Written while nothing is installed, read-only afterwards.
    HookInstaller installer_{};
    ProcessEventLocation location_{};
    Patched patched_[kMaxImplementations]{};
    std::size_t patchedCount_ = 0;
    std::int32_t vtableIndex_ = kOffsetNotFound;

    std::atomic<bool> installed_{false};
    // Calls inside the detour; Remove() waits for this to empty.
    std::atomic<std::int32_t> inFlight_{0};

    std::atomic<Observer> observer_{nullptr};
    std::atomic<void *> observerUser_{nullptr};

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
