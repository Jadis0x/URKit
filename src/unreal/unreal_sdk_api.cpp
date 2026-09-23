#include "unreal_sdk_api.h"
#include "unreal_enums.h"
#include "unreal_owned_values.h"
#include "unreal_places.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {
namespace {

HookInstaller g_installer;

bool CopyOut(const std::string &text, char *output, std::size_t outputSize) {
    if (!output || outputSize == 0)
        return false;
    const std::size_t copy = std::min(text.size(), outputSize - 1);
    std::memcpy(output, text.data(), copy);
    output[copy] = '\0';
    return true;
}

// A handle is a bare address: only one the object array still holds is touched,
// so a mod keeping a destroyed object gets failures, not freed memory.
bool Live(const UnrealEngine &engine, Address object) {
    return engine.Available() && IsLiveObject(engine.Finder(), object);
}

// What an object member write may hold: the member must be an object or class
// reference, and the value one of its class.
bool AssignableMember(const UnrealEngine &engine, const PropertyInfo &info, Address value) {
    return (info.kind == PropertyKind::Object || info.kind == PropertyKind::Class) && Assignable(engine, info, value);
}

// Members resolved per class. A class is the same one while it keeps its name
// and its package's name: a package name is one asset, so a Blueprint class
// reloaded at the same address has the same layout.
class MemberCache {
  public:
    std::optional<PropertyInfo> Find(const UnrealEngine &engine, Address classObject, const std::string &member) {
        const ObjectFinder &finder = engine.Finder();
        const Address package = finder.OuterOf(classObject);
        const std::optional<std::string> className = finder.NameOf(classObject);
        const std::optional<std::string> packageName = finder.NameOf(package);
        if (!className || !packageName)
            return std::nullopt;
        {
            std::shared_lock lock(mutex_);
            const auto entry = classes_.find(classObject);
            if (entry != classes_.end() && entry->second.Same(*className, package, *packageName)) {
                const auto found = entry->second.members.find(member);
                if (found != entry->second.members.end())
                    return found->second;
            }
        }

        const Address field = engine.Chain().FindMemberDeep(classObject, member);
        const std::optional<PropertyInfo> info = field != kNullAddress ? engine.Values().Describe(field) : std::nullopt;
        if (!info || !info->Resolved())
            return std::nullopt;

        std::unique_lock lock(mutex_);
        ClassEntry &entry = classes_[classObject];
        if (!entry.Same(*className, package, *packageName))
            entry = ClassEntry{*className, package, *packageName, {}};
        entry.members[member] = *info;
        return info;
    }

  private:
    struct ClassEntry {
        std::string name;
        Address package = kNullAddress;
        std::string packageName;
        std::unordered_map<std::string, PropertyInfo> members;

        bool Same(const std::string &otherName, Address otherPackage, const std::string &otherPackageName) const {
            return package == otherPackage && name == otherName && packageName == otherPackageName;
        }
    };

    std::shared_mutex mutex_;
    std::unordered_map<Address, ClassEntry> classes_;
};

MemberCache g_members;

// Writes only what the caller's struct has room for: size says which version it
// was compiled against. Version 1 had no bool layout, version 2 no type object.
void FillInfo(URK_UnrealPropertyInfo *info, const PropertyInfo &from) {
    constexpr std::uint32_t kVersion1 = offsetof(URK_UnrealPropertyInfo, bool_byte_offset);
    constexpr std::uint32_t kVersion2 = offsetof(URK_UnrealPropertyInfo, type_object);
    const std::uint32_t room = info->size == 0 ? kVersion1 : info->size;
    info->kind = static_cast<std::int32_t>(from.kind);
    info->element_size = from.elementSize;
    info->array_dim = from.arrayDim;
    info->inner = from.inner;
    std::uint32_t filled = kVersion1;
    if (room >= kVersion2) {
        info->bool_byte_offset = from.boolLayout.byteOffset;
        info->bool_byte_mask = from.boolLayout.byteMask;
        info->bool_field_mask = from.boolLayout.fieldMask;
        info->reserved = 0;
        filled = kVersion2;
    }
    if (room >= sizeof(URK_UnrealPropertyInfo)) {
        info->type_object = from.typeObject;
        filled = sizeof(URK_UnrealPropertyInfo);
    }
    info->size = filled;
}

bool IsStruct(const UnrealEngine &engine, Address object) {
    return Live(engine, object) && ObjectIs(engine.Finder(), engine.Structs(), object, kCastFlagStruct);
}

// One member resolved from an instance. Every value entry goes through this,
// so a missing member or mismatched kind fails the same way everywhere.
struct ResolvedMember {
    PropertyInfo info;
};

std::optional<ResolvedMember> Resolve(UnrealEngine &engine, Address object, const char *memberName) {
    if (!memberName || !Live(engine, object))
        return std::nullopt;

    const Address classObject = engine.Finder().ClassOf(object);
    if (classObject == kNullAddress)
        return std::nullopt;

    const std::optional<PropertyInfo> info = g_members.Find(engine, classObject, memberName);
    if (!info)
        return std::nullopt;
    return ResolvedMember{*info};
}

// --- availability / version ---------------------------------------------

int Unreal_IsAvailable() { return UnrealEngine::Instance().EnsureBootstrapped() ? 1 : 0; }

void Unreal_EngineVersion(std::int32_t *major, std::int32_t *minor, std::int32_t *patch) {
    const EngineVersion &version = UnrealEngine::Instance().Version();
    if (major)
        *major = version.major;
    if (minor)
        *minor = version.minor;
    if (patch)
        *patch = version.patch;
}

int Unreal_UsesFieldProperties() { return UnrealEngine::Instance().Version().UsesFieldProperties() ? 1 : 0; }

// --- object lookup --------------------------------------------------------

URK_UnrealObject Unreal_FindObject(const char *name) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !engine.EnsureBootstrapped())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().Find(name);
}

URK_UnrealObject Unreal_FindObjectInOuter(const char *name, const char *outerName) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !outerName || !engine.EnsureBootstrapped())
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().FindInOuter(name, outerName);
}

