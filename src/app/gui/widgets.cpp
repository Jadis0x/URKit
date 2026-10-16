#include "widgets.h"

#include "theme.h"

#include <wx/clipbrd.h>
#include <wx/dcbuffer.h>
#include <wx/dirdlg.h>
#include <wx/dnd.h>
#include <wx/filedlg.h>
#include <wx/filename.h>
#include <wx/graphics.h>
#include <wx/menu.h>
#include <wx/scrolwin.h>
#include <wx/utils.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace UrkGui {
namespace {

constexpr size_t kMaxLogEntries = 5000;
constexpr double kPi = 3.14159265358979323846;
constexpr double kHoverMs = 140;
constexpr double kMoveMs = 260;

using Theme::Mix;

void FillRounded(wxGraphicsContext &gc, double x, double y, double w, double h, double radius, const wxColour &fill,
                 const wxColour &border = wxNullColour, double borderWidth = 1.0) {
    gc.SetBrush(fill.IsOk() ? wxBrush(fill) : *wxTRANSPARENT_BRUSH);
    if (border.IsOk())
        gc.SetPen(gc.CreatePen(wxGraphicsPenInfo(border).Width(borderWidth)));
    else
        gc.SetPen(*wxTRANSPARENT_PEN);
    const double inset = border.IsOk() ? borderWidth / 2.0 : 0.0;
    gc.DrawRoundedRectangle(x + inset, y + inset, w - 2 * inset, h - 2 * inset, radius);
}

void ClearToParent(wxDC &dc, wxWindow *window) {
    dc.SetBackground(wxBrush(window->GetParent()->GetBackgroundColour()));
    dc.Clear();
}

void DrawGlyph(wxDC &dc, wchar_t glyph, const wxRect &box, const wxColour &colour, double points) {
    dc.SetFont(Theme::IconFont(points));
    dc.SetTextForeground(colour);
    const wxString text(glyph);
    const wxSize extent = dc.GetTextExtent(text);
    dc.DrawText(text, box.x + (box.width - extent.x) / 2, box.y + (box.height - extent.y) / 2);
}

void DrawTextAt(wxDC &dc, const wxString &text, int x, int y, const wxColour &colour, const wxFont &font) {
    dc.SetFont(font);
    dc.SetTextForeground(colour);
    dc.DrawText(text, x, y);
}

int TextHeight(wxWindow *window, const wxFont &font) {
    wxClientDC dc(window);
    dc.SetFont(font);
    return dc.GetTextExtent("Ag").y;
}

void BindHover(wxWindow *window, bool &hovered, Tween &tween, Ticker &ticker) {
    window->Bind(wxEVT_ENTER_WINDOW, [&hovered, &tween, &ticker](wxMouseEvent &event) {
        hovered = true;
        tween.Set(1.0, kHoverMs);
        ticker.Kick();
        event.Skip();
    });
    window->Bind(wxEVT_LEAVE_WINDOW, [&hovered, &tween, &ticker](wxMouseEvent &event) {
        hovered = false;
        tween.Set(0.0, kHoverMs * 1.6);
        ticker.Kick();
        event.Skip();
    });
}

void RefreshOnFocus(wxWindow *window) {
    window->Bind(wxEVT_SET_FOCUS, [window](wxFocusEvent &event) {
        window->Refresh(false);
        event.Skip();
    });
    window->Bind(wxEVT_KILL_FOCUS, [window](wxFocusEvent &event) {
        window->Refresh(false);
        event.Skip();
    });
}

bool IsActivateKey(const wxKeyEvent &event) {
    const int key = event.GetKeyCode();
    return key == WXK_SPACE || key == WXK_RETURN || key == WXK_NUMPAD_ENTER;
}

void HandleTab(wxWindow *window, wxKeyEvent &event) {
    if (event.GetKeyCode() == WXK_TAB) {
        window->Navigate(event.ShiftDown() ? wxNavigationKeyEvent::IsBackward : wxNavigationKeyEvent::IsForward);
        return;
    }
    event.Skip();
}

bool g_focusRing = false;

// Fractional device-independent pixels, for strokes and radii.
double Dip(const wxWindow *window, double value) {
    return value * window->GetDPIScaleFactor();
}

} // namespace

void SetFocusRingVisible(bool visible) {
    g_focusRing = visible;
}

bool ShowsFocusRing(const wxWindow *window) {
    return g_focusRing && window->HasFocus();
}

std::unique_ptr<wxGraphicsContext> MakeGc(wxDC &dc) {
    std::unique_ptr<wxGraphicsContext> gc;
    if (auto *memory = wxDynamicCast(&dc, wxMemoryDC))
        gc.reset(wxGraphicsContext::Create(*memory));
    if (gc)
        gc->SetAntialiasMode(wxANTIALIAS_DEFAULT);
    return gc;
}

double NowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

// Tween

void Tween::Set(double target, double durationMs) {
    if (target == to_ && Running())
        return;
    from_ = Value();
    to_ = target;
    start_ = NowMs();
    duration_ = durationMs;
}

void Tween::Jump(double value) {
    from_ = to_ = value;
    duration_ = 0.0;
}

double Tween::Value() const {
    if (duration_ <= 0.0)
        return to_;
    const double t = (NowMs() - start_) / duration_;
    if (t >= 1.0)
        return to_;
    const double eased = 1.0 - std::pow(1.0 - t, 3.0);
    return from_ + (to_ - from_) * eased;
}

bool Tween::Running() const {
    return duration_ > 0.0 && NowMs() - start_ < duration_;
}

// Ticker

void Ticker::Kick() {
    if (!IsRunning())
        Start(15);
    owner_->Refresh(false);
}

void Ticker::Notify() {
    const bool more = step_ ? step_() : false;
    owner_->Refresh(false);
    if (!more)
        Stop();
}

// Helpers

void RelayoutAncestors(wxWindow *window) {
    for (wxWindow *current = window; current; current = current->GetParent()) {
        if (auto *collapsible = dynamic_cast<Collapsible *>(current))
            collapsible->SyncHeight();
        current->Layout();
        if (dynamic_cast<wxScrollHelperBase *>(current)) {
            current->FitInside();
            current->Refresh(false);
            break;
        }
        if (current->IsTopLevel())
            break;
    }
}

std::vector<wxString> WrapLines(wxDC &dc, const wxString &text, int width) {
    std::vector<wxString> lines;
    for (const wxString &paragraph : wxSplit(text, '\n', '\0')) {
        wxString line;
        for (const wxString &word : wxSplit(paragraph, ' ', '\0')) {
            const wxString candidate = line.empty() ? word : line + " " + word;
            if (line.empty() || dc.GetTextExtent(candidate).x <= width) {
                line = candidate;
            } else {
                lines.push_back(line);
                line = word;
            }
        }
        lines.push_back(line);
    }
    // A single long token, such as a path, is shortened rather than overflowing.
    for (wxString &line : lines)
        if (dc.GetTextExtent(line).x > width)
            line = wxControl::Ellipsize(line, dc, wxELLIPSIZE_MIDDLE, width);
    return lines;
}

void DrawSpinner(wxGraphicsContext &gc, double cx, double cy, double radius, double stroke, const wxColour &colour) {
    const double phase = std::fmod(NowMs() / 850.0, 1.0) * 2 * kPi;
    gc.SetBrush(*wxTRANSPARENT_BRUSH);
    gc.SetPen(gc.CreatePen(wxGraphicsPenInfo(wxColour(colour.Red(), colour.Green(), colour.Blue(), 48)).Width(stroke)));
    wxGraphicsPath track = gc.CreatePath();
    track.AddCircle(cx, cy, radius);
    gc.StrokePath(track);
    gc.SetPen(gc.CreatePen(wxGraphicsPenInfo(colour).Width(stroke).Cap(wxCAP_ROUND)));
    wxGraphicsPath arc = gc.CreatePath();
    arc.AddArc(cx, cy, radius, phase, phase + kPi * 0.6, true);
    gc.StrokePath(arc);
}

// Button

Button::Button(wxWindow *parent, const wxString &label, Kind kind, wchar_t icon)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS), kind_(kind),
      icon_(icon), ticker_(this, [this] { return hover_.Running() || press_.Running() || busy_; }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Theme::UiFont(9.5, kind == Kind::Primary ? wxFONTWEIGHT_SEMIBOLD : wxFONTWEIGHT_MEDIUM));
    wxControl::SetLabel(label);
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &Button::OnPaint, this);
    for (const auto &type : {wxEVT_ENTER_WINDOW, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_DOWN, wxEVT_LEFT_DCLICK, wxEVT_LEFT_UP})
        Bind(type, &Button::OnMouse, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent &) {
        pressed_ = false;
        press_.Set(0.0, kHoverMs);
        ticker_.Kick();
    });
    Bind(wxEVT_KEY_DOWN, &Button::OnKey, this);
    RefreshOnFocus(this);
    SetInitialSize();
}

