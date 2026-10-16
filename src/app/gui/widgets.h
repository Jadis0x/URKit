#pragma once

#include "activity_log.h"

#include <wx/control.h>
#include <wx/listctrl.h>
#include <wx/panel.h>
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

class wxGraphicsContext;

namespace UrkGui {

double NowMs();
// Focus rings show after keyboard navigation and hide again on the next click.
void SetFocusRingVisible(bool visible);
bool ShowsFocusRing(const wxWindow *window);
// Graphics context over a buffered paint DC.
std::unique_ptr<wxGraphicsContext> MakeGc(wxDC &dc);

// Eased value that moves towards a target over a fixed time; read it while painting.
class Tween {
  public:
    explicit Tween(double value = 0.0) : from_(value), to_(value) {}
    void Set(double target, double durationMs);
    void Jump(double value);
    double Value() const;
    double Target() const { return to_; }
    bool Running() const;

  private:
    double from_;
    double to_;
    double start_ = 0.0;
    double duration_ = 0.0;
};

// Repaints its owner every frame while step() returns true.
class Ticker : public wxTimer {
  public:
    Ticker(wxWindow *owner, std::function<bool()> step) : owner_(owner), step_(std::move(step)) {}
    void Kick();
    void Notify() override;

  private:
    wxWindow *owner_;
    std::function<bool()> step_;
};

// Lays out the window's ancestors up to the enclosing page after its size changed.
void RelayoutAncestors(wxWindow *window);
std::vector<wxString> WrapLines(wxDC &dc, const wxString &text, int width);
void DrawSpinner(wxGraphicsContext &gc, double cx, double cy, double radius, double stroke, const wxColour &colour);

class Button : public wxControl {
  public:
    enum class Kind {
        Primary,
        Secondary,
        Quiet,
    };

    Button(wxWindow *parent, const wxString &label, Kind kind = Kind::Secondary, wchar_t icon = 0);

    void SetBusy(bool busy);
    bool IsBusy() const { return busy_; }
    bool Enable(bool enable = true) override;
    bool AcceptsFocus() const override { return IsEnabled() && !busy_; }
    bool AcceptsFocusFromKeyboard() const override { return AcceptsFocus(); }
    void SetLabel(const wxString &label) override;

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    void OnPaint(wxPaintEvent &event);
    void OnMouse(wxMouseEvent &event);
    void OnKey(wxKeyEvent &event);
    void Click();

    Kind kind_;
    wchar_t icon_;
    Tween hover_;
    Tween press_;
    Ticker ticker_;
    bool pressed_ = false;
    bool busy_ = false;
};

// Rounded filled panel.
class Surface : public wxPanel {
  public:
    explicit Surface(wxWindow *parent, int padding = 16);
    wxBoxSizer *Body() const { return body_; }

  private:
    wxBoxSizer *body_ = nullptr;
};

// Pill row with a sliding selection; used for the page navigation and for small option sets.
class Segmented : public wxControl {
  public:
    enum class Style {
        Nav,
        Field,
    };

    Segmented(wxWindow *parent, std::vector<wxString> items, Style style, std::function<void(int)> onSelect);
    void Select(int index, bool animate = true);
    int Selection() const { return selected_; }
    bool Enable(bool enable = true) override;
    bool AcceptsFocus() const override { return IsEnabled(); }
    bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    std::vector<wxRect> ItemRects() const;
    void OnPaint(wxPaintEvent &event);
    void OnMouse(wxMouseEvent &event);
    void Choose(int index);

    std::vector<wxString> items_;
    Style style_;
    std::function<void(int)> onSelect_;
    int selected_ = 0;
    int hover_ = -1;
    Tween x_;
    Tween width_;
    std::vector<Tween> hoverT_;
    Ticker ticker_;
};

// Large selectable cards: icon, title and one sentence each.
class ChoiceTiles : public wxControl {
  public:
    struct Item {
        wchar_t icon;
        wxString title;
        wxString text;
    };

    ChoiceTiles(wxWindow *parent, std::vector<Item> items, std::function<void(int)> onSelect);
    void Select(int index, bool animate = true);
    int Selection() const { return selected_; }
    bool AcceptsFocus() const override { return true; }

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    int TileWidth(int width) const;
    int HeightFor(int width) const;
    wxRect TileRect(int index) const;
    void OnPaint(wxPaintEvent &event);
    void OnMouse(wxMouseEvent &event);

    std::vector<Item> items_;
    std::function<void(int)> onSelect_;
    int selected_ = 0;
    int hover_ = -1;
    int height_ = 0;
    std::vector<Tween> selectT_;
    std::vector<Tween> hoverT_;
    Ticker ticker_;
};

class Toggle : public wxControl {
  public:
    Toggle(wxWindow *parent, const wxString &label, std::function<void(bool)> onChange = {});
    bool GetValue() const { return value_; }
    void SetValue(bool value, bool animate = false);
    bool Enable(bool enable = true) override;
    bool AcceptsFocus() const override { return IsEnabled(); }
    bool AcceptsFocusFromKeyboard() const override { return IsEnabled(); }

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    void OnPaint(wxPaintEvent &event);
    void Flip();

    std::function<void(bool)> onChange_;
    bool value_ = false;
    bool hovered_ = false;
    Tween knob_;
    Ticker ticker_;
};

// Single-line text input in a rounded box that lights up on focus.
class TextField : public wxPanel {
  public:
    TextField(wxWindow *parent, const wxString &value, const wxString &hint = {});
    wxTextCtrl *Ctrl() const { return ctrl_; }