URK_UnrealObject Unreal_ClassOf(URK_UnrealObject object) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, object))
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().ClassOf(object);
}

URK_UnrealObject Unreal_OuterOf(URK_UnrealObject object) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, object))
        return URK_UNREAL_NULL_OBJECT;
    return engine.Finder().OuterOf(object);
}

int Unreal_NameOf(URK_UnrealObject object, char *output, std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, object))
        return 0;
    const std::optional<std::string> name = engine.Finder().NameOf(object);
    return name ? (CopyOut(*name, output, outputSize) ? 1 : 0) : 0;
}

// --- type queries -----------------------------------------------------------

int Unreal_IsChildOf(URK_UnrealObject structObject, URK_UnrealObject base) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, structObject) || !Live(engine, base))
        return 0;
    return engine.Types().IsChildOf(structObject, base) ? 1 : 0;
}

int Unreal_IsA(URK_UnrealObject object, URK_UnrealObject classObject) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, object) || !Live(engine, classObject))
        return 0;
    return engine.Types().IsA(object, classObject) ? 1 : 0;
}

URK_UnrealObject Unreal_DefaultObjectOf(URK_UnrealObject classObject) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, classObject))
        return URK_UNREAL_NULL_OBJECT;
    return engine.Types().DefaultObjectOf(classObject);
}

std::size_t Unreal_InstancesOf(URK_UnrealObject classObject, URK_UnrealObject *output, std::size_t outputCapacity,
                               int exact) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, classObject))
        return 0;

    TypeQueries::InstanceQuery query{};
    query.classObject = classObject;
    query.exact = exact != 0;
    const std::vector<Address> instances = engine.Types().InstancesOf(query);

    if (output && outputCapacity > 0) {
        const std::size_t count = std::min(outputCapacity, instances.size());
        for (std::size_t i = 0; i < count; ++i)
            output[i] = instances[i];
    }
    return instances.size();
}

// --- property access ---------------------------------------------------------

int Unreal_DescribeProperty(URK_UnrealObject object, const char *memberName, URK_UnrealPropertyInfo *info) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!info)
        return 0;
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;

    FillInfo(info, resolved->info);
    return 1;
}

int Unreal_ReadInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<std::int64_t> value = engine.Values().ReadInteger(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value;
    return 1;
}

int Unreal_ReadFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<double> value = engine.Values().ReadFloating(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value;
    return 1;
}

int Unreal_ReadBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<bool> value = engine.Values().ReadBool(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value ? 1 : 0;
    return 1;
}

URK_UnrealObject Unreal_ReadObject(URK_UnrealObject object, const char *memberName, std::int32_t index) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return URK_UNREAL_NULL_OBJECT;
    return engine.Values().ReadObject(object, resolved->info, index);
}

int Unreal_ReadName(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                    std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    const std::optional<std::string> value = engine.Values().ReadName(object, resolved->info, index);
    return value ? (CopyOut(*value, output, outputSize) ? 1 : 0) : 0;
}

int Unreal_ReadString(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                      std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    const std::optional<std::string> value = engine.Values().ReadString(object, resolved->info, index);
    return value ? (CopyOut(*value, output, outputSize) ? 1 : 0) : 0;
}

int Unreal_WriteInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteInteger(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

int Unreal_WriteFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteFloating(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

int Unreal_WriteBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteBool(engine.Writer(), object, resolved->info, value != 0, index) ? 1 : 0;
}

int Unreal_WriteObject(URK_UnrealObject object, const char *memberName, std::int32_t index, URK_UnrealObject value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !AssignableMember(engine, resolved->info, value))
        return 0;
    return engine.Values().WriteObject(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

// --- structs ------------------------------------------------------------------

std::int32_t Unreal_StructSize(URK_UnrealObject structObject) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const StructOffsets &structs = engine.Structs();
    if (!IsStruct(engine, structObject) || structs.propertiesSize == kOffsetNotFound)
        return 0;
    // GetStructureSize(): what a member of this type occupies.
    const std::int32_t size = engine.Reader().ReadInt32(structObject + structs.propertiesSize).value_or(0);
    const std::int32_t alignment = structs.minAlignment == kOffsetNotFound
                                       ? 1
                                       : engine.Reader().ReadAs<std::int16_t>(structObject + structs.minAlignment)
                                             .value_or(1);
    if (size <= 0 || alignment <= 0)
        return size > 0 ? size : 0;
    return (size + alignment - 1) / alignment * alignment;
}

int Unreal_DescribeStructMember(URK_UnrealObject structObject, const char *memberName, URK_UnrealPropertyInfo *info,
                                std::int32_t *offset) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!memberName || !info || !offset || !IsStruct(engine, structObject))
        return 0;
    const Address field = engine.Chain().FindMemberDeep(structObject, memberName);
    const std::optional<PropertyInfo> described =
        field != kNullAddress ? engine.Values().Describe(field) : std::nullopt;
    if (!described || !described->Resolved())
        return 0;
    FillInfo(info, *described);
    *offset = described->offset;
    return 1;
}

int Unreal_ReadStruct(URK_UnrealObject object, const char *memberName, std::int32_t index, void *output,
                      std::size_t size) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output || resolved->info.kind != PropertyKind::Struct ||
        size != static_cast<std::size_t>(resolved->info.elementSize))
        return 0;
    const Address at = engine.Values().ValueAddress(object, resolved->info, index);
    return at != kNullAddress && engine.Reader().Read(at, output, size) ? 1 : 0;
}

int Unreal_WriteStruct(URK_UnrealObject object, const char *memberName, std::int32_t index, const void *value,
                       std::size_t size) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !value || resolved->info.kind != PropertyKind::Struct ||
        size != static_cast<std::size_t>(resolved->info.elementSize))
        return 0;
    const Address at = engine.Values().ValueAddress(object, resolved->info, index);
    std::vector<std::uint8_t> current(size);
    if (at == kNullAddress || !engine.Reader().Read(at, current.data(), size))
        return 0;
    if (!StructChangeAllowed(engine, resolved->info.inner, current.data(), static_cast<const std::uint8_t *>(value),
                             size))
        return 0;
    return engine.Writer().Write(at, value, size) ? 1 : 0;
}

