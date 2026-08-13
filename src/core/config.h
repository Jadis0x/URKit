#pragma once
#include <string>
#include <vector>

struct Config {
    bool showConsole = false;
    bool safeMode = false;
    int initDelayMs = 0;
    std::string runtime = "auto";
    std::string modsDir = "Mods";
    std::string configPath;
    std::vector<std::string> modPaths;
    std::vector<std::string> warnings;
};

Config Config_Load();
Config Config_Load(const std::string &iniPath);
