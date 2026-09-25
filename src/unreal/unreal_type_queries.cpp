#include "unreal_type_queries.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace URK::Unreal {
namespace {

// Bounds the walk so a corrupt chain can't loop.
constexpr std::int32_t kMaxDepth = 0x40;

// Range for ClassDefaultObject; past UStruct, shipped builds add fields before it.
constexpr std::int32_t kMaxDefaultObjectOffset = 0x300;

// Enough classes to rule out a chance match.
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
                // No CDO yet is not evidence against the offset.
                continue;
            }
            // The CDO must belong to this class and carry its name.
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

namespace {

struct ImplementedInterface {
    Address interfaceClass = kNullAddress;
    std::int32_t pointerOffset = 0;
    bool byK2 = false;
};
constexpr std::int32_t kImplementedInterfaceSize = 16;
constexpr std::int32_t kMaxInterfaces = 64;
constexpr std::int32_t kMaxInterfacesOffset = 0x400;

bool DerivesFrom(const ObjectFinder &finder, const StructOffsets &structs, Address type, Address base) {
    for (std::int32_t depth = 0; type != kNullAddress && depth < kMaxDepth; ++depth) {
        if (type == base)
            return true;
        type = finder.Reader().ReadPointer(type + structs.superStruct).value_or(kNullAddress);
    }
    return false;
}

// The TArray at offset read as interfaces; nothing when any entry is not one.
std::optional<std::vector<ImplementedInterface>> ReadInterfaces(const ObjectFinder &finder,
                                                                const StructOffsets &structs, Address classObject,
                                                                std::int32_t offset, Address interfaceBase) {
    const MemoryReader &reader = finder.Reader();
    const std::optional<Address> data = reader.ReadPointer(classObject + offset);
    const std::optional<std::int32_t> num = reader.ReadInt32(classObject + offset + 8);
    const std::optional<std::int32_t> max = reader.ReadInt32(classObject + offset + 12);
    if (!data || !num || !max || *num < 0 || *num > kMaxInterfaces || *max < *num || (*num > 0 && *data == kNullAddress))
        return std::nullopt;
    std::vector<ImplementedInterface> found;
    for (std::int32_t i = 0; i < *num; ++i) {
        const Address at = *data + static_cast<Address>(i) * kImplementedInterfaceSize;
        const std::optional<Address> type = reader.ReadPointer(at);
        const std::optional<std::int32_t> pointerOffset = reader.ReadInt32(at + 8);
        const std::optional<std::uint8_t> byK2 = reader.ReadAs<std::uint8_t>(at + 12);
        if (!type || !pointerOffset || !byK2 || *byK2 > 1 || *pointerOffset < 0 || *pointerOffset > 0x100000 ||
            *type == interfaceBase || !ObjectIs(finder, structs, *type, kCastFlagClass) ||
            !DerivesFrom(finder, structs, *type, interfaceBase))
            return std::nullopt;
        found.push_back({*type, *pointerOffset, *byK2 != 0});
    }
    return found;
}

// Classes that implement a native interface natively in every build.
std::int32_t FindInterfacesOffset(const ObjectFinder &finder, const StructOffsets &structs) {
    const Address interfaceBase = finder.Find("Interface");
    if (structs.superStruct == kOffsetNotFound || interfaceBase == kNullAddress ||
        !ObjectIs(finder, structs, interfaceBase, kCastFlagClass))
        return kOffsetNotFound;
    std::vector<Address> samples;
    for (const char *name : {"Pawn", "PrimitiveComponent", "ActorComponent", "PlayerController", "Actor", "Object"}) {
        const Address type = finder.Find(name);
        if (type != kNullAddress && ObjectIs(finder, structs, type, kCastFlagClass))
            samples.push_back(type);
    }
    std::int32_t found = kOffsetNotFound;
    for (std::int32_t offset = structs.superStruct + 8; offset <= kMaxInterfacesOffset; offset += 8) {
        std::size_t implemented = 0;
        bool consistent = true;
        for (const Address type : samples) {
            const auto interfaces = ReadInterfaces(finder, structs, type, offset, interfaceBase);
            if (!interfaces) {
                consistent = false;
                break;
            }
            for (const ImplementedInterface &entry : *interfaces)
                implemented += !entry.byK2 && entry.pointerOffset > 0 ? 1 : 0;
        }
        if (!consistent || implemented < 2)
            continue;
        if (found != kOffsetNotFound)
            return kOffsetNotFound;
        found = offset;
    }
    return found;
}

} // namespace

