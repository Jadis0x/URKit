#include "unreal_type_codegen.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace UnrealTypeCodegen {
namespace {

// Must match src/unreal/unreal_type_dump.h.
constexpr const char *kMagic = "URKIT-UNREAL-TYPES";
constexpr int kVersion = 4;

// Engine flag values (EPropertyFlags, EFunctionFlags).
constexpr std::uint64_t kConstParm = 0x2;
constexpr std::uint64_t kOutParm = 0x100;
constexpr std::uint64_t kReturnParm = 0x400;
constexpr std::uint32_t kFunctionStatic = 0x2000;
constexpr std::uint32_t kFunctionDelegate = 0x00100000;
constexpr std::uint64_t kPropertyBlueprintVisible = 0x4;

constexpr const char *kRuntime = "::URK::unreal::";
constexpr const char *kTypes = "::URK::unreal::types::";

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
};

struct Type {
    bool isStruct = false;
    bool isEnum = false;
    // Enum value names; their numbers are looked up in the game at runtime.
    std::vector<std::string> values;
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
    std::string ident;
    // Under types/, mirroring the package: "Engine", "Game/Blueprints/Player".
    std::string folder;
};

using TypeMap = std::map<std::string, Type>;

// Blueprint compiler output: anim graph nodes, the ubergraph frame, exposed-input thunks.
bool CompilerMember(const Type &owner, const Member &member) {
    return owner.package.rfind("/Script/", 0) != 0 && !(member.shape.flags & kPropertyBlueprintVisible);
}
bool CompilerFunction(const Function &function) {
    const std::string &name = function.name;
    const auto starts = [&name](const char *prefix) { return name.rfind(prefix, 0) == 0; };
    const auto ends = [&name](const std::string &suffix) {
        return name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    // Input and bound-event thunks, timeline callbacks, async-load continuations.
    return starts("EvaluateGraphExposedInputs_") || name.find("K2Node_") != std::string::npos ||
           starts("BndEvt__") || starts("OnLoaded_") || ends("__UpdateFunc") || ends("__FinishedFunc") ||
           ends("__EventFunc");
}

std::string Key(const std::string &package, const std::string &name) { return package + '\t' + name; }

std::vector<std::string> Split(const std::string &line) {
    std::vector<std::string> fields(1);
    for (const char ch : line) {
        if (ch == '\t')
            fields.emplace_back();
        else if (ch != '\r')
            fields.back() += ch;
    }
    return fields;
}

// "kind|size|type name|type package".
std::optional<Shape> ParseElement(const std::string &text) {
    std::vector<std::string> parts(1);
    for (const char ch : text) {
        if (ch == '|')
            parts.emplace_back();
        else
            parts.back() += ch;
    }
    if (parts.size() != 4)
        return std::nullopt;
    Shape shape;
    shape.kind = parts[0];
    shape.size = std::stoi(parts[1]);
    shape.inner = parts[2];
    shape.innerPackage = parts[3];
    return shape;
}

// elementsAt: where a line's element shapes begin (they close the line).
std::optional<Shape> ParseShape(const std::vector<std::string> &fields, std::size_t elementsAt) {
    if (fields.size() < 8)
        return std::nullopt;
    try {
        Shape shape;
        shape.kind = fields[2];
        shape.size = std::stoi(fields[3]);
        shape.dim = std::stoi(fields[4]);
        shape.flags = std::stoull(fields[5], nullptr, 16);
        shape.inner = fields[6];
        shape.innerPackage = fields[7];
        for (std::size_t i = elementsAt; i < fields.size(); ++i) {
            const std::optional<Shape> element = ParseElement(fields[i]);
            if (!element)
                return std::nullopt;
            shape.elements.push_back(*element);
        }
        return shape;
    } catch (...) {
        return std::nullopt;
    }
}

bool Parse(const std::filesystem::path &path, TypeMap *types, std::string *error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        *error = "cannot read " + path.string();
        return false;
    }
    std::string line;
    std::getline(in, line);
    const std::vector<std::string> magic = Split(line);
    if (magic.size() != 2 || magic[0] != kMagic) {
        *error = path.string() + " is not a URKit Unreal type dump";
        return false;
    }
    const int format = std::atoi(magic[1].c_str());
    if (format < 1 || format > kVersion) {
        *error = path.string() + " has dump format " + magic[1] + "; this urk-sdk reads formats 1-" +
                 std::to_string(kVersion) + ". Use the urk-sdk that matches the loader.";
        return false;
    }

    Type *current = nullptr;
    Function *function = nullptr;
    std::size_t number = 1;
    while (std::getline(in, line)) {
        ++number;
        const std::vector<std::string> fields = Split(line);
        const std::string &tag = fields[0];
        bool ok = true;
        try {
            if ((tag == "C" && fields.size() >= 5) || (tag == "S" && fields.size() >= 7) ||
                (tag == "E" && fields.size() >= 3)) {
                Type &entry = (*types)[Key(fields[2], fields[1])];
                entry = Type{};
                entry.isStruct = tag == "S";
                entry.isEnum = tag == "E";
                entry.name = fields[1];
                entry.package = fields[2];
                if (!entry.isEnum) {
                    entry.superName = fields[3];
                    entry.superPackage = fields[4];
                }
                if (entry.isStruct) {
                    entry.size = std::stoi(fields[5]);
                    entry.alignment = std::stoi(fields[6]);
                }
                current = &entry;
                function = nullptr;
            } else if (tag == "V" && current && current->isEnum && fields.size() >= 2) {
                current->values.push_back(fields[1]);
            } else if (tag == "P" && current && !current->isStruct && !current->isEnum) {
                const std::optional<Shape> shape = ParseShape(fields, 8);
                ok = shape.has_value();
                if (ok)
                    current->members.push_back({fields[1], *shape});
            } else if (tag == "M" && current && current->isStruct && fields.size() >= 12) {
                const std::optional<Shape> shape = ParseShape(fields, 12);
                ok = shape.has_value();
                if (ok)
                    current->members.push_back({fields[1], *shape, std::stoi(fields[8]), std::stoi(fields[9]),
                                                std::stoi(fields[10]), std::stoi(fields[11])});
            } else if (tag == "F" && current && !current->isStruct && !current->isEnum && fields.size() >= 3) {
                current->functions.push_back(
                    {fields[1], static_cast<std::uint32_t>(std::stoul(fields[2], nullptr, 16)), {}});
                function = &current->functions.back();
            } else if (tag == "G" && current && !current->isStruct && !current->isEnum && fields.size() >= 3) {
                current->signatures.push_back({fields[1], 0, {}, fields[2]});
                function = &current->signatures.back();
            } else if (tag == "A" && function) {
                const std::optional<Shape> shape = ParseShape(fields, 8);
                ok = shape.has_value();
                if (ok)
                    function->parameters.push_back({fields[1], *shape});
            } else if (tag != "IMAGE" && tag != "ENGINE" && !line.empty()) {
                ok = false;
            }
        } catch (...) {
            ok = false;
        }
        if (!ok) {
            *error = path.string() + ":" + std::to_string(number) + ": malformed record";
            return false;
        }
    }
    if (types->empty()) {
        *error = path.string() + " holds no types";
        return false;
    }
    return true;
}

