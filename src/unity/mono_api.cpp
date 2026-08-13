#include "mono_api.h"
#include "logger.h"
#include "runtime_wait.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
namespace {
std::atomic<MonoDomain *> g_publishedMonoDomain{nullptr};
thread_local MonoThread *g_currentMonoThread = nullptr;
thread_local int g_currentMonoAttachDepth = 0;
thread_local bool g_currentThreadUsesMono = false;

struct MonoLookupCaches {
    std::unordered_map<std::string, MonoImage *> images;
    std::unordered_map<std::string, MonoClass *> classes;
    std::unordered_map<std::string, MonoMethod *> methods;
    std::unordered_map<std::string, MonoMethod *> exactMethods;
    std::unordered_map<std::string, MonoClassField *> fields;
};

std::mutex g_monoLookupCacheMutex;
MonoLookupCaches g_monoLookupCaches;

void ClearMonoLookupCaches() {
    std::scoped_lock lock(g_monoLookupCacheMutex);
    g_monoLookupCaches.images.clear();
    g_monoLookupCaches.classes.clear();
    g_monoLookupCaches.methods.clear();
    g_monoLookupCaches.exactMethods.clear();
    g_monoLookupCaches.fields.clear();
}
} // namespace
void Mono_PublishDomain(MonoDomain *domain) {
    if (!domain)
        return;
    MonoDomain *previous = g_publishedMonoDomain.exchange(domain, std::memory_order_acq_rel);
    if (previous != domain)
        ClearMonoLookupCaches();
}
MonoDomain *Mono_PublishedDomain() {
    return g_publishedMonoDomain.load(std::memory_order_acquire);
}
bool Mono_AttachCurrentThread(MonoApi &api, MonoDomain *domain, const char *purpose, MonoThread **attachedThread) {
    if (attachedThread)
        *attachedThread = nullptr;
    if (g_currentThreadUsesMono) {
        if (!g_currentMonoThread && api.thread_current)
            g_currentMonoThread = api.thread_current();
        if (attachedThread)
            *attachedThread = g_currentMonoThread;
        return true;
    }

    Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_attach begin domain=%p "
        "purpose=%s",
        GetCurrentProcessId(), GetCurrentThreadId(), domain, purpose ? purpose : "unspecified");
    MonoThread *thread = nullptr;
    if (domain && api.thread_attach)
        thread = api.thread_attach(domain);
    if (thread) {
        g_currentMonoThread = thread;
        g_currentMonoAttachDepth = 1;
        g_currentThreadUsesMono = true;
    }
    if (attachedThread)
        *attachedThread = thread;
    Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_attach end domain=%p "
        "thread=%p attached=%s",
        GetCurrentProcessId(), GetCurrentThreadId(), domain, thread, thread ? "yes" : "no");
    return thread != nullptr;
}

void Mono_DetachThread(MonoApi &api, MonoThread *thread) {
    if (!thread || !api.thread_detach)
        return;

    Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_detach begin thread=%p", GetCurrentProcessId(),
        GetCurrentThreadId(), thread);
    api.thread_detach(thread);
    Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_detach end thread=%p", GetCurrentProcessId(), GetCurrentThreadId(),
        thread);

    if (thread == g_currentMonoThread) {
        g_currentMonoThread = nullptr;
        g_currentMonoAttachDepth = 0;
        g_currentThreadUsesMono = false;
    }
}

