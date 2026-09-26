#pragma once

// PE section enumeration through MemoryReader.

#include "unreal/memory/unreal_memory.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
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
// Length of the instruction at code (Zydis in the loader); 0 when undecodable.
using InstructionLength = std::size_t (*)(const std::uint8_t *code, std::size_t available);

struct ScanRegion {
    Address start = kNullAddress;
    std::uint64_t size = 0;
    // Runtime-constructed globals must live in writable memory.
    bool writable = false;
};

// Empty unless the headers are a plausible 64-bit image.
std::vector<ModuleSection> ReadModuleSections(const MemoryReader &reader, Address moduleBase);

// Readable data sections, excluding code and link-support sections.
std::vector<ScanRegion> ModuleDataRegions(const MemoryReader &reader, Address moduleBase);

// Executable sections; tells native function pointers apart.
std::vector<ScanRegion> ModuleCodeRegions(const MemoryReader &reader, Address moduleBase);

// Read-only data: where string literals live.
std::vector<ScanRegion> ModuleConstantRegions(const MemoryReader &reader, Address moduleBase);

// Writable data: where engine globals live.
std::vector<ScanRegion> ModuleWritableRegions(const MemoryReader &reader, Address moduleBase);

// A named export's address, or kNullAddress. Modular (editor) builds export their globals.
Address FindModuleExport(const MemoryReader &reader, Address moduleBase, std::string_view name);

struct FunctionRange {
    Address begin = kNullAddress;
    Address end = kNullAddress;
};

// Function bounds from .pdata; read on demand.
class FunctionTable {
  public:
    static FunctionTable Read(const MemoryReader &reader, std::span<const Address> modules);

    bool Empty() const { return modules_.empty(); }

    // The entry holding address; a function split by the compiler has several.
    std::optional<FunctionRange> Containing(Address address) const;

    // Start of the function address belongs to, following chained entries.
    Address PrimaryBegin(Address address) const;

    // That function's primary entry and the chained ones laid out right after it.
    std::vector<FunctionRange> Pieces(Address address) const;

    // First entry after address; a leaf function ends there at the latest.
    Address NextBegin(Address address) const;

  private:
    struct Module {
        Address base = kNullAddress;
        Address imageEnd = kNullAddress;
        Address table = kNullAddress;
        std::uint32_t count = 0;
    };

    struct Entry {
        std::uint32_t begin;
        std::uint32_t end;
        std::uint32_t unwind;
    };

    const Module *ModuleOf(Address address) const;
    std::optional<Entry> EntryAt(const Module &module, std::uint32_t index) const;
    std::optional<Entry> Lookup(const Module &module, Address address) const;
    std::optional<std::uint32_t> LookupIndex(const Module &module, Address address) const;
    std::optional<std::uint32_t> PrimaryRva(const Module &module, std::optional<Entry> entry) const;

    const MemoryReader *reader_ = nullptr;
    std::vector<Module> modules_;
};

} // namespace URK::Unreal
