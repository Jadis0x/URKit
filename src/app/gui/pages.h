#pragma once

#include "loader_service.h"
#include "project_updater.h"

#include <wx/panel.h>
#include <wx/scrolwin.h>
#include <wx/timer.h>

#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

class wxListCtrl;
class wxStaticText;

namespace UrkGui {

class Button;
class ChoiceTiles;
class Collapsible;
class DropZone;
class MainFrame;
class Segmented;
class StatusCard;
class TextField;
class Toggle;
class WrapText;
struct ReleaseState;

// Scrollable page with a centred column of limited width.
class Page : public wxScrolled<wxPanel> {
  public:
    Page(wxWindow *parent, MainFrame *frame, const wxString &title);
    virtual void OnGameChanged() {}
    virtual void OnProjectChanged() {}
    virtual void OnShown() {}

  protected:
    wxBoxSizer *Column() const { return column_; }
    MainFrame *frame_;

  private:
    wxBoxSizer *column_ = nullptr;
};

class NewProjectPage : public Page {
  public:
    NewProjectPage(wxWindow *parent, MainFrame *frame);
    void OnGameChanged() override;
    void OnShown() override;

  private:
    void UpdateDerived();
    void Generate();

    DropZone *game_ = nullptr;
    StatusCard *gameProblem_ = nullptr;
    TextField *name_ = nullptr;
    ChoiceTiles *loader_ = nullptr;
    Collapsible *more_ = nullptr;
    Segmented *backend_ = nullptr;
    Segmented *proxy_ = nullptr;
    Toggle *locales_ = nullptr;
    wxStaticText *location_ = nullptr;
    Button *generate_ = nullptr;
    StatusCard *result_ = nullptr;
    Button *openFolder_ = nullptr;
    Button *goUpdate_ = nullptr;
    wxString lastProject_;
    bool projectExists_ = false;
};

class UpdatePage : public Page {
  public:
    UpdatePage(wxWindow *parent, MainFrame *frame);
    void OnProjectChanged() override;
    void OnShown() override;

  private:
    void Check();
    void Apply();
    void StageSdk();
    void ShowPlan();
    void ShowChanges();
    void ResetPlan();
    void SetAction(const wxString &label, bool primary);

    DropZone *project_ = nullptr;
    StatusCard *status_ = nullptr;
    Button *apply_ = nullptr;
    Button *openResult_ = nullptr;
    Collapsible *details_ = nullptr;
    wxListCtrl *changes_ = nullptr;
    Toggle *overwrite_ = nullptr;
    std::shared_ptr<UrkProject::UpdatePreview> preview_;
    std::filesystem::path resultFolder_;
    wxString checkedPath_;
};

class LoaderPage : public Page {
  public:
    LoaderPage(wxWindow *parent, MainFrame *frame);
    void OnGameChanged() override;
    void OnShown() override;

  private:
    void RefreshGame();
    void RefreshProxy();
    void RefreshInjector();
    void RefreshProcesses();
    void UpdateMode();
    void Install();
    void Remove();
    void InjectSelected();
    void InjectPid(std::uint32_t pid, const wxString &name);
    void OnTimer(wxTimerEvent &event);

    DropZone *game_ = nullptr;
    ChoiceTiles *mode_ = nullptr;
    wxPanel *proxyPanel_ = nullptr;
    wxPanel *injectorPanel_ = nullptr;
    StatusCard *proxyStatus_ = nullptr;
    Button *install_ = nullptr;
    Button *remove_ = nullptr;
    Segmented *proxy_ = nullptr;
    StatusCard *injectStatus_ = nullptr;
    Button *inject_ = nullptr;
    Toggle *auto_ = nullptr;
    Collapsible *other_ = nullptr;
    wxListCtrl *processes_ = nullptr;
    Toggle *showAll_ = nullptr;
    Button *injectSelected_ = nullptr;
    std::vector<UrkApp::ProcessInfo> processList_;
    std::optional<UrkApp::ProcessInfo> gameProcess_;
    wxTimer timer_;
    std::uint32_t autoInjectedPid_ = 0;
    std::uint32_t pendingPid_ = 0;
    int pendingTicks_ = 0;
    bool injecting_ = false;
};

class AboutPage : public Page {
  public:
    AboutPage(wxWindow *parent, MainFrame *frame);

  private:
    void CheckRelease();
    void InstallRelease();

    StatusCard *release_ = nullptr;
    Button *check_ = nullptr;
    Button *install_ = nullptr;
    std::shared_ptr<ReleaseState> state_;
};

} // namespace UrkGui