// --- identifiers -----------------------------------------------------------

// C++ keywords, breaking Windows macros and names the runtime already uses.
const std::unordered_set<std::string> &Reserved() {
    static const std::unordered_set<std::string> words = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor", "bool", "break", "case", "catch",
        "char", "char8_t", "char16_t", "char32_t", "class", "compl", "concept", "const", "consteval", "constexpr",
        "constinit", "const_cast", "continue", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
        "do", "double", "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false", "final", "float",
        "for", "friend", "goto", "if", "import", "inline", "int", "long", "module", "mutable", "namespace", "new",
        "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "override", "private", "protected",
        "public", "register", "reinterpret_cast", "requires", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
        "while", "xor", "xor_eq",
        "min", "max", "interface", "small", "near", "far", "IN", "OUT", "OPTIONAL", "DELETE", "ERROR", "TRUE",
        "FALSE", "CONST", "VOID", "TEXT", "PURE", "THIS", "NULL", "errno", "assert", "offsetof",
        "handle", "valid", "name", "klass", "outer", "is_a", "is_child_of", "default_object", "function", "describe",
        "get_int", "get_float", "get_bool", "get_object", "get_name", "get_string", "set_int", "set_float",
        "set_bool", "set_object", "static_class", "cast", "instances", "class_default", "load_class", "kName", "kPackage",
        "kSize", "kFields", "value", "UrkR", "std", "URK"};
    return words;
}

std::string Lower(std::string text) {
    for (char &ch : text)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return text;
}

// ASCII letters, digits, single underscores; never a reserved spelling.
std::string Identifier(const std::string &name) {
    std::string out;
    for (const char ch : name) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        const char next = keep ? ch : '_';
        if (next == '_' && !out.empty() && out.back() == '_')
            continue;
        out += next;
    }
    if (out.empty() || out == "_")
        return "Unnamed";
    if (out.front() == '_' || (out.front() >= '0' && out.front() <= '9'))
        out = "N" + out;
    return out;
}

// Unique within a scope; the urk_ prefix is reserved.
std::string Unique(std::string base, std::set<std::string> &taken, const std::string &avoid) {
    if (Reserved().count(base) || base == avoid || base.rfind("urk_", 0) == 0)
        base += '_';
    std::string candidate = base;
    for (int n = 2; taken.count(candidate); ++n)
        candidate = base + '_' + std::to_string(n);
    taken.insert(candidate);
    return candidate;
}

// A package path segment as a folder name: kept as spelled where Windows allows it.
std::string FolderSegment(const std::string &segment) {
    std::string out;
    for (const char ch : segment) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
                          ch == '_' || ch == '-';
        out += keep ? ch : '_';
    }
    static const std::set<std::string> reserved = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4",
                                                   "lpt1", "lpt2", "lpt3", "lpt4"};
    return reserved.count(Lower(out)) ? out + '_' : out;
}

// "/Script/Engine" is the Engine module; "/Game/Doors/BP_Door" lives in Game/Doors.
std::string FolderOf(const std::string &package) {
    std::vector<std::string> segments;
    std::string segment;
    for (const char ch : package + '/') {
        if (ch != '/') {
            segment += ch;
        } else if (!segment.empty()) {
            segments.push_back(FolderSegment(segment));
            segment.clear();
        }
    }
    if (segments.size() >= 2 && segments[0] == "Script")
        return segments[1];
    if (!segments.empty())
        segments.pop_back();
    std::string folder;
    for (const std::string &part : segments)
        folder += (folder.empty() ? "" : "/") + part;
    return folder.empty() ? "Other" : folder;
}

// One spelling per folder: Windows would merge "Blueprints" and "blueprints" anyway.
void AssignFolders(TypeMap &types) {
    std::map<std::string, std::string> spelling;
    for (auto &[key, entry] : types) {
        std::string folder = FolderOf(entry.package);
        std::string canonical;
        std::size_t start = 0;
        while (start <= folder.size()) {
            const std::size_t slash = std::min(folder.find('/', start), folder.size());
            const std::string prefix = canonical + (canonical.empty() ? "" : "/") + folder.substr(start, slash - start);
            canonical = spelling.try_emplace(Lower(prefix), prefix).first->second;
            start = slash + 1;
        }
        entry.folder = canonical;
    }
}

// Classes and structs share one namespace, and file names stay unique across folders (case-insensitive).
void AssignIdents(TypeMap &types) {
    std::set<std::string> takenLower;
    for (auto &[key, entry] : types) {
        std::string base = Identifier(entry.name);
        if (Reserved().count(base) || takenLower.count(Lower(base))) {
            const std::size_t slash = entry.package.find_last_of('/');
            base += "_" + Identifier(slash == std::string::npos ? entry.package : entry.package.substr(slash + 1));
        }
        std::string candidate = base;
        for (int n = 2; takenLower.count(Lower(candidate)); ++n)
            candidate = base + "_" + std::to_string(n);
        takenLower.insert(Lower(candidate));
        entry.ident = candidate;
    }
}

std::string Escape(const std::string &text) {
    std::string out;
    for (const char ch : text) {
        if (ch == '"' || ch == '\\')
            out += '\\';
        out += ch;
    }
    return out;
}

std::string Wrapped(const std::string &lead, const std::vector<std::string> &items) {
    std::string out;
    std::string line = "    // " + lead;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const std::string item = items[i] + (i + 1 < items.size() ? "," : ".");
        if (line.size() + item.size() + 1 > 116) {
            out += line + '\n';
            line = "    //   ";
        } else if (line.back() != ' ') {
            line += ' ';
        }
        line += item;
    }
    return out + line + '\n';
}

// --- types -------------------------------------------------------------------

