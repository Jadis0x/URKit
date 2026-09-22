#pragma once

// Recognising an Unreal process cheaply, before the bootstrap scan.
//
// A shipped title loads no telltale DLL, but UBT stamps the engine branch into
// its version resource ("++UE5+Release-5.8-CL-56702186"). That is a claim, not
// proof, and packers strip it - so the answer is a confidence, with the UBT
// directory layout as the fallback. Proof stays the GUObjectArray/FNamePool
// scan; this only decides whether to pay for it.
//
// Also picks which modules to scan: one image for a game, Core + CoreUObject
// for a modular or editor build.

#include "unreal_memory.h"

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

    bool Known() const { return major > 0; }

    // 4.25 moved properties to FField. A warning, never a source of offsets.
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

// Handles "++UE5+Release-5.8-CL-56702186" and "5.3.2-29314046+++UE5+Release-5.3";
// anything else stays in branch with nothing claimed.
EngineVersion ParseEngineVersion(const std::string &text);

// The caller supplies the module list: enumerating differs in-process vs out.
UnrealPresence DetectUnreal(const MemoryReader &reader, std::span<const ModuleCandidate> modules);

} // namespace URK::Unreal