ClassOffsets FindClassOffsets(const ObjectFinder &finder, const StructOffsets &structs) {
    ClassOffsets offsets;
    offsets.classDefaultObject = FindClassDefaultObjectOffset(finder, structs);
    offsets.interfaces = FindInterfacesOffset(finder, structs);
    return offsets;
}

std::optional<Address> TypeQueries::InterfaceAddress(Address object, Address interfaceClass) const {
    const Address interfaceBase = finder_->Find("Interface");
    if (object == kNullAddress || interfaceClass == kNullAddress || classes_.interfaces == kOffsetNotFound ||
        !IsChildOf(interfaceClass, interfaceBase))
        return std::nullopt;
    // UInterface itself: any object, no address (GetInterfaceAddress returns null).
    if (interfaceClass == interfaceBase)
        return kNullAddress;
    // A Blueprint interface lives in a game package; the address is the object's.
    Address package = interfaceClass;
    for (std::int32_t depth = 0; depth < kMaxDepth; ++depth) {
        const Address outer = finder_->OuterOf(package);
        if (outer == kNullAddress)
            break;
        package = outer;
    }
    const std::optional<std::string> packageName = finder_->NameOf(package);
    const bool native = packageName && packageName->rfind("/Script/", 0) == 0;
    bool implemented = false;
    for (Address type = finder_->ClassOf(object); type != kNullAddress; type = SuperOf(type)) {
        const auto interfaces = ReadInterfaces(*finder_, structs_, type, classes_.interfaces, interfaceBase);
        if (!interfaces)
            return std::nullopt;
        for (const ImplementedInterface &entry : *interfaces) {
            if (!IsChildOf(entry.interfaceClass, interfaceClass))
                continue;
            if (!native)
                return object;
            if (!entry.byK2)
                return object + static_cast<Address>(entry.pointerOffset);
            implemented = true;
        }
    }
    if (implemented)
        return kNullAddress;
    return std::nullopt;
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

Address TypeQueries::TrustedClassOf(Address object) const {
    const std::int32_t offset = finder_->Offsets().classPointer;
    Address classObject = kNullAddress;
    if (object == kNullAddress || offset == kOffsetNotFound ||
        !finder_->Reader().ReadTrusted(object + offset, &classObject, sizeof(classObject)))
        return kNullAddress;
    return classObject;
}

// One pass; each class is judged once, since a game has far fewer classes than objects.
std::vector<Address> TypeQueries::InstancesOf(const InstanceQuery &query) const {
    std::vector<Address> found;
    if (query.classObject == kNullAddress)
        return found;

    struct Verdict {
        bool matches = false;
        Address defaultObject = kNullAddress;
    };
    std::unordered_map<Address, Verdict> verdicts;
    finder_->Objects().ForEach([&](std::int32_t, Address object) {
        const Address classObject = TrustedClassOf(object);
        if (classObject == kNullAddress)
            return true;
        const auto [entry, added] = verdicts.try_emplace(classObject);
        Verdict &verdict = entry->second;
        if (added) {
            verdict.matches =
                query.exact ? classObject == query.classObject : IsChildOf(classObject, query.classObject);
            if (verdict.matches && !query.includeDefaults)
                verdict.defaultObject = DefaultObjectOf(classObject);
        }
        if (!verdict.matches || (!query.includeDefaults && object == verdict.defaultObject))
            return true;
        found.push_back(object);
        return query.limit == 0 || found.size() < query.limit;
    });
    return found;
}

std::vector<Address> TypeQueries::SubclassesOf(Address classObject) const {
    std::vector<Address> found;
    if (classObject == kNullAddress || structs_.castFlags == kOffsetNotFound)
        return found;

    // Keyed by the object's own class: is it a class-of-classes?
    std::unordered_map<Address, bool> metaclasses;
    const MemoryReader &reader = finder_->Reader();
    finder_->Objects().ForEach([&](std::int32_t, Address object) {
        if (object == classObject)
            return true;
        const Address meta = TrustedClassOf(object);
        if (meta == kNullAddress)
            return true;
        const auto [entry, added] = metaclasses.try_emplace(meta);
        if (added) {
            const std::optional<std::uint64_t> flags = reader.ReadAs<std::uint64_t>(meta + structs_.castFlags);
            entry->second = flags && (*flags & kCastFlagClass) == kCastFlagClass;
        }
        if (entry->second && IsChildOf(object, classObject))
            found.push_back(object);
        return true;
    });
    return found;
}

} // namespace URK::Unreal
