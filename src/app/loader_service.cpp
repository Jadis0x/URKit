#include "loader_service.h"

#include "project_ledger.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <thread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

namespace UrkApp {
namespace {

namespace fs = std::filesystem;

std::string Narrow(const std::wstring &value) {
    if (value.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string output(static_cast<size_t>(std::max(length, 0)), '\0');
    if (length > 0)
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length, nullptr,
                            nullptr);
    return output;
}

std::string WindowsError(DWORD code) {
    return std::system_category().message(static_cast<int>(code)) + " (" + std::to_string(code) + ")";
}

std::string Hex(std::uintptr_t value) {
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
    return buffer;
}

std::wstring LowerWide(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return value;
}

bool PathExists(const fs::path &path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

std::string ShortHash(const fs::path &path) {
    std::string hash;
    std::string ignored;
    return UrkProject::HashFileSha256(path, &hash, &ignored) ? hash.substr(0, 12) : std::string("?");
}

bool SameContent(const fs::path &left, const fs::path &right) {
    std::string a;
    std::string b;
    std::string ignored;
    return UrkProject::HashFileSha256(left, &a, &ignored) && UrkProject::HashFileSha256(right, &b, &ignored) && a == b;
}

class Handle {
  public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
    }
    HANDLE get() const { return value_; }
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }

  private:
    HANDLE value_ = nullptr;
};

bool IsProcessX64(HANDLE process) {
    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    using IsWow64Process2Fn = BOOL(WINAPI *)(HANDLE, USHORT *, USHORT *);
    static const auto isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2")));
    if (isWow64Process2 && isWow64Process2(process, &processMachine, &nativeMachine))
        return processMachine == IMAGE_FILE_MACHINE_UNKNOWN && nativeMachine == IMAGE_FILE_MACHINE_AMD64;
    BOOL wow64 = FALSE;
    return IsWow64Process(process, &wow64) && !wow64;
}

std::wstring ImagePath(HANDLE process) {
    std::wstring buffer(32768, L'\0');
    DWORD size = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &size))
        return {};
    buffer.resize(size);
    return buffer;
}

std::map<DWORD, std::wstring> VisibleWindowTitles() {
    std::map<DWORD, std::wstring> titles;
    EnumWindows(
        [](HWND window, LPARAM parameter) -> BOOL {
            if (!IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
                return TRUE;
            wchar_t title[256]{};
            if (GetWindowTextW(window, title, static_cast<int>(std::size(title))) <= 0)
                return TRUE;
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            auto &map = *reinterpret_cast<std::map<DWORD, std::wstring> *>(parameter);
            if (!map.contains(pid))
                map.emplace(pid, title);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&titles));
    return titles;
}

std::optional<std::uintptr_t> FindModuleBase(DWORD pid, const std::wstring &moduleName) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!snapshot)
        return std::nullopt;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    const std::wstring wanted = LowerWide(moduleName);
    for (BOOL more = Module32FirstW(snapshot.get(), &entry); more; more = Module32NextW(snapshot.get(), &entry)) {
        if (LowerWide(entry.szModule) == wanted)
            return reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
    }
    return std::nullopt;
}

} // namespace

const char *LoaderModeName(LoaderMode mode) {
    switch (mode) {
        case LoaderMode::Proxy:
            return "proxy";
        case LoaderMode::Injector:
            return "injector";
        case LoaderMode::None:
            break;
    }
    return "none";
}

bool ParseLoaderMode(const std::string &text, LoaderMode *mode) {
    if (text == "proxy")
        *mode = LoaderMode::Proxy;
    else if (text == "injector")
        *mode = LoaderMode::Injector;
    else if (text == "none")
        *mode = LoaderMode::None;
    else
        return false;
    return true;
}

bool IsProxyName(const std::string &name) {
    return std::any_of(std::begin(kProxyNames), std::end(kProxyNames),
                       [&](const char *candidate) { return name == candidate; });
}

fs::path ToolDirectory() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer).parent_path();
}

std::optional<fs::path> BundledLoader(const std::string &fileName) {
    const fs::path path = ToolDirectory() / fileName;
    std::error_code ec;
    if (fs::is_regular_file(path, ec) && !ec)
        return path;
    return std::nullopt;
}

