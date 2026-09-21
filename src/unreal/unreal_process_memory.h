#pragma once

// MemoryReader over this process's own address space: the reader the
// calibration runs against once URKit is inside a game.
//
// The ladder probes far more addresses than it accepts, so asking the kernel
// about every one of them would make a scan of a shipped game's .data take
// minutes; committed ranges are therefore remembered between probes. Nothing
// remembered is a promise: a game frees memory while it is being walked, so a
// page that answered a moment ago can be gone by the time it is copied, and the
// copy itself has to survive that.

#include "unreal_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace URK::Unreal {

class ProcessMemory : public MemoryReader, public MemoryWriter {
  public:
    // How many probes a range that answered no is remembered for. A no has to
    // expire because a game commits memory while it is being walked, and a
    // range remembered as unmapped would stay invisible for good. A yes is kept
    // instead: when it goes stale the read fails and says so.
    static constexpr std::uint64_t kUnreadableLifetime = 0x4000;

    explicit ProcessMemory(std::uint64_t unreadableLifetime = kUnreadableLifetime)
        : unreadableLifetime_(unreadableLifetime) {}

    // Never faults, and never returns bytes it did not read in full.
    bool Read(Address address, void *out, std::size_t size) const override;

    // Whether the range is committed and readable now, as far as the remembered
    // ranges know. It touches nothing, so a stale yes costs a failed Read and
    // not a fault.
    bool Readable(Address address, std::size_t size) const override;

    // Writes only where the game already allows writing: a page the engine
    // protected stays protected, because lifting that is a decision a caller
    // has to make deliberately and not a side effect of setting a value.
    bool Write(Address address, const void *data, std::size_t size) override;

    bool Writable(Address address, std::size_t size) const override;

    // Drops what is remembered. Done automatically when a read faults, and
    // worth doing by hand after the game has unloaded something.
    void Forget() const;

    // Ranges the kernel was asked about, and how many of those questions the
    // remembered ranges answered instead. A scan that misses badly here is a
    // scan spending its time in the kernel.
    std::uint64_t Queries() const { return queries_; }
    std::uint64_t Hits() const { return hits_; }

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

    // Probes walk forward, so the range a probe needs is nearly always the one
    // before it or a close neighbour; a handful of slots is enough.
    static constexpr std::size_t kRememberedRanges = 8;

    bool Spans(Address address, std::size_t size, bool Range::*permission) const;
    const Range *Remembered(Address address) const;
    Range &SlotFor(Address address) const;
    const Range &Resolve(Address address) const;

    std::uint64_t unreadableLifetime_;
    mutable std::array<Range, kRememberedRanges> ranges_{};
    mutable std::size_t next_ = 0;
    mutable std::uint64_t probes_ = 0;
    mutable std::uint64_t queries_ = 0;
    mutable std::uint64_t hits_ = 0;
};

// Base of the image that hosts this process. For a shipped Unreal title that is
// the game executable, which is where the engine keeps its globals.
Address MainModuleBase();

// Base of one loaded module by name, or kNullAddress if it is not loaded.
Address ModuleBase(const wchar_t *name);

} // namespace URK::Unreal
