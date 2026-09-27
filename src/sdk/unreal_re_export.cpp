// sdk/unreal/re/: what other tools read. Mappings for asset readers, C layouts and names for disassemblers.

#include "unreal_type_model.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace UnrealTypeCodegen {
namespace {

constexpr std::uint64_t kEditorOnly = 0x0000000800000000ull;
constexpr std::uint32_t kFunctionNative = 0x400;

std::string Ident(const std::string &name) {
    std::string out;
    for (const char ch : name) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        const char next = keep ? ch : '_';
        if (next == '_' && !out.empty() && out.back() == '_')
            continue;
        out += next;
    }
    if (out.empty())
        return "Unnamed";
    if (out.front() >= '0' && out.front() <= '9')
        out = "N" + out;
    return out;
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "0x%llX", static_cast<unsigned long long>(value));
    return text;
}

// --- mappings.usmap (CUE4Parse's reader; version ExplicitEnumValues, uncompressed) -------------------

// EUsmapPropertyType.
enum : std::uint8_t {
    kUByte = 0,
    kUBool = 1,
    kUInt = 2,
    kUFloat = 3,
    kUObject = 4,
    kUName = 5,
    kUDelegate = 6,
    kUDouble = 7,
    kUArray = 8,
    kUStruct = 9,
    kUStr = 10,
    kUText = 11,
    kUInterface = 12,
    kUMulticastDelegate = 13,
    kUWeakObject = 14,
    kULazyObject = 15,
    kUSoftObject = 17,
    kUUInt64 = 18,
    kUUInt32 = 19,
    kUUInt16 = 20,
    kUInt64 = 21,
    kUInt16 = 22,
    kUInt8 = 23,
    kUMap = 24,
    kUSet = 25,
    kUEnum = 26,
    kUFieldPath = 27,
    kUOptional = 28,
    kUUtf8Str = 29,
    kUAnsiStr = 30,
    kUUnknown = 0xFF,
};

class Usmap {
  public:
    std::string Build(const TypeMap &types) {
        std::string enums, structs;
        std::uint32_t enumCount = 0, structCount = 0;
        for (const auto &[key, type] : types) {
            if (!type.isEnum)
                continue;
            ++enumCount;
            Int32(enums, Name(type.name));
            const std::size_t count = std::min<std::size_t>(type.values.size(), 0xFFFF);
            Put<std::uint16_t>(enums, static_cast<std::uint16_t>(count));
            for (std::size_t i = 0; i < count; ++i) {
                std::int64_t value = static_cast<std::int64_t>(i);
                try {
                    value = std::stoll(type.values[i].second);
                } catch (...) {
                }
                Put<std::uint64_t>(enums, static_cast<std::uint64_t>(value));
                Int32(enums, Name(type.values[i].first));
            }
        }
        for (const auto &[key, type] : types) {
            if (type.isEnum)
                continue;
            ++structCount;
            Int32(structs, Name(type.name));
            Int32(structs, type.superName.empty() ? -1 : Name(type.superName));
            std::vector<const Member *> members;
            std::uint32_t slots = 0;
            for (const Member &member : type.members) {
                if (member.shape.flags & kEditorOnly)
                    continue;
                members.push_back(&member);
                slots += static_cast<std::uint32_t>(std::max(1, member.shape.dim));
            }
            Put<std::uint16_t>(structs, static_cast<std::uint16_t>(std::min<std::uint32_t>(slots, 0xFFFF)));
            Put<std::uint16_t>(structs, static_cast<std::uint16_t>(std::min<std::size_t>(members.size(), 0xFFFF)));
            std::uint32_t index = 0;
            for (const Member *member : members) {
                Put<std::uint16_t>(structs, static_cast<std::uint16_t>(index));
                Put<std::uint8_t>(structs, static_cast<std::uint8_t>(std::clamp(member->shape.dim, 1, 255)));
                Int32(structs, Name(member->name));
                Property(structs, member->shape, 0);
                index += static_cast<std::uint32_t>(std::max(1, member->shape.dim));
            }
        }

        std::string data;
        Put<std::uint32_t>(data, static_cast<std::uint32_t>(names_.size()));
        for (const std::string &name : names_) {
            const std::size_t length = std::min<std::size_t>(name.size(), 0xFFFF);
            Put<std::uint16_t>(data, static_cast<std::uint16_t>(length));
            data.append(name, 0, length);
        }
        Put<std::uint32_t>(data, enumCount);
        data += enums;
        Put<std::uint32_t>(data, structCount);
        data += structs;

        std::string file;
        Put<std::uint16_t>(file, 0x30C4);
        // EUsmapVersion::ExplicitEnumValues; no package versioning (a 4-byte false), no compression.
        Put<std::uint8_t>(file, 4);
        Put<std::int32_t>(file, 0);
        Put<std::uint8_t>(file, 0);
        Put<std::uint32_t>(file, static_cast<std::uint32_t>(data.size()));
        Put<std::uint32_t>(file, static_cast<std::uint32_t>(data.size()));
        return file + data;
    }

