#include "il2cpp_sdk_generator.h"
#include "mod_sdk.h"
#include "mod_project_generator_common.h"
#include "mono_sdk_generator.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <shellapi.h>

#include "../win32_tool_ui.h"
#endif

namespace {

namespace fs = std::filesystem;

struct Options {
    std::string backendSelection = "auto";
    std::string backend = "mono";
    std::string sdkOut;
    std::string projectOut;
    std::string includeRoot;
    std::string projectName = "GeneratedMod";
    std::string exportRoot;
    std::string gameExePath;
    std::string gameDir;
    std::string modsDir = "Mods";
    bool enableLocalization = false;
};

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string TrimPath(std::string value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) { return std::isspace(ch); })
                          .base();
    if (first >= last)
        return {};

    value.assign(first, last);
    if (value.size() >= 2 &&
        ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
        return value.substr(1, value.size() - 2);
    return value;
}

bool IsIl2CppBackend(const Options &options) {
    return Lower(options.backend) == "il2cpp";
}

bool IsValidBackend(const std::string &backend) {
    const std::string value = Lower(backend);
    return value == "mono" || value == "il2cpp";
}

bool IsValidBackendSelection(const std::string &backend) {
    const std::string value = Lower(backend);
    return value == "auto" || value == "mono" || value == "il2cpp";
}

const char *BackendDisplayName(const Options &options) {
    return IsIl2CppBackend(options) ? "IL2CPP" : "Mono";
}

fs::path AbsolutePath(const std::string &text) {
    std::error_code ec;
    fs::path path = fs::path(TrimPath(text));
    fs::path absolute = fs::absolute(path, ec);
    return ec ? path : absolute.lexically_normal();
}

