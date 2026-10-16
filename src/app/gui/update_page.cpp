#include "page_common.h"

#include "mod_sdk.h"

#include <wx/listctrl.h>

namespace UrkGui {
namespace {

struct CheckResult {
    bool ok = false;
    std::string error;
    std::shared_ptr<UrkProject::UpdatePreview> preview;
};

struct ApplyResult {
    bool ok = false;
    std::string error;
    UrkProject::UpdateResult result;
};

UrkApp::LogSink UpdateLog() {
    return UrkApp::ActivityLog::Instance().SinkFor("update");
}

UrkProject::ProgressSink UpdateProgress() {
    return [log = UpdateLog()](const std::string &line) { log(UrkApp::LogLevel::Info, line); };
}

int ProjectSdk(const UrkProject::Inspection &inspection) {
    return inspection.detectedSdkVersion > 0 ? inspection.detectedSdkVersion : inspection.manifest.sdkVersion;
}

wxString ProjectTitle(const UrkProject::Inspection &inspection, const wxString &folder) {
    if (!inspection.manifest.projectName.empty())
        return FromUtf8(inspection.manifest.projectName);
    return FromPath(ToPath(folder).filename());
}

const char *ChangeLabel(UrkProject::ChangeKind kind) {
    switch (kind) {
        case UrkProject::ChangeKind::Added:
            return "Add";
        case UrkProject::ChangeKind::Modified:
            return "Replace";
        case UrkProject::ChangeKind::Removed:
            return "Remove";
        case UrkProject::ChangeKind::Conflict:
            return "Differs";
        case UrkProject::ChangeKind::Review:
            break;
    }
    return "Kept, check";
}

wxColour ChangeColour(UrkProject::ChangeKind kind) {
    switch (kind) {
        case UrkProject::ChangeKind::Added:
            return Theme::kSuccess;
        case UrkProject::ChangeKind::Modified:
            return Theme::kText;
        case UrkProject::ChangeKind::Removed:
            return Theme::kTextMuted;
        case UrkProject::ChangeKind::Conflict:
            return Theme::kWarning;
        case UrkProject::ChangeKind::Review:
            break;
    }
    return Theme::kTextMuted;
}

int ChangeRank(UrkProject::ChangeKind kind) {
    switch (kind) {
        case UrkProject::ChangeKind::Conflict:
            return 0;
        case UrkProject::ChangeKind::Modified:
            return 1;
        case UrkProject::ChangeKind::Added:
            return 2;
        case UrkProject::ChangeKind::Removed:
            return 3;
        case UrkProject::ChangeKind::Review:
            break;
    }
    return 4;
}

} // namespace

UpdatePage::UpdatePage(wxWindow *parent, MainFrame *frame) : Page(parent, frame, "Update a project") {
    wxBoxSizer *column = Column();

    project_ = new DropZone(this, DropZone::Mode::Folder, "Drop your project folder here",
                            [this](const wxString &path) {
                                if (wxDirExists(path))
                                    frame_->SetProjectDir(path);
                                else
                                    MainFrame::Log(UrkApp::LogLevel::Warning, "update", "Drop the project folder");
                            });
    column->Add(project_, 0, wxEXPAND);

    status_ = new StatusCard(this);
    openResult_ = new Button(status_, "Open folder", Button::Kind::Secondary, Theme::Icon::kFolder);
    apply_ = new Button(status_, "Update", Button::Kind::Primary, Theme::Icon::kDownload);
    status_->Actions()->Add(openResult_, 0, wxRIGHT, FromDIP(8));
    status_->Actions()->Add(apply_);
    openResult_->Hide();
    apply_->Hide();
    column->Add(status_, 0, wxEXPAND | wxTOP, FromDIP(14));

    column->AddSpacer(FromDIP(14));
    details_ = new Collapsible(this, "Details");
    {
        wxPanel *content = details_->Content();
        auto *sizer = new wxBoxSizer(wxVERTICAL);
        changes_ = MakeList(content, 170);
        changes_->AppendColumn("Change", wxLIST_FORMAT_LEFT, FromDIP(110));
        changes_->AppendColumn("File", wxLIST_FORMAT_LEFT, FromDIP(480));
        sizer->Add(changes_, 0, wxEXPAND | wxTOP, FromDIP(6));
        overwrite_ = new Toggle(content, "Replace those files too (a backup is made first)", [this](bool) {
            if (preview_)
                ShowPlan();
        });
        sizer->Add(overwrite_, 0, wxTOP, FromDIP(10));
        auto *tools = new wxBoxSizer(wxHORIZONTAL);
        auto *backups = new Button(content, "Open backups", Button::Kind::Quiet, Theme::Icon::kFolder);
        auto *stage = new Button(content, "Save new SDK only", Button::Kind::Quiet, Theme::Icon::kDownload);
        stage->SetToolTip("Writes the new SDK files to .urk\\updates and leaves the project as it is.");
        tools->Add(backups, 0, wxRIGHT, FromDIP(4));
        tools->Add(stage);
        sizer->Add(tools, 0, wxTOP | wxBOTTOM, FromDIP(8));
        content->SetSizer(sizer);

        changes_->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
            event.Skip();
            CallAfter([this] {
                changes_->SetColumnWidth(
                    1, std::max(FromDIP(200), changes_->GetClientSize().x - changes_->GetColumnWidth(0) - FromDIP(4)));
            });
        });
        backups->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
            const std::filesystem::path backups =
                ToPath(frame_->settings().projectDir.Strip(wxString::both)) / ".urk" / "backups";
            std::error_code ec;
            if (std::filesystem::is_directory(backups, ec))
                OpenFolder(backups);
            else
                MainFrame::Log(UrkApp::LogLevel::Info, "update", "No backups yet");
        });
        stage->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { StageSdk(); });
    }
    column->Add(details_, 0, wxEXPAND);

    apply_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { Apply(); });
    openResult_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { OpenFolder(resultFolder_); });
    OnProjectChanged();
}

