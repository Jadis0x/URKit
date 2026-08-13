#include "platform_paths.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

std::string Platform_ExeDir() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (char *slash = strrchr(path, '\\'))
        slash[1] = '\0';
    return path;
}

std::string Platform_WindowsError(unsigned long error) {
    char *message = nullptr;
    const DWORD size =
        FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                       nullptr, static_cast<DWORD>(error), 0, reinterpret_cast<char *>(&message), 0, nullptr);
    std::string result = size && message ? std::string(message, size) : "unknown Windows error";
    if (message)
        LocalFree(message);
    while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
        result.pop_back();
    return result;
}

std::string Platform_GameName() {
    char path[MAX_PATH]{};
    GetModuleFileNameA(nullptr, path, MAX_PATH);
    DWORD ignored = 0;
    const DWORD bytes = GetFileVersionInfoSizeA(path, &ignored);
    if (bytes) {
        std::vector<unsigned char> data(bytes);
        if (GetFileVersionInfoA(path, 0, bytes, data.data())) {
            struct Translation {
                WORD language;
                WORD codePage;
            };
            Translation *translations = nullptr;
            UINT translationBytes = 0;
            if (VerQueryValueA(data.data(), "\\VarFileInfo\\Translation", reinterpret_cast<void **>(&translations),
                               &translationBytes) &&
                translations && translationBytes >= sizeof(Translation)) {
                char query[96]{};
                snprintf(query, sizeof(query), "\\StringFileInfo\\%04x%04x\\ProductName", translations[0].language,
                         translations[0].codePage);
                char *productName = nullptr;
                UINT productNameLength = 0;
                if (VerQueryValueA(data.data(), query, reinterpret_cast<void **>(&productName), &productNameLength) &&
                    productName && productNameLength > 1) {
                    return productName;
                }
            }
        }
    }
    const char *name = strrchr(path, '\\');
    std::string result = name ? name + 1 : path;
    const size_t extension = result.rfind('.');
    if (extension != std::string::npos)
        result.resize(extension);
    return result.empty() ? "Unknown game" : result;
}
