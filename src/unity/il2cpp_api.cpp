#include "il2cpp_api.h"

#include "hook_manager.h"
#include "il2cpp_export_policy.h"
#include "logger.h"
#include "runtime_wait.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {
Il2CppApi *g_api = nullptr;
thread_local std::string g_lastError;

constexpr std::size_t kMaxManagedStringUtf8Bytes = 4u * 1024u * 1024u;
constexpr std::int32_t kMaxManagedStringUtf16Units = 1024 * 1024;

std::string JoinTypes(const char *const *types, int count);
std::string Lower(std::string s);
std::string BaseName(std::string s);
std::string StripDll(std::string s);
std::string NormalizeTypeName(std::string type);

bool Empty(const char *s) {
    return !s || !s[0];
}
void SetError(const std::string &message) {
    g_lastError = message.empty() ? "IL2CPP: unknown error" : message;
}
void SetError(const char *message) {
    SetError(std::string(message ? message : "IL2CPP: unknown error"));
}

__declspec(noinline) bool SehCopyMemory(const void *source, void *destination, size_t size, DWORD *exceptionCode) {
    __try {
        std::memcpy(destination, source, size);
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

__declspec(noinline) bool SehBoundedStringLength(const char *value, size_t limit, size_t *length,
                                                 DWORD *exceptionCode) {
    __try {
        const void *terminator = std::memchr(value, '\0', limit);
        *length = terminator ? static_cast<size_t>(static_cast<const char *>(terminator) - value) : limit;
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

template <typename Function, typename Result, typename... Args>
__declspec(noinline) bool SehInvokeValue(Function function, Result *result, DWORD *exceptionCode, Args... args) {
    static_assert(std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>);
    static_assert(std::is_trivially_copyable_v<Result>);
    static_assert((std::is_trivially_copyable_v<Args> && ...));
    __try {
        *result = function(args...);
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

__declspec(noinline) int SehMethodPointerGet(const Il2CppMethod *method, void **target, DWORD *exceptionCode) {
    __try {
        *target = *reinterpret_cast<void *const *>(method);
        return 1;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return 0;
    }
}

__declspec(noinline) int SehFieldStaticGet(il2cpp_field_static_get_value_t fn, Il2CppClassField *field, void *out) {
    __try {
        fn(field, out);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char message[320]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: il2cpp_field_static_get_value raised native exception 0x%08lX field=%p output=%p",
                      GetExceptionCode(), field, out);
        SetError(message);
        return 0;
    }
}

__declspec(noinline) int SehFieldStaticSet(il2cpp_field_static_set_value_t fn, Il2CppClassField *field, void *value) {
    __try {
        fn(field, value);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char message[320]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: il2cpp_field_static_set_value raised native exception 0x%08lX field=%p value=%p",
                      GetExceptionCode(), field, value);
        SetError(message);
        return 0;
    }
}

__declspec(noinline) int SehFieldGet(il2cpp_field_get_value_t fn, Il2CppObject *object, Il2CppClassField *field,
                                     void *out) {
    __try {
        fn(object, field, out);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char message[384]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: il2cpp_field_get_value raised native exception 0x%08lX object=%p field=%p output=%p",
                      GetExceptionCode(), object, field, out);
        SetError(message);
        return 0;
    }
}

__declspec(noinline) int SehFieldSet(il2cpp_field_set_value_t fn, Il2CppObject *object, Il2CppClassField *field,
                                     void *value) {
    __try {
        fn(object, field, value);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        char message[384]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: il2cpp_field_set_value raised native exception 0x%08lX object=%p field=%p value=%p",
                      GetExceptionCode(), object, field, value);
        SetError(message);
        return 0;
    }
}

std::string PtrString(const void *ptr) {
    std::ostringstream oss;
    oss << ptr;
    return oss.str();
}

struct Il2CppLookupCaches {
    std::unordered_map<std::string, const Il2CppImage *> images;
    std::unordered_map<std::string, Il2CppClass *> classes;
    std::unordered_map<std::string, const Il2CppMethod *> methods;
    std::unordered_map<std::string, const Il2CppMethod *> exactMethods;
    std::unordered_map<std::string, Il2CppClassField *> fields;
};

std::mutex g_il2cppLookupCacheMutex;
Il2CppLookupCaches g_il2cppLookupCaches;

void ClearIl2CppCaches() {
    std::scoped_lock lock(g_il2cppLookupCacheMutex);
    g_il2cppLookupCaches.images.clear();
    g_il2cppLookupCaches.classes.clear();
    g_il2cppLookupCaches.methods.clear();
    g_il2cppLookupCaches.exactMethods.clear();
    g_il2cppLookupCaches.fields.clear();
}

std::string PointerKey(const void *ptr) {
    return std::to_string(reinterpret_cast<uintptr_t>(ptr));
}

std::string ImageLookupKey(const char *imageName) {
    if (Empty(imageName))
        return {};
    return Lower(StripDll(BaseName(imageName)));
}

std::string RuntimeCachePrefix(const Il2CppApi &api) {
    std::string key = PointerKey(api.cachedDomain);
    key += "|";
    key += std::to_string(api.gameAssemblyBase);
    key += "|";
    key += std::to_string(api.unityPlayerBase);
    return key;
}

std::string ClassLookupKey(const Il2CppApi &api, const char *imageName, const char *namespc, const char *name) {
    std::string key = RuntimeCachePrefix(api);
    key += "|class|";
    key += ImageLookupKey(imageName);
    key += "|";
    key += namespc ? namespc : "";
    key += "|";
    key += name ? name : "";
    return key;
}

std::string MethodLookupKey(const Il2CppClass *klass, const char *name, int argc) {
    std::string key = PointerKey(klass);
    key += "|method|";
    key += name ? name : "";
    key += "|";
    key += std::to_string(argc);
    return key;
}

std::string ExactMethodLookupKey(const Il2CppClass *klass, const char *name, const char *const *parameterTypes,
                                 int parameterCount) {
    std::string key = PointerKey(klass);
    key += "|exact|";
    key += name ? name : "";
    key += "|";
    key += std::to_string(parameterCount);
    for (int i = 0; i < parameterCount; ++i) {
        key += "|";
        key += NormalizeTypeName(parameterTypes && parameterTypes[i] ? parameterTypes[i] : "");
    }
    return key;
}

std::string FieldLookupKey(const Il2CppClass *klass, const char *name) {
    std::string key = PointerKey(klass);
    key += "|field|";
    key += name ? name : "";
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

void FillManagedHookResult(URK_Il2CppManagedHookResult *result, const void *method, void *nativeTarget,
                           const char *diagnostic) {
    if (!result || result->size < sizeof(URK_Il2CppManagedHookResult))
        return;
    result->method = method;
    result->native_target = nativeTarget;
    result->diagnostic = diagnostic;
}

std::string ManagedMethodIdentity(const URK_Il2CppManagedMethodDesc &desc) {
    std::string out = desc.image_name ? desc.image_name : "<null-image>";
    out += " / ";
    if (desc.namespc && desc.namespc[0]) {
        out += desc.namespc;
        out += ".";
    }
    out += desc.class_name ? desc.class_name : "<null-class>";
    out += " / ";
    out += desc.method_name ? desc.method_name : "<null-method>";
    out += "(";
    out += JoinTypes(desc.parameter_type_names, desc.parameter_count > 0 ? desc.parameter_count : 0);
    out += ")";
    return out;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string BaseName(std::string s) {
    const size_t slash = s.find_last_of("/\\");
    if (slash != std::string::npos)
        s.erase(0, slash + 1);
    return s;
}

std::string StripDll(std::string s) {
    if (s.size() > 4 && Lower(s.substr(s.size() - 4)) == ".dll")
        s.resize(s.size() - 4);
    return s;
}

std::vector<std::string> ImageNameVariants(const char *value) {
    std::vector<std::string> variants;
    if (Empty(value))
        return variants;
    const std::string original = Lower(value);
    const std::string base = BaseName(original);
    for (const std::string &v : {original, StripDll(original), base, StripDll(base)}) {
        if (!v.empty() && std::find(variants.begin(), variants.end(), v) == variants.end())
            variants.push_back(v);
    }
    return variants;
}

bool ImageNameMatches(const std::vector<std::string> &requestedVariants, const char *actual) {
    if (requestedVariants.empty())
        return false;
    const auto actualVariants = ImageNameVariants(actual);
    for (const auto &r : requestedVariants)
        for (const auto &a : actualVariants)
            if (r == a)
                return true;
    return false;
}

std::string NormalizeTypeName(std::string type) {
    std::string suffix;
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        suffix.insert(suffix.begin(), type.back());
        type.pop_back();
    }
    if (type.rfind("class ", 0) == 0)
        type.erase(0, 6);
    if (type.rfind("struct ", 0) == 0)
        type.erase(0, 7);
    const std::string alias = Lower(type);
    if (alias == "bool" || alias == "boolean" || alias == "system.boolean")
        type = "system.boolean";
    else if (alias == "byte" || alias == "system.byte")
        type = "system.byte";
    else if (alias == "sbyte" || alias == "system.sbyte")
        type = "system.sbyte";
    else if (alias == "char" || alias == "system.char")
        type = "system.char";
    else if (alias == "short" || alias == "int16" || alias == "system.int16")
        type = "system.int16";
    else if (alias == "ushort" || alias == "uint16" || alias == "system.uint16")
        type = "system.uint16";
    else if (alias == "int" || alias == "int32" || alias == "system.int32")
        type = "system.int32";
    else if (alias == "uint" || alias == "uint32" || alias == "system.uint32")
        type = "system.uint32";
    else if (alias == "long" || alias == "int64" || alias == "system.int64")
        type = "system.int64";
    else if (alias == "ulong" || alias == "uint64" || alias == "system.uint64")
        type = "system.uint64";
    else if (alias == "float" || alias == "single" || alias == "system.single")
        type = "system.single";
    else if (alias == "double" || alias == "system.double")
        type = "system.double";
    else if (alias == "string" || alias == "system.string")
        type = "system.string";
    else if (alias == "object" || alias == "system.object")
        type = "system.object";
    else if (alias == "type" || alias == "system.type")
        type = "system.type";
    else if (alias == "void" || alias == "system.void")
        type = "system.void";
    return type + suffix;
}

bool TypeNameMatches(const std::string &actual, const char *requested) {
    return NormalizeTypeName(actual) == NormalizeTypeName(requested ? requested : "");
}

std::string JoinTypes(const char *const *types, int count) {
    std::string out;
    for (int i = 0; i < count; ++i) {
        if (i)
            out += ", ";
        out += types[i] ? types[i] : "<null>";
    }
    return out;
}

struct PeExportRecord {
    FARPROC address = nullptr;
    DWORD rva = 0;
    WORD ordinal = 0;
};

// Link-time identical-code folding is common in Unity GameAssembly builds. It can
// legally make several export names point to one implementation when their x64
// calling ABI is identical. The resolver therefore compares the ABI, not API
// names: an alias is safe only when result and argument machine classes match.
enum class ExportAbiValue : std::uint8_t { Void, Gpr8, Gpr16, Gpr32, Gpr64, Floating32, Floating64 };

struct ExportAbiSignature {
    static constexpr size_t kMaxArguments = 8;
    ExportAbiValue result = ExportAbiValue::Void;
    std::uint8_t argumentCount = 0;
    std::array<ExportAbiValue, kMaxArguments> arguments{};

    bool operator==(const ExportAbiSignature &) const = default;
};

template <typename T>
constexpr ExportAbiValue ExportAbiValueOf() {
    using Value = std::remove_cv_t<std::remove_reference_t<T>>;
    if constexpr (std::is_void_v<Value>)
        return ExportAbiValue::Void;
    else if constexpr (std::is_reference_v<T> || std::is_pointer_v<Value>)
        return ExportAbiValue::Gpr64;
    else if constexpr (std::is_same_v<Value, float>)
        return ExportAbiValue::Floating32;
    else if constexpr (std::is_same_v<Value, double>)
        return ExportAbiValue::Floating64;
    else if constexpr (sizeof(Value) == 1)
        return ExportAbiValue::Gpr8;
    else if constexpr (sizeof(Value) == 2)
        return ExportAbiValue::Gpr16;
    else if constexpr (sizeof(Value) == 4)
        return ExportAbiValue::Gpr32;
    else
        return ExportAbiValue::Gpr64;
}

template <typename Result, typename... Args>
constexpr ExportAbiSignature ExportAbiOf(Result (*)(Args...)) {
    static_assert(sizeof...(Args) <= ExportAbiSignature::kMaxArguments,
                  "extend ExportAbiSignature if an IL2CPP export takes more arguments");
    ExportAbiSignature signature{};
    signature.result = ExportAbiValueOf<Result>();
    signature.argumentCount = static_cast<std::uint8_t>(sizeof...(Args));
    size_t index = 0;
    ((signature.arguments[index++] = ExportAbiValueOf<Args>()), ...);
    return signature;
}

const char *ExportAbiValueName(ExportAbiValue value) {
    switch (value) {
    case ExportAbiValue::Void:
        return "void";
    case ExportAbiValue::Gpr8:
        return "gpr8";
    case ExportAbiValue::Gpr16:
        return "gpr16";
    case ExportAbiValue::Gpr32:
        return "gpr32";
    case ExportAbiValue::Gpr64:
        return "gpr64";
    case ExportAbiValue::Floating32:
        return "fp32";
    case ExportAbiValue::Floating64:
        return "fp64";
    }
    return "unknown";
}

std::string ExportAbiName(const ExportAbiSignature &signature) {
    std::string text = ExportAbiValueName(signature.result);
    text += "(";
    for (std::uint8_t i = 0; i < signature.argumentCount; ++i) {
        if (i)
            text += ",";
        text += ExportAbiValueName(signature.arguments[i]);
    }
    text += ")";
    return text;
}

struct BoundExport {
    std::string name;
    ExportAbiSignature abi;
};

class StrictIl2CppExportResolver {
  public:
    bool Initialize(HMODULE module) {
        module_ = module;
        exports_.clear();
        assigned_.clear();
        failure_.clear();
        imageSize_ = 0;
        exportRva_ = 0;
        exportSize_ = 0;
        optionalUnavailable_ = 0;
        sharedExactTargets_ = 0;

        if (!module_) {
            return Fail("GameAssembly module handle is null");
        }

        const auto failRead = [this](const char *context, DWORD exceptionCode) {
            char message[160]{};
            std::snprintf(message, sizeof(message), "native exception 0x%08lX while reading %s",
                          static_cast<unsigned long>(exceptionCode), context);
            return Fail(message);
        };

        const auto *base = reinterpret_cast<const std::uint8_t *>(module_);
        DWORD exceptionCode = 0;
        IMAGE_DOS_HEADER dos{};
        if (!SehCopyMemory(base, &dos, sizeof(dos), &exceptionCode))
            return failRead("GameAssembly DOS header", exceptionCode);
        if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0 ||
            static_cast<std::uint32_t>(dos.e_lfanew) > 0x100000u) {
            return Fail("invalid DOS header");
        }

        IMAGE_NT_HEADERS64 nt{};
        if (!SehCopyMemory(base + dos.e_lfanew, &nt, sizeof(nt), &exceptionCode))
            return failRead("GameAssembly NT headers", exceptionCode);
        if (nt.Signature != IMAGE_NT_SIGNATURE || nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.OptionalHeader.SizeOfImage == 0) {
            return Fail("invalid x64 NT header");
        }

        imageSize_ = nt.OptionalHeader.SizeOfImage;
        const IMAGE_DATA_DIRECTORY directory = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
        if (directory.VirtualAddress == 0 || directory.Size < sizeof(IMAGE_EXPORT_DIRECTORY) ||
            !RangeInImage(directory.VirtualAddress, directory.Size)) {
            return Fail("missing or invalid export directory");
        }
        exportRva_ = directory.VirtualAddress;
        exportSize_ = directory.Size;

        IMAGE_EXPORT_DIRECTORY exportDirectory{};
        if (!SehCopyMemory(base + exportRva_, &exportDirectory, sizeof(exportDirectory), &exceptionCode))
            return failRead("GameAssembly export directory", exceptionCode);
        const size_t functionTableSize = static_cast<size_t>(exportDirectory.NumberOfFunctions) * sizeof(DWORD);
        const size_t nameTableSize = static_cast<size_t>(exportDirectory.NumberOfNames) * sizeof(DWORD);
        const size_t ordinalTableSize = static_cast<size_t>(exportDirectory.NumberOfNames) * sizeof(WORD);
        if (exportDirectory.NumberOfFunctions == 0 || exportDirectory.NumberOfNames == 0 ||
            exportDirectory.NumberOfFunctions > 1000000u || exportDirectory.NumberOfNames > 1000000u ||
            !RangeInImage(exportDirectory.AddressOfFunctions, functionTableSize) ||
            !RangeInImage(exportDirectory.AddressOfNames, nameTableSize) ||
            !RangeInImage(exportDirectory.AddressOfNameOrdinals, ordinalTableSize)) {
            return Fail("malformed export address/name tables");
        }

        std::vector<DWORD> functions(exportDirectory.NumberOfFunctions);
        std::vector<DWORD> names(exportDirectory.NumberOfNames);
        std::vector<WORD> ordinals(exportDirectory.NumberOfNames);
        if (!SehCopyMemory(base + exportDirectory.AddressOfFunctions, functions.data(), functionTableSize,
                           &exceptionCode) ||
            !SehCopyMemory(base + exportDirectory.AddressOfNames, names.data(), nameTableSize, &exceptionCode) ||
            !SehCopyMemory(base + exportDirectory.AddressOfNameOrdinals, ordinals.data(), ordinalTableSize,
                           &exceptionCode)) {
            return failRead("GameAssembly export tables", exceptionCode);
        }

        for (DWORD i = 0; i < exportDirectory.NumberOfNames; ++i) {
            const WORD functionIndex = ordinals[i];
            if (functionIndex >= exportDirectory.NumberOfFunctions || !RangeInImage(names[i], 1))
                return Fail("malformed export name ordinal");

            const char *exportNameAddress = reinterpret_cast<const char *>(base + names[i]);
            const size_t remaining = imageSize_ - names[i];
            size_t length = 0;
            if (!SehBoundedStringLength(exportNameAddress, remaining, &length, &exceptionCode))
                return failRead("GameAssembly export name", exceptionCode);
            if (length == remaining)
                return Fail("unterminated export name");

            std::string exportName(length, '\0');
            if (length != 0 && !SehCopyMemory(exportNameAddress, exportName.data(), length, &exceptionCode))
                return failRead("GameAssembly export name", exceptionCode);

            const DWORD functionRva = functions[functionIndex];
            if (!functionRva || !RangeInImage(functionRva, 1))
                return Fail(std::string("invalid target RVA for export ") + exportName);
            if (functionRva >= exportRva_ && static_cast<size_t>(functionRva - exportRva_) < exportSize_)
                return Fail(std::string("forwarded export is not supported: ") + exportName);

            exports_.emplace(std::move(exportName),
                             PeExportRecord{reinterpret_cast<FARPROC>(const_cast<std::uint8_t *>(base + functionRva)),
                                            functionRva,
                                            static_cast<WORD>(exportDirectory.Base + functionIndex)});
        }
        Log("[IL2CPP][EXPORT] Validated GameAssembly PE export table: imageSize=0x%lX names=%lu functions=%lu.",
            static_cast<unsigned long>(imageSize_), static_cast<unsigned long>(exportDirectory.NumberOfNames),
            static_cast<unsigned long>(exportDirectory.NumberOfFunctions));
        return true;
    }

    FARPROC BindExact(const char *name, ExportAbiSignature abi, Il2CppExportRequirement requirement, bool &ok) {
        const auto found = exports_.find(name ? name : "");
        if (found == exports_.end()) {
            const Il2CppExportBindingDecision decision =
                Il2CppExportPolicy_Decide(requirement, false, false);
            if (decision.failStartup) {
                ok = false;
                Log("[IL2CPP][ERROR] Required exact export GameAssembly.dll!%s is missing; no fallback will be used.",
                    name ? name : "<null>");
            } else {
                Log("[IL2CPP][EXPORT] Optional exact export GameAssembly.dll!%s is not present.",
                    name ? name : "<null>");
                ++optionalUnavailable_;
            }
            return nullptr;
        }

        const PeExportRecord &record = found->second;
        const FARPROC win32Address = GetProcAddress(module_, name);
        if (win32Address != record.address) {
            const Il2CppExportBindingDecision decision =
                Il2CppExportPolicy_Decide(requirement, true, false);
            if (decision.failStartup)
                ok = false;
            else
                ++optionalUnavailable_;
            Log(decision.failStartup
                    ? "[IL2CPP][ERROR] Exact export mismatch for %s: PE=%p (RVA=0x%08lX ordinal=%hu) "
                      "GetProcAddress=%p."
                    : "[IL2CPP][WARNING] Optional exact export mismatch for %s: PE=%p "
                      "(RVA=0x%08lX ordinal=%hu) GetProcAddress=%p; capability disabled.",
                name, reinterpret_cast<void *>(record.address), static_cast<unsigned long>(record.rva), record.ordinal,
                reinterpret_cast<void *>(win32Address));
            return nullptr;
        }

        MEMORY_BASIC_INFORMATION memory{};
        HMODULE addressModule = nullptr;
        const auto targetAddress = reinterpret_cast<const void *>(reinterpret_cast<uintptr_t>(record.address));
        const DWORD protection = VirtualQuery(targetAddress, &memory, sizeof(memory)) == sizeof(memory)
                                     ? memory.Protect & 0xff
                                     : 0;
        const bool executable = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
        if (memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0 || !executable ||
            !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                 reinterpret_cast<LPCSTR>(reinterpret_cast<uintptr_t>(record.address)), &addressModule) ||
            addressModule != module_) {
            const Il2CppExportBindingDecision decision =
                Il2CppExportPolicy_Decide(requirement, true, false);
            if (decision.failStartup)
                ok = false;
            else
                ++optionalUnavailable_;
            Log(decision.failStartup
                    ? "[IL2CPP][ERROR] Export %s target validation failed: address=%p RVA=0x%08lX state=0x%lX "
                      "protect=0x%lX owner=%p expected=%p."
                    : "[IL2CPP][WARNING] Optional export %s target validation failed: address=%p RVA=0x%08lX "
                      "state=0x%lX protect=0x%lX owner=%p expected=%p; capability disabled.",
                name, reinterpret_cast<void *>(record.address), static_cast<unsigned long>(record.rva),
                static_cast<unsigned long>(memory.State), static_cast<unsigned long>(memory.Protect),
                static_cast<void *>(addressModule), static_cast<void *>(module_));
            return nullptr;
        }

        const auto duplicate = assigned_.find(reinterpret_cast<uintptr_t>(record.address));
        if (duplicate != assigned_.end()) {
            if (duplicate->second.abi != abi) {
                static_assert(Il2CppExportPolicy_AcceptSharedExactTarget());
                ++sharedExactTargets_;
                Log("[IL2CPP][EXPORT] Shared exact target: %s (RVA=0x%08lX ABI=%s) and %s (ABI=%s) both "
                    "resolve to %p; accepted after exact-name and target validation.",
                    name, static_cast<unsigned long>(record.rva), ExportAbiName(abi).c_str(),
                    duplicate->second.name.c_str(), ExportAbiName(duplicate->second.abi).c_str(),
                    reinterpret_cast<void *>(record.address));
                return record.address;
            }
            ++sharedExactTargets_;
            Log("[IL2CPP][EXPORT] %s => %p RVA=0x%08lX ordinal=%hu exact=yes aliasOf=%s ABI=%s accepted=yes.",
                name, reinterpret_cast<void *>(record.address), static_cast<unsigned long>(record.rva), record.ordinal,
                duplicate->second.name.c_str(), ExportAbiName(abi).c_str());
            return record.address;
        }
        assigned_.emplace(reinterpret_cast<uintptr_t>(record.address), BoundExport{name, abi});
        Log("[IL2CPP][EXPORT] %s => %p RVA=0x%08lX ordinal=%hu exact=yes executable=yes ABI=%s.", name,
            reinterpret_cast<void *>(record.address), static_cast<unsigned long>(record.rva), record.ordinal,
            ExportAbiName(abi).c_str());
        return record.address;
    }

    const std::string &failure() const { return failure_; }
    size_t optional_unavailable() const { return optionalUnavailable_; }
    size_t shared_exact_targets() const { return sharedExactTargets_; }

  private:
    bool RangeInImage(size_t rva, size_t size) const {
        return rva < imageSize_ && size <= imageSize_ - rva;
    }

    bool Fail(std::string message) {
        failure_ = std::move(message);
        return false;
    }

    HMODULE module_ = nullptr;
    DWORD imageSize_ = 0;
    DWORD exportRva_ = 0;
    DWORD exportSize_ = 0;
    std::unordered_map<std::string, PeExportRecord> exports_;
    std::unordered_map<uintptr_t, BoundExport> assigned_;
    std::string failure_;
    size_t optionalUnavailable_ = 0;
    size_t sharedExactTargets_ = 0;
};

template <typename Fn, typename... Args>
std::invoke_result_t<Fn, Args...> InvokeMetadata(const char *operation, Fn function, Args... args) {
    using Result = std::invoke_result_t<Fn, Args...>;
    static_assert(!std::is_void_v<Result>, "InvokeMetadata is for value-returning IL2CPP metadata APIs only");
    Result result{};
    DWORD exceptionCode = 0;
    if (SehInvokeValue(function, &result, &exceptionCode, args...))
        return result;

    char message[384]{};
    std::snprintf(message, sizeof(message),
                  "IL2CPP: %s raised native exception 0x%08lX; metadata operation was blocked.",
                  operation ? operation : "metadata call", static_cast<unsigned long>(exceptionCode));
    SetError(message);
    Log("[IL2CPP][ERROR] %s", message);
    if (g_api) {
        g_api->metadataReady = false;
        ClearIl2CppCaches();
        Log("[IL2CPP][ERROR] Metadata access has been disabled after the guarded failure; "
            "subsequent native mod metadata calls will be rejected.");
    }
    return Result{};
}

bool ValidateMetadataCString(const char *value, const char *operation) {
    if (!value) {
        SetError(std::string("IL2CPP: ") + (operation ? operation : "metadata string") + " returned null");
        return false;
    }
    constexpr size_t kMaxMetadataNameBytes = 16 * 1024;
    size_t length = 0;
    DWORD exceptionCode = 0;
    if (!SehBoundedStringLength(value, kMaxMetadataNameBytes, &length, &exceptionCode)) {
        char message[256]{};
        std::snprintf(message, sizeof(message), "IL2CPP: %s returned an unreadable string (0x%08lX)",
                      operation ? operation : "metadata string", static_cast<unsigned long>(exceptionCode));
        SetError(message);
        Log("[IL2CPP][ERROR] %s", message);
        return false;
    }
    if (length == kMaxMetadataNameBytes) {
        SetError(std::string("IL2CPP: ") + (operation ? operation : "metadata string") +
                 " returned an unterminated or oversized string");
        return false;
    }
    return true;
}

class Il2CppThreadScope {
  public:
    explicit Il2CppThreadScope(const Il2CppApi &api) : api_(api) {
        if (attach_depth_ > 0) {
            attached_ = true;
            ++attach_depth_;
            return;
        }
        if (!api_.thread_attach_available()) {
            SetError("IL2CPP: thread attach APIs are unavailable; metadata access "
                     "requires il2cpp_thread_attach and il2cpp_thread_detach");
            return;
        }
        if (api_.il2cpp_thread_current) {
            Il2CppThread *current = api_.il2cpp_thread_current();
            if (current) {
                thread_ = current;
                attached_ = true;
                ++attach_depth_;
                return;
            }
        }
        Il2CppDomain *domain = api_.cachedDomain;
        if (!domain) {
            SetError("IL2CPP: domain unavailable while attaching current thread for "
                     "metadata access");
            return;
        }
        thread_ = api_.il2cpp_thread_attach(domain);
        if (!thread_) {
            SetError("IL2CPP: il2cpp_thread_attach failed; metadata access is "
                     "disabled on this native thread");
            return;
        }
        attached_ = true;
        owns_attach_ = true;
        ++attach_depth_;
        if (!logged_attach_) {
            Log("[IL2CPP] Attached native thread %lu for IL2CPP metadata calls.", GetCurrentThreadId());
            logged_attach_ = true;
        }
    }

    ~Il2CppThreadScope() {
        if (attached_ && attach_depth_ > 0)
            --attach_depth_;
        if (owns_attach_ && attach_depth_ == 0 && thread_ && api_.il2cpp_thread_detach)
            api_.il2cpp_thread_detach(thread_);
    }

    bool ok() const {
        return attached_;
    }

  private:
    const Il2CppApi &api_;
    Il2CppThread *thread_ = nullptr;
    bool attached_ = false;
    bool owns_attach_ = false;
    static thread_local bool logged_attach_;
    static thread_local int attach_depth_;
};

thread_local bool Il2CppThreadScope::logged_attach_ = false;
thread_local int Il2CppThreadScope::attach_depth_ = 0;

std::string TypeName(const Il2CppApi &api, const Il2CppType *type) {
    Il2CppThreadScope attach(api);
    if (!attach.ok())
        return {};
    if (!api.il2cpp_type_get_name || !type)
        return {};
    if (!api.il2cpp_free) {
        SetError("IL2CPP: il2cpp_type_get_name requires il2cpp_free to avoid "
                 "leaking runtime-allocated memory");
        return {};
    }
    char *raw = InvokeMetadata("il2cpp_type_get_name during exact method lookup", api.il2cpp_type_get_name, type);
    if (!raw)
        return {};
    if (!ValidateMetadataCString(raw, "il2cpp_type_get_name")) {
        api.il2cpp_free(raw);
        return {};
    }
    std::string name(raw);
    api.il2cpp_free(raw);
    return name;
}

int Api_IsAvailable() {
    return g_api && g_api->MetadataAccessReady() ? 1 : 0;
}
const void *Api_DomainGet() {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return nullptr;
    }
    auto *domain = g_api->Domain();
    if (!domain)
        SetError("IL2CPP: domain unavailable while calling domain_get");
    return domain;
}
const void *Api_FindImage(const char *image) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return nullptr;
    }
    if (Empty(image)) {
        SetError("IL2CPP: image name is required");
        return nullptr;
    }
    const auto *found = g_api->FindImage(image);
    if (!found)
        SetError(std::string("IL2CPP: image not found: requested=\"") + image + "\"");
    return found;
}
const void *Api_FindClass(const char *image, const char *ns, const char *name) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return nullptr;
    }
    if (Empty(image) || Empty(name)) {
        SetError("IL2CPP: image and class name are required");
        return nullptr;
    }
    auto *klass = g_api->FindClass(image, ns ? ns : "", name);
    if (!klass)
        SetError(std::string("IL2CPP: class not found: image=\"") + image + "\" namespace=\"" + (ns ? ns : "") +
                 "\" class=\"" + name + "\"");
    return klass;
}
const void *Api_FindMethod(const void *klass, const char *name, int argc) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return nullptr;
    }
    if (!klass || Empty(name) || argc < 0) {
        SetError("IL2CPP: class, method name, and non-negative argc are required");
        return nullptr;
    }
    g_lastError.clear();
    const auto *method = g_api->FindMethod(static_cast<Il2CppClass *>(const_cast<void *>(klass)), name, argc);
    if (!method && g_lastError.empty())
        SetError(std::string("IL2CPP: method not found: class=") + PtrString(klass) + " method=\"" + name +
                 "\" argc=" + std::to_string(argc));
    return method;
}
const void *Api_FindMethodExact(const void *klass, const char *name, const char *const *types, int count) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return nullptr;
    }
    if (!klass || Empty(name) || count < 0 || (count > 0 && !types)) {
        SetError("IL2CPP: invalid exact method lookup input");
        return nullptr;
    }
    for (int i = 0; i < count; ++i)
        if (Empty(types[i])) {
            SetError("IL2CPP: parameter type names must be non-empty");
            return nullptr;
        }
    g_lastError.clear();
    const auto *method =
        g_api->FindMethodExact(static_cast<Il2CppClass *>(const_cast<void *>(klass)), name, types, count);
    if (!method && g_lastError.empty())
        SetError(std::string("IL2CPP: exact overload not found: class=") + PtrString(klass) + " method=\"" + name +
                 "\" params=[" + JoinTypes(types, count) + "]");
    return method;
}
void *Api_MethodPointer(const void *method) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: method_pointer requires an initialized IL2CPP API");
        return nullptr;
    }
    if (!method) {
        SetError("IL2CPP: method_pointer requires a non-null method handle; native target resolution was not "
                 "attempted");
        return nullptr;
    }
    return g_api->MethodPointer(static_cast<const Il2CppMethod *>(method));
}
const void *Api_FindField(const void *klass, const char *name) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return nullptr;
    }
    if (!klass || Empty(name)) {
        SetError("IL2CPP: class and field name are required");
        return nullptr;
    }
    auto *field = g_api->FindField(static_cast<Il2CppClass *>(const_cast<void *>(klass)), name);
    if (!field)
        SetError(std::string("IL2CPP: field not found: class=") + PtrString(klass) + " field=\"" + name + "\"");
    return field;
}
int32_t Api_FieldOffset(const void *field) {
    if (!g_api || !g_api->valid()) {
        SetError("IL2CPP: API is not initialized");
        return -1;
    }
    if (!field) {
        SetError("IL2CPP: field handle is required");
        return -1;
    }
    Il2CppThreadScope attach(*g_api);
    if (!attach.ok())
        return -1;
    g_lastError.clear();
    const size_t offset = InvokeMetadata("il2cpp_field_get_offset", g_api->il2cpp_field_get_offset,
                                         static_cast<Il2CppClassField *>(const_cast<void *>(field)));
    if (!g_lastError.empty())
        return -1;
    if (offset > static_cast<size_t>(std::numeric_limits<int32_t>::max())) {
        SetError("IL2CPP: field offset exceeds public ABI int32_t range");
        return -1;
    }
    return static_cast<int32_t>(offset);
}
const char *Api_LastError() {
    return g_lastError.c_str();
}

