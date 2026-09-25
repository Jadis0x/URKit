#pragma once

// Finds GUObjectArray and FNamePool from code anchors or a data scan.
// Accepted only as a pair whose objects name themselves through the pool.

#include "unreal_code_anchors.h"
#include "unreal_module.h"
#include "unreal_names.h"
#include "unreal_object_array.h"
#include "unreal_offsets.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace URK::Unreal {

// A calibrated pair; the reader must outlive it.
struct Runtime {
    Address objectArrayAddress;
    Address nameTableAddress;
    ObjectArray objects;
    NameTable names;
    ObjectOffsets header;
    // Sampled objects whose names resolved; the pair's score.
    std::int32_t confirmedNames;
};

// Addresses in the regions whose layout fully resolves. Scan order, capped.
std::vector<Address> FindObjectArrayCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions);
std::vector<Address> FindNameTableCandidates(const MemoryReader &reader, std::span<const ScanRegion> regions);

// Pairs the candidates and keeps the pair that reads back the most names.
std::optional<Runtime> PairCandidates(const MemoryReader &reader, std::span<const Address> arrays,
                                      std::span<const Address> tables);

// Scans the regions for candidates, then pairs them.
std::optional<Runtime> BootstrapRuntime(const MemoryReader &reader, std::span<const ScanRegion> regions);

// Pairs candidates found from code; cheap enough to poll until the engine is up.
std::optional<Runtime> BootstrapAnchored(const MemoryReader &reader, const GlobalCandidates &candidates);

// Why the candidates make no pair, one finding per line.
std::vector<std::string> ExplainNoPair(const MemoryReader &reader, std::span<const Address> arrays,
                                       std::span<const Address> tables);

// As above, over the data sections of one loaded module.
std::optional<Runtime> BootstrapModule(const MemoryReader &reader, Address moduleBase);

} // namespace URK::Unreal
