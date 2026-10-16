#include "gui_app.h"

#include "main_frame.h"
#include "widgets.h"

#include <wx/app.h>
#include <wx/image.h>

namespace UrkGui {
namespace {

std::wstring g_openPath;

class GuiApp : public wxApp {
  public:
    bool OnInit() override {
        MSWEnableDarkMode(DarkMode_Always);
        wxInitAllImageHandlers();
        SetAppName("URKit");
        SetAppDisplayName("URKit");
        auto *frame = new MainFrame(g_openPath);
        frame->Show();
        SetTopWindow(frame);
        return true;
    }

    int FilterEvent(wxEvent &event) override {
        const wxEventType type = event.GetEventType();
        if (type == wxEVT_LEFT_DOWN || type == wxEVT_RIGHT_DOWN) {
            SetFocusRingVisible(false);
        } else if (type == wxEVT_KEY_DOWN) {
            const int key = static_cast<wxKeyEvent &>(event).GetKeyCode();
            if (key == WXK_TAB || key == WXK_LEFT || key == WXK_RIGHT || key == WXK_UP || key == WXK_DOWN)
                SetFocusRingVisible(true);
        }
        return Event_Skip;
    }
};

} // namespace

int RunGui(HINSTANCE instance, int show, const std::wstring &openPath) {
    g_openPath = openPath;
    wxApp::SetInstance(new GuiApp());
    return wxEntry(instance, nullptr, nullptr, show);
}

} // namespace UrkGui