  private:
    template <typename T> static void Put(std::string &out, T value) {
        char bytes[sizeof(T)];
        std::memcpy(bytes, &value, sizeof(T));
        out.append(bytes, sizeof(T));
    }
    static void Int32(std::string &out, std::int32_t value) { Put<std::int32_t>(out, value); }

    std::int32_t Name(const std::string &name) {
        const auto [found, added] = index_.try_emplace(name, static_cast<std::int32_t>(names_.size()));
        if (added)
            names_.push_back(name);
        return found->second;
    }

    // An enum-valued shape: EnumProperty over the underlying integer, as the engine serializes it.
    void Property(std::string &out, const Shape &shape, int depth) {
        static const std::map<std::string, std::uint8_t> plain = {
            {"bool", kUBool},       {"int8", kUInt8},       {"int16", kUInt16},     {"int32", kUInt},
            {"int64", kUInt64},     {"uint16", kUUInt16},   {"uint32", kUUInt32},   {"uint64", kUUInt64},
            {"float", kUFloat},     {"double", kUDouble},   {"name", kUName},       {"string", kUStr},
            {"text", kUText},       {"object", kUObject},   {"class", kUObject},    {"weak object", kUWeakObject},
            {"lazy object", kULazyObject}, {"soft object", kUSoftObject}, {"interface", kUInterface},
            {"delegate", kUDelegate}, {"multicast delegate", kUMulticastDelegate},
            {"sparse delegate", kUMulticastDelegate}, {"field path", kUFieldPath}, {"utf8 string", kUUtf8Str},
            {"ansi string", kUAnsiStr}};
        const auto element = [&](std::size_t i) {
            if (i < shape.elements.size() && depth < 8)
                Property(out, shape.elements[i], depth + 1);
            else
                Put<std::uint8_t>(out, kUUnknown);
        };
        if (shape.kind == "byte" || shape.kind == "enum") {
            if (shape.inner.empty()) {
                Put<std::uint8_t>(out, kUByte);
                return;
            }
            Put<std::uint8_t>(out, kUEnum);
            const std::uint8_t underlying = shape.kind == "byte" || shape.size == 1 ? kUByte
                                            : shape.size == 2                      ? kUUInt16
                                            : shape.size == 8                      ? kUInt64
                                                                                   : kUInt;
            Put<std::uint8_t>(out, underlying);
            Int32(out, Name(shape.inner));
            return;
        }
        if (const auto found = plain.find(shape.kind); found != plain.end()) {
            Put<std::uint8_t>(out, found->second);
            return;
        }
        if (shape.kind == "struct") {
            Put<std::uint8_t>(out, kUStruct);
            Int32(out, shape.inner.empty() ? -1 : Name(shape.inner));
        } else if (shape.kind == "array" || shape.kind == "set" || shape.kind == "optional") {
            Put<std::uint8_t>(out, shape.kind == "array" ? kUArray : shape.kind == "set" ? kUSet : kUOptional);
            element(0);
        } else if (shape.kind == "map") {
            Put<std::uint8_t>(out, kUMap);
            element(0);
            element(1);
        } else {
            Put<std::uint8_t>(out, kUUnknown);
        }
    }

