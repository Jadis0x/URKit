#pragma once

// GWorld and its actors; found as the global pointing at a live UWorld.

#include "unreal_module.h"
#include "unreal_property_values.h"
#include "unreal_type_queries.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace URK::Unreal {

struct WorldLocation {
    // The global itself, so a caller can follow it again after a level change.
    Address global = kNullAddress;
    Address world = kNullAddress;
    // How many globals pointed at this world; what the winner was chosen by.
    std::size_t references = 0;
};

// The world most globals agree on wins; the engine keeps several pointers.
std::optional<WorldLocation> FindWorldGlobal(const ObjectFinder &finder, const TypeQueries &types,
                                             std::span<const ScanRegion> regions);

// The walk from a world to what is in it.
class WorldView {
  public:
    WorldView(const ObjectFinder &finder, const TypeQueries &types, const PropertyChain &chain,
              const PropertyValues &values)
        : finder_(&finder), types_(&types), chain_(&chain), values_(&values) {}

    // The level the world keeps, read through the member of that name.
    Address PersistentLevelOf(Address world) const;

    // Every actor in the level, by whichever route the build allows.
    std::vector<Address> ActorsInLevel(Address level) const;

    // UE5 ships ULevel::Actors unreflected, so this finds nothing there.
    std::vector<Address> ReflectedActorList(Address level) const;

    // Via the object array (actor outer == level). Slower, always works.
    std::vector<Address> ActorsOwnedBy(Address level) const;

    // The two steps together.
    std::vector<Address> ActorsInWorld(Address world) const;

  private:
    // A member of the object's class or of anything it derives from.
    std::optional<PropertyInfo> MemberOf(Address object, std::string_view name) const;

    const ObjectFinder *finder_;
    const TypeQueries *types_;
    const PropertyChain *chain_;
    const PropertyValues *values_;
};

} // namespace URK::Unreal
