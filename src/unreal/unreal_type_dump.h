#pragma once

// Reflected classes written out for the SDK generator: names, kinds and
// signatures only. Offsets never leave the process; mods resolve them live.

#include "unreal_enums.h"
#include "unreal_functions.h"
#include "unreal_type_queries.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>

namespace URK::Unreal {

// Format the generator reads. Bump on any change to the line layout.
inline constexpr int kTypeDumpVersion = 3;
inline constexpr const char *kTypeDumpMagic = "URKIT-UNREAL-TYPES";

// One block per class, keyed "package<TAB>name", so dumps taken on different
// maps merge: a Blueprint class exists only while its map is loaded.
using TypeDumpBlocks = std::map<std::string, std::string>;

// Structs carry layouts (verified at runtime), classes do not. Game thread.
TypeDumpBlocks DumpClasses(const ObjectFinder &finder, const StructOffsets &structs, const PropertyChain &chain,
                           const PropertyValues &values, const FunctionOffsets &functions, const TypeQueries &types,
                           const EnumNames *enums = nullptr);

// Identifies the game build; a dump of another build is replaced, not merged.
struct TypeDumpImage {
    std::uint32_t timeDateStamp = 0;
    std::uint32_t sizeOfImage = 0;
    std::string engine;
};

// Merges blocks into the file at path and writes it whole. Returns how many
// classes the file gained, or -1 when it could not be written.
int WriteTypeDump(const std::string &path, const TypeDumpImage &image, const TypeDumpBlocks &blocks);

} // namespace URK::Unreal
