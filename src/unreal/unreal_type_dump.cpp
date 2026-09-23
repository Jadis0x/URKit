#include "unreal_type_dump.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace URK::Unreal {
namespace {

constexpr std::int32_t kMaxFields = 4096;

// Names are FNames; a tab or line break would split a record.
std::string Clean(std::string text) {
    for (char &ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '\r')
            ch = ' ';
    }
    return text;
}

std::string Hex(std::uint64_t value) {
    char text[24];
    std::snprintf(text, sizeof(text), "%llx", static_cast<unsigned long long>(value));
    return text;
}

struct Named {
    std::string name;
    std::string package;
};

Named NameAndPackage(const ObjectFinder &finder, Address object) {
    if (object == kNullAddress)
        return {};
    return {Clean(finder.NameOf(object).value_or("")), Clean(finder.NameOf(finder.OuterOf(object)).value_or(""))};
}

// kind, element size, array dim, flags, then the referenced class or struct.
// The line is left open for a struct member's layout columns.
void WriteShapeOpen(std::ostringstream &out, const ObjectFinder &finder, const PropertyInfo &info) {
    out << '\t' << PropertyKindName(info.kind) << '\t' << info.elementSize << '\t' << info.arrayDim << '\t'
        << Hex(info.propertyFlags);
    const bool typed = info.kind == PropertyKind::Object || info.kind == PropertyKind::Class ||
                       info.kind == PropertyKind::Struct || info.kind == PropertyKind::WeakObject ||
                       info.kind == PropertyKind::SoftObject || info.kind == PropertyKind::Interface;
    const Named inner = typed ? NameAndPackage(finder, info.inner) : Named{};
    out << '\t' << inner.name << '\t' << inner.package;
}

void WriteShape(std::ostringstream &out, const ObjectFinder &finder, const PropertyInfo &info) {
    WriteShapeOpen(out, finder, info);
    out << '\n';
}

std::string DumpClass(const ObjectFinder &finder, const StructOffsets &structs, const PropertyChain &chain,
                      const PropertyValues &values, const FunctionOffsets &functions, const TypeQueries &types,
                      Address classObject, const Named &self) {
    std::ostringstream out;
    const Named super = NameAndPackage(finder, types.SuperOf(classObject));
    out << "C\t" << self.name << '\t' << self.package << '\t' << super.name << '\t' << super.package << '\n';

    Address field = chain.First(classObject);
    for (std::int32_t step = 0; field != kNullAddress && step < kMaxFields; ++step, field = chain.Next(field)) {
        const std::optional<PropertyInfo> info = values.Describe(field);
        const std::optional<std::string> name = chain.NameOf(field);
        if (!info || !name)
            continue;
        out << "P\t" << Clean(*name);
        WriteShape(out, finder, *info);
    }

    // UFunctions hang off Children as UFields.
    std::optional<Address> child = finder.Reader().ReadPointer(classObject + structs.children);
    for (std::int32_t step = 0; child && *child != kNullAddress && step < kMaxFields;
         ++step, child = finder.Reader().ReadPointer(*child + structs.fieldNext)) {
        if (!ObjectIs(finder, structs, *child, kCastFlagFunction))
            continue;
        const std::optional<FunctionInfo> function = DescribeFunction(chain, values, functions, *child);
        const std::optional<std::string> name = finder.NameOf(*child);
        if (!function || !name)
            continue;
        out << "F\t" << Clean(*name) << '\t' << Hex(function->flags) << '\n';
        for (const FunctionParameter &parameter : function->parameters) {
            out << "A\t" << Clean(parameter.name);
            WriteShape(out, finder, parameter.info);
        }
    }
    return out.str();
}

