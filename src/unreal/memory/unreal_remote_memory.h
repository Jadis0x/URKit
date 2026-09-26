#pragma once

// Read-only MemoryReader over another process; page cache, Forget() drops it.

#include "unreal/memory/unreal_memory.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {

// A handle to a running process, closed when this goes away.
class RemoteProcess {
  public:
    RemoteProcess() = default;
    ~RemoteProcess();

    RemoteProcess(const RemoteProcess &) = delete;
    RemoteProcess &operator=(const RemoteProcess &) = delete;
    RemoteProcess(RemoteProcess &&other) noexcept;
    RemoteProcess &operator=(RemoteProcess &&other) noexcept;

    // First process with this executable name, or nothing.
    static std::optional<RemoteProcess> OpenByName(const std::wstring &executable);

    bool Valid() const { return handle_ != nullptr; }
    void *Handle() const { return handle_; }
    std::uint32_t Pid() const { return pid_; }

    // Main executable module; shipped titles keep globals there.
    Address MainModuleBase() const { return moduleBase_; }
    std::uint64_t MainModuleSize() const { return moduleSize_; }
    const std::wstring &Executable() const { return executable_; }

  private:
    void *handle_ = nullptr;
    std::uint32_t pid_ = 0;
    Address moduleBase_ = kNullAddress;
    std::uint64_t moduleSize_ = 0;
    std::wstring executable_;
};

class RemoteMemory : public MemoryReader {
  public:
    explicit RemoteMemory(const RemoteProcess &process) : handle_(process.Handle()) {}

    bool Read(Address address, void *out, std::size_t size) const override;
    bool Readable(Address address, std::size_t size) const override;

    void Forget() const;

    // What the crossing cost: pages fetched, and reads served without one.
    std::uint64_t Fetches() const { return fetches_; }
    std::uint64_t Queries() const { return queries_; }
    std::uint64_t Reads() const { return reads_; }

  private:
    static constexpr std::size_t kPageSize = 0x1000;
    // Enough for one scan of a shipped title; dropped wholesale, not evicted.
    static constexpr std::size_t kMaxPages = 0x4000;

    struct Range {
        Address start = kNullAddress;
        Address end = kNullAddress;
        bool readable = false;
    };

    const std::uint8_t *Page(Address page) const;
    bool RangeReadable(Address address) const;

    void *handle_ = nullptr;
    mutable std::unordered_map<Address, std::vector<std::uint8_t>> pages_;
    mutable std::unordered_map<Address, Range> ranges_;
    mutable std::uint64_t fetches_ = 0;
    mutable std::uint64_t queries_ = 0;
    mutable std::uint64_t reads_ = 0;
};

} // namespace URK::Unreal
