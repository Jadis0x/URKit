#include "unreal_type_dump.h"

#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <thread>

namespace URK::Unreal {
namespace {

constexpr std::int32_t kMaxFields = 4096;

// Tabs and line breaks split records, a bar splits element shapes.
std::string Clean(std::string text) {
    for (char &ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '\r' || ch == '|')
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

// The object a shape is named by: a class, struct, UEnum or delegate signature.
Named TypeOf(const ObjectFinder &finder, const PropertyInfo &info) {
    if (info.typeObject == kNullAddress || !IsLiveObject(finder, info.typeObject))
        return {};
    return NameAndPackage(finder, info.typeObject);
}

// kind, element size, array dim, flags, type object; left open for layout columns.
void WriteShapeOpen(std::ostringstream &out, const ObjectFinder &finder, const PropertyInfo &info) {
    out << '\t' << PropertyKindName(info.kind) << '\t' << info.elementSize << '\t' << info.arrayDim << '\t'
        << Hex(info.propertyFlags);
    const Named inner = TypeOf(finder, info);
    out << '\t' << inner.name << '\t' << inner.package;
}

// A container element: "kind|size|type name|type package".
std::string ElementShape(const ObjectFinder &finder, const PropertyValues &values, Address field) {
    const std::optional<PropertyInfo> info = values.Describe(field);
    if (!info)
        return "unknown|0||";
    const Named type = TypeOf(finder, *info);
    return std::string(PropertyKindName(info->kind)) + '|' + std::to_string(info->elementSize) + '|' + type.name +
           '|' + type.package;
}

// Closes a shape line: element shapes of an array or set (one) or a map (two).
void WriteElements(std::ostringstream &out, const ObjectFinder &finder, const PropertyValues &values,
                   const PropertyInfo &info) {
    if (info.kind == PropertyKind::Array || info.kind == PropertyKind::Set || info.kind == PropertyKind::Map)
        out << '\t' << ElementShape(finder, values, info.inner);
    if (info.kind == PropertyKind::Map)
        out << '\t' << ElementShape(finder, values, info.valueInner);
    out << '\n';
}

void WriteShape(std::ostringstream &out, const ObjectFinder &finder, const PropertyValues &values,
                const PropertyInfo &info) {
    WriteShapeOpen(out, finder, info);
    WriteElements(out, finder, values, info);
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
        WriteShape(out, finder, values, *info);
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
            WriteShape(out, finder, values, parameter.info);
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
            << static_cast<int>(info->boolLayout.byteMask) << '\t' << static_cast<int>(info->boolLayout.fieldMask);
        WriteElements(out, finder, values, *info);
    }
    return out.str();
}

// Enum names from the running game; values are informational.
std::string DumpEnum(const EnumNames &enums, Address enumObject, const Named &self) {
    std::ostringstream out;
    out << "E\t" << self.name << '\t' << self.package << '\n';
    if (const std::optional<std::vector<EnumNames::Entry>> entries = enums.Entries(enumObject)) {
        for (const EnumNames::Entry &entry : *entries)
            out << "V\t" << Clean(std::string(EnumNames::ShortName(entry.name))) << '\t' << entry.value << '\n';
    }
    return out.str();
}

// "C|S|E<TAB>name<TAB>package..." to the block key "package<TAB>name".
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
        << "IMAGE\t" << Hex(image.timeDateStamp) << '\t' << Hex(image.sizeOfImage) << '\t' << Hex(image.loaderStamp)
        << '\n'
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
        if (line.rfind("C\t", 0) == 0 || line.rfind("S\t", 0) == 0 || line.rfind("E\t", 0) == 0)
            key = KeyOf(line);
        if (!key.empty())
            blocks[key] += line + '\n';
    }
    return blocks;
}

bool WriteAll(const std::string &path, const TypeDumpImage &image, const TypeDumpBlocks &blocks) {
    const std::string temporary = path + ".tmp";
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << Header(image);
        for (const auto &[key, block] : blocks)
            out << block;
        if (!out)
            return false;
    }
    if (!MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileA(temporary.c_str());
        return false;
    }
    return true;
}

constexpr std::uint8_t kKindClass = 1;
constexpr std::uint8_t kKindStruct = 2;
constexpr std::uint8_t kKindEnum = 3;

double MillisecondsSince(const LARGE_INTEGER &start) {
    LARGE_INTEGER now{}, frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    return 1000.0 * static_cast<double>(now.QuadPart - start.QuadPart) / static_cast<double>(frequency.QuadPart);
}

} // namespace

TypeDumper::TypeDumper(const TypeDumpSources &sources, std::string path, const TypeDumpImage &image, Report report)
    : sources_(sources), path_(std::move(path)), image_(image), report_(std::move(report)) {
    *file_ = ReadExisting(path_, image_);
    for (const auto &entry : *file_)
        known_.insert(entry.first);
}

// The writer is detached and owns what it uses, so nothing waits here at exit.
TypeDumper::~TypeDumper() = default;

std::uint64_t TypeDumper::NameValue(Address object) const {
    std::uint64_t name = 0;
    const std::int32_t offset = sources_.finder.Offsets().name;
    if (offset != kOffsetNotFound)
        sources_.finder.Reader().ReadTrusted(object + offset, &name, sizeof(name));
    return name;
}