MonoThreadScope::MonoThreadScope(const MonoApi &api, MonoDomain *domain, const char *purpose)
    : MonoThreadScope(api, domain, purpose, Mode::AttachCurrentThread) {
}
MonoThreadScope::MonoThreadScope(const MonoApi &api, MonoDomain *domain, const char *purpose, Mode mode) : api_(&api) {
    if (g_currentThreadUsesMono) {
        thread_ = g_currentMonoThread;
        attached_ = true;
        ++g_currentMonoAttachDepth;
        return;
    }

    if (mode == Mode::BorrowExistingThread) {
        g_currentThreadUsesMono = true;
        g_currentMonoAttachDepth = 1;
        attached_ = true;
        return;
    }

    Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_attach begin domain=%p "
        "purpose=%s",
        GetCurrentProcessId(), GetCurrentThreadId(), domain, purpose ? purpose : "unspecified");
    if (domain && api.thread_attach)
        thread_ = api.thread_attach(domain);
    if (thread_) {
        g_currentMonoThread = thread_;
        g_currentMonoAttachDepth = 1;
        g_currentThreadUsesMono = true;
        attached_ = true;
        ownsAttach_ = true;
    }
    Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_attach end domain=%p "
        "thread=%p attached=%s",
        GetCurrentProcessId(), GetCurrentThreadId(), domain, thread_, thread_ ? "yes" : "no");
}
MonoThreadScope::~MonoThreadScope() {
    if (!attached_)
        return;

    if (g_currentMonoAttachDepth > 0)
        --g_currentMonoAttachDepth;

    if (!ownsAttach_) {
        if (g_currentMonoAttachDepth == 0) {
            g_currentMonoThread = nullptr;
            g_currentThreadUsesMono = false;
        }
        return;
    }

    if (g_currentMonoAttachDepth != 0)
        return;

    if (api_ && api_->thread_detach) {
        Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_detach begin thread=%p", GetCurrentProcessId(),
            GetCurrentThreadId(), thread_);
        api_->thread_detach(thread_);
        Log("[runtime][Mono] pid=%lu tid=%lu mono_thread_detach end thread=%p", GetCurrentProcessId(),
            GetCurrentThreadId(), thread_);
    } else {
        Log("[runtime][Mono][WARNING] mono_thread_detach unavailable for thread=%p.", thread_);
    }

    g_currentMonoThread = nullptr;
    g_currentThreadUsesMono = false;
}
bool MonoThreadScope::CurrentThreadAttached() {
    return g_currentThreadUsesMono;
}
static constexpr std::array kMonoCandidates{
    RuntimeModuleCandidate{"mono-2.0-bdwgc.dll", "mono_get_root_domain"},
    RuntimeModuleCandidate{"mono-2.0-sgen.dll", "mono_get_root_domain"},
    RuntimeModuleCandidate{"mono-2.0.dll", "mono_get_root_domain"},
    RuntimeModuleCandidate{"mono.dll", "mono_get_root_domain"},
};
template <class T> static bool Bind(HMODULE m, const char *n, T &o, bool req) {
    o = reinterpret_cast<T>(GetProcAddress(m, n));
    if (o)
        Log("[runtime][Mono] export %-40s -> %p%s", n, reinterpret_cast<void *>(o),
            req ? " [required]" : " [optional]");
    else
        Log(req ? "[ERROR] [runtime][Mono] required export missing: %s"
                : "[WARN] [runtime][Mono] optional export missing: %s",
            n);
    return o || !req;
}

static const char *Basename(const char *value) {
    if (!value)
        return nullptr;
    const char *slash = std::strrchr(value, '/');
    const char *backslash = std::strrchr(value, '\\');
    const char *base = nullptr;
    if (slash && backslash)
        base = slash > backslash ? slash : backslash;
    else
        base = slash ? slash : backslash;
    return base ? base + 1 : value;
}

static bool EqualsIgnoreCase(const char *a, const char *b) {
    return a && b && _stricmp(a, b) == 0;
}

static bool EndsWithDll(const char *value) {
    if (!value)
        return false;
    const size_t len = std::strlen(value);
    return len > 4 && _stricmp(value + len - 4, ".dll") == 0;
}

static std::string StripDll(const char *value) {
    if (!value)
        return {};
    std::string result(value);
    if (EndsWithDll(result.c_str()))
        result.resize(result.size() - 4);
    return result;
}

