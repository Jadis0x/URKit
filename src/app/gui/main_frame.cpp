#include "main_frame.h"

#include "pages.h"
#include "project_version.h"
#include "theme.h"
#include "widgets.h"

#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/dnd.h>
#include <wx/fileconf.h>
#include <wx/graphics.h>
#include <wx/iconbndl.h>
#include <wx/msw/private.h>
#include <wx/filename.h>
#include <wx/msgdlg.h>
#include <wx/simplebook.h>
#include <wx/sizer.h>
#include <wx/utils.h>

#include <cmath>
#include <memory>

namespace UrkGui {
namespace {

std::unique_ptr<wxFileConfig> OpenConfig() {
    const std::filesystem::path path = UrkApp::LocalDataDirectory() / L"settings.ini";
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    return std::make_unique<wxFileConfig>(wxEmptyString, wxEmptyString, wxString(path.wstring()), wxEmptyString,
                                          wxCONFIG_USE_LOCAL_FILE);
}

class FileDrop : public wxFileDropTarget {
  public:
    explicit FileDrop(std::function<void(const wxArrayString &)> handler) : handler_(std::move(handler)) {}
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString &files) override {
        handler_(files);
        return true;
    }

  private:
    std::function<void(const wxArrayString &)> handler_;
};

} // namespace

void Settings::Load() {
    auto config = OpenConfig();
    config->Read("game/exe", &gameExe);
    config->Read("project/name", &projectName, projectName);
    config->Read("project/backend", &backend, backend);
    config->Read("project/localization", &localization, localization);
    config->Read("project/dir", &projectDir);
    config->Read("loader/mode", &loaderMode, loaderMode);
    config->Read("loader/proxy", &proxy, proxy);
    config->Read("loader/show_all_processes", &showAllProcesses, showAllProcesses);
    config->Read("loader/auto_inject", &autoInject, autoInject);
    if (loaderMode != "injector")
        loaderMode = "proxy";
}

void Settings::Save() const {
    auto config = OpenConfig();
    config->Write("game/exe", gameExe);
    config->Write("project/name", projectName);
    config->Write("project/backend", backend);
    config->Write("project/localization", localization);
    config->Write("project/dir", projectDir);
    config->Write("loader/mode", loaderMode);
    config->Write("loader/proxy", proxy);
    config->Write("loader/show_all_processes", showAllProcesses);
    config->Write("loader/auto_inject", autoInject);
    config->Flush();
}

MainFrame::MainFrame(const std::wstring &openPath)
    : wxFrame(nullptr, wxID_ANY, "URKit", wxDefaultPosition, wxDefaultSize, wxDEFAULT_FRAME_STYLE),
      slideTimer_(this) {
    settings_.Load();
    SetBackgroundColour(Theme::kWindow);
    SetFont(Theme::UiFont());
    // The whole icon group, so the title bar and the taskbar each get a size that fits their DPI.
    SetIcons(wxIconBundle(wxS("urk_app"), wxGetInstance()));

    auto *root = new wxPanel(this);
    root->SetBackgroundColour(Theme::kWindow);
    auto *rootSizer = new wxBoxSizer(wxVERTICAL);
    rootSizer->Add(BuildHeader(root), 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(18));

    book_ = new wxSimplebook(root);
    book_->SetBackgroundColour(Theme::kWindow);
    newPage_ = new NewProjectPage(book_, this);
    updatePage_ = new UpdatePage(book_, this);
    loaderPage_ = new LoaderPage(book_, this);
    aboutPage_ = new AboutPage(book_, this);
    for (Page *page : std::initializer_list<Page *>{newPage_, updatePage_, loaderPage_, aboutPage_})
        book_->AddPage(page, wxEmptyString);
    rootSizer->Add(book_, 1, wxEXPAND | wxTOP, FromDIP(6));
    rootSizer->Add(BuildLogDrawer(root), 0, wxEXPAND);
    strip_ = new StatusStrip(root, [this] { ToggleLog(); });
    rootSizer->Add(strip_, 0, wxEXPAND);
    root->SetSizer(rootSizer);

    SetDropTarget(new FileDrop([this](const wxArrayString &files) { OnDropFiles(files); }));
    Bind(wxEVT_CLOSE_WINDOW, &MainFrame::OnClose, this);
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        wxWindow *page = book_->GetCurrentPage();
        if (!page)
            return;
        const double t = slide_.Value();
        page->Move(static_cast<int>(std::lround(slideFrom_ * (1.0 - t))), 0);
        if (!slide_.Running()) {
            page->Move(0, 0);
            slideTimer_.Stop();
        }
    });

    UrkApp::ActivityLog::Instance().SetListener([this](const UrkApp::LogEntry &entry) {
        if (wxThread::IsMain())
            OnLogEntry(entry);
        else
            CallAfter([this, entry] { OnLogEntry(entry); });
    });

    SetMinSize(FromDIP(wxSize(820, 640)));
    SetSize(FromDIP(wxSize(980, 760)));
    CentreOnScreen();
    ShowPage(kPageNew);
    strip_->SetMessage(UrkApp::LogLevel::Info, "Ready");
    Log(UrkApp::LogLevel::Debug, "session",
        "urk.exe " + std::string(UrkVersion::kReleaseVersion) + ", log file " +
            UrkApp::ActivityLog::Instance().FilePath().string());
    newPage_->OnGameChanged();
    loaderPage_->OnGameChanged();
    updatePage_->OnProjectChanged();
    if (!openPath.empty()) {
        wxArrayString opened;
        opened.Add(wxString(openPath));
        CallAfter([this, opened] { OnDropFiles(opened); });
    }
    CallAfter([this] { nav_->SetFocus(); });
}

