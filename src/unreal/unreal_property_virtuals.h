#pragma once

// FProperty's own virtuals, called on the engine's property objects: the hash
// and the equality a TSet/TMap key has in the engine (GetValueTypeHashInternal,
// Identical), as FScriptSetHelper uses them. A container is then linked exactly
// as the engine links it, for any key, a struct with its own C++ hash included.
//
// Only the slot indexes are measured, once, from the parameters of two Kismet
// functions (an int32 and a byte property). The hash is the one slot whose
// int32 body is `mov eax,[rdx]; ret` and whose byte body is
// `movzx eax,byte [rdx]; ret`. Identical is the one slot before it whose bodies
// compare (sete, no call) and whose probe calls answer as equality must.
// ClearValue is the one slot after the hash whose bodies store zero
// (`mov [rdx],0; ret`); DestroyValue follows it (a no-op for numbers), and
// InitializeValue is the one slot a few past that which zeroes a probe value.
// CopyValues is the slot before the hash, proven by copying probes. Every
// FProperty class keeps FProperty's slot order, so the address is read from
// each property's own vtable at the call.

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