void UpdatePage::OnProjectChanged() {
    ResetPlan();
    const wxString folder = frame_->settings().projectDir.Strip(wxString::both);
    if (folder.empty()) {
        project_->SetEmpty();
        status_->Set(StatusCard::Tone::Neutral, "No project selected",
                     "Pick the folder URKit created for your mod.");
        return;
    }
    project_->SetFilled(folder, FromPath(ToPath(folder).filename()), wxEmptyString, Theme::kTextMuted);
    status_->Set(StatusCard::Tone::Neutral, "Ready to check", "The project is checked when you open this page.");
    if (IsShownOnScreen())
        Check();
}

void UpdatePage::OnShown() {
    const wxString folder = frame_->settings().projectDir.Strip(wxString::both);
    if (!folder.empty() && folder != checkedPath_ && !frame_->Busy())
        Check();
}

void UpdatePage::ResetPlan() {
    preview_.reset();
    details_->SetExpanded(false, false);
    ShowWindow(details_, false);
    checkedPath_.clear();
    changes_->DeleteAllItems();
    overwrite_->SetValue(false);
    overwrite_->Hide();
    apply_->Hide();
    openResult_->Hide();
    status_->Refit();
}

void UpdatePage::SetAction(const wxString &label, bool show) {
    apply_->SetLabel(label);
    apply_->Show(show);
    apply_->Enable(show && !frame_->Busy());
    status_->Refit();
}

void UpdatePage::ShowPlan() {
    if (!preview_)
        return;
    const UrkProject::Inspection &inspection = preview_->inspection;
    const auto count = [this](UrkProject::ChangeKind kind) {
        return static_cast<int>(UrkProject::CountChanges(preview_->changes, kind));
    };
    const int conflicts = count(UrkProject::ChangeKind::Conflict);
    const int writes = count(UrkProject::ChangeKind::Modified) + count(UrkProject::ChangeKind::Added) +
                       count(UrkProject::ChangeKind::Removed);
    const int sdk = ProjectSdk(inspection);
    openResult_->Hide();
    if (!inspection.updateAvailable) {
        status_->Set(StatusCard::Tone::Success, "Up to date",
                     wxString::Format("Made for SDK %d, the current version.", URK_SDK_VERSION));
        SetAction("Update", false);
        return;
    }
    wxString versions = sdk > 0 && sdk != URK_SDK_VERSION ? wxString::Format(L"SDK %d → %d. ", sdk, URK_SDK_VERSION)
                                                          : wxString();
    if (conflicts > 0 && !overwrite_->GetValue()) {
        if (!inspection.hasGeneratedFileLedger)
            status_->Set(StatusCard::Tone::Warning, "This project is from an older version",
                         wxString::Format("%d file%s can't be updated safely, because URKit can't tell if you changed "
                                          "%s. Nothing is replaced; the new version is saved next to the project.",
                                          conflicts, conflicts == 1 ? "" : "s", conflicts == 1 ? "it" : "them"));
        else
            status_->Set(StatusCard::Tone::Warning,
                         wxString::Format("You edited %d generated file%s", conflicts, conflicts == 1 ? "" : "s"),
                         "Your edits are kept. The new version is saved next to the project so you can merge it.");
        SetAction("Save update", true);
        return;
    }
    const int files = writes + (overwrite_->GetValue() ? conflicts : 0);
    wxString text = versions + wxString::Format("%d file%s will change", files, files == 1 ? "" : "s");
    if (conflicts > 0)
        text << wxString::Format(", including %d that differ", conflicts);
    text << ". A backup is made first.";
    status_->Set(StatusCard::Tone::Info, "Update available", text, Theme::Icon::kUp);
    SetAction("Update", true);
}

