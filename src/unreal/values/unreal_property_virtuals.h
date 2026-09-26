#pragma once

// FProperty virtuals (hash, Identical, value ops); slots measured once.

#include "unreal/layout/unreal_property_offsets.h"
#include "unreal/values/unreal_property_values.h"

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

    // Engine GetValueTypeHash; none without CPF_HasGetValueTypeHash or a proven slot.
    std::optional<std::uint32_t> Hash(const PropertyInfo &property, const std::uint8_t *value);
    // The engine's Identical(a, b, 0).
    std::optional<bool> Identical(const PropertyInfo &property, const std::uint8_t *a, const std::uint8_t *b);

    // InitializeValue and CopyValues, measured with the hash.
    bool ValueOpsReady();
    // Constructs one value (ArrayDim 1) on zeroed bytes, as the engine does.
    bool Initialize(const PropertyInfo &property, std::uint8_t *value);
    // Engine assignment into an initialized dest; deep copy.
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
