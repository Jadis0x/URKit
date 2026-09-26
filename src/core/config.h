#pragma once
#include <string>
#include <vector>

struct Config {
    bool showConsole = false;
    bool safeMode = false;
    int initDelayMs = 0;
    std::string runtime = "auto";
    std::string modsDir = "Mods";
    // [Unreal] DumpTypes: write reflected classes for urk-sdk's typed headers.
    bool unrealDumpTypes = false;
    // Hooks also catch native functions Blueprint calls directly; off until proven in games.
    bool unrealNativeFromBlueprint = false;
    std::string configPath;
    std::vector<std::string> modPaths;
    std::vector<std::string> warnings;
};

Config Config_Load();
Config Config_Load(const std::string &iniPath);
