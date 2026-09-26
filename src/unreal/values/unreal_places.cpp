#include "unreal/values/unreal_places.h"
#include "unreal/memory/unreal_text.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace URK::Unreal {
namespace {

constexpr int kMaxStructDepth = 16;
constexpr int kMaxStructFields = 4096;

template <typename T> T Load(const std::uint8_t *at) {
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

template <typename T> void Store(std::uint8_t *at, T value) { std::memcpy(at, &value, sizeof(T)); }

bool Live(const UnrealEngine &engine, Address object) {
    return engine.Available() && IsLiveObject(engine.Finder(), object);
}

bool IsStruct(const UnrealEngine &engine, Address object) {
    return Live(engine, object) && ObjectIs(engine.Finder(), engine.Structs(), object, kCastFlagStruct);
}

// Plain numbers: every bit pattern is a value the engine can hold.
bool FreeKind(PropertyKind kind) {
    switch (kind) {
    case PropertyKind::Bool:
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
        return true;
    default:
        return false;
    }
}

bool IntegerKind(PropertyKind kind) {
    return FreeKind(kind) && kind != PropertyKind::Bool && kind != PropertyKind::Float && kind != PropertyKind::Double;
}

bool StringKind(PropertyKind kind) {
    return kind == PropertyKind::String || kind == PropertyKind::Utf8String || kind == PropertyKind::AnsiString;
}

bool SoftClass(const PropertyInfo &info) { return (info.castFlags & kCastFlagSoftClassProperty) != 0; }

// Engine flag values (EPropertyFlags) that make two signatures differ.
constexpr std::uint64_t kSignatureFlags = kPropertyFlagParm | kPropertyFlagOutParm | kPropertyFlagReturnParm;

bool CheckName(void *context, const std::uint8_t *name, std::size_t size) {
    return static_cast<EngineCalls *>(context)->ValidName(name, size);
}

// An FString the engine can copy from: its characters readable and terminated.
bool CopyableString(const UnrealEngine &engine, const std::uint8_t *header) {
    const auto data = reinterpret_cast<Address>(Load<const std::uint8_t *>(header));
    const std::int32_t num = Load<std::int32_t>(header + 8);
    const std::int32_t max = Load<std::int32_t>(header + 12);
    if (num == 0)
        return true;
    const std::size_t bytes = static_cast<std::size_t>(num) * sizeof(char16_t);
    return num > 0 && max >= num && num <= kMaxContainerElements && data != kNullAddress &&
           engine.Reader().Readable(data, bytes) &&
           engine.Reader().ReadAs<std::uint16_t>(data + bytes - sizeof(char16_t)) == 0;
}

} // namespace

bool Assignable(const UnrealEngine &engine, const PropertyInfo &info, Address value) {
    if (value == kNullAddress)
        return true;
    if (!Live(engine, value))
        return false;
    if (info.kind == PropertyKind::Class || (info.kind == PropertyKind::SoftObject && SoftClass(info)))
        return ObjectIs(engine.Finder(), engine.Structs(), value, kCastFlagClass);
    if (info.kind == PropertyKind::Interface)
        return engine.Types().InterfaceAddress(value, info.inner).has_value();
    return info.inner != kNullAddress && engine.Types().IsA(value, info.inner);
}

bool StructChangeAllowed(const UnrealEngine &engine, Address structObject, const std::uint8_t *current,
                         const std::uint8_t *proposed, std::size_t size, int depth, NameCheck names,
                         void *namesContext, bool strings) {
    if (depth > kMaxStructDepth || !IsStruct(engine, structObject))
        return false;
    const PropertyChain &chain = engine.Chain();
    int level = 0;
    for (Address owner = structObject; owner != kNullAddress && level < kMaxStructDepth;
         owner = engine.Types().SuperOf(owner), ++level) {
        Address field = chain.First(owner);
        for (int step = 0; field != kNullAddress && step < kMaxStructFields; ++step, field = chain.Next(field)) {
            const std::optional<PropertyInfo> info = engine.Values().Describe(field);
            if (!info || !info->Resolved() || info->offset < 0 || info->elementSize <= 0 || info->arrayDim < 1)
                return false;
            const std::size_t width = static_cast<std::size_t>(info->elementSize);
            for (std::int32_t i = 0; i < info->arrayDim; ++i) {
                const std::size_t at = static_cast<std::size_t>(info->offset) + static_cast<std::size_t>(i) * width;
                if (at + width > size)
                    return false;
                if (std::memcmp(current + at, proposed + at, width) == 0 || FreeKind(info->kind))
                    continue;
                if (info->kind == PropertyKind::Object || info->kind == PropertyKind::Class) {
                    Address value = kNullAddress;
                    if (width != sizeof(value))
                        return false;
                    std::memcpy(&value, proposed + at, sizeof(value));
                    if (!Assignable(engine, *info, value))
                        return false;
                } else if (info->kind == PropertyKind::Name) {
                    if (!names || !names(namesContext, proposed + at, width))
                        return false;
                } else if (info->kind == PropertyKind::String && strings) {
                    if (!CopyableString(engine, proposed + at))
                        return false;
                } else if (info->kind != PropertyKind::Struct ||
                           !StructChangeAllowed(engine, info->inner, current + at, proposed + at, width, depth + 1,
                                                names, namesContext, strings)) {
                    return false;
                }
            }
        }
    }
    return true;
}

void MergeStructMembers(const UnrealEngine &engine, Address structObject, std::uint8_t *merged,
                        const std::uint8_t *proposed, std::size_t size, int depth) {
    if (depth > kMaxStructDepth)
        return;
    const PropertyChain &chain = engine.Chain();
    int level = 0;
    for (Address owner = structObject; owner != kNullAddress && level < kMaxStructDepth;
         owner = engine.Types().SuperOf(owner), ++level) {
        Address field = chain.First(owner);
        for (int step = 0; field != kNullAddress && step < kMaxStructFields; ++step, field = chain.Next(field)) {
            const std::optional<PropertyInfo> info = engine.Values().Describe(field);
            if (!info || !info->Resolved() || info->offset < 0 || info->elementSize <= 0 || info->arrayDim < 1)
                continue;
            const std::size_t width = static_cast<std::size_t>(info->elementSize);
            for (std::int32_t i = 0; i < info->arrayDim; ++i) {
                const std::size_t at = static_cast<std::size_t>(info->offset) + static_cast<std::size_t>(i) * width;
                if (at + width > size)
                    continue;
                if (info->kind == PropertyKind::Bool) {
                    const std::size_t byte = at + info->boolLayout.byteOffset;
                    const std::uint8_t mask = info->boolLayout.fieldMask;
                    if (byte < size)
                        merged[byte] = static_cast<std::uint8_t>((merged[byte] & ~mask) | (proposed[byte] & mask));
                } else if (info->kind == PropertyKind::Struct) {
                    MergeStructMembers(engine, info->inner, merged + at, proposed + at, width, depth + 1);
                } else {
                    std::memcpy(merged + at, proposed + at, width);
                }
            }
        }
    }
}

bool Places::Fail(std::string why) {
    failure_ = std::move(why);
    return false;
}

bool Places::NeedGameThread(bool gameThread) {
    return gameThread ? true : Fail("changing engine memory needs the game thread");
}

bool Places::ArrayHeader(const std::uint8_t *value, std::int32_t elementSize, std::int32_t *num) {
    const auto data = reinterpret_cast<Address>(Load<const std::uint8_t *>(value));
    *num = Load<std::int32_t>(value + 8);
    const std::int32_t max = Load<std::int32_t>(value + 12);
    if (*num < 0 || max < *num || *num > kMaxContainerElements || elementSize <= 0)
        return Fail("the array header is not one");
    if (*num > 0 && (data == kNullAddress ||
                     !engine_->Reader().Readable(data, static_cast<std::size_t>(*num) * elementSize)))
        return Fail("the array's elements are not readable");
    return true;
}

std::optional<PropertyInfo> Places::ElementOf(const PropertyInfo &container) {
    if (container.kind == PropertyKind::Array) {
        std::optional<PropertyInfo> inner = engine_->Values().Describe(container.inner);
        if (!inner || inner->elementSize <= 0) {
            Fail("the array's element type did not resolve");
            return std::nullopt;
        }
        inner->offset = 0;
        return inner;
    }
    if (container.kind == PropertyKind::MulticastDelegate || container.kind == PropertyKind::SparseDelegate) {
        PropertyInfo element = owned_->DelegateElement();
        if (element.elementSize <= 0) {
            Fail("no reflected delegate names FScriptDelegate's size");
            return std::nullopt;
        }
        element.typeObject = container.typeObject;
        return element;
    }
    Fail("the value is not an array");
    return std::nullopt;
}

std::optional<PlaceTarget> Places::Walk(std::uint8_t *root, const PropertyInfo &rootInfo, const URK_UnrealStep *steps,
                                        std::uint32_t count, bool describe, bool gameThread) {
    PlaceTarget target{root, rootInfo};
    if (count > URK_UNREAL_PLACE_MAX_STEPS) {
        Fail("too many steps");
        return std::nullopt;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const URK_UnrealStep &step = steps[i];
        PropertyInfo &info = target.info;
        switch (step.kind) {
        case URK_UNREAL_STEP_MEMBER: {
            if (info.kind != PropertyKind::Struct || !step.name) {
                Fail("a member step needs a struct");
                return std::nullopt;
            }
            const Address field = engine_->Chain().FindMemberDeep(info.inner, step.name);
            const std::optional<PropertyInfo> member =
                field != kNullAddress ? engine_->Values().Describe(field) : std::nullopt;
            if (!member || !member->Resolved() || step.index < 0 || step.index >= member->arrayDim ||
                member->offset + member->elementSize * member->arrayDim > info.elementSize) {
                Fail(std::string("no member ") + step.name + " at that index");
                return std::nullopt;
            }
            if (target.value)
                target.value += member->offset + static_cast<std::size_t>(step.index) * member->elementSize;
            target.info = *member;
            break;
        }
        case URK_UNREAL_STEP_ELEMENT: {
            if (info.kind == PropertyKind::Set) {
                const std::optional<SetLayout> layout = owned_->Stores().LayoutOf(info);
                if (!layout) {
                    Fail(owned_->Stores().Failure());
                    return std::nullopt;
                }
                std::uint8_t *element = nullptr;
                if (target.value && !(describe && step.index == -1)) {
                    element = Containers::Element(*layout, target.value, step.index);
                    if (!element) {
                        Fail("the set slot is not occupied");
                        return std::nullopt;
                    }
                }
                target.value = element;
                target.info = layout->key;
                target.key = true;
                break;
            }
            if (info.kind == PropertyKind::SparseDelegate && target.value && !(describe && step.index == -1)) {
                const std::uint8_t *list = SparseList(target, gameThread);
                if (!list) {
                    Fail(failure_.empty() ? "the element index is out of range" : failure_);
                    return std::nullopt;
                }
                target.value = const_cast<std::uint8_t *>(list);
                target.info.kind = PropertyKind::MulticastDelegate;
            }
            const std::optional<PropertyInfo> element = ElementOf(target.info);
            if (!element)
                return std::nullopt;
            std::uint8_t *at = nullptr;
            if (target.value && !(describe && step.index == -1)) {
                std::int32_t num = 0;
                if (!ArrayHeader(target.value, element->elementSize, &num))
                    return std::nullopt;
                if (step.index < 0 || step.index >= num) {
                    Fail("the element index is out of range");
                    return std::nullopt;
                }
                at = Load<std::uint8_t *>(target.value) + static_cast<std::size_t>(step.index) * element->elementSize;
            }
            target.value = at;
            target.info = *element;
            break;
        }
        case URK_UNREAL_STEP_KEY:
        case URK_UNREAL_STEP_VALUE: {
            if (info.kind != PropertyKind::Map) {
                Fail("a key or value step needs a map");
                return std::nullopt;
            }
            const std::optional<SetLayout> layout = owned_->Stores().LayoutOf(info);
            if (!layout) {
                Fail(owned_->Stores().Failure());
                return std::nullopt;
            }
            const bool isValue = step.kind == URK_UNREAL_STEP_VALUE;
            std::uint8_t *element = nullptr;
            if (target.value && !(describe && step.index == -1)) {
                element = Containers::Element(*layout, target.value, step.index);
                if (!element) {
                    Fail("the map slot is not occupied");
                    return std::nullopt;
                }
                if (isValue)
                    element += layout->valueOffset;
            }
            target.value = element;
            target.info = isValue ? layout->value : layout->key;
            target.key = !isValue;
            target.info.offset = 0;
            break;
        }
        default:
            Fail("unknown step kind");
            return std::nullopt;
        }
        if (!target.value && !describe) {
            Fail("the place names no value");
            return std::nullopt;
        }
    }
    return target;
}

// --- numbers ------------------------------------------------------------------------

bool Places::ReadInteger(const PlaceTarget &target, std::int64_t *output) {
    const PropertyInfo &info = target.info;
    if (!target.value || !IntegerKind(info.kind))
        return Fail("not an integer");
    const std::uint8_t *at = target.value;
    switch (info.kind) {
    case PropertyKind::Int8:
        *output = Load<std::int8_t>(at);
        return true;
    case PropertyKind::Int16:
        *output = Load<std::int16_t>(at);
        return true;
    case PropertyKind::Int32:
        *output = Load<std::int32_t>(at);
        return true;
    case PropertyKind::Int64:
    case PropertyKind::UInt64:
        *output = Load<std::int64_t>(at);
        return true;
    case PropertyKind::Byte:
        *output = Load<std::uint8_t>(at);
        return true;
    case PropertyKind::UInt16:
        *output = Load<std::uint16_t>(at);
        return true;
    case PropertyKind::UInt32:
        *output = Load<std::uint32_t>(at);
        return true;
    case PropertyKind::Enum:
        switch (info.elementSize) {
        case 1:
            *output = Load<std::uint8_t>(at);
            return true;
        case 2:
            *output = Load<std::uint16_t>(at);
            return true;
        case 4:
            *output = Load<std::int32_t>(at);
            return true;
        case 8:
            *output = Load<std::int64_t>(at);
            return true;
        default:
            return Fail("an enum of unexpected width");
        }
    default:
        return Fail("not an integer");
    }
}

bool Places::WriteInteger(const PlaceTarget &target, std::int64_t value) {
    const PropertyInfo &info = target.info;
    if (!target.value || !IntegerKind(info.kind))
        return Fail("not an integer");
    const std::int32_t size = info.kind == PropertyKind::Byte || info.kind == PropertyKind::Int8 ? 1 : info.elementSize;
    if (size != 1 && size != 2 && size != 4 && size != 8)
        return Fail("an integer of unexpected width");
    std::memcpy(target.value, &value, static_cast<std::size_t>(size));
    return true;
}

bool Places::ReadFloating(const PlaceTarget &target, double *output) {
    if (!target.value)
        return Fail("no value");
    if (target.info.kind == PropertyKind::Float) {
        *output = Load<float>(target.value);
        return true;
    }
    if (target.info.kind == PropertyKind::Double) {
        *output = Load<double>(target.value);
        return true;
    }
    return Fail("not a floating point number");
}

bool Places::WriteFloating(const PlaceTarget &target, double value) {
    if (!target.value)
        return Fail("no value");
    if (target.info.kind == PropertyKind::Float) {
        Store<float>(target.value, static_cast<float>(value));
        return true;
    }
    if (target.info.kind == PropertyKind::Double) {
        Store<double>(target.value, value);
        return true;
    }
    return Fail("not a floating point number");
}

bool Places::ReadBool(const PlaceTarget &target, bool *output) {
    const BoolLayout &layout = target.info.boolLayout;
    if (!target.value || target.info.kind != PropertyKind::Bool || layout.fieldMask == 0)
        return Fail("not a bool");
    *output = (target.value[layout.byteOffset] & layout.fieldMask) != 0;
    return true;
}

bool Places::WriteBool(const PlaceTarget &target, bool value) {
    const BoolLayout &layout = target.info.boolLayout;
    if (!target.value || target.info.kind != PropertyKind::Bool || layout.fieldMask == 0)
        return Fail("not a bool");
    std::uint8_t &byte = target.value[layout.byteOffset];
    // FBoolProperty::SetPropertyValue: clear FieldMask, set ByteMask.
    byte = static_cast<std::uint8_t>((byte & ~layout.fieldMask) | (value ? layout.byteMask : 0));
    return true;
}

// --- references --------------------------------------------------------------------------

Address Places::ReadObject(const PlaceTarget &target, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value) {
        Fail("no value");
        return kNullAddress;
    }
    EngineCalls &calls = owned_->Engine();
    switch (info.kind) {
    case PropertyKind::Object:
    case PropertyKind::Class:
    case PropertyKind::Interface:
        return Load<Address>(target.value);
    case PropertyKind::WeakObject:
    case PropertyKind::LazyObject:
    case PropertyKind::Delegate:
        if (!gameThread && !calls.WeakReady()) {
            Fail("weak references are measured on the game thread first");
            return kNullAddress;
        }
        return calls.WeakTarget(target.value);
    case PropertyKind::SoftObject:
        if (!NeedGameThread(gameThread))
            return kNullAddress;
        return calls.SoftTarget(target.value, static_cast<std::size_t>(info.elementSize), SoftClass(info));
    default:
        Fail("not a reference");
        return kNullAddress;
    }
}