// What an object is, judged by its class's cast flags; each class is asked once.
std::uint8_t TypeDumper::KindOf(Address object) {
    const std::int32_t offset = sources_.finder.Offsets().classPointer;
    Address meta = kNullAddress;
    if (offset == kOffsetNotFound || !sources_.finder.Reader().ReadTrusted(object + offset, &meta, sizeof(meta)) ||
        meta == kNullAddress)
        return 0;
    const auto [entry, added] = metaclasses_.try_emplace(meta, std::uint8_t{0});
    if (added) {
        const std::uint64_t flags =
            sources_.finder.Reader().ReadAs<std::uint64_t>(meta + sources_.structs.castFlags).value_or(0);
        if ((flags & kCastFlagClass) == kCastFlagClass)
            entry->second = kKindClass;
        else if (sources_.enums && (flags & kCastFlagEnum) == kCastFlagEnum)
            entry->second = kKindEnum;
        else if ((flags & kCastFlagScriptStruct) == kCastFlagScriptStruct)
            entry->second = kKindStruct;
    }
    return entry->second;
}

std::size_t TypeDumper::Scan(const std::string &label, const EnumNames *enums) {
    if (enums && !sources_.enums) {
        sources_.enums = enums;
        metaclasses_.clear();
    }
    const StructOffsets &structs = sources_.structs;
    if (structs.children == kOffsetNotFound || structs.fieldNext == kOffsetNotFound ||
        structs.castFlags == kOffsetNotFound)
        return 0;
    LARGE_INTEGER started{};
    QueryPerformanceCounter(&started);
    const bool wasIdle = queue_.empty();
    std::size_t queued = 0;
    sources_.finder.Objects().ForEach([&](std::int32_t, Address object) {
        const std::uint8_t kind = object == kNullAddress ? std::uint8_t{0} : KindOf(object);
        if (kind == 0)
            return true;
        const std::uint64_t name = NameValue(object);
        const auto [entry, added] = seen_.try_emplace(object);
        Seen &seen = entry->second;
        // An address freed and reused by another type carries another name.
        if (added || seen.name != name || seen.kind != kind) {
            const Named self = NameAndPackage(sources_.finder, object);
            seen = {name, kind, self.name.empty() || self.package.empty() ? "" : self.package + '\t' + self.name};
        }
        if (seen.key.empty() || !known_.insert(seen.key).second)
            return true;
        queue_.push_back({object, name, kind, seen.key});
        ++queued;
        return true;
    });
    const double scanMs = MillisecondsSince(started);
    if (queued == 0) {
        if (wasIdle && ready_.empty()) {
            char line[512];
            std::snprintf(line, sizeof(line), "[Unreal] types of %s dumped: 0 new (scan %.1fms), %zu known, in %s.",
                          label.c_str(), scanMs, known_.size(), path_.c_str());
            report_(line);
        }
        return 0;
    }
    if (wasIdle) {
        started_ = GetTickCount64();
        spentMs_ = 0;
        frames_ = 0;
    }
    spentMs_ += scanMs;
    label_ = label;
    return queued;
}

void TypeDumper::Step(double budgetMs) {
    if (queue_.empty()) {
        if (!ready_.empty())
            Flush();
        return;
    }
    LARGE_INTEGER started{};
    QueryPerformanceCounter(&started);
    ++frames_;
    double elapsed = 0;
    do {
        const Queued item = std::move(queue_.front());
        queue_.pop_front();
        // Unloaded before its turn: forget it, a later scan may queue it again.
        if (!IsLiveObject(sources_.finder, item.object) || NameValue(item.object) != item.name) {
            known_.erase(item.key);
        } else {
            const std::size_t tab = item.key.find('\t');
            const Named self{item.key.substr(tab + 1), item.key.substr(0, tab)};
            const TypeDumpSources &s = sources_;
            if (item.kind == kKindClass)
                ready_[item.key] = DumpClass(s.finder, s.structs, s.chain, s.values, s.functions, s.types,
                                             item.object, self);
            else if (item.kind == kKindStruct)
                ready_[item.key] = DumpStruct(s.finder, s.structs, s.chain, s.values, s.types, item.object, self);
            else
                ready_[item.key] = DumpEnum(*s.enums, item.object, self);
        }
        elapsed = MillisecondsSince(started);
    } while (!queue_.empty() && elapsed < budgetMs);
    spentMs_ += elapsed;
    if (queue_.empty())
        Flush();
}

// Merging and writing a file of megabytes happens off the game thread.
void TypeDumper::Flush() {
    if (busy_->load(std::memory_order_acquire))
        return;
    busy_->store(true, std::memory_order_release);
    char summary[512];
    std::snprintf(summary, sizeof(summary),
                  "[Unreal] types of %s dumped: %zu new over %d frames, %.0fms on the game thread, %llums in all",
                  label_.c_str(), ready_.size(), frames_, spentMs_, GetTickCount64() - started_);
    std::thread([file = file_, busy = busy_, blocks = std::move(ready_), path = path_, image = image_,
                 report = report_, summary = std::string(summary)]() mutable {
        for (auto &[key, block] : blocks)
            file->insert_or_assign(key, std::move(block));
        if (WriteAll(path, image, *file))
            report(summary + "; " + std::to_string(file->size()) + " types in " + path + ".");
        else
            report("[Unreal][ERROR] Could not write " + path + ".");
        busy->store(false, std::memory_order_release);
    }).detach();
    ready_.clear();
}

} // namespace URK::Unreal