// --- services ---------------------------------------------------------------------

// What makes, changes and frees engine memory, built once the ladder resolved.
// Changes run on the game thread only; reflection-only reads anywhere.
struct Services {
    explicit Services(UnrealEngine &engine)
        : calls(engine.Finder(), engine.Chain(), engine.Values(), engine.Functions(), engine.Types(), engine.Structs(),
                engine.ProcessEvent(), engine.Bounds()),
          owned(engine.Finder(), engine.Chain(), engine.Values(), engine.Types(), calls),
          enums(engine.Finder(), engine.Structs()), places(engine, owned, enums) {}
    EngineCalls calls;
    OwnedValues owned;
    EnumNames enums;
    Places places;
};

void Report(Address subject, const std::string &message);

// Only after UnrealEngine::Available(): it holds the ladder's objects.
Services &Serve() {
    static Services services = [] {
        Containers::SetNote([](const std::string &message) { Report(kNullAddress, message); });
        return Services(UnrealEngine::Instance());
    }();
    return services;
}

std::atomic<LogSink> g_log{nullptr};
std::mutex g_reportedMutex;
std::set<std::pair<Address, std::string>> g_reported;

// Once per subject and message: mods call in loops.
void Report(Address subject, const std::string &message) {
    const LogSink log = g_log.load(std::memory_order_acquire);
    if (!log)
        return;
    {
        std::lock_guard lock(g_reportedMutex);
        if (!g_reported.emplace(subject, message).second)
            return;
    }
    const std::string name =
        subject != kNullAddress ? UnrealEngine::Instance().Finder().NameOf(subject).value_or("?") + ": " : "";
    log(("[Unreal] " + name + message + ".").c_str());
}

bool OnGameThread() {
    const std::uint32_t id = ProcessEventHook::Instance().GameThreadId();
    return id != 0 && id == GetCurrentThreadId();
}

// --- calling ------------------------------------------------------------------

// A frame as mods hold it: the parameter block, plus which parameters hold
// engine memory the loader must give back.
struct LoaderFrame {
    explicit LoaderFrame(FunctionInfo info)
        : frame(std::move(info)), engineOwned(frame.Function().parameters.size(), 0) {}
    CallFrame frame;
    // Made through a place, or written by a call.
    std::vector<char> engineOwned;
    // A written parameter whose memory cannot be released; calls are refused.
    std::string unreleasable;
    bool called = false;
};

LoaderFrame *FrameOf(URK_UnrealCallFrame *frame) { return reinterpret_cast<LoaderFrame *>(frame); }
const LoaderFrame *FrameOf(const URK_UnrealCallFrame *frame) { return reinterpret_cast<const LoaderFrame *>(frame); }

// Engine flag value (EPropertyFlags).
constexpr std::uint64_t kPropertyFlagConstParm = 0x2;

// The return value and non-const out parameters: what the engine assigns into.
bool Written(const FunctionParameter &parameter) {
    const std::uint64_t flags = parameter.info.propertyFlags;
    return parameter.returned || (flags & kPropertyFlagReturnParm) != 0 ||
           ((flags & kPropertyFlagOutParm) != 0 && (flags & kPropertyFlagConstParm) == 0);
}

// Game thread. Releasing calls into the engine, which can come back here.
thread_local bool t_releasing = false;
std::mutex g_pendingMutex;
std::vector<std::unique_ptr<LoaderFrame>> g_pending;

void ReleaseFrame(LoaderFrame &loaderFrame) {
    OwnedValues &owned = Serve().owned;
    const FunctionInfo &function = loaderFrame.frame.Function();
    auto *data = static_cast<std::uint8_t *>(loaderFrame.frame.Data());
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (!loaderFrame.engineOwned[index])
            continue;
        const PropertyInfo &info = function.parameters[index].info;
        for (std::int32_t i = 0; i < info.arrayDim; ++i) {
            if (!owned.Destroy(info, data + info.offset + static_cast<std::size_t>(i) * info.elementSize)) {
                Report(function.function, "a parameter's value could not be released (" + owned.Failure() + ")");
                return;
            }
        }
        loaderFrame.engineOwned[index] = 0;
    }
}

void ReleasePending() {
    if (t_releasing)
        return;
    std::vector<std::unique_ptr<LoaderFrame>> batch;
    {
        std::lock_guard lock(g_pendingMutex);
        batch.swap(g_pending);
    }
    if (batch.empty())
        return;
    t_releasing = true;
    for (const std::unique_ptr<LoaderFrame> &loaderFrame : batch)
        ReleaseFrame(*loaderFrame);
    t_releasing = false;
}

// A UFunction's outer is the class that declares it, so inherited functions
// are found by climbing. An instance stands for its class.
URK_UnrealObject Unreal_FindFunction(URK_UnrealObject ownerClass, const char *name) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !Live(engine, ownerClass))
        return URK_UNREAL_NULL_OBJECT;

    Address owner = ownerClass;
    if (!ObjectIs(engine.Finder(), engine.Structs(), owner, kCastFlagClass))
        owner = engine.Finder().ClassOf(owner);
    constexpr int kMaxDepth = 64;
    for (int depth = 0; owner != kNullAddress && depth < kMaxDepth; ++depth) {
        const Address function = engine.Finder().FindInOuter(name, owner);
        if (function != kNullAddress)
            return function;
        owner = engine.Types().SuperOf(owner);
    }
    return URK_UNREAL_NULL_OBJECT;
}

