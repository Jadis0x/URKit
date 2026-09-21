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

    const MemoryReader &Reader() const { return objects_->Reader(); }
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

} // namespace URK::Unreal
