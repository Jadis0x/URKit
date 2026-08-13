#pragma once

#include <string>

// Initializes logging. If showConsole is true, allocates a console window.
// When logDirectory is empty, opens URKit_logs.log next to the game executable.
// Injected sessions pass an external directory so the game folder stays untouched.
void Log_Init(bool showConsole, const std::string &logDirectory = std::string());
void Log_Shutdown();

// printf-style, timestamped, process/thread tagged, written to console (if any)
// + file + debugger.
void Log(const char *fmt, ...);