MainFrame::~MainFrame() {
    UrkApp::ActivityLog::Instance().SetListener({});
    if (worker_.joinable())
        worker_.join();
}

wxWindow *MainFrame::BuildHeader(wxWindow *parent) {
    auto *header = new wxPanel(parent);
    header->SetBackgroundColour(Theme::kWindow);
    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->AddStretchSpacer();
    nav_ = new Segmented(header, {"Create", "Update", "Loader", "About"}, Segmented::Style::Nav,
                         [this](int index) { ShowPage(index); });
    sizer->Add(nav_, 0, wxALIGN_CENTER_VERTICAL);
    sizer->AddStretchSpacer();
    header->SetSizer(sizer);
    return header;
}

wxWindow *MainFrame::BuildLogDrawer(wxWindow *parent) {
    drawer_ = new Collapsible(parent, wxEmptyString);
    wxPanel *content = drawer_->Content();
    content->SetBackgroundColour(Theme::kSurface);
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    auto *line = new wxPanel(content, wxID_ANY, wxDefaultPosition, wxSize(-1, 1));
    line->SetBackgroundColour(Theme::kBorder);
    sizer->Add(line, 0, wxEXPAND);

    auto *tools = new wxBoxSizer(wxHORIZONTAL);
    tools->Add(MakeSectionLabel(content, "Activity"), 0, wxALIGN_CENTER_VERTICAL);
    tools->AddStretchSpacer();
    auto *copy = new Button(content, "Copy", Button::Kind::Quiet, Theme::Icon::kCopy);
    auto *open = new Button(content, "Log folder", Button::Kind::Quiet, Theme::Icon::kFolder);
    auto *clear = new Button(content, "Clear", Button::Kind::Quiet, Theme::Icon::kDelete);
    tools->Add(copy, 0, wxLEFT, FromDIP(2));
    tools->Add(open, 0, wxLEFT, FromDIP(2));
    tools->Add(clear, 0, wxLEFT, FromDIP(2));
    sizer->Add(tools, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, FromDIP(12));

    log_ = new LogView(content);
    log_->SetMinSize(FromDIP(wxSize(-1, 150)));
    sizer->Add(log_, 0, wxEXPAND | wxALL, FromDIP(12));
    content->SetSizer(sizer);

    copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(log_->Text(false)));
            wxTheClipboard->Close();
        }
    });
    clear->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { log_->Clear(); });
    open->Bind(wxEVT_BUTTON, [](wxCommandEvent &) {
        const std::filesystem::path directory = UrkApp::ActivityLog::Instance().Directory();
        std::error_code ec;
        std::filesystem::create_directories(directory, ec);
        wxLaunchDefaultApplication(wxString(directory.wstring()));
    });
    return drawer_;
}

