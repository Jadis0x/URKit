#include "unreal_process_event.h"

#include <cstring>
#include <set>
#include <vector>

namespace URK::Unreal {
namespace {

// Far enough to cover ProcessEvent, one of the larger UObject methods.
constexpr std::size_t kMaxBodyBytes = 0x1000;
constexpr std::size_t kMinBodyBytes = 0x100;
constexpr std::int32_t kMaxVtableSlots = 256;

// Memory operands carrying this disp32. Only mod=10 encodes a full 32 bits,
// which is what separates an operand from four coincidental bytes.
std::int32_t CountDisplacement(const std::vector<std::uint8_t> &code, std::int32_t displacement) {
    if (displacement == kOffsetNotFound || code.size() < 6)
        return 0;

    std::uint8_t wanted[sizeof(displacement)];
    std::memcpy(wanted, &displacement, sizeof(wanted));

    std::int32_t hits = 0;
    for (std::size_t i = 1; i + sizeof(wanted) <= code.size(); ++i) {
        if (std::memcmp(&code[i], wanted, sizeof(wanted)) != 0)
            continue;
        const bool afterModRM = (code[i - 1] & 0xC0) == 0x80;
        const bool afterSib = i >= 2 && (code[i - 2] & 0xC0) == 0x80 && (code[i - 2] & 0x07) == 0x04;
        if (afterModRM || afterSib)
            ++hits;
    }
    return hits;
}

bool InRegions(std::span<const ScanRegion> regions, Address address) {
    for (const ScanRegion &region : regions) {
        if (address >= region.start && address < region.start + region.size)
            return true;
    }
    return false;
}

// UObject, by name first and by climbing Super as a fallback.
Address RootClass(const ObjectFinder &finder, const TypeQueries &types) {
    Address root = finder.Find("Object");
    if (root != kNullAddress)
        return root;

    Address climbing = finder.Find("Class");
    for (int steps = 0; climbing != kNullAddress && steps < 32; ++steps) {
        const Address super = types.SuperOf(climbing);
        if (super == kNullAddress)
            return climbing;
        climbing = super;
    }
    return kNullAddress;
}

std::vector<Address> ReadVtable(const MemoryReader &reader, std::span<const ScanRegion> code, Address object) {
    std::vector<Address> slots;
    const std::optional<Address> vtable = reader.ReadPointer(object);
    if (!vtable || *vtable == kNullAddress)
        return slots;

    for (std::int32_t slot = 0; slot < kMaxVtableSlots; ++slot) {
        const std::optional<Address> entry = reader.ReadPointer(*vtable + static_cast<Address>(slot) * sizeof(Address));
        if (!entry || !InRegions(code, *entry))
            break;
        slots.push_back(*entry);
    }
    return slots;
}

// Every known function entry (call targets + UFunction::Func), so a slot's body
// ends where the next function starts.
std::set<Address> KnownFunctionEntries(const ObjectFinder &finder, const StructOffsets &structs,
                                       const FunctionOffsets &functions, std::span<const ScanRegion> code) {
    constexpr std::size_t kChunk = 0x10000;
    constexpr std::size_t kCallLength = 5;

    std::set<Address> entries;

    // jmp rel32 is left out: as often a tail call as an entry.
    std::vector<std::uint8_t> buffer(kChunk + kCallLength);
    for (const ScanRegion &region : code) {
        for (std::uint64_t offset = 0; offset < region.size; offset += kChunk) {
            const std::size_t want =
                static_cast<std::size_t>(std::min<std::uint64_t>(kChunk + kCallLength, region.size - offset));
            if (!finder.Reader().Read(region.start + offset, buffer.data(), want))
                continue;
            for (std::size_t i = 0; i + kCallLength <= want; ++i) {
                if (buffer[i] != 0xE8)
                    continue;
                std::int32_t displacement = 0;
                std::memcpy(&displacement, &buffer[i + 1], sizeof(displacement));
                const Address target =
                    region.start + offset + i + kCallLength + static_cast<std::int64_t>(displacement);
                if (InRegions(code, target))
                    entries.insert(target);
            }
        }
    }

    if (functions.func == kOffsetNotFound)
        return entries;

    const ObjectArray &objects = finder.Objects();
    const std::int32_t count = objects.Num();
    for (std::int32_t i = 0; i < count; ++i) {
        const Address object = objects.ObjectAt(i);
        if (object == kNullAddress || !ObjectIs(finder, structs, object, kCastFlagFunction))
            continue;
        const std::optional<Address> entry =
            finder.Reader().ReadPointer(object + static_cast<Address>(functions.func));
        if (entry && *entry != kNullAddress && InRegions(code, *entry))
            entries.insert(*entry);
    }
    return entries;
}

} // namespace

std::optional<ProcessEventLocation> FindProcessEvent(const ObjectFinder &finder, const TypeQueries &types,
                                                     const StructOffsets &structs, const FunctionOffsets &functions,
                                                     std::span<const ScanRegion> codeRegions,
                                                     const FunctionTable &bounds) {
    if (functions.parmsSize == kOffsetNotFound || codeRegions.empty())
        return std::nullopt;

    const Address root = RootClass(finder, types);
    if (root == kNullAddress)
        return std::nullopt;
    const Address cdo = types.DefaultObjectOf(root);
    if (cdo == kNullAddress)
        return std::nullopt;

    const MemoryReader &reader = finder.Reader();
    const std::vector<Address> slots = ReadVtable(reader, codeRegions, cdo);
    if (slots.empty())
        return std::nullopt;

    // Built only if some slot has no exception-table entry to bound it.
    std::vector<Address> sorted;
    const auto guessedSpan = [&](Address target) {
        if (sorted.empty()) {
            std::set<Address> entries = KnownFunctionEntries(finder, structs, functions, codeRegions);
            entries.insert(slots.begin(), slots.end());
            sorted.assign(entries.begin(), entries.end());
        }
        const auto next = std::upper_bound(sorted.begin(), sorted.end(), target);
        const std::size_t span = next == sorted.end() ? kMaxBodyBytes : static_cast<std::size_t>(*next - target);
        return std::max(span, kMinBodyBytes);
    };

    // ParmsSize alone is not rare enough; only a frame builder needs it
    // together with ReturnValueOffset.
    std::vector<ProcessEventLocation> candidates;
    std::vector<std::uint8_t> body;
    for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        const Address target = slots[slot];
        // Without RTTI vtables abut, so a subclass's copy of the same function follows.
        if (std::any_of(candidates.begin(), candidates.end(),
                        [target](const ProcessEventLocation &c) { return c.baseImplementation == target; }))
            continue;

        // Stop where the function ends so the count belongs to this slot.
        std::size_t span = 0;
        if (const std::optional<FunctionRange> range = bounds.Containing(target); range && range->begin == target)
            span = static_cast<std::size_t>(range->end - target);
        else if (const Address next = bounds.NextBegin(target); next != kNullAddress && !range)
            span = static_cast<std::size_t>(next - target);
        else
            span = guessedSpan(target);
        const std::size_t size = std::min(span, kMaxBodyBytes);

        body.assign(size, 0);
        if (!reader.Read(target, body.data(), size))
            continue;

        ProcessEventLocation candidate;
        candidate.parmsSizeReferences = CountDisplacement(body, functions.parmsSize);
        candidate.returnOffsetReferences = CountDisplacement(body, functions.returnValueOffset);
        if (candidate.parmsSizeReferences == 0 || candidate.returnOffsetReferences == 0)
            continue;

        candidate.vtableIndex = static_cast<std::int32_t>(slot);
        candidate.baseImplementation = target;
        candidate.flagsReferences = CountDisplacement(body, functions.functionFlags);
        candidates.push_back(std::move(candidate));
    }

