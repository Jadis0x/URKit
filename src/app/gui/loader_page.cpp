#include "page_common.h"

#include <wx/listctrl.h>

namespace UrkGui {
namespace {

// Seconds between seeing the game process and injecting into it.
constexpr int kAutoInjectDelaySeconds = 3;

std::filesystem::path GameDirectory(const wxString &exe) {
    return ToPath(exe.Strip(wxString::both)).parent_path();
}

int ProxyIndex(const wxString &proxy) {
    for (int index = 0; index < 4; ++index)
        if (proxy == UrkApp::kProxyNames[index])
            return index;
    return 0;
}

bool GameSelected(const wxString &exe) {
    std::error_code ec;
    return !exe.empty() && std::filesystem::is_regular_file(ToPath(exe), ec);
}

} // namespace

LoaderPage::LoaderPage(wxWindow *parent, MainFrame *frame) : Page(parent, frame, "Loader"), timer_(this) {
    const Settings &settings = frame_->settings();
    wxBoxSizer *column = Column();

    game_ = new DropZone(this, DropZone::Mode::File, "Drop your game's .exe here",
                         [this](const wxString &path) { frame_->SetGameExe(path); });
    column->Add(game_, 0, wxEXPAND);

    column->AddSpacer(FromDIP(22));
    column->Add(MakeSectionLabel(this, "How the game loads it"), 0, wxBOTTOM, FromDIP(8));
    mode_ = new ChoiceTiles(this, LoaderTiles(), [this](int index) {
        frame_->SetLoader(index == 1 ? "injector" : "proxy", UrkApp::kProxyNames[proxy_->Selection()]);
        UpdateMode();
    });
    mode_->Select(settings.loaderMode == "injector" ? 1 : 0, false);
    column->Add(mode_, 0, wxEXPAND | wxBOTTOM, FromDIP(14));

    proxyPanel_ = new wxPanel(this);
    proxyPanel_->SetBackgroundColour(Theme::kWindow);
    {
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        proxyStatus_ = new StatusCard(proxyPanel_);
        remove_ = new Button(proxyStatus_, "Remove", Button::Kind::Secondary, Theme::Icon::kDelete);
        install_ = new Button(proxyStatus_, "Install", Button::Kind::Primary, Theme::Icon::kDownload);
        proxyStatus_->Actions()->Add(remove_, 0, wxRIGHT, FromDIP(8));
        proxyStatus_->Actions()->Add(install_);
        sizer->Add(proxyStatus_, 0, wxEXPAND | wxBOTTOM, FromDIP(14));

        auto *more = new Collapsible(proxyPanel_, "Proxy file");
        wxPanel *content = more->Content();
        auto *contentSizer = new wxBoxSizer(wxVERTICAL);
        proxy_ = new Segmented(content, {"version.dll", "winhttp.dll", "winmm.dll", "dwmapi.dll"},
                               Segmented::Style::Field, [this](int index) {
                                   frame_->SetLoader("proxy", UrkApp::kProxyNames[index]);
                                   RefreshProxy();
                               });
        proxy_->Select(ProxyIndex(settings.proxy), false);
        contentSizer->Add(proxy_, 0, wxEXPAND | wxTOP | wxLEFT, FromDIP(6));
        contentSizer->Add(new WrapText(content,
                                       "version.dll works for most games. If URKit does not start, "
                                       "remove it and try another one.",
                                       true, 9.0),
                          0, wxEXPAND | wxTOP | wxLEFT | wxBOTTOM, FromDIP(8));
        content->SetSizer(contentSizer);
        sizer->Add(more, 0, wxEXPAND);
        proxyPanel_->SetSizer(sizer);

        install_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Install(); });
        remove_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Remove(); });
    }
    column->Add(proxyPanel_, 0, wxEXPAND);

    injectorPanel_ = new wxPanel(this);
    injectorPanel_->SetBackgroundColour(Theme::kWindow);
    {
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        injectStatus_ = new StatusCard(injectorPanel_);
        inject_ = new Button(injectStatus_, "Inject", Button::Kind::Primary, Theme::Icon::kBolt);
        injectStatus_->Actions()->Add(inject_);
        sizer->Add(injectStatus_, 0, wxEXPAND | wxBOTTOM, FromDIP(12));

        auto_ = new Toggle(injectorPanel_, "Inject automatically when the game starts", [this](bool value) {
            frame_->settings().autoInject = value;
            frame_->settings().Save();
            pendingPid_ = 0;
            pendingTicks_ = 0;
            RefreshInjector();
        });
        auto_->SetValue(settings.autoInject);
        sizer->Add(auto_, 0, wxBOTTOM, FromDIP(10));

        other_ = new Collapsible(injectorPanel_, "Another process");
        wxPanel *content = other_->Content();
        auto *contentSizer = new wxBoxSizer(wxVERTICAL);
        processes_ = MakeList(content, 180);
        processes_->AppendColumn("Process", wxLIST_FORMAT_LEFT, FromDIP(190));
        processes_->AppendColumn("PID", wxLIST_FORMAT_RIGHT, FromDIP(64));
        processes_->AppendColumn("Window", wxLIST_FORMAT_LEFT, FromDIP(320));
        contentSizer->Add(processes_, 0, wxEXPAND | wxTOP, FromDIP(6));
        auto *tools = new wxBoxSizer(wxHORIZONTAL);
        showAll_ = new Toggle(content, "Show all", [this](bool value) {
            frame_->settings().showAllProcesses = value;
            RefreshProcesses();
        });
        showAll_->SetValue(settings.showAllProcesses);
        auto *refresh = new Button(content, "Refresh", Button::Kind::Quiet, Theme::Icon::kRefresh);
        injectSelected_ = new Button(content, "Inject", Button::Kind::Secondary, Theme::Icon::kBolt);
        tools->Add(showAll_, 0, wxALIGN_CENTER_VERTICAL);
        tools->AddStretchSpacer();
        tools->Add(refresh, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(6));
        tools->Add(injectSelected_, 0, wxALIGN_CENTER_VERTICAL);
        contentSizer->Add(tools, 0, wxEXPAND | wxTOP | wxBOTTOM, FromDIP(10));
        content->SetSizer(contentSizer);
        sizer->Add(other_, 0, wxEXPAND);
        injectorPanel_->SetSizer(sizer);

        inject_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            if (gameProcess_)
                InjectPid(gameProcess_->pid, wxString(gameProcess_->name));
        });
        refresh->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { RefreshProcesses(); });
        injectSelected_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { InjectSelected(); });
        processes_->Bind(wxEVT_LIST_ITEM_ACTIVATED, [this](wxListEvent &) { InjectSelected(); });
        processes_->Bind(wxEVT_LIST_ITEM_SELECTED,
                         [this](wxListEvent &) { injectSelected_->Enable(!frame_->Busy()); });
        processes_->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
            event.Skip();
            CallAfter([this] {
                const int fixed = processes_->GetColumnWidth(0) + processes_->GetColumnWidth(1) + FromDIP(4);
                processes_->SetColumnWidth(2, std::max(FromDIP(140), processes_->GetClientSize().x - fixed));
            });
        });
    }
    column->Add(injectorPanel_, 0, wxEXPAND);

    Bind(wxEVT_TIMER, &LoaderPage::OnTimer, this);
    timer_.Start(1000);
    UpdateMode();
}