    std::vector<std::string> names_;
    std::map<std::string, std::int32_t> index_;
};

// --- unreal_types.h: C structs at the dumped offsets ---------------------------------------------------

class Layouts {
  public:
    explicit Layouts(const TypeMap &types) : types_(types) {
        for (const char *word : kReserved)
            taken_.insert(word);
        // Size of FName and FText as the dump sees them.
        for (const auto &[key, type] : types) {
            for (const Member &member : type.members) {
                if (member.shape.kind == "name")
                    nameSize_ = member.shape.size;
                else if (member.shape.kind == "text")
                    textSize_ = member.shape.size;
            }
        }
        for (const auto &[key, type] : types) {
            std::string base = type.isEnum ? Ident(type.ident) : (type.isStruct ? "F" : Prefix(type)) + Ident(type.ident);
            std::string name = base;
            for (int n = 2; taken_.count(name); ++n)
                name = base + '_' + std::to_string(n);
            taken_.insert(name);
            names_[&type] = name;
        }
    }

    const std::string &NameOf(const Type &type) const { return names_.at(&type); }

    std::string Header(const std::string &project) {
        for (const auto &[key, type] : types_) {
            if (!type.isEnum)
                Emit(type);
        }
        std::ostringstream out;
        out << "// " << (project.empty() ? std::string("The game") : project)
            << ": reflected classes and structs at their dumped offsets, generated by urk-sdk.\n"
            << "// IDA: File > Load file > Parse C header file (or ida_import.py). Ghidra: File > Parse C Source\n"
            << "// (or ghidra_import.py). Offsets are those of the dumped build; members the dump did not place are\n"
            << "// padding. Enum-typed members are their integer, named in a comment.\n\n"
            << "#pragma pack(push, 1)\n\n"
            << "typedef signed char int8;\ntypedef unsigned char uint8;\ntypedef short int16;\n"
            << "typedef unsigned short uint16;\ntypedef int int32;\ntypedef unsigned int uint32;\n"
            << "typedef long long int64;\ntypedef unsigned long long uint64;\n\n";
        out << "typedef struct FName {\n    int32 ComparisonIndex;\n";
        if (nameSize_ == 12)
            out << "    int32 DisplayIndex;\n";
        if (nameSize_ >= 8)
            out << "    int32 Number;\n";
        out << "} FName;\n"
            << "typedef struct FString {\n    wchar_t *Data;\n    int32 Num;\n    int32 Max;\n} FString;\n"
            << "typedef struct FUtf8String {\n    char *Data;\n    int32 Num;\n    int32 Max;\n} FUtf8String;\n"
            << "typedef struct FAnsiString {\n    char *Data;\n    int32 Num;\n    int32 Max;\n} FAnsiString;\n"
            << "typedef struct FText {\n    uint8 Bytes[" << textSize_ << "];\n} FText;\n"
            << "typedef struct FWeakObjectPtr {\n    int32 ObjectIndex;\n    int32 ObjectSerialNumber;\n} FWeakObjectPtr;\n"
            << "typedef struct FScriptInterface {\n    struct UObject *ObjectPointer;\n    void *InterfacePointer;\n"
            << "} FScriptInterface;\n"
            << "typedef struct TArray {\n    void *Data;\n    int32 Num;\n    int32 Max;\n} TArray;\n\n";
        for (const auto &[key, type] : types_) {
            if (!type.isEnum)
                out << "typedef struct " << NameOf(type) << ' ' << NameOf(type) << ";\n";
        }
        out << '\n';
        for (const auto &[key, type] : types_) {
            if (type.isEnum)
                out << EnumText(type);
        }
        out << arrays_.str() << '\n' << structs_.str() << "#pragma pack(pop)\n";
        return out.str();
    }

