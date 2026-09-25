#pragma once

// Non-faulting reader for calibration; tests plug in a synthetic image.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace URK::Unreal {

using Address = std::uint64_t;

inline constexpr Address kNullAddress = 0;

class MemoryReader {
  public:
    virtual ~MemoryReader() = default;

    // Returns false if any byte is unreadable; out is untouched then.
    virtual bool Read(Address address, void *out, std::size_t size) const = 0;

    // Separate from Read so a bad candidate can be rejected without a copy.
    virtual bool Readable(Address address, std::size_t size) const = 0;

    // For engine-held pointers: may skip the range check, never faults.
    virtual bool ReadTrusted(Address address, void *out, std::size_t size) const { return Read(address, out, size); }

    template <typename T> std::optional<T> ReadAs(Address address) const {
        static_assert(std::is_trivially_copyable_v<T>, "MemoryReader copies raw bytes");
        T value{};
        if (!Read(address, &value, sizeof(T)))
            return std::nullopt;
        return value;
    }

    std::optional<Address> ReadPointer(Address address) const { return ReadAs<Address>(address); }
    std::optional<std::int32_t> ReadInt32(Address address) const { return ReadAs<std::int32_t>(address); }
    std::optional<std::uint32_t> ReadUInt32(Address address) const { return ReadAs<std::uint32_t>(address); }

    // Cheap check before Readable(): user pointers are below 2^47.
    static bool PlausiblePointer(Address value) {
        constexpr Address kUserSpaceCeiling = Address{1} << 47;
        constexpr Address kFirstAllocatableAddress = 0x10000;
        return value >= kFirstAllocatableAddress && value < kUserSpaceCeiling;
    }

    // Holds a non-null pointer into mapped memory.
    bool PointsToReadable(Address address, std::size_t size = sizeof(Address)) const {
        const std::optional<Address> pointer = ReadPointer(address);
        return pointer && PlausiblePointer(*pointer) && Readable(*pointer, size);
    }

    // As above, plus a readable pointer at the target: every UObject has a vtable.
    bool PointsToObject(Address address) const {
        const std::optional<Address> pointer = ReadPointer(address);
        if (!pointer || !PlausiblePointer(*pointer))
            return false;
        return PointsToReadable(*pointer);
    }
};

// Kept apart from reading so calibration cannot change what it measures.
class MemoryWriter {
  public:
    virtual ~MemoryWriter() = default;

    // Returns false without writing anything if any byte is not writable.
    virtual bool Write(Address address, const void *data, std::size_t size) = 0;

    virtual bool Writable(Address address, std::size_t size) const = 0;

    template <typename T> bool WriteAs(Address address, const T &value) {
        static_assert(std::is_trivially_copyable_v<T>, "MemoryWriter copies raw bytes");
        return Write(address, &value, sizeof(T));
    }
};

} // namespace URK::Unreal