URK_UnrealCallFrame *Unreal_CallFrameCreate(URK_UnrealObject function) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, function) || !ObjectIs(engine.Finder(), engine.Structs(), function, kCastFlagFunction))
        return nullptr;

    const std::optional<FunctionInfo> info =
        DescribeFunction(engine.Chain(), engine.Values(), engine.Functions(), function);
    if (!info)
        return nullptr;

    auto loaderFrame = std::make_unique<LoaderFrame>(*info);
    for (const FunctionParameter &parameter : info->parameters) {
        if (Written(parameter) && Serve().owned.Classify(parameter.info) == Ownership::Unreleasable &&
            loaderFrame->unreleasable.empty())
            loaderFrame->unreleasable = parameter.name + " (" + PropertyKindName(parameter.info.kind) + ")";
    }
    return reinterpret_cast<URK_UnrealCallFrame *>(loaderFrame.release());
}

void Unreal_CallFrameDestroy(URK_UnrealCallFrame *frame) {
    std::unique_ptr<LoaderFrame> loaderFrame(FrameOf(frame));
    if (!loaderFrame ||
        std::find(loaderFrame->engineOwned.begin(), loaderFrame->engineOwned.end(), 1) == loaderFrame->engineOwned.end())
        return;
    if (OnGameThread() && !t_releasing) {
        t_releasing = true;
        ReleaseFrame(*loaderFrame);
        t_releasing = false;
        return;
    }
    // Released by the game thread's next call or frame.
    std::lock_guard lock(g_pendingMutex);
    g_pending.push_back(std::move(loaderFrame));
}

int Unreal_CallFrameSet(URK_UnrealCallFrame *frame, const char *parameterName, const void *value, std::size_t size) {
    if (!frame || !parameterName || !value)
        return 0;
    LoaderFrame *loaderFrame = FrameOf(frame);
    CallFrame *callFrame = &loaderFrame->frame;
    const FunctionParameter *parameter = callFrame->Function().Parameter(parameterName);
    if (!parameter)
        return 0;
    const std::size_t index = static_cast<std::size_t>(parameter - callFrame->Function().parameters.data());
    const PropertyKind kind = parameter->info.kind;
    const Ownership ownership = Serve().owned.Classify(parameter->info);
    // Engine memory already in the slot would be lost under the mod's bytes;
    // place_clear gives it back first.
    if (ownership != Ownership::None && loaderFrame->engineOwned[index])
        return 0;
    // The engine assigns over what it writes, freeing the old value: that value
    // must be its own (or empty), never a buffer the mod made.
    if (Written(*parameter) && kind != PropertyKind::Struct && ownership != Ownership::None) {
        const std::size_t at = static_cast<std::size_t>(parameter->info.offset);
        if (parameter->info.offset < 0 || size != static_cast<std::size_t>(parameter->info.elementSize) ||
            at + size > callFrame->Size() ||
            std::memcmp(static_cast<const std::uint8_t *>(callFrame->Data()) + at, value, size) != 0)
            return 0;
    }
    if (kind == PropertyKind::Object || kind == PropertyKind::Class) {
        Address object = kNullAddress;
        if (size != sizeof(object))
            return 0;
        std::memcpy(&object, value, sizeof(object));
        if (!Assignable(UnrealEngine::Instance(), parameter->info, object))
            return 0;
    }
    if (kind == PropertyKind::Struct) {
        const std::size_t at = static_cast<std::size_t>(parameter->info.offset);
        if (parameter->info.offset < 0 || size != static_cast<std::size_t>(parameter->info.elementSize) ||
            at + size > callFrame->Size())
            return 0;
        const auto *current = static_cast<const std::uint8_t *>(callFrame->Data()) + at;
        if (!StructChangeAllowed(UnrealEngine::Instance(), parameter->info.inner, current,
                                 static_cast<const std::uint8_t *>(value), size))
            return 0;
    }
    return callFrame->Set(parameterName, value, size) ? 1 : 0;
}

int Unreal_CallFrameGet(const URK_UnrealCallFrame *frame, const char *parameterName, void *output,
                        std::size_t size) {
    if (!frame || !parameterName)
        return 0;
    return FrameOf(frame)->frame.Get(parameterName, output, size) ? 1 : 0;
}

int Unreal_Call(URK_UnrealObject object, URK_UnrealCallFrame *frame) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!frame || !Live(engine, object) || !engine.ProcessEventResolved())
        return 0;

    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (!hook.Installed())
        return 0;
    if (hook.GameThreadId() == 0 || hook.GameThreadId() != GetCurrentThreadId())
        return 0;

    LoaderFrame *loaderFrame = FrameOf(frame);
    const FunctionInfo &function = loaderFrame->frame.Function();
    if (!loaderFrame->unreleasable.empty()) {
        Report(function.function, "call refused: it returns or writes " + loaderFrame->unreleasable +
                                      ", memory of a kind the loader cannot release");
        return 0;
    }
    OwnedValues &owned = Serve().owned;
    auto *data = static_cast<std::uint8_t *>(loaderFrame->frame.Data());
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const FunctionParameter &parameter = function.parameters[index];
        // Texts the call leaves here are dropped by their own Release later.
        if (Written(parameter) && owned.HoldsText(parameter.info) && !owned.Engine().TextReleaseAvailable()) {
            Report(function.function, "call refused: it writes text, and releasing text is unavailable (" +
                                          owned.Engine().Failure() + ")");
            return 0;
        }
    }
    ReleasePending();
    // A text parameter must hold a text: the engine dereferences a zeroed one.
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const PropertyInfo &info = function.parameters[index].info;
        if (!owned.NeedsInitialize(info))
            continue;
        for (std::int32_t i = 0; i < info.arrayDim; ++i) {
            bool made = false;
            if (!owned.FillNullTexts(info, data + info.offset + static_cast<std::size_t>(i) * info.elementSize,
                                     &made)) {
                Report(function.function, "call refused: a text parameter could not be made (" + owned.Failure() + ")");
                return 0;
            }
            if (made)
                loaderFrame->engineOwned[index] = 1;
        }
    }
    if (!InvokeProcessEvent(engine.ProcessEvent(), object, function.function, data))
        return 0;
    loaderFrame->called = true;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (Written(function.parameters[index]) && owned.Classify(function.parameters[index].info) == Ownership::Owned)
            loaderFrame->engineOwned[index] = 1;
    }
    return 1;
}