bool IsUrkitBinary(const fs::path &dll) {
    static constexpr std::wstring_view kMarker = L"URKit.RuntimeOwner.";
    std::ifstream input(dll, std::ios::binary);
    if (!input)
        return false;
    const char *marker = reinterpret_cast<const char *>(kMarker.data());
    const size_t markerBytes = kMarker.size() * sizeof(wchar_t);
    std::vector<char> buffer(1024 * 1024 + markerBytes);
    size_t carried = 0;
    while (input) {
        input.read(buffer.data() + carried, static_cast<std::streamsize>(buffer.size() - carried));
        const size_t total = carried + static_cast<size_t>(input.gcount());
        if (std::search(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(total), marker,
                        marker + markerBytes) != buffer.begin() + static_cast<std::ptrdiff_t>(total))
            return true;
        carried = std::min(total, markerBytes - 1);
        std::copy(buffer.begin() + static_cast<std::ptrdiff_t>(total - carried),
                  buffer.begin() + static_cast<std::ptrdiff_t>(total), buffer.begin());
    }
    return false;
}

std::vector<InstalledLoader> ScanGameDirectory(const fs::path &gameDir) {
    std::vector<InstalledLoader> loaders;
    for (const char *name : kProxyNames) {
        const std::string fileName = std::string(name) + ".dll";
        const fs::path path = gameDir / fileName;
        if (!PathExists(path))
            continue;
        InstalledLoader loader;
        loader.fileName = fileName;
        loader.urkit = IsUrkitBinary(path);
        if (loader.urkit)
            if (const std::optional<fs::path> bundled = BundledLoader(fileName))
                loader.matchesBundled = SameContent(path, *bundled);
        loaders.push_back(std::move(loader));
    }
    return loaders;
}

bool InstallProxy(const fs::path &gameDir, const std::string &proxyName, bool replaceForeign, const LogSink &log,
                  std::string &error) {
    if (!IsProxyName(proxyName)) {
        error = "Unknown proxy: " + proxyName;
        return false;
    }
    const std::string fileName = proxyName + ".dll";
    const std::optional<fs::path> bundled = BundledLoader(fileName);
    if (!bundled) {
        error = fileName + " is not next to urk.exe in " + ToolDirectory().string() +
                ". Extract the whole URKit release into one folder.";
        return false;
    }
    if (!IsUrkitBinary(*bundled)) {
        error = bundled->string() + " is not a URKit loader. Extract the URKit release again.";
        return false;
    }
    std::error_code ec;
    if (!fs::is_directory(gameDir, ec)) {
        error = "Game folder not found: " + gameDir.string();
        return false;
    }

    const fs::path target = gameDir / fileName;
    log(LogLevel::Info, "Source: " + bundled->string() + " (sha256 " + ShortHash(*bundled) + ")");
    if (PathExists(target)) {
        if (IsUrkitBinary(target)) {
            if (SameContent(target, *bundled)) {
                log(LogLevel::Info, fileName + " is already installed and matches this release");
            } else {
                log(LogLevel::Info, "Replacing URKit " + fileName + " (sha256 " + ShortHash(target) + ")");
            }
        } else if (!replaceForeign) {
            error = fileName + " in the game folder is not URKit. Another mod loader or the game may need it.";
            return false;
        } else {
            fs::path backup = target;
            backup += ".urk-backup";
            if (PathExists(backup)) {
                error = backup.filename().string() + " already exists; restore or delete it first.";
                return false;
            }
            fs::rename(target, backup, ec);
            if (ec) {
                error = "Cannot rename " + target.string() + ": " + ec.message();
                return false;
            }
            log(LogLevel::Warning, "Kept the existing " + fileName + " as " + backup.filename().string());
        }
    }

    if (!PathExists(target) || !SameContent(target, *bundled)) {
        fs::path staged = target;
        staged += ".urk-new";
        fs::copy_file(*bundled, staged, fs::copy_options::overwrite_existing, ec);
        if (!ec)
            fs::rename(staged, target, ec);
        if (ec) {
            std::error_code cleanup;
            fs::remove(staged, cleanup);
            error = "Cannot write " + target.string() + ": " + ec.message() +
                    (ec == std::errc::permission_denied ? ". Close the game if it is running." : "");
            return false;
        }
        log(LogLevel::Info, "Copied " + fileName + " to " + gameDir.string());
    }

    for (const char *other : kProxyNames) {
        if (proxyName == other)
            continue;
        const fs::path otherPath = gameDir / (std::string(other) + ".dll");
        if (!PathExists(otherPath) || !IsUrkitBinary(otherPath))
            continue;
        fs::remove(otherPath, ec);
        if (ec)
            log(LogLevel::Warning, "Could not remove the other URKit loader " + otherPath.filename().string() + ": " +
                                       ec.message());
        else
            log(LogLevel::Info, "Removed the other URKit loader " + otherPath.filename().string());
    }

    const fs::path mods = gameDir / "Mods";
    if (!PathExists(mods)) {
        fs::create_directories(mods, ec);
        if (!ec)
            log(LogLevel::Info, "Created " + mods.string());
    }
    log(LogLevel::Success, fileName + " installed. Start the game and check URKit_logs.log beside its executable.");
    return true;
}

