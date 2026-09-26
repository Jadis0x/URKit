#include "unreal/memory/unreal_process_memory.h"

#include <windows.h>

#include <algorithm>
#include <cstring>

namespace URK::Unreal {
namespace {

// Readable protections. PAGE_GUARD is rejected: touching it fires the game's exception.
constexpr DWORD kReadableProtections = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kWritableProtections = PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE |
                                       PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kBlockingProtections = PAGE_GUARD | PAGE_NOACCESS;

// No destructors here (cl.exe C2712); clang needs -fasync-exceptions.
bool CopyGuarded(const void *source, void *out, std::size_t size) {
    __try {
        std::memcpy(out, source, size);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Records the fault so a tripped guard page can be put back.
int CatchFault(const EXCEPTION_POINTERS *info, DWORD *code, ULONG_PTR *address) {
    const EXCEPTION_RECORD *record = info->ExceptionRecord;
    *code = record->ExceptionCode;
    *address = record->NumberParameters >= 2 ? record->ExceptionInformation[1] : 0;
    return EXCEPTION_EXECUTE_HANDLER;
}

bool CopyCatching(const void *source, void *out, std::size_t size, DWORD *code, ULONG_PTR *address) {
    __try {
        std::memcpy(out, source, size);
        return true;
    } __except (CatchFault(GetExceptionInformation(), code, address)) {
        return false;
    }
}

// Touching a guard page clears it; a thread stack would then stop growing.
void RearmGuardPage(ULONG_PTR address) {
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info) ||
        info.State != MEM_COMMIT)
        return;
    DWORD old = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(address), 1, info.Protect | PAGE_GUARD, &old);
}

} // namespace

bool ProcessMemory::Range::Expired(std::uint64_t probes) const {
    return expires != 0 && (probes >= expires || GetTickCount64() >= expiresAtMs);
}

ProcessMemory::Cache &ProcessMemory::ThreadCache() {
    thread_local Cache cache;
    return cache;
}

// The last hit first: scans read one range many times in a row.
const ProcessMemory::Range *ProcessMemory::Remembered(Cache &cache, Address address) const {
    const Range &last = cache.ranges[cache.last];
    if (last.Holds(address) && !last.Expired(cache.probes))
        return &last;
    for (std::size_t i = 0; i < kRememberedRanges; ++i) {
        const Range &range = cache.ranges[i];
        if (range.Holds(address) && !range.Expired(cache.probes)) {
            cache.last = i;
            return &range;
        }
    }
    return nullptr;
}

// A re-queried range reuses its old slot so stale answers don't linger.
ProcessMemory::Range &ProcessMemory::SlotFor(Cache &cache, Address address) const {
    for (Range &range : cache.ranges) {
        if (range.Holds(address))
            return range;
    }
    Range &slot = cache.ranges[cache.next];
    cache.next = (cache.next + 1) % kRememberedRanges;
    return slot;
}

const ProcessMemory::Range &ProcessMemory::Resolve(Address address) const {
    Cache &cache = ThreadCache();
    ++cache.probes;
    if (const Range *remembered = Remembered(cache, address)) {
        hits_.fetch_add(1, std::memory_order_relaxed);
        return *remembered;
    }

    queries_.fetch_add(1, std::memory_order_relaxed);
    Range resolved;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) == sizeof(info)) {
        resolved.start = reinterpret_cast<Address>(info.BaseAddress);
        resolved.end = resolved.start + static_cast<Address>(info.RegionSize);
        const bool usable = info.State == MEM_COMMIT && (info.Protect & kBlockingProtections) == 0;
        resolved.readable = usable && (info.Protect & kReadableProtections) != 0;
        resolved.writable = usable && (info.Protect & kWritableProtections) != 0;
    } else {
        // Unmapped; cache the miss so scans skip the hole.
        resolved.start = address;
        resolved.end = address + 1;
        resolved.readable = false;
    }

    if (!resolved.readable) {
        // Only the 64 KB granule asked about: a free region spans gigabytes, and new allocations land in it.
        constexpr Address kGranule = 0x10000;
        const Address granule = address & ~(kGranule - 1);
        resolved.start = std::max(resolved.start, granule);
        resolved.end = std::min(resolved.end, granule + kGranule);
        resolved.expires = cache.probes + unreadableLifetime_;
        resolved.expiresAtMs = GetTickCount64() + kUnreadableMs;
    }

    Range &slot = SlotFor(cache, address);
    slot = resolved;
    return slot;
}

void ProcessMemory::Forget() const {
    Cache &cache = ThreadCache();
    cache.ranges = {};
    cache.next = 0;
    cache.last = 0;
}

// Continue into adjacent commits with the same permission.
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

    // Freed or reprotected since the query; its neighbours may be stale too.
    Forget();
    return false;
}

bool ProcessMemory::ReadTrusted(Address address, void *out, std::size_t size) const {
    if (!out || size == 0 || !PlausiblePointer(address) || !PlausiblePointer(address + size - 1))
        return false;
    DWORD code = 0;
    ULONG_PTR faultAddress = 0;
    if (CopyCatching(reinterpret_cast<const void *>(address), out, size, &code, &faultAddress))
        return true;
    if (code == STATUS_GUARD_PAGE_VIOLATION)
        RearmGuardPage(faultAddress);
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