bool PathExists(const fs::path &path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

std::optional<std::string> DetectBackendFromGameDir(const fs::path &gameDir) {
    if (PathExists(gameDir / "GameAssembly.dll"))
        return "il2cpp";

    for (const fs::path &candidate : {gameDir / "MonoBleedingEdge" / "EmbedRuntime" / "mono-2.0-bdwgc.dll",
                                      gameDir / "Mono" / "EmbedRuntime" / "mono.dll", gameDir / "mono-2.0-bdwgc.dll",
                                      gameDir / "mono-2.0-sgen.dll", gameDir / "mono-2.0.dll", gameDir / "mono.dll"}) {
        if (PathExists(candidate))
            return "mono";
    }

    return std::nullopt;
}

bool FillDerivedProjectPaths(Options &options, std::string &error) {
    options.backendSelection = Lower(options.backendSelection);
    if (!IsValidBackendSelection(options.backendSelection)) {
        error = "Backend selection must be 'auto', 'mono', or 'il2cpp'.";
        return false;
    }

    options.gameExePath = TrimPath(options.gameExePath);
    if (options.gameExePath.empty()) {
        error = "Target game executable is required.";
        return false;
    }
    options.gameExePath = AbsolutePath(options.gameExePath).string();
    const fs::path gameExePath = fs::path(options.gameExePath);
    if (!PathExists(gameExePath)) {
        error = "Target game executable was not found: " + options.gameExePath;
        return false;
    }
    if (Lower(gameExePath.extension().string()) != ".exe") {
        error = "Target path must point to a game .exe file: " + options.gameExePath;
        return false;
    }

    options.gameDir = gameExePath.parent_path().lexically_normal().string();
    if (options.gameDir.empty()) {
        error = "Could not resolve the target game directory from: " + options.gameExePath;
        return false;
    }

    if (options.backendSelection == "auto") {
        const std::optional<std::string> detectedBackend = DetectBackendFromGameDir(fs::path(options.gameDir));
        if (!detectedBackend) {
            error = "Could not detect the runtime backend from the target game directory. Expected GameAssembly.dll "
                    "for IL2CPP or Mono runtime files for Mono.";
            return false;
        }
        options.backend = *detectedBackend;
    } else {
        options.backend = options.backendSelection;
    }
    if (!IsValidBackend(options.backend)) {
        error = "Resolved backend must be 'mono' or 'il2cpp'.";
        return false;
    }

    options.projectName = ModProjectGenerator::Identifier(
        options.projectName, IsIl2CppBackend(options) ? "GeneratedIl2CppMod" : "GeneratedMod");
    options.exportRoot = (fs::path(options.gameDir) / "urk-sdk-output").lexically_normal().string();
    const fs::path exportRoot = fs::path(options.exportRoot);

    options.projectOut = (exportRoot / options.projectName / "project").lexically_normal().string();
    options.sdkOut = (fs::path(options.projectOut) / "sdk" / (IsIl2CppBackend(options) ? "il2cpp" : "mono")).string();
    options.includeRoot.clear();
    options.modsDir = "Mods";
    return true;
}

bool GenerateSelectedProject(const Options &options, std::string &error) {
    const std::string reportDetails = "Mono and IL2CPP generated projects use runtime API helpers. "
                                      "No offline metadata or dump-generated modules are emitted.\n";

    if (IsIl2CppBackend(options)) {
        if (!Il2CppSdkGenerator::Generate(options.sdkOut, reportDetails, &error))
            return false;
        return Il2CppSdkGenerator::GenerateModProject(options.projectOut, options.sdkOut, options.includeRoot,
                                                      options.projectName, options.gameDir, options.modsDir,
                                                      options.enableLocalization, &error);
    }

    if (!MonoSdkGenerator::Generate(options.sdkOut, reportDetails, &error))
        return false;
    return MonoSdkGenerator::GenerateModProject(options.projectOut, options.sdkOut, options.includeRoot,
                                                options.projectName, options.gameDir, options.modsDir,
                                                options.enableLocalization, &error);
}

#ifdef _WIN32

constexpr int kWindowWidth = 760;
constexpr int kWindowHeight = 590;

constexpr int kIdBackendMono = 1001;
constexpr int kIdProjectName = 1002;
constexpr int kIdGenerate = 1003;
constexpr int kIdGitHubProfile = 1004;
constexpr int kIdCoffee = 1005;
constexpr int kIdBackendIl2Cpp = 1007;
constexpr int kIdBrowseGameExe = 1008;
constexpr int kIdTabs = 1009;
constexpr int kIdProjectRepo = 1010;
constexpr int kIdBackendAuto = 1011;
constexpr int kIdLocalization = 1012;

constexpr const wchar_t *kProjectRepoUrl = L"https://github.com/Jadis0x/URKit";
constexpr const wchar_t *kGitHubProfileUrl = L"https://github.com/Jadis0x";
constexpr const wchar_t *kCoffeeUrl = L"https://www.buymeacoffee.com/Jadis0x";

std::optional<Options> ParseCommandLineOptions(std::string &error) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        error = "CommandLineToArgvW failed.";
        return std::nullopt;
    }

    struct ArgvGuard {
        wchar_t **argv = nullptr;
        ~ArgvGuard() {
            if (argv)
                LocalFree(argv);
        }
    } guard{argv};

    if (argc <= 1)
        return std::nullopt;

    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string key = Lower(URK::ToolUi::WideToUtf8(argv[i]));
        auto requireValue = [&](const char *name) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                error = std::string("Missing value for ") + name + ".";
                return std::nullopt;
            }
            ++i;
            return URK::ToolUi::WideToUtf8(argv[i]);
        };

        if (key == "--backend" || key == "-b") {
            std::optional<std::string> value = requireValue("--backend");
            if (!value)
                return std::nullopt;
            options.backendSelection = *value;
        } else if (key == "--name" || key == "--project-name" || key == "-n") {
            std::optional<std::string> value = requireValue("--name");
            if (!value)
                return std::nullopt;
            options.projectName = *value;
        } else if (key == "--game-exe" || key == "--exe" || key == "-g") {
            std::optional<std::string> value = requireValue("--game-exe");
            if (!value)
                return std::nullopt;
            options.gameExePath = *value;
        } else if (key == "--localization") {
            options.enableLocalization = true;
        } else if (key == "--help" || key == "-h" || key == "/?") {
            error =
                "Usage: urk-sdk.exe --game-exe C:\\path\\to\\Game.exe --backend auto|mono|il2cpp --name "
                "ProjectName [--localization]";
            return std::nullopt;
        } else {
            error = "Unknown argument: " + key;
            return std::nullopt;
        }
    }

    if (options.gameExePath.empty()) {
        error = "Missing value for --game-exe.";
        return std::nullopt;
    }

    if (!FillDerivedProjectPaths(options, error))
        return std::nullopt;
    return options;
}

int RunCommandLineGeneration(const Options &options) {
    std::string error;
    try {
        if (GenerateSelectedProject(options, error))
            return 0;
    } catch (const std::exception &ex) {
        error = ex.what();
    }

    std::fprintf(stderr, "URKit SDK generation failed: %s\n", error.c_str());
    return 1;
}

