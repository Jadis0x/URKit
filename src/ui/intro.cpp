#include "intro.h"
#include "intro_splash_embedded.h"
#include "project_version.h"

#include <objidl.h>
#include <wincrypt.h>
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <gdiplus.h>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace {

// Per-pixel alpha window: everything, including the soft shadow and rounded corners, is drawn
// into one premultiplied bitmap and handed to UpdateLayeredWindow. Never touches the game's window.

HWND g_window = nullptr;
bool g_closing = false;
HANDLE g_thread = nullptr;
HANDLE g_readyEvent = nullptr;
DWORD g_threadId = 0;
std::string g_game = "Unknown";
std::string g_backend = "Auto";
std::string g_status = "Starting";
std::string g_currentMod;
int g_loaded = 0;
int g_failed = 0;
int g_discovered = 0;
float g_progress = 0.03f;
DWORD g_lastProgressTick = 0;
COLORREF g_accent = RGB(45, 125, 245);
std::mutex g_mutex;

constexpr UINT kRefresh = WM_APP + 41;
constexpr UINT kBeginClose = WM_APP + 42;
constexpr UINT_PTR kFrameTimer = 1;
constexpr UINT kFrameMs = 33;
constexpr DWORD kTopmostRefreshMs = 500;
constexpr DWORD kFadeInMs = 180;
constexpr DWORD kFadeOutMs = 220;
constexpr float kProgressModsStart = 0.72f;
constexpr float kProgressModsEnd = 0.96f;
// Card size in 96-DPI pixels; the shadow margin is drawn around it.
constexpr int kCardWidth = 760;
constexpr int kCardHeight = 300;
constexpr int kShadowMargin = 18;

ULONG_PTR g_gdiplusToken = 0;
Gdiplus::Image *g_splashImage = nullptr;
IStream *g_splashStream = nullptr;

struct Surface {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ previous = nullptr;
    void *bits = nullptr;
    int width = 0;
    int height = 0;
};
Surface g_surface;

float g_scale = 1.0f;
float g_shownProgress = 0.0f;
DWORD g_shownAt = 0;
DWORD g_closeAt = 0;
DWORD g_lastTopmost = 0;

float ClampProgress(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

void AdvanceProgressLocked(float value) {
    const float clamped = ClampProgress(value);
    if (clamped > g_progress) {
        g_progress = clamped;
        g_lastProgressTick = GetTickCount();
    }
}

float ProgressForMods(int discovered, int loaded, int failed, bool loading) {
    if (discovered <= 0)
        return loading ? kProgressModsStart : kProgressModsEnd;

    const int rawCompleted = loaded + failed;
    const int completed = rawCompleted < 0 ? 0 : (std::min)(rawCompleted, discovered);
    const float span = kProgressModsEnd - kProgressModsStart;
    float value = kProgressModsStart + span * (static_cast<float>(completed) / static_cast<float>(discovered));

    if (loading && completed < discovered)
        value = (std::max)(value, kProgressModsStart + 0.01f);
    if (!loading)
        value = kProgressModsEnd;
    return value;
}

std::wstring Widen(const std::string &text) {
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>((std::max)(length, 0)), L'\0');
    if (length > 0)
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

std::filesystem::path ExeDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length)
        return {};
    return std::filesystem::path(std::wstring(path, length)).parent_path();
}

std::vector<std::filesystem::path> SplashCandidates() {
    const std::filesystem::path exeDir = ExeDirectory();
    const std::filesystem::path assetDir = exeDir / L"URKit";
    return {
        assetDir / L"splash.png",       assetDir / L"intro_splash.png", assetDir / L"splash.jpg",
        assetDir / L"intro_splash.jpg", assetDir / L"splash.bmp",       assetDir / L"intro_splash.bmp",
        exeDir / L"splash.png",         exeDir / L"intro_splash.png",   exeDir / L"splash.jpg",
        exeDir / L"intro_splash.jpg",   exeDir / L"splash.bmp",         exeDir / L"intro_splash.bmp",
    };
}

void EnsureGdiplusStarted() {
    if (g_gdiplusToken)
        return;
    Gdiplus::GdiplusStartupInput input{};
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Gdiplus::Ok)
        g_gdiplusToken = 0;
}

