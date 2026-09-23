#include "unreal_sdk_api.h"
#include "unreal_owned_values.h"

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

// What an object slot may hold: native code and the GC trust it blindly.
bool Assignable(const UnrealEngine &engine, const PropertyInfo &info, Address value) {
    if (value == kNullAddress)
        return true;
    if (!Live(engine, value))
        return false;
    if (info.kind == PropertyKind::Class)
        return ObjectIs(engine.Finder(), engine.Structs(), value, kCastFlagClass);
    return info.kind == PropertyKind::Object && info.inner != kNullAddress && engine.Types().IsA(value, info.inner);
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
// was compiled against, and version 1 had no bool layout.
void FillInfo(URK_UnrealPropertyInfo *info, const PropertyInfo &from) {
    constexpr std::uint32_t kVersion1 = offsetof(URK_UnrealPropertyInfo, bool_byte_offset);
    const std::uint32_t room = info->size == 0 ? kVersion1 : info->size;
    info->kind = static_cast<std::int32_t>(from.kind);
    info->element_size = from.elementSize;
    info->array_dim = from.arrayDim;
    info->inner = from.inner;
    if (room >= sizeof(URK_UnrealPropertyInfo)) {
        info->bool_byte_offset = from.boolLayout.byteOffset;
        info->bool_byte_mask = from.boolLayout.byteMask;
        info->bool_field_mask = from.boolLayout.fieldMask;
        info->reserved = 0;
    }
    info->size = room < sizeof(URK_UnrealPropertyInfo) ? kVersion1 : sizeof(URK_UnrealPropertyInfo);
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

constexpr int kMaxStructDepth = 16;
constexpr int kMaxStructFields = 4096;

// Whether proposed may replace current as a value of structObject: numbers
// change freely, objects must be live and of their class, and anything owning
// an allocation or not checkable must stay byte-identical.
bool StructChangeAllowed(const UnrealEngine &engine, Address structObject, const std::uint8_t *current,
                         const std::uint8_t *proposed, std::size_t size, int depth = 0) {
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
                } else if (info->kind != PropertyKind::Struct ||
                           !StructChangeAllowed(engine, info->inner, current + at, proposed + at, width, depth + 1)) {
                    return false;
                }
            }
        }
    }
    return true;
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
    if (!resolved || !Assignable(engine, resolved->info, value))
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

// --- calling ------------------------------------------------------------------

// A frame as mods hold it: the parameter block, plus what the engine will leave
// in it that the loader must give back.
struct LoaderFrame {
    explicit LoaderFrame(FunctionInfo info) : frame(std::move(info)) {}
    CallFrame frame;
    // Written parameters owning engine memory, by index.
    std::vector<std::size_t> owned;
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

OwnedValues &Owned() {
    UnrealEngine &engine = UnrealEngine::Instance();
    static OwnedValues owned(engine.Finder(), engine.Chain(), engine.Values(), engine.Functions(), engine.Types(),
                             engine.ProcessEvent());
    return owned;
}

std::atomic<LogSink> g_log{nullptr};
std::mutex g_reportedMutex;
std::set<std::pair<Address, std::string>> g_reported;

// Once per function and message: mods call in loops.
void Report(Address function, const std::string &message) {
    const LogSink log = g_log.load(std::memory_order_acquire);
    if (!log)
        return;
    {
        std::lock_guard lock(g_reportedMutex);
        if (!g_reported.emplace(function, message).second)
            return;
    }
    const std::string name = UnrealEngine::Instance().Finder().NameOf(function).value_or("?");
    log(("[Unreal] " + name + ": " + message + ".").c_str());
}

bool OnGameThread() {
    const std::uint32_t id = ProcessEventHook::Instance().GameThreadId();
    return id != 0 && id == GetCurrentThreadId();
}

// Game thread. Releasing calls into the engine, which can come back here.
thread_local bool t_releasing = false;
std::mutex g_pendingMutex;
std::vector<std::unique_ptr<LoaderFrame>> g_pending;

void ReleaseFrame(LoaderFrame &loaderFrame) {
    OwnedValues &owned = Owned();
    const FunctionInfo &function = loaderFrame.frame.Function();
    auto *data = static_cast<std::uint8_t *>(loaderFrame.frame.Data());
    if (!owned.Ready()) {
        Report(function.function, "a returned value could not be released (" + owned.Failure() + ")");
        return;
    }
    for (const std::size_t index : loaderFrame.owned) {
        const PropertyInfo &info = function.parameters[index].info;
        for (std::int32_t i = 0; i < info.arrayDim; ++i) {
            if (!owned.Release(info, data + info.offset + static_cast<std::size_t>(i) * info.elementSize)) {
                Report(function.function, "a returned value could not be released (" + owned.Failure() + ")");
                return;
            }
        }
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
    for (std::size_t i = 0; i < info->parameters.size(); ++i) {
        const FunctionParameter &parameter = info->parameters[i];
        if (!Written(parameter))
            continue;
        const Ownership ownership = Owned().Classify(parameter.info);
        if (ownership == Ownership::Releasable)
            loaderFrame->owned.push_back(i);
        else if (ownership == Ownership::Unreleasable && loaderFrame->unreleasable.empty())
            loaderFrame->unreleasable = parameter.name + " (" + PropertyKindName(parameter.info.kind) + ")";
    }
    return reinterpret_cast<URK_UnrealCallFrame *>(loaderFrame.release());
}

void Unreal_CallFrameDestroy(URK_UnrealCallFrame *frame) {
    std::unique_ptr<LoaderFrame> loaderFrame(FrameOf(frame));
    if (!loaderFrame || !loaderFrame->called || loaderFrame->owned.empty())
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
    CallFrame *callFrame = &FrameOf(frame)->frame;
    const FunctionParameter *parameter = callFrame->Function().Parameter(parameterName);
    if (!parameter)
        return 0;
    const PropertyKind kind = parameter->info.kind;
    // The engine assigns over what it writes, freeing the old value: that value
    // must be its own (or empty), never a buffer the mod made.
    if (Written(*parameter) && kind != PropertyKind::Struct && Owned().Classify(parameter->info) != Ownership::None) {
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
                                      ", memory the engine allocates and the loader cannot release, so every "
                                      "call would leak");
        return 0;
    }
    if (!loaderFrame->owned.empty() && !Owned().Ready()) {
        Report(function.function, "call refused: it returns strings or arrays and the loader cannot release them (" +
                                      Owned().Failure() + ")");
        return 0;
    }
    ReleasePending();
    if (!InvokeProcessEvent(engine.ProcessEvent(), object, function.function, loaderFrame->frame.Data()))
        return 0;
    loaderFrame->called = true;
    return 1;
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
    if (UnrealEngine::Instance().Available() && OnGameThread())
        ReleasePending();
}

bool UnrealSdk_HoldProcessEventHook() {
    std::lock_guard lock(g_hookMutex);
    if (!EnsureHookInstalled())
        return false;
    g_loaderHold = true;
    return true;
}

} // namespace URK::Unreal