bool Places::WriteObject(const PlaceTarget &target, Address value, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value)
        return Fail("no value");
    if (!Assignable(*engine_, info, value))
        return Fail("the object is not live or not of the declared class");
    EngineCalls &calls = owned_->Engine();
    switch (info.kind) {
    case PropertyKind::Object:
    case PropertyKind::Class:
        Store<Address>(target.value, value);
        return true;
    case PropertyKind::Interface: {
        // FScriptInterface: the object, then its interface's address.
        if (info.elementSize != 16)
            return Fail("interfaces here are not the plain object-and-address layout");
        const Address address = value ? engine_->Types().InterfaceAddress(value, info.inner).value_or(0) : 0;
        Store<Address>(target.value, value);
        Store<Address>(target.value + sizeof(Address), address);
        return true;
    }
    case PropertyKind::WeakObject: {
        if (!NeedGameThread(gameThread))
            return false;
        std::uint8_t weak[EngineCalls::kWeakSize];
        if (!calls.MakeWeak(value, weak))
            return Fail(calls.Failure());
        std::memcpy(target.value, weak, sizeof(weak));
        return true;
    }
    case PropertyKind::SoftObject:
        if (!NeedGameThread(gameThread))
            return false;
        return calls.AssignSoftObject(target.value, static_cast<std::size_t>(info.elementSize), value, SoftClass(info))
                   ? true
                   : Fail(calls.Failure());
    case PropertyKind::LazyObject:
        // Reset(): no object, no GUID. An object's GUID is only the engine's to make.
        if (value != kNullAddress)
            return Fail("a lazy pointer takes an object's GUID: copy it from another lazy reference with write_bytes");
        std::memset(target.value, 0, static_cast<std::size_t>(info.elementSize));
        return true;
    case PropertyKind::Delegate:
        return Fail("a delegate is bound with an object and a function together");
    default:
        return Fail("not a writable reference");
    }
}