bool Button::Enable(bool enable) {
    const bool changed = wxControl::Enable(enable);
    SetCursor(wxCursor(enable ? wxCURSOR_HAND : wxCURSOR_ARROW));
    if (!enable) {
        hover_.Jump(0.0);
        press_.Jump(0.0);
    }
    Refresh(false);
    return changed;
}

void Button::SetBusy(bool busy) {
    busy_ = busy;
    SetCursor(wxCursor(busy || !IsEnabled() ? wxCURSOR_ARROW : wxCURSOR_HAND));
    ticker_.Kick();
}

void Button::SetLabel(const wxString &label) {
    if (label == GetLabel())
        return;
    wxControl::SetLabel(label);
    InvalidateBestSize();
    SetMinSize(wxDefaultSize);
    SetInitialSize();
    RelayoutAncestors(GetParent());
    Refresh(false);
}

wxSize Button::DoGetBestClientSize() const {
    wxClientDC dc(const_cast<Button *>(this));
    dc.SetFont(GetFont());
    const wxSize text = dc.GetTextExtent(GetLabel());
    const bool slot = icon_ != 0 || kind_ == Kind::Primary;
    const int padding = kind_ == Kind::Quiet ? FromDIP(10) : FromDIP(16);
    int width = text.x + 2 * padding + (slot ? FromDIP(22) : 0);
    if (kind_ != Kind::Quiet)
        width = std::max(width, FromDIP(kind_ == Kind::Primary ? 132 : 84));
    return wxSize(width, FromDIP(kind_ == Kind::Quiet ? 30 : 36));
}

void Button::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxColour base = GetParent()->GetBackgroundColour();
    const bool enabled = IsEnabled();
    const double hover = enabled && !busy_ ? hover_.Value() : 0.0;
    const double press = press_.Value();
    const wxSize size = GetClientSize();

    wxColour fill;
    wxColour border;
    wxColour text;
    switch (kind_) {
        case Kind::Primary:
            fill = Mix(Mix(Theme::kPrimary, Theme::kPrimaryHover, hover), wxColour(0xc4, 0xc7, 0xce), press * 0.6);
            text = Theme::kPrimaryText;
            if (!enabled || busy_) {
                fill = Mix(base, Theme::kText, busy_ ? 0.72 : 0.12);
                text = busy_ ? Theme::kPrimaryText : Theme::kTextFaint;
            }
            break;
        case Kind::Secondary:
            fill = Mix(base, Theme::kText, 0.055 + 0.05 * hover - 0.03 * press);
            border = Mix(base, Theme::kText, 0.11 + 0.07 * hover);
            text = enabled ? Theme::kText : Theme::kTextFaint;
            if (!enabled)
                fill = Mix(base, Theme::kText, 0.025);
            break;
        case Kind::Quiet:
            fill = hover > 0.01 ? Mix(base, Theme::kText, 0.07 * hover) : wxColour();
            text = enabled ? Mix(Theme::kTextMuted, Theme::kText, hover) : Theme::kTextFaint;
            break;
    }

    dc.SetFont(GetFont());
    const wxSize extent = dc.GetTextExtent(GetLabel());
    const bool slot = icon_ != 0 || busy_;
    const int glyphWidth = slot ? FromDIP(22) : 0;
    const int left = (size.x - extent.x - glyphWidth) / 2;
    const double radius = FromDIP(8);
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        if (fill.IsOk())
            FillRounded(*gc, 0, 0, size.x, size.y, radius, fill, border);
        if (ShowsFocusRing(this))
            FillRounded(*gc, 0, 0, size.x, size.y, radius, wxNullColour, Theme::kAccent, FromDIP(2));
        if (busy_)
            DrawSpinner(*gc, left + FromDIP(8), size.y / 2.0, FromDIP(6), FromDIP(2), text);
    }
    if (icon_ != 0 && !busy_)
        DrawGlyph(dc, icon_, wxRect(left, 0, FromDIP(16), size.y), text, 10);
    DrawTextAt(dc, GetLabel(), left + glyphWidth, (size.y - extent.y) / 2, text, GetFont());
}

void Button::OnMouse(wxMouseEvent &event) {
    if (!IsEnabled() || busy_) {
        event.Skip();
        return;
    }
    if (event.Entering()) {
        hover_.Set(1.0, kHoverMs);
    } else if (event.Leaving()) {
        hover_.Set(0.0, kHoverMs * 1.6);
    } else if (event.LeftDown() || event.LeftDClick()) {
        pressed_ = true;
        press_.Set(1.0, 60);
        if (!HasCapture())
            CaptureMouse();
        SetFocus();
    } else if (event.LeftUp()) {
        const bool wasPressed = pressed_;
        pressed_ = false;
        press_.Set(0.0, kHoverMs * 1.5);
        if (HasCapture())
            ReleaseMouse();
        if (wasPressed && GetClientRect().Contains(event.GetPosition()))
            CallAfter([this] { Click(); });
    }
    ticker_.Kick();
}

void Button::OnKey(wxKeyEvent &event) {
    if (IsActivateKey(event)) {
        Click();
        return;
    }
    HandleTab(this, event);
}

void Button::Click() {
    if (!IsEnabled() || busy_)
        return;
    wxCommandEvent event(wxEVT_BUTTON, GetId());
    event.SetEventObject(this);
    ProcessWindowEvent(event);
}

// Surface

Surface::Surface(wxWindow *parent, int padding) : wxPanel(parent, wxID_ANY) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::kSurface);
    Bind(wxEVT_PAINT, [this](wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        ClearToParent(dc, this);
        if (std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc)) {
            const wxSize size = GetClientSize();
            FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(12), Theme::kSurface, Theme::kBorder);
        }
    });
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        Refresh(false);
        event.Skip();
    });
    auto *outer = new wxBoxSizer(wxVERTICAL);
    body_ = new wxBoxSizer(wxVERTICAL);
    outer->Add(body_, 1, wxEXPAND | wxALL, FromDIP(padding));
    SetSizer(outer);
}

// Segmented

Segmented::Segmented(wxWindow *parent, std::vector<wxString> items, Style style, std::function<void(int)> onSelect)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
      items_(std::move(items)), style_(style), onSelect_(std::move(onSelect)), hoverT_(items_.size()),
      ticker_(this, [this] {
          bool running = x_.Running() || width_.Running();
          for (const Tween &tween : hoverT_)
              running = running || tween.Running();
          return running;
      }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Theme::UiFont(style == Style::Nav ? 9.5 : 9.0, wxFONTWEIGHT_MEDIUM));
    Bind(wxEVT_PAINT, &Segmented::OnPaint, this);
    for (const auto &type : {wxEVT_MOTION, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_DOWN})
        Bind(type, &Segmented::OnMouse, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        const std::vector<wxRect> rects = ItemRects();
        if (selected_ >= 0 && selected_ < static_cast<int>(rects.size())) {
            x_.Jump(rects[selected_].x);
            width_.Jump(rects[selected_].width);
        }
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
        const int step = event.GetKeyCode() == WXK_LEFT ? -1 : event.GetKeyCode() == WXK_RIGHT ? 1 : 0;
        if (step == 0) {
            HandleTab(this, event);
            return;
        }
        Choose(std::clamp(selected_ + step, 0, static_cast<int>(items_.size()) - 1));
    });
    RefreshOnFocus(this);
    SetInitialSize();
}

bool Segmented::Enable(bool enable) {
    const bool changed = wxControl::Enable(enable);
    Refresh(false);
    return changed;
}

wxSize Segmented::DoGetBestClientSize() const {
    wxClientDC dc(const_cast<Segmented *>(this));
    dc.SetFont(GetFont());
    int width = 2 * FromDIP(3);
    for (const wxString &item : items_)
        width += dc.GetTextExtent(item).x + 2 * FromDIP(style_ == Style::Nav ? 16 : 12);
    return wxSize(width, FromDIP(style_ == Style::Nav ? 36 : 34));
}

std::vector<wxRect> Segmented::ItemRects() const {
    std::vector<wxRect> rects;
    const wxSize size = GetClientSize();
    const int pad = FromDIP(3);
    const int height = size.y - 2 * pad;
    if (style_ == Style::Field) {
        const int count = static_cast<int>(items_.size());
        const int each = count > 0 ? (size.x - 2 * pad) / count : 0;
        for (int index = 0; index < count; ++index)
            rects.emplace_back(pad + index * each, pad, index == count - 1 ? size.x - 2 * pad - index * each : each,
                               height);
        return rects;
    }
    wxClientDC dc(const_cast<Segmented *>(this));
    dc.SetFont(GetFont());
    int x = pad;
    for (const wxString &item : items_) {
        const int width = dc.GetTextExtent(item).x + 2 * FromDIP(16);
        rects.emplace_back(x, pad, width, height);
        x += width;
    }
    return rects;
}