void LoaderPage::OnGameChanged() {
    const Settings &settings = frame_->settings();
    const int mode = settings.loaderMode == "injector" ? 1 : 0;
    if (mode_->Selection() != mode)
        mode_->Select(mode);
    if (proxy_->Selection() != ProxyIndex(settings.proxy))
        proxy_->Select(ProxyIndex(settings.proxy));
    autoInjectedPid_ = 0;
    pendingPid_ = 0;
    UpdateMode();
}

void LoaderPage::OnShown() {
    RefreshGame();
    RefreshProxy();
    RefreshInjector();
    RefreshProcesses();
}

void LoaderPage::UpdateMode() {
    const bool proxy = mode_->Selection() == 0;
    proxyPanel_->Show(proxy);
    injectorPanel_->Show(!proxy);
    RefreshGame();
    RefreshProxy();
    RefreshInjector();
    if (!proxy)
        RefreshProcesses();
    RelayoutAncestors(proxyPanel_);
}

void LoaderPage::RefreshGame() {
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    if (exe.empty()) {
        game_->SetEmpty();
        gameProcess_.reset();
        return;
    }
    gameProcess_ = GameSelected(exe) ? UrkApp::FindProcessByImage(ToPath(exe)) : std::nullopt;
    if (!gameProcess_)
        game_->SetFilled(exe, GameTitle(exe), wxEmptyString, Theme::kTextMuted);
    else if (gameProcess_->urkitLoaded)
        game_->SetFilled(exe, GameTitle(exe), "URKit running", Theme::kSuccess);
    else
        game_->SetFilled(exe, GameTitle(exe), "Running", Theme::kAccentHover);
}

