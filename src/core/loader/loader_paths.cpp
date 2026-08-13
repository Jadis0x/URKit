#include "loader_paths.h"
#include "logger.h"
#include "platform_paths.h"

#include <filesystem>
#include <string>

std::string Loader_ExeDir() {
    return Platform_ExeDir();
}

std::string Loader_WindowsError(unsigned long error) {
    return Platform_WindowsError(error);
}

std::string Loader_ModsDir(const Config &config) {
    std::filesystem::path configured(config.modsDir);
    std::filesystem::path directory =
        configured.is_absolute() ? configured : std::filesystem::path(Platform_ExeDir()) / configured;

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        Log("[mods] Failed to create mod directory %s: %s", directory.string().c_str(), error.message().c_str());
    }

    std::string result = directory.string();
    if (!result.empty() && result.back() != '\\' && result.back() != '/')
        result += '\\';
    return result;
}

std::string Loader_GameName() {
    return Platform_GameName();
}