  private:
    static constexpr const char *kReserved[] = {
        "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern",
        "float", "for", "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed",
        "sizeof", "static", "struct", "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "bool",
        "class", "private", "public", "protected", "template", "this", "new", "delete", "operator", "virtual",
        "namespace", "true", "false", "wchar_t", "int8", "uint8", "int16", "uint16", "int32", "uint32", "int64",
        "uint64", "FName", "FString", "FUtf8String", "FAnsiString", "FText", "FWeakObjectPtr", "FScriptInterface",
        "TArray", "Super", "near", "far", "interface"};

    const Type *Find(const std::string &package, const std::string &name) const {
        const auto found = types_.find(Key(package, name));
        return found == types_.end() ? nullptr : &found->second;
    }

    // A struct's value size is PropertiesSize rounded up to its alignment; a class's is PropertiesSize.
    static int Size(const Type &type) {
        const int alignment = type.alignment;
        if (!type.isStruct || alignment <= 1 || alignment > 256 || (alignment & (alignment - 1)) != 0)
            return type.size;
        return (type.size + alignment - 1) / alignment * alignment;
    }

    std::string Prefix(const Type &type) const {
        const Type *at = &type;
        for (int depth = 0; at && depth < 64; ++depth) {
            if (at->name == "Actor" && at->package == "/Script/Engine")
                return "A";
            at = Find(at->superPackage, at->superName);
        }
        return "U";
    }

    std::string EnumText(const Type &type) const {
        std::ostringstream out;
        out << "enum " << NameOf(type) << " {\n";
        std::set<std::string> used;
        for (const auto &[name, value] : type.values) {
            long long number = 0;
            try {
                number = std::stoll(value);
            } catch (...) {
                continue;
            }
            std::string entry = NameOf(type) + "__" + Ident(name);
            if (!used.insert(entry).second || number < INT32_MIN || number > INT32_MAX)
                continue;
            out << "    " << entry << " = " << number << ",\n";
        }
        out << "};\n";
        return out.str();
    }

    // Integer spellings by size, for bytes and enums.
    static std::string Integer(int size) {
        switch (size) {
        case 1:
            return "uint8";
        case 2:
            return "uint16";
        case 4:
            return "uint32";
        case 8:
            return "uint64";
        default:
            return {};
        }
    }

    // The C type of a value of this shape, exactly shape.size bytes; empty when it has none.
    std::string ValueType(const Shape &shape, std::string &note) {
        static const std::map<std::string, std::pair<std::string, int>> fixed = {
            {"bool", {"bool", 1}},     {"int8", {"int8", 1}},     {"int16", {"int16", 2}},   {"int32", {"int32", 4}},
            {"int64", {"int64", 8}},   {"uint16", {"uint16", 2}}, {"uint32", {"uint32", 4}}, {"uint64", {"uint64", 8}},
            {"float", {"float", 4}},   {"double", {"double", 8}}, {"string", {"FString", 16}},
            {"utf8 string", {"FUtf8String", 16}}, {"ansi string", {"FAnsiString", 16}},
            {"weak object", {"FWeakObjectPtr", 8}}, {"interface", {"FScriptInterface", 16}}};
        if (const auto found = fixed.find(shape.kind); found != fixed.end())
            return found->second.second == shape.size ? found->second.first : std::string();
        if (shape.kind == "byte" || shape.kind == "enum") {
            if (!shape.inner.empty())
                note = shape.inner;
            return Integer(shape.size);
        }
        if (shape.kind == "name")
            return shape.size == nameSize_ ? "FName" : std::string();
        if (shape.kind == "text")
            return shape.size == textSize_ ? "FText" : std::string();
        if (shape.kind == "object" || shape.kind == "class") {
            const Type *target = Find(shape.innerPackage, shape.inner);
            if (shape.kind == "class")
                target = Find("/Script/CoreUObject", "Class");
            if (!target || target->isStruct || target->isEnum)
                target = Find("/Script/CoreUObject", "Object");
            return shape.size == 8 ? (target ? NameOf(*target) : std::string("void")) + " *" : std::string();
        }
        if (shape.kind == "struct") {
            const Type *target = Find(shape.innerPackage, shape.inner);
            if (!target || !target->isStruct || Size(*target) != shape.size)
                return {};
            Emit(*target);
            return defined_.count(target) ? NameOf(*target) : std::string();
        }
        if (shape.kind == "array" && shape.size == 16)
            return ArrayType(shape);
        return {};
    }