// --- text ------------------------------------------------------------------------------------

std::optional<std::string> Places::ReadText(const PlaceTarget &target, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value) {
        Fail("no value");
        return std::nullopt;
    }
    EngineCalls &calls = owned_->Engine();
    if (StringKind(info.kind))
        return engine_->Values().ReadStringAt(reinterpret_cast<Address>(target.value), info.kind);
    switch (info.kind) {
    case PropertyKind::Name:
        return engine_->Finder().Names().ReadFName(reinterpret_cast<Address>(target.value));
    case PropertyKind::Delegate:
        return engine_->Finder().Names().ReadFName(reinterpret_cast<Address>(target.value + EngineCalls::kWeakSize));
    case PropertyKind::Text:
        if (!NeedGameThread(gameThread))
            return std::nullopt;
        if (std::optional<std::string> text = calls.TextToString(target.value))
            return text;
        Fail(calls.Failure());
        return std::nullopt;
    case PropertyKind::SoftObject:
        if (!NeedGameThread(gameThread))
            return std::nullopt;
        if (std::optional<std::string> path =
                calls.SoftPath(target.value, static_cast<std::size_t>(info.elementSize), SoftClass(info)))
            return path;
        Fail(calls.Failure());
        return std::nullopt;
    case PropertyKind::Enum:
    case PropertyKind::Byte: {
        if (info.typeObject == kNullAddress) {
            Fail("a plain byte has no names");
            return std::nullopt;
        }
        if (!enums_->Measured() && !(gameThread && enums_->Measure(calls))) {
            Fail("enum names are unavailable (" + enums_->Failure() + ")");
            return std::nullopt;
        }
        std::int64_t value = 0;
        if (!ReadInteger(target, &value))
            return std::nullopt;
        if (std::optional<std::string> name = enums_->NameOf(info.typeObject, value))
            return name;
        Fail("the value has no name in its enum");
        return std::nullopt;
    }
    default:
        Fail("the value has no text form");
        return std::nullopt;
    }
}

