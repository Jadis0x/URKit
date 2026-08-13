#pragma once
#include <string>

namespace Intro {
void Show(const std::string &title, const std::string &subtitle, const std::string &game, const std::string &backend,
          int red, int green, int blue, int maximumVisibleMs);
void Status(const std::string &text);
void Progress(float value, const std::string &status = std::string());
void Backend(const std::string &backend);
void ModProgress(const std::string &modName, int discovered, int loaded, int failed, bool loading,
                 const std::string &finalStatus = std::string());
void Close();
} // namespace Intro
