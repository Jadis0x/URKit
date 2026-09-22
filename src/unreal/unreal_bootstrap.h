#pragma once

// Finds GUObjectArray and FNamePool by scanning data sections, since a shipped
// game exports neither. Accepted only as a pair: either can be faked alone, but
// the array's objects must read back through the pool as known engine names.

#include "unreal_module.h"
#include "unreal_names.h"
#include "unreal_object_array.h"
#include "unreal_offsets.h"

#include <optional>
#include <span>
#include <vector>

namespace URK::Unreal {

// A calibrated pair plus what was measured through it. The reader must outlive
// it, as it does for the array and the name table held here.
struct Runtime {
    Address objectArrayAddress;
    Address nameTableAddress;
    ObjectArray objects;
    NameTable names;
    ObjectOffsets header;
    // Sampled objects whose names read back as engine identifiers; what the
    // winning pair was chosen by.
    std::int32_t confirmedNames;
};

// Addresses in the regions whose layout fully resolves. Scan order, capped.
std::vector<Address> FindObjectArrayCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions);
std::vector<Address> FindNameTableCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions);

// Pairs the candidates and keeps the pair that reads back the most names.
std::optional<Runtime> BootstrapRuntime(const MemoryReader &reader, std::span<const ScanRegion> regions);

// As above, over the data sections of one loaded module.
std::optional<Runtime> BootstrapModule(const MemoryReader &reader, Address moduleBase);

} // namespace URK::Unreal
