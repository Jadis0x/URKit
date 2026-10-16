#include "cli.h"
#include "gui/gui_app.h"
#include "self_update.h"
#include "text_util.h"

#include <cwchar>
#include <limits>
#include <shellapi.h>
#include <string>
#include <vector>

namespace {

std::vector<std::wstring> Arguments() {
    int count = 0;
    wchar_t **values = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::wstring> arguments;
    for (int index = 1; values && index < count; ++index)
        arguments.emplace_back(values[index]);
    if (values)
        LocalFree(values);
    return arguments;
}

// Runs inside the helper copy started by a self-update.
int ApplySelfUpdate(const std::vector<std::wstring> &arguments) {
    std::wstring source;
    std::wstring target;
    std::wstring digest;
    unsigned long waitPid = 0;
    bool restart = true;
    for (size_t index = 1; index < arguments.size(); ++index) {
        const std::wstring &argument = arguments[index];
        if (argument == L"--no-restart") {
            restart = false;
            continue;
        }
        if (index + 1 >= arguments.size())
            return 2;
        const std::wstring &value = arguments[++index];
        if (argument == L"--source")
            source = value;
        else if (argument == L"--target")
            target = value;
        else if (argument == L"--sha256")
            digest = value;
        else if (argument == L"--wait-pid")
            waitPid = std::wcstoul(value.c_str(), nullptr, 10);
        else
            return 2;
    }
    std::string error;
    if (!UrkApp::ApplyDownloadedUpdate(source, target, UrkApp::Utf8(digest), static_cast<uint32_t>(waitPid), restart,
                                       &error)) {
        MessageBoxW(nullptr, UrkApp::Wide(error).c_str(), L"URKit update failed", MB_OK | MB_ICONERROR);
        return 1;
    }
    return 0;
}

} // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    const std::vector<std::wstring> arguments = Arguments();
    if (!arguments.empty() && arguments.front() == L"--apply-self-update")
        return ApplySelfUpdate(arguments);
    // A file or folder dropped on urk.exe in Explorer arrives as the only argument.
    std::wstring openPath;
    if (arguments.size() == 1 && GetFileAttributesW(arguments.front().c_str()) != INVALID_FILE_ATTRIBUTES)
        openPath = arguments.front();
    else if (std::optional<int> exitCode = UrkApp::RunCommandLine(arguments))
        return *exitCode;
    UrkApp::RemoveStaleSelfUpdateHelper();
    return UrkGui::RunGui(instance, show, openPath);
}