void ReleaseSurface() {
    if (g_surface.dc) {
        if (g_surface.previous)
            SelectObject(g_surface.dc, g_surface.previous);
        DeleteDC(g_surface.dc);
    }
    if (g_surface.bitmap)
        DeleteObject(g_surface.bitmap);
    g_surface = {};
}

void ReleaseGraphics() {
    ReleaseSurface();
    delete g_splashImage;
    g_splashImage = nullptr;
    if (g_splashStream) {
        g_splashStream->Release();
        g_splashStream = nullptr;
    }
    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

Gdiplus::Image *LoadImageFromBytes(const std::vector<BYTE> &bytes) {
    if (bytes.empty())
        return nullptr;
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!memory)
        return nullptr;
    void *target = GlobalLock(memory);
    if (!target) {
        GlobalFree(memory);
        return nullptr;
    }
    std::memcpy(target, bytes.data(), bytes.size());
    GlobalUnlock(memory);

    IStream *stream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream))) {
        GlobalFree(memory);
        return nullptr;
    }
    Gdiplus::Image *image = Gdiplus::Image::FromStream(stream, FALSE);
    if (image && image->GetLastStatus() == Gdiplus::Ok && image->GetWidth() > 0 && image->GetHeight() > 0) {
        g_splashStream = stream;
        return image;
    }
    delete image;
    stream->Release();
    return nullptr;
}

Gdiplus::Image *LoadEmbeddedSplashImage() {
    DWORD decodedSize = 0;
    if (!CryptStringToBinaryA(IntroSplashEmbedded::kImageBase64, 0, CRYPT_STRING_BASE64, nullptr, &decodedSize, nullptr,
                              nullptr) ||
        decodedSize == 0)
        return nullptr;
    std::vector<BYTE> decoded(decodedSize);
    if (!CryptStringToBinaryA(IntroSplashEmbedded::kImageBase64, 0, CRYPT_STRING_BASE64, decoded.data(), &decodedSize,
                              nullptr, nullptr))
        return nullptr;
    decoded.resize(decodedSize);
    return LoadImageFromBytes(decoded);
}

void LoadSplashImage() {
    EnsureGdiplusStarted();
    if (!g_gdiplusToken)
        return;
    for (const std::filesystem::path &path : SplashCandidates()) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec) || ec)
            continue;
        Gdiplus::Image *image = Gdiplus::Image::FromFile(path.c_str(), FALSE);
        if (image && image->GetLastStatus() == Gdiplus::Ok && image->GetWidth() > 0 && image->GetHeight() > 0) {
            g_splashImage = image;
            return;
        }
        delete image;
    }
    g_splashImage = LoadEmbeddedSplashImage();
}

bool CreateSurface(int width, int height) {
    ReleaseSurface();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    HDC screen = GetDC(nullptr);
    g_surface.dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!g_surface.dc)
        return false;
    g_surface.bitmap = CreateDIBSection(g_surface.dc, &info, DIB_RGB_COLORS, &g_surface.bits, nullptr, 0);
    if (!g_surface.bitmap || !g_surface.bits) {
        ReleaseSurface();
        return false;
    }
    g_surface.previous = SelectObject(g_surface.dc, g_surface.bitmap);
    g_surface.width = width;
    g_surface.height = height;
    return true;
}

// Never touches the game's window: minimizing it mid-load can deadlock the engine.
void KeepIntroOnTop(HWND window) {
    if (!window || g_closing)
        return;
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

Gdiplus::Color WithAlpha(COLORREF colour, BYTE alpha) {
    return Gdiplus::Color(alpha, GetRValue(colour), GetGValue(colour), GetBValue(colour));
}

void AddRoundedRect(Gdiplus::GraphicsPath &path, Gdiplus::RectF rect, float radius) {
    const float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.Y, diameter, diameter, 270.0f, 90.0f);
    path.AddArc(rect.GetRight() - diameter, rect.GetBottom() - diameter, diameter, diameter, 0.0f, 90.0f);
    path.AddArc(rect.X, rect.GetBottom() - diameter, diameter, diameter, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRounded(Gdiplus::Graphics &graphics, const Gdiplus::Brush &brush, Gdiplus::RectF rect, float radius) {
    Gdiplus::GraphicsPath path;
    AddRoundedRect(path, rect, radius);
    graphics.FillPath(&brush, &path);
}

float TextWidth(Gdiplus::Graphics &graphics, const std::wstring &text, const Gdiplus::Font &font) {
    Gdiplus::RectF bounds;
    graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), &font, Gdiplus::PointF(0, 0), &bounds);
    return bounds.Width;
}

