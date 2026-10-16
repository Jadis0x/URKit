#pragma once

#include "main_frame.h"
#include "pages.h"
#include "text_util.h"
#include "theme.h"
#include "widgets.h"

#include <wx/msgdlg.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/utils.h>

#include <filesystem>
#include <string>
#include <vector>

class wxListCtrl;

namespace UrkGui {

inline std::filesystem::path ToPath(const wxString &text) {
    return std::filesystem::path(text.ToStdWstring());
}

inline wxString FromPath(const std::filesystem::path &path) {
    return wxString(path.wstring());
}

inline wxString FromUtf8(const std::string &text) {
    return wxString::FromUTF8(text);
}

inline void OpenFolder(const std::filesystem::path &path) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec))
        wxLaunchDefaultApplication(FromPath(path));
}

// "Phasmophobia" for ...\Phasmophobia\Phasmophobia.exe.
inline wxString GameTitle(const wxString &exe) {
    return FromPath(ToPath(exe).stem());
}

inline void ShowWindow(wxWindow *window, bool show) {
    if (window->IsShown() == show)
        return;
    window->Show(show);
    RelayoutAncestors(window->GetParent());
}

// Dark list used for file and process lists.
wxListCtrl *MakeList(wxWindow *parent, int minHeight);
// Proxy and injector tiles, same wording on both pages.
std::vector<ChoiceTiles::Item> LoaderTiles();

} // namespace UrkGui