void Segmented::Select(int index, bool animate) {
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return;
    selected_ = index;
    const std::vector<wxRect> rects = ItemRects();
    if (animate) {
        x_.Set(rects[index].x, kMoveMs);
        width_.Set(rects[index].width, kMoveMs);
        ticker_.Kick();
    } else {
        x_.Jump(rects[index].x);
        width_.Jump(rects[index].width);
        Refresh(false);
    }
}

void Segmented::Choose(int index) {
    if (index == selected_ || !IsEnabled())
        return;
    Select(index);
    if (onSelect_)
        onSelect_(index);
}

void Segmented::OnMouse(wxMouseEvent &event) {
    int hit = -1;
    if (!event.Leaving()) {
        const std::vector<wxRect> rects = ItemRects();
        for (int index = 0; index < static_cast<int>(rects.size()); ++index)
            if (rects[index].Contains(event.GetPosition()))
                hit = index;
    }
    if (hit != hover_) {
        if (hover_ >= 0)
            hoverT_[hover_].Set(0.0, kHoverMs * 1.6);
        if (hit >= 0)
            hoverT_[hit].Set(1.0, kHoverMs);
        hover_ = hit;
        SetCursor(wxCursor(hit >= 0 && IsEnabled() ? wxCURSOR_HAND : wxCURSOR_ARROW));
        ticker_.Kick();
    }
    if (event.LeftDown() && hit >= 0) {
        SetFocus();
        Choose(hit);
    }
}

void Segmented::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxColour base = GetParent()->GetBackgroundColour();
    const wxSize size = GetClientSize();
    const std::vector<wxRect> rects = ItemRects();
    const bool enabled = IsEnabled();
    const double outer = style_ == Style::Nav ? size.y / 2.0 : FromDIP(9);
    const double inner = style_ == Style::Nav ? (size.y - 2 * FromDIP(3)) / 2.0 : FromDIP(7);
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        FillRounded(*gc, 0, 0, size.x, size.y, outer, Mix(base, Theme::kText, 0.035), Mix(base, Theme::kText, 0.075));
        for (int index = 0; index < static_cast<int>(rects.size()); ++index) {
            const double hover = hoverT_[index].Value();
            if (index != selected_ && hover > 0.01 && enabled)
                FillRounded(*gc, rects[index].x, rects[index].y, rects[index].width, rects[index].height, inner,
                            Mix(base, Theme::kText, 0.035 + 0.03 * hover));
        }
        if (!rects.empty()) {
            const wxColour indicator = enabled ? Mix(base, Theme::kText, 0.12) : Mix(base, Theme::kText, 0.06);
            FillRounded(*gc, x_.Value(), FromDIP(3), width_.Value(), size.y - 2 * FromDIP(3), inner, indicator,
                        Mix(base, Theme::kText, enabled ? 0.16 : 0.08));
        }
        if (ShowsFocusRing(this))
            FillRounded(*gc, 0, 0, size.x, size.y, outer, wxNullColour, Theme::kAccent, Dip(this, 1.5));
    }
    dc.SetFont(GetFont());
    for (int index = 0; index < static_cast<int>(rects.size()); ++index) {
        const wxRect &rect = rects[index];
        const wxSize extent = dc.GetTextExtent(items_[index]);
        wxColour colour = index == selected_ ? Theme::kText
                                             : Mix(Theme::kTextMuted, Theme::kText, hoverT_[index].Value() * 0.8);
        if (!enabled)
            colour = index == selected_ ? Theme::kTextMuted : Theme::kTextFaint;
        DrawTextAt(dc, items_[index], rect.x + (rect.width - extent.x) / 2, rect.y + (rect.height - extent.y) / 2,
                   colour, GetFont());
    }
}

// ChoiceTiles

ChoiceTiles::ChoiceTiles(wxWindow *parent, std::vector<Item> items, std::function<void(int)> onSelect)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
      items_(std::move(items)), onSelect_(std::move(onSelect)), selectT_(items_.size()), hoverT_(items_.size()),
      ticker_(this, [this] {
          bool running = false;
          for (size_t index = 0; index < items_.size(); ++index)
              running = running || selectT_[index].Running() || hoverT_[index].Running();
          return running;
      }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    if (!selectT_.empty())
        selectT_[0].Jump(1.0);
    Bind(wxEVT_PAINT, &ChoiceTiles::OnPaint, this);
    for (const auto &type : {wxEVT_MOTION, wxEVT_LEAVE_WINDOW, wxEVT_LEFT_DOWN})
        Bind(type, &ChoiceTiles::OnMouse, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        event.Skip();
        const int height = HeightFor(event.GetSize().x);
        if (height != height_) {
            height_ = height;
            SetMinSize(wxSize(FromDIP(320), height));
            InvalidateBestSize();
            CallAfter([this] { RelayoutAncestors(GetParent()); });
        }
        Refresh(false);
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
        const int step = event.GetKeyCode() == WXK_LEFT ? -1 : event.GetKeyCode() == WXK_RIGHT ? 1 : 0;
        if (step == 0) {
            HandleTab(this, event);
            return;
        }
        const int next = std::clamp(selected_ + step, 0, static_cast<int>(items_.size()) - 1);
        if (next != selected_) {
            Select(next);
            if (onSelect_)
                onSelect_(next);
        }
    });
    RefreshOnFocus(this);
    SetInitialSize(wxSize(FromDIP(320), HeightFor(FromDIP(640))));
}

int ChoiceTiles::TileWidth(int width) const {
    const int count = std::max<int>(1, static_cast<int>(items_.size()));
    return (width - (count - 1) * FromDIP(12)) / count;
}

int ChoiceTiles::HeightFor(int width) const {
    wxClientDC dc(const_cast<ChoiceTiles *>(this));
    dc.SetFont(Theme::UiFont(9.0));
    const int textWidth = TileWidth(width) - 2 * FromDIP(18);
    size_t lines = 1;
    for (const Item &item : items_)
        lines = std::max(lines, WrapLines(dc, item.text, std::max(textWidth, FromDIP(60))).size());
    const int lineHeight = dc.GetTextExtent("Ag").y;
    const int titleHeight = TextHeight(const_cast<ChoiceTiles *>(this), Theme::UiFont(10.5, wxFONTWEIGHT_SEMIBOLD));
    return FromDIP(18) + FromDIP(34) + FromDIP(12) + titleHeight + FromDIP(4) + static_cast<int>(lines) * lineHeight +
           FromDIP(18);
}

wxSize ChoiceTiles::DoGetBestClientSize() const {
    return wxSize(FromDIP(320), HeightFor(std::max(GetClientSize().x, FromDIP(320))));
}

wxRect ChoiceTiles::TileRect(int index) const {
    const wxSize size = GetClientSize();
    const int width = TileWidth(size.x);
    return wxRect(index * (width + FromDIP(12)), 0, width, size.y);
}

void ChoiceTiles::Select(int index, bool animate) {
    if (index < 0 || index >= static_cast<int>(items_.size()))
        return;
    selected_ = index;
    for (int item = 0; item < static_cast<int>(items_.size()); ++item) {
        if (animate)
            selectT_[item].Set(item == index ? 1.0 : 0.0, 220);
        else
            selectT_[item].Jump(item == index ? 1.0 : 0.0);
    }
    ticker_.Kick();
}

void ChoiceTiles::OnMouse(wxMouseEvent &event) {
    int hit = -1;
    if (!event.Leaving())
        for (int index = 0; index < static_cast<int>(items_.size()); ++index)
            if (TileRect(index).Contains(event.GetPosition()))
                hit = index;
    if (hit != hover_) {
        if (hover_ >= 0)
            hoverT_[hover_].Set(0.0, kHoverMs * 1.6);
        if (hit >= 0)
            hoverT_[hit].Set(1.0, kHoverMs);
        hover_ = hit;
        SetCursor(wxCursor(hit >= 0 ? wxCURSOR_HAND : wxCURSOR_ARROW));
        ticker_.Kick();
    }
    if (event.LeftDown() && hit >= 0) {
        SetFocus();
        if (hit != selected_) {
            Select(hit);
            if (onSelect_)
                onSelect_(hit);
        }
    }
}