void DrawText(Gdiplus::Graphics &graphics, const std::wstring &text, const Gdiplus::Font &font, Gdiplus::Color colour,
              float x, float y, float maxWidth = 0.0f, bool alignRight = false) {
    Gdiplus::SolidBrush brush(colour);
    Gdiplus::StringFormat format;
    format.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    format.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    if (alignRight)
        format.SetAlignment(Gdiplus::StringAlignmentFar);
    if (maxWidth > 0.0f) {
        Gdiplus::RectF layout(alignRight ? x - maxWidth : x, y, maxWidth, font.GetHeight(&graphics) + 4.0f);
        graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, layout, &format, &brush);
    } else {
        graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, Gdiplus::PointF(x, y), &format, &brush);
    }
}

struct Snapshot {
    std::wstring game;
    std::wstring backend;
    std::wstring status;
    std::wstring currentMod;
    int discovered = 0;
    int loaded = 0;
    int failed = 0;
    float progress = 0.0f;
    DWORD lastProgressTick = 0;
    COLORREF accent = RGB(45, 125, 245);
};

Snapshot TakeSnapshot() {
    std::lock_guard<std::mutex> lock(g_mutex);
    Snapshot snapshot;
    snapshot.game = Widen(g_game);
    snapshot.backend = Widen(g_backend);
    snapshot.status = Widen(g_status);
    snapshot.currentMod = Widen(g_currentMod);
    snapshot.discovered = g_discovered;
    snapshot.loaded = g_loaded;
    snapshot.failed = g_failed;
    snapshot.progress = g_progress;
    snapshot.lastProgressTick = g_lastProgressTick;
    snapshot.accent = g_accent;
    return snapshot;
}

