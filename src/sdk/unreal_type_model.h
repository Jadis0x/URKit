#pragma once

// The type dump as the generator reads it; shared by the header and pseudo-code writers.

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace UnrealTypeCodegen {

struct Shape {
    std::string kind;
    int size = 0;
    int dim = 1;
    std::uint64_t flags = 0;
    // The object the type is named by: class, struct, UEnum, delegate signature.
    std::string inner;
    std::string innerPackage;
    // An array's or set's element, a map's key and value.
    std::vector<Shape> elements;
};

// A class property, a function parameter, or a struct field (with layout).
struct Member {
    std::string name;
    Shape shape;
    int offset = -1;
    int boolByte = 0;
    int boolMask = 0;
    int fieldMask = 0xFF;
};

struct Function {
    std::string name;
    std::uint32_t flags = 0;
    std::vector<Member> parameters;
    // Delegate signatures only: the package the members name it by.
    std::string package;
    // Format 7: Blueprint locals and bytecode statements (offset, expression), and where decoding stopped.
    std::vector<Member> locals;
    std::vector<std::pair<int, std::string>> script;
    int failedAt = -1;
    std::string failure;
    // Format 7: a native function's entry point as an offset into the game's image; 0 unknown.
    std::uint64_t nativeRva = 0;
};

struct Type {
    bool isStruct = false;
    bool isEnum = false;
    // Enum value names and numbers at dump time; code looks the numbers up in the game.
    std::vector<std::pair<std::string, std::string>> values;
    // Format 6: EClassFlags, and the interfaces the class itself declares.
    std::optional<std::uint32_t> classFlags;
    std::vector<std::string> interfaces;
    std::string name;
    std::string package;
    std::string superName;
    std::string superPackage;
    int size = 0;
    int alignment = 0;
    std::vector<Member> members;
    std::vector<Function> functions;
    // Signatures of the class's delegate members (format 4).
    std::vector<Function> signatures;
    // Format 5: default-object values that differ from the parent ("path", "value").
    std::vector<std::pair<std::string, std::string>> defaults;
    // Format 5: component templates, "component (class)" then its changed members.
    std::vector<std::pair<std::string, std::vector<std::pair<std::string, std::string>>>> components;
    std::string ident;
    // Under types/, mirroring the package: "Engine", "Game/Blueprints/Player".
    std::string folder;
};

// Keyed "package<TAB>name".
using TypeMap = std::map<std::string, Type>;

// Format 7: engine globals by name, as offsets into the game's image.
using Globals = std::vector<std::pair<std::string, std::uint64_t>>;

// A file of the re/ folder: path under it, contents (may be binary).
struct ExportFile {
    std::string name;
    std::string contents;
};

// mappings.usmap, a C header of the layouts, and IDA and Ghidra scripts naming natives and globals.
std::vector<ExportFile> ReverseEngineeringFiles(const TypeMap &types, const Globals &globals,
                                                const std::string &project);

inline std::string Key(const std::string &package, const std::string &name) { return package + '\t' + name; }

// Pseudo-code for a class's Blueprint bytecode; empty when none of its functions has any.
std::string RenderBlueprint(const Type &entry, const TypeMap &types);

} // namespace UnrealTypeCodegen