void ChoiceTiles::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxColour base = GetParent()->GetBackgroundColour();
    const int pad = FromDIP(18);
    const int iconSize = FromDIP(34);
    const wxFont titleFont = Theme::UiFont(10.5, wxFONTWEIGHT_SEMIBOLD);
    const wxFont textFont = Theme::UiFont(9.0);
    const int titleHeight = TextHeight(this, titleFont);
    for (int index = 0; index < static_cast<int>(items_.size()); ++index) {
        const wxRect rect = TileRect(index);
        const double select = selectT_[index].Value();
        const double hover = hoverT_[index].Value();
        const wxColour fill = Mix(Mix(Theme::kSurface, Theme::kSurfaceHover, hover), Theme::kAccent, select * 0.07);
        const wxColour border = Mix(Mix(Theme::kBorder, Theme::kBorderStrong, hover), Theme::kAccent, select);
        const wxRect icon(rect.x + pad, rect.y + pad, iconSize, iconSize);
        const double radioX = rect.GetRight() - pad - FromDIP(8) + 1;
        const double radioY = rect.y + pad + iconSize / 2.0;
        {
            std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
            if (!gc)
                return;
            FillRounded(*gc, rect.x, rect.y, rect.width, rect.height, FromDIP(12), fill, border,
                        FromDIP(1) + select * Dip(this, 0.5));
            FillRounded(*gc, icon.x, icon.y, icon.width, icon.height, FromDIP(9),
                        Mix(Mix(fill, Theme::kText, 0.07), Theme::kAccent, select * 0.22));
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(Mix(Theme::kBorderStrong, Theme::kAccent, select)).Width(Dip(this, 1.5))));
            gc->DrawEllipse(radioX - FromDIP(8), radioY - FromDIP(8), FromDIP(16), FromDIP(16));
            if (select > 0.01) {
                const double dot = Dip(this, 4.5) * select;
                gc->SetPen(*wxTRANSPARENT_PEN);
                gc->SetBrush(wxBrush(Theme::kAccent));
                gc->DrawEllipse(radioX - dot, radioY - dot, 2 * dot, 2 * dot);
            }
        }
        DrawGlyph(dc, items_[index].icon, icon, Mix(Theme::kTextMuted, Theme::kAccentHover, select), 12);
        const int titleY = icon.GetBottom() + FromDIP(12);
        DrawTextAt(dc, items_[index].title, rect.x + pad, titleY, Theme::kText, titleFont);
        dc.SetFont(textFont);
        int y = titleY + titleHeight + FromDIP(4);
        for (const wxString &line : WrapLines(dc, items_[index].text, rect.width - 2 * pad)) {
            DrawTextAt(dc, line, rect.x + pad, y, Mix(Theme::kTextMuted, Theme::kText, select * 0.25), textFont);
            y += dc.GetTextExtent("Ag").y;
        }
        if (ShowsFocusRing(this) && index == selected_) {
            std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
            if (gc)
                FillRounded(*gc, rect.x, rect.y, rect.width, rect.height, FromDIP(12), wxNullColour,
                            Theme::kAccentHover, FromDIP(2));
        }
    }
    (void)base;
}

// Toggle

Toggle::Toggle(wxWindow *parent, const wxString &label, std::function<void(bool)> onChange)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
      onChange_(std::move(onChange)), ticker_(this, [this] { return knob_.Running(); }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Theme::UiFont(9.5));
    wxControl::SetLabel(label);
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &Toggle::OnPaint, this);
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
        SetFocus();
        Flip();
    });
    Bind(wxEVT_LEFT_DCLICK, [this](wxMouseEvent &) { Flip(); });
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) {
        hovered_ = true;
        Refresh(false);
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) {
        hovered_ = false;
        Refresh(false);
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
        if (IsActivateKey(event))
            Flip();
        else
            HandleTab(this, event);
    });
    RefreshOnFocus(this);
    SetInitialSize();
}

bool Toggle::Enable(bool enable) {
    const bool changed = wxControl::Enable(enable);
    SetCursor(wxCursor(enable ? wxCURSOR_HAND : wxCURSOR_ARROW));
    Refresh(false);
    return changed;
}

void Toggle::SetValue(bool value, bool animate) {
    value_ = value;
    if (animate)
        knob_.Set(value ? 1.0 : 0.0, 200);
    else
        knob_.Jump(value ? 1.0 : 0.0);
    ticker_.Kick();
}

void Toggle::Flip() {
    if (!IsEnabled())
        return;
    SetValue(!value_, true);
    if (onChange_)
        onChange_(value_);
}

wxSize Toggle::DoGetBestClientSize() const {
    wxClientDC dc(const_cast<Toggle *>(this));
    dc.SetFont(GetFont());
    return wxSize(FromDIP(38 + 12) + dc.GetTextExtent(GetLabel()).x + FromDIP(4), FromDIP(28));
}

void Toggle::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxColour base = GetParent()->GetBackgroundColour();
    const wxSize size = GetClientSize();
    const double t = knob_.Value();
    const bool enabled = IsEnabled();
    const double trackW = FromDIP(38);
    const double trackH = FromDIP(22);
    const double top = (size.y - trackH) / 2.0;
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        wxColour track = Mix(Mix(base, Theme::kText, hovered_ ? 0.2 : 0.15), Theme::kAccent, t);
        if (!enabled)
            track = Mix(base, Theme::kText, 0.08);
        FillRounded(*gc, 0, top, trackW, trackH, trackH / 2, track);
        const double knob = trackH - FromDIP(6);
        const double x = FromDIP(3) + t * (trackW - knob - FromDIP(6));
        gc->SetPen(*wxTRANSPARENT_PEN);
        gc->SetBrush(wxBrush(!enabled ? Theme::kTextFaint : Mix(wxColour(0xee, 0xef, 0xf2), Theme::kPrimaryText, t)));
        gc->DrawEllipse(x, top + FromDIP(3), knob, knob);
        if (ShowsFocusRing(this))
            FillRounded(*gc, 0, top, trackW, trackH, trackH / 2, wxNullColour, Theme::kAccentHover, Dip(this, 1.5));
    }
    dc.SetFont(GetFont());
    const wxSize extent = dc.GetTextExtent(GetLabel());
    DrawTextAt(dc, GetLabel(), FromDIP(38 + 12), (size.y - extent.y) / 2, enabled ? Theme::kText : Theme::kTextFaint,
               GetFont());
}

// TextField

TextField::TextField(wxWindow *parent, const wxString &value, const wxString &hint)
    : wxPanel(parent, wxID_ANY), ticker_(this, [this] { return focus_.Running(); }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::kInput);
    ctrl_ = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE);
    ctrl_->SetBackgroundColour(Theme::kInput);
    ctrl_->SetForegroundColour(Theme::kText);
    ctrl_->SetFont(Theme::UiFont(10));
    if (!hint.empty())
        ctrl_->SetHint(hint);
    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(ctrl_, 1, wxALIGN_CENTER_VERTICAL | wxLEFT | wxRIGHT, FromDIP(12));
    SetSizer(sizer);
    SetMinSize(wxSize(FromDIP(120), FromDIP(40)));

    Bind(wxEVT_PAINT, &TextField::OnPaint, this);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) { ctrl_->SetFocus(); });
    SetCursor(wxCursor(wxCURSOR_IBEAM));
    ctrl_->Bind(wxEVT_SET_FOCUS, [this](wxFocusEvent &event) {
        event.Skip();
        focus_.Set(1.0, 180);
        ticker_.Kick();
        // Paths are long; keep the caret at the end instead of selecting everything.
        ctrl_->CallAfter([this] { ctrl_->SetInsertionPointEnd(); });
    });
    ctrl_->Bind(wxEVT_KILL_FOCUS, [this](wxFocusEvent &event) {
        event.Skip();
        focus_.Set(0.0, 220);
        ticker_.Kick();
    });
}

void TextField::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
    if (!gc)
        return;
    const wxSize size = GetClientSize();
    const double focus = focus_.Value();
    FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(9), Theme::kInput, Mix(Theme::kBorder, Theme::kAccent, focus),
                FromDIP(1) + focus * Dip(this, 0.5));
}

// DropZone

namespace {

class DropZoneTarget : public wxFileDropTarget {
  public:
    explicit DropZoneTarget(DropZone *zone, std::function<void(const wxString &)> onPick)
        : zone_(zone), onPick_(std::move(onPick)) {}
    wxDragResult OnEnter(wxCoord x, wxCoord y, wxDragResult result) override {
        zone_->SetDragging(true);
        return wxFileDropTarget::OnEnter(x, y, result);
    }
    void OnLeave() override {
        zone_->SetDragging(false);
        wxFileDropTarget::OnLeave();
    }
    bool OnDropFiles(wxCoord, wxCoord, const wxArrayString &files) override {
        zone_->SetDragging(false);
        if (!files.empty() && onPick_)
            onPick_(files[0]);
        return true;
    }

  private:
    DropZone *zone_;
    std::function<void(const wxString &)> onPick_;
};

} // namespace

DropZone::DropZone(wxWindow *parent, Mode mode, const wxString &emptyTitle, std::function<void(const wxString &)> onPick)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS), mode_(mode),
      emptyTitle_(emptyTitle), onPick_(std::move(onPick)),
      ticker_(this, [this] { return hover_.Running() || drag_.Running() || fresh_.Running(); }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetCursor(wxCursor(wxCURSOR_HAND));
    Bind(wxEVT_PAINT, &DropZone::OnPaint, this);
    BindHover(this, hovered_, hover_, ticker_);
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) { Browse(); });
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        Refresh(false);
        event.Skip();
    });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
        if (IsActivateKey(event))
            Browse();
        else
            HandleTab(this, event);
    });
    RefreshOnFocus(this);
    SetDropTarget(new DropZoneTarget(this, onPick_));
    SetInitialSize();
}

