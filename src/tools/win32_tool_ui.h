#pragma once

#ifdef _WIN32

#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>

#include <iterator>
#include <string>
#include <string_view>

namespace URK::ToolUi {

struct Palette {
    COLORREF canvas = RGB(255, 255, 255);
    COLORREF surface = RGB(255, 255, 255);
    COLORREF surfaceMuted = RGB(255, 255, 255);
    COLORREF border = RGB(204, 204, 204);
    COLORREF text = RGB(16, 16, 16);
    COLORREF textMuted = RGB(96, 96, 96);
    COLORREF brand = RGB(0, 0, 0);
    COLORREF accent = RGB(0, 0, 0);
    COLORREF accentHover = RGB(48, 48, 48);
};

inline constexpr Palette kPalette{};

inline HFONT CreateUiFont(int height, int weight = FW_NORMAL, const wchar_t *face = L"Bahnschrift") {
    HFONT font = CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
    return font;
}

inline std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty())
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0)
        return {};
    std::wstring output(static_cast<size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(),
                            size) != size) {
        return {};
    }
    return output;
}

inline std::string WideToUtf8(std::wstring_view value) {
    if (value.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string output(static_cast<size_t>(size), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(),
                            size, nullptr, nullptr) != size) {
        return {};
    }
    return output;
}

inline std::wstring WindowText(HWND control) {
    const int length = GetWindowTextLengthW(control);
    if (length <= 0)
        return {};
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<size_t>(length));
    return text;
}

inline void SetUtf8Text(HWND control, std::string_view text) {
    SetWindowTextW(control, Utf8ToWide(text).c_str());
}

inline void ApplyModernWindowFrame(HWND window) {
    constexpr DWORD kRoundedCorners = 2;
    constexpr COLORREF kCaptionColor = RGB(0, 0, 0);
    constexpr COLORREF kCaptionTextColor = RGB(255, 255, 255);
    DwmSetWindowAttribute(window, 33, &kRoundedCorners, sizeof(kRoundedCorners));
    DwmSetWindowAttribute(window, 35, &kCaptionColor, sizeof(kCaptionColor));
    DwmSetWindowAttribute(window, 36, &kCaptionTextColor, sizeof(kCaptionTextColor));
}

inline void ApplyControlTheme(HWND control) {
    SetWindowTheme(control, L"Explorer", nullptr);
}

inline void Fill(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

inline void DrawRoundedPanel(HDC dc, const RECT &rect, COLORREF fill, COLORREF border, int radius = 6) {
    HBRUSH fillBrush = CreateSolidBrush(fill);
    HPEN borderPen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, fillBrush);
    HGDIOBJ oldPen = SelectObject(dc, borderPen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(borderPen);
    DeleteObject(fillBrush);
}

inline void DrawButton(const DRAWITEMSTRUCT &item, HFONT font, bool primary) {
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF fill = primary ? (disabled  ? RGB(160, 160, 160)
                                     : pressed ? kPalette.accentHover
                                               : kPalette.accent)
                                  : (pressed ? RGB(232, 232, 232) : kPalette.surface);
    const COLORREF border = primary ? fill : (focused ? kPalette.accent : kPalette.border);
    DrawRoundedPanel(item.hDC, rect, fill, border, 4);

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, primary ? RGB(255, 255, 255) : (disabled ? kPalette.textMuted : kPalette.text));
    HGDIOBJ oldFont = font ? SelectObject(item.hDC, font) : nullptr;
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont)
        SelectObject(item.hDC, oldFont);
}

inline void DrawBrandMark(HDC dc, int x, int y, int size) {
    RECT mark{x, y, x + size, y + size};
    HBRUSH brush = CreateSolidBrush(kPalette.surface);
    HPEN pen = CreatePen(PS_SOLID, 1, kPalette.surface);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, mark.left, mark.top, mark.right, mark.bottom, 5, 5);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kPalette.text);
    HFONT markFont = CreateUiFont(size / 2, FW_BOLD);
    HGDIOBJ oldFont = markFont ? SelectObject(dc, markFont) : nullptr;
    DrawTextW(dc, L"UR", -1, &mark, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (oldFont)
        SelectObject(dc, oldFont);
    if (markFont)
        DeleteObject(markFont);
}

} // namespace URK::ToolUi

#endif