bool Places::WriteText(const PlaceTarget &target, const char *utf8, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value || !utf8)
        return Fail("no value");
    EngineCalls &calls = owned_->Engine();
    const std::string_view text(utf8);
    if (info.kind == PropertyKind::Enum || info.kind == PropertyKind::Byte) {
        if (info.typeObject == kNullAddress)
            return Fail("a plain byte has no names");
        if (!enums_->Measured() && !(gameThread && enums_->Measure(calls)))
            return Fail("enum names are unavailable (" + enums_->Failure() + ")");
        const std::optional<std::int64_t> value = enums_->ValueOf(info.typeObject, text);
        return value ? WriteInteger(target, *value) : Fail("the enum has no value named " + std::string(text));
    }
    if (!NeedGameThread(gameThread))
        return false;
    const std::u16string wide = Utf8ToUtf16(text);
    bool done = false;
    switch (info.kind) {
    case PropertyKind::Name:
        done = calls.MakeName(wide, target.value, static_cast<std::size_t>(info.elementSize));
        break;
    case PropertyKind::String:
        done = calls.AssignChars(target.value, wide.data(), wide.size(), sizeof(char16_t));
        break;
    case PropertyKind::Utf8String:
        done = calls.AssignChars(target.value, text.data(), text.size(), 1);
        break;
    case PropertyKind::AnsiString: {
        std::string latin(wide.size(), '?');
        for (std::size_t i = 0; i < wide.size(); ++i)
            latin[i] = wide[i] < 0x100 ? static_cast<char>(wide[i]) : '?';
        done = calls.AssignChars(target.value, latin.data(), latin.size(), 1);
        break;
    }
    case PropertyKind::Text:
        done = calls.AssignText(target.value, wide);
        break;
    case PropertyKind::SoftObject:
        done = calls.AssignSoftPath(target.value, static_cast<std::size_t>(info.elementSize), wide, SoftClass(info));
        break;
    default:
        return Fail("the value has no text form");
    }
    return done ? true : Fail(calls.Failure());
}

