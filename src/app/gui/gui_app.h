#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace UrkGui {

// openPath: a game .exe or a project folder dropped on urk.exe in Explorer; may be empty.
int RunGui(HINSTANCE instance, int show, const std::wstring &openPath);

} // namespace UrkGui
