#include "unreal_remote_memory.h"

#include <windows.h>

#include <tlhelp32.h>

#include <algorithm>
#include <cstring>

namespace URK::Unreal {
namespace {

constexpr DWORD kReadableProtections = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READ |
                                       PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
constexpr DWORD kBlockingProtections = PAGE_GUARD | PAGE_NOACCESS;

// Only what reading needs: nothing here may change the game.
constexpr DWORD kAccess = PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;

std::optional<std::uint32_t> FindProcessId(const std::wstring &executable) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return std::nullopt;

    std::optional<std::uint32_t> pid;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, executable.c_str()) == 0) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return pid;
}

// The module the process was started from, which a snapshot lists first.
bool FindMainModule(std::uint32_t pid, Address &base, std::uint64_t &size) {
    HANDLE snapshot = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 8 && snapshot == INVALID_HANDLE_VALUE; ++attempt) {
        snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
        // Snapshots fail while the loader lock is held; common during startup.
        if (snapshot == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH)
            return false;
    }
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const bool found = Module32FirstW(snapshot, &entry) != FALSE;
    if (found) {
        base = reinterpret_cast<Address>(entry.modBaseAddr);
        size = entry.modBaseSize;
    }

    CloseHandle(snapshot);
    return found;
}

} // namespace

RemoteProcess::~RemoteProcess() {
    if (handle_)
        CloseHandle(handle_);
}

RemoteProcess::RemoteProcess(RemoteProcess &&other) noexcept
    : handle_(other.handle_), pid_(other.pid_), moduleBase_(other.moduleBase_), moduleSize_(other.moduleSize_),
      executable_(std::move(other.executable_)) {
    other.handle_ = nullptr;
}

RemoteProcess &RemoteProcess::operator=(RemoteProcess &&other) noexcept {
    if (this != &other) {
        if (handle_)
            CloseHandle(handle_);
        handle_ = other.handle_;
        pid_ = other.pid_;
        moduleBase_ = other.moduleBase_;
        moduleSize_ = other.moduleSize_;
        executable_ = std::move(other.executable_);
        other.handle_ = nullptr;
    }
    return *this;
}

std::optional<RemoteProcess> RemoteProcess::OpenByName(const std::wstring &executable) {
    const std::optional<std::uint32_t> pid = FindProcessId(executable);
    if (!pid)
        return std::nullopt;

    const HANDLE handle = OpenProcess(kAccess, FALSE, *pid);
    if (handle == nullptr)
        return std::nullopt;

    RemoteProcess process;
    process.handle_ = handle;
    process.pid_ = *pid;
    process.executable_ = executable;
    if (!FindMainModule(*pid, process.moduleBase_, process.moduleSize_))
        return std::nullopt;

    return process;
}

const std::uint8_t *RemoteMemory::Page(Address page) const {
    const auto found = pages_.find(page);
    if (found != pages_.end()) {
        // Remember unreadable pages too, or every dead pointer costs a syscall.
        return found->second.empty() ? nullptr : found->second.data();
    }

    if (pages_.size() >= kMaxPages)
        pages_.clear();

    std::vector<std::uint8_t> bytes(kPageSize, 0);
    SIZE_T read = 0;
    ++fetches_;
    if (!ReadProcessMemory(handle_, reinterpret_cast<LPCVOID>(page), bytes.data(), kPageSize, &read) ||
        read != kPageSize) {
        pages_.emplace(page, std::vector<std::uint8_t>{});
        return nullptr;
    }

    return pages_.emplace(page, std::move(bytes)).first->second.data();
}

bool RemoteMemory::RangeReadable(Address address) const {
    const Address key = address & ~static_cast<Address>(0xFFFF);
    const auto found = ranges_.find(key);
    if (found != ranges_.end() && found->second.start <= address && address < found->second.end)
        return found->second.readable;

    ++queries_;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQueryEx(handle_, reinterpret_cast<LPCVOID>(address), &info, sizeof(info)) != sizeof(info)) {
        ranges_[key] = Range{address, address + 1, false};
        return false;
    }

    Range range;
    range.start = reinterpret_cast<Address>(info.BaseAddress);
    range.end = range.start + static_cast<Address>(info.RegionSize);
    range.readable = info.State == MEM_COMMIT && (info.Protect & kBlockingProtections) == 0 &&
                     (info.Protect & kReadableProtections) != 0;
    ranges_[key] = range;
    return range.readable;
}

bool RemoteMemory::Readable(Address address, std::size_t size) const {
    if (address == kNullAddress || size == 0)
        return false;
    if (address + size < address)
        return false;

    for (Address cursor = address & ~static_cast<Address>(kPageSize - 1); cursor < address + size;
         cursor += kPageSize) {
        if (!RangeReadable(cursor))
            return false;
    }
    return true;
}

bool RemoteMemory::Read(Address address, void *out, std::size_t size) const {
    if (!out || address == kNullAddress || size == 0)
        return false;
    if (address + size < address)
        return false;

    ++reads_;
    auto *destination = static_cast<std::uint8_t *>(out);
    std::size_t copied = 0;
    while (copied < size) {
        const Address at = address + copied;
        const Address page = at & ~static_cast<Address>(kPageSize - 1);
        const std::uint8_t *bytes = Page(page);
        if (bytes == nullptr)
            return false;

        const std::size_t within = static_cast<std::size_t>(at - page);
        const std::size_t available = kPageSize - within;
        const std::size_t take = std::min(available, size - copied);
        std::memcpy(destination + copied, bytes + within, take);
        copied += take;
    }
    return true;
}

void RemoteMemory::Forget() const {
    pages_.clear();
    ranges_.clear();
}

} // namespace URK::Unreal