// --- structs as bytes ---------------------------------------------------------------------------

bool Places::ReadBytes(const PlaceTarget &target, void *output, std::size_t size) {
    if (!target.value || (target.info.kind != PropertyKind::Struct && target.info.kind != PropertyKind::LazyObject) ||
        size != static_cast<std::size_t>(target.info.elementSize))
        return Fail("not a struct of that size");
    std::memcpy(output, target.value, size);
    return true;
}

bool Places::WriteBytes(const PlaceTarget &target, const void *value, std::size_t size, bool gameThread) {
    // A lazy pointer's whole value (weak pointer and GUID), taken from another one.
    if (target.value && target.info.kind == PropertyKind::LazyObject &&
        size == static_cast<std::size_t>(target.info.elementSize) && size >= EngineCalls::kWeakSize) {
        if (!NeedGameThread(gameThread))
            return false;
        const auto *bytes = static_cast<const std::uint8_t *>(value);
        // A null weak part: index 0 in UE5, INDEX_NONE in UE4, serial 0 in both.
        const std::int32_t weakIndex = Load<std::int32_t>(bytes);
        const bool empty = Load<std::int32_t>(bytes + 4) == 0 && (weakIndex == 0 || weakIndex == -1);
        EngineCalls &calls = owned_->Engine();
        if (!empty && calls.WeakTarget(bytes) == kNullAddress)
            return Fail("the lazy pointer's weak part names no live object");
        std::memcpy(target.value, value, size);
        return true;
    }
    if (!target.value || target.info.kind != PropertyKind::Struct ||
        size != static_cast<std::size_t>(target.info.elementSize))
        return Fail("not a struct of that size");
    // Names are confirmed through the engine, so only on the game thread.
    if (!StructChangeAllowed(*engine_, target.info.inner, target.value, static_cast<const std::uint8_t *>(value),
                             size, 0, gameThread ? &CheckName : nullptr, &owned_->Engine()))
        return Fail("the struct write would change what the engine owns");
    std::vector<std::uint8_t> merged(target.value, target.value + size);
    MergeStructMembers(*engine_, target.info.inner, merged.data(), static_cast<const std::uint8_t *>(value), size);
    std::memcpy(target.value, merged.data(), size);
    return true;
}

// --- containers -------------------------------------------------------------------------------------

std::int32_t Places::Count(const PlaceTarget &target, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value)
        return -1;
    if (info.kind == PropertyKind::SparseDelegate) {
        failure_.clear();
        const std::uint8_t *list = SparseList(target, gameThread);
        std::int32_t num = 0;
        if (!list)
            return failure_.empty() ? 0 : -1;
        return ArrayHeader(list, owned_->DelegateElement().elementSize, &num) ? num : -1;
    }
    if (info.kind == PropertyKind::Set || info.kind == PropertyKind::Map) {
        const std::int32_t num = Load<std::int32_t>(target.value + SetFields::kNum);
        const std::int32_t free = Load<std::int32_t>(target.value + SetFields::kNumFree);
        return num >= 0 && free >= 0 && free <= num ? num - free : -1;
    }
    const std::optional<PropertyInfo> element = ElementOf(info);
    std::int32_t num = 0;
    return element && ArrayHeader(target.value, element->elementSize, &num) ? num : -1;
}

