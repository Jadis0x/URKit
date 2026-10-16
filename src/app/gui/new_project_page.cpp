#include "page_common.h"

#include "generator_service.h"

#include <wx/listctrl.h>

namespace UrkGui {
namespace {

constexpr const char *kBackendValues[] = {"auto", "mono", "il2cpp", "unreal"};

struct GenerateResult {
    bool ok = false;
    std::string error;
    std::string loaderError;
    std::string projectOut;
};

int ProxyIndex(const wxString &proxy) {
    for (int index = 0; index < 4; ++index)
        if (proxy == UrkApp::kProxyNames[index])
            return index;
    return 0;
}

} // namespace

std::vector<ChoiceTiles::Item> LoaderTiles() {
    return {
        {Theme::Icon::kLink, "Proxy", "Goes into the game folder and loads every time the game starts."},
        {Theme::Icon::kBolt, "Injector", "Nothing is added to the game. Inject it while the game is running."},
    };
}

wxListCtrl *MakeList(wxWindow *parent, int minHeight) {
    auto *list = new wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                                wxLC_REPORT | wxLC_SINGLE_SEL | wxBORDER_NONE);
    list->SetBackgroundColour(Theme::kInput);
    list->SetForegroundColour(Theme::kText);
    list->SetFont(Theme::UiFont(9.0));
    list->SetMinSize(wxSize(-1, parent->FromDIP(minHeight)));
    return list;
}

Page::Page(wxWindow *parent, MainFrame *frame, const wxString &title)
    : wxScrolled<wxPanel>(parent, wxID_ANY), frame_(frame) {
    SetBackgroundColour(Theme::kWindow);
    SetScrollRate(0, FromDIP(24));
    auto *outer = new wxBoxSizer(wxVERTICAL);
    column_ = new wxBoxSizer(wxVERTICAL);
    auto *heading = new wxStaticText(this, wxID_ANY, title);
    heading->SetFont(Theme::DisplayFont(19));
    heading->SetForegroundColour(Theme::kText);
    column_->Add(heading, 0, wxBOTTOM, FromDIP(18));
    outer->Add(column_, 0, wxALIGN_CENTER_HORIZONTAL | wxTOP | wxBOTTOM, FromDIP(20));
    SetSizer(outer);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        const int width = std::clamp(GetClientSize().x - FromDIP(64), FromDIP(360), FromDIP(680));
        column_->SetMinSize(wxSize(width, -1));
        event.Skip();
    });
}