    // TArray of a known element: its Data typed.
    std::string ArrayType(const Shape &shape) {
        if (shape.elements.empty())
            return "TArray";
        std::string note;
        const std::string element = ValueType(shape.elements[0], note);
        if (element.empty())
            return "TArray";
        std::string name = "TArray_" + Ident(element);
        if (element.back() == '*')
            name = "TArray_" + Ident(element.substr(0, element.size() - 2)) + "_ptr";
        if (arrayTypes_.insert(name).second)
            arrays_ << "typedef struct " << name << " {\n    " << element << " *Data;\n    int32 Num;\n    int32 Max;\n} "
                    << name << ";\n";
        return name;
    }

    std::string Unique(const std::string &base, std::set<std::string> &used) const {
        std::string name = base;
        if (taken_.count(name))
            name += '_';
        std::string candidate = name;
        for (int n = 2; used.count(candidate); ++n)
            candidate = name + '_' + std::to_string(n);
        used.insert(candidate);
        return candidate;
    }

    void Emit(const Type &type) {
        if (emitted_.count(&type) || visiting_.count(&type) || type.isEnum)
            return;
        visiting_.insert(&type);
        const Type *super = Find(type.superPackage, type.superName);
        if (super && !super->isEnum)
            Emit(*super);

        struct Item {
            int at = 0;
            const Member *member = nullptr;
            int bit = -1;
        };
        std::vector<Item> items;
        for (const Member &member : type.members) {
            if (member.offset < 0)
                continue;
            const bool bitfield = member.shape.kind == "bool" && member.fieldMask != 0xFF && member.fieldMask != 0;
            int bit = -1;
            if (bitfield) {
                for (int b = 0; b < 8; ++b) {
                    if (member.fieldMask == (1 << b))
                        bit = b;
                }
                if (bit < 0)
                    continue;
            }
            items.push_back({member.offset + (bitfield ? member.boolByte : 0), &member, bit});
        }
        std::stable_sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
            return a.at != b.at ? a.at < b.at : a.bit < b.bit;
        });

        std::ostringstream body;
        std::set<std::string> used;
        int cursor = 0;
        const bool object = type.name == "Object" && type.package == "/Script/CoreUObject";
        if (object && type.size == 0x28 && nameSize_ == 8) {
            // UObjectBase, 4.25-5.8.
            body << "    void **VTable;\n    int32 ObjectFlags;\n    int32 InternalIndex;\n    struct UClass *ClassPrivate;\n"
                 << "    FName NamePrivate;\n    struct UObject *OuterPrivate;\n";
            cursor = 0x28;
        } else if (super && defined_.count(super) && super->size > 0) {
            body << "    " << NameOf(*super) << " Super;\n";
            cursor = Size(*super);
        }
        const auto pad = [&](int to) {
            if (to > cursor)
                body << "    uint8 " << Unique("pad_" + std::string(Hex(static_cast<std::uint64_t>(cursor))).substr(2), used)
                     << '[' << Hex(static_cast<std::uint64_t>(to - cursor)) << "];\n";
            cursor = std::max(cursor, to);
        };
        for (std::size_t i = 0; i < items.size();) {
            const Item &item = items[i];
            if (item.at < cursor) {
                body << "    // " << item.member->name << " at " << Hex(static_cast<std::uint64_t>(item.at))
                     << " overlaps what precedes it\n";
                ++i;
                continue;
            }
            pad(item.at);
            if (item.bit >= 0) {
                // Bitfield bools sharing one byte.
                int bit = 0;
                for (; i < items.size() && items[i].at == item.at && items[i].bit >= bit; ++i) {
                    if (items[i].bit > bit)
                        body << "    uint8 " << Unique("bits_" + std::to_string(bit), used) << " : "
                             << items[i].bit - bit << ";\n";
                    body << "    uint8 " << Unique(Ident(items[i].member->name), used) << " : 1;\n";
                    bit = items[i].bit + 1;
                }
                if (bit < 8)
                    body << "    uint8 " << Unique("bits_" + std::to_string(bit), used) << " : " << 8 - bit << ";\n";
                cursor = item.at + 1;
                continue;
            }
            const Member &member = *item.member;
            const int dim = std::max(1, member.shape.dim);
            std::string note;
            const std::string value = ValueType(member.shape, note);
            const std::string name = Unique(Ident(member.name), used);
            if (value.empty())
                body << "    uint8 " << name << '[' << Hex(static_cast<std::uint64_t>(member.shape.size) * dim)
                     << "]; // " << member.shape.kind << (member.shape.inner.empty() ? "" : " " + member.shape.inner)
                     << '\n';
            else
                body << "    " << value << ' ' << name << (dim > 1 ? '[' + std::to_string(dim) + ']' : std::string())
                     << ';' << (note.empty() ? "" : " // " + note) << '\n';
            cursor = item.at + member.shape.size * dim;
            ++i;
        }
        pad(Size(type));
        visiting_.erase(&type);
        emitted_.insert(&type);
        // No size (an older dump's class): declared only; C has no empty structs.
        if (cursor == 0)
            return;
        defined_.insert(&type);
        structs_ << "struct " << NameOf(type) << " { // " << Hex(static_cast<std::uint64_t>(Size(type))) << ", "
                 << type.package << '.' << type.name << "\n" << body.str() << "};\n\n";
    }

    const TypeMap &types_;
    std::set<std::string> taken_;
    std::map<const Type *, std::string> names_;
    std::set<const Type *> emitted_;
    std::set<const Type *> defined_;
    std::set<const Type *> visiting_;
    std::set<std::string> arrayTypes_;
    std::ostringstream arrays_;
    std::ostringstream structs_;
    int nameSize_ = 8;
    int textSize_ = 24;
};

