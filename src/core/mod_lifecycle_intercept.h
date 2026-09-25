#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// Forced into loader.cpp to track mod DLL loads without changing the SDK ABI.
extern "C" HMODULE WINAPI URK_LoadLibraryExA(LPCSTR fileName, HANDLE file, DWORD flags);
extern "C" BOOL WINAPI URK_FreeLibrary(HMODULE module);

#define LoadLibraryExA URK_LoadLibraryExA
#define FreeLibrary URK_FreeLibrary