// URK_UnrealPropertyKind, by the dump's kind names.
int KindId(const std::string &kind) {
    static const std::map<std::string, int> ids = {
        {"bool", 1},         {"byte", 2},         {"int8", 3},     {"int16", 4},  {"int32", 5},  {"int64", 6},
        {"uint16", 7},       {"uint32", 8},       {"uint64", 9},   {"float", 10}, {"double", 11}, {"enum", 12},
        {"name", 13},        {"string", 14},      {"text", 15},    {"object", 16}, {"class", 17},
        {"weak object", 18}, {"soft object", 19}, {"interface", 20}, {"struct", 21}, {"array", 22},
        {"set", 23},         {"map", 24},         {"delegate", 25}, {"multicast delegate", 26},
        {"sparse delegate", 27}, {"lazy object", 28}, {"utf8 string", 29}, {"ansi string", 30}};
    const auto found = ids.find(kind);
    return found == ids.end() ? 0 : found->second;
}

// Kinds whose bytes a call frame or a struct copies as they are.
std::optional<std::string> ValueType(const Shape &shape) {
    static const std::map<std::string, std::pair<std::string, int>> fixed = {
        {"bool", {"bool", 1}},          {"byte", {"std::uint8_t", 1}},   {"int8", {"std::int8_t", 1}},
        {"int16", {"std::int16_t", 2}}, {"int32", {"std::int32_t", 4}},  {"int64", {"std::int64_t", 8}},
        {"uint16", {"std::uint16_t", 2}}, {"uint32", {"std::uint32_t", 4}}, {"uint64", {"std::uint64_t", 8}},
        {"float", {"float", 4}},        {"double", {"double", 8}}};
    if (shape.kind == "enum") {
        switch (shape.size) {
        case 1:
            return "std::uint8_t";
        case 2:
            return "std::uint16_t";
        case 4:
            return "std::uint32_t";
        case 8:
            return "std::uint64_t";
        default:
            return std::nullopt;
        }
    }
    const auto found = fixed.find(shape.kind);
    if (found == fixed.end() || found->second.second != shape.size)
        return std::nullopt;
    return found->second.first;
}

// A struct mirror as it will be compiled. size is what a member of it occupies.
struct Layout {
    bool done = false;
    bool visiting = false;
    bool usable = false;
    bool nestable = false;
    int size = 0;
    int align = 1;
    std::string members;
    std::string accessors;
    std::vector<std::string> fields;
    std::vector<std::string> offsets;
    std::vector<std::string> opaque;
    std::set<std::string> includes;
    std::set<std::string> forwards;
};

class Generator {
  public:
    explicit Generator(const TypeMap &types) : types_(types) {
        for (const auto &[key, entry] : types)
            byIdent_.emplace(entry.ident, &entry);
    }

    Header Emit(const Type &entry) {
        if (entry.isEnum)
            return EmitEnum(entry);
        return entry.isStruct ? EmitStruct(entry) : EmitClass(entry);
    }

    bool Emittable(const Type &entry) { return entry.isEnum || !entry.isStruct || LayoutOf(entry).usable; }

  private:
    const Type *Find(const std::string &package, const std::string &name) const {
        const auto found = types_.find(Key(package, name));
        return found == types_.end() ? nullptr : &found->second;
    }

    // The generated class an object reference points at, or the untyped wrapper.
    std::string ObjectType(const Shape &shape, std::set<std::string> &forwards) {
        if (shape.kind == "object") {
            if (const Type *target = Find(shape.innerPackage, shape.inner);
                target && !target->isStruct && !target->isEnum) {
                forwards.insert(target->ident);
                return kTypes + target->ident;
            }
        }
        return std::string(kRuntime) + "Object";
    }

    // A struct usable as a whole value of this width, or null.
    const Type *StructValue(const Shape &shape, std::set<std::string> &includes) {
        if (shape.kind != "struct")
            return nullptr;
        const Type *target = Find(shape.innerPackage, shape.inner);
        if (!target || !target->isStruct)
            return nullptr;
        const Layout &layout = LayoutOf(*target);
        if (!layout.usable || layout.size != shape.size)
            return nullptr;
        includes.insert(target->ident);
        return target;
    }

    // Same rule as the loader: unknown kinds can't be released, so calls are refused.
    bool Leaks(const Shape &shape, int depth = 0) {
        if (shape.kind == "unknown" || depth > 16)
            return true;
        for (const Shape &element : shape.elements) {
            if (Leaks(element, depth + 1))
                return true;
        }
        if (shape.kind != "struct")
            return false;
        const Type *target = Find(shape.innerPackage, shape.inner);
        if (!target || !target->isStruct)
            return true;
        std::vector<const Member *> flat;
        Flatten(*target, flat, 0);
        for (const Member *member : flat) {
            if (Leaks(member->shape, depth + 1))
                return true;
        }
        return false;
    }

    // The generated enum a shape names, for enums and bytes that carry one.
    const Type *EnumOf(const Shape &shape, std::set<std::string> &includes) {
        if (shape.kind != "enum" && shape.kind != "byte")
            return nullptr;
        const Type *target = Find(shape.innerPackage, shape.inner);
        if (!target || !target->isEnum)
            return nullptr;
        includes.insert(target->ident);
        return target;
    }

