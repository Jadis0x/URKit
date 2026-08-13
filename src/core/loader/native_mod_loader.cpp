#include "native_mod_loader.h"
#include "callback_guard.h"
#include "intro.h"
#include "loader_paths.h"
#include "logger.h"
#include "mod_lifecycle_intercept.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
struct ImportedDllStatus {
    std::string name;
    bool loaded = false;
    bool foundViaSearchPath = false;
    bool foundNextToMod = false;
    bool foundNextToGameExe = false;
};

void AppendMatches(const std::string &pattern, const std::string &dir, std::vector<std::string> *out) {
    if (!out)
        return;
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + pattern).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            out->push_back(dir + fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

std::vector<std::string> FindModCandidates(const std::string &dir) {
    std::vector<std::string> out;
    AppendMatches("*.dll", dir, &out);
    std::sort(out.begin(), out.end(), [](const std::string &left, const std::string &right) {
        return _stricmp(left.c_str(), right.c_str()) < 0;
    });
    return out;
}

const char *NonEmpty(const char *value, const char *fallback) {
    return value && value[0] ? value : fallback;
}

bool TryGetModInfo(URK_GetModInfoFn getInfo, const URK_ModInfo **info, DWORD *exceptionCode, std::string *cppError) {
    if (!getInfo || !info)
        return false;
    __try {
        return urk::guard::InvokeCpp([&] { *info = getInfo(); }, cppError);
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        *info = nullptr;
        return false;
    }
}

bool ReadFileBytes(const std::string &path, std::vector<unsigned char> *bytes) {
    if (!bytes)
        return false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        return false;

    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size <= 0)
        return false;
    stream.seekg(0, std::ios::beg);

    bytes->resize(static_cast<size_t>(size));
    stream.read(reinterpret_cast<char *>(bytes->data()), size);
    return stream.good();
}

bool IsApiSetDllName(const std::string &name) {
    return _strnicmp(name.c_str(), "api-ms-", 7) == 0 || _strnicmp(name.c_str(), "ext-ms-", 7) == 0;
}

DWORD RvaToFileOffset(DWORD rva, const IMAGE_NT_HEADERS *ntHeaders, const IMAGE_SECTION_HEADER *sections) {
    if (!ntHeaders || !sections)
        return 0;

    for (WORD index = 0; index < ntHeaders->FileHeader.NumberOfSections; ++index) {
        const IMAGE_SECTION_HEADER &section = sections[index];
        const DWORD sectionStart = section.VirtualAddress;
        const DWORD sectionSize = std::max(section.Misc.VirtualSize, section.SizeOfRawData);
        if (rva >= sectionStart && rva < sectionStart + sectionSize)
            return section.PointerToRawData + (rva - sectionStart);
    }
    return rva;
}

std::vector<std::string> EnumerateImportedDlls(const std::string &path) {
    std::vector<std::string> imports;
    std::vector<unsigned char> fileBytes;
    if (!ReadFileBytes(path, &fileBytes) || fileBytes.size() < sizeof(IMAGE_DOS_HEADER))
        return imports;

    const auto *dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER *>(fileBytes.data());
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE || dosHeader->e_lfanew <= 0)
        return imports;

    const size_t ntOffset = static_cast<size_t>(dosHeader->e_lfanew);
    if (ntOffset + sizeof(IMAGE_NT_HEADERS) > fileBytes.size())
        return imports;

    const auto *ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS *>(fileBytes.data() + ntOffset);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return imports;

    const auto *sections = IMAGE_FIRST_SECTION(ntHeaders);
    const size_t sectionBytesOffset =
        static_cast<size_t>(reinterpret_cast<const unsigned char *>(sections) - fileBytes.data());
    if (sectionBytesOffset + sizeof(IMAGE_SECTION_HEADER) * ntHeaders->FileHeader.NumberOfSections > fileBytes.size()) {
        return imports;
    }

    const IMAGE_DATA_DIRECTORY &importDirectory = ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!importDirectory.VirtualAddress || !importDirectory.Size)
        return imports;

    DWORD descriptorOffset = RvaToFileOffset(importDirectory.VirtualAddress, ntHeaders, sections);
    if (!descriptorOffset || descriptorOffset >= fileBytes.size())
        return imports;

    while (descriptorOffset + sizeof(IMAGE_IMPORT_DESCRIPTOR) <= fileBytes.size()) {
        const auto *descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR *>(fileBytes.data() + descriptorOffset);
        if (descriptor->Name == 0)
            break;

        const DWORD nameOffset = RvaToFileOffset(descriptor->Name, ntHeaders, sections);
        if (!nameOffset || nameOffset >= fileBytes.size())
            break;

        const char *dllName = reinterpret_cast<const char *>(fileBytes.data() + nameOffset);
        const size_t maxLength = fileBytes.size() - nameOffset;
        size_t length = 0;
        while (length < maxLength && dllName[length] != '\0')
            ++length;
        if (length == maxLength)
            break;

        imports.emplace_back(dllName, length);
        descriptorOffset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
    }

    std::sort(imports.begin(), imports.end(), [](const std::string &left, const std::string &right) {
        return _stricmp(left.c_str(), right.c_str()) < 0;
    });
    imports.erase(std::unique(imports.begin(), imports.end(),
                              [](const std::string &left, const std::string &right) {
                                  return _stricmp(left.c_str(), right.c_str()) == 0;
                              }),
                  imports.end());
    return imports;
}

