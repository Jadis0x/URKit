#pragma once

// Finding the world, and walking from it to the actors in it.
//
// The world is not reached through the object array: a game holds several
// UWorld objects over its lifetime - the menu's, the level's, one being
// streamed - and the one that matters is whichever the global currently points
// at. So the global is what is looked for, in the same data sections the
// bootstrap scanned, and it is recognised by what it points at rather than by a
// signature: an object that the object array itself agrees is at that address,
// whose class derives from World.
//
// Everything past that is reflection. PersistentLevel and Actors are ordinary
// reflected members, so they are looked up by name and read through the
// calibrated property layout rather than through offsets of their own.

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

// Scans the regions for a pointer to a world. The world most globals agree on
// wins, because the engine keeps more than one pointer to the live one.
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

    // The list the level keeps, when it keeps one the engine reflects. UE5
    // ships ULevel::Actors as a plain member, so on those builds this finds
    // nothing and says so rather than guessing at an offset.
    std::vector<Address> ReflectedActorList(Address level) const;

    // Every actor the level owns, found through the object array instead: an
    // actor's outer is the level it lives in. Slower, and always available.
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