wxSize DropZone::DoGetBestClientSize() const {
    return wxSize(FromDIP(320), FromDIP(filled_ ? 76 : 124));
}

void DropZone::SetEmpty() {
    const bool changed = filled_;
    filled_ = false;
    path_.clear();
    if (changed) {
        InvalidateBestSize();
        SetMinSize(wxDefaultSize);
        SetInitialSize();
        RelayoutAncestors(GetParent());
        fresh_.Jump(0.0);
        fresh_.Set(1.0, 260);
    }
    ticker_.Kick();
}

void DropZone::SetFilled(const wxString &path, const wxString &title, const wxString &chip, const wxColour &chipColour) {
    const bool changed = !filled_ || path != path_;
    const bool resized = !filled_;
    filled_ = true;
    path_ = path;
    title_ = title;
    chip_ = chip;
    chipColour_ = chipColour;
    if (resized) {
        InvalidateBestSize();
        SetMinSize(wxDefaultSize);
        SetInitialSize();
        RelayoutAncestors(GetParent());
    }
    if (changed) {
        fresh_.Jump(0.0);
        fresh_.Set(1.0, 260);
    }
    ticker_.Kick();
}

void DropZone::SetDragging(bool dragging) {
    drag_.Set(dragging ? 1.0 : 0.0, kHoverMs);
    ticker_.Kick();
}

void DropZone::Browse() {
    wxString picked;
    if (mode_ == Mode::File) {
        const wxString directory = path_.empty() ? wxString() : wxFileName(path_).GetPath();
        wxFileDialog dialog(this, "Choose the game", directory, wxEmptyString, "Game (*.exe)|*.exe",
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
            picked = dialog.GetPath();
    } else {
        wxDirDialog dialog(this, "Choose the project folder", path_, wxDD_DEFAULT_STYLE | wxDD_DIR_MUST_EXIST);
        if (dialog.ShowModal() == wxID_OK)
            picked = dialog.GetPath();
    }
    hovered_ = false;
    hover_.Set(0.0, kHoverMs);
    ticker_.Kick();
    if (!picked.empty() && onPick_)
        onPick_(picked);
}

void DropZone::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxSize size = GetClientSize();
    const double hover = hover_.Value();
    const double drag = drag_.Value();
    const double fresh = fresh_.Value();
    const double lift = std::max(hover, drag);
    const wxFont titleFont = Theme::UiFont(filled_ ? 11 : 10.5, wxFONTWEIGHT_SEMIBOLD);
    const wxFont textFont = Theme::UiFont(9.0);
    const int iconSize = FromDIP(filled_ ? 42 : 44);

    if (!filled_) {
        const wxRect icon((size.x - iconSize) / 2, FromDIP(18), iconSize, iconSize);
        {
            std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
            if (!gc)
                return;
            const wxColour fill = Mix(Mix(Theme::kWindow, Theme::kSurface, 0.55 + 0.45 * hover), Theme::kAccent,
                                      drag * 0.08);
            gc->SetBrush(wxBrush(fill));
            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(Mix(Theme::kBorderStrong, Theme::kAccent, std::max(hover * 0.55, drag)))
                           .Width(Dip(this, 1.5))
                           .Style(wxPENSTYLE_SHORT_DASH)));
            gc->DrawRoundedRectangle(FromDIP(1), FromDIP(1), size.x - FromDIP(2), size.y - FromDIP(2), FromDIP(12));
            const double grow = lift * FromDIP(2);
            FillRounded(*gc, icon.x - grow, icon.y - grow, icon.width + 2 * grow, icon.height + 2 * grow,
                        iconSize / 2.0 + grow, Mix(Mix(fill, Theme::kText, 0.07), Theme::kAccent, 0.18 + lift * 0.2));
            if (ShowsFocusRing(this))
                FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(12), wxNullColour, Theme::kAccentHover, FromDIP(2));
        }
        DrawGlyph(dc, mode_ == Mode::File ? Theme::Icon::kOpenFile : Theme::Icon::kFolder, icon,
                  Mix(Theme::kAccentHover, Theme::kText, lift * 0.5), 13);
        dc.SetFont(titleFont);
        const wxString title = drag > 0.5 ? wxString("Drop it") : emptyTitle_;
        wxSize extent = dc.GetTextExtent(title);
        const int titleY = icon.GetBottom() + FromDIP(10);
        DrawTextAt(dc, title, (size.x - extent.x) / 2, titleY, Mix(Theme::kWindow, Theme::kText, fresh), titleFont);
        dc.SetFont(textFont);
        const wxString hint = "or click to browse";
        const int hintY = titleY + extent.y + FromDIP(1);
        extent = dc.GetTextExtent(hint);
        DrawTextAt(dc, hint, (size.x - extent.x) / 2, hintY, Mix(Theme::kWindow, Theme::kTextMuted, fresh), textFont);
        return;
    }

    const int pad = FromDIP(16);
    const wxRect icon(pad, (size.y - iconSize) / 2, iconSize, iconSize);
    dc.SetFont(Theme::UiFont(8.5, wxFONTWEIGHT_SEMIBOLD));
    const wxSize chipText = chip_.empty() ? wxSize() : dc.GetTextExtent(chip_);
    const wxRect chip(0, 0, chip_.empty() ? 0 : chipText.x + FromDIP(20), FromDIP(24));
    dc.SetFont(textFont);
    const wxString change = "Change";
    const wxSize changeText = dc.GetTextExtent(change);
    const int changeX = size.x - pad - changeText.x;
    const int chipX = changeX - FromDIP(18) - chip.width;
    const wxRect chipRect(chipX, (size.y - chip.height) / 2, chip.width, chip.height);
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        const wxColour fill = Mix(Mix(Theme::kSurface, Theme::kSurfaceHover, hover), Theme::kAccent, drag * 0.08);
        FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(12), fill,
                    Mix(Mix(Theme::kBorder, Theme::kBorderStrong, hover), Theme::kAccent, drag));
        FillRounded(*gc, icon.x, icon.y, icon.width, icon.height, FromDIP(10), Mix(fill, Theme::kText, 0.07));
        if (!chip_.empty())
            FillRounded(*gc, chipRect.x, chipRect.y, chipRect.width, chipRect.height, chipRect.height / 2.0,
                        Mix(fill, chipColour_, 0.16 * fresh));
        if (ShowsFocusRing(this))
            FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(12), wxNullColour, Theme::kAccentHover, FromDIP(2));
    }
    const wxColour fillForText = Mix(Theme::kSurface, Theme::kSurfaceHover, hover);
    DrawGlyph(dc, mode_ == Mode::File ? Theme::Icon::kGame : Theme::Icon::kFolder, icon,
              Mix(fillForText, Theme::kText, fresh), 13);
    const int textX = icon.GetRight() + FromDIP(14);
    const int textWidth = std::max(FromDIP(40), (chip_.empty() ? changeX : chipX) - textX - FromDIP(16));
    dc.SetFont(titleFont);
    const int titleHeight = dc.GetTextExtent("Ag").y;
    dc.SetFont(textFont);
    const int lineHeight = dc.GetTextExtent("Ag").y;
    const int top = (size.y - titleHeight - lineHeight - FromDIP(1)) / 2;
    dc.SetFont(titleFont);
    DrawTextAt(dc, wxControl::Ellipsize(title_, dc, wxELLIPSIZE_END, textWidth), textX, top,
               Mix(fillForText, Theme::kText, fresh), titleFont);
    dc.SetFont(textFont);
    DrawTextAt(dc, wxControl::Ellipsize(path_, dc, wxELLIPSIZE_MIDDLE, textWidth), textX,
               top + titleHeight + FromDIP(1), Mix(fillForText, Theme::kTextMuted, fresh), textFont);
    if (!chip_.empty()) {
        const wxFont chipFont = Theme::UiFont(8.5, wxFONTWEIGHT_SEMIBOLD);
        DrawTextAt(dc, chip_, chipRect.x + (chipRect.width - chipText.x) / 2,
                   chipRect.y + (chipRect.height - chipText.y) / 2, Mix(fillForText, chipColour_, fresh), chipFont);
    }
    DrawTextAt(dc, change, changeX, (size.y - changeText.y) / 2, Mix(Theme::kTextMuted, Theme::kText, hover),
               textFont);
}

// StatusCard

StatusCard::StatusCard(wxWindow *parent)
    : wxPanel(parent, wxID_ANY), ticker_(this, [this] {
          return fresh_.Running() || tone_ == Tone::Busy || tone_ == Tone::Waiting;
      }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::kSurface);
    Bind(wxEVT_PAINT, &StatusCard::OnPaint, this);
    auto *sizer = new wxBoxSizer(wxHORIZONTAL);
    textItem_ = sizer->Add(0, 0, 1, wxALIGN_CENTER_VERTICAL);
    actions_ = new wxBoxSizer(wxHORIZONTAL);
    sizer->Add(actions_, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, FromDIP(16));
    SetSizer(sizer);
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        event.Skip();
        CallAfter([this] { Refit(); });
        Refresh(false);
    });
}

