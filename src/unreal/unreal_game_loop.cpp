#include "unreal_game_loop.h"

#include <algorithm>
#include <cstring>
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

// Target of a `++global`-style write to [rip+disp32], if any.
std::optional<Address> RipWriteTarget(const std::uint8_t *code, std::size_t length, Address at) {
    std::size_t i = 0;
    while (i < length && (code[i] == 0xF0 || code[i] == 0xF2 || code[i] == 0xF3 || code[i] == 0x2E ||
                          code[i] == 0x36 || code[i] == 0x3E || code[i] == 0x26 || code[i] == 0x64 ||
                          code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67))
        ++i;
    if (i < length && (code[i] & 0xF0) == 0x40)
        ++i;
    if (i + 2 > length)
        return std::nullopt;
    bool anyReg = false;
    if (code[i] == 0x0F) {
        if (code[i + 1] != 0xC1)
            return std::nullopt;
        anyReg = true;
        i += 2;
    } else {
        const std::uint8_t opcode = code[i++];
        if (opcode == 0x89 || opcode == 0x01)
            anyReg = true;
        else if (opcode != 0xC7 && opcode != 0x81 && opcode != 0x83 && opcode != 0xFF)
            return std::nullopt;
    }
    if (i + 5 > length)
        return std::nullopt;
    const std::uint8_t modrm = code[i];
    if ((modrm & 0xC7) != 0x05 || (!anyReg && (modrm & 0x38) != 0))
        return std::nullopt;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, code + i + 1, sizeof(displacement));
    return at + length + static_cast<std::int64_t>(displacement);
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

std::vector<Address> GameLoop::FindFrameCounterWrites(const MemoryReader &reader, std::span<const ScanRegion> code,
                                                      const FunctionTable &bounds, Address counter,
                                                      InstructionLength length) {
    std::vector<Address> writes;
    if (counter == kNullAddress || !length || bounds.Empty())
        return writes;

    // Every disp32 that reaches the counter from the end of an instruction
    // ending right after it, or after an imm8/imm32.
    constexpr std::size_t kChunk = 0x10000;
    constexpr std::size_t kOverlap = sizeof(std::int32_t) - 1;
    std::vector<Address> displacements;
    std::vector<std::uint8_t> buffer(kChunk + kOverlap);
    for (const ScanRegion &region : code) {
        for (std::uint64_t offset = 0; offset < region.size; offset += kChunk) {
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(kChunk + kOverlap, region.size - offset));
            if (want < sizeof(std::int32_t) || !reader.Read(region.start + offset, buffer.data(), want))
                continue;
            const Address base = region.start + offset;
            for (std::size_t i = 0; i + sizeof(std::int32_t) <= want; ++i) {
                std::int32_t displacement = 0;
                std::memcpy(&displacement, &buffer[i], sizeof(displacement));
                const Address after = base + i + sizeof(displacement) + static_cast<std::int64_t>(displacement);
                const Address gap = counter - after;
                if (gap == 0 || gap == 1 || gap == 4)
                    displacements.push_back(base + i);
            }
        }
    }

    // Decode each function holding one from its first instruction; a jump
    // table or padding that breaks the sweep only loses a candidate.
    constexpr std::size_t kMaxInstruction = 15;
    std::vector<FunctionRange> decoded;
    std::vector<std::uint8_t> body;
    for (const Address at : displacements) {
        const std::optional<FunctionRange> range = bounds.Containing(at);
        if (!range || range->end <= range->begin)
            continue;
        if (std::any_of(decoded.begin(), decoded.end(), [&](const FunctionRange &r) { return r.begin == range->begin; }))
            continue;
        decoded.push_back(*range);
        body.assign(static_cast<std::size_t>(range->end - range->begin) + kMaxInstruction, 0);
        if (!reader.Read(range->begin, body.data(), body.size() - kMaxInstruction))
            continue;
        for (std::size_t i = 0; i < body.size() - kMaxInstruction;) {
            const std::size_t size = length(&body[i], kMaxInstruction);
            if (size == 0)
                break;
            const Address instruction = range->begin + i;
            if (RipWriteTarget(&body[i], size, instruction) == counter)
                writes.push_back(instruction);
            i += size;
        }
    }
    return writes;
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
