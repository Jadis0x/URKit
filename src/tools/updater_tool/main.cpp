#include "mod_sdk.h"
#include "project_updater.h"
#include "updater_self_update.h"
#include "updater_version.h"

#include <cstdio>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>

#include "../win32_tool_ui.h"
#endif

namespace {

namespace fs = std::filesystem;

struct CommandLineOptions {
    fs::path projectRoot;
    bool update = false;
};

void PrintUsage() {
    std::fprintf(stderr,
                 "Usage: urk-updater.exe --project C:\\path\\to\\project [--check|--update]\n"
                 "       urk-updater.exe --check-updater\n"
                 "\n"
                 "--check  Inspect a generated URKit project without changing files (default).\n"
                 "--update Create a backup and update the project-managed SDK files.\n"
                 "--check-updater Check GitHub for a newer urk-updater.exe.\n");
}

int RunCommandLine(const CommandLineOptions &options) {
    UrkProject::UpdatePreview preview;
    std::string error;
    try {
        if (!UrkProject::PreviewUpdate(options.projectRoot, &preview, &error)) {
            std::fprintf(stderr, "URKit project check failed: %s\n", error.c_str());
            return 1;
        }
        std::printf("%s\n%s\n", UrkProject::Describe(preview.inspection).c_str(),
                    UrkProject::DescribeChanges(preview.changes).c_str());
        if (!options.update)
            return preview.inspection.updateAvailable ? 10 : 0;

        UrkProject::UpdateResult update;
        if (!UrkProject::Update(options.projectRoot, &update, &error)) {
            std::fprintf(stderr, "URKit project update failed: %s\n", error.c_str());
            return 1;
        }
        if (!update.updated) {
            std::printf("Project is already up to date.\n");
            return 0;
        }
        std::printf("Project updated successfully. Backup: %s\n", update.backupDirectory.string().c_str());
        return 0;
    } catch (const std::exception &exception) {
        std::fprintf(stderr, "URKit project update failed: %s\n", exception.what());
        return 1;
    }
}

#ifdef _WIN32

std::optional<CommandLineOptions> ParseCommandLine(std::string *error, bool *showHelp) {
    if (showHelp)
        *showHelp = false;
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        if (error)
            *error = "CommandLineToArgvW failed.";
        return std::nullopt;
    }
    struct ArgvGuard {
        wchar_t **value = nullptr;
        ~ArgvGuard() {
            if (value)
                LocalFree(value);
        }
    } guard{argv};

    if (argc <= 1)
        return std::nullopt;

    CommandLineOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = URK::ToolUi::WideToUtf8(argv[index]);
        if (argument == "--project" || argument == "-p") {
            if (++index >= argc) {
                if (error)
                    *error = "Missing value for --project.";
                return std::nullopt;
            }
            options.projectRoot = fs::path(argv[index]);
        } else if (argument == "--check") {
            options.update = false;
        } else if (argument == "--update") {
            options.update = true;
        } else if (argument == "--help" || argument == "-h" || argument == "/?") {
            if (showHelp)
                *showHelp = true;
            return std::nullopt;
        } else {
            if (error)
                *error = "Unknown argument: " + argument;
            return std::nullopt;
        }
    }
    if (options.projectRoot.empty()) {
        if (error)
            *error = "Missing value for --project.";
        return std::nullopt;
    }
    return options;
}

constexpr int kWindowWidth = 720;
constexpr int kWindowHeight = 430;
constexpr int kIdProjectPath = 2001;
constexpr int kIdBrowse = 2002;
constexpr int kIdCheck = 2003;
constexpr int kIdUpdate = 2004;
constexpr int kIdCheckUpdater = 2005;

