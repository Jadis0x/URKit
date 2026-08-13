#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

// Forced into loader.cpp so native mod DLL loads are registered without
// widening the public SDK ABI or touching the loader's C ABI table.
extern "C" HMODULE WINAPI URK_LoadLibraryExA(LPCSTR fileName, HANDLE file, DWORD flags);
extern "C" BOOL WINAPI URK_FreeLibrary(HMODULE module);

#define LoadLibraryExA URK_LoadLibraryExA
#define FreeLibrary URK_FreeLibrary