    if (candidates.empty())
        return std::nullopt;

    ProcessEventLocation found = candidates.front();
    for (std::size_t i = 1; i < candidates.size(); ++i)
        found.rivalSlots.push_back(candidates[i].vtableIndex);
    return found;
}

Address ProcessEventFor(const MemoryReader &reader, const ProcessEventLocation &location, Address object) {
    if (!location.Resolved() || object == kNullAddress)
        return kNullAddress;

    const std::optional<Address> vtable = reader.ReadPointer(object);
    if (!vtable || *vtable == kNullAddress)
        return kNullAddress;

    const std::optional<Address> entry =
        reader.ReadPointer(*vtable + static_cast<Address>(location.vtableIndex) * sizeof(Address));
    return entry.value_or(kNullAddress);
}

std::vector<Address> ProcessEventImplementations(const ObjectFinder &finder, const TypeQueries &types,
                                                 const StructOffsets &structs,
                                                 const ProcessEventLocation &location) {
    std::vector<Address> implementations;
    if (!location.Resolved())
        return implementations;

    // Base first: a caller that patches only one should patch the common one.
    implementations.push_back(location.baseImplementation);

    const MemoryReader &reader = finder.Reader();
    const ObjectArray &objects = finder.Objects();
    const std::int32_t count = objects.Num();
    for (std::int32_t i = 0; i < count; ++i) {
        const Address object = objects.ObjectAt(i);
        if (object == kNullAddress || !ObjectIs(finder, structs, object, kCastFlagClass))
            continue;

        const Address cdo = types.DefaultObjectOf(object);
        if (cdo == kNullAddress)
            continue;

        const Address entry = ProcessEventFor(reader, location, cdo);
        if (entry == kNullAddress)
            continue;
        if (std::find(implementations.begin(), implementations.end(), entry) == implementations.end())
            implementations.push_back(entry);
    }
    return implementations;
}