// --- disassembler scripts ----------------------------------------------------------------------------

std::string PythonString(const std::string &text) {
    std::string out = "\"";
    for (const char ch : text) {
        if (ch == '"' || ch == '\\')
            out += '\\';
        out += (static_cast<unsigned char>(ch) < 0x20) ? '_' : ch;
    }
    return out + '"';
}

std::string Lists(const std::vector<std::pair<std::uint64_t, std::string>> &functions, const Globals &globals) {
    std::ostringstream out;
    out << "FUNCTIONS = [\n";
    for (const auto &[rva, name] : functions)
        out << "    (" << Hex(rva) << ", " << PythonString(name) << "),\n";
    out << "]\n\nGLOBALS = [\n";
    for (const auto &[name, rva] : globals)
        out << "    (" << Hex(rva) << ", " << PythonString(name) << "),\n";
    out << "]\n";
    return out.str();
}

std::string IdaScript(const std::string &project, const std::string &lists) {
    std::ostringstream out;
    out << "# URKit: names " << project << "'s native function thunks and engine globals, then parses\n"
        << "# unreal_types.h. In IDA with the game's executable open: File > Script file. Offsets are the\n"
        << "# dumped build's; another build gets wrong names.\n"
        << "import os\n\nimport ida_funcs\nimport ida_name\nimport idaapi\nimport idc\n\n"
        << lists << R"PY(

def main():
    base = idaapi.get_imagebase()
    flags = ida_name.SN_NOWARN | ida_name.SN_NOCHECK | ida_name.SN_FORCE
    named = 0
    for rva, name in FUNCTIONS:
        ea = base + rva
        if not ida_funcs.get_func(ea):
            ida_funcs.add_func(ea)
        if ida_name.set_name(ea, name, flags):
            named += 1
    for rva, name in GLOBALS:
        ida_name.set_name(base + rva, name, flags)
    header = os.path.join(os.path.dirname(os.path.abspath(__file__)), "unreal_types.h")
    errors = idc.parse_decls(header, idc.PT_FILE | idc.PT_SILENT)
    print("URKit: %d of %d functions named, %d globals; unreal_types.h parsed with %d errors"
          % (named, len(FUNCTIONS), len(GLOBALS), errors))


main()
)PY";
    return out.str();
}

