#include "generator_service.h"

#include "il2cpp_sdk_generator.h"
#include "mod_project_generator_common.h"
#include "mod_sdk.h"
#include "mono_sdk_generator.h"
#include "project_updater.h"
#include "unreal_sdk_generator.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <system_error>

namespace UrkApp {
namespace {

namespace fs = std::filesystem;

bool PathExists(const fs::path &path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

fs::path AbsolutePath(const std::string &text) {
    std::error_code ec;
    fs::path path = fs::path(TrimPath(text));
    fs::path absolute = fs::absolute(path, ec);
    return ec ? path : absolute.lexically_normal();
}

std::size_t CountFiles(const fs::path &root) {
    std::size_t count = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->path().filename() == "out" && it.depth() == 0) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code typeError;
        if (it->is_regular_file(typeError) && !typeError)
            ++count;
    }
    return count;
}

std::string SizeText(std::uintmax_t bytes) {
    if (bytes >= 1024 * 1024)
        return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string((bytes + 1023) / 1024) + " KB";
}

} // namespace

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string TrimPath(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    const auto last =
        std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); }).base();
    if (first >= last)
        return {};

    value.assign(first, last);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

const char *BackendDisplayName(const std::string &backend) {
    const std::string value = Lower(backend);
    if (value == "unreal")
        return "Unreal";
    if (value == "il2cpp")
        return "IL2CPP";
    if (value == "auto")
        return "Auto";
    return "Mono";
}

bool IsValidBackendSelection(const std::string &value) {
    const std::string lower = Lower(value);
    return lower == "auto" || lower == "mono" || lower == "il2cpp" || lower == "unreal";
}

bool IsUnrealBinariesDir(const fs::path &gameDir) {
    const fs::path binaries = gameDir.parent_path();
    return Lower(gameDir.filename().string()) == "win64" && Lower(binaries.filename().string()) == "binaries" &&
           (PathExists(binaries.parent_path() / "Content") ||
            PathExists(binaries.parent_path().parent_path() / "Engine"));
}

std::optional<BackendDetection> DetectBackend(const fs::path &gameDir) {
    if (PathExists(gameDir / "GameAssembly.dll"))
        return BackendDetection{"il2cpp", "GameAssembly.dll"};
    if (IsUnrealBinariesDir(gameDir))
        return BackendDetection{"unreal", "Binaries/Win64 layout"};

    for (const fs::path &relative : {fs::path("MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll"),
                                     fs::path("Mono/EmbedRuntime/mono.dll"), fs::path("mono-2.0-bdwgc.dll"),
                                     fs::path("mono-2.0-sgen.dll"), fs::path("mono-2.0.dll"), fs::path("mono.dll")}) {
        if (PathExists(gameDir / relative))
            return BackendDetection{"mono", relative.generic_string()};
    }
    return std::nullopt;
}

std::string ExecutableMachine(const fs::path &exe) {
    std::ifstream input(exe, std::ios::binary);
    if (!input)
        return {};
    unsigned char dos[64]{};
    if (!input.read(reinterpret_cast<char *>(dos), sizeof(dos)) || dos[0] != 'M' || dos[1] != 'Z')
        return {};
    const std::uint32_t peOffset = static_cast<std::uint32_t>(dos[60]) | (static_cast<std::uint32_t>(dos[61]) << 8) |
                                   (static_cast<std::uint32_t>(dos[62]) << 16) |
                                   (static_cast<std::uint32_t>(dos[63]) << 24);
    if (peOffset > 64 * 1024 * 1024)
        return {};
    unsigned char header[6]{};
    input.seekg(peOffset);
    if (!input.read(reinterpret_cast<char *>(header), sizeof(header)) || header[0] != 'P' || header[1] != 'E' ||
        header[2] != 0 || header[3] != 0)
        return {};
    const std::uint16_t machine = static_cast<std::uint16_t>(header[4] | (header[5] << 8));
    switch (machine) {
        case 0x8664:
            return "x64";
        case 0x014c:
            return "x86";
        case 0xaa64:
            return "arm64";
        default:
            return "unknown";
    }
}

bool ResolveProjectOptions(ProjectOptions &options, std::string &error) {
    options.backendSelection = Lower(options.backendSelection);
    if (!IsValidBackendSelection(options.backendSelection)) {
        error = "Backend must be auto, mono, il2cpp or unreal.";
        return false;
    }

    options.gameExePath = TrimPath(options.gameExePath);
    if (options.gameExePath.empty()) {
        error = "Select the game executable.";
        return false;
    }
    options.gameExePath = AbsolutePath(options.gameExePath).string();
    const fs::path gameExePath(options.gameExePath);
    if (!PathExists(gameExePath)) {
        error = "Game executable not found: " + options.gameExePath;
        return false;
    }
    if (Lower(gameExePath.extension().string()) != ".exe") {
        error = "Not an .exe file: " + options.gameExePath;
        return false;
    }
    const std::string machine = ExecutableMachine(gameExePath);
    if (machine.empty()) {
        error = "Not a Windows executable: " + options.gameExePath;
        return false;
    }
    if (machine != "x64") {
        error = "The game executable is " + machine + "; URKit supports x64 games only.";
        return false;
    }

    options.gameDir = gameExePath.parent_path().lexically_normal().string();
    options.detectionEvidence.clear();
    if (options.backendSelection == "auto") {
        const std::optional<BackendDetection> detected = DetectBackend(fs::path(options.gameDir));
        if (!detected) {
            error = "Backend not detected. Expected GameAssembly.dll (IL2CPP), a Mono runtime DLL (Mono), or an "
                    "Unreal <Project>/Binaries/Win64 folder. Pick the backend by hand if the game uses another layout.";
            return false;
        }
        options.backend = detected->backend;
        options.detectionEvidence = detected->evidence;
    } else {
        options.backend = options.backendSelection;
    }
    // The root .exe of an Unreal game is a launcher stub; the loader has to sit beside the real one.
    if (options.backend == "unreal" && !IsUnrealBinariesDir(fs::path(options.gameDir))) {
        error = "Unreal games need the executable under <Project>/Binaries/Win64, not the launcher in the game "
                "folder.";
        return false;
    }

    options.projectName = ModProjectGenerator::Identifier(options.projectName, options.backend == "il2cpp"
                                                                                   ? "GeneratedIl2CppMod"
                                                                               : options.backend == "unreal"
                                                                                   ? "GeneratedUnrealMod"
                                                                                   : "GeneratedMod");
    options.exportRoot = (fs::path(options.gameDir) / "urk-sdk-output").lexically_normal().string();
    options.projectOut = (fs::path(options.exportRoot) / options.projectName / "project").lexically_normal().string();
    options.sdkOut = (fs::path(options.projectOut) / "sdk" / options.backend).string();
    options.modsDir = "Mods";
    return true;
}

