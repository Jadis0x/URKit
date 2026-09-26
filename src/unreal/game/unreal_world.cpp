#include "unreal/game/unreal_world.h"

#include <algorithm>
#include <string>

namespace URK::Unreal {
namespace {

// Globals are pointer aligned, as they were for the bootstrap scan.
constexpr Address kScanStep = sizeof(Address);

// Renamed members are reported, not guessed.
constexpr const char *kWorldClassName = "World";
constexpr const char *kPersistentLevelName = "PersistentLevel";
constexpr const char *kActorsName = "Actors";
constexpr const char *kActorClassName = "Actor";

// A level of a shipped game holds thousands of actors, not millions.
constexpr std::int32_t kMaxActors = 0x40000;

} // namespace

std::optional<WorldLocation> FindWorldGlobal(const ObjectFinder &finder, const TypeQueries &types,
                                             std::span<const ScanRegion> regions) {
    const Address worldClass = finder.Find(kWorldClassName);
    if (worldClass == kNullAddress)
        return std::nullopt;

    const MemoryReader &reader = finder.Reader();
    struct Counted {
        Address world = kNullAddress;
        Address global = kNullAddress;
        std::size_t references = 0;
    };
    std::vector<Counted> counted;

    for (const ScanRegion &region : regions) {
        if (region.size < sizeof(Address))
            continue;
        const Address end = region.start + region.size - sizeof(Address);
        for (Address address = region.start; address <= end; address += kScanStep) {
            const std::optional<Address> pointer = reader.ReadPointer(address);
            if (!pointer || *pointer == kNullAddress)
                continue;
            if (!IsLiveObject(finder, *pointer))
                continue;
            if (!types.IsA(*pointer, worldClass))
                continue;

            const auto found = std::find_if(counted.begin(), counted.end(),
                                            [&](const Counted &entry) { return entry.world == *pointer; });
            if (found == counted.end())
                counted.push_back(Counted{*pointer, address, 1});
            else
                ++found->references;
        }
    }

    if (counted.empty())
        return std::nullopt;

    const auto best = std::max_element(counted.begin(), counted.end(),
                                       [](const Counted &left, const Counted &right) {
                                           return left.references < right.references;
                                       });
    return WorldLocation{.global = best->global, .world = best->world, .references = best->references};
}

std::optional<PropertyInfo> WorldView::MemberOf(Address object, std::string_view name) const {
    if (object == kNullAddress)
        return std::nullopt;
    const Address classObject = finder_->ClassOf(object);
    if (classObject == kNullAddress)
        return std::nullopt;

    const Address field = chain_->FindMemberDeep(classObject, name);
    if (field == kNullAddress)
        return std::nullopt;
    return values_->Describe(field);
}

Address WorldView::PersistentLevelOf(Address world) const {
    const std::optional<PropertyInfo> member = MemberOf(world, kPersistentLevelName);
    if (!member || member->kind != PropertyKind::Object)
        return kNullAddress;
    return values_->ReadObject(world, *member);
}

std::vector<Address> WorldView::ActorsInLevel(Address level) const {
    std::vector<Address> actors = ReflectedActorList(level);
    if (!actors.empty())
        return actors;
    return ActorsOwnedBy(level);
}

// Actors whose outer is the level, via the object array.
std::vector<Address> WorldView::ActorsOwnedBy(Address level) const {
    std::vector<Address> actors;
    if (level == kNullAddress)
        return actors;

    const Address actorClass = finder_->Find(kActorClassName);
    if (actorClass == kNullAddress)
        return actors;

    const ObjectArray &objects = finder_->Objects();
    const std::int32_t total = objects.Num();
    for (std::int32_t index = 0; index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress || finder_->OuterOf(object) != level)
            continue;
        if (types_->IsA(object, actorClass))
            actors.push_back(object);
    }
    return actors;
}

std::vector<Address> WorldView::ReflectedActorList(Address level) const {
    std::vector<Address> actors;

    const std::optional<PropertyInfo> member = MemberOf(level, kActorsName);
    if (!member || member->kind != PropertyKind::Array)
        return actors;

    const std::optional<ArrayView> view = values_->ReadArray(level, *member);
    if (!view || view->elementSize != static_cast<std::int32_t>(sizeof(Address)) || view->num > kMaxActors)
        return actors;

    actors.reserve(static_cast<std::size_t>(view->num));
    for (std::int32_t index = 0; index < view->num; ++index) {
        const std::optional<Address> actor = finder_->Reader().ReadPointer(view->ElementAt(index));
        // Destroyed actors leave null slots; keep going.
        if (actor && *actor != kNullAddress)
            actors.push_back(*actor);
    }
    return actors;
}

std::vector<Address> WorldView::ActorsInWorld(Address world) const {
    return ActorsInLevel(PersistentLevelOf(world));
}

} // namespace URK::Unreal
