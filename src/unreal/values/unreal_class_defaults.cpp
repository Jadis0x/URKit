#include "unreal/values/unreal_class_defaults.h"

#include <charconv>
#include <cmath>
#include <cstring>

namespace URK::Unreal {
namespace {

constexpr int kMaxDepth = 6;
constexpr std::int32_t kMaxElements = 16;
constexpr std::int32_t kMaxNodes = 512;
constexpr std::size_t kMaxText = 160;
constexpr std::size_t kMaxLines = 600;
constexpr std::int32_t kMaxFields = 4096;
constexpr std::uint64_t kPropertyEdit = 0x1;
constexpr std::uint64_t kPropertyBlueprintVisible = 0x4;
constexpr std::uint64_t kPropertyTransient = 0x2000;
constexpr std::uint64_t kPropertyDeprecated = 0x20000000;

std::string Clean(std::string text) {
    for (char &ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '\r')
            ch = ' ';
    }
    if (text.size() > kMaxText)
        text = text.substr(0, kMaxText) + "...";
    return text;
}

// Fixed notation where it stays readable (300000, not 3e+05), shortest digits that read back the same.
template <typename T> std::string Number(T value) {
    char text[96];
    const double size = std::fabs(static_cast<double>(value));
    const auto result = size == 0 || (size >= 1e-4 && size < 1e15)
                            ? std::to_chars(text, text + sizeof(text), value, std::chars_format::fixed)
                            : std::to_chars(text, text + sizeof(text), value);
    return std::string(text, result.ptr);
}

bool PlainBytes(PropertyKind kind) {
    switch (kind) {
    case PropertyKind::Byte:
    case PropertyKind::Int8:
    case PropertyKind::Int16:
    case PropertyKind::Int32:
    case PropertyKind::Int64:
    case PropertyKind::UInt16:
    case PropertyKind::UInt32:
    case PropertyKind::UInt64:
    case PropertyKind::Float:
    case PropertyKind::Double:
    case PropertyKind::Enum:
    case PropertyKind::Name:
        return true;
    default:
        return false;
    }
}

bool ObjectKind(PropertyKind kind) {
    return kind == PropertyKind::Object || kind == PropertyKind::Class || kind == PropertyKind::WeakObject ||
           kind == PropertyKind::Interface;
}

bool AllZero(const std::uint8_t *bytes, std::int32_t size) {
    for (std::int32_t i = 0; i < size; ++i) {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

} // namespace

std::string ClassDefaults::Describe(Address classObject) {
    const TypeQueries &types = engine_->Types();
    lines_ = 0;
    visited_.clear();
    std::string out;
    const Address object = types.DefaultObjectOf(classObject);
    const Address super = types.SuperOf(classObject);
    const Address base = super != kNullAddress ? types.DefaultObjectOf(super) : kNullAddress;
    if (object != kNullAddress && base != kNullAddress && IsLiveObject(engine_->Finder(), object) &&
        IsLiveObject(engine_->Finder(), base))
        CompareObject(object, base, super, "", 0, Scope{"D\t", object, base}, out);
    // Components the Blueprint adds (construction script) first, then its changes to inherited ones.
    for (const auto &[name, component] : TemplatesOf(classObject))
        Component(classObject, component, name, out);
    return out;
}

// What a mod can see or edit: editor-visible or Blueprint-visible, not transient or deprecated.
const std::vector<ClassDefaults::Field> &ClassDefaults::FieldsOf(Address structObject) {
    const ObjectFinder &finder = engine_->Finder();
    const std::uint64_t name = finder.NameKeyOf(structObject).value_or(0);
    const Address outer = finder.OuterOf(structObject);
    Fields &entry = fields_[structObject];
    if (name != 0 && entry.name == name && entry.outer == outer)
        return entry.list;
    entry = Fields{name, outer, {}};
    const PropertyChain &chain = engine_->Chain();
    Address field = chain.First(structObject);
    for (std::int32_t step = 0; field != kNullAddress && step < kMaxFields; ++step, field = chain.Next(field)) {
        const std::optional<PropertyInfo> info = engine_->Values().Describe(field);
        std::optional<std::string> fieldName = chain.NameOf(field);
        if (!info || !info->Resolved() || !fieldName || *fieldName == "UberGraphFrame" ||
            !(info->propertyFlags & (kPropertyEdit | kPropertyBlueprintVisible)) ||
            (info->propertyFlags & (kPropertyTransient | kPropertyDeprecated)) || info->kind == PropertyKind::Unknown ||
            info->kind == PropertyKind::Delegate || info->kind == PropertyKind::MulticastDelegate ||
            info->kind == PropertyKind::SparseDelegate)
            continue;
        entry.list.push_back({*info, std::move(*fieldName)});
    }
    return entry.list;
}

void ClassDefaults::CompareObject(Address object, Address base, Address baseClass, const std::string &prefix,
                                  int depth, const Scope &scope, std::string &out) {
    const TypeQueries &types = engine_->Types();
    // Members of baseClass and its supers compare with base; the ones below it with zero.
    bool inherited = false;
    for (Address type = engine_->Finder().ClassOf(object); type != kNullAddress; type = types.SuperOf(type)) {
        inherited = inherited || type == baseClass;
        CompareFields(reinterpret_cast<std::uint8_t *>(object),
                      inherited && base != kNullAddress ? reinterpret_cast<std::uint8_t *>(base) : nullptr, object,
                      base, FieldsOf(type), prefix, depth, scope, out);
    }
}

void ClassDefaults::CompareFields(const std::uint8_t *value, const std::uint8_t *base, Address owner,
                                  Address baseOwner, const std::vector<Field> &fields, const std::string &prefix,
                                  int depth, const Scope &scope, std::string &out) {
    for (const Field &field : fields) {
        PropertyInfo one = field.info;
        one.arrayDim = 1;
        for (std::int32_t i = 0; i < field.info.arrayDim; ++i) {
            const std::size_t at =
                static_cast<std::size_t>(field.info.offset) + static_cast<std::size_t>(i) * field.info.elementSize;
            const PlaceTarget mine{const_cast<std::uint8_t *>(value) + at, one, false, owner};
            const PlaceTarget theirs{base ? const_cast<std::uint8_t *>(base) + at : nullptr, one, false, baseOwner};
            const std::string path =
                prefix + field.name + (field.info.arrayDim > 1 ? "[" + std::to_string(i) + "]" : "");
            CompareValue(mine, base ? &theirs : nullptr, path, depth, scope, out);
        }
    }
}

void ClassDefaults::CompareValue(const PlaceTarget &value, const PlaceTarget *base, const std::string &path,
                                 int depth, const Scope &scope, std::string &out) {
    const PropertyInfo &info = value.info;
    const ObjectFinder &finder = engine_->Finder();
    const TypeQueries &types = engine_->Types();
    if (info.kind == PropertyKind::Struct) {
        // Member by member, supers included, so only what changed shows.
        if (depth >= kMaxDepth)
            return;
        for (Address type = info.inner; type != kNullAddress; type = types.SuperOf(type))
            CompareFields(value.value, base ? base->value : nullptr, value.owner, base ? base->owner : kNullAddress,
                          FieldsOf(type), path + '.', depth + 1, scope, out);
        return;
    }
    if (info.kind == PropertyKind::Bool) {
        bool mine = false, theirs = false;
        if (!places_->ReadBool(value, &mine) || (base && !places_->ReadBool(*base, &theirs)) || mine == theirs)
            return;
        Emit(scope, path, mine ? "true" : "false", out);
        return;
    }
    if (ObjectKind(info.kind)) {
        const Address mine = places_->ReadObject(value, true);
        const Address theirs = base ? places_->ReadObject(*base, true) : kNullAddress;
        if (mine == theirs)
            return;
        // A default subobject (a component the constructor made): its own members, against the parent's one.
        if (mine != kNullAddress && finder.OuterOf(mine) == scope.owner && info.kind == PropertyKind::Object) {
            // RootComponent and the member that made it name one object: described once.
            if (!visited_.insert(mine).second || depth >= kMaxDepth)
                return;
            const Address mineClass = finder.ClassOf(mine);
            Address baseObject = kNullAddress;
            if (theirs != kNullAddress && finder.OuterOf(theirs) == scope.baseOwner &&
                finder.NameOf(theirs) == finder.NameOf(mine) && types.IsChildOf(mineClass, finder.ClassOf(theirs)))
                baseObject = theirs;
            else
                Emit(scope, path, ObjectText(mine, scope), out);
            const Address baseClass = baseObject != kNullAddress ? finder.ClassOf(baseObject) : mineClass;
            if (baseObject == kNullAddress)
                baseObject = types.DefaultObjectOf(mineClass);
            if (baseObject != kNullAddress)
                CompareObject(mine, baseObject, baseClass, path + '.', depth + 1, scope, out);
            return;
        }
        Emit(scope, path, ObjectText(mine, scope), out);
        return;
    }
    // Equal bytes are an equal value of any kind; only differing ones are rendered (texts, soft paths call the engine).
    const std::int32_t size = info.elementSize;
    if (base ? std::memcmp(value.value, base->value, static_cast<std::size_t>(size)) == 0 : AllZero(value.value, size))
        return;
    const std::optional<std::string> text = Render(value, depth, scope);
    if (!text)
        return;
    // Plain bytes that differ are a different value; others may hold equal values in other memory.
    if (base && !PlainBytes(info.kind) &&
        Render(*base, depth, Scope{scope.record, scope.baseOwner, scope.baseOwner}) == text)
        return;
    Emit(scope, path, *text, out);
}

std::optional<std::string> ClassDefaults::Render(const PlaceTarget &value, int depth, const Scope &scope) {
    const PropertyInfo &info = value.info;
    switch (info.kind) {
    case PropertyKind::Bool: {
        bool flag = false;
        return places_->ReadBool(value, &flag) ? std::optional<std::string>(flag ? "true" : "false") : std::nullopt;
    }
    case PropertyKind::Float:
    case PropertyKind::Double: {
        double number = 0;
        if (!places_->ReadFloating(value, &number))
            return std::nullopt;
        return info.kind == PropertyKind::Float ? Number(static_cast<float>(number)) : Number(number);
    }
    case PropertyKind::Byte:
    case PropertyKind::Enum:
        if (info.typeObject != kNullAddress) {
            if (std::optional<std::string> name = places_->ReadText(value, true))
                return std::string(EnumNames::ShortName(*name));
        }
        [[fallthrough]];
    case PropertyKind::Int8:
    case PropertyKind::Int16:
    case PropertyKind::Int32:
    case PropertyKind::Int64:
    case PropertyKind::UInt16:
    case PropertyKind::UInt32:
    case PropertyKind::UInt64: {
        std::int64_t number = 0;
        if (!places_->ReadInteger(value, &number))
            return std::nullopt;
        return std::to_string(number);
    }
    case PropertyKind::Name:
        return places_->ReadText(value, true);
    case PropertyKind::String:
    case PropertyKind::Utf8String:
    case PropertyKind::AnsiString:
    case PropertyKind::Text: {
        const std::optional<std::string> text = places_->ReadText(value, true);
        return text ? std::optional<std::string>('"' + Clean(*text) + '"') : std::nullopt;
    }
    case PropertyKind::SoftObject: {
        const std::optional<std::string> path = places_->ReadText(value, true);
        return path ? std::optional<std::string>(path->empty() ? "None" : *path) : std::nullopt;
    }
    case PropertyKind::Object:
    case PropertyKind::Class:
    case PropertyKind::WeakObject:
    case PropertyKind::Interface:
        return ObjectText(places_->ReadObject(value, true), scope);
    case PropertyKind::Struct: {
        if (depth >= kMaxDepth)
            return std::nullopt;
        std::string text = "(";
        for (Address type = info.inner; type != kNullAddress; type = engine_->Types().SuperOf(type)) {
            for (const Field &field : FieldsOf(type)) {
                PropertyInfo one = field.info;
                one.arrayDim = 1;
                const PlaceTarget inner{value.value + field.info.offset, one, false, value.owner};
                const std::optional<std::string> part = Render(inner, depth + 1, scope);
                text += (text.size() > 1 ? ", " : "") + field.name + '=' + part.value_or("?");
            }
        }
        return text + ')';
    }
    case PropertyKind::Array:
    case PropertyKind::Set:
    case PropertyKind::Map: {
        const std::int32_t count = places_->Count(value, true);
        if (count < 0)
            return std::nullopt;
        std::vector<std::int32_t> slots(static_cast<std::size_t>(std::min(count, kMaxElements)));
        if (info.kind == PropertyKind::Array) {
            for (std::size_t i = 0; i < slots.size(); ++i)
                slots[i] = static_cast<std::int32_t>(i);
        } else if (!slots.empty()) {
            std::vector<std::int32_t> all(static_cast<std::size_t>(count));
            const std::int32_t found = places_->Slots(value, all.data(), count);
            slots.assign(all.begin(), all.begin() + std::min<std::int32_t>(std::max(found, 0), kMaxElements));
        }
        const bool map = info.kind == PropertyKind::Map;
        std::string text = map ? "{" : "[";
        for (std::size_t i = 0; i < slots.size(); ++i) {
            text += i ? ", " : "";
            if (map) {
                const std::optional<PlaceTarget> key = Step(value, URK_UNREAL_STEP_KEY, slots[i]);
                const std::optional<PlaceTarget> entry = Step(value, URK_UNREAL_STEP_VALUE, slots[i]);
                const std::optional<std::string> k = key ? Render(*key, depth + 1, scope) : std::nullopt;
                const std::optional<std::string> v = entry ? Render(*entry, depth + 1, scope) : std::nullopt;
                text += k.value_or("?") + ": " + v.value_or("?");
            } else {
                const std::optional<PlaceTarget> element = Step(value, URK_UNREAL_STEP_ELEMENT, slots[i]);
                text += (element ? Render(*element, depth + 1, scope) : std::nullopt).value_or("?");
            }
        }
        if (count > static_cast<std::int32_t>(slots.size()))
            text += ", ... " + std::to_string(count) + " in all";
        return text + (map ? "}" : "]");
    }
    default:
        return std::nullopt;
    }
}

std::string ClassDefaults::ObjectText(Address object, const Scope &scope) const {
    const ObjectFinder &finder = engine_->Finder();
    if (object == kNullAddress)
        return "None";
    if (!IsLiveObject(finder, object))
        return "?";
    const std::string name = finder.NameOf(object).value_or("?");
    const Address outer = finder.OuterOf(object);
    if (outer != kNullAddress && (outer == scope.owner || outer == scope.baseOwner))
        return name;
    // Package.Object[:Subobject...], as the engine writes object paths.
    std::vector<std::string> names{name};
    for (Address at = outer; at != kNullAddress && names.size() < 8; at = finder.OuterOf(at))
        names.push_back(finder.NameOf(at).value_or("?"));
    std::string path = names.back();
    for (std::size_t i = names.size() - 1; i-- > 0;)
        path += (i + 2 == names.size() ? "." : ":") + names[i];
    return path;
}

std::optional<PlaceTarget> ClassDefaults::MemberOf(Address object, const char *name) const {
    if (object == kNullAddress)
        return std::nullopt;
    const Address field = engine_->Chain().FindMemberDeep(engine_->Finder().ClassOf(object), name);
    const std::optional<PropertyInfo> info = field != kNullAddress ? engine_->Values().Describe(field) : std::nullopt;
    if (!info || !info->Resolved())
        return std::nullopt;
    return PlaceTarget{reinterpret_cast<std::uint8_t *>(object) + info->offset, *info, false, object};
}

std::optional<PlaceTarget> ClassDefaults::Step(const PlaceTarget &from, std::int32_t kind, std::int32_t index,
                                               const char *name) const {
    const URK_UnrealStep step{kind, index, name};
    return places_->Walk(from.value, from.info, &step, 1, false, true);
}

void ClassDefaults::Emit(const Scope &scope, const std::string &path, const std::string &text, std::string &out) {
    if (++lines_ > kMaxLines)
        return;
    out += scope.record + path + '\t' + Clean(text) + '\n';
}

std::vector<std::pair<std::string, Address>> ClassDefaults::TemplatesOf(Address classObject) {
    std::vector<std::pair<std::string, Address>> found;
    const auto read = [&](const std::optional<PlaceTarget> &list, const char *component, const char *variable,
                          bool record) {
        if (!list)
            return;
        const std::int32_t count = std::min(places_->Count(*list, true), kMaxNodes);
        for (std::int32_t i = 0; i < count; ++i) {
            const std::optional<PlaceTarget> element = Step(*list, URK_UNREAL_STEP_ELEMENT, i);
            std::optional<PlaceTarget> object, name;
            if (element && record) {
                // FComponentOverrideRecord: the template, and the inherited node's key.
                object = Step(*element, URK_UNREAL_STEP_MEMBER, 0, component);
                const std::optional<PlaceTarget> key = Step(*element, URK_UNREAL_STEP_MEMBER, 0, "ComponentKey");
                name = key ? Step(*key, URK_UNREAL_STEP_MEMBER, 0, variable) : std::nullopt;
            } else if (element) {
                const Address node = places_->ReadObject(*element, true);
                object = MemberOf(node, component);
                name = MemberOf(node, variable);
            }
            const Address templateObject = object ? places_->ReadObject(*object, true) : kNullAddress;
            const std::optional<std::string> text = name ? places_->ReadText(*name, true) : std::nullopt;
            if (templateObject != kNullAddress && text)
                found.emplace_back(*text, templateObject);
        }
    };
    if (const std::optional<PlaceTarget> script = MemberOf(classObject, "SimpleConstructionScript"))
        read(MemberOf(places_->ReadObject(*script, true), "AllNodes"), "ComponentTemplate", "InternalVariableName",
             false);
    if (const std::optional<PlaceTarget> handler = MemberOf(classObject, "InheritableComponentHandler"))
        read(MemberOf(places_->ReadObject(*handler, true), "Records"), "ComponentTemplate", "SCSVariableName", true);
    return found;
}

// Against the nearest parent Blueprint's template of the same variable, else the component class's defaults.
void ClassDefaults::Component(Address classObject, Address object, const std::string &name, std::string &out) {
    const ObjectFinder &finder = engine_->Finder();
    const TypeQueries &types = engine_->Types();
    if (object == kNullAddress || !IsLiveObject(finder, object))
        return;
    const Address type = finder.ClassOf(object);
    Address base = kNullAddress;
    for (Address parent = types.SuperOf(classObject); parent != kNullAddress && base == kNullAddress;
         parent = types.SuperOf(parent)) {
        for (const auto &[parentName, parentTemplate] : TemplatesOf(parent)) {
            if (parentName == name && types.IsChildOf(type, finder.ClassOf(parentTemplate))) {
                base = parentTemplate;
                break;
            }
        }
    }
    const bool added = base == kNullAddress;
    if (added)
        base = types.DefaultObjectOf(type);
    if (base == kNullAddress)
        return;
    const Scope scope{"K\t" + Clean(name) + '\t' + finder.NameOf(type).value_or("?") + '\t', object, base};
    const std::size_t before = out.size();
    CompareObject(object, base, finder.ClassOf(base), "", 0, scope, out);
    // A component added here with nothing changed still says the Blueprint adds it.
    if (added && out.size() == before)
        Emit(scope, "", "", out);
}

} // namespace URK::Unreal