std::int32_t Places::Slots(const PlaceTarget &target, std::int32_t *output, std::int32_t capacity) {
    const PropertyInfo &info = target.info;
    if (!target.value)
        return -1;
    std::vector<std::int32_t> slots;
    if (info.kind == PropertyKind::Set || info.kind == PropertyKind::Map) {
        const std::optional<SetLayout> layout = owned_->Stores().LayoutOf(info);
        if (!layout || !owned_->Stores().Validate(*layout, target.value))
            return Fail(owned_->Stores().Failure()), -1;
        slots = Containers::Slots(target.value);
    } else {
        const std::int32_t count = Count(target);
        if (count < 0)
            return -1;
        for (std::int32_t i = 0; i < count; ++i)
            slots.push_back(i);
    }
    if (output) {
        for (std::size_t i = 0; i < slots.size() && static_cast<std::int32_t>(i) < capacity; ++i)
            output[i] = slots[i];
    }
    return static_cast<std::int32_t>(slots.size());
}

bool Places::Insert(const PlaceTarget &target, std::int32_t index, std::int32_t count, bool gameThread) {
    if (!target.value || !NeedGameThread(gameThread))
        return false;
    if (target.info.kind == PropertyKind::SparseDelegate)
        return Fail("a sparse delegate gains bindings through bind");
    const std::optional<PropertyInfo> element = ElementOf(target.info);
    if (!element)
        return false;
    if (target.info.kind == PropertyKind::MulticastDelegate && owned_->Classify(*element) != Ownership::None)
        return Fail("delegates here are not the plain weak-object-and-name layout");
    return owned_->Stores().ArrayInsert(*element, target.value, index, count) ? true
                                                                               : Fail(owned_->Stores().Failure());
}

bool Places::Remove(const PlaceTarget &target, std::int32_t index, std::int32_t count, bool gameThread) {
    if (!target.value || !NeedGameThread(gameThread))
        return false;
    const PropertyInfo &info = target.info;
    Containers &stores = owned_->Stores();
    if (info.kind == PropertyKind::Set || info.kind == PropertyKind::Map) {
        const std::optional<SetLayout> layout = stores.LayoutOf(info);
        if (!layout)
            return Fail(stores.Failure());
        if (count != 1)
            return Fail("a set or map slot is removed one at a time");
        return stores.Remove(*layout, target.value, index) ? true : Fail(stores.Failure());
    }
    if (info.kind == PropertyKind::SparseDelegate) {
        const std::int32_t size = owned_->DelegateElement().elementSize;
        const std::uint8_t *list = SparseList(target, gameThread);
        std::int32_t num = 0;
        if (!list || !ArrayHeader(list, size, &num) || index < 0 || count < 0 || index + count > num)
            return Fail("the element index is out of range");
        // Copied first: each removal may rebuild the list.
        std::vector<std::uint8_t> removed(Load<const std::uint8_t *>(list) + static_cast<std::size_t>(index) * size,
                                          Load<const std::uint8_t *>(list) +
                                              static_cast<std::size_t>(index + count) * size);
        for (std::int32_t i = 0; i < count; ++i) {
            if (!delegates_.Remove(info, target.owner, target.value, removed.data() + static_cast<std::size_t>(i) * size))
                return Fail(delegates_.Failure());
        }
        return true;
    }
    const std::optional<PropertyInfo> element = ElementOf(info);
    if (!element)
        return false;
    return stores.ArrayRemove(*element, target.value, index, count) ? true : Fail(stores.Failure());
}

bool Places::Clear(const PlaceTarget &target, bool gameThread) {
    if (!target.value || !NeedGameThread(gameThread))
        return false;
    const PropertyInfo &info = target.info;
    switch (info.kind) {
    case PropertyKind::Text:
        return owned_->Engine().MakeEmptyText(target.value) ? true : Fail(owned_->Engine().Failure());
    case PropertyKind::Delegate:
        if (info.elementSize != 16)
            return Fail("delegates here are not the plain weak-object-and-name layout");
        std::memset(target.value, 0, 16);
        return true;
    case PropertyKind::SparseDelegate:
        if (!delegates_.Ready())
            return Fail(delegates_.Failure());
        return delegates_.Clear(info, target.owner, target.value) ? true : Fail("the sparse delegate has no owner");
    case PropertyKind::String:
    case PropertyKind::Utf8String:
    case PropertyKind::AnsiString:
    case PropertyKind::Array:
    case PropertyKind::Set:
    case PropertyKind::Map:
    case PropertyKind::MulticastDelegate:
    case PropertyKind::SoftObject:
        return owned_->Destroy(info, target.value) ? true : Fail(owned_->Failure());
    default:
        return Fail("the value is not something that empties");
    }
}