void RenderFrame(Gdiplus::Graphics &graphics, const Snapshot &state, DWORD now) {
    const float s = g_scale;
    const float margin = kShadowMargin * s;
    const Gdiplus::RectF card(margin, margin, kCardWidth * s, kCardHeight * s);
    const float radius = 10.0f * s;

    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
    graphics.SetCompositingQuality(Gdiplus::CompositingQualityHighQuality);
    graphics.Clear(Gdiplus::Color(0, 0, 0, 0));

    // Soft shadow: stacked translucent outlines, densest next to the card.
    for (int step = static_cast<int>(margin); step > 0; step -= 2) {
        const float grow = static_cast<float>(step);
        const BYTE alpha = static_cast<BYTE>(std::clamp(10.0f * (1.0f - grow / margin) + 1.0f, 0.0f, 255.0f));
        Gdiplus::SolidBrush shadow(Gdiplus::Color(alpha, 0, 0, 0));
        FillRounded(graphics, shadow,
                    Gdiplus::RectF(card.X - grow, card.Y - grow + 4.0f * s, card.Width + grow * 2, card.Height + grow * 2),
                    radius + grow);
    }

    Gdiplus::GraphicsPath cardPath;
    AddRoundedRect(cardPath, card, radius);
    // The artwork is drawn on pure black, so the card matches it.
    Gdiplus::SolidBrush base(Gdiplus::Color(255, 0, 0, 0));
    graphics.FillPath(&base, &cardPath);

    // Artwork fills the card and sits under the information strip.
    graphics.SetClip(&cardPath);
    if (g_splashImage) {
        const float imageWidth = static_cast<float>(g_splashImage->GetWidth());
        const float imageHeight = static_cast<float>(g_splashImage->GetHeight());
        // Fit the width so the wordmark at the right edge is never cropped; the strip covers the bottom.
        const float fitScale = card.Width / imageWidth;
        graphics.DrawImage(g_splashImage, Gdiplus::RectF(card.X, card.Y, card.Width, imageHeight * fitScale));
    }
    const float stripHeight = 92.0f * s;
    const Gdiplus::RectF strip(card.X, card.GetBottom() - stripHeight, card.Width, stripHeight);
    const float fadeHeight = 64.0f * s;
    Gdiplus::LinearGradientBrush fade(Gdiplus::RectF(strip.X, strip.Y - fadeHeight, strip.Width, fadeHeight + 1.0f),
                                      Gdiplus::Color(0, 6, 7, 9), Gdiplus::Color(246, 6, 7, 9),
                                      Gdiplus::LinearGradientModeVertical);
    graphics.FillRectangle(&fade, strip.X, strip.Y - fadeHeight, strip.Width, fadeHeight);
    Gdiplus::SolidBrush stripBrush(Gdiplus::Color(246, 6, 7, 9));
    graphics.FillRectangle(&stripBrush, strip);
    graphics.ResetClip();

    Gdiplus::Pen border(Gdiplus::Color(48, 255, 255, 255), 1.0f);
    graphics.DrawPath(&border, &cardPath);

    const float padding = 26.0f * s;
    const float left = card.X + padding;
    const float right = card.GetRight() - padding;
    const float width = right - left;

    Gdiplus::Font statusFont(L"Segoe UI Semibold", 14.0f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font detailFont(L"Segoe UI", 12.0f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font smallFont(L"Segoe UI", 11.0f * s, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);

    // Status line, with the mod being loaded after it.
    const float statusY = strip.Y + 14.0f * s;
    std::wstring status = state.status.empty() ? L"Preparing runtime and mods" : state.status;
    std::wstring counters;
    if (state.discovered > 0 || state.loaded > 0 || state.failed > 0) {
        counters = std::to_wstring(state.loaded) + L" of " + std::to_wstring((std::max)(state.discovered, 1)) +
                   L" loaded";
        if (state.failed > 0)
            counters += L"  ·  " + std::to_wstring(state.failed) + L" failed";
    }
    const float countersWidth = counters.empty() ? 0.0f : TextWidth(graphics, counters, detailFont) + 12.0f * s;
    DrawText(graphics, status, statusFont, Gdiplus::Color(255, 240, 242, 245), left, statusY,
             width - countersWidth);
    if (!state.currentMod.empty()) {
        const float statusWidth = (std::min)(TextWidth(graphics, status, statusFont), width - countersWidth);
        DrawText(graphics, state.currentMod, detailFont, Gdiplus::Color(255, 150, 158, 168),
                 left + statusWidth + 6.0f * s, statusY + 2.0f * s, width - countersWidth - statusWidth - 6.0f * s);
    }
    if (!counters.empty()) {
        DrawText(graphics, counters, detailFont,
                 state.failed > 0 ? Gdiplus::Color(255, 239, 110, 110) : Gdiplus::Color(255, 150, 158, 168), right,
                 statusY + 2.0f * s, countersWidth, true);
    }

    // Progress track, eased fill and a slow sheen while a step is waiting.
    const float barY = statusY + 30.0f * s;
    const float barHeight = 4.0f * s;
    Gdiplus::SolidBrush track(Gdiplus::Color(255, 34, 38, 44));
    FillRounded(graphics, track, Gdiplus::RectF(left, barY, width, barHeight), barHeight / 2.0f);
    const float fillWidth = (std::max)(barHeight, width * ClampProgress(g_shownProgress));
    const Gdiplus::RectF fill(left, barY, fillWidth, barHeight);
    Gdiplus::LinearGradientBrush fillBrush(Gdiplus::RectF(left - 1.0f, barY, fillWidth + 2.0f, barHeight),
                                           WithAlpha(state.accent, 200), WithAlpha(state.accent, 255),
                                           Gdiplus::LinearGradientModeHorizontal);
    FillRounded(graphics, fillBrush, fill, barHeight / 2.0f);
    if (now - state.lastProgressTick > 1200 && g_shownProgress < 0.995f) {
        const float phase = static_cast<float>(now % 1800) / 1800.0f;
        const float sheenWidth = 80.0f * s;
        const float sheenX = left - sheenWidth + (fillWidth + sheenWidth) * phase;
        Gdiplus::Region clip(fill);
        graphics.SetClip(&clip);
        Gdiplus::LinearGradientBrush sheen(Gdiplus::RectF(sheenX, barY, sheenWidth, barHeight),
                                           Gdiplus::Color(0, 255, 255, 255), Gdiplus::Color(0, 255, 255, 255),
                                           Gdiplus::LinearGradientModeHorizontal);
        Gdiplus::Color colours[] = {Gdiplus::Color(0, 255, 255, 255), Gdiplus::Color(120, 255, 255, 255),
                                    Gdiplus::Color(0, 255, 255, 255)};
        Gdiplus::REAL positions[] = {0.0f, 0.5f, 1.0f};
        sheen.SetInterpolationColors(colours, positions, 3);
        graphics.FillRectangle(&sheen, sheenX, barY, sheenWidth, barHeight);
        graphics.ResetClip();
    }

    // Context line: game, backend, release.
    const float detailY = barY + 14.0f * s;
    std::wstring context = state.game.empty() ? L"Unknown game" : state.game;
    if (!state.backend.empty())
        context += L"  ·  " + state.backend;
    const std::wstring percent = std::to_wstring(static_cast<int>(std::lround(ClampProgress(g_shownProgress) * 100.0f))) + L"%";
    const std::wstring release = L"URKit " + Widen(std::string(UrkVersion::kReleaseVersion));
    const float rightWidth = TextWidth(graphics, release + L"    " + percent, smallFont) + 8.0f * s;
    DrawText(graphics, context, smallFont, Gdiplus::Color(255, 120, 128, 138), left, detailY, width - rightWidth);
    DrawText(graphics, percent, smallFont, Gdiplus::Color(255, 190, 196, 204), right, detailY,
             TextWidth(graphics, percent, smallFont) + 4.0f * s, true);
    DrawText(graphics, release, smallFont, Gdiplus::Color(255, 92, 100, 110),
             right - TextWidth(graphics, percent, smallFont) - 14.0f * s, detailY, rightWidth, true);
}

BYTE WindowAlpha(DWORD now) {
    float alpha = 1.0f;
    if (g_shownAt)
        alpha = (std::min)(alpha, static_cast<float>(now - g_shownAt) / kFadeInMs);
    if (g_closeAt)
        alpha = (std::min)(alpha, 1.0f - static_cast<float>(now - g_closeAt) / kFadeOutMs);
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    // Ease out so the fade does not look linear.
    alpha = 1.0f - (1.0f - alpha) * (1.0f - alpha);
    return static_cast<BYTE>(alpha * 255.0f);
}

void PresentFrame(HWND window) {
    if (!g_surface.dc || !g_gdiplusToken)
        return;
    const DWORD now = GetTickCount();
    const Snapshot state = TakeSnapshot();
    // Ease toward the reported progress instead of jumping.
    const float target = ClampProgress(state.progress);
    g_shownProgress += (target - g_shownProgress) * 0.18f;
    if (std::fabs(target - g_shownProgress) < 0.001f)
        g_shownProgress = target;

    {
        Gdiplus::Bitmap canvas(g_surface.width, g_surface.height, g_surface.width * 4, PixelFormat32bppPARGB,
                               static_cast<BYTE *>(g_surface.bits));
        Gdiplus::Graphics graphics(&canvas);
        RenderFrame(graphics, state, now);
        graphics.Flush(Gdiplus::FlushIntentionSync);
    }

    RECT rect{};
    GetWindowRect(window, &rect);
    POINT position{rect.left, rect.top};
    SIZE size{g_surface.width, g_surface.height};
    POINT source{0, 0};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, WindowAlpha(now), AC_SRC_ALPHA};
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(window, screen, &position, &size, g_surface.dc, &source, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
}

LRESULT CALLBACK IntroProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    switch (message) {
        case kRefresh:
            return 0;
        case WM_TIMER:
            if (wp == kFrameTimer) {
                const DWORD now = GetTickCount();
                if (now - g_lastTopmost >= kTopmostRefreshMs) {
                    g_lastTopmost = now;
                    KeepIntroOnTop(window);
                }
                PresentFrame(window);
                if (g_closeAt && now - g_closeAt >= kFadeOutMs)
                    DestroyWindow(window);
            }
            return 0;
        case kBeginClose:
        case WM_CLOSE:
            if (!g_closeAt) {
                g_closing = true;
                g_closeAt = GetTickCount();
            }
            return 0;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_DESTROY:
            g_closing = true;
            KillTimer(window, kFrameTimer);
            g_window = nullptr;
            ReleaseGraphics();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(window, message, wp, lp);
    }
}

DWORD WINAPI IntroThread(void *) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = IntroProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"URKIntroWindow";
    RegisterClassW(&wc);

    HDC screen = GetDC(nullptr);
    const int dpi = screen ? GetDeviceCaps(screen, LOGPIXELSX) : 96;
    if (screen)
        ReleaseDC(nullptr, screen);
    g_scale = std::clamp(static_cast<float>(dpi) / 96.0f, 1.0f, 3.0f);

    LoadSplashImage();
    const int width = static_cast<int>(std::lround((kCardWidth + kShadowMargin * 2) * g_scale));
    const int height = static_cast<int>(std::lround((kCardHeight + kShadowMargin * 2) * g_scale));
    RECT work{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + ((work.right - work.left) - width) / 2;
    const int y = work.top + ((work.bottom - work.top) - height) / 2;

    if (!g_gdiplusToken || !CreateSurface(width, height)) {
        ReleaseGraphics();
        if (g_readyEvent)
            SetEvent(g_readyEvent);
        return 0;
    }
    g_window = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_APPWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                               L"URKit", WS_POPUP, x, y, width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_window) {
        ReleaseGraphics();
        if (g_readyEvent)
            SetEvent(g_readyEvent);
        return 0;
    }

    g_shownAt = GetTickCount();
    g_closeAt = 0;
    g_shownProgress = 0.0f;
    PresentFrame(g_window);
    ShowWindow(g_window, SW_SHOWNOACTIVATE);
    SetTimer(g_window, kFrameTimer, kFrameMs, nullptr);
    KeepIntroOnTop(g_window);

    if (g_readyEvent)
        SetEvent(g_readyEvent);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void PostRefresh() {
    if (g_window)
        PostMessageW(g_window, kRefresh, 0, 0);
}

} // namespace

