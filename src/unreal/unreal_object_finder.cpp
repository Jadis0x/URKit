#include "unreal_object_finder.h"

namespace URK::Unreal {

ObjectFinder ObjectFinder::Build(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &offsets) {
    ObjectFinder finder(objects, names, offsets);

    const std::int32_t total = objects.Num();
    for (std::int32_t index = 0; index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;
        std::optional<std::string> name = finder.NameOf(object);
        if (!name || name->empty())
            continue;
        finder.byName_[*name].push_back(object);
        ++finder.count_;
    }

    return finder;
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

std::optional<std::string> ObjectFinder::NameOf(Address object) const {
    if (object == kNullAddress || offsets_.name == kOffsetNotFound)
        return std::nullopt;

    const std::optional<std::uint32_t> comparisonIndex = Reader().ReadUInt32(object + offsets_.name);
    if (!comparisonIndex)
        return std::nullopt;

    std::optional<std::string> name = names_->Read(*comparisonIndex);
    if (!name)
        return std::nullopt;

    const std::optional<std::int32_t> number = Reader().ReadInt32(object + offsets_.name + sizeof(std::int32_t));
    if (number && *number > 0)
        name->append("_").append(std::to_string(*number - 1));
    return name;
}

Address ObjectFinder::Find(std::string_view name) const {
    const auto entry = byName_.find(std::string(name));
    if (entry == byName_.end() || entry->second.empty())
        return kNullAddress;
    return entry->second.front();
}

Address ObjectFinder::FindInOuter(std::string_view name, std::string_view outerName) const {
    const auto entry = byName_.find(std::string(name));
    if (entry == byName_.end())
        return kNullAddress;

    for (const Address candidate : entry->second) {
        const Address outer = OuterOf(candidate);
        if (outer == kNullAddress)
            continue;
        const std::optional<std::string> resolved = NameOf(outer);
        if (resolved && *resolved == outerName)
            return candidate;
    }
    return kNullAddress;
}

} // namespace URK::Unreal
