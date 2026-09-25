#pragma once

// FMulticastDelegateProperty virtuals; sparse delegate bindings are only reachable through them.

#include "unreal_property_offsets.h"
#include "unreal_property_values.h"

#include <cstdint>
#include <mutex>
#include <string>

namespace URK::Unreal {

class ObjectFinder;
class OwnedValues;

class DelegateVirtuals {
  public:
    DelegateVirtuals(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
                     OwnedValues &owned)
        : finder_(&finder), chain_(&chain), values_(&values), owned_(&owned) {}

    // Game thread. Measured on first use with a probe list the engine fills and empties.
    bool Ready();
    const std::string &Failure() const { return failure_; }

    // The bindings list (FMulticastScriptDelegate), null when none is bound.
    const std::uint8_t *List(const PropertyInfo &property, const std::uint8_t *value);
    // delegate: an FScriptDelegate as the engine lays it out (weak object, FName).
    bool Add(const PropertyInfo &property, Address owner, std::uint8_t *value, const std::uint8_t *delegate);
    bool Remove(const PropertyInfo &property, Address owner, std::uint8_t *value, const std::uint8_t *delegate);
    bool Clear(const PropertyInfo &property, Address owner, std::uint8_t *value);

  private:
    void Measure();
    void **VtableOf(Address field) const;

    const ObjectFinder *finder_;
    const PropertyChain *chain_;
    const PropertyValues *values_;
    OwnedValues *owned_;
    std::mutex mutex_;
    bool measured_ = false;
    std::int32_t getSlot_ = -1;
    std::int32_t delegateSize_ = 0;
    std::string failure_;
};

} // namespace URK::Unreal