namespace Intro {
void Show(const std::string & /*title*/, const std::string & /*subtitle*/, const std::string &game,
          const std::string &backend, int r, int g, int b, int /*maximumVisibleMs*/) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_game = game.empty() ? "Unknown" : game;
        g_backend = backend.empty() ? "Auto" : backend;
        g_status = "Preparing runtime and mods";
        g_currentMod.clear();
        g_loaded = 0;
        g_failed = 0;
        g_discovered = 0;
        g_progress = 0.03f;
        g_lastProgressTick = GetTickCount();
        g_accent = RGB(static_cast<BYTE>(std::clamp(r, 0, 255)), static_cast<BYTE>(std::clamp(g, 0, 255)),
                       static_cast<BYTE>(std::clamp(b, 0, 255)));
    }

    g_closing = false;
    if (g_readyEvent) {
        CloseHandle(g_readyEvent);
        g_readyEvent = nullptr;
    }
    g_readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, IntroThread, nullptr, 0, &g_threadId);
    if (g_readyEvent)
        WaitForSingleObject(g_readyEvent, 1000);
}

void Status(const std::string &text) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_status = text;
    }
    PostRefresh();
}

void Progress(float value, const std::string &status) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        AdvanceProgressLocked(value);
        if (!status.empty())
            g_status = status;
    }
    PostRefresh();
}

void Backend(const std::string &backend) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_backend = backend;
    }
    PostRefresh();
}

void ModProgress(const std::string &modName, int discovered, int loaded, int failed, bool loading,
                 const std::string &finalStatus) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_currentMod = modName;
        g_discovered = discovered;
        g_loaded = loaded;
        g_failed = failed;
        if (!finalStatus.empty())
            g_status = finalStatus;
        AdvanceProgressLocked(ProgressForMods(discovered, loaded, failed, loading));
    }
    PostRefresh();
}

void Close() {
    if (g_window)
        PostMessageW(g_window, kBeginClose, 0, 0);
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = nullptr;
    }
    if (g_readyEvent) {
        CloseHandle(g_readyEvent);
        g_readyEvent = nullptr;
    }
}
} // namespace Intro