// --- places -----------------------------------------------------------------------

struct PlaceContext {
    PlaceTarget target;
    LoaderFrame *frame = nullptr;
    std::size_t parameter = 0;
};

std::optional<PlaceContext> ResolvePlace(const URK_UnrealPlace *place, bool describe) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!place || !place->member || !engine.Available() || place->step_count > URK_UNREAL_PLACE_MAX_STEPS)
        return std::nullopt;
    PlaceContext context;
    std::uint8_t *root = nullptr;
    PropertyInfo info;
    if (place->frame) {
        if (place->object != URK_UNREAL_NULL_OBJECT)
            return std::nullopt;
        LoaderFrame *frame = FrameOf(place->frame);
        const FunctionInfo &function = frame->frame.Function();
        const FunctionParameter *parameter = function.Parameter(place->member);
        if (!parameter || parameter->info.offset < 0 || place->member_index < 0 ||
            place->member_index >= parameter->info.arrayDim ||
            static_cast<std::size_t>(parameter->info.offset + parameter->info.elementSize * parameter->info.arrayDim) >
                frame->frame.Size())
            return std::nullopt;
        info = parameter->info;
        root = static_cast<std::uint8_t *>(frame->frame.Data()) + info.offset +
               static_cast<std::size_t>(place->member_index) * info.elementSize;
        context.frame = frame;
        context.parameter = static_cast<std::size_t>(parameter - function.parameters.data());
    } else {
        const std::optional<ResolvedMember> resolved = Resolve(engine, place->object, place->member);
        if (!resolved || place->member_index < 0 || place->member_index >= resolved->info.arrayDim)
            return std::nullopt;
        info = resolved->info;
        root = reinterpret_cast<std::uint8_t *>(static_cast<std::uintptr_t>(place->object)) + info.offset +
               static_cast<std::size_t>(place->member_index) * info.elementSize;
    }
    Places &places = Serve().places;
    std::optional<PlaceTarget> target = places.Walk(root, info, place->steps, place->step_count, describe);
    if (!target) {
        Report(kNullAddress, std::string("place ") + place->member + ": " + places.Failure());
        return std::nullopt;
    }
    context.target = *target;
    return context;
}

// A change through a place: on success a frame parameter now holds what the
// loader must give back.
template <typename Change> int Changed(const URK_UnrealPlace *place, Change change) {
    std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context)
        return 0;
    if (context->target.key) {
        Report(kNullAddress, std::string("place ") + place->member +
                                 ": a set element or map key is replaced by removing it and adding the new one");
        return 0;
    }
    Places &places = Serve().places;
    if (!change(places, context->target)) {
        Report(kNullAddress, std::string("place ") + place->member + ": " + places.Failure());
        return 0;
    }
    if (context->frame)
        context->frame->engineOwned[context->parameter] = 1;
    return 1;
}

int Unreal_PlaceDescribe(const URK_UnrealPlace *place, URK_UnrealPropertyInfo *info) {
    const std::optional<PlaceContext> context = ResolvePlace(place, true);
    if (!context || !info)
        return 0;
    FillInfo(info, context->target.info);
    return 1;
}

int Unreal_PlaceReadInteger(const URK_UnrealPlace *place, std::int64_t *output) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context && output && Serve().places.ReadInteger(context->target, output) ? 1 : 0;
}

int Unreal_PlaceWriteInteger(const URK_UnrealPlace *place, std::int64_t value) {
    return Changed(place, [&](Places &places, const PlaceTarget &target) { return places.WriteInteger(target, value); });
}

int Unreal_PlaceReadFloating(const URK_UnrealPlace *place, double *output) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context && output && Serve().places.ReadFloating(context->target, output) ? 1 : 0;
}

int Unreal_PlaceWriteFloating(const URK_UnrealPlace *place, double value) {
    return Changed(place,
                   [&](Places &places, const PlaceTarget &target) { return places.WriteFloating(target, value); });
}

int Unreal_PlaceReadBool(const URK_UnrealPlace *place, int *output) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    bool value = false;
    if (!context || !output || !Serve().places.ReadBool(context->target, &value))
        return 0;
    *output = value ? 1 : 0;
    return 1;
}

int Unreal_PlaceWriteBool(const URK_UnrealPlace *place, int value) {
    return Changed(place, [&](Places &places, const PlaceTarget &target) { return places.WriteBool(target, value != 0); });
}

URK_UnrealObject Unreal_PlaceReadObject(const URK_UnrealPlace *place) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context ? Serve().places.ReadObject(context->target, OnGameThread()) : URK_UNREAL_NULL_OBJECT;
}

int Unreal_PlaceWriteObject(const URK_UnrealPlace *place, URK_UnrealObject value) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.WriteObject(target, value, gameThread);
    });
}

int Unreal_PlaceReadText(const URK_UnrealPlace *place, char *output, std::size_t outputSize, std::size_t *length) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context)
        return 0;
    const std::optional<std::string> text = Serve().places.ReadText(context->target, OnGameThread());
    if (!text)
        return 0;
    if (length)
        *length = text->size();
    if (!output || outputSize <= text->size())
        return 0;
    std::memcpy(output, text->data(), text->size());
    output[text->size()] = '\0';
    return 1;
}

int Unreal_PlaceWriteText(const URK_UnrealPlace *place, const char *utf8) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.WriteText(target, utf8, gameThread);
    });
}

int Unreal_PlaceReadBytes(const URK_UnrealPlace *place, void *output, std::size_t size) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context && output && Serve().places.ReadBytes(context->target, output, size) ? 1 : 0;
}

int Unreal_PlaceWriteBytes(const URK_UnrealPlace *place, const void *value, std::size_t size) {
    if (!value)
        return 0;
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.WriteBytes(target, value, size, gameThread);
    });
}

std::int32_t Unreal_PlaceCount(const URK_UnrealPlace *place) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context ? Serve().places.Count(context->target) : -1;
}

std::int32_t Unreal_PlaceSlots(const URK_UnrealPlace *place, std::int32_t *output, std::int32_t capacity) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context ? Serve().places.Slots(context->target, output, capacity) : -1;
}