void LoaderPage::RefreshProxy() {
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    const bool busy = frame_->Busy();
    if (!GameSelected(exe)) {
        install_->Hide();
        remove_->Hide();
        proxyStatus_->Set(StatusCard::Tone::Neutral, "Pick a game first", "Drop its .exe above.");
        proxyStatus_->Refit();
        return;
    }
    const wxString selected = wxString(UrkApp::kProxyNames[proxy_->Selection()]) + ".dll";
    const UrkApp::InstalledLoader *urkit = nullptr;
    const UrkApp::InstalledLoader *foreign = nullptr;
    const std::vector<UrkApp::InstalledLoader> loaders = UrkApp::ScanGameDirectory(GameDirectory(exe));
    for (const UrkApp::InstalledLoader &loader : loaders) {
        if (loader.urkit && !urkit)
            urkit = &loader;
        else if (!loader.urkit && FromUtf8(loader.fileName).CmpNoCase(selected) == 0)
            foreign = &loader;
    }
    if (urkit) {
        // Offer the proxy that is already there.
        const int index = ProxyIndex(FromUtf8(urkit->fileName).BeforeLast('.'));
        if (index != proxy_->Selection()) {
            proxy_->Select(index, false);
            frame_->settings().proxy = UrkApp::kProxyNames[index];
        }
    }
    remove_->Show(urkit != nullptr);
    remove_->Enable(!busy);
    install_->Enable(!busy);
    if (urkit && urkit->matchesBundled) {
        install_->Hide();
        proxyStatus_->Set(StatusCard::Tone::Success, "Installed",
                          FromUtf8(urkit->fileName) + " is in the game folder. Start the game to load URKit.");
    } else if (urkit) {
        install_->SetLabel("Update");
        install_->Show();
        proxyStatus_->Set(StatusCard::Tone::Warning, "Another version is installed",
                          FromUtf8(urkit->fileName) + " is from a different URKit build.");
    } else if (foreign) {
        install_->SetLabel("Install");
        install_->Show();
        proxyStatus_->Set(StatusCard::Tone::Warning, selected + " belongs to another program",
                          "Installing renames it to " + selected + ".urk-backup. Remove brings it back.");
    } else {
        install_->SetLabel("Install");
        install_->Show();
        proxyStatus_->Set(StatusCard::Tone::Neutral, "Not installed",
                          "Copies " + selected + " into the game folder.", Theme::Icon::kLink);
    }
    proxyStatus_->Refit();
}