void StatusCard::Set(Tone tone, const wxString &title, const wxString &text, wchar_t icon) {
    const bool changed = tone != tone_ || title != title_ || text != text_;
    tone_ = tone;
    title_ = title;
    text_ = text;
    icon_ = icon;
    if (changed) {
        fresh_.Jump(0.0);
        fresh_.Set(1.0, 320);
    }
    Refit();
    ticker_.Kick();
}

int StatusCard::TextLeft() const {
    return FromDIP(16 + 38 + 14);
}

int StatusCard::TextRight() const {
    const int actionsWidth = actions_->GetItemCount() > 0 ? actions_->CalcMin().x + FromDIP(16) : 0;
    return GetClientSize().x - FromDIP(16) - actionsWidth;
}

void StatusCard::Refit() {
    wxClientDC dc(this);
    const int width = std::max(FromDIP(80), TextRight() - TextLeft());
    dc.SetFont(Theme::UiFont(10.5, wxFONTWEIGHT_SEMIBOLD));
    int height = dc.GetTextExtent("Ag").y;
    if (!text_.empty()) {
        dc.SetFont(Theme::UiFont(9.0));
        height += FromDIP(2) + static_cast<int>(WrapLines(dc, text_, width).size()) * dc.GetTextExtent("Ag").y;
    }
    height = std::max(height, FromDIP(38)) + 2 * FromDIP(16);
    textItem_->SetMinSize(wxSize(TextLeft(), height));
    if (height != textHeight_) {
        textHeight_ = height;
        InvalidateBestSize();
        RelayoutAncestors(GetParent());
    } else {
        Layout();
    }
    Refresh(false);
}

void StatusCard::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxSize size = GetClientSize();
    const double fresh = fresh_.Value();
    wxColour colour = Theme::kTextMuted;
    wchar_t glyph = icon_;
    switch (tone_) {
        case Tone::Neutral:
            glyph = glyph ? glyph : Theme::Icon::kInfo;
            break;
        case Tone::Busy:
        case Tone::Waiting:
            colour = Theme::kAccent;
            break;
        case Tone::Info:
            colour = Theme::kAccentHover;
            glyph = glyph ? glyph : Theme::Icon::kInfo;
            break;
        case Tone::Success:
            colour = Theme::kSuccess;
            glyph = glyph ? glyph : Theme::Icon::kCheck;
            break;
        case Tone::Warning:
            colour = Theme::kWarning;
            glyph = glyph ? glyph : Theme::Icon::kWarning;
            break;
        case Tone::Error:
            colour = Theme::kError;
            glyph = glyph ? glyph : Theme::Icon::kError;
            break;
    }
    const int iconSize = FromDIP(38);
    const wxRect icon(FromDIP(16), (size.y - iconSize) / 2, iconSize, iconSize);
    const double cx = icon.x + iconSize / 2.0;
    const double cy = icon.y + iconSize / 2.0;
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        const wxColour border = tone_ == Tone::Warning || tone_ == Tone::Error ? Mix(Theme::kBorder, colour, 0.3)
                                                                                : Theme::kBorder;
        FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(12), Theme::kSurface, border);
        const double scale = 0.8 + 0.2 * fresh;
        const double radius = iconSize / 2.0 * scale;
        if (tone_ == Tone::Waiting) {
            const double phase = std::fmod(NowMs() / 1500.0, 1.0);
            const double ring = iconSize / 2.0 * (0.55 + 0.55 * phase);
            gc->SetBrush(*wxTRANSPARENT_BRUSH);
            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(wxColour(colour.Red(), colour.Green(), colour.Blue(),
                                                  static_cast<unsigned char>(150 * (1.0 - phase))))
                           .Width(Dip(this, 1.5))));
            gc->DrawEllipse(cx - ring, cy - ring, 2 * ring, 2 * ring);
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(Mix(Theme::kSurface, colour, 0.18)));
            gc->DrawEllipse(cx - radius * 0.6, cy - radius * 0.6, radius * 1.2, radius * 1.2);
            gc->SetBrush(wxBrush(colour));
            gc->DrawEllipse(cx - FromDIP(4), cy - FromDIP(4), FromDIP(8), FromDIP(8));
        } else {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(Mix(Theme::kSurface, colour, 0.16)));
            gc->DrawEllipse(cx - radius, cy - radius, 2 * radius, 2 * radius);
            if (tone_ == Tone::Busy)
                DrawSpinner(*gc, cx, cy, FromDIP(8), FromDIP(2), colour);
        }
    }
    if (tone_ != Tone::Busy && tone_ != Tone::Waiting)
        DrawGlyph(dc, glyph, icon, Mix(Theme::kSurface, colour, fresh), 12 * (0.85 + 0.15 * fresh));

    const int left = TextLeft();
    const int width = std::max(FromDIP(80), TextRight() - left);
    const wxFont titleFont = Theme::UiFont(10.5, wxFONTWEIGHT_SEMIBOLD);
    const wxFont textFont = Theme::UiFont(9.0);
    dc.SetFont(titleFont);
    const int titleHeight = dc.GetTextExtent("Ag").y;
    dc.SetFont(textFont);
    const std::vector<wxString> lines = text_.empty() ? std::vector<wxString>() : WrapLines(dc, text_, width);
    const int lineHeight = dc.GetTextExtent("Ag").y;
    const int block = titleHeight + (lines.empty() ? 0 : FromDIP(2) + static_cast<int>(lines.size()) * lineHeight);
    const int shift = static_cast<int>((1.0 - fresh) * FromDIP(4));
    int y = (size.y - block) / 2 + shift;
    dc.SetFont(titleFont);
    DrawTextAt(dc, wxControl::Ellipsize(title_, dc, wxELLIPSIZE_END, width), left, y,
               Mix(Theme::kSurface, Theme::kText, fresh), titleFont);
    y += titleHeight + FromDIP(2);
    for (const wxString &line : lines) {
        DrawTextAt(dc, line, left, y, Mix(Theme::kSurface, Theme::kTextMuted, fresh), textFont);
        y += lineHeight;
    }
}

// Collapsible

class Collapsible::Header : public wxControl {
  public:
    Header(wxWindow *parent, const wxString &title, std::function<void()> onToggle)
        : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS),
          onToggle_(std::move(onToggle)), ticker_(this, [this] { return open_.Running() || hover_.Running(); }) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetFont(Theme::UiFont(9.5, wxFONTWEIGHT_MEDIUM));
        wxControl::SetLabel(title);
        SetCursor(wxCursor(wxCURSOR_HAND));
        Bind(wxEVT_PAINT, &Header::OnPaint, this);
        BindHover(this, hovered_, hover_, ticker_);
        Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent &) {
            if (onToggle_)
                onToggle_();
        });
        Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
            if (IsActivateKey(event) && onToggle_)
                onToggle_();
            else
                HandleTab(this, event);
        });
        RefreshOnFocus(this);
        SetInitialSize();
    }
    bool AcceptsFocus() const override { return true; }
    void SetOpen(bool open, bool animate) {
        if (animate)
            open_.Set(open ? 1.0 : 0.0, 220);
        else
            open_.Jump(open ? 1.0 : 0.0);
        ticker_.Kick();
    }

  protected:
    wxSize DoGetBestClientSize() const override {
        wxClientDC dc(const_cast<Header *>(this));
        dc.SetFont(GetFont());
        return wxSize(dc.GetTextExtent(GetLabel()).x + FromDIP(30), FromDIP(32));
    }

  private:
    void OnPaint(wxPaintEvent &) {
        wxAutoBufferedPaintDC dc(this);
        ClearToParent(dc, this);
        const wxSize size = GetClientSize();
        const double hover = hover_.Value();
        const wxColour colour = Mix(Theme::kTextMuted, Theme::kText, hover);
        {
            std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
            if (!gc)
                return;
            // Chevron pointing right when closed, down when open.
            const double cx = FromDIP(7);
            const double cy = size.y / 2.0;
            const double angle = open_.Value() * kPi / 2;
            const double arm = Dip(this, 3.5);
            gc->PushState();
            gc->Translate(cx, cy);
            gc->Rotate(angle);
            gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(colour).Width(Dip(this, 1.5)).Cap(wxCAP_ROUND).Join(wxJOIN_ROUND)));
            wxGraphicsPath path = gc->CreatePath();
            path.MoveToPoint(-arm / 2, -arm);
            path.AddLineToPoint(arm / 2, 0);
            path.AddLineToPoint(-arm / 2, arm);
            gc->StrokePath(path);
            gc->PopState();
            if (ShowsFocusRing(this))
                FillRounded(*gc, 0, FromDIP(2), size.x, size.y - FromDIP(4), FromDIP(6), wxNullColour,
                            Theme::kAccentHover, Dip(this, 1.5));
        }
        dc.SetFont(GetFont());
        const wxSize extent = dc.GetTextExtent(GetLabel());
        DrawTextAt(dc, GetLabel(), FromDIP(20), (size.y - extent.y) / 2, colour, GetFont());
    }

    std::function<void()> onToggle_;
    bool hovered_ = false;
    Tween hover_;
    Tween open_;
    Ticker ticker_;
};

