#pragma once

// Live-process bootstrap. Every step above this file starts from a known
// GUObjectArray or FNamePool address, and a shipped game exports neither, so
// both are found by scanning the module's data sections.
//
// Neither global is accepted on its own: a structure can satisfy the array's
// invariants and still be a stale copy, and a block table can look like a name
// pool. They are accepted as a pair, once the objects the array reports read
// back through the pool as names the engine is known to register.

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