int Unreal_PlaceInsert(const URK_UnrealPlace *place, std::int32_t index, std::int32_t count) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.Insert(target, index, count, gameThread);
    });
}

int Unreal_PlaceRemove(const URK_UnrealPlace *place, std::int32_t index, std::int32_t count) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.Remove(target, index, count, gameThread);
    });
}

int Unreal_PlaceClear(const URK_UnrealPlace *place) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) { return places.Clear(target, gameThread); });
}

std::int32_t Unreal_PlaceFind(const URK_UnrealPlace *place, const URK_UnrealKey *key) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context || !key)
        return -1;
    Places &places = Serve().places;
    const std::int32_t slot = places.Find(context->target, *key, OnGameThread());
    if (slot < 0 && !places.Failure().empty())
        Report(kNullAddress, std::string("place ") + place->member + ": " + places.Failure());
    return slot;
}

std::int32_t Unreal_PlaceAdd(const URK_UnrealPlace *place, const URK_UnrealKey *key) {
    if (!key)
        return -1;
    const bool gameThread = OnGameThread();
    std::int32_t slot = -1;
    Changed(place, [&](Places &places, const PlaceTarget &target) {
        slot = places.Add(target, *key, gameThread);
        return slot >= 0;
    });
    return slot;
}

int Unreal_PlaceBind(const URK_UnrealPlace *place, URK_UnrealObject object, const char *function) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.Bind(target, object, function, gameThread);
    });
}

// --- enums ---------------------------------------------------------------------------

// Names are measured on the game thread; after that any thread reads them.
const EnumNames *Enums() {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return nullptr;
    Services &services = Serve();
    if (!services.enums.Measured() && !(OnGameThread() && services.enums.Measure(services.calls)))
        return nullptr;
    return &services.enums;
}

std::int32_t Unreal_EnumCount(URK_UnrealObject enumObject) {
    const EnumNames *enums = Enums();
    const auto entries = enums ? enums->Entries(enumObject) : std::nullopt;
    return entries ? static_cast<std::int32_t>(entries->size()) : 0;
}

int Unreal_EnumEntry(URK_UnrealObject enumObject, std::int32_t index, char *name, std::size_t nameSize,
                     std::int64_t *value) {
    const EnumNames *enums = Enums();
    const auto entries = enums ? enums->Entries(enumObject) : std::nullopt;
    if (!entries || index < 0 || static_cast<std::size_t>(index) >= entries->size())
        return 0;
    const EnumNames::Entry &entry = (*entries)[static_cast<std::size_t>(index)];
    if (value)
        *value = entry.value;
    return !name || CopyOut(std::string(EnumNames::ShortName(entry.name)), name, nameSize) ? 1 : 0;
}

int Unreal_EnumValue(URK_UnrealObject enumObject, const char *name, std::int64_t *value) {
    const EnumNames *enums = Enums();
    const std::optional<std::int64_t> found = enums && name ? enums->ValueOf(enumObject, name) : std::nullopt;
    if (!found || !value)
        return 0;
    *value = *found;
    return 1;
}

int Unreal_EnumName(URK_UnrealObject enumObject, std::int64_t value, char *output, std::size_t outputSize) {
    const EnumNames *enums = Enums();
    const std::optional<std::string> name = enums ? enums->NameOf(enumObject, value) : std::nullopt;
    return name && CopyOut(*name, output, outputSize) ? 1 : 0;
}

// --- hooking / dispatch ---------------------------------------------------

// The loader's game loop and mods share one hook. It comes off only when
// neither holds it, so a mod's remove cannot stop the game loop.
std::mutex g_hookMutex;
bool g_loaderHold = false;
bool g_modHold = false;

bool EnsureHookInstalled() {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.EnsureBootstrapped() || !engine.ProcessEventResolved() || !g_installer.Valid())
        return false;

    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (hook.Installed())
        return true;

    const std::vector<Address> implementations =
        ProcessEventImplementations(engine.Finder(), engine.Types(), engine.Structs(), engine.ProcessEvent());
    return hook.Install(g_installer, implementations, engine.ProcessEvent());
}

int Unreal_HookInstall() {
    std::lock_guard lock(g_hookMutex);
    if (!EnsureHookInstalled())
        return 0;
    g_modHold = true;
    return 1;
}

int Unreal_HookInstalled() { return ProcessEventHook::Instance().Installed() ? 1 : 0; }

int Unreal_HookRemove() {
    std::lock_guard lock(g_hookMutex);
    g_modHold = false;
    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (!g_loaderHold)
        return hook.Remove() ? 1 : 0;
    // What remove means to a mod: its observer stops seeing calls.
    hook.Observe(nullptr, nullptr);
    return 1;
}

// bool and int differ as return types, so the ABI observer needs a trampoline.
std::atomic<URK_UnrealProcessEventObserverFn> g_observerFn{nullptr};
std::atomic<void *> g_observerUser{nullptr};

bool ObserverTrampoline(void *, Address object, Address function, void *parms) {
    const URK_UnrealProcessEventObserverFn fn = g_observerFn.load(std::memory_order_acquire);
    if (!fn)
        return true;
    return fn(g_observerUser.load(std::memory_order_acquire), object, function, parms) != 0;
}

void Unreal_ProcessEventObserve(URK_UnrealProcessEventObserverFn observer, void *userData) {
    g_observerUser.store(userData, std::memory_order_release);
    g_observerFn.store(observer, std::memory_order_release);
    ProcessEventHook::Instance().Observe(observer ? &ObserverTrampoline : nullptr, nullptr);
}

std::uint32_t Unreal_GameThreadId() { return ProcessEventHook::Instance().GameThreadId(); }

int Unreal_PostToGameThread(URK_UnrealPostedWorkFn work, void *userData) {
    if (!work)
        return 0;
    return ProcessEventHook::Instance().Post(reinterpret_cast<ProcessEventHook::Work>(work), userData) ? 1 : 0;
}

