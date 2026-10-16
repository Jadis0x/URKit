#pragma once

#include <string>
#include <string_view>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace UrkApp {

inline std::string Utf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0,
                                           nullptr, nullptr);
    std::string output(static_cast<size_t>(length > 0 ? length : 0), '\0');
    if (length > 0)
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length, nullptr,
                            nullptr);
    return output;
}

inline std::wstring Wide(std::string_view value) {
    if (value.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    std::wstring output(static_cast<size_t>(length > 0 ? length : 0), L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), output.data(), length);
    return output;
}

} // namespace UrkApp