bool Places::BuildKey(const PropertyInfo &key, const URK_UnrealKey &input, bool forAdd,
                      std::vector<std::uint8_t> *bytes, std::u16string *text) {
    bytes->assign(static_cast<std::size_t>(key.elementSize), 0);
    std::uint8_t *at = bytes->data();
    EngineCalls &calls = owned_->Engine();
    switch (key.kind) {
    case PropertyKind::Bool:
        at[key.boolLayout.byteOffset] = input.integer ? key.boolLayout.byteMask : 0;
        return true;
    case PropertyKind::Enum:
    case PropertyKind::Byte:
        if (input.text && key.typeObject != kNullAddress) {
            if (!enums_->Measured() && !enums_->Measure(calls))
                return Fail("enum names are unavailable (" + enums_->Failure() + ")");
            const std::optional<std::int64_t> value = enums_->ValueOf(key.typeObject, input.text);
            if (!value)
                return Fail("the enum has no value named " + std::string(input.text));
            std::memcpy(at, &*value, std::min<std::size_t>(bytes->size(), 8));
            return true;
        }
        [[fallthrough]];
    case PropertyKind::Int8:
    case PropertyKind::Int16:
    case PropertyKind::Int32:
    case PropertyKind::Int64:
    case PropertyKind::UInt16:
    case PropertyKind::UInt32:
    case PropertyKind::UInt64:
        std::memcpy(at, &input.integer, std::min<std::size_t>(bytes->size(), 8));
        return true;
    case PropertyKind::Float:
        Store<float>(at, static_cast<float>(input.floating));
        return true;
    case PropertyKind::Double:
        Store<double>(at, input.floating);
        return true;
    case PropertyKind::Name:
        if (!input.text)
            return Fail("a name key needs text");
        return calls.MakeName(Utf8ToUtf16(input.text), at, bytes->size()) ? true : Fail(calls.Failure());
    case PropertyKind::String: {
        if (!input.text)
            return Fail("a string key needs text");
        *text = Utf8ToUtf16(input.text);
        if (forAdd)
            return calls.AssignChars(at, text->data(), text->size(), sizeof(char16_t)) ? true : Fail(calls.Failure());
        // A stand-in pointing at our own characters: compared, never stored.
        text->push_back(u'\0');
        Store<const char16_t *>(at, text->data());
        Store<std::int32_t>(at + 8, static_cast<std::int32_t>(text->size()));
        Store<std::int32_t>(at + 12, static_cast<std::int32_t>(text->size()));
        return true;
    }
    case PropertyKind::Object:
    case PropertyKind::Class:
        if (forAdd && !Assignable(*engine_, key, input.object))
            return Fail("the object is not live or not of the declared class");
        Store<Address>(at, input.object);
        return true;
    case PropertyKind::Struct: {
        if (!input.bytes || input.size != bytes->size())
            return Fail("a struct key needs its whole value");
        const auto *proposed = static_cast<const std::uint8_t *>(input.bytes);
        // Keys may carry strings; the stored key is the engine's copy, not the mod's buffer.
        PropertyVirtuals &virtuals = owned_->Stores().Virtuals();
        const bool engineMade = key.arrayDim == 1 && virtuals.ValueOpsReady();
        if (!StructChangeAllowed(*engine_, key.inner, at, proposed, bytes->size(), 0, &CheckName, &calls, engineMade))
            return Fail(engineMade ? "a struct key may carry numbers, objects, names and strings, not other "
                                     "engine-owned values"
                                   : "a struct key may carry numbers, objects and names, not engine-owned values");
        if (engineMade) {
            // A stand-in for a lookup is only compared, never stored.
            if (!forAdd) {
                std::memcpy(at, proposed, bytes->size());
                return true;
            }
            if (virtuals.Initialize(key, at) && virtuals.Copy(key, at, proposed))
                return true;
            owned_->Destroy(key, at);
            return Fail("the engine could not make the struct key");
        }
        std::memcpy(at, proposed, bytes->size());
        if (forAdd && !owned_->Initialize(key, at)) {
            // Texts made before the failure go back with the rest.
            const std::string why = owned_->Failure();
            owned_->Destroy(key, at);
            return Fail(why);
        }
        return true;
    }
    default:
        return Fail(std::string("a ") + PropertyKindName(key.kind) + " key is not supported");
    }
}

std::int32_t Places::Find(const PlaceTarget &target, const URK_UnrealKey &key, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value || (info.kind != PropertyKind::Set && info.kind != PropertyKind::Map))
        return Fail("not a set or map"), -1;
    Containers &stores = owned_->Stores();
    const std::optional<SetLayout> layout = stores.LayoutOf(info);
    if (!layout)
        return Fail(stores.Failure()), -1;
    // Names are made (and a struct's names confirmed) through the engine.
    if ((layout->key.kind == PropertyKind::Name || layout->key.kind == PropertyKind::Struct) &&
        !NeedGameThread(gameThread))
        return -1;
    std::vector<std::uint8_t> bytes;
    std::u16string text;
    if (!BuildKey(layout->key, key, false, &bytes, &text))
        return -1;
    const std::int32_t slot = stores.Find(*layout, target.value, bytes.data());
    if (slot < 0 && !stores.Failure().empty())
        failure_ = stores.Failure();
    return slot;
}

