#pragma once

// GFrameCounter and the viewport world via reflection (4.25-5.8).

#include "unreal_code_anchors.h"
#include "unreal_functions.h"
#include "unreal_type_queries.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace URK::Unreal {

struct WorldState {
    Address world = kNullAddress;
    // GameState->bReplicatedHasBegunPlay; true when the world has no readable
    // game state, so a map is still announced once.
    bool begunPlay = false;
};

class GameLoop {
  public:
    GameLoop(const ObjectFinder &finder, const TypeQueries &types, const PropertyChain &chain,
             const PropertyValues &values)
        : finder_(&finder), types_(&types), chain_(&chain), values_(&values) {}

    // GFrameCounter, read out of GetFrameCount's native thunk. Null if the
    // function or the load in it is not found.
    static Address FindFrameCounter(const ObjectFinder &finder, const FunctionOffsets &functions,
                                    const FunctionTable &bounds, std::span<const ScanRegion> writable);

    // The host's decoder: the length of the instruction code starts with, 0 if
    // it does not decode.
    using InstructionLength = Unreal::InstructionLength;

    // GFrameCounter writes, kept only on decoded instruction boundaries.
    static std::vector<Address> FindFrameCounterWrites(const MemoryReader &reader, std::span<const ScanRegion> code,
                                                       const FunctionTable &bounds, Address counter,
                                                       InstructionLength length);

    // Game thread only: members are resolved lazily and cached per class.
    WorldState CurrentWorld();

    // The world's object name, i.e. the map; its array index tells two loads of
    // one map apart.
    std::string MapName(Address world) const;
    std::int32_t ObjectIndex(Address object) const;

  private:
    struct Member {
        Address owner = kNullAddress;
        std::optional<PropertyInfo> info;
    };

    Address Engine();
    Address ReadMember(Address object, Member &member, const char *name);
    std::optional<bool> ReadFlag(Address object, Member &member, const char *name);
    const PropertyInfo *Resolve(Address object, Member &member, const char *name);

    const ObjectFinder *finder_;
    const TypeQueries *types_;
    const PropertyChain *chain_;
    const PropertyValues *values_;

    Address engine_ = kNullAddress;
    Member viewport_{};
    Member world_{};
    Member gameState_{};
    Member begunPlay_{};
};

} // namespace URK::Unreal
