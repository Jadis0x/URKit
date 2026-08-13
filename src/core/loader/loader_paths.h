#pragma once

#include "config.h"

#include <string>

std::string Loader_ExeDir();
std::string Loader_ModsDir(const Config &config);
std::string Loader_GameName();
std::string Loader_WindowsError(unsigned long error);