void UpdatePage::ShowChanges() {
    changes_->DeleteAllItems();
    if (!preview_)
        return;
    std::vector<const UrkProject::PlannedChange *> ordered;
    for (const UrkProject::PlannedChange &change : preview_->changes)
        ordered.push_back(&change);
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto *left, const auto *right) {
        return ChangeRank(left->kind) < ChangeRank(right->kind);
    });
    long row = 0;
    for (const UrkProject::PlannedChange *change : ordered) {
        const long index = changes_->InsertItem(row++, ChangeLabel(change->kind));
        changes_->SetItem(index, 1, FromUtf8(change->relativePath.generic_string()));
        changes_->SetItemTextColour(index, ChangeColour(change->kind));
        changes_->SetItemBackgroundColour(index, Theme::kInput);
    }
    if (preview_->changes.empty())
        changes_->InsertItem(0, "No file changes");
    // The inspection notes are technical; they are in the activity log.
    const int rows = std::clamp(changes_->GetItemCount(), 2, 8);
    changes_->SetMinSize(wxSize(-1, FromDIP(30 + 21 * rows)));
    changes_->InvalidateBestSize();
    const bool conflicts = UrkProject::CountChanges(preview_->changes, UrkProject::ChangeKind::Conflict) > 0;
    overwrite_->Show(conflicts);
    ShowWindow(details_, true);
    RelayoutAncestors(details_->Content());
}

void UpdatePage::Check() {
    const wxString folder = frame_->settings().projectDir.Strip(wxString::both);
    if (folder.empty() || frame_->Busy())
        return;
    ResetPlan();
    checkedPath_ = folder;
    status_->Set(StatusCard::Tone::Busy, "Checking project...", "Building the new version to compare.");
    auto result = std::make_shared<CheckResult>();
    const std::filesystem::path root = ToPath(folder);
    frame_->RunTask(
        "Checking project",
        [root, result] {
            const UrkApp::LogSink log = UpdateLog();
            bool recovered = false;
            std::filesystem::path backup;
            if (!UrkProject::RecoverInterruptedUpdate(root, &recovered, &backup, &result->error)) {
                log(UrkApp::LogLevel::Error, result->error);
                return;
            }
            if (recovered)
                log(UrkApp::LogLevel::Warning, "Restored an interrupted update from " + backup.string());
            auto preview = std::make_shared<UrkProject::UpdatePreview>();
            if (!UrkProject::PreviewUpdate(root, preview.get(), &result->error, UpdateProgress())) {
                log(UrkApp::LogLevel::Error, "Check failed: " + result->error);
                return;
            }
            log(preview->inspection.updateAvailable ? UrkApp::LogLevel::Info : UrkApp::LogLevel::Success,
                preview->inspection.updateAvailable ? "Update available" : "Project is up to date");
            result->preview = std::move(preview);
            result->ok = true;
        },
        [this, result, folder] {
            if (folder != frame_->settings().projectDir.Strip(wxString::both))
                return;
            if (!result->ok) {
                status_->Set(StatusCard::Tone::Error, "Couldn't read this project", FromUtf8(result->error));
                return;
            }
            preview_ = result->preview;
            const UrkProject::Inspection &inspection = preview_->inspection;
            const int sdk = ProjectSdk(inspection);
            project_->SetFilled(folder, ProjectTitle(inspection, folder),
                                sdk > 0 ? wxString::Format("SDK %d", sdk) : wxString(),
                                sdk == URK_SDK_VERSION ? Theme::kSuccess : Theme::kAccentHover);
            ShowChanges();
            ShowPlan();
        });
}

