#pragma once

// MemoryReader over this process's own address space. Committed ranges are
// cached between probes, since the scan asks about far more addresses than it
// accepts - but a cached yes is never a promise: the game frees memory while
// it is being walked, so the copy itself must survive a stale answer.

#include "unreal_memory.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace URK::Unreal {

class ProcessMemory : public MemoryReader, public MemoryWriter {
  public:
    // A cached "no" must expire, or memory committed mid-scan stays invisible.
    // A "yes" need not: when it goes stale the read simply fails.
    static constexpr std::uint64_t kUnreadableLifetime = 0x4000;

    explicit ProcessMemory(std::uint64_t unreadableLifetime = kUnreadableLifetime)
        : unreadableLifetime_(unreadableLifetime) {}

    // Never faults, and never returns bytes it did not read in full.
    bool Read(Address address, void *out, std::size_t size) const override;

    // Touches nothing, so a stale yes costs a failed Read, not a fault.
    bool Readable(Address address, std::size_t size) const override;

    // Never lifts protection; that has to be a deliberate decision elsewhere.
    bool Write(Address address, const void *data, std::size_t size) override;

    bool Writable(Address address, std::size_t size) const override;

    // Only the calling thread's cache; others heal on their next failed read.
    void Forget() const;

    // Kernel queries vs cache hits, summed across threads. A bad ratio means
    // the scan is living in the kernel.
    std::uint64_t Queries() const { return queries_.load(std::memory_order_relaxed); }
    std::uint64_t Hits() const { return hits_.load(std::memory_order_relaxed); }

  private:
    struct Range {
        Address start = kNullAddress;
        Address end = kNullAddress;
        bool readable = false;
        bool writable = false;
        // The probe count this range stops answering at; zero never expires.
        std::uint64_t expires = 0;

        bool Holds(Address address) const { return start != end && address >= start && address < end; }
        bool Expired(std::uint64_t probes) const { return expires != 0 && probes >= expires; }
    };

    // Probes walk forward, so a handful of recent ranges covers most of them.
    static constexpr std::size_t kRememberedRanges = 8;

    // Per thread, not shared: the ABI is multi-threaded, and a shared cache
    // was both a data race and a source of mutual eviction. A lock is out of
    // the question on a path that runs tens of millions of times.
    struct Cache {
        std::array<Range, kRememberedRanges> ranges{};
        std::size_t next = 0;
        std::uint64_t probes = 0;
    };

    static Cache &ThreadCache();

    bool Spans(Address address, std::size_t size, bool Range::*permission) const;
    const Range *Remembered(Cache &cache, Address address) const;
    Range &SlotFor(Cache &cache, Address address) const;
    const Range &Resolve(Address address) const;

    std::uint64_t unreadableLifetime_;
    // Diagnostics only; nothing is ordered against them.
    mutable std::atomic<std::uint64_t> queries_{0};
    mutable std::atomic<std::uint64_t> hits_{0};
};

// Host image base; for a shipped Unreal title, where the globals live.
Address MainModuleBase();

// Base of one loaded module by name, or kNullAddress if it is not loaded.
Address ModuleBase(const wchar_t *name);

} // namespace URK::Unreal