#define REQAPI(fn, msg, ret)                                                                                           \
    if (!g_api || !g_api->fn) {                                                                                        \
        SetError(msg);                                                                                                 \
        return ret;                                                                                                    \
    }
#define ATTACHAPI(ret)                                                                                                 \
    Il2CppThreadScope attach(*g_api);                                                                                  \
    if (!attach.ok())                                                                                                  \
        return ret;
size_t Api_DomainAssemblyCount() {
    if (!g_api || !g_api->MetadataAccessReady())
        return 0;
    Il2CppThreadScope a(*g_api);
    if (!a.ok())
        return 0;
    size_t c = 0;
    return InvokeMetadata("il2cpp_domain_get_assemblies", g_api->il2cpp_domain_get_assemblies, g_api->Domain(), &c)
               ? c
               : 0;
}
const void *Api_DomainAssembly(size_t index) {
    if (!g_api || !g_api->MetadataAccessReady())
        return nullptr;
    Il2CppThreadScope a(*g_api);
    if (!a.ok())
        return nullptr;
    size_t c = 0;
    auto as = InvokeMetadata("il2cpp_domain_get_assemblies", g_api->il2cpp_domain_get_assemblies, g_api->Domain(),
                             &c);
    return as && index < c ? as[index] : nullptr;
}
const void *Api_AssemblyImage(const void *assembly) {
    REQAPI(il2cpp_assembly_get_image, "IL2CPP: il2cpp_assembly_get_image unavailable", nullptr);
    ATTACHAPI(nullptr);
    return assembly ? InvokeMetadata("il2cpp_assembly_get_image", g_api->il2cpp_assembly_get_image,
                                     static_cast<const Il2CppAssembly *>(assembly))
                    : nullptr;
}
const char *Api_ImageName(const void *image) {
    REQAPI(il2cpp_image_get_name, "IL2CPP: il2cpp_image_get_name unavailable", nullptr);
    ATTACHAPI(nullptr);
    const char *name = image ? InvokeMetadata("il2cpp_image_get_name", g_api->il2cpp_image_get_name,
                                              static_cast<const Il2CppImage *>(image))
                             : nullptr;
    return name && ValidateMetadataCString(name, "il2cpp_image_get_name") ? name : nullptr;
}
size_t Api_ImageClassCount(const void *image) {
    REQAPI(il2cpp_image_get_class_count, "IL2CPP: il2cpp_image_get_class_count unavailable", 0);
    ATTACHAPI(0);
    return image ? InvokeMetadata("il2cpp_image_get_class_count", g_api->il2cpp_image_get_class_count,
                                  static_cast<const Il2CppImage *>(image))
                 : 0;
}
const void *Api_ImageClass(const void *image, size_t i) {
    REQAPI(il2cpp_image_get_class, "IL2CPP: il2cpp_image_get_class unavailable", nullptr);
    ATTACHAPI(nullptr);
    return image ? InvokeMetadata("il2cpp_image_get_class", g_api->il2cpp_image_get_class,
                                  static_cast<const Il2CppImage *>(image), i)
                 : nullptr;
}
const char *Api_ClassName(const void *k) {
    REQAPI(il2cpp_class_get_name, "IL2CPP: il2cpp_class_get_name unavailable", nullptr);
    ATTACHAPI(nullptr);
    const char *name = k ? InvokeMetadata("il2cpp_class_get_name", g_api->il2cpp_class_get_name,
                                          (Il2CppClass *)const_cast<void *>(k))
                         : nullptr;
    return name && ValidateMetadataCString(name, "il2cpp_class_get_name") ? name : nullptr;
}
const char *Api_ClassNamespace(const void *k) {
    REQAPI(il2cpp_class_get_namespace, "IL2CPP: il2cpp_class_get_namespace unavailable", nullptr);
    ATTACHAPI(nullptr);
    const char *namespc = k ? InvokeMetadata("il2cpp_class_get_namespace", g_api->il2cpp_class_get_namespace,
                                              (Il2CppClass *)const_cast<void *>(k))
                             : nullptr;
    return namespc && ValidateMetadataCString(namespc, "il2cpp_class_get_namespace") ? namespc : nullptr;
}
const void *Api_ClassParent(const void *k) {
    REQAPI(il2cpp_class_get_parent, "IL2CPP: il2cpp_class_get_parent unavailable", nullptr);
    ATTACHAPI(nullptr);
    return k ? g_api->il2cpp_class_get_parent((Il2CppClass *)const_cast<void *>(k)) : nullptr;
}
uint32_t Api_ClassFlags(const void *k) {
    REQAPI(il2cpp_class_get_flags, "IL2CPP: il2cpp_class_get_flags unavailable", 0);
    ATTACHAPI(0);
    return k ? g_api->il2cpp_class_get_flags((Il2CppClass *)const_cast<void *>(k)) : 0;
}
int Api_ClassValueType(const void *k) {
    REQAPI(il2cpp_class_is_valuetype, "IL2CPP: il2cpp_class_is_valuetype unavailable", 0);
    ATTACHAPI(0);
    return k && g_api->il2cpp_class_is_valuetype((const Il2CppClass *)k) ? 1 : 0;
}
int Api_ClassEnum(const void *k) {
    REQAPI(il2cpp_class_is_enum, "IL2CPP: il2cpp_class_is_enum unavailable", 0);
    ATTACHAPI(0);
    return k && g_api->il2cpp_class_is_enum((const Il2CppClass *)k) ? 1 : 0;
}
const void *Api_ClassFields(const void *k, void **it) {
    REQAPI(il2cpp_class_get_fields, "IL2CPP: il2cpp_class_get_fields unavailable", nullptr);
    ATTACHAPI(nullptr);
    return k && it ? g_api->il2cpp_class_get_fields((Il2CppClass *)const_cast<void *>(k), it) : nullptr;
}
const void *Api_ClassMethods(const void *k, void **it) {
    REQAPI(il2cpp_class_get_methods, "IL2CPP: il2cpp_class_get_methods unavailable", nullptr);
    ATTACHAPI(nullptr);
    return k && it ? g_api->il2cpp_class_get_methods((Il2CppClass *)const_cast<void *>(k), it) : nullptr;
}
const void *Api_ClassProperties(const void *k, void **it) {
    REQAPI(il2cpp_class_get_properties, "IL2CPP: il2cpp_class_get_properties unavailable", nullptr);
    ATTACHAPI(nullptr);
    return k && it ? g_api->il2cpp_class_get_properties((Il2CppClass *)const_cast<void *>(k), it) : nullptr;
}
const void *Api_ClassNested(const void *k, void **it) {
    REQAPI(il2cpp_class_get_nested_types, "IL2CPP: il2cpp_class_get_nested_types unavailable", nullptr);
    ATTACHAPI(nullptr);
    return k && it ? g_api->il2cpp_class_get_nested_types((Il2CppClass *)const_cast<void *>(k), it) : nullptr;
}
const void *Api_ClassInterfaces(const void *k, void **it) {
    REQAPI(il2cpp_class_get_interfaces, "IL2CPP: il2cpp_class_get_interfaces unavailable", nullptr);
    ATTACHAPI(nullptr);
    return k && it ? g_api->il2cpp_class_get_interfaces((Il2CppClass *)const_cast<void *>(k), it) : nullptr;
}
const char *Api_MethodName(const void *m) {
    REQAPI(il2cpp_method_get_name, "IL2CPP: il2cpp_method_get_name unavailable", nullptr);
    ATTACHAPI(nullptr);
    const char *name = m ? InvokeMetadata("il2cpp_method_get_name", g_api->il2cpp_method_get_name,
                                          (const Il2CppMethod *)m)
                         : nullptr;
    return name && ValidateMetadataCString(name, "il2cpp_method_get_name") ? name : nullptr;
}
const void *Api_MethodClass(const void *m) {
    REQAPI(il2cpp_method_get_class, "IL2CPP: il2cpp_method_get_class unavailable", nullptr);
    ATTACHAPI(nullptr);
    return m ? g_api->il2cpp_method_get_class((const Il2CppMethod *)m) : nullptr;
}
uint32_t Api_MethodParamCount(const void *m) {
    REQAPI(il2cpp_method_get_param_count, "IL2CPP: il2cpp_method_get_param_count unavailable", 0);
    ATTACHAPI(0);
    return m ? g_api->il2cpp_method_get_param_count((const Il2CppMethod *)m) : 0;
}
const void *Api_MethodParam(const void *m, uint32_t i) {
    REQAPI(il2cpp_method_get_param, "IL2CPP: il2cpp_method_get_param unavailable", nullptr);
    ATTACHAPI(nullptr);
    return m ? g_api->il2cpp_method_get_param((const Il2CppMethod *)m, i) : nullptr;
}
const void *Api_MethodReturn(const void *m) {
    REQAPI(il2cpp_method_get_return_type, "IL2CPP: il2cpp_method_get_return_type unavailable", nullptr);
    ATTACHAPI(nullptr);
    return m ? g_api->il2cpp_method_get_return_type((const Il2CppMethod *)m) : nullptr;
}
uint32_t Api_MethodFlags(const void *m, uint32_t *f) {
    REQAPI(il2cpp_method_get_flags, "IL2CPP: il2cpp_method_get_flags unavailable", 0);
    ATTACHAPI(0);
    return m ? g_api->il2cpp_method_get_flags((const Il2CppMethod *)m, f) : 0;
}
uint32_t Api_MethodToken(const void *m) {
    REQAPI(il2cpp_method_get_token, "IL2CPP: il2cpp_method_get_token unavailable", 0);
    ATTACHAPI(0);
    return m ? g_api->il2cpp_method_get_token((const Il2CppMethod *)m) : 0;
}
const char *Api_FieldName(const void *f) {
    REQAPI(il2cpp_field_get_name, "IL2CPP: il2cpp_field_get_name unavailable", nullptr);
    ATTACHAPI(nullptr);
    const char *name = f ? InvokeMetadata("il2cpp_field_get_name", g_api->il2cpp_field_get_name,
                                          (Il2CppClassField *)const_cast<void *>(f))
                         : nullptr;
    return name && ValidateMetadataCString(name, "il2cpp_field_get_name") ? name : nullptr;
}
const void *Api_FieldType(const void *f) {
    REQAPI(il2cpp_field_get_type, "IL2CPP: il2cpp_field_get_type unavailable", nullptr);
    ATTACHAPI(nullptr);
    return f ? g_api->il2cpp_field_get_type((Il2CppClassField *)const_cast<void *>(f)) : nullptr;
}
uint32_t Api_FieldFlags(const void *f) {
    REQAPI(il2cpp_field_get_flags, "IL2CPP: il2cpp_field_get_flags unavailable", 0);
    ATTACHAPI(0);
    return f ? g_api->il2cpp_field_get_flags((Il2CppClassField *)const_cast<void *>(f)) : 0;
}
int Api_FieldStaticGet(const void *f, void *o) {
    g_lastError.clear();
    REQAPI(il2cpp_field_static_get_value, "IL2CPP: il2cpp_field_static_get_value unavailable", 0);
    ATTACHAPI(0);
    if (!f || !o) {
        SetError("IL2CPP: field_static_get_value requires field and output");
        return 0;
    }
    return SehFieldStaticGet(g_api->il2cpp_field_static_get_value, (Il2CppClassField *)const_cast<void *>(f), o);
}
int Api_FieldStaticSet(const void *f, void *v) {
    g_lastError.clear();
    REQAPI(il2cpp_field_static_set_value, "IL2CPP: il2cpp_field_static_set_value unavailable", 0);
    ATTACHAPI(0);
    if (!f) {
        SetError("IL2CPP: field_static_set_value requires field");
        return 0;
    }
    return SehFieldStaticSet(g_api->il2cpp_field_static_set_value, (Il2CppClassField *)const_cast<void *>(f), v);
}
const char *Api_PropertyName(const void *p) {
    REQAPI(il2cpp_property_get_name, "IL2CPP: il2cpp_property_get_name unavailable", nullptr);
    ATTACHAPI(nullptr);
    return p ? g_api->il2cpp_property_get_name((Il2CppProperty *)const_cast<void *>(p)) : nullptr;
}
const void *Api_PropertyGet(const void *p) {
    REQAPI(il2cpp_property_get_get_method, "IL2CPP: il2cpp_property_get_get_method unavailable", nullptr);
    ATTACHAPI(nullptr);
    return p ? g_api->il2cpp_property_get_get_method((Il2CppProperty *)const_cast<void *>(p)) : nullptr;
}
const void *Api_PropertySet(const void *p) {
    REQAPI(il2cpp_property_get_set_method, "IL2CPP: il2cpp_property_get_set_method unavailable", nullptr);
    ATTACHAPI(nullptr);
    return p ? g_api->il2cpp_property_get_set_method((Il2CppProperty *)const_cast<void *>(p)) : nullptr;
}
uint32_t Api_PropertyFlags(const void *p) {
    REQAPI(il2cpp_property_get_flags, "IL2CPP: il2cpp_property_get_flags unavailable", 0);
    ATTACHAPI(0);
    return p ? g_api->il2cpp_property_get_flags((Il2CppProperty *)const_cast<void *>(p)) : 0;
}
int Api_TypeName(const void *t, char *out, size_t n) {
    if (out && n)
        out[0] = 0;
    if (!g_api || !g_api->il2cpp_type_get_name || !t || !out || !n)
        return 0;
    if (!g_api->il2cpp_free) {
        SetError("IL2CPP: il2cpp_type_get_name requires il2cpp_free to avoid "
                 "leaking runtime-allocated memory");
        return 0;
    }
    ATTACHAPI(0);
    char *r = g_api->il2cpp_type_get_name((const Il2CppType *)t);
    if (!r)
        return 0;
    const size_t count = (std::min)(std::strlen(r), n - 1);
    std::memcpy(out, r, count);
    out[count] = 0;
    g_api->il2cpp_free(r);
    return 1;
}
int32_t Api_TypeKind(const void *t) {
    REQAPI(il2cpp_type_get_type, "IL2CPP: il2cpp_type_get_type unavailable", -1);
    ATTACHAPI(-1);
    return t ? g_api->il2cpp_type_get_type((const Il2CppType *)t) : -1;
}
uint32_t Api_TypeAttrs(const void *t) {
    REQAPI(il2cpp_type_get_attrs, "IL2CPP: il2cpp_type_get_attrs unavailable", 0);
    ATTACHAPI(0);
    return t ? g_api->il2cpp_type_get_attrs((const Il2CppType *)t) : 0;
}
const void *Api_TypeClass(const void *t) {
    if (!g_api || (!g_api->il2cpp_type_get_class_or_element_class && !g_api->il2cpp_class_from_type)) {
        SetError("IL2CPP: il2cpp_type_get_class_or_element_class and "
                 "il2cpp_class_from_type unavailable");
        return nullptr;
    }
    if (!t) {
        SetError("IL2CPP: type_get_class type is required");
        return nullptr;
    }
    ATTACHAPI(nullptr);
    if (g_api->il2cpp_type_get_class_or_element_class)
        return g_api->il2cpp_type_get_class_or_element_class((const Il2CppType *)t);
    return g_api->il2cpp_class_from_type((const Il2CppType *)t);
}
const void *Api_ObjectClass(void *o) {
    REQAPI(il2cpp_object_get_class, "IL2CPP: il2cpp_object_get_class unavailable", nullptr);
    ATTACHAPI(nullptr);
    if (!o) {
        SetError("IL2CPP: object_get_class object is required");
        return nullptr;
    }
    return g_api->il2cpp_object_get_class((Il2CppObject *)o);
}
void *Api_ObjectUnbox(void *o) {
    REQAPI(il2cpp_object_unbox, "IL2CPP: il2cpp_object_unbox unavailable", nullptr);
    ATTACHAPI(nullptr);
    if (!o) {
        SetError("IL2CPP: object_unbox object is required");
        return nullptr;
    }
    return g_api->il2cpp_object_unbox((Il2CppObject *)o);
}
void *Api_StringNew(const char *u) {
    g_lastError.clear();
    REQAPI(il2cpp_string_new, "IL2CPP: il2cpp_string_new unavailable", nullptr);
    ATTACHAPI(nullptr);
    if (!u) {
        SetError("IL2CPP: string_new utf8 input is required");
        return nullptr;
    }
    size_t utf8Length = 0;
    DWORD exceptionCode = 0;
    if (!SehBoundedStringLength(u, kMaxManagedStringUtf8Bytes + 1, &utf8Length, &exceptionCode)) {
        SetError("IL2CPP: string_new input points to unreadable memory");
        return nullptr;
    }
    if (utf8Length == kMaxManagedStringUtf8Bytes + 1) {
        SetError("IL2CPP: string_new input exceeds the 4 MiB safety limit");
        return nullptr;
    }
    Il2CppString *result = nullptr;
    if (!SehInvokeValue(g_api->il2cpp_string_new, &result, &exceptionCode, u)) {
        char message[256]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: il2cpp_string_new raised native exception 0x%08lX utf8_bytes=%zu",
                      static_cast<unsigned long>(exceptionCode), utf8Length);
        SetError(message);
        Log("[IL2CPP][ERROR] %s", message);
        return nullptr;
    }
    if (!result) {
        SetError("IL2CPP: il2cpp_string_new returned null");
        Log("[IL2CPP][ERROR] il2cpp_string_new returned null for %zu UTF-8 bytes.", utf8Length);
    }
    return result;
}
void *Api_StringNewLen(const char *u, uint32_t length) {
    g_lastError.clear();
    REQAPI(il2cpp_string_new_len, "IL2CPP: il2cpp_string_new_len unavailable", nullptr);
    ATTACHAPI(nullptr);
    if (!u) {
        SetError("IL2CPP: string_new_len UTF-8 input is required");
        return nullptr;
    }
    Il2CppString *result = nullptr;
    DWORD exceptionCode = 0;
    if (!SehInvokeValue(g_api->il2cpp_string_new_len, &result, &exceptionCode, u, length)) {
        char message[256]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: il2cpp_string_new_len raised native exception 0x%08lX utf8_bytes=%u",
                      static_cast<unsigned long>(exceptionCode), length);
        SetError(message);
        Log("[IL2CPP][ERROR] %s", message);
        return nullptr;
    }
    if (!result) {
        SetError("IL2CPP: il2cpp_string_new_len returned null");
        Log("[IL2CPP][ERROR] il2cpp_string_new_len returned null for %u UTF-8 bytes.", length);
    }
    return result;
}
int32_t Api_StringLengthSafe(void *str) {
    REQAPI(il2cpp_string_length, "IL2CPP: il2cpp_string_length unavailable", -1);
    ATTACHAPI(-1);
    if (!str) {
        SetError("IL2CPP: string_length string is required");
        return -1;
    }
    int32_t length = -1;
    DWORD exceptionCode = 0;
    if (!SehInvokeValue(g_api->il2cpp_string_length, &length, &exceptionCode, static_cast<Il2CppString *>(str))) {
        SetError("IL2CPP: string_length blocked an invalid managed string pointer");
        return -1;
    }
    if (length < 0 || length > kMaxManagedStringUtf16Units) {
        SetError("IL2CPP: string_length rejected an invalid or oversized managed string");
        return -1;
    }
    return length;
}
int Api_StringUtf8(void *str, char *out, size_t n) {
    if (out && n)
        out[0] = 0;
    if (!out || n == 0) {
        SetError("IL2CPP: string_to_utf8 output buffer is required");
        return 0;
    }
    if (!str) {
        SetError("IL2CPP: string_to_utf8 string is required");
        return 0;
    }
    if (!g_api || !g_api->il2cpp_string_chars || !g_api->il2cpp_string_length) {
        SetError("IL2CPP: il2cpp_string_chars or il2cpp_string_length unavailable");
        return 0;
    }
    ATTACHAPI(0);
    const int32_t len = Api_StringLengthSafe(str);
    if (len < 0)
        return 0;
    if (len == 0)
        return 1;
    const uint16_t *chars = nullptr;
    DWORD exceptionCode = 0;
    if (!SehInvokeValue(g_api->il2cpp_string_chars, &chars, &exceptionCode, static_cast<Il2CppString *>(str))) {
        SetError("IL2CPP: string_to_utf8 blocked an invalid managed string pointer");
        return 0;
    }
    if (!chars)
        return 0;
    if (n > static_cast<size_t>(INT_MAX))
        n = static_cast<size_t>(INT_MAX);
    const int written = WideCharToMultiByte(CP_UTF8, 0, reinterpret_cast<const wchar_t *>(chars), len, out,
                                            static_cast<int>(n - 1), nullptr, nullptr);
    if (written <= 0)
        return 0;
    out[written] = 0;
    return 1;
}
bool TryGetArrayLength(void *array, size_t *length, const char *operation) {
    if (length)
        *length = 0;
    if (!array) {
        SetError(std::string("IL2CPP: ") + operation + " array is required");
        return false;
    }
    if (!g_api || !g_api->il2cpp_offset_of_array_length_in_array_object_header) {
        SetError(std::string("IL2CPP: ") + operation +
                 " unavailable: il2cpp_offset_of_array_length_in_array_object_header export is required");
        return false;
    }

    // Use the runtime's explicitly exported array-layout query instead of a
    // folded accessor or a value inferred from the element type. In particular
    // System.Type[] is used by reflection's MakeGenericMethod and has exposed
    // differing results from the inferred path in this Unity build.
    const uint32_t lengthOffset = g_api->il2cpp_offset_of_array_length_in_array_object_header();
    const uint32_t headerSize = g_api->il2cpp_array_object_header_size
        ? g_api->il2cpp_array_object_header_size()
        : 0;
    if (lengthOffset == 0 || lengthOffset % alignof(uintptr_t) != 0 ||
        (headerSize != 0 && lengthOffset + sizeof(uintptr_t) > headerSize)) {
        SetError(std::string("IL2CPP: ") + operation + " failed: runtime reported an invalid array-length offset");
        return false;
    }

    uintptr_t rawLength = 0;
    DWORD exceptionCode = 0;
    if (!SehCopyMemory(static_cast<const char *>(array) + lengthOffset, &rawLength, sizeof(rawLength),
                       &exceptionCode)) {
        SetError(std::string("IL2CPP: ") + operation + " blocked an invalid array pointer");
        return false;
    }
    if (length)
        *length = static_cast<size_t>(rawLength);
    return true;
}