NewProjectPage::NewProjectPage(wxWindow *parent, MainFrame *frame) : Page(parent, frame, "Create a mod") {
    const Settings &settings = frame_->settings();
    wxBoxSizer *column = Column();

    game_ = new DropZone(this, DropZone::Mode::File, "Drop your game's .exe here",
                         [this](const wxString &path) { frame_->SetGameExe(path); });
    column->Add(game_, 0, wxEXPAND);
    gameProblem_ = new StatusCard(this);
    goUpdate_ = new Button(gameProblem_, "Update it");
    gameProblem_->Actions()->Add(goUpdate_);
    goUpdate_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
        frame_->SetProjectDir(lastProject_);
        frame_->ShowPage(MainFrame::kPageUpdate);
    });
    gameProblem_->Hide();
    column->Add(gameProblem_, 0, wxEXPAND | wxTOP, FromDIP(10));

    column->AddSpacer(FromDIP(22));
    column->Add(MakeSectionLabel(this, "Mod name"), 0, wxBOTTOM, FromDIP(8));
    name_ = new TextField(this, settings.projectName, "MyMod");
    column->Add(name_, 0, wxEXPAND);

    column->AddSpacer(FromDIP(22));
    column->Add(MakeSectionLabel(this, "How the game loads it"), 0, wxBOTTOM, FromDIP(8));
    loader_ = new ChoiceTiles(this, LoaderTiles(), [this](int index) {
        frame_->SetLoader(index == 1 ? "injector" : "proxy", UrkApp::kProxyNames[proxy_->Selection()]);
        proxy_->Enable(index == 0);
    });
    loader_->Select(settings.loaderMode == "injector" ? 1 : 0, false);
    column->Add(loader_, 0, wxEXPAND);

    column->AddSpacer(FromDIP(14));
    more_ = new Collapsible(this, "More options");
    {
        wxPanel *content = more_->Content();
        auto *grid = new wxFlexGridSizer(2, FromDIP(12), FromDIP(16));
        grid->AddGrowableCol(1, 1);
        auto label = [content](const wxString &text) {
            wxStaticText *result = MakeLabel(content, text, true, 9.0);
            result->SetMinSize(wxSize(content->FromDIP(96), -1));
            return result;
        };
        backend_ = new Segmented(content, {"Auto", "Mono", "IL2CPP", "Unreal"}, Segmented::Style::Field,
                                 [this](int) { UpdateDerived(); });
        for (int index = 0; index < 4; ++index)
            if (settings.backend == kBackendValues[index])
                backend_->Select(index, false);
        grid->Add(label("Engine"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(backend_, 1, wxEXPAND);

        proxy_ = new Segmented(content, {"version.dll", "winhttp.dll", "winmm.dll", "dwmapi.dll"},
                               Segmented::Style::Field, [this](int index) {
                                   frame_->SetLoader(loader_->Selection() == 1 ? "injector" : "proxy",
                                                     UrkApp::kProxyNames[index]);
                               });
        proxy_->Select(ProxyIndex(settings.proxy), false);
        proxy_->Enable(loader_->Selection() == 0);
        grid->Add(label("Proxy file"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(proxy_, 1, wxEXPAND);

        locales_ = new Toggle(content, "Add translation files");
        locales_->SetValue(settings.localization);
        grid->Add(label("Languages"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(locales_, 0, wxALIGN_CENTER_VERTICAL);

        location_ = new wxStaticText(content, wxID_ANY, "-", wxDefaultPosition, wxDefaultSize,
                                     wxST_ELLIPSIZE_MIDDLE | wxST_NO_AUTORESIZE);
        location_->SetFont(Theme::UiFont(9.0));
        location_->SetForegroundColour(Theme::kTextMuted);
        location_->SetMinSize(wxSize(FromDIP(120), -1));
        grid->Add(label("Saved to"), 0, wxALIGN_CENTER_VERTICAL);
        grid->Add(location_, 1, wxEXPAND | wxALIGN_CENTER_VERTICAL);

        auto *contentSizer = new wxBoxSizer(wxVERTICAL);
        contentSizer->Add(grid, 0, wxEXPAND | wxLEFT | wxTOP | wxBOTTOM, FromDIP(8));
        content->SetSizer(contentSizer);
    }
    column->Add(more_, 0, wxEXPAND);

    column->AddSpacer(FromDIP(20));
    auto *actions = new wxBoxSizer(wxHORIZONTAL);
    actions->AddStretchSpacer();
    generate_ = new Button(this, "Create project", Button::Kind::Primary, Theme::Icon::kAdd);
    actions->Add(generate_);
    column->Add(actions, 0, wxEXPAND);

    result_ = new StatusCard(this);
    openFolder_ = new Button(result_, "Open folder", Button::Kind::Secondary, Theme::Icon::kFolder);
    result_->Actions()->Add(openFolder_);
    result_->Hide();
    column->Add(result_, 0, wxEXPAND | wxTOP, FromDIP(16));

    name_->Ctrl()->Bind(wxEVT_TEXT, [this](wxCommandEvent &) {
        ShowWindow(result_, false);
        UpdateDerived();
    });
    generate_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Generate(); });
    openFolder_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OpenFolder(ToPath(lastProject_)); });
    UpdateDerived();
}

void NewProjectPage::OnGameChanged() {
    const Settings &settings = frame_->settings();
    const int mode = settings.loaderMode == "injector" ? 1 : 0;
    if (loader_->Selection() != mode)
        loader_->Select(mode);
    if (proxy_->Selection() != ProxyIndex(settings.proxy))
        proxy_->Select(ProxyIndex(settings.proxy));
    proxy_->Enable(mode == 0);
    UpdateDerived();
}

void NewProjectPage::OnShown() {
    UpdateDerived();
}

void NewProjectPage::UpdateDerived() {
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    UrkApp::ProjectOptions options;
    options.gameExePath = exe.utf8_string();
    options.projectName = name_->Ctrl()->GetValue().Strip(wxString::both).utf8_string();
    options.backendSelection = kBackendValues[std::max(0, backend_->Selection())];
    projectExists_ = false;
    if (exe.empty()) {
        game_->SetEmpty();
        ShowWindow(gameProblem_, false);
        location_->SetLabel("-");
        generate_->Enable(false);
        return;
    }
    std::string error;
    if (!UrkApp::ResolveProjectOptions(options, error)) {
        game_->SetFilled(exe, GameTitle(exe), wxEmptyString, Theme::kTextMuted);
        goUpdate_->Hide();
        gameProblem_->Set(StatusCard::Tone::Warning, "Can't create a project yet", FromUtf8(error));
        ShowWindow(gameProblem_, true);
        location_->SetLabel("-");
        generate_->Enable(false);
        return;
    }
    game_->SetFilled(exe, GameTitle(exe), UrkApp::BackendDisplayName(options.backend), Theme::kSuccess);
    std::error_code ec;
    projectExists_ = std::filesystem::exists(ToPath(FromUtf8(options.projectOut)) / "CMakeLists.txt", ec);
    // Right after creating it, the result card already says where the project is.
    const bool justCreated = result_->IsShown() && lastProject_ == FromUtf8(options.projectOut);
    if (projectExists_ && !justCreated) {
        lastProject_ = FromUtf8(options.projectOut);
        goUpdate_->Show();
        gameProblem_->Set(StatusCard::Tone::Info, "This mod already exists",
                          "Pick another name, or update the existing project.");
        gameProblem_->Refit();
    }
    ShowWindow(gameProblem_, projectExists_ && !justCreated);
    location_->SetLabel(FromUtf8(options.projectOut));
    location_->SetToolTip(FromUtf8(options.projectOut));
    generate_->Enable(!frame_->Busy() && !projectExists_);
}

void NewProjectPage::Generate() {
    Settings &settings = frame_->settings();
    settings.projectName = name_->Ctrl()->GetValue().Strip(wxString::both);
    settings.backend = kBackendValues[std::max(0, backend_->Selection())];
    settings.localization = locales_->GetValue();
    settings.loaderMode = loader_->Selection() == 1 ? "injector" : "proxy";
    settings.proxy = UrkApp::kProxyNames[std::max(0, proxy_->Selection())];
    settings.Save();

    UrkApp::ProjectOptions options;
    options.gameExePath = settings.gameExe.Strip(wxString::both).utf8_string();
    options.projectName = settings.projectName.utf8_string();
    options.backendSelection = settings.backend.utf8_string();
    options.enableLocalization = settings.localization;
    std::string error;
    if (!UrkApp::ResolveProjectOptions(options, error)) {
        MainFrame::Log(UrkApp::LogLevel::Error, "generate", error);
        UpdateDerived();
        return;
    }

    const bool installProxy = settings.loaderMode == "proxy";
    const std::string proxy = settings.proxy.utf8_string();
    bool replaceForeign = false;
    if (installProxy) {
        for (const UrkApp::InstalledLoader &loader : UrkApp::ScanGameDirectory(options.gameDir)) {
            if (loader.fileName != proxy + ".dll" || loader.urkit)
                continue;
            const wxString question = FromUtf8(loader.fileName) +
                                      " in the game folder belongs to another program.\n\nRename it to " +
                                      FromUtf8(loader.fileName) + ".urk-backup and install URKit?";
            if (wxMessageBox(question, "File in use", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
                return;
            replaceForeign = true;
        }
    }

    ShowWindow(result_, false);
    generate_->SetLabel("Creating...");
    generate_->SetBusy(true);
    auto result = std::make_shared<GenerateResult>();
    const bool injector = settings.loaderMode == "injector";
    frame_->RunTask(
        "Creating project",
        [options, result, installProxy, proxy, replaceForeign, injector] {
            const UrkApp::LogSink log = UrkApp::ActivityLog::Instance().SinkFor("generate");
            if (!UrkApp::GenerateProject(options, log, result->error)) {
                log(UrkApp::LogLevel::Error, "Generation failed: " + result->error);
                return;
            }
            result->projectOut = options.projectOut;
            result->ok = true;
            if (installProxy) {
                const UrkApp::LogSink loaderLog = UrkApp::ActivityLog::Instance().SinkFor("loader");
                if (!UrkApp::InstallProxy(options.gameDir, proxy, replaceForeign, loaderLog, result->loaderError))
                    loaderLog(UrkApp::LogLevel::Error, "Proxy not installed: " + result->loaderError);
            } else if (injector) {
                log(UrkApp::LogLevel::Info, "Injector mode: start the game, then inject from the Loader page");
            }
        },
        [this, result, injector] {
            generate_->SetBusy(false);
            generate_->SetLabel("Create project");
            UpdateDerived();
            if (!result->ok) {
                openFolder_->Hide();
                result_->Set(StatusCard::Tone::Error, "Couldn't create the project", FromUtf8(result->error));
            } else {
                lastProject_ = FromUtf8(result->projectOut);
                frame_->SetProjectDir(lastProject_);
                openFolder_->Show();
                if (!result->loaderError.empty())
                    result_->Set(StatusCard::Tone::Warning, "Project created, loader not installed",
                                 FromUtf8(result->loaderError));
                else
                    result_->Set(StatusCard::Tone::Success, "Project created",
                                 injector ? wxString("Build it, start the game, then inject it from the Loader page.")
                                          : wxString("Build it, then start the game."));
            }
            result_->Refit();
            ShowWindow(result_, true);
            Scroll(0, GetVirtualSize().y);
        });
}

} // namespace UrkGui
