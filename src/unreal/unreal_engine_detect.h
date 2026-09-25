#pragma once

// Cheap Unreal check (version resource, else UBT layout); also picks modules to scan.

#include "unreal_memory.h"
#include "unreal_module.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace URK::Unreal {

struct EngineVersion {
    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::int32_t patch = 0;
    std::uint64_t changelist = 0;
    // Verbatim, for logs and for branches this cannot parse.
    std::string branch;
    // Where the numbers came from, for logs.
    std::string source;

    bool Known() const { return major > 0; }

    // 4.25 moved properties to FField; it is also URKit's floor.
    bool UsesFieldProperties() const { return major > 4 || (major == 4 && minor >= 25); }
};

enum class UnrealLayout {
    Unknown,
    // One image with the engine linked in: a shipped game.
    Monolithic,
    // Engine split across DLLs: an editor build, or a game built modular.
    Modular,
};

enum class DetectionConfidence {
    None,
    // UBT's directory layout, but no version resource - a packed title.
    Possible,
    // Engine branch string, or Core/CoreUObject loaded.
    Confirmed,
};

struct ModuleCandidate {
    // As the loader spells it, with extension. Case-insensitive.
    std::string name;
    // Worth passing: the UBT layout survives a stripped version resource.
    std::string path;
    Address base = kNullAddress;
};

struct UnrealPresence {
    DetectionConfidence confidence = DetectionConfidence::None;
    UnrealLayout layout = UnrealLayout::Unknown;
    EngineVersion version;

    // Data sections to scan, in order. Modular builds need more than one.
    std::vector<Address> runtimeModules;

    std::string reason;

    // A maybe counts: packed titles look like nothing and often are Unreal.
    bool WorthScanning() const { return confidence != DetectionConfidence::None; }
    bool Confirmed() const { return confidence == DetectionConfidence::Confirmed; }
};

// Version resource of a mapped image; empty when it carries none.
std::string ReadModuleVersionString(const MemoryReader &reader, Address moduleBase);

// Parses "++UE5+Release-5.8-CL-..." and "5.3.2-...+++UE5+Release-5.3"; else nothing claimed.
EngineVersion ParseEngineVersion(const std::string &text);

// From FEngineVersion constructor stores. X.0 is never claimed (too common).
EngineVersion FindVersionInCode(const MemoryReader &reader, std::span<const ScanRegion> code, InstructionLength length);

// "<Target>-Core/CoreUObject/Engine.dll": the modules a modular build's reflection lives in.
bool IsEngineCoreModule(const std::string &name);

// The caller supplies the module list: enumerating differs in-process vs out.
UnrealPresence DetectUnreal(const MemoryReader &reader, std::span<const ModuleCandidate> modules);

} // namespace URK::Unreal
