#pragma once

// Object lookup by name. The remaining calibration steps are anchored on
// well-known engine objects - "Actor", "Class", "Guid" - so they need a way to
// turn a name into an address before any struct offset is known.

#include "unreal_names.h"
#include "unreal_offsets.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {

class ObjectFinder {
  public:
    // Walks every object once and indexes it by name.
    static ObjectFinder Build(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &offsets);

    Address ClassOf(Address object) const;
    Address OuterOf(Address object) const;
    std::optional<std::string> NameOf(Address object) const;

    // First object carrying this name, or kNullAddress.
    Address Find(std::string_view name) const;

    // First object with this name whose outer carries outerName. Outers are not
    // unique by name, so this is how a member is addressed.
    Address FindInOuter(std::string_view name, std::string_view outerName) const;

    // The same, when the outer is already an address rather than a name - a
    // caller holding a class object asking for one of its members, which is
    // the shape the SDK's find_function takes.
    Address FindInOuter(std::string_view name, Address outer) const;

    const MemoryReader &Reader() const { return objects_->Reader(); }
    const ObjectArray &Objects() const { return *objects_; }
    const NameTable &Names() const { return *names_; }
    const ObjectOffsets &Offsets() const { return offsets_; }
    std::size_t IndexedCount() const { return count_; }

  private:
    ObjectFinder(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &offsets)
        : objects_(&objects), names_(&names), offsets_(offsets) {}

    const ObjectArray *objects_;
    const NameTable *names_;
    ObjectOffsets offsets_;
    std::unordered_map<std::string, std::vector<Address>> byName_;
    std::size_t count_ = 0;
};

// Whether the address holds an object the array agrees is there. An object
// knows which slot holds it and the array has to agree, which a pointer into
// the middle of an object, or at an FField, or at a destroyed object, does not.
bool IsLiveObject(const ObjectFinder &finder, Address candidate);

} // namespace URK::Unreal