URK_UnrealApi BuildTable() {
    URK_UnrealApi api{};
    api.version = URK_UNREAL_API_VERSION;
    api.size = sizeof(URK_UnrealApi);

    api.is_available = &Unreal_IsAvailable;
    api.engine_version = &Unreal_EngineVersion;
    api.uses_field_properties = &Unreal_UsesFieldProperties;

    api.find_object = &Unreal_FindObject;
    api.find_object_in_outer = &Unreal_FindObjectInOuter;
    api.class_of = &Unreal_ClassOf;
    api.outer_of = &Unreal_OuterOf;
    api.name_of = &Unreal_NameOf;

    api.is_child_of = &Unreal_IsChildOf;
    api.is_a = &Unreal_IsA;
    api.default_object_of = &Unreal_DefaultObjectOf;
    api.instances_of = &Unreal_InstancesOf;

    api.describe_property = &Unreal_DescribeProperty;
    api.read_integer = &Unreal_ReadInteger;
    api.read_floating = &Unreal_ReadFloating;
    api.read_bool = &Unreal_ReadBool;
    api.read_object = &Unreal_ReadObject;
    api.read_name = &Unreal_ReadName;
    api.read_string = &Unreal_ReadString;
    api.write_integer = &Unreal_WriteInteger;
    api.write_floating = &Unreal_WriteFloating;
    api.write_bool = &Unreal_WriteBool;
    api.write_object = &Unreal_WriteObject;

    api.find_function = &Unreal_FindFunction;
    api.call_frame_create = &Unreal_CallFrameCreate;
    api.call_frame_destroy = &Unreal_CallFrameDestroy;
    api.call_frame_set = &Unreal_CallFrameSet;
    api.call_frame_get = &Unreal_CallFrameGet;
    api.call = &Unreal_Call;

    api.hook_install = &Unreal_HookInstall;
    api.hook_installed = &Unreal_HookInstalled;
    api.hook_remove = &Unreal_HookRemove;
    api.process_event_observe = &Unreal_ProcessEventObserve;
    api.game_thread_id = &Unreal_GameThreadId;
    api.post_to_game_thread = &Unreal_PostToGameThread;

    api.struct_size = &Unreal_StructSize;
    api.describe_struct_member = &Unreal_DescribeStructMember;
    api.read_struct = &Unreal_ReadStruct;
    api.write_struct = &Unreal_WriteStruct;

    api.place_describe = &Unreal_PlaceDescribe;
    api.place_read_integer = &Unreal_PlaceReadInteger;
    api.place_write_integer = &Unreal_PlaceWriteInteger;
    api.place_read_floating = &Unreal_PlaceReadFloating;
    api.place_write_floating = &Unreal_PlaceWriteFloating;
    api.place_read_bool = &Unreal_PlaceReadBool;
    api.place_write_bool = &Unreal_PlaceWriteBool;
    api.place_read_object = &Unreal_PlaceReadObject;
    api.place_write_object = &Unreal_PlaceWriteObject;
    api.place_read_text = &Unreal_PlaceReadText;
    api.place_write_text = &Unreal_PlaceWriteText;
    api.place_read_bytes = &Unreal_PlaceReadBytes;
    api.place_write_bytes = &Unreal_PlaceWriteBytes;
    api.place_count = &Unreal_PlaceCount;
    api.place_slots = &Unreal_PlaceSlots;
    api.place_insert = &Unreal_PlaceInsert;
    api.place_remove = &Unreal_PlaceRemove;
    api.place_clear = &Unreal_PlaceClear;
    api.place_find = &Unreal_PlaceFind;
    api.place_add = &Unreal_PlaceAdd;
    api.place_bind = &Unreal_PlaceBind;
    api.enum_count = &Unreal_EnumCount;
    api.enum_entry = &Unreal_EnumEntry;
    api.enum_value = &Unreal_EnumValue;
    api.enum_name = &Unreal_EnumName;

    return api;
}

} // namespace

UnrealEngine &UnrealEngine::Instance() {
    static UnrealEngine engine;
    return engine;
}

