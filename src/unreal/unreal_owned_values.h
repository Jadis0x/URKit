#pragma once

// Engine memory a reflected call leaves in its parameter block: the FStrings and
// TArrays it returned or wrote to out parameters. The caller owns them, as a C++
// caller of ProcessEvent would. They are emptied through the engine's own move
// assignment (a native function returning an empty FString into the slot), so
// the engine's allocator frees what it allocated. Kinds that cannot be emptied
// that way are reported, and calls producing them are refused instead of leaking.

#include "unreal_functions.h"
#include "unreal_process_event.h"

#include <cstdint>
#include <string>
#include <vector>

namespace URK::Unreal {

enum class Ownership {
    None,         // numbers, names, object references
    Releasable,   // FString, TArray, structs of those
    Unreleasable, // FText, TSet, TMap, soft references, delegates, unknown kinds
};

class OwnedValues {
  public:
    OwnedValues(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
                const FunctionOffsets &functions, const TypeQueries &types, const ProcessEventLocation &processEvent)
        : finder_(&finder), chain_(&chain), values_(&values), functions_(&functions), types_(&types),
          processEvent_(processEvent) {}

    // Safe off the game thread: reads reflection only.
    Ownership Classify(const PropertyInfo &info, int depth = 0) const;

    // Game thread only. Finds the emptying call once and measures that it
    // allocates nothing; false for good when it does not.
    bool Ready();
    const std::string &Failure() const { return failure_; }

    // Game thread only, after Ready(). Leaves the value empty (all zero bytes).
    bool Release(const PropertyInfo &info, std::uint8_t *value, int depth = 0);

  private:
    bool Empty(std::uint8_t *array);
    bool Invoke();

    const ObjectFinder *finder_;
    const PropertyChain *chain_;
    const PropertyValues *values_;
    const FunctionOffsets *functions_;
    const TypeQueries *types_;
    ProcessEventLocation processEvent_;

    int state_ = 0; // 0 not measured, 1 ready, 2 unavailable
    std::string failure_;
    Address library_ = kNullAddress;
    Address function_ = kNullAddress;
    std::int32_t returnOffset_ = 0;
    std::vector<std::uint8_t> parms_;
};

} // namespace URK::Unreal