size_t Api_ArrayLength(void *a) {
    ATTACHAPI(0);
    size_t length = 0;
    return TryGetArrayLength(a, &length, "array_length") ? length : 0;
}

void *ArrayElementAddress(void *array, int elementSize, size_t index, const char *operation) {
    if (!array) {
        SetError(std::string("IL2CPP: ") + operation + " array is required");
        return nullptr;
    }
    if (elementSize <= 0) {
        SetError(std::string("IL2CPP: ") + operation + " element size must be positive");
        return nullptr;
    }
    size_t length = 0;
    if (!TryGetArrayLength(array, &length, operation))
        return nullptr;
    if (index >= length) {
        SetError(std::string("IL2CPP: ") + operation + " index out of range");
        return nullptr;
    }

    if (g_api->il2cpp_array_addr_with_size) {
        return g_api->il2cpp_array_addr_with_size((Il2CppArray *)array, elementSize, index);
    }

    if (!g_api->il2cpp_array_object_header_size) {
        SetError(std::string("IL2CPP: ") + operation +
                 " unavailable: il2cpp_array_addr_with_size and "
                 "il2cpp_array_object_header_size exports are missing");
        return nullptr;
    }

    const uint32_t headerSize = g_api->il2cpp_array_object_header_size();
    if (headerSize == 0) {
        SetError(std::string("IL2CPP: ") + operation + " failed: il2cpp_array_object_header_size returned 0");
        return nullptr;
    }

    const auto elementBytes = static_cast<size_t>(elementSize);
    if (index > std::numeric_limits<size_t>::max() / elementBytes) {
        SetError(std::string("IL2CPP: ") + operation + " failed: array element offset overflow");
        return nullptr;
    }
    const size_t elementOffset = index * elementBytes;
    if (elementOffset > std::numeric_limits<size_t>::max() - static_cast<size_t>(headerSize)) {
        SetError(std::string("IL2CPP: ") + operation + " failed: array data address overflow");
        return nullptr;
    }

    if (g_api->il2cpp_array_get_byte_length) {
        const size_t byteLength = static_cast<size_t>(g_api->il2cpp_array_get_byte_length((Il2CppArray *)array));
        if (elementOffset > byteLength || elementBytes > byteLength - elementOffset) {
            SetError(std::string("IL2CPP: ") + operation + " failed: element range exceeds array byte length");
            return nullptr;
        }
    }

    return static_cast<char *>(array) + headerSize + elementOffset;
}