void MainFrame::ToggleLog() {
    const bool open = !drawer_->IsExpanded();
    drawer_->SetExpanded(open);
    strip_->SetOpen(open);
}

void MainFrame::OnLogEntry(const UrkApp::LogEntry &entry) {
    log_->Append(entry);
    if (entry.level != UrkApp::LogLevel::Debug)
        strip_->SetMessage(entry.level, wxString::FromUTF8(entry.message));
}

void MainFrame::ShowPage(int index) {
    if (index < 0 || index >= static_cast<int>(book_->GetPageCount()))
        return;
    const int previous = book_->GetSelection();
    nav_->Select(index, previous != wxNOT_FOUND);
    if (previous == index)
        return;
    book_->ChangeSelection(static_cast<size_t>(index));
    auto *page = static_cast<Page *>(book_->GetPage(static_cast<size_t>(index)));
    page->OnShown();
    if (previous != wxNOT_FOUND) {
        // Short slide in the direction of travel.
        slideFrom_ = (index > previous ? 1 : -1) * FromDIP(32);
        slide_.Jump(0.0);
        slide_.Set(1.0, 280);
        page->Move(slideFrom_, 0);
        slideTimer_.Start(15);
    }
}

void MainFrame::SetGameExe(const wxString &path) {
    if (settings_.gameExe == path)
        return;
    settings_.gameExe = path;
    settings_.Save();
    newPage_->OnGameChanged();
    loaderPage_->OnGameChanged();
}

void MainFrame::SetProjectDir(const wxString &path) {
    if (settings_.projectDir == path)
        return;
    settings_.projectDir = path;
    settings_.Save();
    updatePage_->OnProjectChanged();
}

void MainFrame::SetLoader(const wxString &mode, const wxString &proxy) {
    if (settings_.loaderMode == mode && settings_.proxy == proxy)
        return;
    settings_.loaderMode = mode;
    settings_.proxy = proxy;
    settings_.Save();
    newPage_->OnGameChanged();
    loaderPage_->OnGameChanged();
}

bool MainFrame::RunTask(const wxString &status, std::function<void()> work, std::function<void()> done) {
    if (busy_) {
        wxBell();
        return false;
    }
    if (worker_.joinable())
        worker_.join();
    busy_ = true;
    strip_->SetBusy(status + "...");
    worker_ = std::thread([this, work = std::move(work), done = std::move(done)]() mutable {
        try {
            work();
        } catch (const std::exception &exception) {
            Log(UrkApp::LogLevel::Error, "app", std::string("Unexpected error: ") + exception.what());
        }
        CallAfter([this, done = std::move(done)] {
            busy_ = false;
            strip_->SetBusy(wxEmptyString);
            if (done)
                done();
        });
    });
    return true;
}

void MainFrame::Log(UrkApp::LogLevel level, const std::string &source, const std::string &message) {
    UrkApp::ActivityLog::Instance().Write(level, source, message);
}

void MainFrame::OnClose(wxCloseEvent &event) {
    if (busy_ && event.CanVeto()) {
        wxMessageBox("URKit is still working. Close it when it is done.", "URKit", wxOK | wxICON_INFORMATION, this);
        event.Veto();
        return;
    }
    settings_.Save();
    event.Skip();
}

void MainFrame::OnDropFiles(const wxArrayString &files) {
    if (files.empty())
        return;
    const wxFileName dropped(files[0]);
    if (wxDirExists(files[0])) {
        SetProjectDir(files[0]);
        ShowPage(kPageUpdate);
        return;
    }
    if (dropped.GetExt().Lower() == "exe") {
        SetGameExe(files[0]);
        if (book_->GetSelection() != kPageNew && book_->GetSelection() != kPageLoader)
            ShowPage(kPageNew);
        return;
    }
    Log(UrkApp::LogLevel::Warning, "app", "Drop the game's .exe or a project folder");
}

} // namespace UrkGui
