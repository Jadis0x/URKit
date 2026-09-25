#pragma once

// Callbacks before and after one UFunction's calls, its Blueprint overrides'
// included, from ProcessEvent and the Blueprint VM. Calls a callback makes pass by.

#include "unreal_memory.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {

struct HookedCall {
    Address object = kNullAddress;
    Address function = kNullAddress;
    void *parms = nullptr;
    // The return value's home when the caller keeps it outside parms.
    void *result = nullptr;
    bool after = false;
    bool skipped = false;
};

class FunctionHooks {
  public:
    // Before: false skips the body. After: the answer is ignored.
    using Callback = bool (*)(void *user, const HookedCall &call);
    static constexpr std::uint32_t kDefaultRemoveTimeoutMs = 5000;

    struct Entry;
    // One call's hooks, fixed when it starts so before and after pair up.
    struct Pending {
        std::vector<std::shared_ptr<Entry>> entries;
        HookedCall call;
    };

    // Calls on this thread pass unhooked while alive.
    class Quiet {
      public:
        Quiet();
        ~Quiet();
        Quiet(const Quiet &) = delete;
        Quiet &operator=(const Quiet &) = delete;
    };

    static FunctionHooks &Instance();

    // UStruct::SuperStruct, which leads an override to the function it overrides.
    void SetSuperOffset(std::int32_t offset) { superOffset_.store(offset, std::memory_order_release); }

    // Zero when function is null or both callbacks are.
    std::uint64_t Add(Address function, Callback before, Callback after, void *user);
    // True: no callback of it runs again. Other threads' calls past before get
    // their after first; false when they time out, and user must stay alive.
    bool Remove(std::uint64_t id, std::uint32_t timeoutMs = kDefaultRemoveTimeoutMs);

    // False when a before callback skips the body. Every Before needs its After.
    bool Before(Pending &pending, Address object, Address function, void *parms, void *result);
    void After(Pending &pending);

  private:
    FunctionHooks() = default;

    mutable std::shared_mutex mutex_;
    std::unordered_map<Address, std::vector<std::shared_ptr<Entry>>> byFunction_;
    std::unordered_map<std::uint64_t, Address> functionOf_;
    std::atomic<std::size_t> count_{0};
    std::atomic<std::int32_t> superOffset_{-1};
    std::uint64_t nextId_ = 1;
};

} // namespace URK::Unreal