class UpdaterWindow {
  public:
    int Run(HINSTANCE instance) {
        instance_ = instance;
        const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool shouldUninitializeCom = SUCCEEDED(comResult);
        INITCOMMONCONTROLSEX controls{};
        controls.dwSize = sizeof(controls);
        controls.dwICC = ICC_STANDARD_CLASSES;
        InitCommonControlsEx(&controls);

        font_ = URK::ToolUi::CreateUiFont(13);
        titleFont_ = URK::ToolUi::CreateUiFont(19, FW_SEMIBOLD);
        sectionFont_ = URK::ToolUi::CreateUiFont(14, FW_SEMIBOLD);
        logFont_ = URK::ToolUi::CreateUiFont(12);

        WNDCLASSW windowClass{};
        windowClass.lpfnWndProc = &UpdaterWindow::WndProc;
        windowClass.hInstance = instance_;
        windowClass.lpszClassName = L"URKitProjectUpdaterWindow";
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        RegisterClassW(&windowClass);

        hwnd_ = CreateWindowExW(0, windowClass.lpszClassName, L"URKit Project Updater",
                                WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT,
                                CW_USEDEFAULT, kWindowWidth, kWindowHeight, nullptr, nullptr, instance_, this);
        if (!hwnd_) {
            if (shouldUninitializeCom)
                CoUninitialize();
            return 1;
        }
        URK::ToolUi::ApplyModernWindowFrame(hwnd_);
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);

        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        const int exitCode = static_cast<int>(message.wParam);
        if (shouldUninitializeCom)
            CoUninitialize();
        return exitCode;
    }

  private:
    static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
        UpdaterWindow *self = nullptr;
        if (message == WM_NCCREATE) {
            const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lparam);
            self = static_cast<UpdaterWindow *>(create->lpCreateParams);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hwnd_ = window;
        } else {
            self = reinterpret_cast<UpdaterWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
        }
        return self ? self->HandleMessage(message, wparam, lparam) : DefWindowProcW(window, message, wparam, lparam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
            case WM_CREATE:
                CreateControls();
                return 0;
            case WM_COMMAND:
                return HandleCommand(LOWORD(wparam), HIWORD(wparam));
            case WM_DRAWITEM:
                if (const auto *item = reinterpret_cast<const DRAWITEMSTRUCT *>(lparam)) {
                    if (item->CtlID == kIdCheck || item->CtlID == kIdUpdate) {
                        URK::ToolUi::DrawButton(*item, font_, item->CtlID == kIdUpdate);
                        return TRUE;
                    }
                }
                return FALSE;
            case WM_DESTROY:
                DestroyFonts();
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProcW(hwnd_, message, wparam, lparam);
        }
    }

    HWND MakeControl(const wchar_t *className, const wchar_t *text, DWORD style, DWORD exStyle, int x, int y, int width,
                     int height, int id = 0) {
        HWND control = CreateWindowExW(exStyle, className, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                                       hwnd_, reinterpret_cast<HMENU>(static_cast<intptr_t>(id)), instance_, nullptr);
        if (control && font_)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
        if (control)
            URK::ToolUi::ApplyControlTheme(control);
        return control;
    }

    void CreateControls() {
        title_ = MakeControl(L"STATIC", L"URKit Project Updater", 0, 0, 26, 18, 360, 26);
        SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(titleFont_), TRUE);
        const std::wstring subtitle = L"Inspect and safely refresh an existing project | SDK ABI v" +
                                      std::to_wstring(URK_SDK_VERSION);
        MakeControl(L"STATIC", subtitle.c_str(), 0, 0, 27, 48, 610, 18);

        HWND projectTitle = MakeControl(L"STATIC", L"Project", 0, 0, 27, 88, 160, 20);
        SendMessageW(projectTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        MakeControl(L"STATIC", L"Project folder", 0, 0, 28, 119, 96, 20);
        projectPath_ = MakeControl(L"EDIT", L"", ES_AUTOHSCROLL, WS_EX_CLIENTEDGE, 126, 116, 448, 25, kIdProjectPath);
        browseButton_ = MakeControl(L"BUTTON", L"Browse...", WS_TABSTOP, 0, 584, 116, 100, 25, kIdBrowse);

        HWND activityTitle = MakeControl(L"STATIC", L"Check result", 0, 0, 27, 164, 160, 20);
        SendMessageW(activityTitle, WM_SETFONT, reinterpret_cast<WPARAM>(sectionFont_), TRUE);
        resultText_ = MakeControl(L"EDIT", L"Select a URKit project, then check it before updating.",
                                  ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_VSCROLL, WS_EX_CLIENTEDGE, 28, 192,
                                  656, 144);
        SendMessageW(resultText_, WM_SETFONT, reinterpret_cast<WPARAM>(logFont_), TRUE);

        checkButton_ = MakeControl(L"BUTTON", L"Check Project", BS_OWNERDRAW | WS_TABSTOP, 0, 402, 352, 132, 31,
                                   kIdCheck);
        updateButton_ = MakeControl(L"BUTTON", L"Update Project", BS_OWNERDRAW | WS_TABSTOP, 0, 552, 352, 132, 31,
                                    kIdUpdate);
        checkUpdaterButton_ = MakeControl(L"BUTTON", L"Check Updater", WS_TABSTOP, 0, 28, 352, 132, 31,
                                          kIdCheckUpdater);
    }

    void DestroyFonts() {
        for (HFONT font : {font_, titleFont_, sectionFont_, logFont_})
            if (font)
                DeleteObject(font);
        font_ = titleFont_ = sectionFont_ = logFont_ = nullptr;
    }

    void SetResult(const std::string &text) {
        std::string normalized;
        normalized.reserve(text.size() + 16);
        for (size_t index = 0; index < text.size(); ++index) {
            if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r'))
                normalized.push_back('\r');
            normalized.push_back(text[index]);
        }
        URK::ToolUi::SetUtf8Text(resultText_, normalized);
    }

    std::optional<fs::path> SelectedProject() const {
        const std::wstring value = URK::ToolUi::WindowText(projectPath_);
        if (value.empty())
            return std::nullopt;
        return fs::path(value);
    }

    void BrowseProject() {
        BROWSEINFOW dialog{};
        dialog.hwndOwner = hwnd_;
        dialog.lpszTitle = L"Select the generated URKit project folder";
        dialog.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_USENEWUI;
        PIDLIST_ABSOLUTE selected = SHBrowseForFolderW(&dialog);
        if (!selected)
            return;
        wchar_t path[MAX_PATH]{};
        const bool resolved = SHGetPathFromIDListW(selected, path) != FALSE;
        CoTaskMemFree(selected);
        if (!resolved) {
            SetResult("The selected folder could not be resolved.");
            return;
        }
        SetWindowTextW(projectPath_, path);
        CheckProject();
    }

    bool CheckProject() {
        const std::optional<fs::path> project = SelectedProject();
        if (!project) {
            SetResult("Select a project folder first.");
            return false;
        }
        std::string error;
        UrkProject::UpdatePreview preview;
        try {
            if (!UrkProject::PreviewUpdate(*project, &preview, &error)) {
                SetResult("Check failed: " + error);
                return false;
            }
        } catch (const std::exception &exception) {
            SetResult(std::string("Check failed: ") + exception.what());
            return false;
        }
        SetResult(UrkProject::Describe(preview.inspection) + "\n\n" + UrkProject::DescribeChanges(preview.changes));
        return true;
    }

    void UpdateProject() {
        const std::optional<fs::path> project = SelectedProject();
        if (!project) {
            SetResult("Select a project folder first.");
            return;
        }

        std::string previewError;
        UrkProject::UpdatePreview preview;
        try {
            if (!UrkProject::PreviewUpdate(*project, &preview, &previewError)) {
                SetResult("Update preview failed: " + previewError);
                MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(previewError).c_str(), L"URKit project update failed",
                            MB_OK | MB_ICONERROR);
                return;
            }
        } catch (const std::exception &exception) {
            previewError = exception.what();
            SetResult("Update preview failed: " + previewError);
            MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(previewError).c_str(), L"URKit project update failed",
                        MB_OK | MB_ICONERROR);
            return;
        }
        SetResult(UrkProject::Describe(preview.inspection) + "\n\n" + UrkProject::DescribeChanges(preview.changes));
        if (!preview.inspection.updateAvailable) {
            MessageBoxW(hwnd_, L"This project is already up to date.", L"URKit Project Updater", MB_OK | MB_ICONINFORMATION);
            return;
        }

        const std::string confirmation =
            "The listed URKit-managed files will be updated. A backup will be created under .urk\\backups.\n\n" +
            UrkProject::DescribeChanges(preview.changes) + "\n\nContinue?";
        if (MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(confirmation).c_str(), L"Confirm URKit project update",
                        MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
            return;
        }
        EnableWindow(updateButton_, FALSE);
        EnableWindow(checkButton_, FALSE);
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));

        std::string error;
        UrkProject::UpdateResult update;
        bool succeeded = false;
        try {
            succeeded = UrkProject::Update(*project, &update, &error);
        } catch (const std::exception &exception) {
            error = exception.what();
        }

        if (!succeeded) {
            SetResult("Update failed: " + error);
            MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(error).c_str(), L"URKit project update failed", MB_OK | MB_ICONERROR);
        } else if (!update.updated) {
            SetResult(UrkProject::Describe(update.inspection));
            MessageBoxW(hwnd_, L"This project is already up to date.", L"URKit Project Updater", MB_OK | MB_ICONINFORMATION);
        } else {
            SetResult(UrkProject::Describe(update.inspection) + "\nBackup: " + update.backupDirectory.string());
            MessageBoxW(hwnd_, L"Project updated successfully. A backup was created in .urk\\backups.",
                        L"URKit Project Updater", MB_OK | MB_ICONINFORMATION);
        }

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        EnableWindow(checkButton_, TRUE);
        EnableWindow(updateButton_, TRUE);
    }

    void CheckUpdaterUpdate() {
        EnableWindow(checkUpdaterButton_, FALSE);
        SetCursor(LoadCursorW(nullptr, IDC_WAIT));

        UrkUpdater::UpdateCheckResult check;
        UrkUpdater::AvailableUpdate available;
        std::string error;
        const bool checked = UrkUpdater::CheckForUpdate(&check, &available, &error);
        if (!checked) {
            SetResult("Updater update check failed: " + error);
            MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(error).c_str(), L"URKit updater update check", MB_OK | MB_ICONERROR);
        } else if (!check.updateAvailable) {
            const std::string message = "URKit Project Updater is current.\nInstalled: v" + check.currentVersion +
                                        "\nLatest release: " + check.latestVersion;
            SetResult(message);
            MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(message).c_str(), L"URKit updater update check",
                        MB_OK | MB_ICONINFORMATION);
        } else {
            const std::string confirmation = "URKit Project Updater v" + available.availableVersion +
                                             " is available.\n\nIt will be downloaded from the official GitHub release, "
                                             "verified against GitHub's SHA-256 digest, then this app will restart.\n\n"
                                             "Install now?";
            if (MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(confirmation).c_str(), L"Update URKit Project Updater",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                if (UrkUpdater::DownloadAndRestart(available, &error)) {
                    MessageBoxW(hwnd_, L"The verified update was downloaded. URKit Project Updater will restart now.",
                                L"Update scheduled", MB_OK | MB_ICONINFORMATION);
                    DestroyWindow(hwnd_);
                } else {
                    SetResult("Updater self-update failed: " + error);
                    MessageBoxW(hwnd_, URK::ToolUi::Utf8ToWide(error).c_str(), L"URKit updater self-update failed",
                                MB_OK | MB_ICONERROR);
                }
            }
        }

        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        if (hwnd_)
            EnableWindow(checkUpdaterButton_, TRUE);
    }

    LRESULT HandleCommand(int identifier, int notification) {
        if (notification != BN_CLICKED)
            return 0;
        switch (identifier) {
            case kIdBrowse:
                BrowseProject();
                return 0;
            case kIdCheck:
                CheckProject();
                return 0;
            case kIdUpdate:
                UpdateProject();
                return 0;
            case kIdCheckUpdater:
                CheckUpdaterUpdate();
                return 0;
            default:
                return 0;
        }
    }

    HINSTANCE instance_ = nullptr;
    HWND hwnd_ = nullptr;
    HWND title_ = nullptr;
    HWND projectPath_ = nullptr;
    HWND browseButton_ = nullptr;
    HWND resultText_ = nullptr;
    HWND checkButton_ = nullptr;
    HWND updateButton_ = nullptr;
    HWND checkUpdaterButton_ = nullptr;
    HFONT font_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT sectionFont_ = nullptr;
    HFONT logFont_ = nullptr;
};

