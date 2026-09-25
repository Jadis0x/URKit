#include "unreal_function_hooks.h"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <thread>

namespace URK::Unreal {

struct FunctionHooks::Entry {
    std::uint64_t id = 0;
    Callback before = nullptr;
    Callback after = nullptr;
    void *user = nullptr;
    std::atomic<bool> removed{false};
    // Its calls on the removing thread skip after: that thread is not waited for.
    std::atomic<std::uint32_t> removedBy{0};
    std::atomic<std::int32_t> running{0};
};

namespace {

// Blueprint override chains are short; this only bounds a corrupt one.
constexpr int kMaxOverrideDepth = 16;

// Inside a callback or a Quiet scope: this thread's calls are not hooked.
thread_local std::int32_t t_quiet = 0;
// Entries between their before and after on this thread, innermost last.
thread_local std::vector<const FunctionHooks::Entry *> t_armed;

// Held from before to after, so a removal cannot split the pair.
bool Arm(FunctionHooks::Entry &entry) {
    entry.running.fetch_add(1, std::memory_order_acq_rel);
    if (entry.removed.load(std::memory_order_acquire)) {
        entry.running.fetch_sub(1, std::memory_order_acq_rel);
        return false;
    }
    t_armed.push_back(&entry);
    return true;
}

void Disarm(FunctionHooks::Entry &entry) {
    t_armed.pop_back();
    entry.running.fetch_sub(1, std::memory_order_acq_rel);
}

} // namespace

FunctionHooks::Quiet::Quiet() { ++t_quiet; }

FunctionHooks::Quiet::~Quiet() { --t_quiet; }

FunctionHooks &FunctionHooks::Instance() {
    static FunctionHooks hooks;
    return hooks;
}

std::uint64_t FunctionHooks::Add(Address function, Callback before, Callback after, void *user) {
    if (function == kNullAddress || (!before && !after))
        return 0;
    auto entry = std::make_shared<Entry>();
    entry->before = before;
    entry->after = after;
    entry->user = user;
    const std::unique_lock lock(mutex_);
    entry->id = nextId_++;
    byFunction_[function].push_back(entry);
    functionOf_[entry->id] = function;
    count_.fetch_add(1, std::memory_order_release);
    return entry->id;
}

bool FunctionHooks::Remove(std::uint64_t id, std::uint32_t timeoutMs) {
    std::shared_ptr<Entry> entry;
    {
        const std::unique_lock lock(mutex_);
        const auto owner = functionOf_.find(id);
        if (owner == functionOf_.end())
            return false;
        auto &entries = byFunction_[owner->second];
        const auto at = std::find_if(entries.begin(), entries.end(), [&](const auto &e) { return e->id == id; });
        if (at != entries.end()) {
            entry = *at;
            entries.erase(at);
        }
        if (entries.empty())
            byFunction_.erase(owner->second);
        functionOf_.erase(owner);
        count_.fetch_sub(1, std::memory_order_release);
    }
    if (!entry)
        return false;
    entry->removedBy.store(GetCurrentThreadId(), std::memory_order_relaxed);
    entry->removed.store(true, std::memory_order_release);
    // Calls of it this thread is inside, a callback removing its own hook among them.
    const auto own = static_cast<std::int32_t>(std::count(t_armed.begin(), t_armed.end(), entry.get()));
    const std::uint64_t deadline = GetTickCount64() + timeoutMs;
    while (entry->running.load(std::memory_order_acquire) > own) {
        if (GetTickCount64() >= deadline)
            return false;
        std::this_thread::yield();
    }
    return true;
}

bool FunctionHooks::Before(Pending &pending, Address object, Address function, void *parms, void *result) {
    if (t_quiet > 0 || count_.load(std::memory_order_acquire) == 0)
        return true;
    {
        const std::shared_lock lock(mutex_);
        auto found = byFunction_.find(function);
        // A Blueprint override is a UFunction of its own, its SuperStruct the
        // function it overrides; the call in progress keeps the chain alive.
        const std::int32_t super = superOffset_.load(std::memory_order_acquire);
        Address at = function;
        for (int depth = 0; found == byFunction_.end() && super >= 0 && depth < kMaxOverrideDepth; ++depth) {
            at = *reinterpret_cast<const Address *>(static_cast<std::uintptr_t>(at + super));
            if (at == kNullAddress)
                break;
            found = byFunction_.find(at);
        }
        if (found == byFunction_.end())
            return true;
        pending.entries = found->second;
    }
    pending.call = HookedCall{object, function, parms, result, false, false};
    bool proceed = true;
    ++t_quiet;
    for (std::shared_ptr<Entry> &entry : pending.entries) {
        if (!Arm(*entry)) {
            entry.reset();
            continue;
        }
        if (entry->before && !entry->before(entry->user, pending.call))
            proceed = false;
    }
    --t_quiet;
    pending.call.skipped = !proceed;
    return proceed;
}

void FunctionHooks::After(Pending &pending) {
    if (pending.entries.empty())
        return;
    pending.call.after = true;
    ++t_quiet;
    const std::uint32_t thread = GetCurrentThreadId();
    for (const std::shared_ptr<Entry> &entry : pending.entries) {
        if (!entry || !entry->after)
            continue;
        if (entry->removed.load(std::memory_order_acquire) &&
            entry->removedBy.load(std::memory_order_relaxed) == thread)
            continue;
        entry->after(entry->user, pending.call);
    }
    --t_quiet;
    // Armed in order, so released in reverse.
    for (auto entry = pending.entries.rbegin(); entry != pending.entries.rend(); ++entry) {
        if (*entry)
            Disarm(**entry);
    }
    pending.entries.clear();
}

} // namespace URK::Unreal