std::string GhidraScript(const std::string &project, const std::string &lists) {
    std::ostringstream out;
    out << "# URKit: names " << project << "'s native function thunks and engine globals, then parses\n"
        << "# unreal_types.h. Run from the Script Manager with the game's executable open. Offsets are the\n"
        << "# dumped build's; another build gets wrong names.\n"
        << "# @category URKit\n"
        << "import os\n\nfrom ghidra.program.model.symbol import SourceType\n\n" << lists << R"PY(

def namespace(owner):
    symbols = currentProgram.getSymbolTable()
    root = currentProgram.getGlobalNamespace()
    if not owner:
        return root
    found = symbols.getNamespace(owner, root)
    return found if found else symbols.createClass(root, owner, SourceType.USER_DEFINED)


def name(address, full, function):
    owner, _, short = full.rpartition("::")
    target = getFunctionAt(address)
    if target is None and function:
        target = createFunction(address, None)
    if target is not None:
        target.setParentNamespace(namespace(owner))
        target.setName(short, SourceType.USER_DEFINED)
    else:
        createLabel(address, short, namespace(owner), True, SourceType.USER_DEFINED)


def main():
    base = currentProgram.getImageBase()
    named = 0
    for rva, full in FUNCTIONS:
        try:
            name(base.add(rva), full, True)
            named += 1
        except Exception as error:
            print("URKit: %s: %s" % (full, error))
    for rva, full in GLOBALS:
        try:
            name(base.add(rva), full, False)
        except Exception as error:
            print("URKit: %s: %s" % (full, error))
    print("URKit: %d of %d functions named, %d globals" % (named, len(FUNCTIONS), len(GLOBALS)))
    header = os.path.join(getSourceFile().getParentFile().getAbsolutePath(), "unreal_types.h")
    try:
        from ghidra.app.util.cparser.C import CParser
        parser = CParser(currentProgram.getDataTypeManager(), True, None)
        with open(header) as source:
            parser.parse(source.read())
        print("URKit: unreal_types.h parsed")
    except Exception as error:
        print("URKit: parse unreal_types.h with File > Parse C Source instead (%s)" % error)


main()
)PY";
    return out.str();
}

} // namespace

std::vector<ExportFile> ReverseEngineeringFiles(const TypeMap &types, const Globals &globals,
                                                const std::string &project) {
    std::vector<ExportFile> files;
    const std::string label = project.empty() ? "the game" : project;
    files.push_back({(project.empty() ? std::string("mappings") : Ident(project)) + ".usmap", Usmap().Build(types)});

    Layouts layouts(types);
    files.push_back({"unreal_types.h", layouts.Header(project)});

    std::vector<std::pair<std::uint64_t, std::string>> functions;
    for (const auto &[key, type] : types) {
        if (type.isEnum || type.isStruct)
            continue;
        for (const Function &function : type.functions) {
            if ((function.flags & kFunctionNative) && function.nativeRva != 0)
                functions.emplace_back(function.nativeRva, layouts.NameOf(type) + "::exec" + function.name);
        }
    }
    std::sort(functions.begin(), functions.end());
    // One name per thunk: shared thunks (the same exec for several functions) keep the first.
    functions.erase(std::unique(functions.begin(), functions.end(),
                                [](const auto &a, const auto &b) { return a.first == b.first; }),
                    functions.end());
    const std::string lists = Lists(functions, globals);
    files.push_back({"ida_import.py", IdaScript(label, lists)});
    files.push_back({"ghidra_import.py", GhidraScript(label, lists)});
    return files;
}

} // namespace UnrealTypeCodegen