    // C++ type used through a place (Traits<T>). In function bodies object refs are plain Objects.
    std::optional<std::string> PlaceType(const Shape &shape, std::set<std::string> &includes,
                                         std::set<std::string> &forwards, int depth = 0, bool untyped = false) {
        if (depth > 4)
            return std::nullopt;
        if (untyped && (shape.kind == "object" || shape.kind == "weak object" || shape.kind == "lazy object"))
            return std::string(kRuntime) + "Object";
        if (const Type *type = EnumOf(shape, includes))
            return kTypes + type->ident;
        if (shape.kind == "bool")
            return std::string("bool");
        if (const std::optional<std::string> value = ValueType(shape))
            return value;
        if (shape.kind == "name" || shape.kind == "string" || shape.kind == "text" || shape.kind == "utf8 string" ||
            shape.kind == "ansi string")
            return std::string("std::string");
        if (shape.kind == "object" || shape.kind == "weak object" || shape.kind == "lazy object")
            return ObjectTypeNamed(shape, forwards);
        if (shape.kind == "class")
            return std::string(kRuntime) + "Object";
        if (shape.kind == "soft object")
            return std::string(kRuntime) + "SoftPath";
        if (shape.kind == "delegate")
            return std::string(kRuntime) + "Binding";
        if (shape.kind == "struct") {
            if (const Type *value = StructValue(shape, includes))
                return kTypes + value->ident;
            // No mirror to copy it into: reached in place only, as an element.
            if (depth > 0 && !untyped)
                return std::string(kRuntime) + "Opaque";
            return std::nullopt;
        }
        if ((shape.kind == "array" || shape.kind == "set") && shape.elements.size() == 1) {
            if (shape.kind == "set" && !Keyable(shape.elements[0]))
                return std::nullopt;
            const std::optional<std::string> element = PlaceType(shape.elements[0], includes, forwards, depth + 1, untyped);
            return element ? std::optional<std::string>("std::vector<" + *element + ">") : std::nullopt;
        }
        if (shape.kind == "multicast delegate")
            return "std::vector<" + std::string(kRuntime) + "Binding>";
        if (shape.kind == "map" && shape.elements.size() == 2 && Keyable(shape.elements[0])) {
            const std::optional<std::string> key = PlaceType(shape.elements[0], includes, forwards, depth + 1, untyped);
            const std::optional<std::string> value = PlaceType(shape.elements[1], includes, forwards, depth + 1, untyped);
            if (key && value)
                return "std::vector<std::pair<" + *key + ", " + *value + ">>";
        }
        return std::nullopt;
    }

    // What the loader can hash and compare as a set element or map key.
    static bool Keyable(const Shape &shape) {
        static const std::set<std::string> keys = {"bool",   "byte",   "int8",   "int16",  "int32", "int64",
                                                   "uint16", "uint32", "uint64", "float",  "double", "enum",
                                                   "name",   "string", "object", "class",  "struct"};
        return keys.count(shape.kind) != 0;
    }

    // An object reference's generated class; weak and lazy references name it too.
    std::string ObjectTypeNamed(const Shape &shape, std::set<std::string> &forwards) {
        if (const Type *target = Find(shape.innerPackage, shape.inner); target && !target->isStruct && !target->isEnum) {
            forwards.insert(target->ident);
            return kTypes + target->ident;
        }
        return std::string(kRuntime) + "Object";
    }

    // Wrapper type for a member reached through a place, if any.
    std::optional<std::string> PlaceMemberType(const Shape &shape, std::set<std::string> &includes,
                                               std::set<std::string> &forwards) {
        if (const Type *type = EnumOf(shape, includes))
            return std::string(kRuntime) + "EnumMember<" + kTypes + type->ident + ">";
        if (shape.kind == "name")
            return std::string(kRuntime) + "NameValue";
        if (shape.kind == "string" || shape.kind == "utf8 string" || shape.kind == "ansi string")
            return std::string(kRuntime) + "StringValue";
        if (shape.kind == "text")
            return std::string(kRuntime) + "TextValue";
        if (shape.kind == "soft object")
            return std::string(kRuntime) + "SoftMember<" + ObjectTypeNamed(shape, forwards) + ">";
        if (shape.kind == "weak object" || shape.kind == "lazy object")
            return std::string(kRuntime) + "WeakMember<" + ObjectTypeNamed(shape, forwards) + ">";
        if (shape.kind == "delegate")
            return std::string(kRuntime) + "DelegateMember";
        if (shape.kind == "interface")
            return std::string(kRuntime) + "InterfaceMember";
        if (shape.kind == "multicast delegate" || shape.kind == "sparse delegate")
            return std::string(kRuntime) + "MulticastMember";
        if (shape.kind == "array" && shape.elements.size() == 1) {
            const std::optional<std::string> element = PlaceType(shape.elements[0], includes, forwards, 1);
            return element ? std::optional<std::string>(std::string(kRuntime) + "ArrayMember<" + *element + ">")
                           : std::nullopt;
        }
        if (shape.kind == "set" && shape.elements.size() == 1 && Keyable(shape.elements[0])) {
            const std::optional<std::string> element = PlaceType(shape.elements[0], includes, forwards);
            return element ? std::optional<std::string>(std::string(kRuntime) + "SetMember<" + *element + ">")
                           : std::nullopt;
        }
        if (shape.kind == "map" && shape.elements.size() == 2 && Keyable(shape.elements[0])) {
            const std::optional<std::string> key = PlaceType(shape.elements[0], includes, forwards);
            const std::optional<std::string> value = PlaceType(shape.elements[1], includes, forwards, 1);
            if (key && value && key->find("Opaque") == std::string::npos)
                return std::string(kRuntime) + "MapMember<" + *key + ", " + *value + ">";
        }
        return std::nullopt;
    }

    void Flatten(const Type &entry, std::vector<const Member *> &out, int depth) {
        if (depth > 32)
            return;
        if (const Type *super = Find(entry.superPackage, entry.superName); super && super->isStruct)
            Flatten(*super, out, depth + 1);
        for (const Member &member : entry.members)
            out.push_back(&member);
    }