std::int32_t Places::Add(const PlaceTarget &target, const URK_UnrealKey &key, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (!target.value || (info.kind != PropertyKind::Set && info.kind != PropertyKind::Map))
        return Fail("not a set or map"), -1;
    if (!NeedGameThread(gameThread))
        return -1;
    Containers &stores = owned_->Stores();
    const std::optional<SetLayout> layout = stores.LayoutOf(info);
    if (!layout)
        return Fail(stores.Failure()), -1;
    std::vector<std::uint8_t> bytes;
    std::u16string text;
    if (!BuildKey(layout->key, key, true, &bytes, &text))
        return -1;
    bool consumed = false;
    const std::int32_t slot = stores.Add(*layout, target.value, bytes.data(), &consumed);
    if (slot < 0)
        Fail(stores.Failure());
    // An existing key wins; the one built for it goes back.
    if (!consumed && !owned_->Destroy(layout->key, bytes.data()))
        Fail(owned_->Failure());
    return slot;
}

// --- delegates -------------------------------------------------------------------------------------

// Reduced UFunction::IsSignatureCompatibleWith.
bool Places::SignatureCompatible(Address signature, Address function) {
    const PropertyChain &chain = engine_->Chain();
    const auto parameters = [&](Address owner) {
        std::vector<PropertyInfo> list;
        Address field = chain.First(owner);
        for (int step = 0; field != kNullAddress && step < kMaxStructFields; ++step, field = chain.Next(field)) {
            const std::optional<PropertyInfo> info = engine_->Values().Describe(field);
            if (!info)
                return std::optional<std::vector<PropertyInfo>>();
            if (info->propertyFlags & kPropertyFlagParm)
                list.push_back(*info);
        }
        return std::optional<std::vector<PropertyInfo>>(list);
    };
    const auto expected = parameters(signature);
    const auto actual = parameters(function);
    if (!expected || !actual || expected->size() != actual->size())
        return false;
    for (std::size_t i = 0; i < expected->size(); ++i) {
        const PropertyInfo &a = (*expected)[i];
        const PropertyInfo &b = (*actual)[i];
        if (a.kind != b.kind || a.elementSize != b.elementSize || a.arrayDim != b.arrayDim ||
            (a.propertyFlags & kSignatureFlags) != (b.propertyFlags & kSignatureFlags))
            return false;
        if ((a.kind == PropertyKind::Struct || a.kind == PropertyKind::Object || a.kind == PropertyKind::Class) &&
            a.inner != b.inner)
            return false;
    }
    return true;
}

const std::uint8_t *Places::SparseList(const PlaceTarget &target, bool gameThread) {
    if (!NeedGameThread(gameThread))
        return nullptr;
    if (!delegates_.Ready()) {
        Fail(delegates_.Failure());
        return nullptr;
    }
    return delegates_.List(target.info, target.value);
}

bool Places::Bind(const PlaceTarget &target, Address object, const char *function, bool gameThread) {
    const PropertyInfo &info = target.info;
    if (target.value && info.kind == PropertyKind::SparseDelegate) {
        std::uint8_t delegate[32]{};
        if (!NeedGameThread(gameThread) || !MakeDelegate(info.typeObject, object, function, delegate))
            return false;
        if (target.owner == kNullAddress)
            return Fail("a sparse delegate is bound on the object that has it");
        if (!delegates_.Ready())
            return Fail(delegates_.Failure());
        return delegates_.Add(info, target.owner, target.value, delegate) ? true : Fail("the delegate did not bind");
    }
    if (!target.value || info.kind != PropertyKind::Delegate)
        return Fail("not a delegate");
    if (info.elementSize != 16)
        return Fail("delegates here are not the plain weak-object-and-name layout");
    if (!NeedGameThread(gameThread))
        return false;
    if (object == kNullAddress) {
        std::memset(target.value, 0, 16);
        return true;
    }
    std::uint8_t delegate[16]{};
    if (!MakeDelegate(info.typeObject, object, function, delegate))
        return false;
    std::memcpy(target.value, delegate, sizeof(delegate));
    return true;
}

bool Places::MakeDelegate(Address signature, Address object, const char *function, std::uint8_t *delegate) {
    if (object == kNullAddress || !function || !Live(*engine_, object))
        return Fail("the object is not live");
    // Function by name on the class chain, as FindFunctionChecked resolves it.
    const ObjectFinder &finder = engine_->Finder();
    Address found = kNullAddress;
    Address owner = finder.ClassOf(object);
    for (int depth = 0; owner != kNullAddress && depth < 64 && found == kNullAddress; ++depth) {
        found = finder.FindInOuter(function, owner);
        if (found != kNullAddress && !ObjectIs(finder, engine_->Structs(), found, kCastFlagFunction))
            found = kNullAddress;
        owner = engine_->Types().SuperOf(owner);
    }
    if (found == kNullAddress)
        return Fail(std::string("the object has no function ") + function);
    if (signature == kNullAddress || !IsStruct(*engine_, signature) || !SignatureCompatible(signature, found))
        return Fail(std::string(function) + " does not match the delegate's signature");
    EngineCalls &calls = owned_->Engine();
    if (!calls.MakeWeak(object, delegate))
        return Fail(calls.Failure());
    // The function's own FName: exactly what the engine looks the name up by.
    const std::size_t nameSize = static_cast<std::size_t>(finder.Names().Layout().size);
    if (EngineCalls::kWeakSize + nameSize > 16 ||
        !engine_->Reader().Read(found + finder.Offsets().name, delegate + EngineCalls::kWeakSize, nameSize))
        return Fail("the function's name is unreadable");
    return true;
}

} // namespace URK::Unreal
