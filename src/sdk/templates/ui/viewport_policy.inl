std::string Win32ViewportPolicyHeaderModule() {
    return R"URK(#pragma once
#include <span>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {
struct Win32ViewportPolicyResult {
    HWND window{};
    const char *operation{};
    DWORD error{ERROR_SUCCESS};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ERROR_SUCCESS;
    }
};

struct Win32ViewportTopology {
    HWND owner{};
    DWORD windowThread{};
    LONG_PTR extendedStyle{};
};

class Win32ViewportPolicy final {
  public:
    [[nodiscard]] Win32ViewportPolicyResult apply(std::span<const HWND> windows, HWND gameWindow);
    void reset() noexcept;

  private:
    struct ConfiguredWindow {
        HWND window{};
        HWND owner{};
    };

    [[nodiscard]] bool is_configured(HWND window, HWND owner) const noexcept;
    void retain_live_windows(std::span<const HWND> windows);

    std::vector<ConfiguredWindow> configuredWindows_;
};

[[nodiscard]] Win32ViewportTopology query_viewport_topology(HWND window) noexcept;
} // namespace ModRenderHook
)URK";
}

std::string Win32ViewportPolicySourceModule() {
    return R"URK(#include "win32_viewport_policy.h"

#include <algorithm>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool contains_window(std::span<const HWND> windows, HWND candidate) {
    return std::find(windows.begin(), windows.end(), candidate) != windows.end();
}

[[nodiscard]] bool same_thread(HWND left, HWND right) {
    return left && right && GetWindowThreadProcessId(left, nullptr) == GetWindowThreadProcessId(right, nullptr);
}

} // namespace

bool Win32ViewportPolicy::is_configured(HWND window, HWND owner) const noexcept {
    return std::any_of(configuredWindows_.begin(), configuredWindows_.end(), [window, owner](const auto &configured) {
        return configured.window == window && configured.owner == owner;
    });
}

void Win32ViewportPolicy::retain_live_windows(std::span<const HWND> windows) {
    std::erase_if(configuredWindows_, [windows](const ConfiguredWindow &configured) {
        return !configured.window || !IsWindow(configured.window) || !contains_window(windows, configured.window);
    });
}

Win32ViewportPolicyResult Win32ViewportPolicy::apply(std::span<const HWND> windows, HWND gameWindow) {
    retain_live_windows(windows);
    Win32ViewportPolicyResult firstFailure{};

    for (const HWND window : windows) {
        // cross-thread owner deadlocks the engine; stay ownerless + topmost instead
        const HWND owner = same_thread(window, gameWindow) ? gameWindow : nullptr;
        if (!window || !IsWindow(window) || is_configured(window, owner))
            continue;

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR currentStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        DWORD error = GetLastError();
        if (!currentStyle && error != ERROR_SUCCESS) {
            if (firstFailure)
                firstFailure = {window, "GetWindowLongPtrW(GWL_EXSTYLE)", error};
            continue;
        }

        const LONG_PTR desiredStyle =
            (currentStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
        bool frameChanged = false;
        if (desiredStyle != currentStyle) {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, GWL_EXSTYLE, desiredStyle);
            error = GetLastError();
            if (!previous && error != ERROR_SUCCESS) {
                if (firstFailure)
                    firstFailure = {window, "SetWindowLongPtrW(GWL_EXSTYLE)", error};
                continue;
            }
            frameChanged = true;
        }

        if (owner && GetWindow(window, GW_OWNER) != owner) {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previousOwner =
                SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
            error = GetLastError();
            if (!previousOwner && error != ERROR_SUCCESS) {
                if (firstFailure)
                    firstFailure = {window, "SetWindowLongPtrW(GWLP_HWNDPARENT)", error};
                continue;
            }
        }

        UINT positionFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
        if (owner)
            positionFlags |= SWP_NOZORDER;
        if (frameChanged)
            positionFlags |= SWP_FRAMECHANGED;
        if ((frameChanged || !owner) &&
            !SetWindowPos(window, owner ? nullptr : HWND_TOPMOST, 0, 0, 0, 0, positionFlags)) {
            if (firstFailure)
                firstFailure = {window, "SetWindowPos", GetLastError()};
            continue;
        }

        configuredWindows_.push_back({window, owner});
    }
    return firstFailure;
}

void Win32ViewportPolicy::reset() noexcept {
    configuredWindows_.clear();
}

Win32ViewportTopology query_viewport_topology(HWND window) noexcept {
    if (!window || !IsWindow(window))
        return {};
    Win32ViewportTopology result{};
    result.owner = GetWindow(window, GW_OWNER);
    result.windowThread = GetWindowThreadProcessId(window, nullptr);
    result.extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    return result;
}
} // namespace ModRenderHook
)URK";
}