    Layout &LayoutOf(const Type &entry) {
        Layout &layout = layouts_[&entry];
        if (layout.done || layout.visiting)
            return layout;
        layout.visiting = true;
        // A power of two or unknown; a wrong one shows up as a size mismatch at runtime.
        const bool sane = entry.alignment > 0 && entry.alignment <= 256 && (entry.alignment & (entry.alignment - 1)) == 0;
        const int alignment = sane ? entry.alignment : 1;
        layout.size = entry.size > 0 ? (entry.size + alignment - 1) / alignment * alignment : 0;
        layout.align = alignment;

        std::vector<const Member *> flat;
        Flatten(entry, flat, 0);
        std::stable_sort(flat.begin(), flat.end(), [](const Member *a, const Member *b) {
            return a->offset + a->boolByte < b->offset + b->boolByte;
        });

        std::set<std::string> taken;
        std::ostringstream members;
        std::ostringstream accessors;
        int cursor = 0;
        int bitsByte = -1;
        bool ok = layout.size > 0;
        const auto pad = [&](int to) {
            if (to > cursor)
                members << "    std::uint8_t urk_pad_" << cursor << '[' << (to - cursor) << "];\n";
            cursor = std::max(cursor, to);
        };
        for (const Member *member : flat) {
            const Shape &shape = member->shape;
            const bool bitfield = shape.kind == "bool" && member->fieldMask != 0xFF;
            layout.fields.push_back("{\"" + Escape(member->name) + "\", " + std::to_string(member->offset) + ", " +
                                    std::to_string(shape.size) + ", " + std::to_string(shape.dim) + ", " +
                                    std::to_string(KindId(shape.kind)) + ", " + std::to_string(member->boolByte) +
                                    ", " + std::to_string(member->boolMask) + ", " +
                                    std::to_string(member->fieldMask) + "}");
            if (!ok)
                continue;
            if (member->offset < 0 || shape.size <= 0 || shape.dim < 1) {
                ok = false;
                continue;
            }
            if (bitfield) {
                const int byte = member->offset + member->boolByte;
                if (byte != bitsByte) {
                    if (byte < cursor) {
                        ok = false;
                        continue;
                    }
                    pad(byte);
                    members << "    std::uint8_t urk_bits_" << byte << ";\n";
                    cursor = byte + 1;
                    bitsByte = byte;
                }
                const std::string ident = Unique(Identifier(member->name), taken, entry.ident);
                const std::string setter = Unique("set_" + ident, taken, entry.ident);
                const std::string bits = "urk_bits_" + std::to_string(byte);
                const std::string mask = std::to_string(member->boolMask);
                accessors << "    bool " << ident << "() const { return (" << bits << " & " << mask << ") != 0; }\n"
                          << "    void " << setter << "(bool value) {\n        " << bits
                          << " = static_cast<std::uint8_t>(value ? (" << bits << " | " << mask << ") : (" << bits
                          << " & ~" << mask << "));\n    }\n";
                continue;
            }
            if (member->offset < cursor) {
                ok = false;
                continue;
            }

            std::string type = "std::uint8_t";
            int align = 1;
            bool typed = false;
            if (shape.kind == "bool" && shape.size == 1) {
                type = "bool";
                typed = true;
            } else if (const std::optional<std::string> value = ValueType(shape)) {
                type = *value;
                align = shape.size;
                typed = true;
            } else if ((shape.kind == "object" || shape.kind == "class") && shape.size == 8) {
                type = std::string(kRuntime) + "StructObject<" + ObjectType(shape, layout.forwards) + ">";
                align = 8;
                typed = true;
            } else if (const Type *nested = StructValue(shape, layout.includes)) {
                type = kTypes + nested->ident;
                align = LayoutOf(*nested).align;
                typed = true;
            }
            if (typed && member->offset % align != 0) {
                type = "std::uint8_t";
                align = 1;
                typed = false;
            }
            pad(member->offset);
            const std::string name = Unique(Identifier(member->name), taken, entry.ident);
            const std::string ident = typed ? name : "urk_opaque_" + name;
            const int count = typed ? shape.dim : shape.size * shape.dim;
            members << "    " << type << ' ' << ident;
            if (count != 1 || !typed)
                members << '[' << count << ']';
            members << ";\n";
            if (!typed) {
                layout.opaque.push_back(member->name);
                // Kept as bytes, reached in place (strings, arrays, text...).
                if (const std::optional<std::string> access =
                        PlaceMemberType(shape, layout.includes, layout.forwards)) {
                    accessors << "    static " << *access << ' ' << name << "(const " << kRuntime << "Place &self"
                              << (shape.dim > 1 ? ", std::int32_t index" : "") << ") { return self.member(\""
                              << Escape(member->name) << "\"" << (shape.dim > 1 ? ", index" : "") << "); }\n";
                }
            }
            layout.offsets.push_back(ident + ") == " + std::to_string(member->offset));
            cursor = member->offset + shape.size * shape.dim;
            layout.align = std::max(layout.align, align);
        }
        if (ok && cursor > layout.size)
            ok = false;
        if (ok) {
            pad(layout.size);
            layout.members = members.str();
            layout.accessors = accessors.str();
        } else if (layout.size > 0) {
            // Not expressible member by member: the value is still copyable whole.
            layout.members = "    std::uint8_t urk_bytes[" + std::to_string(layout.size) + "];\n";
            layout.accessors.clear();
            layout.offsets.clear();
            layout.opaque = {"all members"};
            layout.includes.clear();
            layout.forwards.clear();
            layout.align = alignment;
        }
        layout.usable = layout.size > 0;
        layout.nestable = layout.usable && layout.size % layout.align == 0;
        layout.visiting = false;
        layout.done = true;
        return layout;
    }

    // Header paths are relative, so they hold wherever the project keeps sdk/.
    static std::string Include(const Type &from, const std::string &path) {
        return std::filesystem::path(path).lexically_relative(from.folder).generic_string();
    }
    static std::string RuntimeInclude(const Type &from) { return Include(from, "../unreal_runtime.h"); }
    std::string IncludeOf(const Type &from, const std::string &ident) const {
        const auto found = byIdent_.find(ident);
        return Include(from, (found != byIdent_.end() ? found->second->folder : from.folder) + "/" + ident + ".h");
    }
    static std::string FileOf(const Type &entry) { return entry.folder + "/" + entry.ident + ".h"; }

    static std::string Preamble(const Type &entry, const std::string &what) {
        return "// Generated by urk-sdk from " + std::string(kDumpFileName) + "; regenerated with it, do not edit.\n" +
               "// " + Escape(entry.name) + " in " + Escape(entry.package) + what + "\n#pragma once\n\n";
    }

    Header EmitStruct(const Type &entry) {
        const Layout &layout = LayoutOf(entry);
        std::ostringstream out;
        out << Preamble(entry, ": a value of " + std::to_string(layout.size) +
                                   " bytes, checked against the running game before any copy.");
        out << "#include \"" << RuntimeInclude(entry) << "\"\n";
        for (const std::string &include : layout.includes)
            out << "#include \"" << IncludeOf(entry, include) << "\"\n";
        out << "\nnamespace URK::unreal::types {\n";
        for (const std::string &forward : layout.forwards)
            out << "class " << forward << ";\n";
        out << "\nstruct ";
        if (layout.align > 1)
            out << "alignas(" << layout.align << ") ";
        out << entry.ident << " {\n"
            << "    static constexpr const char *kName = \"" << Escape(entry.name) << "\";\n"
            << "    static constexpr const char *kPackage = \"" << Escape(entry.package) << "\";\n"
            << "    static constexpr std::int32_t kSize = " << layout.size << ";\n"
            << "    static constexpr std::array<" << kRuntime << "FieldLayout, " << layout.fields.size() << "> kFields{";
        if (!layout.fields.empty()) {
            out << "{\n";
            for (const std::string &field : layout.fields)
                out << "        " << field << ",\n";
            out << "    }";
        }
        out << "};\n\n" << layout.members;
        if (!layout.accessors.empty())
            out << '\n' << layout.accessors;
        if (!layout.opaque.empty())
            out << Wrapped("Kept as bytes; a write must leave them as read: ", layout.opaque);
        out << "};\n";
        out << "static_assert(sizeof(" << entry.ident << ") " << (layout.nestable ? "==" : ">=") << ' ' << entry.ident
            << "::kSize);\n";
        for (const std::string &offset : layout.offsets)
            out << "static_assert(offsetof(" << entry.ident << ", " << offset << ");\n";
        out << "} // namespace URK::unreal::types\n";
        return {FileOf(entry), out.str()};
    }