bool GenerateProject(const ProjectOptions &options, const LogSink &log, std::string &error) {
    const auto started = std::chrono::steady_clock::now();
    const fs::path projectRoot(options.projectOut);
    // Regenerating over a project skips the update's conflict checks and backup.
    if (PathExists(projectRoot / "CMakeLists.txt")) {
        error = "A project already exists at " + options.projectOut +
                ". Update it from the Update tab (urk.exe update), or pick another project name.";
        return false;
    }

    log(LogLevel::Info, "Game: " + options.gameExePath + " (x64)");
    log(LogLevel::Info, std::string("Backend: ") + BackendDisplayName(options.backend) +
                            (options.detectionEvidence.empty() ? " (selected)"
                                                               : " (detected from " + options.detectionEvidence + ")"));
    log(LogLevel::Info, "SDK " + std::to_string(URK_SDK_VERSION) + ", runtime API " +
                            std::to_string(URK_RUNTIME_API_VERSION) + ", mono " + std::to_string(URK_MONO_API_VERSION) +
                            ", il2cpp " + std::to_string(URK_IL2CPP_API_VERSION) + ", unreal " +
                            std::to_string(URK_UNREAL_API_VERSION) + ", network " +
                            std::to_string(URK_NETWORK_API_VERSION));
    log(LogLevel::Info, "Project: " + options.projectOut);

    const std::string reportDetails = std::string(UrkProject::kSdkReportDetails) + "\n";
    bool ok = false;
    try {
        if (options.backend == "unreal") {
            const std::string dump = UnrealSdkGenerator::TypeDumpPath(options.gameDir);
            std::error_code ec;
            if (fs::is_regular_file(dump, ec)) {
                const auto size = fs::file_size(dump, ec);
                log(LogLevel::Info, "Type dump: " + dump + " (" + SizeText(ec ? 0 : size) + ")");
            } else {
                log(LogLevel::Warning, "No type dump at " + dump +
                                           "; typed headers are skipped. Run the game once with [Unreal] DumpTypes=1.");
            }
            log(LogLevel::Info, "Writing sdk/unreal");
            ok = UnrealSdkGenerator::Generate(options.sdkOut, reportDetails, dump, &error);
            if (ok) {
                log(LogLevel::Info, "Writing project files");
                ok = UnrealSdkGenerator::GenerateModProject(options.projectOut, options.sdkOut, {}, options.projectName,
                                                            options.gameDir, options.modsDir,
                                                            options.enableLocalization, &error);
            }
        } else if (options.backend == "il2cpp") {
            log(LogLevel::Info, "Writing sdk/il2cpp");
            ok = Il2CppSdkGenerator::Generate(options.sdkOut, reportDetails, &error);
            if (ok) {
                log(LogLevel::Info, "Writing project files");
                ok = Il2CppSdkGenerator::GenerateModProject(options.projectOut, options.sdkOut, {}, options.projectName,
                                                            options.gameDir, options.modsDir,
                                                            options.enableLocalization, &error);
            }
        } else {
            log(LogLevel::Info, "Writing sdk/mono");
            ok = MonoSdkGenerator::Generate(options.sdkOut, reportDetails, &error);
            if (ok) {
                log(LogLevel::Info, "Writing project files");
                ok = MonoSdkGenerator::GenerateModProject(options.projectOut, options.sdkOut, {}, options.projectName,
                                                          options.gameDir, options.modsDir, options.enableLocalization,
                                                          &error);
            }
        }
    } catch (const std::exception &exception) {
        error = exception.what();
        ok = false;
    }
    if (!ok)
        return false;

    std::string baselineError;
    if (!UrkProject::RecordEditableBaselines(projectRoot, {}, &baselineError))
        log(LogLevel::Warning, "Editable-file ledger not written: " + baselineError);

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    log(LogLevel::Info, "Deploy folder: " + (fs::path(options.gameDir) / options.modsDir).string());
    log(LogLevel::Success, std::to_string(CountFiles(projectRoot)) + " files in the project, generated in " +
                               std::to_string(elapsed) + " ms");
    return true;
}

} // namespace UrkApp
