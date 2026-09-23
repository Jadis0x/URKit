#include "unreal_process_event_hook.h"

#include <windows.h>

#include <array>
#include <thread>
#include <utility>

namespace URK::Unreal {
namespace {

// The detour can reach ProcessEvent again through an observer or posted work,
// so reentrancy is tracked rather than recursed into forever.
thread_local std::int32_t g_depth = 0;

struct DepthGuard {
    DepthGuard() { ++g_depth; }
    ~DepthGuard() { --g_depth; }
    bool Outermost() const { return g_depth == 1; }
};

std::uint32_t CurrentThread() { return static_cast<std::uint32_t>(GetCurrentThreadId()); }

} // namespace

ProcessEventHook &ProcessEventHook::Instance() {
    static ProcessEventHook hook;
    return hook;
}

bool ProcessEventHook::Install(const HookInstaller &installer, std::span<const Address> implementations,
                               const ProcessEventLocation &location) {
    if (installed_.load(std::memory_order_acquire) || !installer.Valid() || !location.Resolved())
        return false;
    if (implementations.empty() || implementations.size() > kMaxImplementations)
        return false;
    // A timed-out Remove() left the patch in place; patching again would hook
    // the detour to itself.
    if (patchedCount_ != 0)
        return false;

    installer_ = installer;
    location_ = location;
    patchedCount_ = 0;

    for (const Address target : implementations) {
        if (target == kNullAddress)
            continue;

        const std::size_t slot = patchedCount_;
        void *trampoline = nullptr;
        void *handle = installer_.attach(installer_.context, reinterpret_cast<void *>(target),
                                         reinterpret_cast<void *>(DetourFor(slot)), &trampoline);
        if (!handle || !trampoline) {
            // Partial coverage hides what it misses; take it all back off.
            if (handle)
                installer_.detach(installer_.context, handle);
            for (std::size_t i = 0; i < patchedCount_; ++i)
                installer_.detach(installer_.context, patched_[i].handle);
            ResetPatches();
            return false;
        }

        patched_[slot].target = target;
        patched_[slot].handle = handle;
        patched_[slot].original.store(reinterpret_cast<ProcessEventFn>(trampoline), std::memory_order_release);
        ++patchedCount_;
    }

    if (patchedCount_ == 0)
        return false;

    installed_.store(true, std::memory_order_release);
    return true;
}

bool ProcessEventHook::Remove(std::uint32_t timeoutMs) {
    if (!installed_.exchange(false, std::memory_order_acq_rel))
        return true;

    // Stop observing first: a call on its way out must not reach a mod that is
    // being taken down.
    observer_.store(nullptr, std::memory_order_release);
    frameTick_.store(nullptr, std::memory_order_release);
    for (Queued &slot : queue_)
        slot.work.store(nullptr, std::memory_order_release);
    drained_.store(posted_.load(std::memory_order_acquire), std::memory_order_release);

    // New calls see installed_ false and pass through, so this does empty.
    const std::uint64_t deadline = GetTickCount64() + timeoutMs;
    while (inFlight_.load(std::memory_order_acquire) > 0) {
        if (GetTickCount64() >= deadline) {
            // Leaking the patch beats freeing a trampoline still in use.
            installed_.store(false, std::memory_order_release);
            return false;
        }
        std::this_thread::yield();
    }

    for (std::size_t i = 0; i < patchedCount_; ++i) {
        if (patched_[i].handle)
            installer_.detach(installer_.context, patched_[i].handle);
    }

    ResetPatches();
    return true;
}

bool ProcessEventHook::Installed() const { return installed_.load(std::memory_order_acquire); }

void ProcessEventHook::Observe(Observer observer, void *user) {
    observerUser_.store(user, std::memory_order_release);
    observer_.store(observer, std::memory_order_release);
}

void ProcessEventHook::SetFrameTick(FrameTick tick, void *user, const volatile std::uint64_t *frameCounter) {
    frameCounter_.store(frameCounter, std::memory_order_release);
    frameTickUser_.store(user, std::memory_order_release);
    frameTick_.store(tick, std::memory_order_release);
}

void ProcessEventHook::TickFrame() {
    const FrameTick tick = frameTick_.load(std::memory_order_acquire);
    if (!tick)
        return;
    const volatile std::uint64_t *counter = frameCounter_.load(std::memory_order_acquire);
    const std::uint64_t frame = counter ? *counter : GetTickCount64() / kUnclockedFrameMs;
    if (frame == lastFrame_)
        return;
    lastFrame_ = frame;
    tick(frameTickUser_.load(std::memory_order_acquire));
}

std::uint32_t ProcessEventHook::GameThreadId() const { return gameThread_.load(std::memory_order_acquire); }
std::uint64_t ProcessEventHook::Calls() const { return calls_.load(std::memory_order_relaxed); }
std::uint64_t ProcessEventHook::Dropped() const { return dropped_.load(std::memory_order_relaxed); }
std::size_t ProcessEventHook::Pending() const {
    const std::uint64_t posted = posted_.load(std::memory_order_acquire);
    const std::uint64_t drained = drained_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(posted - drained);
}

bool ProcessEventHook::Post(Work work, void *user) {
    if (!work || !installed_.load(std::memory_order_acquire))
        return false;
    if (gameThread_.load(std::memory_order_acquire) == 0)
        return false;

    // Reserve only when there is room: an unpublished slot stalls the drain.
    std::uint64_t ticket = posted_.load(std::memory_order_acquire);
    for (;;) {
        if (ticket - drained_.load(std::memory_order_acquire) >= kMaxQueued)
            return false;
        if (posted_.compare_exchange_weak(ticket, ticket + 1, std::memory_order_acq_rel,
                                          std::memory_order_acquire))
            break;
    }

    Queued &slot = queue_[ticket % kMaxQueued];
    slot.user = user;
    // Published last: the drain runs a slot only once its work is visible.
    slot.work.store(work, std::memory_order_release);
    return true;
}

template <std::size_t Slot> void __fastcall ProcessEventHook::DetourAt(void *object, void *function, void *parms) {
    Instance().Enter(Slot, object, function, parms);
}

ProcessEventFn ProcessEventHook::DetourFor(std::size_t slot) {
    static constexpr auto detours = []<std::size_t... Slots>(std::index_sequence<Slots...>) {
        return std::array<ProcessEventFn, sizeof...(Slots)>{&DetourAt<Slots>...};
    }(std::make_index_sequence<kMaxImplementations>{});
    return detours[slot];
}

void ProcessEventHook::ResetPatches() {
    for (Patched &patch : patched_) {
        patch.target = kNullAddress;
        patch.handle = nullptr;
        patch.original.store(nullptr, std::memory_order_release);
    }
    patchedCount_ = 0;
}

void ProcessEventHook::Tally(std::uint32_t thread) {
    for (std::size_t i = 0; i < kTrackedThreads; ++i) {
        std::uint32_t owner = threads_[i].thread.load(std::memory_order_acquire);
        if (owner == 0) {
            std::uint32_t expected = 0;
            if (!threads_[i].thread.compare_exchange_strong(expected, thread, std::memory_order_acq_rel))
                owner = expected;
            else
                owner = thread;
        }
        if (owner != thread)
            continue;

        const std::uint64_t seen = threads_[i].calls.fetch_add(1, std::memory_order_relaxed) + 1;
        if (seen < kThreadEvidence)
            return;

        // The engine does not say which thread is its own, so count.
        std::uint32_t best = thread;
        std::uint64_t bestCalls = seen;
        for (std::size_t j = 0; j < kTrackedThreads; ++j) {
            const std::uint64_t calls = threads_[j].calls.load(std::memory_order_relaxed);
            if (calls > bestCalls) {
                bestCalls = calls;
                best = threads_[j].thread.load(std::memory_order_relaxed);
            }
        }
        gameThread_.store(best, std::memory_order_release);
        return;
    }
}

void ProcessEventHook::Drain() {
    // Single consumer. The count is read once, so work posted mid-drain waits
    // for the next call instead of extending this one.
    const std::uint64_t upTo = posted_.load(std::memory_order_acquire);
    std::uint64_t at = drained_.load(std::memory_order_relaxed);

    for (; at < upTo; ++at) {
        Queued &slot = queue_[at % kMaxQueued];
        Work work = slot.work.exchange(nullptr, std::memory_order_acq_rel);
        if (!work) {
            // Reserved but not published; stepping over it would lose it.
            break;
        }
        work(slot.user);
    }
    drained_.store(at, std::memory_order_release);
}

ProcessEventHook::PassThrough::PassThrough() { ++g_depth; }

ProcessEventHook::PassThrough::~PassThrough() { --g_depth; }

void ProcessEventHook::Dispatch(ProcessEventFn original, void *object, void *function, void *parms) {
    calls_.fetch_add(1, std::memory_order_relaxed);

    const DepthGuard depth;
    if (!depth.Outermost()) {
        // Reentrant: pass through rather than run posted work mid-call.
        if (original)
            original(object, function, parms);
        return;
    }

    const std::uint32_t thread = CurrentThread();
    Tally(thread);

    bool proceed = true;
    if (const Observer observer = observer_.load(std::memory_order_acquire))
        proceed = observer(observerUser_.load(std::memory_order_acquire), reinterpret_cast<Address>(object),
                           reinterpret_cast<Address>(function), parms);

    if (proceed) {
        if (original)
            original(object, function, parms);
    } else {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    if (thread == gameThread_.load(std::memory_order_acquire)) {
        TickFrame();
        Drain();
    }
}

void ProcessEventHook::Enter(std::size_t slot, void *object, void *function, void *parms) {
    // Counted before the installed check, so Remove() cannot race a call in.
    inFlight_.fetch_add(1, std::memory_order_acq_rel);

    // Only between the patch going live and attach returning; microseconds.
    ProcessEventFn original = patched_[slot].original.load(std::memory_order_acquire);
    while (!original) {
        YieldProcessor();
        original = patched_[slot].original.load(std::memory_order_acquire);
    }

    if (installed_.load(std::memory_order_acquire))
        Dispatch(original, object, function, parms);
    else
        original(object, function, parms);
    inFlight_.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace URK::Unreal
