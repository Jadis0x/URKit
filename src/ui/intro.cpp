#include "intro.h"
#include "intro_splash_embedded.h"
#include <objidl.h>
#include <wincrypt.h>
#include <windows.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <gdiplus.h>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>

namespace {
HWND g_window = nullptr;
HWND g_gameWindow = nullptr;
bool g_gameWindowMinimizedByIntro = false;
bool g_closing = false;
HANDLE g_thread = nullptr;
HANDLE g_readyEvent = nullptr;
DWORD g_threadId = 0;
std::string g_title = "URKit";
std::string g_subtitle = "by Jadis0x";
std::string g_status = "Starting...";
std::string g_game = "Unknown";
std::string g_backend = "Auto";
std::string g_currentMod;
int g_loaded = 0;
int g_failed = 0;
int g_discovered = 0;
float g_progress = 0.03f;
COLORREF g_accent = RGB(250, 250, 250);
std::mutex g_mutex;
constexpr UINT kRefresh = WM_APP + 41;
constexpr UINT_PTR kAnimationTimer = 1;
constexpr UINT kFocusRefreshMs = 100;
constexpr float kProgressModsStart = 0.72f;
constexpr float kProgressModsEnd = 0.96f;
constexpr int kIntroWindowWidth = 750;
constexpr int kIntroWindowHeight = 281;
ULONG_PTR g_gdiplusToken = 0;
Gdiplus::Image *g_splashImage = nullptr;
IStream *g_splashStream = nullptr;

struct WindowSearch {
    DWORD pid = 0;
    HWND hwnd = nullptr;
};

int RectWidth(const RECT &rect) {
    return static_cast<int>(rect.right - rect.left);
}

int RectHeight(const RECT &rect) {
    return static_cast<int>(rect.bottom - rect.top);
}

float ClampProgress(float value) {
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

void AdvanceProgressLocked(float value) {
    const float clamped = ClampProgress(value);
    if (clamped > g_progress)
        g_progress = clamped;
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

void ReleaseSplashImage() {
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
        decodedSize == 0) {
        return nullptr;
    }

    std::vector<BYTE> decoded(decodedSize);
    if (!CryptStringToBinaryA(IntroSplashEmbedded::kImageBase64, 0, CRYPT_STRING_BASE64, decoded.data(), &decodedSize,
                              nullptr, nullptr)) {
        return nullptr;
    }
    decoded.resize(decodedSize);
    return LoadImageFromBytes(decoded);
}

void LoadSplashImage() {
    ReleaseSplashImage();
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

bool HasSplashImage() {
    return g_splashImage && g_splashImage->GetLastStatus() == Gdiplus::Ok && g_splashImage->GetWidth() > 0 &&
           g_splashImage->GetHeight() > 0;
}

BOOL CALLBACK FindGameWindowProc(HWND hwnd, LPARAM param) {
    auto *search = reinterpret_cast<WindowSearch *>(param);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != search->pid)
        return TRUE;
    if (!IsWindowVisible(hwnd))
        return TRUE;
    if (GetWindow(hwnd, GW_OWNER))
        return TRUE;
    if (hwnd == g_window)
        return TRUE;

    char className[128]{};
    GetClassNameA(hwnd, className, sizeof(className));
    if (std::strcmp(className, "URKIntroWindow") == 0)
        return TRUE;

    const LONG_PTR exStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW)
        return TRUE;

    char title[256]{};
    GetWindowTextA(hwnd, title, sizeof(title));
    if (!title[0])
        return TRUE;

    search->hwnd = hwnd;
    return FALSE;
}

HWND FindMainGameWindow() {
    WindowSearch search{};
    search.pid = GetCurrentProcessId();
    EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&search));
    return search.hwnd;
}

void MinimizeGameWindowIfNeeded() {
    HWND gameWindow = FindMainGameWindow();
    if (!gameWindow || !IsWindow(gameWindow))
        return;

    g_gameWindow = gameWindow;
    if (!IsIconic(gameWindow)) {
        ShowWindowAsync(gameWindow, SW_MINIMIZE);
        g_gameWindowMinimizedByIntro = true;
    }
}

void KeepIntroAsStartupSurface(HWND window) {
    if (!window || g_closing)
        return;

    MinimizeGameWindowIfNeeded();

    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
    BringWindowToTop(window);

    if (GetForegroundWindow() != window)
        SetForegroundWindow(window);
}

