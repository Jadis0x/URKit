#pragma once

// GFrameCounter and the viewport world via reflection (4.25-5.8).

#include "unreal/detect/unreal_code_anchors.h"
#include "unreal/reflection/unreal_functions.h"
#include "unreal/reflection/unreal_type_queries.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace URK::Unreal {

struct WorldState {
    Address world = kNullAddress;
    // A reload may reuse the world's address; its game state is always a new actor.
    Address gameState = kNullAddress;
    // GameState->bReplicatedHasBegunPlay; true without a readable game state.
    bool begunPlay = false;
};

class GameLoop {
  public:
    GameLoop(const ObjectFinder &finder, const TypeQueries &types, const PropertyChain &chain,
             const PropertyValues &values)
        : finder_(&finder), types_(&types), chain_(&chain), values_(&values) {}

    // GFrameCounter from GetFrameCount's thunk, or null.
    static Address FindFrameCounter(const ObjectFinder &finder, const FunctionOffsets &functions,
                                    const FunctionTable &bounds, std::span<const ScanRegion> writable);

    // Instruction length decoder; 0 when undecodable.
    using InstructionLength = Unreal::InstructionLength;

    // GFrameCounter writes, kept only on decoded instruction boundaries.
    static std::vector<Address> FindFrameCounterWrites(const MemoryReader &reader, std::span<const ScanRegion> code,
                                                       const FunctionTable &bounds, Address counter,
                                                       InstructionLength length);

    // Game thread only: members are resolved lazily and cached per class.
    WorldState CurrentWorld();

    // World name (the map); its array index separates reloads of the same map.
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