// A struct is a value: its own members with offsets, bitfield bools with masks.
std::string DumpStruct(const ObjectFinder &finder, const StructOffsets &structs, const PropertyChain &chain,
                       const PropertyValues &values, const TypeQueries &types, Address structObject,
                       const Named &self) {
    const MemoryReader &reader = finder.Reader();
    const std::int32_t size = structs.propertiesSize == kOffsetNotFound
                                  ? 0
                                  : reader.ReadInt32(structObject + structs.propertiesSize).value_or(0);
    // int16 since UE5.x (int32 before; the low half is the same value).
    const std::int32_t alignment = structs.minAlignment == kOffsetNotFound
                                       ? 0
                                       : reader.ReadAs<std::int16_t>(structObject + structs.minAlignment).value_or(0);
    std::ostringstream out;
    const Named super = NameAndPackage(finder, types.SuperOf(structObject));
    out << "S\t" << self.name << '\t' << self.package << '\t' << super.name << '\t' << super.package << '\t' << size
        << '\t' << alignment << '\n';

    Address field = chain.First(structObject);
    for (std::int32_t step = 0; field != kNullAddress && step < kMaxFields; ++step, field = chain.Next(field)) {
        const std::optional<PropertyInfo> info = values.Describe(field);
        const std::optional<std::string> name = chain.NameOf(field);
        if (!info || !info->Resolved() || !name)
            continue;
        out << "M\t" << Clean(*name);
        WriteShapeOpen(out, finder, *info);
        out << '\t' << info->offset << '\t' << static_cast<int>(info->boolLayout.byteOffset) << '\t'
            << static_cast<int>(info->boolLayout.byteMask) << '\t' << static_cast<int>(info->boolLayout.fieldMask)
            << '\n';
    }
    return out.str();
}

// "C|S<TAB>name<TAB>package..." to the block key "package<TAB>name".
std::string KeyOf(const std::string &recordLine) {
    std::istringstream fields(recordLine);
    std::string tag, name, package;
    std::getline(fields, tag, '\t');
    std::getline(fields, name, '\t');
    std::getline(fields, package, '\t');
    return package + '\t' + name;
}

std::string Header(const TypeDumpImage &image) {
    std::ostringstream out;
    out << kTypeDumpMagic << '\t' << kTypeDumpVersion << '\n'
        << "IMAGE\t" << Hex(image.timeDateStamp) << '\t' << Hex(image.sizeOfImage) << '\n'
        << "ENGINE\t" << Clean(image.engine) << '\n';
    return out.str();
}

// Blocks of an earlier dump of this same build and format; anything else starts over.
TypeDumpBlocks ReadExisting(const std::string &path, const TypeDumpImage &image) {
    TypeDumpBlocks blocks;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return blocks;
    const std::string expected = Header(image);
    std::string header;
    std::string line;
    for (int i = 0; i < 3 && std::getline(in, line); ++i)
        header += line + '\n';
    if (header != expected)
        return blocks;

    std::string key;
    while (std::getline(in, line)) {
        if (line.rfind("C\t", 0) == 0 || line.rfind("S\t", 0) == 0)
            key = KeyOf(line);
        if (!key.empty())
            blocks[key] += line + '\n';
    }
    return blocks;
}

} // namespace

TypeDumpBlocks DumpClasses(const ObjectFinder &finder, const StructOffsets &structs, const PropertyChain &chain,
                           const PropertyValues &values, const FunctionOffsets &functions, const TypeQueries &types) {
    TypeDumpBlocks blocks;
    if (structs.children == kOffsetNotFound || structs.fieldNext == kOffsetNotFound)
        return blocks;

    const ObjectArray &objects = finder.Objects();
    const std::int32_t total = objects.Num();
    for (std::int32_t slot = 0; slot < total; ++slot) {
        const Address object = objects.ObjectAt(slot);
        if (object == kNullAddress)
            continue;
        const bool isClass = ObjectIs(finder, structs, object, kCastFlagClass);
        if (!isClass && !ObjectIs(finder, structs, object, kCastFlagScriptStruct))
            continue;
        const Named self = NameAndPackage(finder, object);
        if (self.name.empty() || self.package.empty())
            continue;
        blocks.emplace(self.package + '\t' + self.name,
                       isClass ? DumpClass(finder, structs, chain, values, functions, types, object, self)
                               : DumpStruct(finder, structs, chain, values, types, object, self));
    }
    return blocks;
}

int WriteTypeDump(const std::string &path, const TypeDumpImage &image, const TypeDumpBlocks &blocks) {
    TypeDumpBlocks merged = ReadExisting(path, image);
    int added = 0;
    for (const auto &[key, block] : blocks) {
        const auto [at, inserted] = merged.insert_or_assign(key, block);
        added += inserted ? 1 : 0;
    }

    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << Header(image);
        for (const auto &[key, block] : merged)
            out << block;
        if (!out)
            return -1;
    }
    if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(temporary.c_str());
        return -1;
    }
    return added;
}

} // namespace URK::Unreal
