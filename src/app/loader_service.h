#pragma once

#include "activity_log.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace UrkApp {

enum class LoaderMode {
    None,
    Proxy,
    Injector,
};

// File names without extension, in the order they are offered.
inline constexpr const char *kProxyNames[] = {"version", "winhttp", "winmm", "dwmapi"};
inline constexpr const char *kInjectorFileName = "URKitInjector.dll";

const char *LoaderModeName(LoaderMode mode);
bool ParseLoaderMode(const std::string &text, LoaderMode *mode);
bool IsProxyName(const std::string &name);

std::filesystem::path ToolDirectory();
// A loader DLL shipped beside urk.exe.
std::optional<std::filesystem::path> BundledLoader(const std::string &fileName);
// True when the DLL carries the URKit runtime marker.
bool IsUrkitBinary(const std::filesystem::path &dll);

struct InstalledLoader {
    std::string fileName;
    bool urkit = false;
    // Same bytes as the loader beside urk.exe.
    bool matchesBundled = false;
};

// Proxy-named DLLs in the game folder, URKit or not.
std::vector<InstalledLoader> ScanGameDirectory(const std::filesystem::path &gameDir);

// Copies <proxy>.dll next to the game and removes any other URKit proxy there.
// A DLL of the same name that is not URKit is renamed to <name>.dll.urk-backup only with replaceForeign.
bool InstallProxy(const std::filesystem::path &gameDir, const std::string &proxyName, bool replaceForeign,
                  const LogSink &log, std::string &error);
// Removes URKit proxies from the game folder and restores any .urk-backup they replaced.
bool RemoveProxies(const std::filesystem::path &gameDir, const LogSink &log, std::string &error);

struct ProcessInfo {
    std::uint32_t pid = 0;
    std::wstring name;
    std::wstring imagePath;
    std::wstring windowTitle;
    bool x64 = false;
    bool urkitLoaded = false;
};

// Running x64 processes in this session. windowedOnly keeps processes that own a visible window.
std::vector<ProcessInfo> ListProcesses(bool windowedOnly);
std::optional<ProcessInfo> FindProcessByImage(const std::filesystem::path &imagePath);
bool UrkitRuntimeRunning(std::uint32_t pid);
bool InjectLoader(std::uint32_t pid, const LogSink &log, std::string &error);

} // namespace UrkApp
