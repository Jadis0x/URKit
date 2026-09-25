#pragma once

// Type tests over the Super chain, plus CDO lookup.

#include "unreal_object_finder.h"
#include "unreal_struct_offsets.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace URK::Unreal {

// What the engine prefixes a class's default object with.
inline constexpr const char *kDefaultObjectPrefix = "Default__";

struct ClassOffsets {
    std::int32_t classDefaultObject = kOffsetNotFound;
    // UClass::Interfaces: TArray<FImplementedInterface{UClass*, PointerOffset, bImplementedByK2}>.
    std::int32_t interfaces = kOffsetNotFound;

    bool Resolved() const { return classDefaultObject != kOffsetNotFound; }
};

std::int32_t FindClassDefaultObjectOffset(const ObjectFinder &finder, const StructOffsets &structs);
ClassOffsets FindClassOffsets(const ObjectFinder &finder, const StructOffsets &structs);

class TypeQueries {
  public:
    TypeQueries(const ObjectFinder &finder, const StructOffsets &structs, const ClassOffsets &classes)
        : finder_(&finder), structs_(structs), classes_(classes) {}

    Address SuperOf(Address structObject) const;

    // Whether the struct derives from base, or is base itself.
    bool IsChildOf(Address structObject, Address base) const;

    // The same question asked of an instance, through its class.
    bool IsA(Address object, Address classObject) const;

    // Depth of base in the chain, or -1.
    std::int32_t DistanceTo(Address structObject, Address base) const;

    Address DefaultObjectOf(Address classObject) const;

    // CDOs are excluded by default.
    bool IsDefaultObject(Address object) const;

    struct InstanceQuery {
        Address classObject = kNullAddress;
        // Only objects whose class is exactly this one.
        bool exact = false;
        bool includeDefaults = false;
        // Stop after this many; zero means every one of them.
        std::size_t limit = 0;
    };

    std::vector<Address> InstancesOf(const InstanceQuery &query) const;
    std::vector<Address> InstancesOf(Address classObject) const { return InstancesOf(InstanceQuery{classObject}); }

    // Every class deriving from this one, itself excluded.
    std::vector<Address> SubclassesOf(Address classObject) const;

    // Interface address as UObject::GetInterfaceAddress returns it; nothing if not implemented.
    std::optional<Address> InterfaceAddress(Address object, Address interfaceClass) const;

  private:
    // For objects from the array: no range query per read.
    Address TrustedClassOf(Address object) const;

    const ObjectFinder *finder_;
    StructOffsets structs_;
    ClassOffsets classes_;
};

} // namespace URK::Unreal
