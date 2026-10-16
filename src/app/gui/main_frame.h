#pragma once

#include "activity_log.h"
#include "widgets.h"

#include <wx/frame.h>
#include <wx/string.h>
#include <wx/timer.h>

#include <atomic>
#include <functional>
#include <thread>

class wxSimplebook;

namespace UrkGui {

class Collapsible;
class LogView;
class Segmented;
class StatusStrip;
class NewProjectPage;
class UpdatePage;
class LoaderPage;
class AboutPage;

struct Settings {
    wxString gameExe;
    wxString projectName = "MyMod";
    wxString backend = "auto";
    wxString loaderMode = "proxy";
    wxString proxy = "version";
    wxString projectDir;
    bool localization = false;
    bool showAllProcesses = false;
    bool autoInject = true;

    void Load();
    void Save() const;
};

class MainFrame : public wxFrame {
  public:
    enum PageIndex {
        kPageNew,
        kPageUpdate,
        kPageLoader,
        kPageAbout,
    };

    explicit MainFrame(const std::wstring &openPath = {});
    ~MainFrame() override;

    Settings &settings() { return settings_; }

    // Shared game selection used by the New project and Loader pages.
    void SetGameExe(const wxString &path);
    void SetProjectDir(const wxString &path);
    // Loader mode ("proxy" or "injector") and proxy name, shared by the New project and Loader pages.
    void SetLoader(const wxString &mode, const wxString &proxy);
    void ShowPage(int index);

    // Runs work on a background thread; done runs on the UI thread afterwards.
    bool RunTask(const wxString &status, std::function<void()> work, std::function<void()> done = {});
    bool Busy() const { return busy_; }

    static void Log(UrkApp::LogLevel level, const std::string &source, const std::string &message);

  private:
    wxWindow *BuildHeader(wxWindow *parent);
    wxWindow *BuildLogDrawer(wxWindow *parent);
    void ToggleLog();
    void OnLogEntry(const UrkApp::LogEntry &entry);
    void OnClose(wxCloseEvent &event);
    void OnDropFiles(const wxArrayString &files);

    Settings settings_;
    Segmented *nav_ = nullptr;
    wxSimplebook *book_ = nullptr;
    Collapsible *drawer_ = nullptr;
    LogView *log_ = nullptr;
    StatusStrip *strip_ = nullptr;
    NewProjectPage *newPage_ = nullptr;
    UpdatePage *updatePage_ = nullptr;
    LoaderPage *loaderPage_ = nullptr;
    AboutPage *aboutPage_ = nullptr;
    Tween slide_;
    int slideFrom_ = 0;
    wxTimer slideTimer_;
    std::thread worker_;
    std::atomic<bool> busy_{false};
};

} // namespace UrkGui
