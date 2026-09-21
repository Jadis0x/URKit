#include "unreal_process_memory.h"

#include <windows.h>

#include <cstring>

namespace URK::Unreal {
namespace {

// Protections a read is allowed through. PAGE_GUARD and PAGE_NOACCESS are not
// among them, and PAGE_GUARD especially must be rejected without touching the
// page: touching it is what arms the exception the game is waiting on.
constexpr DWORD kReadableProtections = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kWritableProtections = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                                       PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kBlockingProtections = PAGE_GUARD | PAGE_NOACCESS;

// A leaf with nothing to unwind: cl.exe rejects __try in a frame holding an
// object with a destructor, and the copy is the only thing that may fault.
//
// This handler only runs for a fault if the translation unit is built with
// asynchronous exceptions on - clang needs -fasync-exceptions, cl.exe needs
// nothing. Built without it, a freed page crashes the game instead of failing
// the read, which is the one thing this file exists to prevent.
bool CopyGuarded(const void *source, void *out, std::size_t size) {
    __try {
        std::memcpy(out, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

const ProcessMemory::Range *ProcessMemory::Remembered(Address address) const {
    for (const Range &range : ranges_) {
        if (range.Holds(address) && !range.Expired(probes_))
            return &range;
    }
    return nullptr;
}

// A range that is being re-asked takes its old slot back, so an expired answer
// cannot sit alongside the answer that replaced it.
ProcessMemory::Range &ProcessMemory::SlotFor(Address address) const {
    for (Range &range : ranges_) {
        if (range.Holds(address))
            return range;
    }
    Range &slot = ranges_[next_];
    next_ = (next_ + 1) % kRememberedRanges;
    return slot;
}

const ProcessMemory::Range &ProcessMemory::Resolve(Address address) const {
    ++probes_;
    if (const Range *remembered = Remembered(address)) {
        ++hits_;
        return *remembered;
    }

    ++queries_;
    Range resolved;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
        resolved.start = reinterpret_cast<Address>(info.BaseAddress);
        resolved.end = resolved.start + static_cast<Address>(info.RegionSize);
        const bool usable = info.State == MEM_COMMIT && (info.Protect & kBlockingProtections) == 0;
        resolved.readable = usable && (info.Protect & kReadableProtections) != 0;
        resolved.writable = usable && (info.Protect & kWritableProtections) != 0;
    } else {
        // Nothing is mapped there. Remembering the failure keeps a scan from
        // asking about every address in a hole.
        resolved.start = address;
        resolved.end = address + 1;
        resolved.readable = false;
    }

    if (!resolved.readable)
        resolved.expires = probes_ + unreadableLifetime_;

    Range &slot = SlotFor(address);
    slot = resolved;
    return slot;
}

void ProcessMemory::Forget() const {
    ranges_ = {};
    next_ = 0;
}

// A range can end mid-request, so the walk continues into the next one:
// adjacent commits carrying the permission read as one buffer.
bool ProcessMemory::Spans(Address address, std::size_t size, bool Range::*permission) const {
    if (address == kNullAddress || size == 0)
        return false;
    if (address + size < address)
        return false;

    const Address end = address + size;
    Address cursor = address;
    while (cursor < end) {
        const Range &range = Resolve(cursor);
        if (!(range.*permission))
            return false;
        if (range.end <= cursor)
            return false;
        cursor = range.end;
    }
    return true;
}

bool ProcessMemory::Readable(Address address, std::size_t size) const {
    return Spans(address, size, &Range::readable);
}

bool ProcessMemory::Writable(Address address, std::size_t size) const {
    return Spans(address, size, &Range::writable);
}

bool ProcessMemory::Read(Address address, void *out, std::size_t size) const {
    if (!out || !Readable(address, size))
        return false;

    if (CopyGuarded(reinterpret_cast<const void *>(address), out, size))
        return true;

    // The range was freed or reprotected between the question and the copy, so
    // what is remembered about it is wrong - and so may be its neighbours, if
    // they were freed in the same call.
    Forget();
    return false;
}

bool ProcessMemory::Write(Address address, const void *data, std::size_t size) {
    if (!data || !Writable(address, size))
        return false;

    if (CopyGuarded(data, reinterpret_cast<void *>(address), size))
        return true;

    Forget();
    return false;
}

Address MainModuleBase() { return reinterpret_cast<Address>(GetModuleHandleW(nullptr)); }

Address ModuleBase(const wchar_t *name) { return reinterpret_cast<Address>(GetModuleHandleW(name)); }

} // namespace URK::Unreal