void UpdatePage::Apply() {
    if (!preview_)
        return;
    const bool overwrite = overwrite_->GetValue();
    const int conflicts =
        static_cast<int>(UrkProject::CountChanges(preview_->changes, UrkProject::ChangeKind::Conflict));
    if (overwrite && conflicts > 0) {
        const wxString question = wxString::Format(
            "Replace %d file%s you edited?\n\nA backup is made first.", conflicts, conflicts == 1 ? "" : "s");
        if (wxMessageBox(question, "Update", wxYES_NO | wxNO_DEFAULT | wxICON_WARNING, this) != wxYES)
            return;
    }
    apply_->SetLabel("Updating...");
    apply_->SetBusy(true);
    status_->Set(StatusCard::Tone::Busy, "Updating...", "Backing up and writing files.");
    auto preview = preview_;
    auto result = std::make_shared<ApplyResult>();
    const wxString folder = checkedPath_;
    frame_->RunTask(
        "Updating project",
        [preview, overwrite, result] {
            const UrkApp::LogSink log = UpdateLog();
            UrkProject::ApplyOptions options;
            options.overwriteConflicts = overwrite;
            if (!UrkProject::ApplyUpdate(*preview, options, &result->result, &result->error, UpdateProgress())) {
                log(UrkApp::LogLevel::Error, "Update failed: " + result->error);
                return;
            }
            result->ok = true;
            const UrkProject::UpdateResult &update = result->result;
            if (update.staged)
                log(UrkApp::LogLevel::Warning,
                    "Project unchanged; the update is in " + update.stagedUpdateDirectory.string());
            else if (update.updated)
                log(UrkApp::LogLevel::Success, std::to_string(update.filesWritten) + " written, " +
                                                   std::to_string(update.filesRemoved) + " removed. Backup: " +
                                                   update.backupDirectory.string());
            else
                log(UrkApp::LogLevel::Success, "Nothing to apply");
        },
        [this, result, folder] {
            apply_->SetBusy(false);
            preview_.reset();
            changes_->DeleteAllItems();
            ShowWindow(details_, false);
            overwrite_->SetValue(false);
            overwrite_->Hide();
            if (!result->ok) {
                status_->Set(StatusCard::Tone::Error, "Update failed", FromUtf8(result->error));
                SetAction("Update", false);
                checkedPath_.clear();
                return;
            }
            const UrkProject::UpdateResult &update = result->result;
            const int sdk = ProjectSdk(update.inspection);
            project_->SetFilled(folder, ProjectTitle(update.inspection, folder),
                                sdk > 0 ? wxString::Format("SDK %d", sdk) : wxString(),
                                sdk == URK_SDK_VERSION ? Theme::kSuccess : Theme::kAccentHover);
            SetAction("Update", false);
            if (update.staged) {
                resultFolder_ = update.stagedUpdateDirectory;
                status_->Set(StatusCard::Tone::Warning, "Update saved separately",
                             "Merge the files listed in CONFLICTS.txt, then build.");
            } else if (update.updated) {
                resultFolder_ = update.backupDirectory;
                status_->Set(StatusCard::Tone::Success, wxString::Format("Updated to SDK %d", URK_SDK_VERSION),
                             "Rebuild the project. The old files are in the backup.");
                openResult_->SetLabel("Open backup");
            } else {
                resultFolder_.clear();
                status_->Set(StatusCard::Tone::Success, "Up to date", "Nothing to change.");
            }
            if (!resultFolder_.empty()) {
                if (update.staged)
                    openResult_->SetLabel("Open folder");
                openResult_->Show();
            }
            status_->Refit();
        });
}

void UpdatePage::StageSdk() {
    const wxString folder = frame_->settings().projectDir.Strip(wxString::both);
    if (folder.empty())
        return;
    const std::filesystem::path root = ToPath(folder);
    auto staged = std::make_shared<std::filesystem::path>();
    auto error = std::make_shared<std::string>();
    status_->Set(StatusCard::Tone::Busy, "Saving the new SDK...", wxEmptyString);
    frame_->RunTask(
        "Saving SDK",
        [root, staged, error] {
            const UrkApp::LogSink log = UpdateLog();
            if (!UrkProject::StageSdkMigration(root, staged.get(), error.get())) {
                log(UrkApp::LogLevel::Error, "Saving the SDK failed: " + *error);
                return;
            }
            log(UrkApp::LogLevel::Success, "Project unchanged. New SDK in " + staged->string());
        },
        [this, staged, error] {
            apply_->Hide();
            if (staged->empty()) {
                status_->Set(StatusCard::Tone::Error, "Couldn't save the SDK", FromUtf8(*error));
                openResult_->Hide();
            } else {
                resultFolder_ = *staged;
                status_->Set(StatusCard::Tone::Success, "New SDK saved", "The project was not changed.");
                openResult_->SetLabel("Open folder");
                openResult_->Show();
            }
            checkedPath_.clear();
            status_->Refit();
        });
}

} // namespace UrkGui
