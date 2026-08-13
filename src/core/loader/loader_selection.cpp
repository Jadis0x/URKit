#include "loader_selection.h"

#include <windows.h>

#include <commdlg.h>
#include <filesystem>
#include <iterator>
#include <shobjidl.h>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kWindowClass[] = L"URKitInjectedLoaderSelection";
constexpr int kConfigEdit = 1001;
constexpr int kModsEdit = 1002;
constexpr int kBrowseConfig = 1003;
constexpr int kBrowseMods = 1004;
constexpr int kLoad = 1005;
constexpr int kCancel = 1006;

struct SelectionWindow {
    HINSTANCE instance = nullptr;
    HWND window = nullptr;
    HWND configEdit = nullptr;
    HWND modsEdit = nullptr;
    bool accepted = false;
    bool closed = false;
    std::wstring configPath;
    std::vector<std::wstring> modPaths;
};

std::string NativePathString(const std::wstring &path) {
    return std::filesystem::path(path).string();
}

void SetControlText(HWND control, const std::wstring &value) {
    if (control)
        SetWindowTextW(control, value.c_str());
}

std::wstring ControlText(HWND control) {
    const int length = control ? GetWindowTextLengthW(control) : 0;
    if (length <= 0)
        return {};

    std::wstring value(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<size_t>(length));
    return value;
}

void BrowseForConfig(SelectionWindow *state) {
    wchar_t path[MAX_PATH] = {};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = state->window;
    dialog.lpstrFilter = L"INI files (*.ini)\0*.ini\0All files (*.*)\0*.*\0";
    dialog.lpstrFile = path;
    dialog.nMaxFile = static_cast<DWORD>(std::size(path));
    dialog.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    dialog.lpstrTitle = L"Select URKit configuration file";
    if (GetOpenFileNameW(&dialog))
        SetControlText(state->configEdit, path);
}

void BrowseForMods(SelectionWindow *state) {
    SetForegroundWindow(state->window);

    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninitialize = SUCCEEDED(apartment);
    if (FAILED(apartment) && apartment != RPC_E_CHANGED_MODE) {
        MessageBoxW(state->window, L"Could not initialize the Windows file picker.", L"URKit",
                    MB_OK | MB_ICONERROR);
        return;
    }

    IFileOpenDialog *dialog = nullptr;
    HRESULT result = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(result)) {
        const COMDLG_FILTERSPEC filters[] = {{L"URKit mod DLLs", L"*.dll"}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(static_cast<UINT>(std::size(filters)), filters);
        dialog->SetTitle(L"Select URKit mod DLL");
        dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
        dialog->SetDefaultExtension(L"dll");
        result = dialog->Show(state->window);
    }

    PWSTR selectedPath = nullptr;
    if (SUCCEEDED(result)) {
        IShellItem *item = nullptr;
        result = dialog->GetResult(&item);
        if (SUCCEEDED(result)) {
            result = item->GetDisplayName(SIGDN_FILESYSPATH, &selectedPath);
            item->Release();
        }
    }

    if (SUCCEEDED(result) && selectedPath && selectedPath[0]) {
        state->modPaths = {selectedPath};
        SetControlText(state->modsEdit, selectedPath);
    } else if (result != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        MessageBoxW(state->window, L"Could not open the mod DLL file picker.", L"URKit",
                    MB_OK | MB_ICONERROR);
    }

    if (selectedPath)
        CoTaskMemFree(selectedPath);
    if (dialog)
        dialog->Release();
    if (uninitialize)
        CoUninitialize();
}

bool ValidateSelection(SelectionWindow *state, std::wstring *configPath, std::vector<std::wstring> *modPaths) {
    if (!state || !configPath || !modPaths)
        return false;

    *configPath = ControlText(state->configEdit);
    *modPaths = state->modPaths;
    if (configPath->empty() || modPaths->empty()) {
        MessageBoxW(state->window, L"Select both a configuration INI file and at least one mod DLL.", L"URKit",
                    MB_OK | MB_ICONWARNING);
        return false;
    }

    std::error_code error;
    const std::filesystem::path config(*configPath);
    if (!std::filesystem::is_regular_file(config, error) || error) {
        MessageBoxW(state->window, L"The selected configuration file does not exist or is not a file.", L"URKit",
                    MB_OK | MB_ICONERROR);
        return false;
    }
    if (_wcsicmp(config.extension().c_str(), L".ini") != 0) {
        MessageBoxW(state->window, L"The selected configuration must be an .ini file.", L"URKit",
                    MB_OK | MB_ICONERROR);
        return false;
    }

    for (const std::wstring &modPath : *modPaths) {
        error.clear();
        const std::filesystem::path mod(modPath);
        if (!std::filesystem::is_regular_file(mod, error) || error || _wcsicmp(mod.extension().c_str(), L".dll") != 0) {
            MessageBoxW(state->window, L"Every selected mod must be an existing .dll file.", L"URKit",
                        MB_OK | MB_ICONERROR);
            return false;
        }
    }
    return true;
}

