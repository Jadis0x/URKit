#pragma once

// Module section enumeration, read through the same MemoryReader as the rest of
// the calibration. The bootstrap needs to know where a module keeps its globals
// before it can look for any; the mapped PE headers already say, so nothing here
// is asked of the loader and the enumeration can be run against a test image.

#include "unreal_memory.h"

#include <cstdint>
#include <string>
#include <vector>

namespace URK::Unreal {

struct ModuleSection {
    std::string name;
    Address start = kNullAddress;
    std::uint64_t size = 0;
    std::uint32_t characteristics = 0;

    bool Readable() const;
    bool Executable() const;
    bool Writable() const;
    bool HoldsData() const;
};

// A span of mapped memory to search. Sections are the only source today.
struct ScanRegion {
    Address start = kNullAddress;
    std::uint64_t size = 0;
};

// Empty unless the headers are a plausible 64-bit image.
std::vector<ModuleSection> ReadModuleSections(const MemoryReader &reader, Address moduleBase);

// The sections engine globals can live in: readable data, never code, and never
// the link-support sections that hold nothing a scan wants.
std::vector<ScanRegion> ModuleDataRegions(const MemoryReader &reader, Address moduleBase);

} // namespace URK::Unreal