    // Names only; resolved in the running game, so renumbering needs no rebuild.
    Header EmitEnum(const Type &entry) {
        std::ostringstream out;
        out << Preamble(entry, ": value names only, each looked up in the running game.");
        out << "#include \"" << RuntimeInclude(entry) << "\"\n\nnamespace URK::unreal::types {\n\n"
            << "struct " << entry.ident << " : " << kRuntime << "Enum<" << entry.ident << "> {\n"
            << "    using Enum::Enum;\n"
            << "    static constexpr const char *kName = \"" << Escape(entry.name) << "\";\n"
            << "    static constexpr const char *kPackage = \"" << Escape(entry.package) << "\";\n";
        std::set<std::string> taken = {"Enum", "from_value", "enum_object", "name_literal", "UrkEnumTag"};
        for (const std::string &value : entry.values) {
            const std::string ident = Unique(Identifier(value), taken, entry.ident);
            out << "    static " << entry.ident << ' ' << ident << "() { return " << entry.ident << "(\""
                << Escape(value) << "\"); }\n";
        }
        out << "};\n} // namespace URK::unreal::types\n";
        return {FileOf(entry), out.str()};
    }

    struct ClassContext {
        std::set<std::string> forwards;
        std::set<std::string> includes;
        // Delegate signature key to its generated event view.
        std::map<std::string, std::string> events;
    };

    std::optional<std::string> PropertyType(const Shape &shape, ClassContext &context) {
        if (const auto event = context.events.find(Key(shape.innerPackage, shape.inner));
            event != context.events.end() && (shape.kind == "multicast delegate" || shape.kind == "sparse delegate"))
            return std::string(kRuntime) + "EventMember<" + event->second + ">";
        // Enums (and bytes naming one) are typed by their generated enum first.
        if (EnumOf(shape, context.includes))
            return PlaceMemberType(shape, context.includes, context.forwards);
        if (const std::optional<std::string> value = ValueType(shape))
            return std::string(kRuntime) + "Value<" + *value + ">";
        if (shape.kind == "object" || shape.kind == "class")
            return std::string(kRuntime) + "ObjectMember<" + ObjectType(shape, context.forwards) + ">";
        if (const Type *value = StructValue(shape, context.includes))
            return std::string(kRuntime) + "StructMember<" + kTypes + value->ident + ">";
        return PlaceMemberType(shape, context.includes, context.forwards);
    }

    Header EmitClass(const Type &entry) {
        ClassContext context;
        std::ostringstream body;
        std::ostringstream compiler;
        std::set<std::string> taken;
        std::vector<std::string> skipped;

        // Views of each multicast delegate's broadcast, before the members that name them.
        std::ostringstream events;
        for (const Member &property : entry.members) {
            const Shape &shape = property.shape;
            if (shape.kind != "multicast delegate" && shape.kind != "sparse delegate")
                continue;
            const std::string key = Key(shape.innerPackage, shape.inner);
            const auto signature = std::find_if(entry.signatures.begin(), entry.signatures.end(),
                                                [&](const Function &f) { return Key(f.package, f.name) == key; });
            if (signature == entry.signatures.end() || context.events.count(key))
                continue;
            std::string base = shape.inner;
            if (const std::size_t tail = base.rfind("__DelegateSignature"); tail != std::string::npos)
                base.erase(tail);
            const std::string view = Unique(Identifier(base) + "_Event", taken, entry.ident);
            context.events[key] = view;
            EmitView(entry, view, signature->parameters, context, events);
        }
        for (const Member &property : entry.members) {
            const std::optional<std::string> type = PropertyType(property.shape, context);
            if (!type) {
                skipped.push_back(property.name + " (" + property.shape.kind + ")");
                continue;
            }
            std::ostringstream &to = CompilerMember(entry, property) ? compiler : body;
            const std::string ident = Unique(Identifier(property.name), taken, entry.ident);
            if (property.shape.dim > 1)
                to << "    " << *type << ' ' << ident << "(std::int32_t index) const { return {*this, \""
                   << Escape(property.name) << "\", index}; }\n";
            else
                to << "    " << *type << ' ' << ident << "() const { return {*this, \"" << Escape(property.name)
                   << "\"}; }\n";
        }
        std::vector<const Function *> hooked;
        for (const Function &function : entry.functions) {
            if ((function.flags & kFunctionDelegate) || function.name.rfind("ExecuteUbergraph", 0) == 0)
                continue;
            hooked.push_back(&function);
            if (!EmitFunction(entry, function, taken, context, CompilerFunction(function) ? compiler : body))
                skipped.push_back(function.name + "()");
        }
        // After every function has its name, so a hook never takes one.
        std::ostringstream hooks;
        for (const Function *function : hooked)
            EmitHook(entry, *function, taken, context, CompilerFunction(*function) ? compiler : hooks);

        const Type *super = Find(entry.superPackage, entry.superName);
        if (super && super->isStruct)
            super = nullptr;
        const std::string base = super ? kTypes + super->ident : std::string(kRuntime) + "Object";
        std::ostringstream out;
        out << Preamble(entry, "")
            << "#include \"" << (super ? IncludeOf(entry, super->ident) : RuntimeInclude(entry)) << "\"\n";
        for (const std::string &include : context.includes)
            out << "#include \"" << IncludeOf(entry, include) << "\"\n";
        out << "\nnamespace URK::unreal::types {\n";
        for (const std::string &ident : context.forwards) {
            if (ident != entry.ident && (!super || ident != super->ident))
                out << "class " << ident << ";\n";
        }
        out << "\nclass " << entry.ident << " : public " << base << " {\n  public:\n"
            << "    URK_UNREAL_TYPE(" << entry.ident << ", " << base << ", \"" << Escape(entry.name) << "\", \""
            << Escape(entry.package) << "\")\n";
        if (!events.str().empty())
            out << "\n    // What a broadcast carries: Member().subscribe([](View &event) { ... }).\n" << events.str();
        if (!body.str().empty())
            out << '\n' << body.str();
        if (!hooks.str().empty())
            out << "\n    // hook_<Function>(before, after): the callbacks get a typed view of the call.\n" << hooks.str();
        if (!compiler.str().empty())
            out << "\n    // Blueprint compiler output, not authored in the Blueprint.\n" << compiler.str();
        if (!skipped.empty())
            out << Wrapped("No typed form yet: ", skipped);
        out << "};\n} // namespace URK::unreal::types\n";
        return {FileOf(entry), out.str()};
    }