bool RemoveProxies(const fs::path &gameDir, const LogSink &log, std::string &error) {
    std::size_t removed = 0;
    for (const char *name : kProxyNames) {
        const std::string fileName = std::string(name) + ".dll";
        const fs::path path = gameDir / fileName;
        std::error_code ec;
        if (PathExists(path) && IsUrkitBinary(path)) {
            fs::remove(path, ec);
            if (ec) {
                error = "Cannot remove " + path.string() + ": " + ec.message() +
                        (ec == std::errc::permission_denied ? ". Close the game if it is running." : "");
                return false;
            }
            ++removed;
            log(LogLevel::Info, "Removed " + fileName);
        }
        fs::path backup = path;
        backup += ".urk-backup";
        if (PathExists(backup) && !PathExists(path)) {
            fs::rename(backup, path, ec);
            if (ec)
                log(LogLevel::Warning, "Could not restore " + fileName + " from its backup: " + ec.message());
            else
                log(LogLevel::Info, "Restored the original " + fileName);
        }
    }
    if (removed == 0)
        log(LogLevel::Info, "No URKit proxy in " + gameDir.string());
    else
        log(LogLevel::Success, "URKit proxy removed from " + gameDir.string() + ". Mods were left in place.");
    return true;
}

bool UrkitRuntimeRunning(std::uint32_t pid) {
    wchar_t name[96]{};
    swprintf_s(name, L"Local\\URKit.RuntimeOwner.%lu", static_cast<unsigned long>(pid));
    Handle marker(OpenMutexW(SYNCHRONIZE, FALSE, name));
    return static_cast<bool>(marker);
}

std::vector<ProcessInfo> ListProcesses(bool windowedOnly) {
    std::vector<ProcessInfo> processes;
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot)
        return processes;
    const std::map<DWORD, std::wstring> titles = VisibleWindowTitles();
    const DWORD self = GetCurrentProcessId();
    DWORD sessionId = 0;
    ProcessIdToSessionId(self, &sessionId);

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot.get(), &entry); more; more = Process32NextW(snapshot.get(), &entry)) {
        if (entry.th32ProcessID <= 4 || entry.th32ProcessID == self)
            continue;
        DWORD processSession = 0;
        if (!ProcessIdToSessionId(entry.th32ProcessID, &processSession) || processSession != sessionId)
            continue;
        const auto title = titles.find(entry.th32ProcessID);
        if (windowedOnly && title == titles.end())
            continue;
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID));
        if (!process)
            continue;
        ProcessInfo info;
        info.pid = entry.th32ProcessID;
        info.name = entry.szExeFile;
        info.imagePath = ImagePath(process.get());
        info.x64 = IsProcessX64(process.get());
        if (!info.x64)
            continue;
        if (title != titles.end())
            info.windowTitle = title->second;
        info.urkitLoaded = UrkitRuntimeRunning(info.pid);
        processes.push_back(std::move(info));
    }
    std::sort(processes.begin(), processes.end(), [](const ProcessInfo &left, const ProcessInfo &right) {
        const std::wstring a = LowerWide(left.name);
        const std::wstring b = LowerWide(right.name);
        return a == b ? left.pid < right.pid : a < b;
    });
    return processes;
}

std::optional<ProcessInfo> FindProcessByImage(const fs::path &imagePath) {
    std::error_code ec;
    const std::wstring wanted = LowerWide(fs::absolute(imagePath, ec).lexically_normal().wstring());
    for (ProcessInfo &process : ListProcesses(false))
        if (!process.imagePath.empty() && LowerWide(fs::path(process.imagePath).lexically_normal().wstring()) == wanted)
            return std::move(process);
    return std::nullopt;
}

