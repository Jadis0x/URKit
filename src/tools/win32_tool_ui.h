#pragma once

#ifdef _WIN32

#include <dwmapi.h>
#include <uxtheme.h>
#include <windows.h>

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>

// gdiplus.h references unqualified min/max, which NOMINMAX removes.
namespace Gdiplus {
using std::max;
using std::min;
}
#include <objidl.h>

#include <gdiplus.h>

namespace URK::ToolUi {

struct Palette {
    COLORREF canvas = RGB(20, 21, 25);
    COLORREF surface = RGB(28, 30, 36);
    COLORREF surfaceMuted = RGB(23, 25, 30);
    COLORREF surfacePressed = RGB(38, 41, 49);
    // Only one step off the surface it sits on. A brighter border is what made
    // every panel edge read as a lit outline on a near-black canvas.
    COLORREF border = RGB(42, 45, 54);
    COLORREF shadow = RGB(6, 7, 9);
    COLORREF text = RGB(228, 230, 237);
    COLORREF textMuted = RGB(138, 143, 157);
    COLORREF brand = RGB(16, 17, 21);
    // Desaturated from full-chroma violet: at this luminance a 1px accent line
    // no longer blooms against the dark background.
    COLORREF accent = RGB(122, 112, 226);
    COLORREF accentHover = RGB(104, 94, 204);
};

inline constexpr Palette kPalette{};

// ANTIALIASED_QUALITY, not CLEARTYPE_QUALITY: ClearType is subpixel rendering,
// and on a dark surface its per-channel coverage shows up as orange and blue
// fringes around every glyph. Grayscale antialiasing has no such artifact.
inline HFONT CreateUiFont(int height, int weight = FW_NORMAL, const wchar_t *face = L"Segoe UI") {
    HFONT font = CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
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
    const COLORREF kCaptionColor = kPalette.brand;
    constexpr COLORREF kCaptionTextColor = RGB(255, 255, 255);
    DwmSetWindowAttribute(window, 33, &kRoundedCorners, sizeof(kRoundedCorners));
    DwmSetWindowAttribute(window, 35, &kCaptionColor, sizeof(kCaptionColor));
    DwmSetWindowAttribute(window, 36, &kCaptionTextColor, sizeof(kCaptionTextColor));
}

inline void ApplyControlTheme(HWND control) {
    SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
}

inline void Fill(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

// GDI's RoundRect cannot antialias, so every rounded corner came out as a
// visible stair-step. GDI+ can, and it is already a dependency elsewhere in the
// repo. The token is intentionally never released: the process owns the UI for
// its whole lifetime and shutdown ordering against live device contexts is not
// worth the risk.
inline bool EnsureGdiPlus() {
    static const bool ready = [] {
        Gdiplus::GdiplusStartupInput input{};
        ULONG_PTR token = 0;
        return Gdiplus::GdiplusStartup(&token, &input, nullptr) == Gdiplus::Ok;
    }();
    return ready;
}

inline Gdiplus::Color ToGdiPlusColor(COLORREF color, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, GetRValue(color), GetGValue(color), GetBValue(color));
}

inline void AddRoundedRect(Gdiplus::GraphicsPath &path, const Gdiplus::RectF &rect, float radius) {
    const float diameter = radius * 2.0f;
    if (diameter <= 0.0f) {
        path.AddRectangle(rect);
        path.CloseFigure();
        return;
    }
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

// kNoBackground means "leave whatever is already in the device context". Any
// other value clears the full rect first, which owner-drawn controls must do:
// RoundRect never touches the pixels outside the rounded shape, so the four
// corners of a button kept uninitialised DC content and rendered as white
// specks.
inline constexpr COLORREF kNoBackground = CLR_INVALID;

inline void DrawRoundedPanel(HDC dc, const RECT &rect, COLORREF fill, COLORREF border, int radius = 6,
                             COLORREF background = kNoBackground) {
    if (background != kNoBackground)
        Fill(dc, rect, background);
    if (!EnsureGdiPlus()) {
        HBRUSH fillBrush = CreateSolidBrush(fill);
        HPEN borderPen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldBrush = SelectObject(dc, fillBrush);
        HGDIOBJ oldPen = SelectObject(dc, borderPen);
        RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(borderPen);
        DeleteObject(fillBrush);
        return;
    }

    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);

    // Half-pixel inset so the one-pixel stroke lands on the pixel grid instead
    // of straddling two rows at 50% coverage, which reads as a blurred edge.
    const Gdiplus::RectF bounds(static_cast<Gdiplus::REAL>(rect.left) + 0.5f,
                                static_cast<Gdiplus::REAL>(rect.top) + 0.5f,
                                static_cast<Gdiplus::REAL>(rect.right - rect.left) - 1.0f,
                                static_cast<Gdiplus::REAL>(rect.bottom - rect.top) - 1.0f);
    Gdiplus::GraphicsPath path;
    // A radio ring asks for a radius of exactly half the box, and the half-pixel
    // inset leaves the bounds one pixel smaller than that, so clamp before the
    // arcs would overrun each other.
    const float maxRadius = std::min(bounds.Width, bounds.Height) * 0.5f;
    AddRoundedRect(path, bounds, std::min(static_cast<float>(radius), maxRadius));

    Gdiplus::SolidBrush fillBrush(ToGdiPlusColor(fill));
    graphics.FillPath(&fillBrush, &path);
    if (border != fill) {
        Gdiplus::Pen borderPen(ToGdiPlusColor(border), 1.0f);
        graphics.DrawPath(&borderPen, &path);
    }
}

inline void DrawElevatedPanel(HDC dc, const RECT &rect, COLORREF fill, COLORREF border, int radius = 8) {
    if (EnsureGdiPlus()) {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        // A stack of translucent strokes instead of one hard offset rectangle:
        // a solid black slab under the panel is what made the card look pasted
        // on rather than raised.
        for (int spread = 5; spread >= 1; --spread) {
            const Gdiplus::RectF bounds(static_cast<Gdiplus::REAL>(rect.left - spread),
                                        static_cast<Gdiplus::REAL>(rect.top - spread + 2),
                                        static_cast<Gdiplus::REAL>(rect.right - rect.left + spread * 2),
                                        static_cast<Gdiplus::REAL>(rect.bottom - rect.top + spread * 2));
            Gdiplus::GraphicsPath path;
            AddRoundedRect(path, bounds, static_cast<float>(radius + spread));
            Gdiplus::Pen pen(ToGdiPlusColor(kPalette.shadow, static_cast<BYTE>(10 + (5 - spread) * 6)), 2.0f);
            graphics.DrawPath(&pen, &path);
        }
    }
    DrawRoundedPanel(dc, rect, fill, border, radius);
}

inline void DrawButton(const DRAWITEMSTRUCT &item, HFONT font, bool primary,
                       COLORREF background = kPalette.surface) {
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF fill = primary ? (disabled  ? kPalette.surfacePressed
                                     : pressed ? kPalette.accentHover
                                               : kPalette.accent)
                                  : (pressed ? kPalette.surfacePressed : kPalette.surface);
    const COLORREF border = primary ? fill : (focused ? kPalette.accent : kPalette.border);
    DrawRoundedPanel(item.hDC, rect, fill, border, 5, background);

    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, primary ? RGB(255, 255, 255) : (disabled ? kPalette.textMuted : kPalette.text));
    HGDIOBJ oldFont = font ? SelectObject(item.hDC, font) : nullptr;
    DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (oldFont)
        SelectObject(item.hDC, oldFont);
}