    // A hooked call's parameter, reached in place; any kind falls back to a raw Place.
    std::string HookParameterType(const Shape &shape, ClassContext &context) {
        if (shape.dim != 1)
            return std::string(kRuntime) + "Place";
        if (EnumOf(shape, context.includes))
            return *PlaceMemberType(shape, context.includes, context.forwards);
        if (const std::optional<std::string> value = ValueType(shape))
            return std::string(kRuntime) + "Member<" + *value + ">";
        if (shape.kind == "bool")
            return std::string(kRuntime) + "Member<bool>";
        if (shape.kind == "object" || shape.kind == "class")
            return std::string(kRuntime) + "ObjectPlace<" + ObjectType(shape, context.forwards) + ">";
        if (const Type *value = StructValue(shape, context.includes))
            return std::string(kRuntime) + "Member<" + kTypes + value->ident + ">";
        return PlaceMemberType(shape, context.includes, context.forwards).value_or(std::string(kRuntime) + "Place");
    }

    // <Function>_Call, a view of one hooked call, and hook_<Function>(before, after).
    void EmitHook(const Type &owner, const Function &function, std::set<std::string> &taken, ClassContext &context,
                  std::ostringstream &body) {
        const std::string base = Identifier(function.name);
        const std::string view = Unique(base + "_Call", taken, owner.ident);
        const std::string hook = Unique("hook_" + base, taken, owner.ident);
        EmitView(owner, view, function.parameters, context, body);
        body << "    URK_UNREAL_HOOK(" << view << ", " << hook << ", \"" << Escape(function.name) << "\")\n";
    }

    // A TypedCall with one accessor per parameter, for a hooked call or a broadcast.
    void EmitView(const Type &owner, const std::string &view, const std::vector<Member> &parameters,
                  ClassContext &context, std::ostringstream &body) {
        // What HookedCall itself names.
        std::set<std::string> names = {"self",     "object",     "function",   "after",     "skipped",   "valid",
                                       "raw",      "set",        "get",        "set_struct", "get_struct", "parameter",
                                       "set_value", "get_value", "TypedCall", "HookedCall", "FrameView"};
        body << "    struct " << view << " : " << kRuntime << "TypedCall<" << owner.ident << "> {"
             << (parameters.empty() ? " " : "\n        ") << "using TypedCall::TypedCall;";
        for (const Member &parameter : parameters) {
            const std::string ident = Unique(Identifier(parameter.name), names, view);
            body << "\n        " << HookParameterType(parameter.shape, context) << ' ' << ident
                 << "() const { return parameter(\"" << Escape(parameter.name) << "\"); }";
        }
        body << (parameters.empty() ? " };\n" : "\n    };\n");
    }

    bool EmitFunction(const Type &owner, const Function &function, std::set<std::string> &taken,
                      ClassContext &context, std::ostringstream &body) {
        const Member *returned = nullptr;
        std::vector<std::string> signature;
        std::vector<std::string> sets;
        std::vector<std::string> outs;
        std::set<std::string> parameterNames;
        for (const Member &parameter : function.parameters) {
            const Shape &shape = parameter.shape;
            if (shape.flags & kReturnParm) {
                returned = &parameter;
                continue;
            }
            if (shape.dim != 1)
                return false;
            const std::string ident = Unique(Identifier(parameter.name), parameterNames, owner.ident);
            const std::string name = "\"" + Escape(parameter.name) + "\"";
            const bool enumerated = EnumOf(shape, context.includes) != nullptr;
            const std::optional<std::string> value = enumerated ? std::nullopt : ValueType(shape);
            const Type *structValue = value ? nullptr : StructValue(shape, context.includes);
            // Kinds that go through a place.
            const bool object = shape.kind == "object" || shape.kind == "class";
            const std::optional<std::string> placed =
                value || structValue || object ? std::nullopt : PlaceType(shape, context.includes, context.forwards, 0, true);
            if ((shape.flags & kOutParm) && !(shape.flags & kConstParm)) {
                if (Leaks(shape))
                    return false;
                // An object written back is read through a place as a plain Object.
                const std::optional<std::string> written =
                    object ? PlaceType(shape, context.includes, context.forwards, 0, true) : placed;
                if (written) {
                    signature.push_back(*written + " *" + ident);
                    outs.push_back("        if (" + ident + ")\n            if (auto urk_out = urk_frame.get_value<" +
                                   *written + ">(" + name + "))\n                *" + ident +
                                   " = std::move(*urk_out);\n");
                } else if (placed) {
                    signature.push_back(*placed + " *" + ident);
                    outs.push_back("        if (" + ident + ")\n            if (auto urk_out = urk_frame.get_value<" +
                                   *placed + ">(" + name + "))\n                *" + ident + " = std::move(*urk_out);\n");
                } else if (value) {
                    signature.push_back(*value + " *" + ident);
                    outs.push_back("        if (" + ident + ")\n            if (const auto urk_out = urk_frame.get<" +
                                   *value + ">(" + name + "))\n                *" + ident + " = *urk_out;\n");
                } else if (structValue) {
                    const std::string type = kTypes + structValue->ident;
                    signature.push_back(type + " *" + ident);
                    outs.push_back("        if (" + ident + ")\n            if (const auto urk_out = urk_frame.get_struct<" +
                                   type + ">(" + name + "))\n                *" + ident + " = *urk_out;\n");
                } else {
                    return false;
                }
            } else if (placed) {
                signature.push_back("const " + *placed + " &" + ident);
                sets.push_back("urk_frame.set_value<" + *placed + ">(" + name + ", " + ident + ")");
            } else if (value) {
                signature.push_back(*value + " " + ident);
                sets.push_back("urk_frame.set<" + *value + ">(" + name + ", " + ident + ")");
            } else if (structValue) {
                const std::string type = kTypes + structValue->ident;
                signature.push_back("const " + type + " &" + ident);
                sets.push_back("urk_frame.set_struct<" + type + ">(" + name + ", " + ident + ")");
            } else if (shape.kind == "object" || shape.kind == "class") {
                signature.push_back(std::string(kRuntime) + "Arg<" + ObjectType(shape, context.forwards) + "> " +
                                    ident);
                sets.push_back("urk_frame.set<URK_UnrealObject>(" + name + ", " + ident + ".handle())");
            } else {
                return false;
            }
        }

        std::string result = "bool";
        std::string failed = "false";
        std::string succeeded = "true";
        std::string prefix;
        if (returned) {
            if (Leaks(returned->shape))
                return false;
            const std::string name = "\"" + Escape(returned->name) + "\"";
            const Shape &shape = returned->shape;
            const bool object = shape.kind == "object" || shape.kind == "class";
            const bool enumerated = EnumOf(shape, context.includes) != nullptr;
            const std::optional<std::string> placed =
                object || (!enumerated && (ValueType(shape) || StructValue(shape, context.includes)))
                    ? std::nullopt
                    : PlaceType(shape, context.includes, context.forwards, 0, true);
            if (placed) {
                result = "std::optional<" + *placed + ">";
                failed = "std::nullopt";
                succeeded = "urk_frame.get_value<" + *placed + ">(" + name + ")";
            } else if (const std::optional<std::string> value = ValueType(returned->shape)) {
                result = "std::optional<" + *value + ">";
                failed = "std::nullopt";
                succeeded = "urk_frame.get<" + *value + ">(" + name + ")";
            } else if (const Type *structValue = StructValue(returned->shape, context.includes)) {
                const std::string type = kTypes + structValue->ident;
                result = "std::optional<" + type + ">";
                failed = "std::nullopt";
                succeeded = "urk_frame.get_struct<" + type + ">(" + name + ")";
            } else if (returned->shape.kind == "object" || returned->shape.kind == "class") {
                prefix = "template <typename UrkR = " + ObjectType(returned->shape, context.forwards) + "> ";
                result = "UrkR";
                failed = "UrkR()";
                succeeded = "UrkR(urk_frame.get<URK_UnrealObject>(" + name + ").value_or(" + kRuntime + "null_handle))";
            } else {
                return false;
            }
        }

        const bool isStatic = (function.flags & kFunctionStatic) != 0;
        const std::string ident = Unique(Identifier(function.name), taken, owner.ident);
        body << "    " << prefix << (isStatic ? "static " : "") << result << ' ' << ident << '(';
        for (std::size_t i = 0; i < signature.size(); ++i)
            body << (i ? ", " : "") << signature[i];
        body << ')' << (isStatic ? "" : " const") << " {\n"
             << "        const " << kRuntime << "Object urk_self = "
             << (isStatic ? "static_class().default_object()" : "*this") << ";\n"
             << "        " << kRuntime << "CallFrame urk_frame(urk_self.function(\"" << Escape(function.name)
             << "\"));\n        if (!(";
        for (const std::string &set : sets)
            body << set << " &&\n              ";
        body << kRuntime << "call(urk_self, urk_frame)))\n            return " << failed << ";\n";
        for (const std::string &out : outs)
            body << out;
        body << "        return " << succeeded << ";\n    }\n";
        return true;
    }