bool SearchPathContainsDll(const char *searchPath, const std::string &dllName) {
    char resolved[MAX_PATH] = {};
    return SearchPathA(searchPath, dllName.c_str(), nullptr, MAX_PATH, resolved, nullptr) != 0;
}

std::vector<ImportedDllStatus> CollectImportedDllStatus(const std::string &modPath) {
    std::vector<ImportedDllStatus> statuses;
    const std::filesystem::path modDirectory = std::filesystem::path(modPath).parent_path();
    const std::string modDir = modDirectory.string();
    const std::string exeDir = Loader_ExeDir();

    for (const std::string &dllName : EnumerateImportedDlls(modPath)) {
        ImportedDllStatus status;
        status.name = dllName;
        status.loaded = GetModuleHandleA(dllName.c_str()) != nullptr;
        status.foundViaSearchPath = SearchPathContainsDll(nullptr, dllName);
        status.foundNextToMod = SearchPathContainsDll(modDir.c_str(), dllName);
        status.foundNextToGameExe = SearchPathContainsDll(exeDir.c_str(), dllName);
        statuses.push_back(std::move(status));
    }
    return statuses;
}

void LogImportedDllDiagnostics(const std::string &path) {
    const std::vector<ImportedDllStatus> imports = CollectImportedDllStatus(path);
    if (imports.empty()) {
        Log("[INFO]           No import table entries were parsed from the mod DLL.");
        return;
    }

    bool loggedMissingHint = false;
    Log("[INFO]           Imported DLLs:");
    for (const ImportedDllStatus &entry : imports) {
        Log("[INFO]             - %s [loaded=%s search=%s modDir=%s exeDir=%s]", entry.name.c_str(),
            entry.loaded ? "yes" : "no", entry.foundViaSearchPath ? "yes" : "no", entry.foundNextToMod ? "yes" : "no",
            entry.foundNextToGameExe ? "yes" : "no");

        if (!loggedMissingHint && !entry.loaded && !entry.foundViaSearchPath && !entry.foundNextToMod &&
            !entry.foundNextToGameExe && !IsApiSetDllName(entry.name)) {
            Log("[WARNING]           Likely missing dependency candidate: %s", entry.name.c_str());
            loggedMissingHint = true;
        }
    }

    if (!loggedMissingHint) {
        Log("[INFO]           The mod DLL was found; inspect transitive dependencies, "
            "delay-load imports, and plugin-specific sidecar DLLs.");
    }
}

void PublishModInfo(HMODULE module, const std::string &path) {
    auto getInfo = reinterpret_cast<URK_GetModInfoFn>(GetProcAddress(module, "URKGetModInfo"));
    if (!getInfo)
        return;

    const URK_ModInfo *info = nullptr;
    DWORD exceptionCode = 0;
    std::string cppError;
    if (!TryGetModInfo(getInfo, &info, &exceptionCode, &cppError)) {
        if (!cppError.empty()) {
            Log("  [ERROR][metadata ignored] %s: URKGetModInfo threw a C++ exception: %s.", path.c_str(),
                cppError.c_str());
        } else {
            Log("  [ERROR][metadata ignored] %s: URKGetModInfo crashed with native exception 0x%08lX.",
                path.c_str(), exceptionCode);
        }
        return;
    }

    if (!info)
        return;
    Log("[INFO]  [metadata] %s v%s by %s", NonEmpty(info->displayName, NonEmpty(info->projectName, "Unnamed mod")),
        NonEmpty(info->version, "unknown"), NonEmpty(info->author, "Unknown"));
}

bool InvokeModInitExSafely(URK_ModInitExFn init, const URK_ModContext &context, int *status, DWORD *exceptionCode,
                           std::string *cppError) {
    if (!init || !status)
        return false;
    __try {
        return urk::guard::InvokeCpp([&] { *status = init(&context); }, cppError);
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        return false;
    }
}