Collapsible::Collapsible(wxWindow *parent, const wxString &title) : wxPanel(parent, wxID_ANY), timer_(this) {
    SetBackgroundColour(parent->GetBackgroundColour());
    auto *sizer = new wxBoxSizer(wxVERTICAL);
    if (!title.empty()) {
        header_ = new Header(this, title, [this] { SetExpanded(!expanded_); });
        sizer->Add(header_, 0, wxBOTTOM, FromDIP(4));
    }
    clip_ = new wxPanel(this);
    clip_->SetBackgroundColour(GetBackgroundColour());
    content_ = new wxPanel(clip_);
    content_->SetBackgroundColour(GetBackgroundColour());
    content_->Hide();
    clip_->SetMinSize(wxSize(-1, 0));
    sizer->Add(clip_, 0, wxEXPAND);
    SetSizer(sizer);

    clip_->Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        event.Skip();
        content_->SetSize(0, 0, event.GetSize().x, std::max(ContentHeight(), event.GetSize().y));
    });
    Bind(wxEVT_TIMER, [this](wxTimerEvent &) {
        SyncHeight();
        RelayoutAncestors(GetParent());
        if (!progress_.Running()) {
            timer_.Stop();
            if (!expanded_)
                content_->Hide();
        }
    });
}

int Collapsible::ContentHeight() const {
    wxSizer *sizer = content_->GetSizer();
    return sizer ? sizer->CalcMin().y : content_->GetBestSize().y;
}

void Collapsible::SyncHeight() {
    const int height = static_cast<int>(std::lround(ContentHeight() * progress_.Value()));
    if (clip_->GetMinSize().y != height) {
        clip_->SetMinSize(wxSize(-1, height));
        clip_->InvalidateBestSize();
    }
    if (content_->IsShown()) {
        const wxSize clip = clip_->GetClientSize();
        content_->SetSize(0, 0, clip.x, std::max(ContentHeight(), clip.y));
    }
}

void Collapsible::SetExpanded(bool expanded, bool animate) {
    expanded_ = expanded;
    if (header_)
        header_->SetOpen(expanded, animate);
    if (expanded) {
        content_->Show();
        content_->Layout();
    }
    if (animate) {
        progress_.Set(expanded ? 1.0 : 0.0, expanded ? 260 : 200);
        timer_.Start(15);
    } else {
        progress_.Jump(expanded ? 1.0 : 0.0);
        if (!expanded)
            content_->Hide();
        SyncHeight();
        RelayoutAncestors(GetParent());
    }
}

// LinkRow

LinkRow::LinkRow(wxWindow *parent, wchar_t icon, const wxString &label, const wxString &display, const wxString &url)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxWANTS_CHARS), icon_(icon),
      display_(display), url_(url), ticker_(this, [this] { return hover_.Running(); }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetFont(Theme::UiFont(10, wxFONTWEIGHT_MEDIUM));
    wxControl::SetLabel(label);
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetToolTip(url);
    Bind(wxEVT_PAINT, &LinkRow::OnPaint, this);
    BindHover(this, hovered_, hover_, ticker_);
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) { wxLaunchDefaultBrowser(url_); });
    Bind(wxEVT_KEY_DOWN, [this](wxKeyEvent &event) {
        if (IsActivateKey(event))
            wxLaunchDefaultBrowser(url_);
        else
            HandleTab(this, event);
    });
    RefreshOnFocus(this);
    SetInitialSize();
}

wxSize LinkRow::DoGetBestClientSize() const {
    return wxSize(FromDIP(280), FromDIP(48));
}

void LinkRow::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    ClearToParent(dc, this);
    const wxColour base = GetParent()->GetBackgroundColour();
    const wxSize size = GetClientSize();
    const double hover = hover_.Value();
    const int iconSize = FromDIP(32);
    const wxRect icon(FromDIP(8), (size.y - iconSize) / 2, iconSize, iconSize);
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        if (hover > 0.01)
            FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(9), Mix(base, Theme::kText, 0.05 * hover));
        FillRounded(*gc, icon.x, icon.y, icon.width, icon.height, FromDIP(8),
                    Mix(Mix(base, Theme::kText, 0.06), Theme::kAccent, 0.12 * hover));
        if (ShowsFocusRing(this))
            FillRounded(*gc, 0, 0, size.x, size.y, FromDIP(9), wxNullColour, Theme::kAccentHover, Dip(this, 1.5));
    }
    DrawGlyph(dc, icon_, icon, Mix(Theme::kTextMuted, Theme::kAccentHover, hover), 11);
    dc.SetFont(GetFont());
    const wxSize label = dc.GetTextExtent(GetLabel());
    DrawTextAt(dc, GetLabel(), icon.GetRight() + FromDIP(14), (size.y - label.y) / 2, Theme::kText, GetFont());

    const wxRect arrow(size.x - FromDIP(14) - FromDIP(14), 0, FromDIP(14), size.y);
    DrawGlyph(dc, Theme::Icon::kExternal, arrow, Mix(Theme::kTextFaint, Theme::kText, hover), 9);
    const wxFont small = Theme::UiFont(9.0);
    dc.SetFont(small);
    const int available = arrow.x - FromDIP(10) - (icon.GetRight() + FromDIP(14) + label.x + FromDIP(24));
    const wxString display = wxControl::Ellipsize(display_, dc, wxELLIPSIZE_END, std::max(available, FromDIP(20)));
    const wxSize extent = dc.GetTextExtent(display);
    DrawTextAt(dc, display, arrow.x - FromDIP(10) - extent.x, (size.y - extent.y) / 2,
               Mix(Theme::kTextMuted, Theme::kText, hover * 0.5), small);
}

// StatusStrip

StatusStrip::StatusStrip(wxWindow *parent, std::function<void()> onToggle)
    : wxControl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE), onToggle_(std::move(onToggle)),
      ticker_(this, [this] { return busy_ || open_.Running() || fresh_.Running(); }) {
    SetBackgroundStyle(wxBG_STYLE_PAINT);
    SetBackgroundColour(Theme::kWindow);
    SetCursor(wxCursor(wxCURSOR_HAND));
    SetToolTip("Show or hide the activity log");
    Bind(wxEVT_PAINT, &StatusStrip::OnPaint, this);
    Bind(wxEVT_ENTER_WINDOW, [this](wxMouseEvent &) {
        hovered_ = true;
        Refresh(false);
    });
    Bind(wxEVT_LEAVE_WINDOW, [this](wxMouseEvent &) {
        hovered_ = false;
        Refresh(false);
    });
    Bind(wxEVT_LEFT_UP, [this](wxMouseEvent &) {
        if (onToggle_)
            onToggle_();
    });
    SetInitialSize();
}

wxSize StatusStrip::DoGetBestClientSize() const {
    return wxSize(FromDIP(200), FromDIP(34));
}

void StatusStrip::SetMessage(UrkApp::LogLevel level, const wxString &text) {
    level_ = level;
    message_ = text;
    if (!busy_) {
        fresh_.Jump(0.0);
        fresh_.Set(1.0, 260);
    }
    ticker_.Kick();
}

void StatusStrip::SetBusy(const wxString &text) {
    busy_ = !text.empty();
    busyText_ = text;
    fresh_.Jump(0.0);
    fresh_.Set(1.0, 260);
    ticker_.Kick();
}

void StatusStrip::SetOpen(bool open) {
    open_.Set(open ? 1.0 : 0.0, 220);
    ticker_.Kick();
}