void LoaderPage::RefreshInjector() {
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    const wxString name = GameTitle(exe);
    inject_->Enable(!frame_->Busy());
    bool showInject = false;
    if (injecting_) {
        showInject = true;
    } else if (!GameSelected(exe)) {
        injectStatus_->Set(StatusCard::Tone::Neutral, "Pick a game first", "Drop its .exe above.");
    } else if (gameProcess_ && gameProcess_->urkitLoaded) {
        injectStatus_->Set(StatusCard::Tone::Success, "URKit is running", "Loaded into " + name + ".");
    } else if (gameProcess_ && auto_->GetValue() && pendingPid_ == gameProcess_->pid) {
        injectStatus_->Set(StatusCard::Tone::Busy, name + " started",
                           wxString::Format("Injecting in %d s.", std::max(1, kAutoInjectDelaySeconds - pendingTicks_)));
    } else if (gameProcess_) {
        injectStatus_->Set(StatusCard::Tone::Info, name + " is running", "Inject URKit now.", Theme::Icon::kPlay);
        showInject = true;
    } else if (auto_->GetValue()) {
        injectStatus_->Set(StatusCard::Tone::Waiting, "Waiting for " + name,
                           "Start the game. URKit is injected a few seconds after it opens.");
    } else {
        injectStatus_->Set(StatusCard::Tone::Neutral, name + " is not running", "Start the game, then press Inject.");
    }
    if (inject_->IsShown() != showInject) {
        inject_->Show(showInject);
        injectStatus_->Refit();
    }
}

void LoaderPage::RefreshProcesses() {
    if (mode_->Selection() != 1)
        return;
    processList_ = UrkApp::ListProcesses(!showAll_->GetValue());
    processes_->DeleteAllItems();
    const std::filesystem::path game = ToPath(frame_->settings().gameExe.Strip(wxString::both));
    long gameRow = -1;
    long row = 0;
    for (const UrkApp::ProcessInfo &process : processList_) {
        const long index = processes_->InsertItem(row++, wxString(process.name));
        processes_->SetItem(index, 1, wxString::Format("%u", process.pid));
        processes_->SetItem(index, 2, wxString(process.windowTitle));
        processes_->SetItemBackgroundColour(index, Theme::kInput);
        if (process.urkitLoaded)
            processes_->SetItemTextColour(index, Theme::kSuccess);
        if (!game.empty() && _wcsicmp(process.imagePath.c_str(), game.wstring().c_str()) == 0) {
            gameRow = index;
            if (!process.urkitLoaded)
                processes_->SetItemTextColour(index, Theme::kAccentHover);
        }
    }
    if (gameRow >= 0) {
        processes_->SetItemState(gameRow, wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED,
                                 wxLIST_STATE_SELECTED | wxLIST_STATE_FOCUSED);
        processes_->EnsureVisible(gameRow);
    }
    injectSelected_->Enable(processes_->GetSelectedItemCount() > 0 && !frame_->Busy());
}

