#include "config.h"
#include "platform_paths.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <sstream>

namespace {

int BoolInt(bool value) {
    return value ? 1 : 0;
}

const std::string &LoaderSection() {
    static const std::string value = "Loader";
    return value;
}

const std::string &PublicConfigFilename() {
    static const std::string value = "URKit_config.ini";
    return value;
}

std::string PublicConfigPath() {
    return Platform_ExeDir() + PublicConfigFilename();
}

bool FileExists(const std::string &path) {
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

const char *RuntimeSelectionKey() {
    return "Backend";
}

std::string DefaultIni() {
    const Config defaults;
    std::ostringstream out;
    out << "; Generated automatically by URKit.\n"
        << "[Loader]\n"
        << "ShowConsole=" << BoolInt(defaults.showConsole) << "\n"
        << "SafeMode=" << BoolInt(defaults.safeMode) << "\n"
        << "InitDelayMs=" << defaults.initDelayMs << "\n"
        << RuntimeSelectionKey() << "=" << defaults.runtime << "\n"
        << "ModsDir=" << defaults.modsDir << "\n";
    return out.str();
}

void CreateDefaultIniIfMissing(const std::string &path, Config &config) {
    if (FileExists(path))
        return;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << DefaultIni();
    if (!output)
        config.warnings.push_back("URKit_config.ini was missing and could not be created.");
}

bool NormalizeChoice(std::string &value, const char *fallback, std::initializer_list<const char *> allowed) {
    value.erase(value.begin(),
                std::find_if(value.begin(), value.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool valid = std::any_of(allowed.begin(), allowed.end(), [&](const char *option) { return value == option; });
    if (!valid)
        value = fallback;
    return valid;
}

void ReadPublicConfig(const std::string &ini, Config &c) {
    const std::string &section = LoaderSection();
    char buf[2048];

    c.showConsole = GetPrivateProfileIntA(section.c_str(), "ShowConsole", c.showConsole, ini.c_str()) != 0;
    c.safeMode = GetPrivateProfileIntA(section.c_str(), "SafeMode", c.safeMode, ini.c_str()) != 0;
    c.initDelayMs = GetPrivateProfileIntA(section.c_str(), "InitDelayMs", c.initDelayMs, ini.c_str());
    GetPrivateProfileStringA(section.c_str(), RuntimeSelectionKey(), c.runtime.c_str(), buf, sizeof(buf), ini.c_str());
    c.runtime = buf;
    GetPrivateProfileStringA(section.c_str(), "ModsDir", c.modsDir.c_str(), buf, sizeof(buf), ini.c_str());
    c.modsDir = buf;
}

void NormalizeAndClamp(Config &c) {
    c.initDelayMs = std::clamp(c.initDelayMs, 0, 120000);
    if (!NormalizeChoice(c.runtime, "auto", {"auto", "mono", "il2cpp"}))
        c.warnings.push_back("Runtime selection was invalid; using auto.");
    if (c.modsDir.empty()) {
        c.modsDir = "Mods";
        c.warnings.push_back("ModsDir was empty; using Mods.");
    }
}

Config LoadConfigFromPath(const std::string &iniPath, bool createIfMissing) {
    Config c;
    c.configPath = iniPath;
    if (createIfMissing)
        CreateDefaultIniIfMissing(iniPath, c);
    ReadPublicConfig(iniPath, c);
    NormalizeAndClamp(c);
    return c;
}

} // namespace

Config Config_Load() {
    return LoadConfigFromPath(PublicConfigPath(), true);
}

Config Config_Load(const std::string &iniPath) {
    return LoadConfigFromPath(iniPath, false);
}