  private:
    void OnPaint(wxPaintEvent &event);

    wxTextCtrl *ctrl_ = nullptr;
    Tween focus_;
    Ticker ticker_;
};

// Drop target and picker for the game executable or a project folder.
class DropZone : public wxControl {
  public:
    enum class Mode {
        File,
        Folder,
    };

    DropZone(wxWindow *parent, Mode mode, const wxString &emptyTitle, std::function<void(const wxString &)> onPick);
    void SetEmpty();
    void SetFilled(const wxString &path, const wxString &title, const wxString &chip, const wxColour &chipColour);
    void SetDragging(bool dragging);
    void Browse();
    bool AcceptsFocus() const override { return true; }

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    void OnPaint(wxPaintEvent &event);

    Mode mode_;
    wxString emptyTitle_;
    std::function<void(const wxString &)> onPick_;
    bool filled_ = false;
    wxString path_;
    wxString title_;
    wxString chip_;
    wxColour chipColour_;
    bool hovered_ = false;
    Tween hover_;
    Tween drag_;
    Tween fresh_{1.0};
    Ticker ticker_;
};

// One-glance state: icon, title, a sentence and optional buttons on the right.
class StatusCard : public wxPanel {
  public:
    enum class Tone {
        Neutral,
        Busy,
        Waiting,
        Info,
        Success,
        Warning,
        Error,
    };

    explicit StatusCard(wxWindow *parent);
    void Set(Tone tone, const wxString &title, const wxString &text = {}, wchar_t icon = 0);
    Tone GetTone() const { return tone_; }
    wxBoxSizer *Actions() const { return actions_; }
    // Call after adding, removing or relabelling buttons.
    void Refit();

  private:
    void OnPaint(wxPaintEvent &event);
    int TextLeft() const;
    int TextRight() const;

    Tone tone_ = Tone::Neutral;
    wxString title_;
    wxString text_;
    wchar_t icon_ = 0;
    wxBoxSizer *actions_ = nullptr;
    wxSizerItem *textItem_ = nullptr;
    int textHeight_ = 0;
    Tween fresh_{1.0};
    Ticker ticker_;
};

// Header that expands a panel below it; without a title it is driven from code.
class Collapsible : public wxPanel {
  public:
    Collapsible(wxWindow *parent, const wxString &title);
    wxPanel *Content() const { return content_; }
    void SetExpanded(bool expanded, bool animate = true);
    bool IsExpanded() const { return expanded_; }
    void SyncHeight();

  private:
    int ContentHeight() const;

    class Header;
    Header *header_ = nullptr;
    wxPanel *clip_ = nullptr;
    wxPanel *content_ = nullptr;
    bool expanded_ = false;
    Tween progress_;
    wxTimer timer_;
};

class LinkRow : public wxControl {
  public:
    LinkRow(wxWindow *parent, wchar_t icon, const wxString &label, const wxString &display, const wxString &url);
    bool AcceptsFocus() const override { return true; }

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    void OnPaint(wxPaintEvent &event);

    wchar_t icon_;
    wxString display_;
    wxString url_;
    bool hovered_ = false;
    Tween hover_;
    Ticker ticker_;
};

// Bottom strip: latest activity on the left, log toggle on the right.
class StatusStrip : public wxControl {
  public:
    StatusStrip(wxWindow *parent, std::function<void()> onToggle);
    void SetMessage(UrkApp::LogLevel level, const wxString &text);
    void SetBusy(const wxString &text);
    void SetOpen(bool open);

  protected:
    wxSize DoGetBestClientSize() const override;

  private:
    void OnPaint(wxPaintEvent &event);

    std::function<void()> onToggle_;
    UrkApp::LogLevel level_ = UrkApp::LogLevel::Info;
    wxString message_;
    wxString busyText_;
    bool busy_ = false;
    bool hovered_ = false;
    Tween open_;
    Tween fresh_{1.0};
    Ticker ticker_;
};

// Virtual list of activity entries.
class LogView : public wxListCtrl {
  public:
    explicit LogView(wxWindow *parent);
    void Append(const UrkApp::LogEntry &entry);
    void Clear();
    wxString Text(bool selectedOnly) const;

  protected:
    wxString OnGetItemText(long item, long column) const override;
    wxItemAttr *OnGetItemAttr(long item) const override;

  private:
    void FitMessageColumn();
    void OnKey(wxKeyEvent &event);
    void OnContextMenu(wxContextMenuEvent &event);

    std::deque<UrkApp::LogEntry> entries_;
    mutable wxItemAttr attributes_[5];
};

// Static text that re-wraps to whatever width its sizer gives it.
class WrapText : public wxStaticText {
  public:
    WrapText(wxWindow *parent, const wxString &text, bool muted = true, double points = 9.0);
    void SetText(const wxString &text);

  private:
    void Rewrap(int width);

    wxString raw_;
    int wrappedWidth_ = -1;
};

wxStaticText *MakeLabel(wxWindow *parent, const wxString &text, bool muted = false, double points = 9.5,
                        wxFontWeight weight = wxFONTWEIGHT_NORMAL);
// Small heading above a group of controls.
wxStaticText *MakeSectionLabel(wxWindow *parent, const wxString &text);

} // namespace UrkGui
