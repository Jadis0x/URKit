std::string Win32InputCoordinatesHeaderModule() {
    return R"URK(#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

[[nodiscard]] bool client_mouse_to_desktop(HWND window, POINT *position) noexcept;
[[nodiscard]] bool desktop_mouse_to_imgui(HWND window, bool multiViewport, POINT *position) noexcept;

} // namespace ModRenderHook
)URK";
}

std::string Win32InputCoordinatesSourceModule() {
    return R"URK(#include "win32_input_coordinates.h"

namespace ModRenderHook {

bool client_mouse_to_desktop(HWND window, POINT *position) noexcept {
    return window && position && ClientToScreen(window, position) != FALSE;
}

bool desktop_mouse_to_imgui(HWND window, bool multiViewport, POINT *position) noexcept {
    if (!position)
        return false;
    if (multiViewport)
        return true;
    return window && ScreenToClient(window, position) != FALSE;
}

} // namespace ModRenderHook
)URK";
}

std::string Win32MessagePumpHeaderModule() {
    return R"URK(#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

struct WindowMessagePumpResult {
    std::uint32_t dispatched = 0;
    std::uint32_t foreignThreadWindows = 0;
    bool backlogRemaining = false;
};

[[nodiscard]] WindowMessagePumpResult pump_owned_window_messages(std::span<const HWND> windows,
                                                                 std::size_t messageBudget = 512);

[[nodiscard]] bool is_imgui_platform_window(HWND window);

} // namespace ModRenderHook
)URK";
}

std::string Win32MessagePumpSourceModule() {
    return R"URK(#include "win32_message_pump.h"

#include <algorithm>
#include <vector>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool contains_window(std::span<const HWND> windows, std::size_t end, HWND candidate) {
    const auto finish = windows.begin() + static_cast<std::ptrdiff_t>(end);
    return std::find(windows.begin(), finish, candidate) != finish;
}

} // namespace

WindowMessagePumpResult pump_owned_window_messages(std::span<const HWND> windows, std::size_t messageBudget) {
    WindowMessagePumpResult result{};
    if (windows.empty() || messageBudget == 0)
        return result;

    const DWORD currentThread = GetCurrentThreadId();
    thread_local std::vector<HWND> ownedWindows;
    ownedWindows.clear();
    ownedWindows.reserve(windows.size());

    for (std::size_t index = 0; index < windows.size(); ++index) {
        const HWND window = windows[index];
        if (!window || !IsWindow(window) || contains_window(windows, index, window))
            continue;
        if (GetWindowThreadProcessId(window, nullptr) != currentThread) {
            ++result.foreignThreadWindows;
            continue;
        }
        ownedWindows.push_back(window);
    }

    MSG message{};
    std::size_t remaining = messageBudget;
    bool madeProgress = true;
    while (remaining != 0 && madeProgress) {
        madeProgress = false;
        for (const HWND window : ownedWindows) {
            if (remaining == 0)
                break;
            if (!PeekMessageW(&message, window, 0, 0, PM_REMOVE))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            ++result.dispatched;
            --remaining;
            madeProgress = true;
        }
    }

    if (remaining == 0) {
        for (const HWND window : ownedWindows) {
            if (PeekMessageW(&message, window, 0, 0, PM_NOREMOVE)) {
                result.backlogRemaining = true;
                break;
            }
        }
    }
    return result;
}

bool is_imgui_platform_window(HWND window) {
    if (!window || !IsWindow(window))
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId == GetCurrentProcessId() && GetPropA(window, "IMGUI_CONTEXT") != nullptr;
}

} // namespace ModRenderHook
)URK";
}