class SdkGeneratorWindow {
  public:
    int Run(HINSTANCE instance) {
        instance_ = instance;
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_TAB_CLASSES;
        InitCommonControlsEx(&controls);

        font_ = URK::ToolUi::CreateUiFont(13);
        logFont_ = URK::ToolUi::CreateUiFont(12);
        titleFont_ = URK::ToolUi::CreateUiFont(19, FW_SEMIBOLD);
        sectionFont_ = URK::ToolUi::CreateUiFont(14, FW_SEMIBOLD);
        whiteBrush_ = CreateSolidBrush(URK::ToolUi::kPalette.surface);
        logBrush_ = CreateSolidBrush(URK::ToolUi::kPalette.surfaceMuted);
        canvasBrush_ = CreateSolidBrush(URK::ToolUi::kPalette.canvas);
        brandBrush_ = CreateSolidBrush(URK::ToolUi::kPalette.brand);

        WNDCLASSW wc{};
        wc.lpfnWndProc = &SdkGeneratorWindow::WndProc;
        wc.hInstance = instance_;
        wc.lpszClassName = L"URKitSdkGeneratorWindow";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = canvasBrush_;
        wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassW(&wc);

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"URKit SDK Generator",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                                kWindowWidth, kWindowHeight, nullptr, nullptr, instance_, this);
        if (!hwnd_)
            return 1;

