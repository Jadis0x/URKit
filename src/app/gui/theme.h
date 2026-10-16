#pragma once

#include <wx/colour.h>
#include <wx/font.h>
#include <wx/fontenum.h>
#include <wx/settings.h>

namespace UrkGui::Theme {

inline const wxColour kWindow(0x0d, 0x0e, 0x11);
inline const wxColour kSurface(0x15, 0x17, 0x1c);
inline const wxColour kSurfaceHover(0x1b, 0x1e, 0x24);
inline const wxColour kSurfaceActive(0x23, 0x27, 0x2f);
inline const wxColour kInput(0x10, 0x12, 0x16);
inline const wxColour kBorder(0x24, 0x27, 0x2f);
inline const wxColour kBorderStrong(0x34, 0x39, 0x43);
inline const wxColour kText(0xec, 0xed, 0xf1);
inline const wxColour kTextMuted(0x8d, 0x93, 0x9f);
inline const wxColour kTextFaint(0x5a, 0x60, 0x6b);
inline const wxColour kAccent(0xee, 0xef, 0xf2);
inline const wxColour kAccentHover(0xff, 0xff, 0xff);
inline const wxColour kPrimary(0xee, 0xef, 0xf2);
inline const wxColour kPrimaryHover(0xff, 0xff, 0xff);
inline const wxColour kPrimaryText(0x0d, 0x0e, 0x11);
inline const wxColour kSuccess(0x34, 0xc7, 0x7b);
inline const wxColour kWarning(0xe8, 0xa8, 0x2a);
inline const wxColour kError(0xf0, 0x5a, 0x62);

inline wxColour Mix(const wxColour &a, const wxColour &b, double t) {
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    auto channel = [t](unsigned char x, unsigned char y) {
        return static_cast<unsigned char>(x + (y - x) * t + 0.5);
    };
    return wxColour(channel(a.Red(), b.Red()), channel(a.Green(), b.Green()), channel(a.Blue(), b.Blue()));
}

inline wxFont UiFont(double points = 9.5, wxFontWeight weight = wxFONTWEIGHT_NORMAL) {
    static const wxString face =
        wxFontEnumerator::IsValidFacename("Segoe UI Variable Text") ? "Segoe UI Variable Text" : "Segoe UI";
    wxFont font(wxFontInfo(points).FaceName(face));
    font.SetNumericWeight(weight);
    return font;
}

inline wxFont DisplayFont(double points, wxFontWeight weight = wxFONTWEIGHT_SEMIBOLD) {
    static const wxString face =
        wxFontEnumerator::IsValidFacename("Segoe UI Variable Display") ? "Segoe UI Variable Display" : "Segoe UI";
    wxFont font(wxFontInfo(points).FaceName(face));
    font.SetNumericWeight(weight);
    return font;
}

// Segoe Fluent Icons on Windows 11, MDL2 Assets on Windows 10; both share the code points used here.
inline wxFont IconFont(double points = 10.5) {
    static const wxString face =
        wxFontEnumerator::IsValidFacename("Segoe Fluent Icons") ? "Segoe Fluent Icons" : "Segoe MDL2 Assets";
    return wxFont(wxFontInfo(points).FaceName(face));
}

inline wxFont MonoFont(double points = 9.0) {
    static const wxString face = wxFontEnumerator::IsValidFacename("Cascadia Mono") ? "Cascadia Mono" : "Consolas";
    return wxFont(wxFontInfo(points).FaceName(face));
}

namespace Icon {
inline constexpr wchar_t kAdd = 0xE710;
inline constexpr wchar_t kClose = 0xE711;
inline constexpr wchar_t kRefresh = 0xE72C;
inline constexpr wchar_t kCheck = 0xE73E;
inline constexpr wchar_t kChevronDown = 0xE70D;
inline constexpr wchar_t kWarning = 0xE7BA;
inline constexpr wchar_t kError = 0xE783;
inline constexpr wchar_t kInfo = 0xE946;
inline constexpr wchar_t kFolder = 0xE8B7;
inline constexpr wchar_t kOpenFile = 0xE8E5;
inline constexpr wchar_t kDownload = 0xE896;
inline constexpr wchar_t kUp = 0xE74A;
inline constexpr wchar_t kPlay = 0xE768;
inline constexpr wchar_t kGame = 0xE7FC;
inline constexpr wchar_t kCode = 0xE943;
inline constexpr wchar_t kBug = 0xEBE8;
inline constexpr wchar_t kPerson = 0xE77B;
inline constexpr wchar_t kHeart = 0xEB51;
inline constexpr wchar_t kExternal = 0xE8A7;
inline constexpr wchar_t kLink = 0xE71B;
inline constexpr wchar_t kBolt = 0xE945;
inline constexpr wchar_t kCopy = 0xE8C8;
inline constexpr wchar_t kDelete = 0xE74D;
} // namespace Icon

} // namespace UrkGui::Theme