    const TypeMap &types_;
    std::map<std::string, const Type *> byIdent_;
    std::map<const Type *, Layout> layouts_;
};

// The game's module is named after its project folder: <Project>/Binaries/Win64/<dump>.
std::string ProjectOf(const std::filesystem::path &dumpPath) {
    const std::filesystem::path win64 = dumpPath.parent_path();
    const std::filesystem::path binaries = win64.parent_path();
    if (Lower(binaries.filename().string()) != "binaries")
        return {};
    return binaries.parent_path().filename().string();
}

// Every folder with the types it holds, the game's own first.
std::string Index(const TypeMap &types, const std::vector<const Type *> &emitted, const std::string &project) {
    struct Folder {
        std::vector<std::string> classes, structs, enums;
    };
    std::map<std::string, Folder> game, engine;
    for (const Type *entry : emitted) {
        const bool own = entry->package.rfind("/Game/", 0) == 0 ||
                         (!project.empty() && Lower(entry->package) == Lower("/Script/" + project));
        Folder &folder = (own ? game : engine)[entry->folder];
        (entry->isEnum ? folder.enums : entry->isStruct ? folder.structs : folder.classes).push_back(entry->ident);
    }
    std::ostringstream out;
    out << "# Types\n\nGenerated by urk-sdk from `" << kDumpFileName << "`; regenerated with it, do not edit. "
        << types.size() << " types, " << emitted.size() << " with a header. Folders mirror the package: "
        << "`/Script/Engine` is `Engine/`, `/Game/Doors/BP_Door` is `Game/Doors/`.\n\n"
        << "```cpp\n#include \"sdk/unreal/types/Engine/Character.h\"\n```\n";
    const auto section = [&out](const char *title, const std::map<std::string, Folder> &folders) {
        if (folders.empty())
            return;
        out << "\n## " << title << "\n";
        for (const auto &[name, folder] : folders) {
            out << "\n### " << name << "\n";
            const auto list = [&out](const char *kind, std::vector<std::string> names) {
                if (names.empty())
                    return;
                std::sort(names.begin(), names.end());
                out << "\n" << kind << " (" << names.size() << "): ";
                for (std::size_t i = 0; i < names.size(); ++i)
                    out << (i ? ", " : "") << names[i];
                out << "\n";
            };
            list("Classes", folder.classes);
            list("Structs", folder.structs);
            list("Enums", folder.enums);
        }
    };
    section(project.empty() ? "Game" : ("Game: " + project).c_str(), game);
    section("Engine and plugins", engine);
    return out.str();
}

} // namespace

bool Build(const std::filesystem::path &dumpPath, std::vector<Header> *headers, std::string *error) {
    std::string ignored;
    std::string &message = error ? *error : ignored;
    TypeMap types;
    if (!Parse(dumpPath, &types, &message))
        return false;
    AssignIdents(types);
    AssignFolders(types);

    Generator generator(types);
    headers->clear();
    headers->reserve(types.size() + 1);
    std::vector<const Type *> emitted;
    for (const auto &[key, entry] : types) {
        if (!generator.Emittable(entry))
            continue;
        headers->push_back(generator.Emit(entry));
        emitted.push_back(&entry);
    }
    headers->push_back({"INDEX.md", Index(types, emitted, ProjectOf(dumpPath))});
    return true;
}

} // namespace UnrealTypeCodegen