static bool MonoImageNameMatches(const char *actual, const char *wanted) {
    if (!actual || !wanted || !*wanted)
        return false;

    if (EqualsIgnoreCase(actual, wanted))
        return true;
    if (EqualsIgnoreCase(Basename(actual), wanted))
        return true;

    const std::string actualNoDll = StripDll(actual);
    const std::string actualBaseNoDll = StripDll(Basename(actual));
    const std::string wantedNoDll = StripDll(wanted);

    return (!wantedNoDll.empty() && (EqualsIgnoreCase(actualNoDll.c_str(), wantedNoDll.c_str()) ||
                                     EqualsIgnoreCase(actualBaseNoDll.c_str(), wantedNoDll.c_str())));
}

static std::string NormalizeMonoTypeName(std::string type) {
    std::string suffix;
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        suffix.insert(suffix.begin(), type.back());
        type.pop_back();
    }
    if (type.rfind("class ", 0) == 0)
        type.erase(0, 6);
    if (type.rfind("struct ", 0) == 0)
        type.erase(0, 7);
    std::transform(type.begin(), type.end(), type.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (type == "bool" || type == "boolean" || type == "system.boolean")
        type = "system.boolean";
    else if (type == "byte" || type == "system.byte")
        type = "system.byte";
    else if (type == "sbyte" || type == "system.sbyte")
        type = "system.sbyte";
    else if (type == "char" || type == "system.char")
        type = "system.char";
    else if (type == "short" || type == "int16" || type == "system.int16")
        type = "system.int16";
    else if (type == "ushort" || type == "uint16" || type == "system.uint16")
        type = "system.uint16";
    else if (type == "int" || type == "int32" || type == "system.int32")
        type = "system.int32";
    else if (type == "uint" || type == "uint32" || type == "system.uint32")
        type = "system.uint32";
    else if (type == "long" || type == "int64" || type == "system.int64")
        type = "system.int64";
    else if (type == "ulong" || type == "uint64" || type == "system.uint64")
        type = "system.uint64";
    else if (type == "float" || type == "single" || type == "system.single")
        type = "system.single";
    else if (type == "double" || type == "system.double")
        type = "system.double";
    else if (type == "string" || type == "system.string")
        type = "system.string";
    else if (type == "object" || type == "system.object")
        type = "system.object";
    else if (type == "void" || type == "system.void")
        type = "system.void";
    return type + suffix;
}

static bool MonoTypeNameMatches(const char *actual, const char *wanted) {
    if (!actual || !wanted)
        return false;
    return NormalizeMonoTypeName(actual) == NormalizeMonoTypeName(wanted);
}

