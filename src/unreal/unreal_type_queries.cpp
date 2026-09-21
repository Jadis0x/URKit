#include "unreal_type_queries.h"

#include <string>

namespace URK::Unreal {
namespace {

// Deep enough for any engine hierarchy, shallow enough that a corrupted chain
// cannot become a loop.
constexpr std::int32_t kMaxDepth = 0x40;

// Where a class can keep its default object. UClass declares it past everything
// UStruct does, and shipped builds add members ahead of it rather than after.
constexpr std::int32_t kMaxDefaultObjectOffset = 0x300;

// Enough classes that an offset satisfying all of them by chance is not worth
// considering.
constexpr std::size_t kClassSamples = 8;
constexpr std::int32_t kMaxObjectsWalked = 0x4000;

bool NamedAfterClass(const ObjectFinder &finder, Address defaultObject, const std::string &className) {
    const std::optional<std::string> name = finder.NameOf(defaultObject);
    return name && *name == kDefaultObjectPrefix + className;
}

} // namespace

std::int32_t FindClassDefaultObjectOffset(const ObjectFinder &finder, const StructOffsets &structs) {
    if (structs.castFlags == kOffsetNotFound)
        return kOffsetNotFound;

    const MemoryReader &reader = finder.Reader();
    const ObjectArray &objects = finder.Objects();

    struct Sample {
        Address classObject = kNullAddress;
        std::string name;
    };

    std::vector<Sample> samples;
    const std::int32_t total = objects.Num();
    const std::int32_t walked = total < kMaxObjectsWalked ? total : kMaxObjectsWalked;
    for (std::int32_t index = 0; index < walked && samples.size() < kClassSamples; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;
        if (!ObjectIs(finder, structs, object, kCastFlagClass))
            continue;
        const std::optional<std::string> name = finder.NameOf(object);
        if (!name || name->empty())
            continue;
        samples.push_back(Sample{object, *name});
    }

    if (samples.empty())
        return kOffsetNotFound;

    const std::int32_t start = FirstOffsetPastHeader(finder.Offsets());
    for (std::int32_t offset = start; offset <= kMaxDefaultObjectOffset;
         offset += static_cast<std::int32_t>(sizeof(Address))) {
        bool satisfied = true;
        std::size_t confirmed = 0;

        for (const Sample &sample : samples) {
            const std::optional<Address> defaultObject = reader.ReadPointer(sample.classObject + offset);
            if (!defaultObject || *defaultObject == kNullAddress) {
                // A class whose default object has not been created yet is not
                // evidence against the offset.
                continue;
            }
            // The two things only the real field satisfies: what it points at
            // belongs to this very class, and carries its name.
            if (finder.ClassOf(*defaultObject) != sample.classObject ||
                !NamedAfterClass(finder, *defaultObject, sample.name)) {
                satisfied = false;
                break;
            }
            ++confirmed;
        }

        if (satisfied && confirmed >= 2)
            return offset;
    }

    return kOffsetNotFound;
}

ClassOffsets FindClassOffsets(const ObjectFinder &finder, const StructOffsets &structs) {
    ClassOffsets offsets;
    offsets.classDefaultObject = FindClassDefaultObjectOffset(finder, structs);
    return offsets;
}

Address TypeQueries::SuperOf(Address structObject) const {
    if (structObject == kNullAddress || structs_.superStruct == kOffsetNotFound)
        return kNullAddress;
    const std::optional<Address> super = finder_->Reader().ReadPointer(structObject + structs_.superStruct);
    return super ? *super : kNullAddress;
}

std::int32_t TypeQueries::DistanceTo(Address structObject, Address base) const {
    if (structObject == kNullAddress || base == kNullAddress)
        return -1;

    Address current = structObject;
    for (std::int32_t depth = 0; depth < kMaxDepth && current != kNullAddress; ++depth) {
        if (current == base)
            return depth;
        const Address super = SuperOf(current);
        if (super == current)
            break;
        current = super;
    }
    return -1;
}

bool TypeQueries::IsChildOf(Address structObject, Address base) const {
    return DistanceTo(structObject, base) >= 0;
}

bool TypeQueries::IsA(Address object, Address classObject) const {
    return IsChildOf(finder_->ClassOf(object), classObject);
}

Address TypeQueries::DefaultObjectOf(Address classObject) const {
    if (classObject == kNullAddress || !classes_.Resolved())
        return kNullAddress;
    const std::optional<Address> defaultObject =
        finder_->Reader().ReadPointer(classObject + classes_.classDefaultObject);
    return defaultObject ? *defaultObject : kNullAddress;
}

bool TypeQueries::IsDefaultObject(Address object) const {
    if (object == kNullAddress)
        return false;
    return DefaultObjectOf(finder_->ClassOf(object)) == object;
}

std::vector<Address> TypeQueries::InstancesOf(const InstanceQuery &query) const {
    std::vector<Address> found;
    if (query.classObject == kNullAddress)
        return found;

    const ObjectArray &objects = finder_->Objects();
    const std::int32_t total = objects.Num();
    for (std::int32_t index = 0; index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;

        const Address classObject = finder_->ClassOf(object);
        if (classObject == kNullAddress)
            continue;
        if (query.exact ? classObject != query.classObject : !IsChildOf(classObject, query.classObject))
            continue;
        if (!query.includeDefaults && IsDefaultObject(object))
            continue;

        found.push_back(object);
        if (query.limit != 0 && found.size() >= query.limit)
            break;
    }

    return found;
}

std::vector<Address> TypeQueries::SubclassesOf(Address classObject) const {
    std::vector<Address> found;
    if (classObject == kNullAddress || structs_.castFlags == kOffsetNotFound)
        return found;

    const ObjectArray &objects = finder_->Objects();
    const std::int32_t total = objects.Num();

    for (std::int32_t index = 0; index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress || object == classObject)
            continue;
        if (!ObjectIs(*finder_, structs_, object, kCastFlagClass))
            continue;
        if (IsChildOf(object, classObject))
            found.push_back(object);
    }

    return found;
}

} // namespace URK::Unreal
