#include "page_common.h"

#include "mod_sdk.h"
#include "project_version.h"
#include "self_update.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>

namespace UrkGui {

struct ReleaseState {
    UrkApp::UpdateCheckResult check;
    UrkApp::AvailableUpdate available;
    std::string error;
    bool ok = false;
    bool installed = false;
};

AboutPage::AboutPage(wxWindow *parent, MainFrame *frame)
    : Page(parent, frame, "About"), state_(std::make_shared<ReleaseState>()) {
    wxBoxSizer *column = Column();

    auto *hero = new Surface(this, 20);
    {
        auto *row = new wxBoxSizer(wxHORIZONTAL);
        auto *text = new wxBoxSizer(wxVERTICAL);
        auto *name = new wxStaticText(hero, wxID_ANY, "URKit");
        name->SetFont(Theme::DisplayFont(16));
        name->SetForegroundColour(Theme::kText);
        text->Add(name);
        text->Add(MakeLabel(hero, "Version " + FromUtf8(std::string(UrkVersion::kReleaseVersion)), true, 10), 0,
                  wxTOP, FromDIP(1));
        auto *apis = MakeLabel(hero,
                               wxString::Format(L"SDK %d  ·  Runtime %d  ·  Mono %d  ·  IL2CPP %d  "
                                                L"·  Unreal %d  ·  Network %d",
                                                URK_SDK_VERSION, URK_RUNTIME_API_VERSION, URK_MONO_API_VERSION,
                                                URK_IL2CPP_API_VERSION, URK_UNREAL_API_VERSION,
                                                URK_NETWORK_API_VERSION),
                               false, 8.5);
        apis->SetForegroundColour(Theme::kTextFaint);
        text->Add(apis, 0, wxTOP, FromDIP(6));
        row->Add(text, 1, wxALIGN_CENTER_VERTICAL);
        hero->Body()->Add(row, 0, wxEXPAND);
    }
    column->Add(hero, 0, wxEXPAND);

    release_ = new StatusCard(this);
    check_ = new Button(release_, "Check", Button::Kind::Secondary, Theme::Icon::kRefresh);
    install_ = new Button(release_, "Install", Button::Kind::Primary, Theme::Icon::kDownload);
    release_->Actions()->Add(check_, 0, wxRIGHT, FromDIP(8));
    release_->Actions()->Add(install_);
    install_->Hide();
    release_->Set(StatusCard::Tone::Neutral, "Updates", "Check GitHub for a newer version.", Theme::Icon::kUp);
    column->Add(release_, 0, wxEXPAND | wxTOP, FromDIP(14));

    column->AddSpacer(FromDIP(24));
    column->Add(MakeSectionLabel(this, "Links"), 0, wxBOTTOM, FromDIP(8));
    auto *links = new Surface(this, 6);
    struct Link {
        wchar_t icon;
        const char *label;
        const char *display;
        const char *url;
    };
    const Link items[] = {
        {Theme::Icon::kCode, "Source code", "github.com/Jadis0x/URKit", "https://github.com/Jadis0x/URKit"},
        {Theme::Icon::kBug, "Report a problem", "github.com/Jadis0x/URKit/issues",
         "https://github.com/Jadis0x/URKit/issues"},
        {Theme::Icon::kPerson, "Author", "github.com/Jadis0x", "https://github.com/Jadis0x"},
        {Theme::Icon::kHeart, "Support the project", "buymeacoffee.com/Jadis0x",
         "https://www.buymeacoffee.com/Jadis0x"},
    };
    for (const Link &item : items)
        links->Body()->Add(new LinkRow(links, item.icon, item.label, item.display, item.url), 0, wxEXPAND);
    column->Add(links, 0, wxEXPAND);

    check_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { CheckRelease(); });
    install_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { InstallRelease(); });
}

void AboutPage::CheckRelease() {
    check_->SetLabel("Checking...");
    check_->SetBusy(true);
    install_->Hide();
    release_->Set(StatusCard::Tone::Busy, "Checking for updates...", wxEmptyString);
    auto state = std::make_shared<ReleaseState>();
    frame_->RunTask(
        "Checking for updates",
        [state] {
            const UrkApp::LogSink log = UrkApp::ActivityLog::Instance().SinkFor("release");
            log(UrkApp::LogLevel::Debug, "GET https://api.github.com/repos/Jadis0x/URKit/releases/latest");
            state->ok = UrkApp::CheckForUpdate(&state->check, &state->available, &state->error);
            if (!state->ok)
                log(UrkApp::LogLevel::Error, "Update check failed: " + state->error);
            else if (state->check.updateAvailable)
                log(UrkApp::LogLevel::Info, "Version " + state->check.latestVersion + " is available");
            else
                log(UrkApp::LogLevel::Success, "Up to date (" + state->check.latestVersion + ")");
        },
        [this, state] {
            state_ = state;
            check_->SetBusy(false);
            check_->SetLabel("Check");
            if (!state->ok) {
                release_->Set(StatusCard::Tone::Error, "Couldn't check for updates", FromUtf8(state->error));
            } else if (state->check.updateAvailable) {
                release_->Set(StatusCard::Tone::Info, "Version " + FromUtf8(state->check.latestVersion) + " is out",
                              "You have " + FromUtf8(state->check.currentVersion) + ". Installing restarts URKit.",
                              Theme::Icon::kUp);
                install_->SetLabel("Install " + FromUtf8(state->check.latestVersion));
                install_->Show();
            } else {
                release_->Set(StatusCard::Tone::Success, "You're up to date",
                              FromUtf8(state->check.latestVersion) + " is the latest version.");
            }
            release_->Refit();
        });
}

void AboutPage::InstallRelease() {
    if (!state_ || !state_->ok || !state_->check.updateAvailable)
        return;
    install_->SetLabel("Downloading...");
    install_->SetBusy(true);
    check_->Enable(false);
    release_->Set(StatusCard::Tone::Busy, "Downloading " + FromUtf8(state_->available.availableVersion) + "...",
                  "URKit restarts when it is done.");
    auto state = state_;
    frame_->RunTask(
        "Downloading update",
        [state] {
            const UrkApp::LogSink log = UrkApp::ActivityLog::Instance().SinkFor("release");
            state->installed = UrkApp::DownloadAndRestart(state->available, log, true, &state->error);
            if (!state->installed)
                log(UrkApp::LogLevel::Error, "Update failed: " + state->error);
        },
        [this, state] {
            install_->SetBusy(false);
            check_->Enable(true);
            if (state->installed) {
                frame_->Close(true);
                return;
            }
            install_->SetLabel("Try again");
            release_->Set(StatusCard::Tone::Error, "Update failed", FromUtf8(state->error));
            release_->Refit();
        });
}

} // namespace UrkGui
