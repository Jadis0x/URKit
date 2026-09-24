#pragma once

// Releases what a value owns, always through engine code.

#include "unreal_containers.h"
#include "unreal_engine_calls.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace URK::Unreal {

enum class Ownership {
    None,         // numbers, names, object/weak references, delegates
    Owned,        // anything holding engine memory the loader can release
    Unreleasable, // a kind the loader does not know
};

class OwnedValues {
  public:
    OwnedValues(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
                const TypeQueries &types, EngineCalls &engine)
        : finder_(&finder), chain_(&chain), values_(&values), types_(&types), engine_(&engine),
          containers_(finder, chain, values, engine, *this) {}

    // Reflection only; safe off the game thread.
    Ownership Classify(const PropertyInfo &info, int depth = 0) const;
    // Whether zeroed bytes are not yet a valid value (a text must hold the
    // engine's empty text).
    bool NeedsInitialize(const PropertyInfo &info, int depth = 0) const;

    // Game thread. Releases and zeroes one element; unchanged when refused.
    bool Destroy(const PropertyInfo &info, std::uint8_t *value, int depth = 0);
    // Game thread. Whether Destroy would release the value; changes nothing.
    bool Releasable(const PropertyInfo &info, const std::uint8_t *value);
    // Game thread. Makes zeroed bytes a valid default value.
    bool Initialize(const PropertyInfo &info, std::uint8_t *value, int depth = 0);
    // Game thread. Gives every text slot still zeroed the engine's empty text,
    // leaving texts already made alone; *made tells whether any was.
    bool FillNullTexts(const PropertyInfo &info, std::uint8_t *value, bool *made, int depth = 0);
    // Whether the value, or anything it contains, is an FText.
    bool HoldsText(const PropertyInfo &info, int depth = 0) const;
    // DefaultKeyFuncs equality; strings ignore case.
    bool Equal(const PropertyInfo &info, const std::uint8_t *a, const std::uint8_t *b, int depth = 0) const;

    const std::string &Failure() const { return failure_; }
    EngineCalls &Engine() { return *engine_; }
    Containers &Stores() { return containers_; }

    // FScriptDelegate as a multicast delegate's element: measured from a reflected
    // single-cast delegate, 0 when none was found.
    std::int32_t DelegateSize();
    // The element a multicast delegate's invocation list holds.
    PropertyInfo DelegateElement();

  private:
    template <typename Visit> bool ForEachMember(Address structObject, Visit visit, int depth) const;
    bool Fail(std::string why);
    bool Release(const PropertyInfo &info, std::uint8_t *value, int depth, bool apply);
    void Leaked(const std::string &why);

    const ObjectFinder *finder_;
    const PropertyChain *chain_;
    const PropertyValues *values_;
    const TypeQueries *types_;
    EngineCalls *engine_;
    Containers containers_;
    std::string failure_;
    int leaks_ = 0;
    std::string leakReason_;
    std::atomic<std::int32_t> delegateSize_{-1};
};

} // namespace URK::Unreal