void CreateSelectionControls(SelectionWindow *state) {
    HFONT font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    auto create = [&](LPCWSTR className, LPCWSTR text, DWORD style, int id, int x, int y, int width, int height) {
        HWND control = CreateWindowExW(0, className, text, WS_CHILD | WS_VISIBLE | style, x, y, width, height,
                                       state->window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), state->instance,
                                       nullptr);
        if (control)
            SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        return control;
    };

    create(L"STATIC", L"Config INI:", SS_LEFT, 0, 20, 22, 100, 22);
    state->configEdit = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL, kConfigEdit, 120, 18, 410, 26);
    create(L"BUTTON", L"Browse...", BS_PUSHBUTTON, kBrowseConfig, 540, 18, 100, 26);

    create(L"STATIC", L"Mod DLL(s):", SS_LEFT, 0, 20, 67, 100, 22);
    state->modsEdit = create(L"EDIT", L"", WS_BORDER | ES_AUTOHSCROLL | ES_READONLY, kModsEdit, 120, 63, 410, 26);
    create(L"BUTTON", L"Select DLL...", BS_PUSHBUTTON, kBrowseMods, 540, 63, 100, 26);

    create(L"STATIC", L"Only the selected DLLs are loaded; nothing is created in the game folder.", SS_LEFT, 0, 20, 108,
           600, 22);
    create(L"BUTTON", L"Load", BS_DEFPUSHBUTTON, kLoad, 430, 145, 100, 30);
    create(L"BUTTON", L"Cancel", BS_PUSHBUTTON, kCancel, 540, 145, 100, 30);
}

LRESULT CALLBACK SelectionWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto *state = reinterpret_cast<SelectionWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        state = static_cast<SelectionWindow *>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    switch (message) {
    case WM_CREATE:
        CreateSelectionControls(state);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case kBrowseConfig:
            BrowseForConfig(state);
            return 0;
        case kBrowseMods:
            BrowseForMods(state);
            return 0;
        case kLoad: {
            if (ValidateSelection(state, &state->configPath, &state->modPaths)) {
                state->accepted = true;
                DestroyWindow(window);
            }
            return 0;
        }
        case kCancel:
            DestroyWindow(window);
            return 0;
        default:
            break;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        state->closed = true;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

bool RegisterSelectionClass(HINSTANCE instance) {
    WNDCLASSW windowClass{};
    windowClass.hInstance = instance;
    windowClass.lpfnWndProc = SelectionWindowProc;
    windowClass.lpszClassName = kWindowClass;
    windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (RegisterClassW(&windowClass))
        return true;
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
} // namespace

bool Loader_SelectPaths(LoaderSelection *selection) {
    if (!selection)
        return false;

    selection->configPath.clear();
    selection->modPaths.clear();

    HINSTANCE instance = GetModuleHandleW(L"URKitInjector.dll");
    if (!instance)
        instance = GetModuleHandleW(nullptr);
    if (!RegisterSelectionClass(instance)) {
        OutputDebugStringA("[URKit][selection][ERROR] RegisterClassW failed.\n");
        MessageBoxW(nullptr, L"URKit could not create its selection window.", L"URKit", MB_OK | MB_ICONERROR);
        return false;
    }

    SelectionWindow state;
    state.instance = instance;
    state.window = CreateWindowExW(WS_EX_APPWINDOW | WS_EX_TOPMOST, kWindowClass, L"URKit - Select content",
                                   WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT,
                                   680, 225, nullptr, nullptr, instance, &state);
    if (!state.window) {
        OutputDebugStringA("[URKit][selection][ERROR] CreateWindowExW failed.\n");
        MessageBoxW(nullptr, L"URKit could not create its selection window.", L"URKit", MB_OK | MB_ICONERROR);
        return false;
    }

    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    RECT windowRect{};
    GetWindowRect(state.window, &windowRect);
    const int width = windowRect.right - windowRect.left;
    const int height = windowRect.bottom - windowRect.top;
    SetWindowPos(state.window, HWND_TOPMOST, (workArea.left + workArea.right - width) / 2,
                 (workArea.top + workArea.bottom - height) / 2, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(state.window);

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    if (!state.accepted)
        return false;

    selection->configPath = NativePathString(state.configPath);
    for (const std::wstring &modPath : state.modPaths)
        selection->modPaths.push_back(NativePathString(modPath));
    return !selection->configPath.empty() && !selection->modPaths.empty();
}