void RestoreGameWindow() {
    const HWND gameWindow = g_gameWindow;
    const bool shouldRestore = g_gameWindowMinimizedByIntro;
    g_gameWindow = nullptr;
    g_gameWindowMinimizedByIntro = false;

    if (shouldRestore && gameWindow && IsWindow(gameWindow)) {
        ShowWindowAsync(gameWindow, SW_RESTORE);
        SetForegroundWindow(gameWindow);
    }
}

void PaintFallbackBackground(HDC dc, const RECT &rect) {
    HBRUSH background = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(dc, &rect, background);
    DeleteObject(background);
}

void PaintSplashBackground(HDC dc, const RECT &rect) {
    if (!HasSplashImage()) {
        PaintFallbackBackground(dc, rect);
        return;
    }

    const int width = RectWidth(rect);
    const int height = RectHeight(rect);

    Gdiplus::Graphics graphics(dc);
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    graphics.DrawImage(g_splashImage, static_cast<INT>(rect.left), static_cast<INT>(rect.top), static_cast<INT>(width),
                       static_cast<INT>(height));
}

void PaintProgressPanel(HDC dc, const RECT &panel) {
    if (!HasSplashImage())
        return;

    Gdiplus::Graphics graphics(dc);
    Gdiplus::SolidBrush panelBrush(Gdiplus::Color(150, 0, 0, 0));
    graphics.FillRectangle(&panelBrush, static_cast<INT>(panel.left), static_cast<INT>(panel.top),
                           static_cast<INT>(panel.right - panel.left), static_cast<INT>(panel.bottom - panel.top));
}

void PaintCard(HDC dc, const RECT &rect) {
    PaintSplashBackground(dc, rect);

    const bool hasSplashImage = HasSplashImage();
    const int margin = 28;
    const int progressWidth = (std::min)(260, (std::max)(120, RectWidth(rect) - (margin * 2)));
    RECT progressBg{static_cast<LONG>(margin), static_cast<LONG>(rect.bottom - 38),
                    static_cast<LONG>(margin + progressWidth), static_cast<LONG>(rect.bottom - 31)};

    std::string title;
    std::string subtitle;
    std::string status;
    std::string currentMod;
    int discovered = 0;
    int loaded = 0;
    int failed = 0;
    float progress = 0.0f;
    COLORREF accent = RGB(0, 168, 232);
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        title = g_title;
        subtitle = g_subtitle;
        status = g_status;
        currentMod = g_currentMod;
        discovered = g_discovered;
        loaded = g_loaded;
        failed = g_failed;
        progress = g_progress;
        accent = g_accent;
    }

    if (status.empty())
        status = "Preparing runtime and mods";
    if (!currentMod.empty())
        status += "  " + currentMod;

    RECT panel{static_cast<LONG>(margin - 10), static_cast<LONG>(progressBg.top - 36),
               static_cast<LONG>(progressBg.right + 10), static_cast<LONG>(progressBg.bottom + 12)};
    PaintProgressPanel(dc, panel);

    HBRUSH progressBack = CreateSolidBrush(hasSplashImage ? RGB(20, 24, 30) : RGB(10, 18, 24));
    FillRect(dc, &progressBg, progressBack);
    DeleteObject(progressBack);

    const int filledWidth = static_cast<int>(RectWidth(progressBg) * ClampProgress(progress));
    if (filledWidth > 0) {
        RECT progressFill{progressBg.left, progressBg.top, static_cast<LONG>(progressBg.left + filledWidth),
                          progressBg.bottom};
        HBRUSH accentBrush = CreateSolidBrush(accent);
        FillRect(dc, &progressFill, accentBrush);
        DeleteObject(accentBrush);
    }

    SetBkMode(dc, TRANSPARENT);

    HFONT titleFont = CreateFontA(32, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT authorFont = CreateFontA(15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT statusFont = CreateFontA(14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");
    HFONT smallFont = CreateFontA(12, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, "Segoe UI");

    HGDIOBJ oldFont = SelectObject(dc, titleFont);

    if (!hasSplashImage) {
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, margin, 24, title.c_str(), static_cast<int>(title.size()));

        SelectObject(dc, authorFont);
        SetTextColor(dc, RGB(145, 145, 145));
        TextOutA(dc, margin + 152, 58, subtitle.c_str(), static_cast<int>(subtitle.size()));
    }

    SelectObject(dc, statusFont);
    SetTextColor(dc, RGB(245, 245, 245));
    TextOutA(dc, margin, static_cast<int>(progressBg.top - 24), status.c_str(), static_cast<int>(status.size()));

    if (discovered > 0 || loaded > 0 || failed > 0) {
        SelectObject(dc, smallFont);
        SetTextColor(dc, RGB(165, 165, 165));
        const std::string modStats = std::to_string(discovered) + " discovered / " + std::to_string(loaded) +
                                     " loaded / " + std::to_string(failed) + " failed";
        TextOutA(dc, margin, static_cast<int>(progressBg.bottom + 7), modStats.c_str(),
                 static_cast<int>(modStats.size()));
    }

    SelectObject(dc, oldFont);
    DeleteObject(titleFont);
    DeleteObject(authorFont);
    DeleteObject(statusFont);
    DeleteObject(smallFont);
}

LRESULT CALLBACK IntroProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(window, &paint);
        RECT rect{};
        GetClientRect(window, &rect);
        PaintCard(dc, rect);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == kRefresh) {
        InvalidateRect(window, nullptr, FALSE);
        KeepIntroAsStartupSurface(window);
        return 0;
    }
    if (message == WM_TIMER && wp == kAnimationTimer) {
        KeepIntroAsStartupSurface(window);
        InvalidateRect(window, nullptr, FALSE);
        return 0;
    }
    if (message == WM_CLOSE) {
        g_closing = true;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        g_closing = true;
        KillTimer(window, kAnimationTimer);
        g_window = nullptr;
        RestoreGameWindow();
        ReleaseSplashImage();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wp, lp);
}

