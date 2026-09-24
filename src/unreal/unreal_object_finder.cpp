#include "unreal_object_finder.h"

#include <chrono>

namespace URK::Unreal {
namespace {

std::uint64_t NowMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                          std::chrono::steady_clock::now().time_since_epoch())
                                          .count());
}

} // namespace

ObjectFinder ObjectFinder::Build(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &offsets) {
    ObjectFinder finder(objects, names, offsets);
    finder.Rebuild(*finder.index_);
    return finder;
}

void ObjectFinder::Rebuild(Index &index) const {
    index.byName.clear();
    index.count = 0;

    const std::int32_t total = objects_->Num();
    for (std::int32_t slot = 0; slot < total; ++slot) {
        const Address object = objects_->ObjectAt(slot);
        if (object == kNullAddress)
            continue;
        std::optional<std::string> name = NameOf(object);
        if (!name || name->empty())
            continue;
        index.byName[NameKey(*name)].push_back(object);
        ++index.count;
    }
    index.builtAtMs = NowMs();
}

std::size_t ObjectFinder::IndexedCount() const {
    std::lock_guard lock(index_->mutex);
    return index_->count;
}

bool IsLiveObject(const ObjectFinder &finder, Address candidate) {
    if (candidate == kNullAddress)
        return false;

    const ObjectOffsets &offsets = finder.Offsets();
    if (offsets.index == kOffsetNotFound)
        return false;

    const std::optional<std::int32_t> index = finder.Reader().ReadInt32(candidate + offsets.index);
    if (!index || *index < 0)
        return false;
    return finder.Objects().ObjectAt(*index) == candidate;
}

Address ObjectFinder::ClassOf(Address object) const {
    if (object == kNullAddress || offsets_.classPointer == kOffsetNotFound)
        return kNullAddress;
    const std::optional<Address> value = Reader().ReadPointer(object + offsets_.classPointer);
    return value ? *value : kNullAddress;
}

Address ObjectFinder::OuterOf(Address object) const {
    if (object == kNullAddress || offsets_.outer == kOffsetNotFound)
        return kNullAddress;
    const std::optional<Address> value = Reader().ReadPointer(object + offsets_.outer);
    return value ? *value : kNullAddress;
}

std::optional<std::string> ObjectFinder::BaseName(std::uint32_t comparisonIndex) const {
    {
        std::shared_lock lock(nameCache_->mutex);
        const auto cached = nameCache_->names.find(comparisonIndex);
        if (cached != nameCache_->names.end())
            return cached->second;
    }
    // Failures are not cached: an entry being written can read short once.
    std::optional<std::string> name = names_->Read(comparisonIndex);
    if (!name)
        return std::nullopt;
    std::unique_lock lock(nameCache_->mutex);
    nameCache_->names.emplace(comparisonIndex, *name);
    return name;
}

std::optional<std::string> ObjectFinder::NameOf(Address object) const {
    if (object == kNullAddress || offsets_.name == kOffsetNotFound)
        return std::nullopt;

    const std::optional<std::uint32_t> comparisonIndex = Reader().ReadUInt32(object + offsets_.name);
    if (!comparisonIndex)
        return std::nullopt;

    std::optional<std::string> name = BaseName(*comparisonIndex);
    if (!name)
        return std::nullopt;

    const std::optional<std::int32_t> number = Reader().ReadInt32(object + offsets_.name + sizeof(std::int32_t));
    if (number && *number > 0)
        name->append("_").append(std::to_string(*number - 1));
    return name;
}

// A hit must still be live and still carry the name: GC frees objects and
// reuses both their slots and their memory.
template <typename Accept> Address ObjectFinder::Lookup(std::string_view name, Accept accept) const {
    const std::string key = NameKey(name);
    std::lock_guard lock(index_->mutex);
    for (int pass = 0; pass < 2; ++pass) {
        const auto entry = index_->byName.find(key);
        if (entry != index_->byName.end()) {
            for (const Address candidate : entry->second) {
                const std::optional<std::string> current = NameOf(candidate);
                if (!IsLiveObject(*this, candidate) || !current || !SameName(*current, name))
                    continue;
                if (accept(candidate))
                    return candidate;
            }
        }
        if (pass == 1 || NowMs() - index_->builtAtMs < kRefreshIntervalMs)
            break;
        Rebuild(*index_);
    }
    return kNullAddress;
}

Address ObjectFinder::Find(std::string_view name) const {
    return Lookup(name, [](Address) { return true; });
}

Address ObjectFinder::FindInOuter(std::string_view name, std::string_view outerName) const {
    return Lookup(name, [&](Address candidate) {
        const Address outer = OuterOf(candidate);
        if (outer == kNullAddress)
            return false;
        const std::optional<std::string> resolved = NameOf(outer);
        return resolved && SameName(*resolved, outerName);
    });
}

Address ObjectFinder::FindInOuter(std::string_view name, Address outer) const {
    if (outer == kNullAddress)
        return kNullAddress;
    return Lookup(name, [&](Address candidate) { return OuterOf(candidate) == outer; });
}

} // namespace URK::Unreal