bool UnrealEngine::EnsureBootstrapped() {
    if (available_.load(std::memory_order_acquire))
        return true;
    if (ruledOut_.load(std::memory_order_acquire))
        return false;

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t last = lastAttemptMs_.load(std::memory_order_acquire);
    if (last != 0 && now - last < kRetryCooldownMs)
        return false;

    std::lock_guard<std::mutex> lock(bootstrapMutex_);
    // Re-checked under the lock: another thread may have climbed or failed.
    if (available_.load(std::memory_order_relaxed))
        return true;
    if (ruledOut_.load(std::memory_order_relaxed))
        return false;
    const std::uint64_t lastUnderLock = lastAttemptMs_.load(std::memory_order_relaxed);
    if (lastUnderLock != 0 && GetTickCount64() - lastUnderLock < kRetryCooldownMs)
        return false;
    const std::uint64_t attemptStart = GetTickCount64();
    lastAttemptMs_.store(attemptStart, std::memory_order_release);
    ++profile_.attempts;
    std::uint64_t mark = attemptStart;
    const auto lap = [&mark] {
        const std::uint64_t now = GetTickCount64();
        const std::uint64_t elapsed = now - mark;
        mark = now;
        return elapsed;
    };

    // Partial state is never read while available_ is false; every failure
    // restamps the cooldown.
    const auto failed = [this, attemptStart](const char *why) {
        failure_.store(why, std::memory_order_release);
        const std::uint64_t now = GetTickCount64();
        profile_.failedMs += now - attemptStart;
        lastAttemptMs_.store(now, std::memory_order_release);
        return false;
    };

    const UnrealPresence &presence = Presence();
    if (ruledOut_.load(std::memory_order_acquire))
        return false;
    version_ = presence.version;

    std::vector<ScanRegion> dataRegions;
    std::vector<ScanRegion> codeRegions;
    for (const Address module : presence.runtimeModules) {
        const std::vector<ScanRegion> moduleData = ModuleDataRegions(memory_, module);
        const std::vector<ScanRegion> moduleCode = ModuleCodeRegions(memory_, module);
        dataRegions.insert(dataRegions.end(), moduleData.begin(), moduleData.end());
        codeRegions.insert(codeRegions.end(), moduleCode.begin(), moduleCode.end());
    }
    if (dataRegions.empty())
        return failed("the engine image has no data sections");

    if (!anchors_) {
        anchors_.emplace();
        for (const Address module : presence.runtimeModules) {
            GlobalCandidates found = FindGlobalCandidates(memory_, module);
            anchors_->objectArrays.insert(anchors_->objectArrays.end(), found.objectArrays.begin(),
                                          found.objectArrays.end());
            anchors_->namePools.insert(anchors_->namePools.end(), found.namePools.begin(), found.namePools.end());
        }
        functionTable_ = FunctionTable::Read(memory_, presence.runtimeModules);
        profile_.anchoredArrays = anchors_->objectArrays.size();
        profile_.anchoredPools = anchors_->namePools.size();
        profile_.anchorMs = lap();
        const bool anchored = !anchors_->objectArrays.empty() && !anchors_->namePools.empty();
        scanAllowedAtMs_ = anchored ? GetTickCount64() + kScanFallbackMs : 0;
    }

    // Ordinary early on: the object array is built long after the loader lands.
    runtime_ = BootstrapAnchored(memory_, *anchors_);
    profile_.locatedBy = "code anchors";
    if (!runtime_ && GetTickCount64() >= scanAllowedAtMs_) {
        ++profile_.scans;
        runtime_ = BootstrapRuntime(memory_, dataRegions);
        profile_.locatedBy = "data scan";
        const bool anchored = !anchors_->objectArrays.empty() && !anchors_->namePools.empty();
        scanAllowedAtMs_ = GetTickCount64() + (anchored ? kScanFallbackMs : kScanRetryMs);
    }
    profile_.locateMs = lap();
    if (!runtime_)
        return failed("GUObjectArray and FNamePool not found yet");

    finder_ = std::make_unique<ObjectFinder>(ObjectFinder::Build(runtime_->objects, runtime_->names, runtime_->header));
    profile_.indexMs = lap();
    // Holds even when the version resource was stripped.
    if (!UsesFPropertySystem(*finder_)) {
        failure_.store("properties are UObjects, so the engine predates UE4.25", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
        return false;
    }
    structs_ = FindStructOffsets(*finder_);
    fields_ = FindFieldOffsets(*finder_, runtime_->names, structs_);
    if (!fields_.Resolved())
        return failed("the FField layout did not resolve");

    tail_ = FindPropertyTailOffsets(*finder_, structs_, fields_);
    classes_ = FindClassOffsets(*finder_, structs_);
    chain_ = std::make_unique<PropertyChain>(memory_, runtime_->names, structs_, fields_);
    values_ = std::make_unique<PropertyValues>(memory_, runtime_->names, structs_, fields_, tail_);
    types_ = std::make_unique<TypeQueries>(*finder_, structs_, classes_);
    profile_.offsetsMs = lap();
    functions_ = FindFunctionOffsets(*finder_, structs_, fields_, tail_, codeRegions);
    profile_.functionsMs = lap();
    processEvent_ = FindProcessEvent(*finder_, *types_, structs_, functions_, codeRegions, functionTable_)
                       .value_or(ProcessEventLocation{});
    profile_.processEventMs = lap();

    available_.store(true, std::memory_order_release);
    return true;
}

const UnrealPresence &UnrealEngine::Presence() {
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (presence_)
        return *presence_;

    char pathBuffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, sizeof(pathBuffer));
    std::string path(pathBuffer, length < sizeof(pathBuffer) ? length : sizeof(pathBuffer) - 1);
    std::string name = path;
    const std::size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    const ModuleCandidate self{name, path, MainModuleBase()};
    presence_ = DetectUnreal(memory_, std::span<const ModuleCandidate>(&self, 1));
    // Neither answer changes while the process lives.
    if (!presence_->WorthScanning() || presence_->runtimeModules.empty()) {
        failure_.store("not a UBT-built process", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
    } else if (presence_->version.Known() && !presence_->version.UsesFieldProperties()) {
        failure_.store("the version resource names an engine before UE4.25", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
    }
    return *presence_;
}

bool UnrealEngine::RuledOut() const { return ruledOut_.load(std::memory_order_acquire); }

bool UnrealEngine::Available() const { return available_.load(std::memory_order_acquire); }

const URK_UnrealApi *UnrealSdkApi(const HookInstaller &installer) {
    static const URK_UnrealApi table = BuildTable();
    g_installer = installer;
    return &table;
}

void UnrealSdk_SetLog(LogSink log) { g_log.store(log, std::memory_order_release); }

void UnrealSdk_ReleasePending() {
    if (!UnrealEngine::Instance().Available() || !OnGameThread())
        return;
    // Once, on the first frame: every measurement engine memory depends on, so
    // a build where one fails says so at load rather than at a mod's first use.
    static bool measured = false;
    if (!measured) {
        measured = true;
        Services &services = Serve();
        const auto check = [](bool ok, const char *what, const std::string &why) {
            if (!ok)
                Report(kNullAddress, std::string(what) + " unavailable: " + why);
        };
        const std::optional<EngineCalls::Block> probe = services.calls.Allocate(64, 16);
        check(probe && services.calls.Free(probe->data), "engine allocation", services.calls.Failure());
        check(services.calls.TextReleaseAvailable(), "releasing text", services.calls.Failure());
        check(services.calls.MeasureWeakNow(), "weak references", services.calls.Failure());
        check(services.enums.Measure(services.calls), "enum names", services.enums.Failure());
    }
    ReleasePending();
}

const EnumNames *UnrealSdk_Enums() { return Enums(); }

bool UnrealSdk_HoldProcessEventHook() {
    std::lock_guard lock(g_hookMutex);
    if (!EnsureHookInstalled())
        return false;
    g_loaderHold = true;
    return true;
}

} // namespace URK::Unreal