bool InjectLoader(std::uint32_t pid, const LogSink &log, std::string &error) {
    const std::optional<fs::path> dll = BundledLoader(kInjectorFileName);
    if (!dll) {
        error = std::string(kInjectorFileName) + " is not next to urk.exe in " + ToolDirectory().string() + ".";
        return false;
    }
    if (UrkitRuntimeRunning(pid)) {
        error = "URKit already runs in process " + std::to_string(pid) + " (a proxy or an earlier injection).";
        return false;
    }

    constexpr DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                             PROCESS_VM_WRITE | PROCESS_VM_READ | SYNCHRONIZE;
    Handle process(OpenProcess(access, FALSE, pid));
    if (!process) {
        const DWORD code = GetLastError();
        error = "Cannot open process " + std::to_string(pid) + ": " + WindowsError(code);
        if (code == ERROR_ACCESS_DENIED)
            error += ". If the game runs as administrator, run urk.exe as administrator too.";
        return false;
    }
    const std::wstring image = ImagePath(process.get());
    log(LogLevel::Info, "Process " + std::to_string(pid) + ": " + Narrow(image));
    if (!IsProcessX64(process.get())) {
        error = "Process " + std::to_string(pid) + " is not a 64-bit process.";
        return false;
    }
    if (FindModuleBase(pid, L"URKitInjector.dll")) {
        error = std::string(kInjectorFileName) + " is already loaded in process " + std::to_string(pid) + ".";
        return false;
    }

    const std::wstring path = dll->wstring();
    const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
    void *remote = VirtualAllocEx(process.get(), nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        error = "VirtualAllocEx failed: " + WindowsError(GetLastError());
        return false;
    }
    struct RemoteFree {
        HANDLE process;
        void *address;
        ~RemoteFree() { VirtualFreeEx(process, address, 0, MEM_RELEASE); }
    } freeRemote{process.get(), remote};

    SIZE_T written = 0;
    if (!WriteProcessMemory(process.get(), remote, path.c_str(), bytes, &written) || written != bytes) {
        error = "WriteProcessMemory failed: " + WindowsError(GetLastError());
        return false;
    }
    log(LogLevel::Debug, "Loader path written at " + Hex(reinterpret_cast<std::uintptr_t>(remote)) + " (" +
                             std::to_string(bytes) + " bytes)");

    // kernel32 is mapped at the same address in every process of a session.
    const auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW")));
    if (!loadLibrary) {
        error = "LoadLibraryW not found in kernel32.dll";
        return false;
    }
    Handle thread(CreateRemoteThread(process.get(), nullptr, 0, loadLibrary, remote, 0, nullptr));
    if (!thread) {
        error = "CreateRemoteThread failed: " + WindowsError(GetLastError());
        return false;
    }
    log(LogLevel::Debug, "Remote LoadLibraryW thread " + std::to_string(GetThreadId(thread.get())) + " started");
    const DWORD wait = WaitForSingleObject(thread.get(), 15000);
    if (wait != WAIT_OBJECT_0) {
        error = "LoadLibraryW did not return within 15 s; the process may be suspended or under a debugger.";
        freeRemote.address = nullptr;
        return false;
    }
    DWORD exitCode = 0;
    GetExitCodeThread(thread.get(), &exitCode);
    const std::optional<std::uintptr_t> base = FindModuleBase(pid, L"URKitInjector.dll");
    if (!base) {
        error = "LoadLibraryW returned " + Hex(exitCode) + "; " + std::string(kInjectorFileName) +
                " is not loaded. Anti-cheat or a missing dependency can block it.";
        return false;
    }
    log(LogLevel::Info, std::string(kInjectorFileName) + " mapped at " + Hex(*base));

    for (int attempt = 0; attempt < 20 && !UrkitRuntimeRunning(pid); ++attempt)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!UrkitRuntimeRunning(pid)) {
        log(LogLevel::Warning, "The loader is mapped but did not claim the process. It only starts in Unity or "
                               "Unreal game processes; see URKit_logs.log.");
        return true;
    }
    log(LogLevel::Success, "Loader started in process " + std::to_string(pid) +
                               ". It asks for the config and mod DLLs inside the game.");
    return true;
}

} // namespace UrkApp
