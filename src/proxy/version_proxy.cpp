#include <windows.h>

namespace {

HMODULE SystemVersionModule() {
    static HMODULE module = [] {
        wchar_t systemDirectory[MAX_PATH] = {};
        UINT length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (!length || length >= MAX_PATH - 12)
            return static_cast<HMODULE>(nullptr);
        lstrcatW(systemDirectory, L"\\version.dll");
        return LoadLibraryW(systemDirectory);
    }();
    return module;
}

template <typename T> T Resolve(const char *name) {
    HMODULE module = SystemVersionModule();
    return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
}

} // namespace

extern "C" {

BOOL WINAPI Proxy_GetFileVersionInfoA(LPCSTR file, DWORD handle, DWORD length, LPVOID data) {
    using Function = BOOL(WINAPI *)(LPCSTR, DWORD, DWORD, LPVOID);
    static Function function = Resolve<Function>("GetFileVersionInfoA");
    return function ? function(file, handle, length, data) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoW(LPCWSTR file, DWORD handle, DWORD length, LPVOID data) {
    using Function = BOOL(WINAPI *)(LPCWSTR, DWORD, DWORD, LPVOID);
    static Function function = Resolve<Function>("GetFileVersionInfoW");
    return function ? function(file, handle, length, data) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoExA(DWORD flags, LPCSTR file, DWORD handle, DWORD length, LPVOID data) {
    using Function = BOOL(WINAPI *)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    static Function function = Resolve<Function>("GetFileVersionInfoExA");
    return function ? function(flags, file, handle, length, data) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoExW(DWORD flags, LPCWSTR file, DWORD handle, DWORD length, LPVOID data) {
    using Function = BOOL(WINAPI *)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    static Function function = Resolve<Function>("GetFileVersionInfoExW");
    return function ? function(flags, file, handle, length, data) : FALSE;
}

BOOL WINAPI Proxy_GetFileVersionInfoByHandle(DWORD flags, HANDLE file, LPVOID *data, PDWORD length) {
    using Function = BOOL(WINAPI *)(DWORD, HANDLE, LPVOID *, PDWORD);
    static Function function = Resolve<Function>("GetFileVersionInfoByHandle");
    return function ? function(flags, file, data, length) : FALSE;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeA(LPCSTR file, LPDWORD handle) {
    using Function = DWORD(WINAPI *)(LPCSTR, LPDWORD);
    static Function function = Resolve<Function>("GetFileVersionInfoSizeA");
    return function ? function(file, handle) : 0;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeW(LPCWSTR file, LPDWORD handle) {
    using Function = DWORD(WINAPI *)(LPCWSTR, LPDWORD);
    static Function function = Resolve<Function>("GetFileVersionInfoSizeW");
    return function ? function(file, handle) : 0;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeExA(DWORD flags, LPCSTR file, LPDWORD handle) {
    using Function = DWORD(WINAPI *)(DWORD, LPCSTR, LPDWORD);
    static Function function = Resolve<Function>("GetFileVersionInfoSizeExA");
    return function ? function(flags, file, handle) : 0;
}

DWORD WINAPI Proxy_GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR file, LPDWORD handle) {
    using Function = DWORD(WINAPI *)(DWORD, LPCWSTR, LPDWORD);
    static Function function = Resolve<Function>("GetFileVersionInfoSizeExW");
    return function ? function(flags, file, handle) : 0;
}

DWORD WINAPI Proxy_VerFindFileA(DWORD flags, LPCSTR file, LPCSTR winDir, LPCSTR appDir, LPSTR currentDir,
                                PUINT currentLength, LPSTR destinationDir, PUINT destinationLength) {
    using Function = DWORD(WINAPI *)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    static Function function = Resolve<Function>("VerFindFileA");
    return function
               ? function(flags, file, winDir, appDir, currentDir, currentLength, destinationDir, destinationLength)
               : 0;
}

DWORD WINAPI Proxy_VerFindFileW(DWORD flags, LPCWSTR file, LPCWSTR winDir, LPCWSTR appDir, LPWSTR currentDir,
                                PUINT currentLength, LPWSTR destinationDir, PUINT destinationLength) {
    using Function = DWORD(WINAPI *)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    static Function function = Resolve<Function>("VerFindFileW");
    return function
               ? function(flags, file, winDir, appDir, currentDir, currentLength, destinationDir, destinationLength)
               : 0;
}

DWORD WINAPI Proxy_VerInstallFileA(DWORD flags, LPCSTR sourceFile, LPCSTR destinationFile, LPCSTR sourceDir,
                                   LPCSTR destinationDir, LPCSTR currentDir, LPSTR temporaryFile,
                                   PUINT temporaryLength) {
    using Function = DWORD(WINAPI *)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    static Function function = Resolve<Function>("VerInstallFileA");
    return function ? function(flags, sourceFile, destinationFile, sourceDir, destinationDir, currentDir, temporaryFile,
                               temporaryLength)
                    : 0;
}

DWORD WINAPI Proxy_VerInstallFileW(DWORD flags, LPCWSTR sourceFile, LPCWSTR destinationFile, LPCWSTR sourceDir,
                                   LPCWSTR destinationDir, LPCWSTR currentDir, LPWSTR temporaryFile,
                                   PUINT temporaryLength) {
    using Function = DWORD(WINAPI *)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    static Function function = Resolve<Function>("VerInstallFileW");
    return function ? function(flags, sourceFile, destinationFile, sourceDir, destinationDir, currentDir, temporaryFile,
                               temporaryLength)
                    : 0;
}

DWORD WINAPI Proxy_VerLanguageNameA(DWORD language, LPSTR buffer, DWORD size) {
    using Function = DWORD(WINAPI *)(DWORD, LPSTR, DWORD);
    static Function function = Resolve<Function>("VerLanguageNameA");
    return function ? function(language, buffer, size) : 0;
}

DWORD WINAPI Proxy_VerLanguageNameW(DWORD language, LPWSTR buffer, DWORD size) {
    using Function = DWORD(WINAPI *)(DWORD, LPWSTR, DWORD);
    static Function function = Resolve<Function>("VerLanguageNameW");
    return function ? function(language, buffer, size) : 0;
}

BOOL WINAPI Proxy_VerQueryValueA(LPCVOID block, LPCSTR subBlock, LPVOID *buffer, PUINT length) {
    using Function = BOOL(WINAPI *)(LPCVOID, LPCSTR, LPVOID *, PUINT);
    static Function function = Resolve<Function>("VerQueryValueA");
    return function ? function(block, subBlock, buffer, length) : FALSE;
}

BOOL WINAPI Proxy_VerQueryValueW(LPCVOID block, LPCWSTR subBlock, LPVOID *buffer, PUINT length) {
    using Function = BOOL(WINAPI *)(LPCVOID, LPCWSTR, LPVOID *, PUINT);
    static Function function = Resolve<Function>("VerQueryValueW");
    return function ? function(block, subBlock, buffer, length) : FALSE;
}

} // extern "C"