struct ModLoadStats {
    int discovered = 0;
    int loaded = 0;
    int failed = 0;
};

void PublishModProgress(const std::string &modName, const ModLoadStats &totals, bool loading,
                        const std::string &finalStatus = std::string()) {
    Intro::ModProgress(modName, totals.discovered, totals.loaded, totals.failed, loading, finalStatus);
}
} // namespace

NativeModLoadPlan NativeMods_Discover(const Config &config) {
    NativeModLoadPlan plan;
    if (!config.modPaths.empty()) {
        plan.paths = config.modPaths;
        Log("[INFO][mods] loading %zu explicitly selected mod DLL(s)", plan.paths.size());
    } else {
        const std::string modsDir = Loader_ModsDir(config);
        plan.paths = FindModCandidates(modsDir);
        Log("[INFO][mods] scanning %s (%zu mod candidate found)", modsDir.c_str(), plan.paths.size());
    }

    if (plan.Empty()) {
        const ModLoadStats totals;
        PublishModProgress("", totals, false, "No native mods found");
        Log("[SUCCESS][mods] 0 loaded, 0 failed, 0 discovered; runtime event hooks were not installed.");
    }
    return plan;
}

void NativeMods_Load(const NativeModLoadPlan &plan, const URK_ModContext &context) {
    ModLoadStats totals;
    totals.discovered = static_cast<int>(plan.paths.size());
    if (plan.Empty())
        return;

    for (const auto &path : plan.paths) {
        const std::string filename = std::filesystem::path(path).filename().string();
        PublishModProgress(filename, totals, true);
        SetLastError(ERROR_SUCCESS);
        HMODULE h = LoadLibraryExA(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h) {
            const DWORD error = GetLastError();
            Log("  [ERROR][FAILED] %s: LoadLibrary error %lu (%s)", path.c_str(), error, Loader_WindowsError(error).c_str());
            if (error == ERROR_BAD_EXE_FORMAT)
                Log("[WARNING]           The mod and game must use the same architecture "
                    "(normally x64).");
            if (error == ERROR_MOD_NOT_FOUND) {
                Log("[ERROR]           The mod DLL was found, but a dependent DLL could not "
                    "be resolved.");
                LogImportedDllDiagnostics(path);
            }
            ++totals.failed;
            PublishModProgress(filename, totals, true);
            continue;
        }
        PublishModInfo(h, path);

        const auto initEx = reinterpret_cast<URK_ModInitExFn>(GetProcAddress(h, "ModInitEx"));
        if (!initEx) {
            Log("  [ERROR][FAILED] %s: ModInitEx C export was not found.", path.c_str());
            Log("[INFO]           Use: extern \"C\" __declspec(dllexport) int "
                "ModInitEx(const URK_ModContext* ctx).");
            FreeLibrary(h);
            ++totals.failed;
            PublishModProgress(filename, totals, true);
            continue;
        }
        DWORD exceptionCode = 0;
        int initializationStatus = 0;
        std::string cppError;
        const bool invoked =
            InvokeModInitExSafely(initEx, context, &initializationStatus, &exceptionCode, &cppError);
        if (invoked && initializationStatus != 0) {
            ++totals.loaded;
            Log("  [SUCCESS][LOADED] %s", path.c_str());
        } else {
            if (!invoked) {
                if (!cppError.empty()) {
                    Log("  [ERROR][FAILED] %s: ModInitEx threw a C++ exception: %s.", path.c_str(),
                        cppError.c_str());
                } else {
                    Log("  [ERROR][FAILED] %s: ModInitEx crashed with native exception 0x%08lX.", path.c_str(),
                        exceptionCode);
                }
            } else {
                Log("  [ERROR][FAILED] %s: ModInitEx reported initialization failure; module was not activated.", path.c_str());
            }
            FreeLibrary(h);
            ++totals.failed;
        }
        PublishModProgress(filename, totals, true);
    }
    const std::string finalStatus =
        totals.failed == 0 ? "All mods loaded"
                           : (totals.loaded > 0 ? "Mod loading completed with errors" : "Mod loading failed");
    PublishModProgress("", totals, false, finalStatus);
    Log(totals.failed == 0 ? "[SUCCESS][mods] %d loaded, %d failed, %zu discovered"
                           : "[WARNING][mods] %d loaded, %d failed, %zu discovered",
        totals.loaded, totals.failed,
        static_cast<size_t>(totals.discovered));
}
