// Shared state and services, availability, object lookup and type queries.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

HookInstaller g_installer;

bool CopyOut(const std::string &text, char *output, std::size_t outputSize) {
    if (!output || outputSize == 0)
        return false;
    const std::size_t copy = std::min(text.size(), outputSize - 1);
    std::memcpy(output, text.data(), copy);
    output[copy] = '\0';
    return true;
}

// Only handles still in the object array are touched; stale ones just fail.
bool Live(const UnrealEngine &engine, Address object) {
    return engine.Available() && IsLiveObject(engine.Finder(), object);
}

// Object member writes: member must be an object/class ref and the value of its class.
bool AssignableMember(const UnrealEngine &engine, const PropertyInfo &info, Address value) {
    return (info.kind == PropertyKind::Object || info.kind == PropertyKind::Class) && Assignable(engine, info, value);
}

// Members per class, keyed by class and package name (raw FNames: decoding them cost most of a read).
class MemberCache {
  public:
    std::optional<PropertyInfo> Find(const UnrealEngine &engine, Address classObject, std::string_view member) {
        const ObjectFinder &finder = engine.Finder();
        const Address package = finder.OuterOf(classObject);
        const std::optional<std::uint64_t> className = finder.NameKeyOf(classObject);
        const std::optional<std::uint64_t> packageName = finder.NameKeyOf(package);
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
        entry.members.insert_or_assign(std::string(member), *info);
        return info;
    }

  private:
    struct MemberHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view name) const { return std::hash<std::string_view>{}(name); }
    };

    struct ClassEntry {
        std::uint64_t name = 0;
        Address package = kNullAddress;
        std::uint64_t packageName = 0;
        std::unordered_map<std::string, PropertyInfo, MemberHash, std::equal_to<>> members;

        bool Same(std::uint64_t otherName, Address otherPackage, std::uint64_t otherPackageName) const {
            return package == otherPackage && name == otherName && packageName == otherPackageName;
        }
    };

    std::shared_mutex mutex_;
    std::unordered_map<Address, ClassEntry> classes_;
};

MemberCache g_members;

// Writes only what the caller's struct size covers (v1: no bool layout, v2: no type object).
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

// 4.25 is the floor, so a bootstrapped engine has FField properties even with no version.
int Unreal_UsesFieldProperties() {
    const UnrealEngine &engine = UnrealEngine::Instance();
    return engine.Available() || engine.Version().UsesFieldProperties() ? 1 : 0;
}

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

// Only after UnrealEngine::Available(): it holds the ladder's objects.
Services &Serve() {
    static Services services = [] {
        SetMemoryNote([](const std::string &message) { Report(kNullAddress, message); });
        return Services(UnrealEngine::Instance());
    }();
    return services;
}

std::atomic<LogSink> g_log{nullptr};

static std::mutex g_reportedMutex;

static std::set<std::pair<Address, std::string>> g_reported;

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

} // namespace URK::Unreal::SdkApi