        URK::ToolUi::ApplyModernWindowFrame(hwnd_);

        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (font_)
            DeleteObject(font_);
        if (logFont_)
            DeleteObject(logFont_);
        if (titleFont_)
            DeleteObject(titleFont_);
        if (sectionFont_)
            DeleteObject(sectionFont_);
        if (whiteBrush_)
            DeleteObject(whiteBrush_);
        if (logBrush_)
            DeleteObject(logBrush_);
        if (canvasBrush_)
            DeleteObject(canvasBrush_);
        if (brandBrush_)
            DeleteObject(brandBrush_);
        return static_cast<int>(msg.wParam);
    }

  private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        SdkGeneratorWindow *self = nullptr;
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
            self = static_cast<SdkGeneratorWindow *>(create->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = hwnd;
        } else {
            self = reinterpret_cast<SdkGeneratorWindow *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        }

        if (!self)
            return DefWindowProcW(hwnd, message, wParam, lParam);
        return self->HandleMessage(message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
            case WM_CREATE:
                DragAcceptFiles(hwnd_, TRUE);
                CreateControls();
                RefreshDerivedPaths(false);
                AddLog("Choose a backend mode, project name, and target game executable.");
                return 0;
            case WM_COMMAND:
                return HandleCommand(LOWORD(wParam), HIWORD(wParam));
            case WM_NOTIFY:
                return HandleNotify(reinterpret_cast<NMHDR *>(lParam));
            case WM_DRAWITEM:
                return DrawControl(*reinterpret_cast<DRAWITEMSTRUCT *>(lParam));
            case WM_DROPFILES:
                HandleDrop(reinterpret_cast<HDROP>(wParam));
                return 0;
            case WM_CTLCOLORSTATIC:
                if (reinterpret_cast<HWND>(lParam) == logEdit_) {
                    SetTextColor(reinterpret_cast<HDC>(wParam), URK::ToolUi::kPalette.text);
                    SetBkColor(reinterpret_cast<HDC>(wParam), URK::ToolUi::kPalette.surfaceMuted);
                    return reinterpret_cast<LRESULT>(logBrush_);
                }
                if (reinterpret_cast<HWND>(lParam) == title_ || reinterpret_cast<HWND>(lParam) == subtitle_) {
                    SetTextColor(reinterpret_cast<HDC>(wParam),
                                 reinterpret_cast<HWND>(lParam) == title_ ? RGB(255, 255, 255) : RGB(192, 192, 192));
                    SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
                    return reinterpret_cast<LRESULT>(brandBrush_);
                }
                if (IsLinkControl(reinterpret_cast<HWND>(lParam))) {
                    SetTextColor(reinterpret_cast<HDC>(wParam), URK::ToolUi::kPalette.accent);
                    SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
                    return reinterpret_cast<LRESULT>(whiteBrush_);
                }
                SetTextColor(reinterpret_cast<HDC>(wParam), URK::ToolUi::kPalette.text);
                SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
                return reinterpret_cast<LRESULT>(whiteBrush_);
            case WM_CTLCOLOREDIT:
                if (reinterpret_cast<HWND>(lParam) == logEdit_) {
                    SetTextColor(reinterpret_cast<HDC>(wParam), RGB(32, 38, 46));
                    SetBkColor(reinterpret_cast<HDC>(wParam), RGB(248, 250, 252));
                    return reinterpret_cast<LRESULT>(logBrush_);
                }
                break;
            case WM_SETCURSOR:
                if (IsLinkControl(reinterpret_cast<HWND>(wParam))) {
                    SetCursor(LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            case WM_PAINT:
                PaintPanels();
                return 0;
            case WM_CLOSE:
                DestroyWindow(hwnd_);
                return 0;
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd_, message, wParam, lParam);
    }

    LRESULT HandleNotify(const NMHDR *header) {
        if (!header || header->idFrom != kIdTabs || header->code != TCN_SELCHANGE)
            return 0;
        activeTab_ = static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
        ApplyTabVisibility();
        InvalidateRect(tab_, nullptr, TRUE);
        return 0;
    }

    LRESULT DrawControl(const DRAWITEMSTRUCT &item) {
        if (item.CtlType == ODT_BUTTON) {
            URK::ToolUi::DrawButton(item, font_, item.CtlID == kIdGenerate);
            return TRUE;
        }
        if (item.CtlType == ODT_TAB && item.hwndItem == tab_) {
            RECT rect = item.rcItem;
            const int selectedTab = static_cast<int>(SendMessageW(tab_, TCM_GETCURSEL, 0, 0));
            const bool selected = static_cast<int>(item.itemID) == selectedTab;
            URK::ToolUi::Fill(item.hDC, rect, selected ? URK::ToolUi::kPalette.surface : URK::ToolUi::kPalette.canvas);
            if (selected) {
                RECT accent{rect.left + 18, rect.bottom - 3, rect.right - 18, rect.bottom};
                URK::ToolUi::Fill(item.hDC, accent, URK::ToolUi::kPalette.accent);
            }
            wchar_t text[64]{};
            TCITEMW tabItem{};
            tabItem.mask = TCIF_TEXT;
            tabItem.pszText = text;
            tabItem.cchTextMax = static_cast<int>(std::size(text));
            SendMessageW(tab_, TCM_GETITEMW, item.itemID, reinterpret_cast<LPARAM>(&tabItem));
            SetBkMode(item.hDC, TRANSPARENT);
            SetTextColor(item.hDC, selected ? URK::ToolUi::kPalette.text : URK::ToolUi::kPalette.textMuted);
            HGDIOBJ oldFont = SelectObject(item.hDC, font_);
            DrawTextW(item.hDC, text, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(item.hDC, oldFont);
            return TRUE;
        }
        return FALSE;
    }

    LRESULT HandleCommand(int id, int notification) {
        switch (id) {
            case kIdBackendAuto:
                if (notification == BN_CLICKED) {
                    options_.backendSelection = "auto";
                    RefreshDerivedPaths(false);
                }
                return 0;
            case kIdBackendMono:
                if (notification == BN_CLICKED) {
                    options_.backendSelection = "mono";
                    RefreshDerivedPaths(false);
                }
                return 0;
            case kIdBackendIl2Cpp:
                if (notification == BN_CLICKED) {
                    options_.backendSelection = "il2cpp";
                    RefreshDerivedPaths(false);
                }
                return 0;
            case kIdLocalization:
                if (notification == BN_CLICKED)
                    options_.enableLocalization =
                        SendMessageW(localizationCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;
                return 0;
            case kIdBrowseGameExe:
                BrowseGameExecutable();
                return 0;
            case kIdGenerate:
                Generate();
                return 0;
            case kIdGitHubProfile:
                if (notification == STN_CLICKED) {
                    ShellExecuteW(hwnd_, L"open", kGitHubProfileUrl, nullptr, nullptr, SW_SHOWNORMAL);
                }
                return 0;
            case kIdCoffee:
                if (notification == STN_CLICKED) {
                    ShellExecuteW(hwnd_, L"open", kCoffeeUrl, nullptr, nullptr, SW_SHOWNORMAL);
                }
                return 0;
            case kIdProjectRepo:
                if (notification == STN_CLICKED) {
                    ShellExecuteW(hwnd_, L"open", kProjectRepoUrl, nullptr, nullptr, SW_SHOWNORMAL);
                }
                return 0;
            case kIdProjectName:
                if (notification == EN_CHANGE)
                    RefreshDerivedPaths(false);
                return 0;
            default:
                return 0;
        }
    }

    enum class Panel {
        None,
        Generate,
        Support,
    };

    HWND MakeControl(const wchar_t *className, const wchar_t *text, DWORD style, DWORD exStyle, int x, int y, int width,
                     int height, int id = 0, Panel panel = Panel::None) {
        HWND control =
            CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | style, x, y, width,
                            height, hwnd_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), instance_, nullptr);
        if (control && font_)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        if (control)
            URK::ToolUi::ApplyControlTheme(control);
        if (control)
            RegisterPanelControl(control, panel);
        return control;
    }

    void RegisterPanelControl(HWND control, Panel panel) {
        switch (panel) {
            case Panel::Generate:
                generateControls_.push_back(control);
                break;
            case Panel::Support:
                supportControls_.push_back(control);
                break;
            case Panel::None:
                break;
        }
    }

    void ApplyTabVisibility() {
        const bool showGenerate = activeTab_ == 0;
        for (HWND control : generateControls_)
            ShowWindow(control, showGenerate ? SW_SHOW : SW_HIDE);
        for (HWND control : supportControls_)
            ShowWindow(control, showGenerate ? SW_HIDE : SW_SHOW);
        InvalidateRect(panelBackground_, nullptr, TRUE);
        InvalidateRect(hwnd_, nullptr, TRUE);
    }

    void CreateControls() {
        int y = 13;

        title_ = MakeControl(L"STATIC", L"URKit SDK Generator", 0, 0, 72, y, 360, 24);
        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);

        const std::wstring versionText = L"Runtime-only Unity mod SDK | SDK ABI v" +
                                         std::to_wstring(URK_SDK_VERSION) + L" | Runtime v" +
                                         std::to_wstring(URK_RUNTIME_API_VERSION) + L" | Mono v" +
                                         std::to_wstring(URK_MONO_API_VERSION) + L" | IL2CPP v" +
                                         std::to_wstring(URK_IL2CPP_API_VERSION) + L" | Network v" +
                                         std::to_wstring(URK_NETWORK_API_VERSION);
        subtitle_ = MakeControl(L"STATIC", versionText.c_str(), 0, 0, 73, y + 27, 620, 18);

        tab_ = MakeControl(WC_TABCONTROLW, L"", WS_TABSTOP | TCS_FIXEDWIDTH | TCS_OWNERDRAWFIXED, 0, 20, 86, 704, 450,
                           kIdTabs);
        SendMessageW(tab_, TCM_SETITEMSIZE, 0, MAKELPARAM(124, 30));
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t *>(L"Generate");
        SendMessageW(tab_, TCM_INSERTITEMW, 0, reinterpret_cast<LPARAM>(&item));
        item.pszText = const_cast<wchar_t *>(L"Support");
        SendMessageW(tab_, TCM_INSERTITEMW, 1, reinterpret_cast<LPARAM>(&item));

        panelBackground_ = MakeControl(L"STATIC", L"", SS_WHITERECT, 0, 29, 120, 694, 414);

        const int panelX = 42;
        const int panelY = 130;
        const int labelWidth = 112;
        const int editX = panelX + labelWidth;
        const int editWidth = 378;
        const int buttonWidth = 94;
        const int valueWidth = editWidth + buttonWidth + 8;
        const int wideButtonWidth = 132;
        const int logHeight = 66;

        HWND setupTitle = MakeControl(L"STATIC", L"Project Setup", 0, 0, panelX, panelY, 180, 20, 0, Panel::Generate);
        if (setupTitle)
            SendMessageW(setupTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);

        y = panelY + 28;
        MakeControl(L"STATIC", L"Runtime backend", 0, 0, panelX, y + 3, labelWidth, 18, 0, Panel::Generate);
        autoRadio_ = MakeControl(L"BUTTON", L"Auto", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, 0, editX, y, 76, 20,
                                 kIdBackendAuto, Panel::Generate);
        monoRadio_ = MakeControl(L"BUTTON", L"Mono", BS_AUTORADIOBUTTON | WS_TABSTOP, 0, editX + 84, y, 78, 20,
                                 kIdBackendMono, Panel::Generate);
        il2cppRadio_ = MakeControl(L"BUTTON", L"IL2CPP", BS_AUTORADIOBUTTON | WS_TABSTOP, 0, editX + 170, y, 88, 20,
                                   kIdBackendIl2Cpp, Panel::Generate);
        SendMessageW(autoRadio_, BM_SETCHECK, BST_CHECKED, 0);
        y += 32;

        MakeControl(L"STATIC", L"Project name", 0, 0, panelX, y + 4, labelWidth, 18, 0, Panel::Generate);
        projectEdit_ = MakeControl(L"EDIT", L"GeneratedMod", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, editX, y, editWidth, 24,
                                   kIdProjectName, Panel::Generate);
        y += 32;

        MakeControl(L"STATIC", L"Language support", 0, 0, panelX, y + 3, labelWidth, 18, 0, Panel::Generate);
        localizationCheck_ = MakeControl(L"BUTTON", L"Generate selectable JSON locales", BS_AUTOCHECKBOX | WS_TABSTOP,
                                         0, editX, y, 280, 20, kIdLocalization, Panel::Generate);
        y += 32;

        MakeControl(L"STATIC", L"Target game exe", 0, 0, panelX, y + 4, labelWidth, 18, 0, Panel::Generate);
        gameExeEdit_ = MakeControl(L"STATIC", L"", SS_PATHELLIPSIS, 0, editX, y + 4, editWidth, 18, 0, Panel::Generate);
        gameExeButton_ = MakeControl(L"BUTTON", L"Browse...", BS_OWNERDRAW | WS_TABSTOP, 0, editX + editWidth + 8, y,
                                     buttonWidth, 24, kIdBrowseGameExe, Panel::Generate);
        y += 32;

        MakeControl(L"STATIC", L"Export directory", 0, 0, panelX, y + 4, labelWidth, 18, 0, Panel::Generate);
        exportEdit_ = MakeControl(L"STATIC", L"", SS_PATHELLIPSIS, 0, editX, y + 4, valueWidth, 18, 0, Panel::Generate);
        y += 32;

        MakeControl(L"STATIC", L"Project path", 0, 0, panelX, y + 4, labelWidth, 18, 0, Panel::Generate);
        outputEdit_ = MakeControl(L"STATIC", L"", SS_PATHELLIPSIS, 0, editX, y + 4, valueWidth, 18, 0, Panel::Generate);
        y += 36;

        HWND activityTitle = MakeControl(L"STATIC", L"Activity", 0, 0, panelX, y, 140, 20, 0, Panel::Generate);
        if (activityTitle)
            SendMessageW(activityTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        y += 24;
        logEdit_ = MakeControl(L"EDIT", L"", ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, WS_EX_CLIENTEDGE,
                               panelX, y, 640, logHeight, 0, Panel::Generate);
        SendMessageW(logEdit_, WM_SETFONT, reinterpret_cast<WPARAM>(logFont_), TRUE);

        generateButton_ =
            MakeControl(L"BUTTON", L"Generate SDK", BS_OWNERDRAW | WS_TABSTOP, 0, panelX + 640 - wideButtonWidth,
                        y + logHeight + 10, wideButtonWidth, 30, kIdGenerate, Panel::Generate);

        CreateSupportControls();

        SetWindowPos(panelBackground_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        SetWindowPos(tab_, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ApplyTabVisibility();
    }

    void CreateSupportControls() {
        const int panelX = 42;
        int y = 134;

        HWND supportTitle = MakeControl(L"STATIC", L"Support and Links", 0, 0, panelX, y, 220, 20, 0, Panel::Support);
        if (supportTitle)
            SendMessageW(supportTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        y += 28;

        MakeControl(L"STATIC", L"Project resources and author links are kept here so generation stays focused.", 0, 0,
                    panelX, y, 570, 20, 0, Panel::Support);
        y += 32;

        constexpr int linkWidth = 620;
        constexpr DWORD linkStyle = SS_NOTIFY | SS_CENTERIMAGE | SS_LEFTNOWORDWRAP;
        projectRepoLink_ = MakeControl(L"STATIC", L"Project repository  |  github.com/Jadis0x/URKit",
                                       linkStyle, 0, panelX, y, linkWidth, 26,
                                       kIdProjectRepo, Panel::Support);
        githubProfileLink_ = MakeControl(L"STATIC", L"Author GitHub  |  github.com/Jadis0x", linkStyle, 0, panelX,
                                         y + 34, linkWidth, 26,
                                         kIdGitHubProfile, Panel::Support);
        coffeeLink_ = MakeControl(L"STATIC", L"Buy Me a Coffee  |  buymeacoffee.com/Jadis0x", linkStyle, 0, panelX,
                                  y + 68, linkWidth, 26, kIdCoffee, Panel::Support);
    }

    bool IsLinkControl(HWND control) const {
        return control == projectRepoLink_ || control == githubProfileLink_ || control == coffeeLink_;
    }

    void AddLog(const std::string &message) {
        ++logLine_;
        char prefix[32]{};
        std::snprintf(prefix, sizeof(prefix), "%02u | ", static_cast<unsigned>(logLine_));
        if (!logText_.empty())
            logText_ += "\r\n";
        logText_ += prefix;
        logText_ += message;
        URK::ToolUi::SetUtf8Text(logEdit_, logText_);
        SendMessageW(logEdit_, EM_SETSEL, static_cast<WPARAM>(-1), static_cast<LPARAM>(-1));
        SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
    }

    void BrowseGameExecutable() {
        wchar_t pathBuffer[MAX_PATH] = L"";
        OPENFILENAMEW dialog{};
        dialog.lStructSize = sizeof(dialog);
        dialog.hwndOwner = hwnd_;
        dialog.lpstrFilter = L"Executable files (*.exe)\0*.exe\0All files (*.*)\0*.*\0";
        dialog.lpstrFile = pathBuffer;
        dialog.nMaxFile = MAX_PATH;
        dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
        dialog.lpstrTitle = L"Select target game executable";
        if (!GetOpenFileNameW(&dialog))
            return;

        SetGameExecutable(pathBuffer);
    }

    void HandleDrop(HDROP drop) {
        wchar_t pathBuffer[MAX_PATH]{};
        const UINT pathLength = DragQueryFileW(drop, 0, pathBuffer, static_cast<UINT>(std::size(pathBuffer)));
        DragFinish(drop);
        if (pathLength == 0 || pathLength >= std::size(pathBuffer)) {
            AddLog("Dropped executable path is empty or exceeds MAX_PATH.");
            return;
        }
        if (Lower(fs::path(pathBuffer).extension().string()) != ".exe") {
            AddLog("Dropped file must be a Windows executable (.exe): " + URK::ToolUi::WideToUtf8(pathBuffer));
            return;
        }
        SetGameExecutable(pathBuffer);
    }

    void SetGameExecutable(const wchar_t *path) {
        options_.gameExePath = URK::ToolUi::WideToUtf8(path);
        URK::ToolUi::SetUtf8Text(gameExeEdit_, options_.gameExePath);
        AddLog("Target game executable: " + options_.gameExePath);
        RefreshDerivedPaths(false);
    }

    bool RefreshDerivedPaths(bool logResult) {
        Options next;
        if (SendMessageW(il2cppRadio_, BM_GETCHECK, 0, 0) == BST_CHECKED)
            next.backendSelection = "il2cpp";
        else if (SendMessageW(monoRadio_, BM_GETCHECK, 0, 0) == BST_CHECKED)
            next.backendSelection = "mono";
        else
            next.backendSelection = "auto";
        next.projectName = URK::ToolUi::WideToUtf8(URK::ToolUi::WindowText(projectEdit_));
        next.gameExePath = options_.gameExePath;
        next.enableLocalization = SendMessageW(localizationCheck_, BM_GETCHECK, 0, 0) == BST_CHECKED;

        std::string error;
        if (!FillDerivedProjectPaths(next, error)) {
            options_ = {};
            URK::ToolUi::SetUtf8Text(gameExeEdit_, next.gameExePath);
            URK::ToolUi::SetUtf8Text(exportEdit_, "");
            URK::ToolUi::SetUtf8Text(outputEdit_, "");
            EnableWindow(generateButton_, FALSE);
            if (logResult)
                AddLog("Project setup failed: " + error);
            return false;
        }

        options_ = next;
        URK::ToolUi::SetUtf8Text(gameExeEdit_, options_.gameExePath);
        URK::ToolUi::SetUtf8Text(exportEdit_, options_.exportRoot);
        URK::ToolUi::SetUtf8Text(outputEdit_, options_.projectOut);
        EnableWindow(generateButton_, TRUE);

        if (logResult) {
            AddLog("Backend mode: " + options_.backendSelection);
            AddLog(std::string("Resolved backend: ") + BackendDisplayName(options_));
            AddLog("Target game executable: " + options_.gameExePath);
            AddLog("Export directory: " + options_.exportRoot);
            AddLog("Output project: " + options_.projectOut);
            AddLog("Deploy folder: " + (fs::path(options_.gameDir) / options_.modsDir).string());
            AddLog(std::string("Language support: ") + (options_.enableLocalization ? "enabled" : "fixed English"));
            AddLog("URKit ABI: sdk=" + std::to_string(URK_SDK_VERSION) + " runtime=" +
                   std::to_string(URK_RUNTIME_API_VERSION) + " mono=" + std::to_string(URK_MONO_API_VERSION) +
                   " il2cpp=" + std::to_string(URK_IL2CPP_API_VERSION) +
                   " network=" + std::to_string(URK_NETWORK_API_VERSION));
            AddLog("Generation mode: runtime API helpers only; no dump-generated "
                   "wrappers.");
        }
        return true;
    }

    void Generate() {
        if (!RefreshDerivedPaths(false)) {
            AddLog("Fix the project settings before generation.");
            return;
        }

        EnableWindow(generateButton_, FALSE);
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));
        AddLog(std::string("Generating ") + BackendDisplayName(options_) + " SDK and starter mod project...");

        std::string error;
        bool ok = false;
        try {
            ok = GenerateSelectedProject(options_, error);
        } catch (const std::exception &ex) {
            error = ex.what();
        }

        if (ok) {
            AddLog("Generation completed successfully.");
            AddLog("Project: " + options_.projectOut);
            AddLog("Deploy folder: " + (fs::path(options_.gameDir) / options_.modsDir).string());
            MessageBoxW(hwnd_, L"SDK project generated successfully.", L"URKit SDK generator",
                        MB_OK | MB_ICONINFORMATION);
        } else {
            AddLog("Generation failed: " + error);
            MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(error).c_str(), L"Generation failed", MB_OK | MB_ICONERROR);
        }

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        EnableWindow(generateButton_, TRUE);
    }

    void PaintPanels() {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(hwnd_, &paint);
        RECT client{};
        GetClientRect(hwnd_, &client);
        RECT header{0, 0, client.right, 72};
        FillRect(dc, &header, brandBrush_);
        URK::ToolUi::DrawBrandMark(dc, 22, 13, 40);
        RECT generatePanel{20, 120, 724, 536};
        URK::ToolUi::DrawRoundedPanel(dc, generatePanel, URK::ToolUi::kPalette.surface, URK::ToolUi::kPalette.border,
                                      6);
        EndPaint(hwnd_, &paint);
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND title_ = nullptr;
    HWND subtitle_ = nullptr;
    HWND tab_ = nullptr;
    HWND panelBackground_ = nullptr;
    HWND projectRepoLink_ = nullptr;
    HWND githubProfileLink_ = nullptr;
    HWND coffeeLink_ = nullptr;
    HWND autoRadio_ = nullptr;
    HWND monoRadio_ = nullptr;
    HWND il2cppRadio_ = nullptr;
    HWND projectEdit_ = nullptr;
    HWND localizationCheck_ = nullptr;
    HWND gameExeEdit_ = nullptr;
    HWND gameExeButton_ = nullptr;
    HWND exportEdit_ = nullptr;
    HWND outputEdit_ = nullptr;
    HWND logEdit_ = nullptr;
    HWND generateButton_ = nullptr;
    HFONT font_ = nullptr;
    HFONT logFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT sectionFont_ = nullptr;
    HBRUSH whiteBrush_ = nullptr;
    HBRUSH logBrush_ = nullptr;
    HBRUSH canvasBrush_ = nullptr;
    HBRUSH brandBrush_ = nullptr;
    std::vector<HWND> generateControls_;
    std::vector<HWND> supportControls_;
    Options options_;
    int activeTab_ = 0;
    unsigned logLine_ = 0;
    std::string logText_;
};

#endif

} // namespace

#ifdef _WIN32
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    std::string commandLineError;
    std::optional<Options> commandLineOptions = ParseCommandLineOptions(commandLineError);
    if (commandLineOptions)
        return RunCommandLineGeneration(*commandLineOptions);
    if (!commandLineError.empty()) {
        MessageBoxW(nullptr, URK::ToolUi::Utf8ToWide(commandLineError).c_str(), L"URKit SDK generator",
                    MB_OK | (commandLineError.rfind("Usage:", 0) == 0 ? MB_ICONINFORMATION : MB_ICONERROR));
        return commandLineError.rfind("Usage:", 0) == 0 ? 0 : 2;
    }

    SdkGeneratorWindow window;
    return window.Run(instance);
}
#else
int main() {
    return 2;
}
#endif