const FunctionParameter *CallFrame::Find(std::string_view name) const { return info_.Parameter(name); }

bool CallFrame::Set(std::string_view name, const void *value, std::size_t size) {
    const FunctionParameter *parameter = Find(name);
    if (!parameter || !value)
        return false;

    const PropertyInfo &info = parameter->info;
    if (info.offset == kOffsetNotFound || info.elementSize <= 0)
        return false;
    if (size != static_cast<std::size_t>(info.elementSize))
        return false;

    const std::size_t at = static_cast<std::size_t>(info.offset);
    if (at + size > bytes_.size())
        return false;

    std::memcpy(bytes_.data() + at, value, size);
    return true;
}

bool CallFrame::Get(std::string_view name, void *out, std::size_t size) const {
    const FunctionParameter *parameter = Find(name);
    if (!parameter || !out)
        return false;

    const PropertyInfo &info = parameter->info;
    if (info.offset == kOffsetNotFound || info.elementSize <= 0)
        return false;
    if (size != static_cast<std::size_t>(info.elementSize))
        return false;

    const std::size_t at = static_cast<std::size_t>(info.offset);
    if (at + size > bytes_.size())
        return false;

    std::memcpy(out, bytes_.data() + at, size);
    return true;
}

bool InvokeProcessEvent(const ProcessEventLocation &location, Address object, Address function, void *frame) {
    if (!location.Resolved() || object == kNullAddress || function == kNullAddress)
        return false;

    // In-process: addresses are ours, so follow them directly.
    auto **vtable = *reinterpret_cast<void ***>(static_cast<std::uintptr_t>(object));
    if (!vtable)
        return false;

    void *entry = vtable[location.vtableIndex];
    if (!entry)
        return false;

    using ProcessEventFn = void(__fastcall *)(void *, void *, void *);
    reinterpret_cast<ProcessEventFn>(entry)(reinterpret_cast<void *>(static_cast<std::uintptr_t>(object)),
                                            reinterpret_cast<void *>(static_cast<std::uintptr_t>(function)), frame);
    return true;
}

} // namespace URK::Unreal
