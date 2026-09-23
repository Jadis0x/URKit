#include "unreal_game_loop.h"

#include <algorithm>
#include <vector>

namespace URK::Unreal {
namespace {

// A native thunk is a few instructions; this covers one without an exception
// table entry of its own.
constexpr Address kMaxThunkBytes = 0x80;
// A frame counter of a running game is far below this; stray data is not.
constexpr std::uint64_t kMaxPlausibleFrame = std::uint64_t{1} << 40;

FunctionRange BodyAt(const FunctionTable &bounds, Address entry) {
    if (const std::optional<FunctionRange> range = bounds.Containing(entry); range && range->begin == entry)
        return *range;
    const Address next = bounds.NextBegin(entry);
    const Address end = next != kNullAddress && next > entry ? std::min(next, entry + kMaxThunkBytes)
                                                             : entry + kMaxThunkBytes;
    return FunctionRange{.begin = entry, .end = end};
}

} // namespace

Address GameLoop::FindFrameCounter(const ObjectFinder &finder, const FunctionOffsets &functions,
                                   const FunctionTable &bounds, std::span<const ScanRegion> writable) {
    if (functions.func == kOffsetNotFound)
        return kNullAddress;
    const Address function = finder.FindInOuter("GetFrameCount", "KismetSystemLibrary");
    if (function == kNullAddress)
        return kNullAddress;
    const std::optional<Address> thunk = finder.Reader().ReadPointer(function + functions.func);
    if (!thunk || *thunk == kNullAddress)
        return kNullAddress;

    // The thunk either loads GFrameCounter itself or calls GetFrameCount, which does.
    Address entry = *thunk;
    for (int depth = 0; depth < 2 && entry != kNullAddress; ++depth) {
        const FunctionRange body = BodyAt(bounds, entry);
        std::vector<Address> loads = GlobalReferences(finder.Reader(), body, writable);
        std::sort(loads.begin(), loads.end());
        loads.erase(std::unique(loads.begin(), loads.end()), loads.end());
        if (loads.size() == 1) {
            const std::optional<std::uint64_t> frame = finder.Reader().ReadAs<std::uint64_t>(loads.front());
            return frame && *frame < kMaxPlausibleFrame ? loads.front() : kNullAddress;
        }
        if (!loads.empty())
            return kNullAddress;
        entry = FirstBranchTarget(finder.Reader(), body).value_or(kNullAddress);
    }
    return kNullAddress;
}

Address GameLoop::Engine() {
    if (engine_ != kNullAddress && IsLiveObject(*finder_, engine_))
        return engine_;
    engine_ = kNullAddress;
    // A game may subclass either; the class itself is never the running engine.
    for (const char *className : {"GameEngine", "Engine"}) {
        const Address engineClass = finder_->Find(className);
        if (engineClass == kNullAddress)
            continue;
        TypeQueries::InstanceQuery query{};
        query.classObject = engineClass;
        query.limit = 1;
        const std::vector<Address> engines = types_->InstancesOf(query);
        if (!engines.empty()) {
            engine_ = engines.front();
            break;
        }
    }
    return engine_;
}

const PropertyInfo *GameLoop::Resolve(Address object, Member &member, const char *name) {
    const Address owner = finder_->ClassOf(object);
    if (owner == kNullAddress)
        return nullptr;
    if (member.owner != owner) {
        // A miss is cached too: the same class is asked every frame.
        member.owner = owner;
        member.info.reset();
        const Address field = chain_->FindMemberDeep(owner, name);
        if (field != kNullAddress) {
            std::optional<PropertyInfo> info = values_->Describe(field);
            if (info && info->Resolved())
                member.info = info;
        }
    }
    return member.info ? &*member.info : nullptr;
}

Address GameLoop::ReadMember(Address object, Member &member, const char *name) {
    const PropertyInfo *info = Resolve(object, member, name);
    return info ? values_->ReadObject(object, *info) : kNullAddress;
}

std::optional<bool> GameLoop::ReadFlag(Address object, Member &member, const char *name) {
    const PropertyInfo *info = Resolve(object, member, name);
    return info ? values_->ReadBool(object, *info) : std::nullopt;
}

WorldState GameLoop::CurrentWorld() {
    WorldState state;
    const Address engine = Engine();
    if (engine == kNullAddress)
        return state;
    const Address viewport = ReadMember(engine, viewport_, "GameViewport");
    if (viewport == kNullAddress)
        return state;
    const Address world = ReadMember(viewport, world_, "World");
    if (world == kNullAddress || !IsLiveObject(*finder_, world))
        return state;

    state.world = world;
    const Address gameState = ReadMember(world, gameState_, "GameState");
    if (gameState == kNullAddress) {
        // Null while the map streams in; unreadable means no way to wait.
        state.begunPlay = !gameState_.info.has_value();
        return state;
    }
    state.begunPlay = ReadFlag(gameState, begunPlay_, "bReplicatedHasBegunPlay").value_or(true);
    return state;
}

std::string GameLoop::MapName(Address world) const { return finder_->NameOf(world).value_or(std::string{}); }

std::int32_t GameLoop::ObjectIndex(Address object) const {
    const std::int32_t offset = finder_->Offsets().index;
    if (object == kNullAddress || offset == kOffsetNotFound)
        return -1;
    return finder_->Reader().ReadInt32(object + offset).value_or(-1);
}

} // namespace URK::Unreal
