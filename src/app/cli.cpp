#include "cli.h"

#include "activity_log.h"
#include "generator_service.h"
#include "loader_service.h"
#include "mcp_server.h"
#include "project_updater.h"
#include "project_version.h"
#include "self_update.h"
#include "text_util.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <limits>
#include <map>
#include <thread>

#include <io.h>

namespace UrkApp {
namespace {

namespace fs = std::filesystem;

constexpr int kExitOk = 0;
constexpr int kExitFailed = 1;
constexpr int kExitUsage = 2;
// Update check found work, or the update was staged for a manual merge.
constexpr int kExitPending = 10;

constexpr const char *kUsage =
    "URKit %s\n"
    "\n"
    "  urk.exe                               open the window\n"
    "  urk.exe new --game-exe <path> [--backend auto|mono|il2cpp|unreal] [--name <name>]\n"
    "              [--localization] [--loader none|proxy|injector] [--proxy version|winhttp|winmm|dwmapi]\n"
    "  urk.exe update --project <dir> [--check | --apply [--overwrite-conflicts] | --stage-sdk]\n"
    "  urk.exe loader status  --game-exe <path>\n"
    "  urk.exe loader install --game-exe <path> [--proxy version|winhttp|winmm|dwmapi] [--replace-foreign]\n"
    "  urk.exe loader remove  --game-exe <path>\n"
    "  urk.exe loader inject  (--pid <pid> | --process <name.exe>) [--wait]\n"
    "  urk.exe self-update [--check]\n"
    "  urk.exe mcp [--project <dir>] [--game-pid <pid>]\n"
    "  urk.exe version\n"
    "\n"
    "Exit codes: 0 done, 1 failed, 2 bad arguments, 10 update available or staged.\n";

bool HandleUsable(DWORD which) {
    HANDLE handle = GetStdHandle(which);
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

// urk.exe is a GUI-subsystem program; text goes to the parent console or to redirected handles.
void BindConsole() {
    const bool out = HandleUsable(STD_OUTPUT_HANDLE);
    const bool err = HandleUsable(STD_ERROR_HANDLE);
    if (!(out && err) && AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *stream = nullptr;
        if (!out)
            freopen_s(&stream, "CONOUT$", "w", stdout);
        if (!err)
            freopen_s(&stream, "CONOUT$", "w", stderr);
        // The shell already printed its prompt.
        std::fputs("\n", stdout);
    }
    SetConsoleOutputCP(CP_UTF8);
}

void PrintEntry(const LogEntry &entry) {
    FILE *stream = entry.level == LogLevel::Error || entry.level == LogLevel::Warning ? stderr : stdout;
    if (entry.level == LogLevel::Debug)
        return;
    std::fprintf(stream, "%-5s  %s\n", LogLevelName(entry.level), entry.message.c_str());
    std::fflush(stream);
}

struct Arguments {
    std::vector<std::string> positional;
    std::map<std::string, std::string> values;
    std::vector<std::string> flags;
    std::string error;

    bool Has(const std::string &flag) const { return std::find(flags.begin(), flags.end(), flag) != flags.end(); }
    std::string Value(const std::string &key, const std::string &fallback = {}) const {
        const auto found = values.find(key);
        return found == values.end() ? fallback : found->second;
    }
};

// Options listed in valueOptions take the next argument; everything else starting with - is a flag.
Arguments Parse(const std::vector<std::wstring> &raw, size_t start, std::initializer_list<const char *> valueOptions,
                std::initializer_list<const char *> flagOptions,
                const std::map<std::string, std::string> &aliases = {}) {
    Arguments parsed;
    for (size_t index = start; index < raw.size(); ++index) {
        std::string argument = Utf8(raw[index]);
        if (const auto alias = aliases.find(argument); alias != aliases.end())
            argument = alias->second;
        if (argument.empty() || argument.front() != '-') {
            parsed.positional.push_back(argument);
            continue;
        }
        if (std::find_if(valueOptions.begin(), valueOptions.end(),
                         [&](const char *option) { return argument == option; }) != valueOptions.end()) {
            if (index + 1 >= raw.size()) {
                parsed.error = "missing value for " + argument;
                return parsed;
            }
            parsed.values[argument] = Utf8(raw[++index]);
            continue;
        }
        if (std::find_if(flagOptions.begin(), flagOptions.end(),
                         [&](const char *option) { return argument == option; }) != flagOptions.end()) {
            parsed.flags.push_back(argument);
            continue;
        }
        parsed.error = "unknown option " + argument;
        return parsed;
    }
    return parsed;
}

int UsageError(const std::string &message) {
    std::fprintf(stderr, "error: %s\n\n", message.c_str());
    std::fprintf(stderr, kUsage, std::string(UrkVersion::kReleaseVersion).c_str());
    return kExitUsage;
}

bool ParsePid(const std::string &text, std::uint32_t *pid) {
    errno = 0;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 10);
    if (errno == ERANGE || !end || *end != '\0' || value == 0 || value > (std::numeric_limits<std::uint32_t>::max)())
        return false;
    *pid = static_cast<std::uint32_t>(value);
    return true;
}

int RunNew(const std::vector<std::wstring> &raw, size_t start) {
    const Arguments args = Parse(raw, start, {"--game-exe", "--backend", "--name", "--loader", "--proxy"},
                                 {"--localization", "--replace-foreign"},
                                 {{"-g", "--game-exe"}, {"--exe", "--game-exe"}, {"-b", "--backend"},
                                  {"-n", "--name"}, {"--project-name", "--name"}});
    if (!args.error.empty())
        return UsageError(args.error);
    if (!args.positional.empty())
        return UsageError("unexpected argument " + args.positional.front());

    ProjectOptions options;
    options.gameExePath = args.Value("--game-exe");
    options.backendSelection = args.Value("--backend", "auto");
    options.projectName = args.Value("--name", "GeneratedMod");
    options.enableLocalization = args.Has("--localization");
    LoaderMode loader = LoaderMode::None;
    if (!ParseLoaderMode(args.Value("--loader", "none"), &loader))
        return UsageError("--loader must be none, proxy or injector");
    const std::string proxy = args.Value("--proxy", "version");
    if (!IsProxyName(proxy))
        return UsageError("--proxy must be version, winhttp, winmm or dwmapi");
    if (options.gameExePath.empty())
        return UsageError("--game-exe is required");

    const LogSink log = ActivityLog::Instance().SinkFor("generate");
    std::string error;
    if (!ResolveProjectOptions(options, error) || !GenerateProject(options, log, error)) {
        log(LogLevel::Error, error);
        return kExitFailed;
    }
    if (loader == LoaderMode::Proxy) {
        const LogSink loaderLog = ActivityLog::Instance().SinkFor("loader");
        if (!InstallProxy(fs::path(options.gameDir), proxy, args.Has("--replace-foreign"), loaderLog, error)) {
            loaderLog(LogLevel::Error, error);
            return kExitFailed;
        }
    } else if (loader == LoaderMode::Injector) {
        log(LogLevel::Info, "Injector mode: start the game, then run urk.exe loader inject --process " +
                                fs::path(options.gameExePath).filename().string());
    }
    return kExitOk;
}

int RunUpdate(const std::vector<std::wstring> &raw, size_t start) {
    const Arguments args = Parse(raw, start, {"--project"}, {"--check", "--apply", "--update", "--overwrite-conflicts",
                                                            "--stage-sdk"},
                                 {{"-p", "--project"}});
    if (!args.error.empty())
        return UsageError(args.error);
    std::string project = args.Value("--project");
    if (project.empty() && args.positional.size() == 1)
        project = args.positional.front();
    else if (!args.positional.empty())
        return UsageError("unexpected argument " + args.positional.front());
    if (project.empty())
        return UsageError("--project is required");
    const bool apply = args.Has("--apply") || args.Has("--update");
    if (args.Has("--stage-sdk") && apply)
        return UsageError("--stage-sdk cannot be combined with --apply");

    const fs::path root = fs::path(Wide(project));
    const LogSink log = ActivityLog::Instance().SinkFor("update");
    const UrkProject::ProgressSink progress = [&](const std::string &line) { log(LogLevel::Info, line); };
    std::string error;
    try {
        if (args.Has("--stage-sdk")) {
            fs::path staged;
            if (!UrkProject::StageSdkMigration(root, &staged, &error)) {
                log(LogLevel::Error, error);
                return kExitFailed;
            }
            log(LogLevel::Success, "Project unchanged. SDK candidate: " + staged.string());
            return kExitOk;
        }
        if (!apply) {
            UrkProject::UpdatePreview preview;
            if (!UrkProject::PreviewUpdate(root, &preview, &error, progress)) {
                log(LogLevel::Error, error);
                return kExitFailed;
            }
            std::printf("\n%s\n\n%s\n", UrkProject::Describe(preview.inspection).c_str(),
                        UrkProject::DescribeChanges(preview.changes).c_str());
            return preview.inspection.updateAvailable ? kExitPending : kExitOk;
        }
        UrkProject::ApplyOptions options;
        options.overwriteConflicts = args.Has("--overwrite-conflicts");
        UrkProject::UpdateResult result;
        if (!UrkProject::Update(root, options, &result, &error, progress)) {
            log(LogLevel::Error, error);
            return kExitFailed;
        }
        if (result.staged) {
            log(LogLevel::Warning, std::to_string(result.conflicts.size()) +
                                       " generated files were edited; nothing changed. Candidate: " +
                                       result.stagedUpdateDirectory.string());
            return kExitPending;
        }
        if (!result.updated) {
            log(LogLevel::Success, "Already up to date");
            return kExitOk;
        }
        log(LogLevel::Success, std::to_string(result.filesWritten) + " written, " +
                                   std::to_string(result.filesRemoved) + " removed. Backup: " +
                                   result.backupDirectory.string());
        return kExitOk;
    } catch (const std::exception &exception) {
        log(LogLevel::Error, exception.what());
        return kExitFailed;
    }
}

int RunLoader(const std::vector<std::wstring> &raw, size_t start) {
    if (start >= raw.size())
        return UsageError("loader needs status, install, remove or inject");
    const std::string verb = Utf8(raw[start]);
    const Arguments args = Parse(raw, start + 1, {"--game-exe", "--proxy", "--pid", "--process"},
                                 {"--replace-foreign", "--wait"}, {{"-g", "--game-exe"}});
    if (!args.error.empty())
        return UsageError(args.error);
    const LogSink log = ActivityLog::Instance().SinkFor("loader");
    std::string error;

    if (verb == "inject") {
        std::uint32_t pid = 0;
        if (args.values.contains("--pid")) {
            if (!ParsePid(args.Value("--pid"), &pid))
                return UsageError("--pid must be a positive number");
        } else if (args.values.contains("--process")) {
            const std::wstring wanted = Wide(args.Value("--process"));
            for (int attempt = 0; pid == 0; ++attempt) {
                for (const ProcessInfo &process : ListProcesses(false))
                    if (_wcsicmp(process.name.c_str(), wanted.c_str()) == 0) {
                        pid = process.pid;
                        break;
                    }
                if (pid != 0 || !args.Has("--wait"))
                    break;
                if (attempt == 0)
                    log(LogLevel::Info, "Waiting for " + args.Value("--process") + " to start");
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            if (pid == 0) {
                log(LogLevel::Error, "No running x64 process named " + args.Value("--process"));
                return kExitFailed;
            }
            if (args.Has("--wait"))
                std::this_thread::sleep_for(std::chrono::seconds(2));
        } else {
            return UsageError("inject needs --pid or --process");
        }
        if (!InjectLoader(pid, log, error)) {
            log(LogLevel::Error, error);
            return kExitFailed;
        }
        return kExitOk;
    }

    const std::string gameExe = args.Value("--game-exe");
    if (gameExe.empty())
        return UsageError("--game-exe is required");
    const fs::path gameDir = fs::path(Wide(gameExe)).parent_path();
    if (verb == "status") {
        const std::vector<InstalledLoader> loaders = ScanGameDirectory(gameDir);
        if (loaders.empty())
            std::printf("No proxy DLL in %s\n", gameDir.string().c_str());
        for (const InstalledLoader &loader : loaders)
            std::printf("%-12s %s\n", loader.fileName.c_str(),
                        !loader.urkit           ? "not URKit"
                        : loader.matchesBundled ? "URKit, same build as this urk.exe"
                                                : "URKit, different build");
        return kExitOk;
    }
    if (verb == "install") {
        const std::string proxy = args.Value("--proxy", "version");
        if (!IsProxyName(proxy))
            return UsageError("--proxy must be version, winhttp, winmm or dwmapi");
        if (!InstallProxy(gameDir, proxy, args.Has("--replace-foreign"), log, error)) {
            log(LogLevel::Error, error);
            return kExitFailed;
        }
        return kExitOk;
    }
    if (verb == "remove") {
        if (!RemoveProxies(gameDir, log, error)) {
            log(LogLevel::Error, error);
            return kExitFailed;
        }
        return kExitOk;
    }
    return UsageError("unknown loader command " + verb);
}

int RunSelfUpdate(const std::vector<std::wstring> &raw, size_t start) {
    const Arguments args = Parse(raw, start, {}, {"--check"});
    if (!args.error.empty())
        return UsageError(args.error);
    const LogSink log = ActivityLog::Instance().SinkFor("release");
    UpdateCheckResult check;
    AvailableUpdate available;
    std::string error;
    if (!CheckForUpdate(&check, &available, &error)) {
        log(LogLevel::Error, "Release check failed: " + error);
        return kExitFailed;
    }
    log(LogLevel::Info, "Installed " + check.currentVersion + ", latest " + check.latestVersion);
    if (!check.updateAvailable) {
        log(LogLevel::Success, "urk.exe is up to date");
        return kExitOk;
    }
    if (args.Has("--check")) {
        log(LogLevel::Info, "Release notes: " + check.releaseUrl);
        return kExitPending;
    }
    if (!DownloadAndRestart(available, log, false, &error)) {
        log(LogLevel::Error, error);
        return kExitFailed;
    }
    return kExitOk;
}

int RunMcp(const std::vector<std::wstring> &raw, size_t start) {
    fs::path projectRoot = fs::current_path();
    std::optional<std::uint32_t> gamePid;
    for (size_t index = start; index < raw.size(); ++index) {
        const std::wstring_view argument = raw[index];
        if (argument == L"--project" && index + 1 < raw.size()) {
            projectRoot = fs::path(raw[++index]);
            continue;
        }
        if (argument == L"--game-pid" && index + 1 < raw.size()) {
            std::uint32_t pid = 0;
            if (!ParsePid(Utf8(raw[++index]), &pid)) {
                std::fputs("--game-pid requires a positive integer\n", stderr);
                return kExitUsage;
            }
            gamePid = pid;
            continue;
        }
        std::fprintf(stderr, "Unknown or incomplete argument: %s\n", Utf8(argument).c_str());
        return kExitUsage;
    }
    std::fprintf(stderr,
                 "URKit Development MCP %s\nProject: %s\nTarget : %s\nTransport: stdio\n",
                 std::string(UrkVersion::kReleaseVersion).c_str(), projectRoot.string().c_str(),
                 gamePid ? ("game PID " + std::to_string(*gamePid)).c_str() : "automatic bridge discovery");
    std::fflush(stderr);
    URK::DevMcp::McpServer server(std::move(projectRoot), gamePid);
    return server.Run();
}

bool IsLegacyNewOption(const std::string &argument) {
    return argument == "--game-exe" || argument == "-g" || argument == "--exe" || argument == "--backend" ||
           argument == "-b" || argument == "--name" || argument == "-n" || argument == "--project-name" ||
           argument == "--localization";
}

} // namespace

std::optional<int> RunCommandLine(const std::vector<std::wstring> &arguments) {
    if (arguments.empty())
        return std::nullopt;
    const std::string verb = Utf8(arguments.front());

    // MCP owns stdin/stdout; nothing else may write to them.
    if (verb == "mcp") {
        if (!HandleUsable(STD_INPUT_HANDLE) || !HandleUsable(STD_OUTPUT_HANDLE)) {
            BindConsole();
            std::fputs("urk.exe mcp talks MCP over stdin/stdout. Register it in an MCP client; see docs/DEV_MCP.md.\n",
                       stderr);
            return kExitUsage;
        }
        return RunMcp(arguments, 1);
    }

    BindConsole();
    ActivityLog::Instance().SetListener(PrintEntry);
    if (verb == "help" || verb == "--help" || verb == "-h" || verb == "/?") {
        std::printf(kUsage, std::string(UrkVersion::kReleaseVersion).c_str());
        return kExitOk;
    }
    if (verb == "version" || verb == "--version") {
        std::printf("urk.exe %s\n", std::string(UrkVersion::kReleaseVersion).c_str());
        return kExitOk;
    }
    if (verb == "new")
        return RunNew(arguments, 1);
    if (verb == "update")
        return RunUpdate(arguments, 1);
    if (verb == "loader")
        return RunLoader(arguments, 1);
    if (verb == "self-update")
        return RunSelfUpdate(arguments, 1);
    // Forms used by urk-sdk.exe and urk-updater.exe, kept for existing scripts.
    if (verb == "--check-updater") {
        const std::vector<std::wstring> check{L"self-update", L"--check"};
        return RunSelfUpdate(check, 1);
    }
    if (IsLegacyNewOption(verb))
        return RunNew(arguments, 0);
    if (verb == "--project" || verb == "-p")
        return RunUpdate(arguments, 0);
    return UsageError("unknown command " + verb);
}

} // namespace UrkApp
