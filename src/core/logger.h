#pragma once

#include <string>

// Opens the log (logDirectory or next to the exe) and optionally a console.
void Log_Init(bool showConsole, const std::string &logDirectory = std::string());
void Log_Shutdown();

// printf-style; timestamped, written to console, file and debugger.
void Log(const char *fmt, ...);