#endif

} // namespace

#ifdef _WIN32
bool TryRunSelfUpdateCheck(int *exitCode) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return false;
    struct ArgvGuard {
        wchar_t **value = nullptr;
        ~ArgvGuard() {
            if (value)
                LocalFree(value);
        }
    } guard{argv};
    if (argc != 2 || std::wstring_view(argv[1]) != L"--check-updater")
        return false;

    UrkUpdater::UpdateCheckResult check;
    UrkUpdater::AvailableUpdate available;
    std::string error;
    if (!UrkUpdater::CheckForUpdate(&check, &available, &error)) {
        std::fprintf(stderr, "URKit updater self-update check failed: %s\n", error.c_str());
        if (exitCode)
            *exitCode = 1;
        return true;
    }
    std::printf("Updater version: %s\nLatest release: %s\n", check.currentVersion.c_str(), check.latestVersion.c_str());
    if (check.updateAvailable)
        std::printf("Status: update available\nRelease: %s\n", check.releaseUrl.c_str());
    else
        std::printf("Status: up to date\n");
    if (exitCode)
        *exitCode = check.updateAvailable ? 10 : 0;
    return true;
}

bool TryRunSelfUpdateHelper(int *exitCode) {
    int argc = 0;
    wchar_t **argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
        return false;
    struct ArgvGuard {
        wchar_t **value = nullptr;
        ~ArgvGuard() {
            if (value)
                LocalFree(value);
        }
    } guard{argv};
    if (argc < 2 || std::wstring_view(argv[1]) != L"--apply-self-update")
        return false;

    std::filesystem::path source;
    std::filesystem::path target;
    std::string digest;
    uint32_t waitPid = 0;
    for (int index = 2; index < argc; ++index) {
        const std::wstring_view argument(argv[index]);
        if (index + 1 >= argc) {
            if (exitCode)
                *exitCode = 2;
            return true;
        }
        const std::wstring_view value(argv[++index]);
        if (argument == L"--source") {
            source = std::filesystem::path(value);
        } else if (argument == L"--target") {
            target = std::filesystem::path(value);
        } else if (argument == L"--sha256") {
            digest = URK::ToolUi::WideToUtf8(value);
        } else if (argument == L"--wait-pid") {
            const std::wstring valueText(value);
            wchar_t *end = nullptr;
            const unsigned long parsed = std::wcstoul(valueText.c_str(), &end, 10);
            if (!end || *end != L'\0' || parsed > std::numeric_limits<uint32_t>::max()) {
                if (exitCode)
                    *exitCode = 2;
                return true;
            }
            waitPid = static_cast<uint32_t>(parsed);
        } else {
            if (exitCode)
                *exitCode = 2;
            return true;
        }
    }

    std::string error;
    const bool updated = UrkUpdater::ApplyDownloadedUpdate(source, target, digest, waitPid, &error);
    if (!updated) {
        std::fprintf(stderr, "URKit updater self-update helper failed: %s\n", error.c_str());
        MessageBoxW(nullptr, URK::ToolUi::Utf8ToWide(error).c_str(), L"URKit Project Updater self-update failed",
                    MB_OK | MB_ICONERROR);
    }
    if (exitCode)
        *exitCode = updated ? 0 : 1;
    return true;
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    int helperExitCode = 0;
    if (TryRunSelfUpdateCheck(&helperExitCode))
        return helperExitCode;
    if (TryRunSelfUpdateHelper(&helperExitCode))
        return helperExitCode;
    std::string error;
    bool showHelp = false;
    const std::optional<CommandLineOptions> options = ParseCommandLine(&error, &showHelp);
    if (options)
        return RunCommandLine(*options);
    if (showHelp) {
        PrintUsage();
        return 0;
    }
    if (!error.empty()) {
        MessageBoxW(nullptr, URK::ToolUi::Utf8ToWide(error).c_str(), L"URKit Project Updater", MB_OK | MB_ICONERROR);
        return 2;
    }
    UpdaterWindow window;
    return window.Run(instance);
}
#else
int main() {
    PrintUsage();
    return 2;
}
#endif
