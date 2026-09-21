#pragma once

// Flat process image for the Unreal calibration tests. Nothing is mapped
// outside it, so a speculative read of a bogus pointer fails the way it would
// against a live process.

#include "src/unreal/unreal_memory.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace UnrealTest {

using URK::Unreal::Address;

class FakeImage : public URK::Unreal::MemoryReader {
  public:
    FakeImage(Address base, std::size_t size) : base_(base), bytes_(size, 0) {}

    Address Base() const { return base_; }
    Address At(Address offset) const { return base_ + offset; }

    bool Read(Address address, void *out, std::size_t size) const override {
        if (!Readable(address, size))
            return false;
        std::memcpy(out, bytes_.data() + (address - base_), size);
        return true;
    }

    bool Readable(Address address, std::size_t size) const override {
        if (address < base_ || size == 0)
            return false;
        const Address offset = address - base_;
        return offset + size <= bytes_.size();
    }

    template <typename T> void Put(Address offset, T value) {
        std::memcpy(bytes_.data() + offset, &value, sizeof(T));
    }

    void PutBytes(Address offset, const void *data, std::size_t size) {
        std::memcpy(bytes_.data() + offset, data, size);
    }

  private:
    Address base_;
    std::vector<std::uint8_t> bytes_;
};

} // namespace UnrealTest