DWORD WINAPI IntroThread(void *) {
    WNDCLASSA wc{};
    wc.lpfnWndProc = IntroProc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = "URKIntroWindow";
    RegisterClassA(&wc);

    LoadSplashImage();
    MinimizeGameWindowIfNeeded();

    const int width = kIntroWindowWidth;
    const int height = kIntroWindowHeight;
    const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    g_window = CreateWindowExA(WS_EX_TOPMOST | WS_EX_APPWINDOW, wc.lpszClassName, g_title.c_str(), WS_POPUP, x, y,
                               width, height, nullptr, nullptr, wc.hInstance, nullptr);
    if (!g_window) {
        RestoreGameWindow();
        ReleaseSplashImage();
        if (g_readyEvent)
            SetEvent(g_readyEvent);
        return 0;
    }

    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, 12, 12);
    if (region)
        SetWindowRgn(g_window, region, TRUE);
    SetTimer(g_window, kAnimationTimer, kFocusRefreshMs * 5, nullptr);
    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);
    SetWindowPos(g_window, HWND_TOPMOST, x, y, width, height, SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
    BringWindowToTop(g_window);
    SetForegroundWindow(g_window);

    if (g_readyEvent)
        SetEvent(g_readyEvent);

    MSG msg{};
    while (GetMessageA(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return 0;
}
} // namespace

namespace Intro {
void Show(const std::string & /*title*/, const std::string & /*subtitle*/, const std::string &game,
          const std::string &backend, int r, int g, int b, int /*maximumVisibleMs*/) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_title = "URKit";
        g_subtitle = "by Jadis0x";
        g_game = game.empty() ? "Unknown" : game;
        g_backend = backend.empty() ? "Auto" : backend;
        g_status = "Preparing runtime and mods";
        g_currentMod.clear();
        g_loaded = 0;
        g_failed = 0;
        g_discovered = 0;
        g_progress = 0.03f;
        g_accent = RGB(static_cast<BYTE>(std::clamp(r, 0, 255)), static_cast<BYTE>(std::clamp(g, 0, 255)),
                       static_cast<BYTE>(std::clamp(b, 0, 255)));
    }

    g_gameWindow = nullptr;
    g_gameWindowMinimizedByIntro = false;
    g_closing = false;

    if (g_readyEvent) {
        CloseHandle(g_readyEvent);
        g_readyEvent = nullptr;
    }
    g_readyEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
    g_thread = CreateThread(nullptr, 0, IntroThread, nullptr, 0, &g_threadId);

    if (g_readyEvent)
        WaitForSingleObject(g_readyEvent, 1000);
}

void Status(const std::string &text) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_status = text;
    }
    if (g_window)
        PostMessageA(g_window, kRefresh, 0, 0);
}

void Progress(float value, const std::string &status) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        AdvanceProgressLocked(value);
        if (!status.empty())
            g_status = status;
    }
    if (g_window)
        PostMessageA(g_window, kRefresh, 0, 0);
}

void Backend(const std::string &backend) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_backend = backend;
    }
    if (g_window)
        PostMessageA(g_window, kRefresh, 0, 0);
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
    if (g_window)
        PostMessageA(g_window, kRefresh, 0, 0);
}

void Close() {
    if (g_window)
        PostMessageA(g_window, WM_CLOSE, 0, 0);
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