void StatusStrip::OnPaint(wxPaintEvent &) {
    wxAutoBufferedPaintDC dc(this);
    dc.SetBackground(wxBrush(Theme::kWindow));
    dc.Clear();
    const wxSize size = GetClientSize();
    const double fresh = fresh_.Value();
    wxColour dot = Theme::kTextFaint;
    switch (level_) {
        case UrkApp::LogLevel::Success:
            dot = Theme::kSuccess;
            break;
        case UrkApp::LogLevel::Warning:
            dot = Theme::kWarning;
            break;
        case UrkApp::LogLevel::Error:
            dot = Theme::kError;
            break;
        default:
            break;
    }
    const double cx = FromDIP(22);
    const double cy = size.y / 2.0;
    const int toggleWidth = FromDIP(120);
    {
        std::unique_ptr<wxGraphicsContext> gc = MakeGc(dc);
        if (!gc)
            return;
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(Theme::kBorder).Width(1)));
        gc->StrokeLine(0, 0.5, size.x, 0.5);
        if (busy_) {
            DrawSpinner(*gc, cx, cy, FromDIP(5), Dip(this, 1.6), Theme::kAccent);
        } else {
            gc->SetPen(*wxTRANSPARENT_PEN);
            gc->SetBrush(wxBrush(Mix(Theme::kWindow, dot, 0.25)));
            gc->DrawEllipse(cx - FromDIP(5), cy - FromDIP(5), FromDIP(10), FromDIP(10));
            gc->SetBrush(wxBrush(dot));
            gc->DrawEllipse(cx - Dip(this, 2.5), cy - Dip(this, 2.5), FromDIP(5), FromDIP(5));
        }
        // Chevron: up when the log is closed, down when it is open.
        const double chevronX = size.x - FromDIP(22);
        const double angle = -kPi / 2 + open_.Value() * kPi;
        const double arm = Dip(this, 3.5);
        gc->PushState();
        gc->Translate(chevronX, cy);
        gc->Rotate(angle);
        gc->SetPen(gc->CreatePen(wxGraphicsPenInfo(hovered_ ? Theme::kText : Theme::kTextMuted)
                       .Width(Dip(this, 1.4))
                       .Cap(wxCAP_ROUND)
                       .Join(wxJOIN_ROUND)));
        wxGraphicsPath path = gc->CreatePath();
        path.MoveToPoint(-arm / 2, -arm);
        path.AddLineToPoint(arm / 2, 0);
        path.AddLineToPoint(-arm / 2, arm);
        gc->StrokePath(path);
        gc->PopState();
    }
    const wxFont font = Theme::UiFont(9.0);
    dc.SetFont(font);
    const wxString label = "Activity";
    const wxSize labelExtent = dc.GetTextExtent(label);
    DrawTextAt(dc, label, size.x - FromDIP(36) - labelExtent.x, (size.y - labelExtent.y) / 2,
               hovered_ ? Theme::kText : Theme::kTextMuted, font);
    const wxString text = busy_ ? busyText_ : message_;
    const int textX = FromDIP(36);
    const int available = size.x - toggleWidth - textX - FromDIP(12);
    const wxString shown = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, std::max(available, FromDIP(40)));
    const wxSize extent = dc.GetTextExtent(shown);
    const wxColour colour = busy_ ? Theme::kText : level_ == UrkApp::LogLevel::Error ? Theme::kError : Theme::kTextMuted;
    DrawTextAt(dc, shown, textX, (size.y - extent.y) / 2, Mix(Theme::kWindow, colour, fresh), font);
}

// LogView

LogView::LogView(wxWindow *parent)
    : wxListCtrl(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize,
                 wxLC_REPORT | wxLC_VIRTUAL | wxLC_NO_HEADER | wxBORDER_NONE) {
    const wxColour background = parent->GetBackgroundColour();
    SetBackgroundColour(background);
    SetForegroundColour(Theme::kText);
    SetFont(Theme::MonoFont(8.75));
    AppendColumn("Time", wxLIST_FORMAT_LEFT, FromDIP(92));
    AppendColumn("Message", wxLIST_FORMAT_LEFT, FromDIP(600));
    const wxColour levelColours[5] = {Theme::kTextFaint, Theme::kTextMuted, Theme::kSuccess, Theme::kWarning,
                                      Theme::kError};
    for (int index = 0; index < 5; ++index) {
        attributes_[index].SetTextColour(levelColours[index]);
        attributes_[index].SetBackgroundColour(background);
    }
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        event.Skip();
        CallAfter([this] { FitMessageColumn(); });
    });
    Bind(wxEVT_KEY_DOWN, &LogView::OnKey, this);
    Bind(wxEVT_CONTEXT_MENU, &LogView::OnContextMenu, this);
}

void LogView::FitMessageColumn() {
    const int available = GetClientSize().x - GetColumnWidth(0) - FromDIP(4);
    SetColumnWidth(1, std::max(available, FromDIP(240)));
}

void LogView::Append(const UrkApp::LogEntry &entry) {
    const long count = GetItemCount();
    const bool follow = count == 0 || GetTopItem() + GetCountPerPage() >= count - 1;
    entries_.push_back(entry);
    if (entries_.size() > kMaxLogEntries)
        entries_.pop_front();
    SetItemCount(static_cast<long>(entries_.size()));
    if (follow && !entries_.empty())
        EnsureVisible(static_cast<long>(entries_.size()) - 1);
    Refresh();
}

void LogView::Clear() {
    entries_.clear();
    SetItemCount(0);
    Refresh();
}

wxString LogView::Text(bool selectedOnly) const {
    wxString text;
    for (long index = 0; index < static_cast<long>(entries_.size()); ++index) {
        if (selectedOnly && GetItemState(index, wxLIST_STATE_SELECTED) == 0)
            continue;
        text << wxString::FromUTF8(UrkApp::FormatLogEntry(entries_[static_cast<size_t>(index)])) << "\r\n";
    }
    return text;
}

wxString LogView::OnGetItemText(long item, long column) const {
    if (item < 0 || item >= static_cast<long>(entries_.size()))
        return {};
    const UrkApp::LogEntry &entry = entries_[static_cast<size_t>(item)];
    if (column == 0)
        return UrkApp::FormatLogTime(entry.time);
    return wxString::FromUTF8(entry.message);
}

wxItemAttr *LogView::OnGetItemAttr(long item) const {
    if (item < 0 || item >= static_cast<long>(entries_.size()))
        return nullptr;
    return &attributes_[static_cast<int>(entries_[static_cast<size_t>(item)].level)];
}

void LogView::OnKey(wxKeyEvent &event) {
    if (event.ControlDown() && event.GetKeyCode() == 'C') {
        if (wxTheClipboard->Open()) {
            wxTheClipboard->SetData(new wxTextDataObject(Text(GetSelectedItemCount() > 0)));
            wxTheClipboard->Close();
        }
        return;
    }
    if (event.ControlDown() && event.GetKeyCode() == 'A') {
        for (long index = 0; index < GetItemCount(); ++index)
            SetItemState(index, wxLIST_STATE_SELECTED, wxLIST_STATE_SELECTED);
        return;
    }
    event.Skip();
}

void LogView::OnContextMenu(wxContextMenuEvent &) {
    wxMenu menu;
    menu.Append(1, "Copy selected\tCtrl+C");
    menu.Append(2, "Copy all");
    menu.AppendSeparator();
    menu.Append(3, "Clear");
    menu.Enable(1, GetSelectedItemCount() > 0);
    const int choice = GetPopupMenuSelectionFromUser(menu);
    if (choice == 3) {
        Clear();
        return;
    }
    if ((choice == 1 || choice == 2) && wxTheClipboard->Open()) {
        wxTheClipboard->SetData(new wxTextDataObject(Text(choice == 1)));
        wxTheClipboard->Close();
    }
}

// WrapText

WrapText::WrapText(wxWindow *parent, const wxString &text, bool muted, double points)
    : wxStaticText(parent, wxID_ANY, text, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE), raw_(text) {
    SetFont(Theme::UiFont(points));
    SetForegroundColour(muted ? Theme::kTextMuted : Theme::kText);
    // The sizer decides the width; the text only reports the height it needs there.
    SetMinSize(wxSize(FromDIP(60), -1));
    Bind(wxEVT_SIZE, [this](wxSizeEvent &event) {
        event.Skip();
        if (event.GetSize().x != wrappedWidth_)
            Rewrap(event.GetSize().x);
    });
}

void WrapText::SetText(const wxString &text) {
    raw_ = text;
    const int width = wrappedWidth_;
    wrappedWidth_ = -1;
    Rewrap(width > 0 ? width : GetSize().x);
}

void WrapText::Rewrap(int width) {
    if (width <= 0)
        return;
    wrappedWidth_ = width;
    wxStaticText::SetLabel(raw_);
    Wrap(width);
    // Measured directly: the cached best size of a static text lags behind Wrap().
    wxClientDC dc(this);
    dc.SetFont(GetFont());
    wxCoord textWidth = 0;
    wxCoord textHeight = 0;
    dc.GetMultiLineTextExtent(GetLabel(), &textWidth, &textHeight);
    InvalidateBestSize();
    SetMinSize(wxSize(FromDIP(60), textHeight));
    CallAfter([this] { RelayoutAncestors(GetParent()); });
}

// Helpers

wxStaticText *MakeLabel(wxWindow *parent, const wxString &text, bool muted, double points, wxFontWeight weight) {
    auto *label = new wxStaticText(parent, wxID_ANY, text);
    label->SetFont(Theme::UiFont(points, weight));
    label->SetForegroundColour(muted ? Theme::kTextMuted : Theme::kText);
    return label;
}

wxStaticText *MakeSectionLabel(wxWindow *parent, const wxString &text) {
    return MakeLabel(parent, text, true, 9.0, wxFONTWEIGHT_SEMIBOLD);
}

} // namespace UrkGui
