#pragma once

// FProperty virtuals (hash, Identical, value ops) as the engine's set helpers
// use them. Slots are measured once from function bodies and probes.

#include "unreal_property_offsets.h"
#include "unreal_property_values.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <set>
#include <string>

namespace URK::Unreal {

class ObjectFinder;

class PropertyVirtuals {
  public:
    PropertyVirtuals(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values)
        : finder_(&finder), chain_(&chain), values_(&values) {}

    // Measured on first use; false (with Failure) when the slot was not proven.
    bool HashReady();
    bool IdenticalReady();
    std::string Failure();
    std::int32_t HashSlot();
    std::int32_t IdenticalSlot();

    // The engine's GetValueTypeHash of the value; none when the property has no
    // hash (CPF_HasGetValueTypeHash) or the slot is not proven.
    std::optional<std::uint32_t> Hash(const PropertyInfo &property, const std::uint8_t *value);
    // The engine's Identical(a, b, 0).
    std::optional<bool> Identical(const PropertyInfo &property, const std::uint8_t *a, const std::uint8_t *b);

    // InitializeValue and CopyValues, measured with the hash.
    bool ValueOpsReady();
    // One value of a property whose ArrayDim is 1, made on zeroed bytes as the
    // engine makes it: a struct's C++ constructor (its vtable) and defaults included.
    bool Initialize(const PropertyInfo &property, std::uint8_t *value);
    // An initialized dest takes a copy of src as the engine's assignment makes
    // it; what src points to is copied, never adopted.
    bool Copy(const PropertyInfo &property, std::uint8_t *dest, const std::uint8_t *src);

  private:
    void Measure();
    void MeasureLocked();
    // The property's vtable when it is a table of image code.
    void **VtableOf(Address field);

    const ObjectFinder *finder_;
    const PropertyChain *chain_;
    const PropertyValues *values_;
    std::mutex mutex_;
    bool measured_ = false;
    std::int32_t hashSlot_ = -1;
    std::int32_t identicalSlot_ = -1;
    std::int32_t initializeSlot_ = -1;
    std::int32_t copySlot_ = -1;
    std::string valueFailure_;
    std::string failure_;
    std::set<Address> vtables_;
};

} // namespace URK::Unreal