bool ValidateReferenceArray(void *array, const char *operation, Il2CppClass **arrayClass) {
    if (arrayClass)
        *arrayClass = nullptr;
    if (!g_api || !g_api->il2cpp_object_get_class || !g_api->il2cpp_class_get_element_class ||
        !g_api->il2cpp_class_is_valuetype) {
        SetError(std::string("IL2CPP: ") + operation +
                 " unavailable: il2cpp_object_get_class, il2cpp_class_get_element_class, and "
                 "il2cpp_class_is_valuetype exports are required to validate a reference array");
        return false;
    }

    auto *klass = g_api->il2cpp_object_get_class(static_cast<Il2CppObject *>(array));
    if (!klass) {
        SetError(std::string("IL2CPP: ") + operation + " failed: array class metadata is unavailable");
        return false;
    }
    auto *elementClass = g_api->il2cpp_class_get_element_class(klass);
    if (!elementClass) {
        SetError(std::string("IL2CPP: ") + operation +
                 " failed: object class has no array element-class metadata");
        return false;
    }
    if (g_api->il2cpp_class_is_valuetype(elementClass)) {
        SetError(std::string("IL2CPP: ") + operation +
                 " refused: value-type arrays cannot be accessed through reference-array helpers");
        return false;
    }
    if (arrayClass)
        *arrayClass = klass;
    return true;
}

void *Api_ArrayAddr(void *a, int e, size_t i) {
    ATTACHAPI(nullptr);
    return ArrayElementAddress(a, e, i, "array_addr_with_size");
}
void *Api_ArrayRefAt(void *a, size_t i) {
    if (!a) {
        SetError("IL2CPP: Unity array element access failed: array is null");
        return nullptr;
    }
    ATTACHAPI(nullptr);
    Il2CppClass *arrayClass = nullptr;
    if (!ValidateReferenceArray(a, "Unity array element access", &arrayClass))
        return nullptr;
    size_t length = 0;
    if (!TryGetArrayLength(a, &length, "Unity array element access"))
        return nullptr;
    if (i >= length) {
        SetError("IL2CPP: Unity array element access failed: index out of range");
        return nullptr;
    }

    int elementSize = static_cast<int>(sizeof(void *));
    if (g_api->il2cpp_array_element_size) {
        const int runtimeElementSize = g_api->il2cpp_array_element_size(arrayClass);
        if (runtimeElementSize > 0)
            elementSize = runtimeElementSize;
    }

    void **slot = reinterpret_cast<void **>(ArrayElementAddress(a, elementSize, i, "Unity array element access"));
    if (!slot) {
        if (g_lastError.empty()) {
            SetError("IL2CPP: Unity array element access failed: element address is null");
        }
        return nullptr;
    }
    return *slot;
}
int Api_ArraySetRef(void *a, size_t i, void *value) {
    REQAPI(il2cpp_gc_wbarrier_set_field,
           "IL2CPP: Unity array reference write unavailable: "
           "il2cpp_gc_wbarrier_set_field export is missing",
           0);
    if (!a) {
        SetError("IL2CPP: Unity array reference write failed: array is null");
        return 0;
    }
    ATTACHAPI(0);
    if (!ValidateReferenceArray(a, "Unity array reference write", nullptr))
        return 0;
    size_t length = 0;
    if (!TryGetArrayLength(a, &length, "Unity array reference write"))
        return 0;
    if (i >= length) {
        SetError("IL2CPP: Unity array reference write failed: index out of range");
        return 0;
    }
    void **slot = reinterpret_cast<void **>(
        ArrayElementAddress(a, static_cast<int>(sizeof(void *)), i, "Unity array reference write"));
    if (!slot) {
        if (g_lastError.empty()) {
            SetError("IL2CPP: Unity array reference write failed: element address "
                     "is null");
        }
        return 0;
    }
    g_api->il2cpp_gc_wbarrier_set_field((Il2CppObject *)a, slot, (Il2CppObject *)value);
    return 1;
}
int Api_FieldGet(void *o, const void *f, void *out) {
    g_lastError.clear();
    REQAPI(il2cpp_field_get_value, "IL2CPP: il2cpp_field_get_value unavailable", 0);
    ATTACHAPI(0);
    if (!o || !f || !out) {
        SetError("IL2CPP: field_get_value requires object, field, and output");
        return 0;
    }
    return SehFieldGet(g_api->il2cpp_field_get_value, (Il2CppObject *)o, (Il2CppClassField *)const_cast<void *>(f),
                       out);
}
int Api_FieldSet(void *o, const void *f, void *v) {
    g_lastError.clear();
    REQAPI(il2cpp_field_set_value, "IL2CPP: il2cpp_field_set_value unavailable", 0);
    ATTACHAPI(0);
    if (!o || !f) {
        SetError("IL2CPP: field_set_value requires object and field");
        return 0;
    }
    return SehFieldSet(g_api->il2cpp_field_set_value, (Il2CppObject *)o, (Il2CppClassField *)const_cast<void *>(f), v);
}
int Api_RuntimeInvoke(const void *m, void *o, void **p, void **r, void **e) {
    g_lastError.clear();
    if (r)
        *r = nullptr;
    if (e)
        *e = nullptr;
    REQAPI(il2cpp_runtime_invoke, "IL2CPP: il2cpp_runtime_invoke unavailable", 0);
    if (!m) {
        SetError("IL2CPP: runtime_invoke method is required");
        return 0;
    }
    ATTACHAPI(0);
    Il2CppObject *ex = nullptr;
    auto *res = g_api->il2cpp_runtime_invoke((const Il2CppMethod *)m, o, p, &ex);
    if (r)
        *r = res;
    if (e)
        *e = ex;
    if (ex) {
        if (g_api->il2cpp_format_exception) {
            char message[1024]{};
            g_api->il2cpp_format_exception(ex, message, static_cast<int>(sizeof(message)));
            SetError(std::string("IL2CPP: runtime_invoke threw exception: ") + message);
        } else {
            SetError("IL2CPP: runtime_invoke threw exception; "
                     "il2cpp_format_exception unavailable");
        }
        return 0;
    }
    return 1;
}
const void *Api_ThreadCurrent() {
    if (!g_api || !g_api->il2cpp_thread_current) {
        SetError("IL2CPP: il2cpp_thread_current unavailable");
        return nullptr;
    }
    return g_api->il2cpp_thread_current();
}
const void *Api_ThreadAttach(const void *d) {
    if (!g_api || !g_api->il2cpp_thread_attach) {
        SetError("IL2CPP: il2cpp_thread_attach unavailable");
        return nullptr;
    }
    auto *domain = (Il2CppDomain *)const_cast<void *>(d ? d : g_api->cachedDomain);
    if (!domain) {
        SetError("IL2CPP: thread_attach domain is unavailable");
        return nullptr;
    }
    return g_api->il2cpp_thread_attach(domain);
}
void Api_ThreadDetach(const void *t) {
    if (g_api && g_api->il2cpp_thread_detach && t)
        g_api->il2cpp_thread_detach((Il2CppThread *)const_cast<void *>(t));
}
void *Api_Alloc(size_t size) {
    REQAPI(il2cpp_alloc, "IL2CPP: il2cpp_alloc unavailable", nullptr);
    return size ? g_api->il2cpp_alloc(size) : nullptr;
}
void Api_Free(void *ptr) {
    if (g_api && g_api->il2cpp_free && ptr)
        g_api->il2cpp_free(ptr);
}

int Api_AttachManagedMethodHook(const URK_Il2CppManagedMethodDesc *desc, void **original, void *detour,
                                const URK_HookOptions *options, URK_Il2CppManagedHookResult *result) {
    FillManagedHookResult(result, nullptr, nullptr, nullptr);
    if (!desc || desc->size < sizeof(URK_Il2CppManagedMethodDesc)) {
        SetError("IL2CPP: managed-method hook requires a complete "
                 "URK_Il2CppManagedMethodDesc");
        FillManagedHookResult(result, nullptr, nullptr, Api_LastError());
        return 0;
    }
    if (!original || !detour) {
        SetError("IL2CPP: managed-method hook requires an original slot and detour");
        FillManagedHookResult(result, nullptr, nullptr, Api_LastError());
        return 0;
    }
    if (Empty(desc->image_name) || Empty(desc->class_name) || Empty(desc->method_name) || desc->parameter_count < 0 ||
        (desc->parameter_count > 0 && !desc->parameter_type_names)) {
        SetError("IL2CPP: managed-method hook requires image, class, method, and "
                 "valid parameter type list");
        FillManagedHookResult(result, nullptr, nullptr, Api_LastError());
        return 0;
    }
    for (int i = 0; i < desc->parameter_count; ++i) {
        if (Empty(desc->parameter_type_names[i])) {
            SetError("IL2CPP: managed-method hook parameter type names must be non-empty");
            FillManagedHookResult(result, nullptr, nullptr, Api_LastError());
            return 0;
        }
    }

    const std::string identity = ManagedMethodIdentity(*desc);
    const void *klass = Api_FindClass(desc->image_name, desc->namespc ? desc->namespc : "", desc->class_name);
    if (!klass) {
        SetError(std::string("IL2CPP: managed-method hook class lookup failed for ") + identity + ": " +
                 Api_LastError());
        FillManagedHookResult(result, nullptr, nullptr, Api_LastError());
        return 0;
    }

    const void *method =
        Api_FindMethodExact(klass, desc->method_name, desc->parameter_type_names, desc->parameter_count);
    if (!method) {
        SetError(std::string("IL2CPP: managed-method hook method lookup failed for ") + identity + ": " +
                 Api_LastError());
        FillManagedHookResult(result, nullptr, nullptr, Api_LastError());
        return 0;
    }

    void *nativeTarget = Api_MethodPointer(method);
    if (!nativeTarget) {
        SetError(std::string("IL2CPP: managed-method hook could not resolve MethodInfo::methodPointer for ") +
                 identity + ": " + Api_LastError());
        FillManagedHookResult(result, method, nullptr, Api_LastError());
        return 0;
    }

    *original = nativeTarget;
    if (!HookManager_Attach(original, detour, options)) {
        const uint32_t backend = options && options->size >= sizeof(URK_HookOptions)
                                     ? options->backend
                                     : static_cast<uint32_t>(URK_HOOK_BACKEND_AUTO);
        SetError(std::string("IL2CPP: managed-method hook attach failed for ") + identity +
                 ": target=" + PtrString(nativeTarget) + " detour=" + PtrString(detour) +
                 " backend=" + std::to_string(backend) + "; see [hooks] diagnostics for the Detours error");
        FillManagedHookResult(result, method, nativeTarget, Api_LastError());
        return 0;
    }

    g_lastError.clear();
    FillManagedHookResult(result, method, nativeTarget, nullptr);
    return 1;
}
#undef ATTACHAPI
#undef REQAPI