void LoaderPage::Install() {
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    if (!GameSelected(exe))
        return;
    const std::filesystem::path gameDir = GameDirectory(exe);
    const std::string proxy = UrkApp::kProxyNames[proxy_->Selection()];
    frame_->SetLoader("proxy", proxy);
    bool replaceForeign = false;
    for (const UrkApp::InstalledLoader &loader : UrkApp::ScanGameDirectory(gameDir)) {
        if (loader.fileName != proxy + ".dll" || loader.urkit)
            continue;
        const wxString question = FromUtf8(loader.fileName) +
                                  " in the game folder belongs to another program.\n\nRename it to " +
                                  FromUtf8(loader.fileName) + ".urk-backup and install URKit?";
        if (wxMessageBox(question, "File in use", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
            return;
        replaceForeign = true;
    }
    install_->SetLabel("Installing...");
    install_->SetBusy(true);
    remove_->Enable(false);
    frame_->RunTask(
        "Installing proxy",
        [gameDir, proxy, replaceForeign] {
            const UrkApp::LogSink log = UrkApp::ActivityLog::Instance().SinkFor("loader");
            std::string error;
            if (!UrkApp::InstallProxy(gameDir, proxy, replaceForeign, log, error))
                log(UrkApp::LogLevel::Error, "Install failed: " + error);
        },
        [this] {
            install_->SetBusy(false);
            RefreshProxy();
        });
}

void LoaderPage::Remove() {
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    if (!GameSelected(exe))
        return;
    const std::filesystem::path gameDir = GameDirectory(exe);
    remove_->SetLabel("Removing...");
    remove_->SetBusy(true);
    install_->Enable(false);
    frame_->RunTask(
        "Removing proxy",
        [gameDir] {
            const UrkApp::LogSink log = UrkApp::ActivityLog::Instance().SinkFor("loader");
            std::string error;
            if (!UrkApp::RemoveProxies(gameDir, log, error))
                log(UrkApp::LogLevel::Error, "Remove failed: " + error);
        },
        [this] {
            remove_->SetBusy(false);
            remove_->SetLabel("Remove");
            RefreshProxy();
        });
}

void LoaderPage::InjectSelected() {
    const long selected = processes_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (selected < 0 || selected >= static_cast<long>(processList_.size()))
        return;
    const UrkApp::ProcessInfo &process = processList_[static_cast<size_t>(selected)];
    InjectPid(process.pid, wxString(process.name));
}

void LoaderPage::InjectPid(std::uint32_t pid, const wxString &name) {
    if (frame_->Busy())
        return;
    injecting_ = true;
    inject_->SetLabel("Injecting...");
    inject_->SetBusy(true);
    injectSelected_->Enable(false);
    injectStatus_->Set(StatusCard::Tone::Busy, "Injecting into " + name, wxEmptyString);
    RefreshInjector();
    const std::string label = name.utf8_string();
    auto error = std::make_shared<std::string>();
    frame_->RunTask(
        "Injecting",
        [pid, label, error] {
            const UrkApp::LogSink log = UrkApp::ActivityLog::Instance().SinkFor("inject");
            log(UrkApp::LogLevel::Info, "Target " + label + " (pid " + std::to_string(pid) + ")");
            if (!UrkApp::InjectLoader(pid, log, *error))
                log(UrkApp::LogLevel::Error, "Injection failed: " + *error);
        },
        [this, error] {
            injecting_ = false;
            inject_->SetBusy(false);
            inject_->SetLabel("Inject");
            RefreshGame();
            RefreshInjector();
            RefreshProcesses();
            if (!error->empty()) {
                injectStatus_->Set(StatusCard::Tone::Error, "Injection failed", FromUtf8(*error));
                inject_->Show(gameProcess_.has_value());
                injectStatus_->Refit();
            }
        });
}

void LoaderPage::OnTimer(wxTimerEvent &) {
    const bool injector = mode_->Selection() == 1;
    const bool visible = IsShownOnScreen();
    if (frame_->Busy() || (!visible && !(injector && auto_->GetValue())))
        return;
    const wxString exe = frame_->settings().gameExe.Strip(wxString::both);
    if (!GameSelected(exe))
        return;
    const std::optional<UrkApp::ProcessInfo> previous = gameProcess_;
    RefreshGame();
    const bool changed = previous.has_value() != gameProcess_.has_value() ||
                         (previous && gameProcess_ && (previous->pid != gameProcess_->pid ||
                                                       previous->urkitLoaded != gameProcess_->urkitLoaded));
    if (injector && auto_->GetValue() && gameProcess_ && !gameProcess_->urkitLoaded &&
        gameProcess_->pid != autoInjectedPid_) {
        if (pendingPid_ != gameProcess_->pid) {
            pendingPid_ = gameProcess_->pid;
            pendingTicks_ = 0;
            MainFrame::Log(UrkApp::LogLevel::Info, "inject",
                           "Game started, pid " + std::to_string(gameProcess_->pid) + "; injecting in " +
                               std::to_string(kAutoInjectDelaySeconds) + " s");
        } else if (++pendingTicks_ >= kAutoInjectDelaySeconds) {
            autoInjectedPid_ = gameProcess_->pid;
            pendingPid_ = 0;
            InjectPid(gameProcess_->pid, wxString(gameProcess_->name));
            return;
        }
        RefreshInjector();
        return;
    }
    if (!gameProcess_)
        pendingPid_ = 0;
    if (changed) {
        RefreshInjector();
        if (visible && injector)
            RefreshProcesses();
    }
}

} // namespace UrkGui
