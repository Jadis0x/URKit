#pragma once

// Name-to-address lookup; later rungs anchor on well-known engine objects.

#include "unreal_names.h"
#include "unreal_offsets.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {

class ObjectFinder {
  public:
    // Walks every object once and indexes it by name.
    static ObjectFinder Build(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &offsets);

    // Hits are rechecked; a miss rebuilds the index at most this often.
    static constexpr std::uint64_t kRefreshIntervalMs = 250;

    Address ClassOf(Address object) const;
    Address OuterOf(Address object) const;
    std::optional<std::string> NameOf(Address object) const;

    // First live object carrying this name, or kNullAddress.
    Address Find(std::string_view name) const;

    // By name and outer name; outers aren't unique either.
    Address FindInOuter(std::string_view name, std::string_view outerName) const;

    // As above, with the outer given as an address (the SDK's find_function).
    Address FindInOuter(std::string_view name, Address outer) const;

    const MemoryReader &Reader() const { return objects_->Reader(); }
    const ObjectArray &Objects() const { return *objects_; }
    const NameTable &Names() const { return *names_; }
    const ObjectOffsets &Offsets() const { return offsets_; }
    std::size_t IndexedCount() const;

  private:
    ObjectFinder(const ObjectArray &objects, const NameTable &names, const ObjectOffsets &offsets)
        : objects_(&objects), names_(&names), offsets_(offsets) {}

    struct Index {
        std::mutex mutex;
        std::unordered_map<std::string, std::vector<Address>> byName;
        std::size_t count = 0;
        std::uint64_t builtAtMs = 0;
    };

    // FName entries never change, so a decoded name is good for the process.
    struct NameCache {
        std::shared_mutex mutex;
        std::unordered_map<std::uint32_t, std::string> names;
    };

    void Rebuild(Index &index) const;
    std::optional<std::string> BaseName(std::uint32_t comparisonIndex) const;
    template <typename Accept> Address Lookup(std::string_view name, Accept accept) const;

    const ObjectArray *objects_;
    const NameTable *names_;
    ObjectOffsets offsets_;
    std::unique_ptr<Index> index_ = std::make_unique<Index>();
    std::unique_ptr<NameCache> nameCache_ = std::make_unique<NameCache>();
};

// Whether the array agrees an object lives here: its slot must point back at it.
bool IsLiveObject(const ObjectFinder &finder, Address candidate);

} // namespace URK::Unreal