const URK_Il2CppApi g_publicApi = [] {
    URK_Il2CppApi api{};
    api.version = URK_IL2CPP_API_VERSION;
    api.size = sizeof(URK_Il2CppApi);
    api.is_available = &Api_IsAvailable;
    api.domain_get = &Api_DomainGet;
    api.find_image = &Api_FindImage;
    api.find_class = &Api_FindClass;
    api.find_method = &Api_FindMethod;
    api.find_method_exact = &Api_FindMethodExact;
    api.method_pointer = &Api_MethodPointer;
    api.find_field = &Api_FindField;
    api.field_offset = &Api_FieldOffset;
    api.last_error = &Api_LastError;
    api.domain_get_assembly_count = &Api_DomainAssemblyCount;
    api.domain_get_assembly = &Api_DomainAssembly;
    api.assembly_get_image = &Api_AssemblyImage;
    api.image_get_name = &Api_ImageName;
    api.image_get_class_count = &Api_ImageClassCount;
    api.image_get_class = &Api_ImageClass;
    api.class_get_name = &Api_ClassName;
    api.class_get_namespace = &Api_ClassNamespace;
    api.class_get_parent = &Api_ClassParent;
    api.class_get_flags = &Api_ClassFlags;
    api.class_is_valuetype = &Api_ClassValueType;
    api.class_is_enum = &Api_ClassEnum;
    api.class_get_fields = &Api_ClassFields;
    api.class_get_methods = &Api_ClassMethods;
    api.class_get_properties = &Api_ClassProperties;
    api.class_get_nested_types = &Api_ClassNested;
    api.class_get_interfaces = &Api_ClassInterfaces;
    api.method_get_name = &Api_MethodName;
    api.method_get_declaring_class = &Api_MethodClass;
    api.method_get_param_count = &Api_MethodParamCount;
    api.method_get_param = &Api_MethodParam;
    api.method_get_return_type = &Api_MethodReturn;
    api.method_get_flags = &Api_MethodFlags;
    api.method_get_token = &Api_MethodToken;
    api.field_get_name = &Api_FieldName;
    api.field_get_type = &Api_FieldType;
    api.field_get_flags = &Api_FieldFlags;
    api.field_static_get_value = &Api_FieldStaticGet;
    api.field_static_set_value = &Api_FieldStaticSet;
    api.property_get_name = &Api_PropertyName;
    api.property_get_get_method = &Api_PropertyGet;
    api.property_get_set_method = &Api_PropertySet;
    api.property_get_flags = &Api_PropertyFlags;
    api.type_get_name = &Api_TypeName;
    api.type_get_type = &Api_TypeKind;
    api.type_get_attrs = &Api_TypeAttrs;
    api.type_get_class_or_element_class = &Api_TypeClass;
    api.object_get_class = &Api_ObjectClass;
    api.object_unbox = &Api_ObjectUnbox;
    api.string_new = &Api_StringNew;
    api.string_to_utf8 = &Api_StringUtf8;
    api.array_length = &Api_ArrayLength;
    api.array_addr_with_size = &Api_ArrayAddr;
    api.array_ref_at = &Api_ArrayRefAt;
    api.field_get_value = &Api_FieldGet;
    api.field_set_value = &Api_FieldSet;
    api.runtime_invoke = &Api_RuntimeInvoke;
    api.thread_current = &Api_ThreadCurrent;
    api.thread_attach = &Api_ThreadAttach;
    api.thread_detach = &Api_ThreadDetach;
    api.alloc = &Api_Alloc;
    api.free = &Api_Free;
    // Extended IL2CPP public surface. These are direct, optional wrappers over
    // the internal GameAssembly export table; unavailable exports fail closed.
    api.init = [](const char *domain_name) {
        if (g_api && g_api->il2cpp_init)
            g_api->il2cpp_init(domain_name);
    };
    api.shutdown = []() {
        if (g_api && g_api->il2cpp_shutdown)
            g_api->il2cpp_shutdown();
    };
    api.set_config_dir = [](const char *config_path) {
        if (g_api && g_api->il2cpp_set_config_dir)
            g_api->il2cpp_set_config_dir(config_path);
    };
    api.set_data_dir = [](const char *data_path) {
        if (g_api && g_api->il2cpp_set_data_dir)
            g_api->il2cpp_set_data_dir(data_path);
    };
    api.set_commandline_arguments = [](int argc, const char *argv[], const char *basedir) {
        if (g_api && g_api->il2cpp_set_commandline_arguments)
            g_api->il2cpp_set_commandline_arguments(argc, argv, basedir);
    };
    api.set_memory_callbacks = [](void *callbacks) {
        if (g_api && g_api->il2cpp_set_memory_callbacks)
            g_api->il2cpp_set_memory_callbacks(reinterpret_cast<Il2CppMemoryCallbacks *>(callbacks));
    };
    api.set_find_plugin_callback = [](void *method) {
        if (g_api && g_api->il2cpp_set_find_plugin_callback)
            g_api->il2cpp_set_find_plugin_callback(reinterpret_cast<Il2CppSetFindPlugInCallback>(method));
    };
    api.add_internal_call = [](const char *name, void *method) {
        if (g_api && g_api->il2cpp_add_internal_call)
            g_api->il2cpp_add_internal_call(name, reinterpret_cast<Il2CppMethodPointer>(method));
    };
    api.resolve_icall = [](const char *name) -> void * {
        return g_api && g_api->il2cpp_resolve_icall ? g_api->il2cpp_resolve_icall(name) : nullptr;
    };
    api.domain_assembly_open = [](const void *domain, const char *name) -> const void * {
        return g_api && g_api->il2cpp_domain_assembly_open
                   ? g_api->il2cpp_domain_assembly_open(
                         (Il2CppDomain *)const_cast<void *>(domain ? domain
                                                                   : static_cast<const void *>(g_api->cachedDomain)),
                         name)
                   : nullptr;
    };
    api.get_corlib = []() -> const void * {
        return g_api && g_api->il2cpp_get_corlib ? g_api->il2cpp_get_corlib() : nullptr;
    };
    api.image_get_assembly = [](const void *image) -> const void * {
        return g_api && g_api->il2cpp_image_get_assembly && image
                   ? g_api->il2cpp_image_get_assembly((const Il2CppImage *)image)
                   : nullptr;
    };
    api.image_get_filename = [](const void *image) -> const char * {
        return g_api && g_api->il2cpp_image_get_filename && image
                   ? g_api->il2cpp_image_get_filename((const Il2CppImage *)image)
                   : nullptr;
    };
    api.image_get_entry_point = [](const void *image) -> const void * {
        return g_api && g_api->il2cpp_image_get_entry_point && image
                   ? g_api->il2cpp_image_get_entry_point((const Il2CppImage *)image)
                   : nullptr;
    };

    api.class_from_type = [](const void *type) -> const void * {
        return g_api && g_api->il2cpp_class_from_type && type ? g_api->il2cpp_class_from_type((const Il2CppType *)type)
                                                              : nullptr;
    };
    api.class_from_il2cpp_type = [](const void *type) -> const void * {
        return g_api && g_api->il2cpp_class_from_il2cpp_type && type
                   ? g_api->il2cpp_class_from_il2cpp_type((const Il2CppType *)type)
                   : nullptr;
    };
    api.class_from_system_type = [](void *reflection_type) -> const void * {
        return g_api && g_api->il2cpp_class_from_system_type && reflection_type
                   ? g_api->il2cpp_class_from_system_type((Il2CppReflectionType *)reflection_type)
                   : nullptr;
    };
    api.class_get_assemblyname = [](const void *klass) -> const char * {
        return g_api && g_api->il2cpp_class_get_assemblyname && klass
                   ? g_api->il2cpp_class_get_assemblyname((Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.class_get_image = [](const void *klass) -> const void * {
        return g_api && g_api->il2cpp_class_get_image && klass
                   ? g_api->il2cpp_class_get_image((Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.class_get_declaring_type = [](const void *klass) -> const void * {
        return g_api && g_api->il2cpp_class_get_declaring_type && klass
                   ? g_api->il2cpp_class_get_declaring_type((Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.class_get_element_class = [](const void *klass) -> const void * {
        return g_api && g_api->il2cpp_class_get_element_class && klass
                   ? g_api->il2cpp_class_get_element_class((Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.class_get_type = [](const void *klass) -> const void * {
        return g_api && g_api->il2cpp_class_get_type && klass
                   ? g_api->il2cpp_class_get_type((Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.class_get_type_token = [](const void *klass) -> uint32_t {
        return g_api && g_api->il2cpp_class_get_type_token && klass
                   ? g_api->il2cpp_class_get_type_token((Il2CppClass *)const_cast<void *>(klass))
                   : 0;
    };
    api.class_get_rank = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_get_rank && klass ? g_api->il2cpp_class_get_rank((const Il2CppClass *)klass)
                                                              : 0;
    };
    api.class_instance_size = [](const void *klass) -> int32_t {
        return g_api && g_api->il2cpp_class_instance_size && klass
                   ? g_api->il2cpp_class_instance_size((Il2CppClass *)const_cast<void *>(klass))
                   : 0;
    };
    api.class_value_size = [](const void *klass, uint32_t *align) -> int32_t {
        return g_api && g_api->il2cpp_class_value_size && klass
                   ? g_api->il2cpp_class_value_size((Il2CppClass *)const_cast<void *>(klass), align)
                   : 0;
    };
    api.class_num_fields = [](const void *klass) -> size_t {
        return g_api && g_api->il2cpp_class_num_fields && klass
                   ? g_api->il2cpp_class_num_fields((const Il2CppClass *)klass)
                   : 0;
    };
    api.class_array_element_size = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_array_element_size && klass
                   ? g_api->il2cpp_class_array_element_size((const Il2CppClass *)klass)
                   : 0;
    };
    api.class_is_generic = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_is_generic && klass &&
                       g_api->il2cpp_class_is_generic((const Il2CppClass *)klass)
                   ? 1
                   : 0;
    };
    api.class_is_inflated = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_is_inflated && klass &&
                       g_api->il2cpp_class_is_inflated((const Il2CppClass *)klass)
                   ? 1
                   : 0;
    };
    api.class_is_abstract = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_is_abstract && klass &&
                       g_api->il2cpp_class_is_abstract((const Il2CppClass *)klass)
                   ? 1
                   : 0;
    };
    api.class_is_interface = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_is_interface && klass &&
                       g_api->il2cpp_class_is_interface((const Il2CppClass *)klass)
                   ? 1
                   : 0;
    };
    api.class_is_subclass_of = [](const void *klass, const void *klassc, int check_interfaces) -> int {
        return g_api && g_api->il2cpp_class_is_subclass_of && klass && klassc &&
                       g_api->il2cpp_class_is_subclass_of((Il2CppClass *)const_cast<void *>(klass),
                                                          (Il2CppClass *)const_cast<void *>(klassc),
                                                          check_interfaces != 0)
                   ? 1
                   : 0;
    };
    api.class_is_assignable_from = [](const void *klass, const void *oklass) -> int {
        return g_api && g_api->il2cpp_class_is_assignable_from && klass && oklass &&
                       g_api->il2cpp_class_is_assignable_from((Il2CppClass *)const_cast<void *>(klass),
                                                              (Il2CppClass *)const_cast<void *>(oklass))
                   ? 1
                   : 0;
    };
    api.class_has_parent = [](const void *klass, const void *klassc) -> int {
        return g_api && g_api->il2cpp_class_has_parent && klass && klassc &&
                       g_api->il2cpp_class_has_parent((Il2CppClass *)const_cast<void *>(klass),
                                                      (Il2CppClass *)const_cast<void *>(klassc))
                   ? 1
                   : 0;
    };
    api.class_has_attribute = [](const void *klass, const void *attr_class) -> int {
        return g_api && g_api->il2cpp_class_has_attribute && klass && attr_class &&
                       g_api->il2cpp_class_has_attribute((Il2CppClass *)const_cast<void *>(klass),
                                                         (Il2CppClass *)const_cast<void *>(attr_class))
                   ? 1
                   : 0;
    };
    api.class_has_references = [](const void *klass) -> int {
        return g_api && g_api->il2cpp_class_has_references && klass &&
                       g_api->il2cpp_class_has_references((Il2CppClass *)const_cast<void *>(klass))
                   ? 1
                   : 0;
    };
    api.class_enum_basetype = [](const void *klass) -> const void * {
        return g_api && g_api->il2cpp_class_enum_basetype && klass
                   ? g_api->il2cpp_class_enum_basetype((Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.class_get_property_from_name = [](const void *klass, const char *name) -> const void * {
        return g_api && g_api->il2cpp_class_get_property_from_name && klass && name
                   ? g_api->il2cpp_class_get_property_from_name((Il2CppClass *)const_cast<void *>(klass), name)
                   : nullptr;
    };
    api.class_get_events = [](const void *klass, void **iterator) -> const void * {
        return g_api && g_api->il2cpp_class_get_events && klass && iterator
                   ? g_api->il2cpp_class_get_events((Il2CppClass *)const_cast<void *>(klass), iterator)
                   : nullptr;
    };
    api.class_get_bitmap_size = [](const void *klass) -> size_t {
        return g_api && g_api->il2cpp_class_get_bitmap_size && klass
                   ? g_api->il2cpp_class_get_bitmap_size((const Il2CppClass *)klass)
                   : 0;
    };
    api.class_get_bitmap = [](const void *klass, size_t *bitmap) {
        if (g_api && g_api->il2cpp_class_get_bitmap && klass && bitmap)
            g_api->il2cpp_class_get_bitmap((Il2CppClass *)const_cast<void *>(klass), bitmap);
    };

    api.method_get_declaring_type = [](const void *method) -> const void * {
        return g_api && g_api->il2cpp_method_get_declaring_type && method
                   ? g_api->il2cpp_method_get_declaring_type((const Il2CppMethod *)method)
                   : nullptr;
    };
    api.method_get_object = [](const void *method, const void *refclass) -> void * {
        return g_api && g_api->il2cpp_method_get_object && method
                   ? g_api->il2cpp_method_get_object((const Il2CppMethod *)method,
                                                     (Il2CppClass *)const_cast<void *>(refclass))
                   : nullptr;
    };
    api.method_is_generic = [](const void *method) -> int {
        return g_api && g_api->il2cpp_method_is_generic && method &&
                       g_api->il2cpp_method_is_generic((const Il2CppMethod *)method)
                   ? 1
                   : 0;
    };
    api.method_is_inflated = [](const void *method) -> int {
        return g_api && g_api->il2cpp_method_is_inflated && method &&
                       g_api->il2cpp_method_is_inflated((const Il2CppMethod *)method)
                   ? 1
                   : 0;
    };
    api.method_is_instance = [](const void *method) -> int {
        return g_api && g_api->il2cpp_method_is_instance && method &&
                       g_api->il2cpp_method_is_instance((const Il2CppMethod *)method)
                   ? 1
                   : 0;
    };
    api.method_has_attribute = [](const void *method, const void *attr_class) -> int {
        return g_api && g_api->il2cpp_method_has_attribute && method && attr_class &&
                       g_api->il2cpp_method_has_attribute((const Il2CppMethod *)method,
                                                          (Il2CppClass *)const_cast<void *>(attr_class))
                   ? 1
                   : 0;
    };
    api.method_get_param_name = [](const void *method, uint32_t index) -> const char * {
        return g_api && g_api->il2cpp_method_get_param_name && method
                   ? g_api->il2cpp_method_get_param_name((const Il2CppMethod *)method, index)
                   : nullptr;
    };

    api.field_get_parent = [](const void *field) -> const void * {
        return g_api && g_api->il2cpp_field_get_parent && field
                   ? g_api->il2cpp_field_get_parent((Il2CppClassField *)const_cast<void *>(field))
                   : nullptr;
    };
    api.field_get_value_object = [](const void *field, void *object) -> void * {
        return g_api && g_api->il2cpp_field_get_value_object && field
                   ? g_api->il2cpp_field_get_value_object((Il2CppClassField *)const_cast<void *>(field),
                                                          (Il2CppObject *)object)
                   : nullptr;
    };
    api.field_has_attribute = [](const void *field, const void *attr_class) -> int {
        return g_api && g_api->il2cpp_field_has_attribute && field && attr_class &&
                       g_api->il2cpp_field_has_attribute((Il2CppClassField *)const_cast<void *>(field),
                                                         (Il2CppClass *)const_cast<void *>(attr_class))
                   ? 1
                   : 0;
    };
    api.property_get_parent = [](const void *property) -> const void * {
        return g_api && g_api->il2cpp_property_get_parent && property
                   ? g_api->il2cpp_property_get_parent((Il2CppProperty *)const_cast<void *>(property))
                   : nullptr;
    };
    api.type_get_object = [](const void *type) -> void * {
        return g_api && g_api->il2cpp_type_get_object && type ? g_api->il2cpp_type_get_object((const Il2CppType *)type)
                                                              : nullptr;
    };

    api.object_get_size = [](void *object) -> uint32_t {
        return g_api && g_api->il2cpp_object_get_size && object ? g_api->il2cpp_object_get_size((Il2CppObject *)object)
                                                                : 0;
    };
    api.object_get_virtual_method = [](void *object, const void *method) -> const void * {
        return g_api && g_api->il2cpp_object_get_virtual_method && object && method
                   ? g_api->il2cpp_object_get_virtual_method((Il2CppObject *)object, (const Il2CppMethod *)method)
                   : nullptr;
    };
    api.object_new = [](const void *klass) -> void * {
        return g_api && g_api->il2cpp_object_new && klass ? g_api->il2cpp_object_new((const Il2CppClass *)klass)
                                                          : nullptr;
    };
    api.object_is_inst = [](void *object, const void *klass) -> void * {
        return g_api && g_api->il2cpp_object_is_inst && object && klass
                   ? g_api->il2cpp_object_is_inst((Il2CppObject *)object, (Il2CppClass *)const_cast<void *>(klass))
                   : nullptr;
    };
    api.value_box = [](const void *klass, void *data) -> void * {
        return g_api && g_api->il2cpp_value_box && klass && data
                   ? g_api->il2cpp_value_box((Il2CppClass *)const_cast<void *>(klass), data)
                   : nullptr;
    };

    api.string_new_len = &Api_StringNewLen;
    api.string_new_utf16 = [](const uint16_t *text, int32_t length) -> void * {
        if (!text || length < 0 || length > kMaxManagedStringUtf16Units) {
            SetError("IL2CPP: string_new_utf16 rejected null input or an oversized string");
            return nullptr;
        }
        return g_api && g_api->il2cpp_string_new_utf16
                   ? g_api->il2cpp_string_new_utf16(text, length)
                   : nullptr;
    };
    api.string_new_wrapper = &Api_StringNew;
    api.string_length = &Api_StringLengthSafe;
    api.string_chars = [](void *str) -> const uint16_t * {
        return g_api && g_api->il2cpp_string_chars && str ? g_api->il2cpp_string_chars((Il2CppString *)str) : nullptr;
    };
    api.string_intern = [](void *str) -> void * {
        return g_api && g_api->il2cpp_string_intern && str ? g_api->il2cpp_string_intern((Il2CppString *)str) : nullptr;
    };
    api.string_is_interned = [](void *str) -> void * {
        return g_api && g_api->il2cpp_string_is_interned && str ? g_api->il2cpp_string_is_interned((Il2CppString *)str)
                                                                : nullptr;
    };

    api.array_class_get = [](const void *element_class, uint32_t rank) -> const void * {
        return g_api && g_api->il2cpp_array_class_get && element_class
                   ? g_api->il2cpp_array_class_get((Il2CppClass *)const_cast<void *>(element_class), rank)
                   : nullptr;
    };
    api.bounded_array_class_get = [](const void *element_class, uint32_t rank, int bounded) -> const void * {
        return g_api && g_api->il2cpp_bounded_array_class_get && element_class
                   ? g_api->il2cpp_bounded_array_class_get((Il2CppClass *)const_cast<void *>(element_class), rank,
                                                           bounded != 0)
                   : nullptr;
    };
    api.array_get_byte_length = [](void *array) -> size_t {
        return g_api && g_api->il2cpp_array_get_byte_length && array
                   ? static_cast<size_t>(g_api->il2cpp_array_get_byte_length((Il2CppArray *)array))
                   : 0;
    };
    api.array_element_size = [](const void *array_class) -> int {
        return g_api && g_api->il2cpp_array_element_size && array_class
                   ? g_api->il2cpp_array_element_size((const Il2CppClass *)array_class)
                   : 0;
    };
    api.array_new = [](const void *element_class, size_t length) -> void * {
        return g_api && g_api->il2cpp_array_new && element_class
                   ? g_api->il2cpp_array_new((Il2CppClass *)const_cast<void *>(element_class),
                                             static_cast<uintptr_t>(length))
                   : nullptr;
    };
    api.array_new_specific = [](const void *array_class, size_t length) -> void * {
        return g_api && g_api->il2cpp_array_new_specific && array_class
                   ? g_api->il2cpp_array_new_specific((Il2CppClass *)const_cast<void *>(array_class),
                                                      static_cast<uintptr_t>(length))
                   : nullptr;
    };
    api.array_new_full = [](const void *array_class, size_t *lengths, size_t *lower_bounds) -> void * {
        return g_api && g_api->il2cpp_array_new_full && array_class && lengths
                   ? g_api->il2cpp_array_new_full((Il2CppClass *)const_cast<void *>(array_class),
                                                  reinterpret_cast<il2cpp_array_size_t *>(lengths),
                                                  reinterpret_cast<il2cpp_array_size_t *>(lower_bounds))
                   : nullptr;
    };

    api.runtime_invoke_convert_args = [](const void *method, void *object, void **params, int param_count,
                                         void **exception) -> void * {
        return g_api && g_api->il2cpp_runtime_invoke_convert_args && method
                   ? g_api->il2cpp_runtime_invoke_convert_args((const Il2CppMethod *)method, object,
                                                               reinterpret_cast<Il2CppObject **>(params), param_count,
                                                               reinterpret_cast<Il2CppObject **>(exception))
                   : nullptr;
    };
    api.runtime_class_init = [](const void *klass) {
        if (g_api && g_api->il2cpp_runtime_class_init && klass)
            g_api->il2cpp_runtime_class_init((Il2CppClass *)const_cast<void *>(klass));
    };
    api.runtime_object_init = [](void *object) {
        if (g_api && g_api->il2cpp_runtime_object_init && object)
            g_api->il2cpp_runtime_object_init((Il2CppObject *)object);
    };
    api.runtime_object_init_exception = [](void *object, void **exception) {
        if (g_api && g_api->il2cpp_runtime_object_init_exception && object)
            g_api->il2cpp_runtime_object_init_exception((Il2CppObject *)object,
                                                        reinterpret_cast<Il2CppObject **>(exception));
    };
    api.runtime_unhandled_exception_policy_set = [](int value) {
        if (g_api && g_api->il2cpp_runtime_unhandled_exception_policy_set)
            g_api->il2cpp_runtime_unhandled_exception_policy_set(
                static_cast<Il2CppRuntimeUnhandledExceptionPolicy>(value));
    };

    api.raise_exception = [](void *exception) {
        if (g_api && g_api->il2cpp_raise_exception && exception)
            g_api->il2cpp_raise_exception((Il2CppException *)exception);
    };
    api.exception_from_name_msg = [](const void *image, const char *name_space, const char *name,
                                     const char *msg) -> void * {
        return g_api && g_api->il2cpp_exception_from_name_msg && image
                   ? g_api->il2cpp_exception_from_name_msg((Il2CppImage *)const_cast<void *>(image), name_space, name,
                                                           msg)
                   : nullptr;
    };
    api.get_exception_argument_null = [](const char *arg) -> void * {
        return g_api && g_api->il2cpp_get_exception_argument_null ? g_api->il2cpp_get_exception_argument_null(arg)
                                                                  : nullptr;
    };
    api.format_exception = [](const void *exception, char *message, int message_size) {
        if (g_api && g_api->il2cpp_format_exception && exception && message)
            g_api->il2cpp_format_exception((const Il2CppException *)exception, message, message_size);
    };
    api.format_stack_trace = [](const void *exception, char *output, int output_size) {
        if (g_api && g_api->il2cpp_format_stack_trace && exception && output)
            g_api->il2cpp_format_stack_trace((const Il2CppException *)exception, output, output_size);
    };
    api.unhandled_exception = [](void *exception) {
        if (g_api && g_api->il2cpp_unhandled_exception && exception)
            g_api->il2cpp_unhandled_exception((Il2CppException *)exception);
    };

    api.gc_collect = [](int max_generations) {
        if (g_api && g_api->il2cpp_gc_collect)
            g_api->il2cpp_gc_collect(max_generations);
    };
    api.gc_get_used_size = []() -> int64_t {
        return g_api && g_api->il2cpp_gc_get_used_size ? g_api->il2cpp_gc_get_used_size() : 0;
    };
    api.gc_get_heap_size = []() -> int64_t {
        return g_api && g_api->il2cpp_gc_get_heap_size ? g_api->il2cpp_gc_get_heap_size() : 0;
    };
    api.gchandle_new = [](void *object, int pinned) -> uint32_t {
        return g_api && g_api->il2cpp_gchandle_new && object
                   ? g_api->il2cpp_gchandle_new((Il2CppObject *)object, pinned != 0)
                   : 0;
    };
    api.gchandle_new_weakref = [](void *object, int track_resurrection) -> uint32_t {
        return g_api && g_api->il2cpp_gchandle_new_weakref && object
                   ? g_api->il2cpp_gchandle_new_weakref((Il2CppObject *)object, track_resurrection != 0)
                   : 0;
    };
    api.gchandle_get_target = [](uint32_t gchandle) -> void * {
        return g_api && g_api->il2cpp_gchandle_get_target && gchandle ? g_api->il2cpp_gchandle_get_target(gchandle)
                                                                      : nullptr;
    };
    api.gchandle_free = [](uint32_t gchandle) {
        if (g_api && g_api->il2cpp_gchandle_free && gchandle)
            g_api->il2cpp_gchandle_free(gchandle);
    };

    api.thread_get_name = [](const void *thread, uint32_t *length) -> char * {
        if (!g_api || !g_api->il2cpp_thread_get_name || !thread)
            return nullptr;
        if (!g_api->il2cpp_free) {
            SetError("IL2CPP: il2cpp_thread_get_name requires il2cpp_free to avoid "
                     "leaking runtime-allocated memory");
            return nullptr;
        }
        return g_api->il2cpp_thread_get_name((Il2CppThread *)const_cast<void *>(thread), length);
    };
    api.thread_get_all_attached_threads = [](size_t *size) -> const void ** {
        if (!g_api || !g_api->il2cpp_thread_get_all_attached_threads)
            return nullptr;
        if (!g_api->il2cpp_free) {
            SetError("IL2CPP: il2cpp_thread_get_all_attached_threads requires "
                     "il2cpp_free to avoid leaking runtime-allocated memory");
            return nullptr;
        }
        return (const void **)g_api->il2cpp_thread_get_all_attached_threads(size);
    };
    api.is_vm_thread = [](const void *thread) -> int {
        return g_api && g_api->il2cpp_is_vm_thread && thread &&
                       g_api->il2cpp_is_vm_thread((Il2CppThread *)const_cast<void *>(thread))
                   ? 1
                   : 0;
    };
    api.current_thread_walk_frame_stack = [](void *callback, void *user_data) {
        if (g_api && g_api->il2cpp_current_thread_walk_frame_stack && callback)
            g_api->il2cpp_current_thread_walk_frame_stack(reinterpret_cast<Il2CppFrameWalkFunc>(callback), user_data);
    };
    api.thread_walk_frame_stack = [](const void *thread, void *callback, void *user_data) {
        if (g_api && g_api->il2cpp_thread_walk_frame_stack && thread && callback)
            g_api->il2cpp_thread_walk_frame_stack((Il2CppThread *)const_cast<void *>(thread),
                                                  reinterpret_cast<Il2CppFrameWalkFunc>(callback), user_data);
    };
    api.current_thread_get_top_frame = [](void *frame) -> int {
        return g_api && g_api->il2cpp_current_thread_get_top_frame && frame &&
                       g_api->il2cpp_current_thread_get_top_frame(*reinterpret_cast<Il2CppStackFrameInfo *>(frame))
                   ? 1
                   : 0;
    };
    api.thread_get_top_frame = [](const void *thread, void *frame) -> int {
        return g_api && g_api->il2cpp_thread_get_top_frame && thread && frame &&
                       g_api->il2cpp_thread_get_top_frame((Il2CppThread *)const_cast<void *>(thread),
                                                          *reinterpret_cast<Il2CppStackFrameInfo *>(frame))
                   ? 1
                   : 0;
    };
    api.current_thread_get_frame_at = [](int32_t offset, void *frame) -> int {
        return g_api && g_api->il2cpp_current_thread_get_frame_at && frame &&
                       g_api->il2cpp_current_thread_get_frame_at(offset,
                                                                 *reinterpret_cast<Il2CppStackFrameInfo *>(frame))
                   ? 1
                   : 0;
    };
    api.thread_get_frame_at = [](const void *thread, int32_t offset, void *frame) -> int {
        return g_api && g_api->il2cpp_thread_get_frame_at && thread && frame &&
                       g_api->il2cpp_thread_get_frame_at((Il2CppThread *)const_cast<void *>(thread), offset,
                                                         *reinterpret_cast<Il2CppStackFrameInfo *>(frame))
                   ? 1
                   : 0;
    };
    api.current_thread_get_stack_depth = []() -> int32_t {
        return g_api && g_api->il2cpp_current_thread_get_stack_depth ? g_api->il2cpp_current_thread_get_stack_depth()
                                                                     : 0;
    };
    api.thread_get_stack_depth = [](const void *thread) -> int32_t {
        return g_api && g_api->il2cpp_thread_get_stack_depth && thread
                   ? g_api->il2cpp_thread_get_stack_depth((Il2CppThread *)const_cast<void *>(thread))
                   : 0;
    };

    api.monitor_enter = [](void *object) {
        if (g_api && g_api->il2cpp_monitor_enter && object)
            g_api->il2cpp_monitor_enter((Il2CppObject *)object);
    };
    api.monitor_try_enter = [](void *object, uint32_t timeout) -> int {
        return g_api && g_api->il2cpp_monitor_try_enter && object &&
                       g_api->il2cpp_monitor_try_enter((Il2CppObject *)object, timeout)
                   ? 1
                   : 0;
    };
    api.monitor_exit = [](void *object) {
        if (g_api && g_api->il2cpp_monitor_exit && object)
            g_api->il2cpp_monitor_exit((Il2CppObject *)object);
    };
    api.monitor_pulse = [](void *object) {
        if (g_api && g_api->il2cpp_monitor_pulse && object)
            g_api->il2cpp_monitor_pulse((Il2CppObject *)object);
    };
    api.monitor_pulse_all = [](void *object) {
        if (g_api && g_api->il2cpp_monitor_pulse_all && object)
            g_api->il2cpp_monitor_pulse_all((Il2CppObject *)object);
    };
    api.monitor_wait = [](void *object) {
        if (g_api && g_api->il2cpp_monitor_wait && object)
            g_api->il2cpp_monitor_wait((Il2CppObject *)object);
    };
    api.monitor_try_wait = [](void *object, uint32_t timeout) -> int {
        return g_api && g_api->il2cpp_monitor_try_wait && object &&
                       g_api->il2cpp_monitor_try_wait((Il2CppObject *)object, timeout)
                   ? 1
                   : 0;
    };

    api.delegate_begin_invoke = [](void *delegate_obj, void **params, void *async_callback, void *state) -> void * {
        return g_api && g_api->il2cpp_delegate_begin_invoke && delegate_obj
                   ? g_api->il2cpp_delegate_begin_invoke((Il2CppDelegate *)delegate_obj, params,
                                                         (Il2CppDelegate *)async_callback, (Il2CppObject *)state)
                   : nullptr;
    };
    api.delegate_end_invoke = [](void *async_result, void **out_args) -> void * {
        return g_api && g_api->il2cpp_delegate_end_invoke && async_result
                   ? g_api->il2cpp_delegate_end_invoke((Il2CppAsyncResult *)async_result, out_args)
                   : nullptr;
    };

    api.profiler_install = [](void *profiler, void *shutdown_callback) {
        if (g_api && g_api->il2cpp_profiler_install)
            g_api->il2cpp_profiler_install((Il2CppProfiler *)profiler,
                                           reinterpret_cast<Il2CppProfileFunc>(shutdown_callback));
    };
    api.profiler_set_events = [](int events) {
        if (g_api && g_api->il2cpp_profiler_set_events)
            g_api->il2cpp_profiler_set_events(static_cast<Il2CppProfileFlags>(events));
    };
    api.profiler_install_enter_leave = [](void *enter, void *leave) {
        if (g_api && g_api->il2cpp_profiler_install_enter_leave)
            g_api->il2cpp_profiler_install_enter_leave(reinterpret_cast<Il2CppProfileMethodFunc>(enter),
                                                       reinterpret_cast<Il2CppProfileMethodFunc>(leave));
    };
    api.profiler_install_allocation = [](void *callback) {
        if (g_api && g_api->il2cpp_profiler_install_allocation)
            g_api->il2cpp_profiler_install_allocation(reinterpret_cast<Il2CppProfileAllocFunc>(callback));
    };
    api.profiler_install_gc = [](void *callback, void *heap_resize_callback) {
        if (g_api && g_api->il2cpp_profiler_install_gc)
            g_api->il2cpp_profiler_install_gc(reinterpret_cast<Il2CppProfileGCFunc>(callback),
                                              reinterpret_cast<Il2CppProfileGCResizeFunc>(heap_resize_callback));
    };

    api.unity_liveness_calculation_begin = [](const void *filter, int max_object_count, void *callback, void *userdata,
                                              void *on_world_started, void *on_world_stopped) -> void * {
        return g_api && g_api->il2cpp_unity_liveness_calculation_begin
                   ? g_api->il2cpp_unity_liveness_calculation_begin(
                         (Il2CppClass *)const_cast<void *>(filter), max_object_count,
                         reinterpret_cast<Il2CppRegisterObjectCallback>(callback), userdata,
                         reinterpret_cast<Il2CppWorldChangedCallback>(on_world_started),
                         reinterpret_cast<Il2CppWorldChangedCallback>(on_world_stopped))
                   : nullptr;
    };
    api.unity_liveness_calculation_end = [](void *state) {
        if (g_api && g_api->il2cpp_unity_liveness_calculation_end && state)
            g_api->il2cpp_unity_liveness_calculation_end(state);
    };
    api.unity_liveness_calculation_from_root = [](void *root, void *state) {
        if (g_api && g_api->il2cpp_unity_liveness_calculation_from_root && root && state)
            g_api->il2cpp_unity_liveness_calculation_from_root((Il2CppObject *)root, state);
    };
    api.unity_liveness_calculation_from_statics = [](void *state) {
        if (g_api && g_api->il2cpp_unity_liveness_calculation_from_statics && state)
            g_api->il2cpp_unity_liveness_calculation_from_statics(state);
    };

    api.stats_dump_to_file = [](const char *path) -> int {
        return g_api && g_api->il2cpp_stats_dump_to_file && path && g_api->il2cpp_stats_dump_to_file(path) ? 1 : 0;
    };
    api.stats_get_value = [](int stat) -> uint64_t {
        return g_api && g_api->il2cpp_stats_get_value ? g_api->il2cpp_stats_get_value(static_cast<Il2CppStat>(stat))
                                                      : 0;
    };
    api.capture_memory_snapshot = []() -> void * {
        return g_api && g_api->il2cpp_capture_memory_snapshot ? g_api->il2cpp_capture_memory_snapshot() : nullptr;
    };
    api.free_captured_memory_snapshot = [](void *snapshot) {
        if (g_api && g_api->il2cpp_free_captured_memory_snapshot && snapshot)
            g_api->il2cpp_free_captured_memory_snapshot((Il2CppManagedMemorySnapshot *)snapshot);
    };

    api.debug_get_class_info = [](const void *klass) -> const void * {
        return g_api && g_api->il2cpp_debug_get_class_info && klass
                   ? g_api->il2cpp_debug_get_class_info((const Il2CppClass *)klass)
                   : nullptr;
    };
    api.debug_class_get_document = [](const void *info) -> const void * {
        return g_api && g_api->il2cpp_debug_class_get_document && info
                   ? g_api->il2cpp_debug_class_get_document((const Il2CppDebugTypeInfo *)info)
                   : nullptr;
    };
    api.debug_document_get_filename = [](const void *document) -> const char * {
        return g_api && g_api->il2cpp_debug_document_get_filename && document
                   ? g_api->il2cpp_debug_document_get_filename((const Il2CppDebugDocument *)document)
                   : nullptr;
    };
    api.debug_document_get_directory = [](const void *document) -> const char * {
        return g_api && g_api->il2cpp_debug_document_get_directory && document
                   ? g_api->il2cpp_debug_document_get_directory((const Il2CppDebugDocument *)document)
                   : nullptr;
    };
    api.debug_get_method_info = [](const void *method) -> const void * {
        return g_api && g_api->il2cpp_debug_get_method_info && method
                   ? g_api->il2cpp_debug_get_method_info((const Il2CppMethod *)method)
                   : nullptr;
    };
    api.debug_method_get_document = [](const void *info) -> const void * {
        return g_api && g_api->il2cpp_debug_method_get_document && info
                   ? g_api->il2cpp_debug_method_get_document((const Il2CppDebugMethodInfo *)info)
                   : nullptr;
    };
    api.debug_method_get_offset_table = [](const void *info) -> const int32_t * {
        return g_api && g_api->il2cpp_debug_method_get_offset_table && info
                   ? g_api->il2cpp_debug_method_get_offset_table((const Il2CppDebugMethodInfo *)info)
                   : nullptr;
    };
    api.debug_method_get_code_size = [](const void *info) -> size_t {
        return g_api && g_api->il2cpp_debug_method_get_code_size && info
                   ? g_api->il2cpp_debug_method_get_code_size((const Il2CppDebugMethodInfo *)info)
                   : 0;
    };
    api.debug_update_frame_il_offset = [](int32_t il_offset) {
        if (g_api && g_api->il2cpp_debug_update_frame_il_offset)
            g_api->il2cpp_debug_update_frame_il_offset(il_offset);
    };
    api.debug_method_get_locals_info = [](const void *info) -> const void ** {
        return g_api && g_api->il2cpp_debug_method_get_locals_info && info
                   ? reinterpret_cast<const void **>(
                         g_api->il2cpp_debug_method_get_locals_info((const Il2CppDebugMethodInfo *)info))
                   : nullptr;
    };
    api.debug_local_get_type = [](const void *info) -> const void * {
        return g_api && g_api->il2cpp_debug_local_get_type && info
                   ? g_api->il2cpp_debug_local_get_type((const Il2CppDebugLocalsInfo *)info)
                   : nullptr;
    };
    api.debug_local_get_name = [](const void *info) -> const char * {
        return g_api && g_api->il2cpp_debug_local_get_name && info
                   ? g_api->il2cpp_debug_local_get_name((const Il2CppDebugLocalsInfo *)info)
                   : nullptr;
    };
    api.debug_local_get_start_offset = [](const void *info) -> uint32_t {
        return g_api && g_api->il2cpp_debug_local_get_start_offset && info
                   ? g_api->il2cpp_debug_local_get_start_offset((const Il2CppDebugLocalsInfo *)info)
                   : 0;
    };
    api.debug_local_get_end_offset = [](const void *info) -> uint32_t {
        return g_api && g_api->il2cpp_debug_local_get_end_offset && info
                   ? g_api->il2cpp_debug_local_get_end_offset((const Il2CppDebugLocalsInfo *)info)
                   : 0;
    };
    api.debug_method_get_param_value = [](const void *info, uint32_t position) -> void * {
        return g_api && g_api->il2cpp_debug_method_get_param_value && info
                   ? g_api->il2cpp_debug_method_get_param_value((const Il2CppStackFrameInfo *)info, position)
                   : nullptr;
    };
    api.debug_frame_get_local_value = [](const void *info, uint32_t position) -> void * {
        return g_api && g_api->il2cpp_debug_frame_get_local_value && info
                   ? g_api->il2cpp_debug_frame_get_local_value((const Il2CppStackFrameInfo *)info, position)
                   : nullptr;
    };
    api.debug_method_get_breakpoint_data_at = [](const void *info, int64_t uid, int32_t offset) -> void * {
        return g_api && g_api->il2cpp_debug_method_get_breakpoint_data_at && info
                   ? g_api->il2cpp_debug_method_get_breakpoint_data_at((const Il2CppDebugMethodInfo *)info, uid, offset)
                   : nullptr;
    };
    api.debug_method_set_breakpoint_data_at = [](const void *info, uint64_t location, void *data) {
        if (g_api && g_api->il2cpp_debug_method_set_breakpoint_data_at && info)
            g_api->il2cpp_debug_method_set_breakpoint_data_at((const Il2CppDebugMethodInfo *)info, location, data);
    };
    api.debug_method_clear_breakpoint_data = [](const void *info) {
        if (g_api && g_api->il2cpp_debug_method_clear_breakpoint_data && info)
            g_api->il2cpp_debug_method_clear_breakpoint_data((const Il2CppDebugMethodInfo *)info);
    };
    api.debug_method_clear_breakpoint_data_at = [](const void *info, uint64_t location) {
        if (g_api && g_api->il2cpp_debug_method_clear_breakpoint_data_at && info)
            g_api->il2cpp_debug_method_clear_breakpoint_data_at((const Il2CppDebugMethodInfo *)info, location);
    };
    api.attach_managed_method_hook = &Api_AttachManagedMethodHook;
    api.object_header_size = []() -> uint32_t {
        return g_api && g_api->il2cpp_object_header_size ? g_api->il2cpp_object_header_size() : 0;
    };
    api.array_object_header_size = []() -> uint32_t {
        return g_api && g_api->il2cpp_array_object_header_size ? g_api->il2cpp_array_object_header_size() : 0;
    };
    api.offset_of_array_length_in_array_object_header = []() -> uint32_t {
        return g_api && g_api->il2cpp_offset_of_array_length_in_array_object_header
                   ? g_api->il2cpp_offset_of_array_length_in_array_object_header()
                   : 0;
    };
    api.offset_of_array_bounds_in_array_object_header = []() -> uint32_t {
        return g_api && g_api->il2cpp_offset_of_array_bounds_in_array_object_header
                   ? g_api->il2cpp_offset_of_array_bounds_in_array_object_header()
                   : 0;
    };
    api.allocation_granularity = []() -> uint32_t {
        return g_api && g_api->il2cpp_allocation_granularity ? g_api->il2cpp_allocation_granularity() : 0;
    };
    api.array_set_ref = &Api_ArraySetRef;
    return api;
}();
} // namespace

bool Il2CppApi::valid() const {
    return exportsValidated && gameAssembly && il2cpp_domain_get && il2cpp_domain_get_assemblies && il2cpp_assembly_get_image &&
           il2cpp_image_get_name && il2cpp_class_from_name && il2cpp_class_get_methods && il2cpp_method_get_name &&
           il2cpp_method_get_param_count &&
           il2cpp_method_get_param && il2cpp_type_get_name && il2cpp_class_get_field_from_name &&
           il2cpp_field_get_offset;
}

bool Il2CppApi::thread_attach_available() const {
    return il2cpp_thread_attach && il2cpp_thread_detach && il2cpp_thread_current;
}

bool Il2CppApi::MetadataAccessReady() const {
    return metadataReady && valid() && thread_attach_available() && cachedDomain;
}

void *Il2CppApi::MethodPointer(const Il2CppMethod *method) const {
    if (!method) {
        SetError("IL2CPP: native method target resolution requires a non-null MethodInfo");
        return nullptr;
    }

    void *target = nullptr;
    DWORD exceptionCode = 0;
    if (!SehMethodPointerGet(method, &target, &exceptionCode)) {
        char message[256]{};
        std::snprintf(message, sizeof(message),
                      "IL2CPP: reading MethodInfo::methodPointer raised native exception 0x%08lX method=%p",
                      exceptionCode, method);
        SetError(message);
        return nullptr;
    }

    if (!target) {
        SetError(std::string("IL2CPP: MethodInfo::methodPointer is null for method=") + PtrString(method));
        return nullptr;
    }

    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(target, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT ||
        (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
        SetError(std::string("IL2CPP: MethodInfo::methodPointer is not readable executable memory: method=") +
                 PtrString(method) + " target=" + PtrString(target));
        return nullptr;
    }

    const DWORD protection = memory.Protect & 0xff;
    const bool executable = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
                            protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    if (!executable) {
        SetError(std::string("IL2CPP: MethodInfo::methodPointer is not executable: method=") + PtrString(method) +
                 " target=" + PtrString(target));
        return nullptr;
    }

    HMODULE targetModule = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(target), &targetModule) ||
        (targetModule != gameAssembly && targetModule != unityPlayer)) {
        SetError(std::string("IL2CPP: MethodInfo::methodPointer is outside GameAssembly.dll and UnityPlayer.dll: ") +
                 "method=" + PtrString(method) + " target=" + PtrString(target));
        return nullptr;
    }

    g_lastError.clear();
    return target;
}

Il2CppDomain *Il2CppApi::Domain() const {
    return MetadataAccessReady() ? cachedDomain : nullptr;
}

const Il2CppImage *Il2CppApi::FindImage(const char *imageName) const {
    if (!MetadataAccessReady()) {
        SetError("IL2CPP: metadata access is not ready during image lookup");
        return nullptr;
    }
    Il2CppThreadScope attach(*this);
    if (!attach.ok())
        return nullptr;
    Il2CppDomain *domain = Domain();
    if (Empty(imageName))
        return nullptr;
    if (!domain) {
        SetError("IL2CPP: domain unavailable during image lookup");
        return nullptr;
    }
    const std::string cacheKey = RuntimeCachePrefix(*this) + "|image|" + ImageLookupKey(imageName);
    {
        const Il2CppImage *cached = nullptr;
        std::scoped_lock lock(g_il2cppLookupCacheMutex);
        if (TryGetCached(g_il2cppLookupCaches.images, cacheKey, cached))
            return cached;
    }
    size_t count = 0;
    const Il2CppAssembly **assemblies =
        InvokeMetadata("il2cpp_domain_get_assemblies during image lookup", il2cpp_domain_get_assemblies, domain, &count);
    if (!assemblies) {
        SetError(std::string("IL2CPP: assembly list unavailable during image "
                             "lookup: requested=\"") +
                 imageName + "\"");
        return nullptr;
    }
    const auto requestedVariants = ImageNameVariants(imageName);
    for (size_t i = 0; i < count; ++i) {
        const Il2CppImage *image = InvokeMetadata("il2cpp_assembly_get_image during image lookup",
                                                   il2cpp_assembly_get_image, assemblies[i]);
        const char *actualName = image ? InvokeMetadata("il2cpp_image_get_name during image lookup",
                                                         il2cpp_image_get_name, image)
                                       : nullptr;
        if (image && ValidateMetadataCString(actualName, "il2cpp_image_get_name") &&
            ImageNameMatches(requestedVariants, actualName)) {
            std::scoped_lock lock(g_il2cppLookupCacheMutex);
            g_il2cppLookupCaches.images[cacheKey] = image;
            return image;
        }
    }
    return nullptr;
}

Il2CppClass *Il2CppApi::FindClass(const char *imageName, const char *namespc, const char *name) const {
    if (!MetadataAccessReady()) {
        SetError("IL2CPP: metadata access is not ready during class lookup");
        return nullptr;
    }
    Il2CppThreadScope attach(*this);
    if (!attach.ok())
        return nullptr;
    if (!Empty(imageName) && !Empty(name)) {
        const std::string cacheKey = ClassLookupKey(*this, imageName, namespc, name);
        Il2CppClass *cached = nullptr;
        {
            std::scoped_lock lock(g_il2cppLookupCacheMutex);
            if (TryGetCached(g_il2cppLookupCaches.classes, cacheKey, cached))
                return cached;
        }
        const Il2CppImage *image = FindImage(imageName);
        Il2CppClass *klass = image ? InvokeMetadata("il2cpp_class_from_name during class lookup", il2cpp_class_from_name,
                                                     image, namespc ? namespc : "", name)
                                   : nullptr;
        if (klass) {
            std::scoped_lock lock(g_il2cppLookupCacheMutex);
            g_il2cppLookupCaches.classes[cacheKey] = klass;
        }
        return klass;
    }
    const Il2CppImage *image = FindImage(imageName);
    return image && !Empty(name)
               ? InvokeMetadata("il2cpp_class_from_name during class lookup", il2cpp_class_from_name, image,
                                namespc ? namespc : "", name)
               : nullptr;
}

const Il2CppMethod *Il2CppApi::FindMethod(Il2CppClass *klass, const char *name, int argc) const {
    if (!MetadataAccessReady()) {
        SetError("IL2CPP: metadata access is not ready during method lookup");
        return nullptr;
    }
    Il2CppThreadScope attach(*this);
    if (!attach.ok())
        return nullptr;
    if (!klass || Empty(name) || argc < 0)
        return nullptr;
    const std::string cacheKey = MethodLookupKey(klass, name, argc);
    {
        const Il2CppMethod *cached = nullptr;
        std::scoped_lock lock(g_il2cppLookupCacheMutex);
        if (TryGetCached(g_il2cppLookupCaches.methods, cacheKey, cached))
            return cached;
    }
    Il2CppClass *current = klass;
    while (current) {
        const Il2CppMethod *match = nullptr;
        size_t candidateCount = 0;
        void *iterator = nullptr;
        while (const Il2CppMethod *candidate =
                   InvokeMetadata("il2cpp_class_get_methods during method lookup", il2cpp_class_get_methods, current,
                                  &iterator)) {
            const char *candidateName =
                InvokeMetadata("il2cpp_method_get_name during method lookup", il2cpp_method_get_name, candidate);
            if (candidateName && !ValidateMetadataCString(candidateName, "il2cpp_method_get_name"))
                return nullptr;
            if (!candidateName || std::strcmp(candidateName, name) != 0 ||
                static_cast<int>(InvokeMetadata("il2cpp_method_get_param_count during method lookup",
                                                 il2cpp_method_get_param_count, candidate)) != argc) {
                continue;
            }
            ++candidateCount;
            if (!match)
                match = candidate;
        }
        if (candidateCount > 1) {
            SetError(std::string("IL2CPP: ambiguous method lookup: method=\"") + name + "\" argc=" +
                     std::to_string(argc) + " requestedClass=" + PtrString(klass) +
                     " declaringClass=" + PtrString(current) + " candidates=" + std::to_string(candidateCount) +
                     "; use find_method_exact with parameter type names");
            return nullptr;
        }
        if (match) {
            std::scoped_lock lock(g_il2cppLookupCacheMutex);
            g_il2cppLookupCaches.methods[cacheKey] = match;
            return match;
        }
        if (!il2cpp_class_get_parent)
            break;
        current = InvokeMetadata("il2cpp_class_get_parent during method lookup", il2cpp_class_get_parent, current);
    }
    return nullptr;
}

const Il2CppMethod *Il2CppApi::FindMethodExact(Il2CppClass *klass, const char *name, const char *const *parameterTypes,
                                               int parameterCount) const {
    if (!MetadataAccessReady()) {
        SetError("IL2CPP: metadata access is not ready during exact method lookup");
        return nullptr;
    }
    Il2CppThreadScope attach(*this);
    if (!attach.ok())
        return nullptr;
    if (!klass || Empty(name) || parameterCount < 0 || (parameterCount > 0 && !parameterTypes))
        return nullptr;
    const std::string cacheKey = ExactMethodLookupKey(klass, name, parameterTypes, parameterCount);
    {
        const Il2CppMethod *cached = nullptr;
        std::scoped_lock lock(g_il2cppLookupCacheMutex);
        if (TryGetCached(g_il2cppLookupCaches.exactMethods, cacheKey, cached))
            return cached;
    }
    const Il2CppMethod *match = nullptr;
    int sameArity = 0;
    std::string firstMismatch;
    Il2CppClass *current = klass;
    while (current) {
        void *iter = nullptr;
        while (const Il2CppMethod *method =
                   InvokeMetadata("il2cpp_class_get_methods during exact method lookup", il2cpp_class_get_methods,
                                  current, &iter)) {
            const char *methodName =
                InvokeMetadata("il2cpp_method_get_name during exact method lookup", il2cpp_method_get_name, method);
            if (methodName && !ValidateMetadataCString(methodName, "il2cpp_method_get_name"))
                return nullptr;
            if (!methodName || std::strcmp(methodName, name) != 0)
                continue;
            if (static_cast<int>(InvokeMetadata("il2cpp_method_get_param_count during exact method lookup",
                                                 il2cpp_method_get_param_count, method)) != parameterCount)
                continue;
            ++sameArity;
            bool same = true;
            for (int i = 0; i < parameterCount; ++i) {
                const std::string actual =
                    TypeName(*this, InvokeMetadata("il2cpp_method_get_param during exact method lookup",
                                                    il2cpp_method_get_param, method, static_cast<uint32_t>(i)));
                if (!TypeNameMatches(actual, parameterTypes[i])) {
                    if (firstMismatch.empty()) {
                        firstMismatch = std::string(" first mismatch: param=") + std::to_string(i) + " requested=\"" +
                                        parameterTypes[i] + "\" actual=\"" + actual + "\"";
                    }
                    same = false;
                    break;
                }
            }
            if (!same)
                continue;
            if (match) {
                SetError(std::string("IL2CPP: ambiguous exact overload: method=\"") + name + "\" params=[" +
                         JoinTypes(parameterTypes, parameterCount) + "]");
                return nullptr;
            }
            match = method;
        }
        if (match) {
            std::scoped_lock lock(g_il2cppLookupCacheMutex);
            g_il2cppLookupCaches.exactMethods[cacheKey] = match;
            return match;
        }
        if (!il2cpp_class_get_parent)
            break;
        current = InvokeMetadata("il2cpp_class_get_parent during exact method lookup", il2cpp_class_get_parent,
                                 current);
    }
    if (!match) {
        SetError(std::string("IL2CPP: exact overload not found: method=\"") + name + "\" params=[" +
                 JoinTypes(parameterTypes, parameterCount) + "] same-arity candidates=" + std::to_string(sameArity) +
                 firstMismatch);
    }
    return nullptr;
}

Il2CppClassField *Il2CppApi::FindField(Il2CppClass *klass, const char *name) const {
    if (!MetadataAccessReady()) {
        SetError("IL2CPP: metadata access is not ready during field lookup");
        return nullptr;
    }
    Il2CppThreadScope attach(*this);
    if (!attach.ok())
        return nullptr;
    if (!klass || Empty(name))
        return nullptr;
    const std::string cacheKey = FieldLookupKey(klass, name);
    {
        Il2CppClassField *cached = nullptr;
        std::scoped_lock lock(g_il2cppLookupCacheMutex);
        if (TryGetCached(g_il2cppLookupCaches.fields, cacheKey, cached))
            return cached;
    }
    Il2CppClassField *field =
        InvokeMetadata("il2cpp_class_get_field_from_name during field lookup", il2cpp_class_get_field_from_name, klass,
                       name);
    if (field) {
        std::scoped_lock lock(g_il2cppLookupCacheMutex);
        g_il2cppLookupCaches.fields[cacheKey] = field;
    }
    return field;
}

bool Il2CppApi::WaitForMetadataAccess(std::chrono::milliseconds timeout, std::chrono::milliseconds preDomainDelay) {
    metadataReady = false;
    cachedDomain = nullptr;
    ClearIl2CppCaches();
    if (!valid())
        return false;
    if (!thread_attach_available()) {
        Log("[IL2CPP][ERROR] Cannot wait for metadata readiness: "
            "il2cpp_thread_attach/il2cpp_thread_detach exports are required.");
        SetError("IL2CPP: thread attach APIs are required before metadata "
                 "readiness probes");
        return false;
    }

    constexpr auto kPoll = std::chrono::milliseconds(100);
    constexpr auto kDomainSettle = std::chrono::milliseconds(250);
    const auto start = std::chrono::steady_clock::now();
    const auto firstDomainProbe = start + preDomainDelay;
    auto nextLog = start;
    Log("[IL2CPP] Exports bound; deferring first il2cpp_domain_get() probe for "
        "%lld ms before metadata readiness checks.",
        static_cast<long long>(preDomainDelay.count()));
    auto firstDomainSeen = std::chrono::steady_clock::time_point{};

    while (std::chrono::steady_clock::now() - start <= timeout) {
        const auto beforeProbe = std::chrono::steady_clock::now();
        if (beforeProbe < firstDomainProbe) {
            if (beforeProbe >= nextLog) {
                const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(beforeProbe - start).count();
                Log("[IL2CPP] Waiting for safe metadata access before first domain "
                    "probe (%lld/%lld ms).",
                    static_cast<long long>(waited), static_cast<long long>(timeout.count()));
                nextLog = beforeProbe + std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(
                std::min(kPoll, std::chrono::duration_cast<std::chrono::milliseconds>(firstDomainProbe - beforeProbe)));
            continue;
        }

        Il2CppDomain *domain = InvokeMetadata("il2cpp_domain_get during metadata readiness", il2cpp_domain_get);
        if (domain) {
            cachedDomain = domain;
            const auto now = std::chrono::steady_clock::now();
            if (firstDomainSeen == std::chrono::steady_clock::time_point{}) {
                firstDomainSeen = now;
                Log("[IL2CPP] Domain is visible; waiting briefly before first metadata "
                    "enumeration probe.");
            }

            if (now - firstDomainSeen >= kDomainSettle) {
                Il2CppThreadScope attach(*this);
                if (attach.ok()) {
                    size_t count = 0;
                    const Il2CppAssembly **assemblies = InvokeMetadata(
                        "il2cpp_domain_get_assemblies during metadata readiness", il2cpp_domain_get_assemblies, domain,
                        &count);
                    if (assemblies && count > 0) {
                        const Il2CppImage *firstImage = InvokeMetadata(
                            "il2cpp_assembly_get_image during metadata readiness", il2cpp_assembly_get_image,
                            assemblies[0]);
                        const char *firstImageName = firstImage ? InvokeMetadata(
                            "il2cpp_image_get_name during metadata readiness", il2cpp_image_get_name, firstImage)
                                                                  : nullptr;
                        if (firstImage && ValidateMetadataCString(firstImageName, "il2cpp_image_get_name")) {
                            cachedDomain = domain;
                            metadataReady = true;
                            Log("[SUCCESS][IL2CPP] Metadata access ready: domain=%p assemblyCount=%zu "
                                "firstImage=%p firstImageName=\"%s\".",
                                domain, count, firstImage, firstImageName);
                            return true;
                        }
                        SetError("IL2CPP: assembly list was present but its first image/name validation failed");
                    } else {
                        SetError("IL2CPP: metadata assembly list is not ready yet");
                    }
                }
            }
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= nextLog) {
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            Log("[IL2CPP] Waiting for safe metadata access (domain/thread "
                "attach/assemblies) (%lld/%lld ms).",
                static_cast<long long>(waited), static_cast<long long>(timeout.count()));
            nextLog = now + std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(kPoll);
    }

    Log("[IL2CPP][ERROR] Timed out after %lld ms waiting for safe metadata "
        "access; native mods will not load. Last error: %s",
        static_cast<long long>(timeout.count()), g_lastError.empty() ? "none" : g_lastError.c_str());
    return false;
}

bool Il2CppApi::TryAssemblyCount(size_t &count) const {
    count = 0;
    if (!MetadataAccessReady()) {
        SetError("IL2CPP: metadata access is not ready; assembly count skipped");
        return false;
    }
    Il2CppThreadScope attach(*this);
    if (!attach.ok())
        return false;
    Il2CppDomain *domain = Domain();
    if (!domain)
        return false;
    const Il2CppAssembly **assemblies =
        InvokeMetadata("il2cpp_domain_get_assemblies during assembly count", il2cpp_domain_get_assemblies, domain,
                       &count);
    return assemblies != nullptr;
}

bool Il2Cpp_BindExports(Il2CppApi &api, int timeoutMs) {
    api = {};
    constexpr std::array candidates{RuntimeModuleCandidate{"GameAssembly.dll", "il2cpp_domain_get"}};
    RuntimeModule module = WaitForRuntime("IL2CPP", candidates, std::chrono::milliseconds(timeoutMs));
    if (!module.handle) {
        Log("[IL2CPP][ERROR] GameAssembly.dll with il2cpp_domain_get was not "
            "discovered.");
        return false;
    }
    api.gameAssembly = module.handle;
    api.gameAssemblyBase = reinterpret_cast<uintptr_t>(module.handle);
    api.unityPlayer = GetModuleHandleA("UnityPlayer.dll");
    api.unityPlayerBase = reinterpret_cast<uintptr_t>(api.unityPlayer);
    if (api.unityPlayer)
        Log("[IL2CPP] UnityPlayer.dll found at %p.", static_cast<void *>(api.unityPlayer));
    else
        Log("[IL2CPP] UnityPlayer.dll is not loaded; continuing with "
            "GameAssembly-only context.");

    StrictIl2CppExportResolver resolver;
    if (!resolver.Initialize(api.gameAssembly)) {
        SetError(std::string("IL2CPP: GameAssembly export table validation failed: ") + resolver.failure());
        Log("[IL2CPP][ERROR] %s. Native mods will not start.", g_lastError.c_str());
        return false;
    }

    bool ok = true;
#define BIND(name)                                                                                                    \
    api.name = reinterpret_cast<name##_t>(                                                                             \
        resolver.BindExact(#name, ExportAbiOf(name##_t{}), Il2CppExportRequirement::Required, ok))
    BIND(il2cpp_domain_get);
    BIND(il2cpp_domain_get_assemblies);
    BIND(il2cpp_assembly_get_image);
    BIND(il2cpp_image_get_name);
    BIND(il2cpp_class_from_name);
    BIND(il2cpp_class_get_methods);
    BIND(il2cpp_method_get_name);
    BIND(il2cpp_method_get_param_count);
    BIND(il2cpp_method_get_param);
    BIND(il2cpp_type_get_name);
    BIND(il2cpp_class_get_field_from_name);
    BIND(il2cpp_field_get_offset);
#undef BIND
#define BIND_OPTIONAL(name)                                                                                           \
    api.name = reinterpret_cast<name##_t>(                                                                             \
        resolver.BindExact(#name, ExportAbiOf(name##_t{}), Il2CppExportRequirement::Optional, ok))
    // lifecycle / config
    BIND_OPTIONAL(il2cpp_init);
    BIND_OPTIONAL(il2cpp_shutdown);
    BIND_OPTIONAL(il2cpp_set_config_dir);
    BIND_OPTIONAL(il2cpp_set_data_dir);
    BIND_OPTIONAL(il2cpp_set_commandline_arguments);
    BIND_OPTIONAL(il2cpp_set_memory_callbacks);
    BIND_OPTIONAL(il2cpp_set_find_plugin_callback);

    // allocation / internal calls
    BIND_OPTIONAL(il2cpp_alloc);
    BIND_OPTIONAL(il2cpp_free);
    BIND_OPTIONAL(il2cpp_add_internal_call);
    BIND_OPTIONAL(il2cpp_resolve_icall);

    // domain / image extras
    BIND_OPTIONAL(il2cpp_domain_assembly_open);
    BIND_OPTIONAL(il2cpp_get_corlib);
    BIND_OPTIONAL(il2cpp_image_get_assembly);
    BIND_OPTIONAL(il2cpp_image_get_filename);
    BIND_OPTIONAL(il2cpp_image_get_class_count);
    BIND_OPTIONAL(il2cpp_image_get_class);
    BIND_OPTIONAL(il2cpp_image_get_entry_point);

    // class extras
    BIND_OPTIONAL(il2cpp_class_from_type);
    BIND_OPTIONAL(il2cpp_class_from_il2cpp_type);
    BIND_OPTIONAL(il2cpp_class_from_system_type);
    BIND_OPTIONAL(il2cpp_class_get_name);
    BIND_OPTIONAL(il2cpp_class_get_namespace);
    BIND_OPTIONAL(il2cpp_class_get_assemblyname);
    BIND_OPTIONAL(il2cpp_class_get_image);
    BIND_OPTIONAL(il2cpp_class_get_parent);
    BIND_OPTIONAL(il2cpp_class_has_parent);
    BIND_OPTIONAL(il2cpp_class_get_declaring_type);
    BIND_OPTIONAL(il2cpp_class_get_element_class);
    BIND_OPTIONAL(il2cpp_class_get_type);
    BIND_OPTIONAL(il2cpp_class_get_type_token);
    BIND_OPTIONAL(il2cpp_class_get_flags);
    BIND_OPTIONAL(il2cpp_class_get_rank);
    BIND_OPTIONAL(il2cpp_class_instance_size);
    BIND_OPTIONAL(il2cpp_class_value_size);
    BIND_OPTIONAL(il2cpp_class_num_fields);
    BIND_OPTIONAL(il2cpp_class_array_element_size);
    BIND_OPTIONAL(il2cpp_class_is_valuetype);
    BIND_OPTIONAL(il2cpp_class_is_enum);
    BIND_OPTIONAL(il2cpp_class_is_generic);
    BIND_OPTIONAL(il2cpp_class_is_inflated);
    BIND_OPTIONAL(il2cpp_class_is_abstract);
    BIND_OPTIONAL(il2cpp_class_is_interface);
    BIND_OPTIONAL(il2cpp_class_is_subclass_of);
    BIND_OPTIONAL(il2cpp_class_is_assignable_from);
    BIND_OPTIONAL(il2cpp_class_has_attribute);
    BIND_OPTIONAL(il2cpp_class_has_references);
    BIND_OPTIONAL(il2cpp_class_enum_basetype);
    BIND_OPTIONAL(il2cpp_class_get_property_from_name);
    BIND_OPTIONAL(il2cpp_class_get_fields);
    BIND_OPTIONAL(il2cpp_class_get_properties);
    BIND_OPTIONAL(il2cpp_class_get_nested_types);
    BIND_OPTIONAL(il2cpp_class_get_interfaces);
    BIND_OPTIONAL(il2cpp_class_get_events);
    BIND_OPTIONAL(il2cpp_class_get_bitmap_size);
    BIND_OPTIONAL(il2cpp_class_get_bitmap);

    // method extras
    BIND_OPTIONAL(il2cpp_method_get_class);
    BIND_OPTIONAL(il2cpp_method_get_declaring_type);
    BIND_OPTIONAL(il2cpp_method_get_return_type);
    BIND_OPTIONAL(il2cpp_method_get_param_name);
    BIND_OPTIONAL(il2cpp_method_get_flags);
    BIND_OPTIONAL(il2cpp_method_get_token);
    BIND_OPTIONAL(il2cpp_method_get_object);
    BIND_OPTIONAL(il2cpp_method_is_generic);
    BIND_OPTIONAL(il2cpp_method_is_inflated);
    BIND_OPTIONAL(il2cpp_method_is_instance);
    BIND_OPTIONAL(il2cpp_method_has_attribute);

    // field extras
    BIND_OPTIONAL(il2cpp_field_get_name);
    BIND_OPTIONAL(il2cpp_field_get_parent);
    BIND_OPTIONAL(il2cpp_field_get_type);
    BIND_OPTIONAL(il2cpp_field_get_flags);
    BIND_OPTIONAL(il2cpp_field_get_value);
    BIND_OPTIONAL(il2cpp_field_set_value);
    BIND_OPTIONAL(il2cpp_field_static_get_value);
    BIND_OPTIONAL(il2cpp_field_static_set_value);
    BIND_OPTIONAL(il2cpp_field_get_value_object);
    BIND_OPTIONAL(il2cpp_field_has_attribute);

    // property extras
    BIND_OPTIONAL(il2cpp_property_get_name);
    BIND_OPTIONAL(il2cpp_property_get_parent);
    BIND_OPTIONAL(il2cpp_property_get_get_method);
    BIND_OPTIONAL(il2cpp_property_get_set_method);
    BIND_OPTIONAL(il2cpp_property_get_flags);

    // type extras
    BIND_OPTIONAL(il2cpp_type_get_type);
    BIND_OPTIONAL(il2cpp_type_get_attrs);
    BIND_OPTIONAL(il2cpp_type_get_object);
    BIND_OPTIONAL(il2cpp_type_get_class_or_element_class);

    // object
    BIND_OPTIONAL(il2cpp_object_get_class);
    BIND_OPTIONAL(il2cpp_object_get_size);
    BIND_OPTIONAL(il2cpp_object_get_virtual_method);
    BIND_OPTIONAL(il2cpp_object_new);
    BIND_OPTIONAL(il2cpp_object_unbox);
    BIND_OPTIONAL(il2cpp_object_is_inst);
    BIND_OPTIONAL(il2cpp_value_box);
    BIND_OPTIONAL(il2cpp_object_header_size);
    BIND_OPTIONAL(il2cpp_array_object_header_size);
    BIND_OPTIONAL(il2cpp_offset_of_array_length_in_array_object_header);
    BIND_OPTIONAL(il2cpp_offset_of_array_bounds_in_array_object_header);
    BIND_OPTIONAL(il2cpp_allocation_granularity);

    // string
    BIND_OPTIONAL(il2cpp_string_new);
    BIND_OPTIONAL(il2cpp_string_new_len);
    BIND_OPTIONAL(il2cpp_string_new_utf16);
    BIND_OPTIONAL(il2cpp_string_new_wrapper);
    BIND_OPTIONAL(il2cpp_string_length);
    BIND_OPTIONAL(il2cpp_string_chars);
    BIND_OPTIONAL(il2cpp_string_intern);
    BIND_OPTIONAL(il2cpp_string_is_interned);

    // array
    BIND_OPTIONAL(il2cpp_array_class_get);
    BIND_OPTIONAL(il2cpp_bounded_array_class_get);
    BIND_OPTIONAL(il2cpp_array_length);
    BIND_OPTIONAL(il2cpp_array_get_byte_length);
    BIND_OPTIONAL(il2cpp_array_element_size);
    BIND_OPTIONAL(il2cpp_array_addr_with_size);
    BIND_OPTIONAL(il2cpp_array_new);
    BIND_OPTIONAL(il2cpp_array_new_specific);
    BIND_OPTIONAL(il2cpp_array_new_full);

    // runtime
    BIND_OPTIONAL(il2cpp_runtime_invoke);
    BIND_OPTIONAL(il2cpp_runtime_invoke_convert_args);
    BIND_OPTIONAL(il2cpp_runtime_class_init);
    BIND_OPTIONAL(il2cpp_runtime_object_init);
    BIND_OPTIONAL(il2cpp_runtime_object_init_exception);
    BIND_OPTIONAL(il2cpp_runtime_unhandled_exception_policy_set);

    // exception
    BIND_OPTIONAL(il2cpp_raise_exception);
    BIND_OPTIONAL(il2cpp_exception_from_name_msg);
    BIND_OPTIONAL(il2cpp_get_exception_argument_null);
    BIND_OPTIONAL(il2cpp_format_exception);
    BIND_OPTIONAL(il2cpp_format_stack_trace);
    BIND_OPTIONAL(il2cpp_unhandled_exception);

    // gc / gchandle
    BIND_OPTIONAL(il2cpp_gc_collect);
    BIND_OPTIONAL(il2cpp_gc_get_used_size);
    BIND_OPTIONAL(il2cpp_gc_get_heap_size);
    BIND_OPTIONAL(il2cpp_gchandle_new);
    BIND_OPTIONAL(il2cpp_gchandle_new_weakref);
    BIND_OPTIONAL(il2cpp_gchandle_get_target);
    BIND_OPTIONAL(il2cpp_gchandle_free);
    BIND_OPTIONAL(il2cpp_gc_wbarrier_set_field);

    // thread / stacktrace
    BIND_OPTIONAL(il2cpp_thread_current);
    BIND_OPTIONAL(il2cpp_thread_attach);
    BIND_OPTIONAL(il2cpp_thread_detach);
    BIND_OPTIONAL(il2cpp_thread_get_name);
    BIND_OPTIONAL(il2cpp_thread_get_all_attached_threads);
    BIND_OPTIONAL(il2cpp_is_vm_thread);
    BIND_OPTIONAL(il2cpp_current_thread_walk_frame_stack);
    BIND_OPTIONAL(il2cpp_thread_walk_frame_stack);
    BIND_OPTIONAL(il2cpp_current_thread_get_top_frame);
    BIND_OPTIONAL(il2cpp_thread_get_top_frame);
    BIND_OPTIONAL(il2cpp_current_thread_get_frame_at);
    BIND_OPTIONAL(il2cpp_thread_get_frame_at);
    BIND_OPTIONAL(il2cpp_current_thread_get_stack_depth);
    BIND_OPTIONAL(il2cpp_thread_get_stack_depth);

    // monitor
    BIND_OPTIONAL(il2cpp_monitor_enter);
    BIND_OPTIONAL(il2cpp_monitor_try_enter);
    BIND_OPTIONAL(il2cpp_monitor_exit);
    BIND_OPTIONAL(il2cpp_monitor_pulse);
    BIND_OPTIONAL(il2cpp_monitor_pulse_all);
    BIND_OPTIONAL(il2cpp_monitor_wait);
    BIND_OPTIONAL(il2cpp_monitor_try_wait);

    // delegate
    BIND_OPTIONAL(il2cpp_delegate_begin_invoke);
    BIND_OPTIONAL(il2cpp_delegate_end_invoke);

    // profiler
    BIND_OPTIONAL(il2cpp_profiler_install);
    BIND_OPTIONAL(il2cpp_profiler_set_events);
    BIND_OPTIONAL(il2cpp_profiler_install_enter_leave);
    BIND_OPTIONAL(il2cpp_profiler_install_allocation);
    BIND_OPTIONAL(il2cpp_profiler_install_gc);

    // liveness
    BIND_OPTIONAL(il2cpp_unity_liveness_calculation_begin);
    BIND_OPTIONAL(il2cpp_unity_liveness_calculation_end);
    BIND_OPTIONAL(il2cpp_unity_liveness_calculation_from_root);
    BIND_OPTIONAL(il2cpp_unity_liveness_calculation_from_statics);

    // stats / memory / debug
    BIND_OPTIONAL(il2cpp_stats_dump_to_file);
    BIND_OPTIONAL(il2cpp_stats_get_value);
    BIND_OPTIONAL(il2cpp_capture_memory_snapshot);
    BIND_OPTIONAL(il2cpp_free_captured_memory_snapshot);
    BIND_OPTIONAL(il2cpp_debug_get_class_info);
    BIND_OPTIONAL(il2cpp_debug_class_get_document);
    BIND_OPTIONAL(il2cpp_debug_document_get_filename);
    BIND_OPTIONAL(il2cpp_debug_document_get_directory);
    BIND_OPTIONAL(il2cpp_debug_get_method_info);
    BIND_OPTIONAL(il2cpp_debug_method_get_document);
    BIND_OPTIONAL(il2cpp_debug_method_get_offset_table);
    BIND_OPTIONAL(il2cpp_debug_method_get_code_size);
    BIND_OPTIONAL(il2cpp_debug_update_frame_il_offset);
    BIND_OPTIONAL(il2cpp_debug_method_get_locals_info);
    BIND_OPTIONAL(il2cpp_debug_local_get_type);
    BIND_OPTIONAL(il2cpp_debug_local_get_name);
    BIND_OPTIONAL(il2cpp_debug_local_get_start_offset);
    BIND_OPTIONAL(il2cpp_debug_local_get_end_offset);
    BIND_OPTIONAL(il2cpp_debug_method_get_param_value);
    BIND_OPTIONAL(il2cpp_debug_frame_get_local_value);
    BIND_OPTIONAL(il2cpp_debug_method_get_breakpoint_data_at);
    BIND_OPTIONAL(il2cpp_debug_method_set_breakpoint_data_at);
    BIND_OPTIONAL(il2cpp_debug_method_clear_breakpoint_data);
    BIND_OPTIONAL(il2cpp_debug_method_clear_breakpoint_data_at);

#undef BIND_OPTIONAL
    if (!ok) {
        SetError("IL2CPP: exact GameAssembly export validation failed; see [IL2CPP][ERROR] export diagnostics");
        Log("[IL2CPP][ERROR] Export binding aborted: a required export is missing or its PE target "
            "failed validation. Native mods will not start.");
        return false;
    }
    api.exportsValidated = true;
    Log("[IL2CPP] Export binding policy summary: optionalUnavailable=%zu sharedExactTargets=%zu.",
        resolver.optional_unavailable(), resolver.shared_exact_targets());
    Log("[IL2CPP] Thread attach APIs: attach=%s current=%s detach=%s.", api.il2cpp_thread_attach ? "found" : "missing",
        api.il2cpp_thread_current ? "found" : "missing", api.il2cpp_thread_detach ? "found" : "missing");
    if (!ok || !api.valid())
        return false;
    Log("[IL2CPP] Export binding complete; domain and metadata probes are "
        "deferred until readiness phase.");
    return true;
}

bool Il2Cpp_WaitForMetadataReady(Il2CppApi &api, int timeoutMs, int preDomainDelayMs) {
    if (!api.valid())
        return false;
    if (!api.thread_attach_available()) {
        Log("[IL2CPP][ERROR] Exports bound, but required IL2CPP thread attach APIs "
            "are missing; domain readiness not safe and metadata readiness not "
            "safe. Refusing native mod startup to avoid unknown-thread crashes.");
        SetError("IL2CPP: il2cpp_thread_attach/il2cpp_thread_detach exports are "
                 "required for backend metadata calls");
        return false;
    }
    if (!api.WaitForMetadataAccess(std::chrono::milliseconds(timeoutMs), std::chrono::milliseconds(preDomainDelayMs))) {
        Log("[IL2CPP][ERROR] IL2CPP exports were bound and thread APIs attach=%s "
            "current=%s detach=%s, but domain readiness not safe or metadata "
            "readiness not safe; refusing native mod startup.",
            api.il2cpp_thread_attach ? "found" : "missing", api.il2cpp_thread_current ? "found" : "missing",
            api.il2cpp_thread_detach ? "found" : "missing");
        return false;
    }
    g_api = &api;
    Il2CppDomain *domain = api.Domain();
    size_t assemblyCount = 0;
    const bool haveAssemblyCount = api.TryAssemblyCount(assemblyCount);
    if (haveAssemblyCount) {
        Log("[IL2CPP] Initialized: GameAssembly base=%p UnityPlayer base=%p "
            "domain=%p assemblyCount=%zu.",
            reinterpret_cast<void *>(api.gameAssemblyBase), reinterpret_cast<void *>(api.unityPlayerBase), domain,
            assemblyCount);
    } else {
        Log("[IL2CPP] Initialized: GameAssembly base=%p UnityPlayer base=%p "
            "domain=%p assemblyCount=skipped (%s).",
            reinterpret_cast<void *>(api.gameAssemblyBase), reinterpret_cast<void *>(api.unityPlayerBase), domain,
            g_lastError.empty() ? "metadata not ready" : g_lastError.c_str());
    }
    return true;
}

const URK_Il2CppApi *ModApi_Il2Cpp(Il2CppApi *api) {
    if (!api || !api->MetadataAccessReady())
        return nullptr;
    if (g_api != api) {
        SetError("IL2CPP: public API table requested for an unpublished backend instance");
        return nullptr;
    }
    return &g_publicApi;
}