// BS_OWNERDRAW buttons carry no check state, so ODS_CHECKED never arrives.
// The owner passes its own state instead.
inline void DrawRadioButton(const DRAWITEMSTRUCT &item, HFONT font, bool checked) {
    RECT rect = item.rcItem;
    Fill(item.hDC, rect, kPalette.surface);

    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const int diameter = 18;
    const int top = rect.top + (rect.bottom - rect.top - diameter) / 2;
    RECT circle{rect.left, top, rect.left + diameter, top + diameter};

    // A GDI Ellipse is not antialiased, so the ring came out as a ragged
    // pixel outline. The radius equals half the box, so the shared rounded-rect
    // helper draws the same circle with smooth edges.
    DrawRoundedPanel(item.hDC, circle, kPalette.surfaceMuted, checked ? kPalette.accent : kPalette.textMuted,
                     diameter / 2);

    if (checked) {
        constexpr int inset = 5;
        RECT dot{circle.left + inset, circle.top + inset, circle.right - inset, circle.bottom - inset};
        const COLORREF dotColor = disabled ? kPalette.textMuted : kPalette.accent;
        DrawRoundedPanel(item.hDC, dot, dotColor, dotColor, (diameter - inset * 2) / 2);
    }

    RECT textRect{rect.left + diameter + 8, rect.top, rect.right, rect.bottom};
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? kPalette.textMuted : kPalette.text);
    HGDIOBJ oldFont = font ? SelectObject(item.hDC, font) : nullptr;
    wchar_t text[128]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    DrawTextW(item.hDC, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (oldFont)
        SelectObject(item.hDC, oldFont);
}

inline void DrawCheckbox(const DRAWITEMSTRUCT &item, HFONT font, bool checked) {
    RECT rect = item.rcItem;
    Fill(item.hDC, rect, kPalette.surface);

    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const int size = 16;
    const int top = rect.top + (rect.bottom - rect.top - size) / 2;
    RECT box{rect.left, top, rect.left + size, top + size};

    const COLORREF fill = checked ? (disabled ? kPalette.textMuted : kPalette.accent) : kPalette.surfaceMuted;
    const COLORREF border = checked ? fill : kPalette.textMuted;
    DrawRoundedPanel(item.hDC, box, fill, border, 3);

    if (checked && EnsureGdiPlus()) {
        Gdiplus::Graphics graphics(item.hDC);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen checkPen(Gdiplus::Color(255, 255, 255, 255), 1.8f);
        checkPen.SetStartCap(Gdiplus::LineCapRound);
        checkPen.SetEndCap(Gdiplus::LineCapRound);
        checkPen.SetLineJoin(Gdiplus::LineJoinRound);
        const Gdiplus::PointF points[3] = {
            {static_cast<Gdiplus::REAL>(box.left) + 3.5f, static_cast<Gdiplus::REAL>(box.top) + 8.0f},
            {static_cast<Gdiplus::REAL>(box.left) + 6.5f, static_cast<Gdiplus::REAL>(box.top) + 11.0f},
            {static_cast<Gdiplus::REAL>(box.left) + 12.5f, static_cast<Gdiplus::REAL>(box.top) + 4.5f},
        };
        graphics.DrawLines(&checkPen, points, 3);
    }

    RECT textRect{rect.left + size + 8, rect.top, rect.right, rect.bottom};
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, disabled ? kPalette.textMuted : kPalette.text);
    HGDIOBJ oldFont = font ? SelectObject(item.hDC, font) : nullptr;
    wchar_t text[256]{};
    GetWindowTextW(item.hwndItem, text, static_cast<int>(std::size(text)));
    DrawTextW(item.hDC, text, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    if (oldFont)
        SelectObject(item.hDC, oldFont);
}

inline void DrawBrandMark(HDC dc, int x, int y, int size) {
    RECT mark{x, y, x + size, y + size};
    DrawRoundedPanel(dc, mark, kPalette.accent, kPalette.accent, size / 4);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
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
