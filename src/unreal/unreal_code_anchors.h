#pragma once

// Engine globals located from the code that uses them instead of by scanning
// data. Only the image is read, so candidates exist before the engine builds
// either global; validating them is the caller's job.

#include "unreal_module.h"

#include <string_view>
#include <vector>

namespace URK::Unreal {

// Best-supported first. Unvalidated: they may not hold anything yet.
struct GlobalCandidates {
    std::vector<Address> objectArrays;
    std::vector<Address> namePools;

    bool Empty() const { return objectArrays.empty() && namePools.empty(); }
};

// GUObjectArray from UObjectBaseInit, which reads the "gc.MaxObjectsInGame"
// config key; FNamePool from its constructor, which stores "ByteProperty".
// Both literals are unchanged from UE4.25 to UE5.8 and survive shipping builds.
GlobalCandidates FindGlobalCandidates(const MemoryReader &reader, Address moduleBase);

// Functions taking the text's address, directly or via a UE5 log record.
std::vector<Address> FunctionsReferencingText(const MemoryReader &reader, Address moduleBase,
                                              const FunctionTable &functions, std::string_view text);

// The one function reading "t.IdleWhenNotForeground" plus a second Tick string.
Address FindEngineLoopTick(const MemoryReader &reader, Address moduleBase, const FunctionTable &functions);

// Count of rip-relative lea's taking each target's address.
std::vector<std::size_t> AddressTakenCounts(const MemoryReader &reader, std::span<const ScanRegion> code,
                                            const std::vector<Address> &targets);

// Globals the code in range reads or writes through rip-relative operands,
// kept to the writable regions. Not a decoder: stray matches are possible.
std::vector<Address> GlobalReferences(const MemoryReader &reader, const FunctionRange &range,
                                      std::span<const ScanRegion> writable);

// The first call or jump rel32 in range, for following a thunk into its body.
std::optional<Address> FirstBranchTarget(const MemoryReader &reader, const FunctionRange &range);

} // namespace URK::Unreal