static std::string LowerString(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

static std::string PointerKey(const void *ptr) {
    return std::to_string(reinterpret_cast<uintptr_t>(ptr));
}

static std::string MonoRuntimeCachePrefix(const MonoApi &api) {
    std::string key = PointerKey(api.module);
    key += "|";
    key += PointerKey(g_publishedMonoDomain.load(std::memory_order_acquire));
    return key;
}

static std::string MonoImageLookupKey(const char *image) {
    const char *base = Basename(image);
    std::string key = StripDll(base ? base : image);
    return LowerString(key);
}

static std::string MonoClassLookupKey(const MonoApi &api, const char *image, const char *namespc, const char *name) {
    std::string key = MonoRuntimeCachePrefix(api);
    key += "|class|";
    key += MonoImageLookupKey(image);
    key += "|";
    key += LowerString(namespc ? namespc : "");
    key += "|";
    key += LowerString(name ? name : "");
    return key;
}

static std::string MonoMethodLookupKey(const MonoClass *klass, const char *methodName, int argc) {
    std::string key = PointerKey(klass);
    key += "|method|";
    key += LowerString(methodName ? methodName : "");
    key += "|";
    key += std::to_string(argc);
    return key;
}

static std::string MonoExactMethodLookupKey(const MonoClass *klass, const char *methodName,
                                            const char *const *parameterTypes, int parameterCount) {
    std::string key = PointerKey(klass);
    key += "|exact|";
    key += LowerString(methodName ? methodName : "");
    key += "|";
    key += std::to_string(parameterCount);
    for (int i = 0; i < parameterCount; ++i) {
        key += "|";
        key += NormalizeMonoTypeName(parameterTypes && parameterTypes[i] ? parameterTypes[i] : "");
    }
    return key;
}

static std::string MonoFieldLookupKey(const MonoClass *klass, const char *fieldName) {
    std::string key = PointerKey(klass);
    key += "|field|";
    key += LowerString(fieldName ? fieldName : "");
    return key;
}

template <typename T>
bool TryGetCached(const std::unordered_map<std::string, T> &cache, const std::string &key, T &value) {
    auto found = cache.find(key);
    if (found == cache.end())
        return false;
    value = found->second;
    return true;
}

#define R(n, m) ok &= Bind(api.module, n, api.m, true)
#define O(n, m) Bind(api.module, n, api.m, false)
bool Mono_Resolve(MonoApi &api, int timeout) {
    api = {};
    ClearMonoLookupCaches();
    const RuntimeModule runtime = WaitForRuntime("Mono", kMonoCandidates, std::chrono::milliseconds{timeout});
    if (!runtime)
        return false;
    api.module = runtime.handle;
    api.base = reinterpret_cast<uintptr_t>(api.module);
    bool ok = true;
    O("mono_jit_init", jit_init);
    R("mono_get_root_domain", get_root_domain);
    R("mono_thread_attach", thread_attach);
    R("mono_thread_detach", thread_detach);
    R("mono_image_loaded", image_loaded);
    R("mono_class_from_name", class_from_name);
    O("mono_class_get_method_from_name", class_get_method_from_name);
    R("mono_compile_method", compile_method);
    O("mono_lookup_internal_call", lookup_internal_call);
    O("mono_object_new", object_new);
    O("mono_value_box", value_box);
    O("mono_type_get_object", type_get_object);
    O("mono_runtime_object_init", runtime_object_init);
#define X(n, m) O(n, m);

    X("mono_domain_get", domain_get)
    X("mono_assembly_foreach", assembly_foreach)
    X("mono_domain_assembly_open", domain_assembly_open)
    X("mono_assembly_get_image", assembly_get_image)
    X("mono_image_get_name", image_get_name)
    X("mono_image_get_filename", image_get_filename)
    X("mono_image_get_table_rows", image_get_table_rows)
    X("mono_class_get", class_get)
    X("mono_class_get_name", class_get_name)
    X("mono_class_get_namespace", class_get_namespace)
    X("mono_class_get_parent", class_get_parent)
    X("mono_class_get_type", class_get_type)
    X("mono_class_is_valuetype", class_is_valuetype)
    X("mono_class_is_enum", class_is_enum)
    X("mono_class_get_flags", class_get_flags)
    X("mono_class_get_methods", class_get_methods)
    X("mono_class_get_properties", class_get_properties)
    X("mono_method_get_name", method_get_name)
    X("mono_method_is_generic", method_is_generic)
    X("mono_method_get_object", method_get_object)
    X("mono_method_get_flags", method_get_flags)
    X("mono_method_signature", method_signature)
    X("mono_signature_get_param_count", signature_get_param_count)
    X("mono_signature_get_params", signature_get_params)
    X("mono_signature_get_return_type", signature_get_return_type)
    X("mono_type_get_name", type_get_name)
    X("mono_class_get_fields", class_get_fields)
    X("mono_field_get_name", field_get_name)
    X("mono_field_get_type", field_get_type)
    X("mono_field_get_offset", field_get_offset)
    X("mono_field_get_flags", field_get_flags)
    X("mono_field_get_value", field_get_value)
    X("mono_field_static_get_value", field_static_get_value)
    X("mono_field_set_value", field_set_value)
    X("mono_property_get_name", property_get_name)
    X("mono_property_get_get_method", property_get_get_method)
    X("mono_property_get_set_method", property_get_set_method)
    X("mono_runtime_invoke", runtime_invoke)
    X("mono_object_get_class", object_get_class)
    X("mono_object_unbox", object_unbox)
    X("mono_string_new", string_new)
    X("mono_string_to_utf8", string_to_utf8)
    X("mono_string_length", string_length)
    X("mono_free", free_)
    X("mono_array_length", array_length)
    X("mono_array_addr_with_size", array_addr_with_size)
    X("mono_gc_wbarrier_set_arrayref", gc_wbarrier_set_arrayref)
    X("mono_gchandle_new", gchandle_new)
    X("mono_gchandle_new_weakref", gchandle_new_weakref)
    X("mono_gchandle_get_target", gchandle_get_target)
    X("mono_gchandle_free", gchandle_free)
    X("mono_jit_init_version", jit_init_version)
    X("mono_thread_current", thread_current)
    X("mono_class_get_nested_types", class_get_nested_types)
    X("mono_class_get_interfaces", class_get_interfaces)
    X("mono_type_get_type", type_get_type)
    X("mono_type_get_attrs", type_get_attrs)
    X("mono_type_get_class", type_get_class)
    X("mono_field_static_set_value", field_static_set_value)
    X("mono_property_get_flags", property_get_flags)
    X("mono_class_vtable", class_vtable)
#undef X
    if (!ok || !api.valid()) {
        Log("[ERROR] Mono API resolution was incomplete.");
        return false;
    }
    Log("[SUCCESS] Mono API resolved; optional capabilities are available per non-null "
        "export.");
    return true;
}
#undef R
#undef O

bool MonoApi::valid() const {
    return module && get_root_domain && thread_attach && thread_detach && image_loaded && class_from_name &&
           compile_method;
}

MonoImage *MonoApi::FindImage(const char *image) const {
    if (!image || !*image)
        return nullptr;

    const std::string cacheKey = MonoRuntimeCachePrefix(*this) + "|image|" + MonoImageLookupKey(image);
    {
        MonoImage *cached = nullptr;
        std::scoped_lock lock(g_monoLookupCacheMutex);
        if (TryGetCached(g_monoLookupCaches.images, cacheKey, cached))
            return cached;
    }

    if (image_loaded) {
        if (MonoImage *direct = image_loaded(image)) {
            std::scoped_lock lock(g_monoLookupCacheMutex);
            g_monoLookupCaches.images[cacheKey] = direct;
            return direct;
        }

        const std::string noDll = StripDll(image);
        if (!noDll.empty() && std::strcmp(noDll.c_str(), image) != 0) {
            if (MonoImage *direct = image_loaded(noDll.c_str())) {
                std::scoped_lock lock(g_monoLookupCacheMutex);
                g_monoLookupCaches.images[cacheKey] = direct;
                return direct;
            }
        }
    }

    if (!assembly_foreach || !assembly_get_image)
        return nullptr;

    struct Search {
        const MonoApi *api;
        const char *image;
        MonoImage *result;
    } search{this, image, nullptr};

    assembly_foreach(
        [](MonoAssembly *assembly, void *user) {
            auto *search = static_cast<Search *>(user);
            if (!search || search->result || !assembly)
                return;

            MonoImage *candidate = search->api->assembly_get_image(assembly);
            if (!candidate)
                return;

            const char *name = search->api->image_get_name ? search->api->image_get_name(candidate) : nullptr;
            const char *filename =
                search->api->image_get_filename ? search->api->image_get_filename(candidate) : nullptr;

            if (MonoImageNameMatches(name, search->image) || MonoImageNameMatches(filename, search->image)) {
                search->result = candidate;
            }
        },
        &search);

    if (search.result) {
        std::scoped_lock lock(g_monoLookupCacheMutex);
        g_monoLookupCaches.images[cacheKey] = search.result;
    }
    return search.result;
}

MonoClass *MonoApi::FindClass(const char *image, const char *namespc, const char *name) const {
    if (!class_from_name || !name)
        return nullptr;

    const bool cacheable = image && *image;
    std::string cacheKey;
    if (cacheable) {
        cacheKey = MonoClassLookupKey(*this, image, namespc, name);
        MonoClass *cached = nullptr;
        {
            std::scoped_lock lock(g_monoLookupCacheMutex);
            if (TryGetCached(g_monoLookupCaches.classes, cacheKey, cached))
                return cached;
        }
    }

    auto fromImage = [&](MonoImage *img) -> MonoClass * {
        return img ? class_from_name(img, namespc ? namespc : "", name) : nullptr;
    };

    if (image && *image) {
        MonoClass *klass = fromImage(FindImage(image));
        if (klass) {
            std::scoped_lock lock(g_monoLookupCacheMutex);
            g_monoLookupCaches.classes[cacheKey] = klass;
        }
        return klass;
    }

    if (!assembly_foreach || !assembly_get_image)
        return nullptr;

    struct Search {
        const MonoApi *api;
        const char *namespc;
        const char *name;
        MonoClass *result;
    } search{this, namespc ? namespc : "", name, nullptr};

    assembly_foreach(
        [](MonoAssembly *assembly, void *user) {
            auto *search = static_cast<Search *>(user);
            if (!search || search->result || !assembly)
                return;

            MonoImage *candidate = search->api->assembly_get_image(assembly);
            if (!candidate)
                return;

            search->result =
                search->api->class_from_name(candidate, search->namespc ? search->namespc : "", search->name);
        },
        &search);

    return search.result;
}

MonoMethod *MonoApi::FindMethod(const char *image, const char *namespc, const char *klass, const char *method_name,
                                int argc) const {
    MonoClass *k = FindClass(image, namespc, klass);

    if (!k || !method_name)
        return nullptr;

    const std::string cacheKey = MonoMethodLookupKey(k, method_name, argc);
    {
        MonoMethod *cached = nullptr;
        std::scoped_lock lock(g_monoLookupCacheMutex);
        if (TryGetCached(g_monoLookupCaches.methods, cacheKey, cached))
            return cached;
    }

    if (!class_get_methods || !method_get_name || (argc >= 0 && (!method_signature || !signature_get_param_count))) {
        Log("[runtime][Mono][ERROR] method lookup unavailable: exact name/arity metadata exports are missing: "
            "image='%s' type='%s.%s' method='%s' argc=%d.",
            image ? image : "", namespc ? namespc : "", klass ? klass : "", method_name, argc);
        return nullptr;
    }

    MonoMethod *match = nullptr;
    void *iterator = nullptr;
    while (MonoMethod *method = class_get_methods(k, &iterator)) {
        const char *name = method_get_name(method);
        if (!name || std::strcmp(name, method_name) != 0)
            continue;
        if (argc >= 0) {
            MonoMethodSignature *sig = method_signature(method);
            if (!sig || static_cast<int>(signature_get_param_count(sig)) != argc)
                continue;
        }
        if (match && match != method) {
            Log("[runtime][Mono][ERROR] method lookup is ambiguous: image='%s' type='%s.%s' method='%s' argc=%d "
                "first=%p second=%p.",
                image ? image : "", namespc ? namespc : "", klass ? klass : "", method_name, argc, match, method);
            return nullptr;
        }
        match = method;
    }
    if (match) {
        std::scoped_lock lock(g_monoLookupCacheMutex);
        g_monoLookupCaches.methods[cacheKey] = match;
    }
    return match;
}

MonoMethod *MonoApi::FindMethodExact(const char *image, const char *namespc, const char *klass, const char *method_name,
                                     const char *const *parameter_types, int parameter_count) const {
    if (parameter_count < 0) {
        Log("[runtime][Mono][ERROR] exact method lookup rejected a negative parameter count: "
            "image='%s' type='%s.%s' method='%s' parameterCount=%d.",
            image ? image : "", namespc ? namespc : "", klass ? klass : "", method_name ? method_name : "",
            parameter_count);
        return nullptr;
    }

    if (parameter_count > 0 && !parameter_types) {
        Log("[runtime][Mono][ERROR] exact method lookup requires parameter type names: "
            "image='%s' type='%s.%s' method='%s' parameterCount=%d.",
            image ? image : "", namespc ? namespc : "", klass ? klass : "", method_name ? method_name : "",
            parameter_count);
        return nullptr;
    }
    for (int i = 0; i < parameter_count; ++i) {
        if (!parameter_types[i] || !parameter_types[i][0]) {
            Log("[runtime][Mono][ERROR] exact method lookup rejected an empty parameter type: "
                "image='%s' type='%s.%s' method='%s' parameterIndex=%d parameterCount=%d.",
                image ? image : "", namespc ? namespc : "", klass ? klass : "", method_name ? method_name : "", i,
                parameter_count);
            return nullptr;
        }
    }

    if (!class_get_methods || !method_get_name || !method_signature || !signature_get_param_count ||
        (parameter_count > 0 && (!signature_get_params || !type_get_name))) {
        Log("[runtime][Mono][ERROR] exact method lookup unavailable: required metadata exports are missing "
            "(mono_class_get_methods=%s mono_method_get_name=%s mono_method_signature=%s "
            "mono_signature_get_param_count=%s mono_signature_get_params=%s mono_type_get_name=%s).",
            class_get_methods ? "yes" : "no", method_get_name ? "yes" : "no", method_signature ? "yes" : "no",
            signature_get_param_count ? "yes" : "no", signature_get_params ? "yes" : "no",
            type_get_name ? "yes" : "no");
        return nullptr;
    }
    if (parameter_count > 0 && !free_) {
        static std::atomic_bool loggedMissingFree{false};
        if (!loggedMissingFree.exchange(true, std::memory_order_acq_rel)) {
            Log("[WARN] Mono exact method lookup unavailable: mono_type_get_name "
                "requires mono_free to avoid leaking runtime-allocated memory.");
        }
        return nullptr;
    }

    MonoClass *k = FindClass(image, namespc, klass);
    if (!k || !method_name)
        return nullptr;

    const std::string cacheKey = MonoExactMethodLookupKey(k, method_name, parameter_types, parameter_count);
    {
        MonoMethod *cached = nullptr;
        std::scoped_lock lock(g_monoLookupCacheMutex);
        if (TryGetCached(g_monoLookupCaches.exactMethods, cacheKey, cached))
            return cached;
    }

    for (MonoClass *current = k; current; current = class_get_parent ? class_get_parent(current) : nullptr) {
        MonoMethod *exact_match = nullptr;
        void *iterator = nullptr;
        while (MonoMethod *method = class_get_methods(current, &iterator)) {
            const char *name = method_get_name(method);
            if (!name || std::strcmp(name, method_name) != 0)
                continue;

            MonoMethodSignature *sig = method_signature(method);
            if (!sig || static_cast<int>(signature_get_param_count(sig)) != parameter_count)
                continue;

            bool matches = true;
            void *param_iterator = nullptr;
            for (int i = 0; i < parameter_count; ++i) {
                MonoType *param = signature_get_params(sig, &param_iterator);
                char *raw = param ? type_get_name(param) : nullptr;
                matches = raw && MonoTypeNameMatches(raw, parameter_types[i]);

                if (raw)
                    free_(raw);

                if (!matches)
                    break;
            }

            if (matches) {
                if (exact_match && exact_match != method) {
                    Log("[runtime][Mono][ERROR] exact method lookup is ambiguous: image='%s' type='%s.%s' "
                        "method='%s' parameterCount=%d first=%p second=%p.",
                        image ? image : "", namespc ? namespc : "", klass ? klass : "", method_name, parameter_count,
                        exact_match, method);
                    return nullptr;
                }
                exact_match = method;
            }
        }

        if (exact_match) {
            std::scoped_lock lock(g_monoLookupCacheMutex);
            g_monoLookupCaches.exactMethods[cacheKey] = exact_match;
            return exact_match;
        }
    }
    return nullptr;
}

MonoClassField *MonoApi::FindField(const char *image, const char *namespc, const char *klass,
                                   const char *field_name) const {
    MonoClass *k = FindClass(image, namespc, klass);

    if (!k || !class_get_fields || !field_get_name || !field_name)
        return nullptr;

    const std::string cacheKey = MonoFieldLookupKey(k, field_name);
    {
        MonoClassField *cached = nullptr;
        std::scoped_lock lock(g_monoLookupCacheMutex);
        if (TryGetCached(g_monoLookupCaches.fields, cacheKey, cached))
            return cached;
    }

    void *iterator = nullptr;

    while (MonoClassField *field = class_get_fields(k, &iterator)) {
        const char *name = field_get_name(field);

        if (name && std::strcmp(name, field_name) == 0) {
            std::scoped_lock lock(g_monoLookupCacheMutex);
            g_monoLookupCaches.fields[cacheKey] = field;
            return field;
        }
    }

    return nullptr;
}

void *MonoApi::CompileMethodSafe(MonoMethod *method, uint32_t *native_exception) const {
    uint32_t local_native_exception = 0;
    if (!native_exception)
        native_exception = &local_native_exception;
    *native_exception = 0;

    if (!compile_method || !method) {
        Log("[runtime][Mono][ERROR] JIT compile rejected: method=%p mono_compile_method=%s.", method,
            compile_method ? "available" : "unavailable");
        return nullptr;
    }

    void *target = nullptr;
    __try {
        target = compile_method(method);
    } __except (native_exception ? (*native_exception = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
                                 : EXCEPTION_EXECUTE_HANDLER) {
        Log("[runtime][Mono][ERROR] mono_compile_method raised native exception 0x%08X for method=%p.",
            *native_exception, method);
        return nullptr;
    }

    if (!target) {
        Log("[runtime][Mono][ERROR] mono_compile_method returned a null native target for method=%p.", method);
        return nullptr;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(target, &memory, sizeof(memory)) != sizeof(memory)) {
        Log("[runtime][Mono][ERROR] JIT target validation failed: VirtualQuery target=%p error=%lu.", target,
            GetLastError());
        return nullptr;
    }

    const DWORD protection = memory.Protect & 0xFFu;
    const bool executable = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    if (memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD) != 0 || protection == PAGE_NOACCESS ||
        !executable) {
        Log("[runtime][Mono][ERROR] JIT target validation failed: target=%p state=0x%08lX protect=0x%08lX "
            "allocationBase=%p; expected committed executable memory.",
            target, memory.State, memory.Protect, memory.AllocationBase);
        return nullptr;
    }

    return target;
}

MonoObject *MonoApi::RuntimeInvokeSafe(MonoMethod *method, void *object, void **params, MonoObject **exception,
                                        uint32_t *native_exception) const {
    if (exception)
        *exception = nullptr;

    uint32_t local_native_exception = 0;
    if (!native_exception)
        native_exception = &local_native_exception;
    *native_exception = 0;

    if (!runtime_invoke || !method)
        return nullptr;

    __try {
        return runtime_invoke(method, object, params, exception);
    } __except (native_exception ? (*native_exception = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)
                                 : EXCEPTION_EXECUTE_HANDLER) {
        Log("[ERROR] Mono runtime invoke raised native exception 0x%08X.", *native_exception);

        return nullptr;
    }
}

MonoString *MonoApi::NewString(const char *value) const {
    MonoDomain *domain = domain_get ? domain_get() : (get_root_domain ? get_root_domain() : nullptr);

    return domain && string_new && value ? string_new(domain, value) : nullptr;
}

size_t MonoApi::ArrayLength(MonoArray *array) const {
    return array && array_length ? static_cast<size_t>(array_length(array)) : 0;
}

std::string MonoApi::ObjectClassName(MonoObject *object) const {
    if (!object || !object_get_class || !class_get_name)
        return {};

    MonoClass *klass = object_get_class(object);

    const char *name = klass ? class_get_name(klass) : nullptr;

    const char *namespc = klass && class_get_namespace ? class_get_namespace(klass) : nullptr;

    if (!name)
        return {};

    if (namespc && *namespc)
        return std::string(namespc) + "." + name;

    return std::string(name);
}
