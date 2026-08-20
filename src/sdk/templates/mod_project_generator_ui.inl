// Internal UI, localization, and render-hook templates. Included by mod_project_generator_common.cpp.

std::string ThemeModule() {
    return R"URK(#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <imgui.h>

namespace ModUI::Theme {

struct Palette {
    ImVec4 bg_base{0.055f, 0.055f, 0.055f, 0.98f};
    ImVec4 bg_overlay{0.075f, 0.075f, 0.075f, 0.98f};
    ImVec4 bg_elevated{0.095f, 0.095f, 0.095f, 0.98f};

    ImVec4 surface{0.105f, 0.105f, 0.105f, 0.96f};
    ImVec4 surface_raised{0.135f, 0.135f, 0.135f, 0.98f};
    ImVec4 surface_hover{0.180f, 0.180f, 0.180f, 0.98f};
    ImVec4 surface_active{0.240f, 0.240f, 0.240f, 1.00f};
    ImVec4 surface_glass{0.025f, 0.025f, 0.025f, 0.84f};

    ImVec4 border_subtle{0.800f, 0.800f, 0.800f, 0.18f};
    ImVec4 border_strong{0.850f, 0.850f, 0.850f, 0.36f};
    ImVec4 border_focus{0.950f, 0.950f, 0.950f, 0.78f};

    ImVec4 text_primary{0.940f, 0.940f, 0.940f, 1.00f};
    ImVec4 text_secondary{0.720f, 0.720f, 0.720f, 1.00f};
    ImVec4 text_muted{0.500f, 0.500f, 0.500f, 1.00f};
    ImVec4 text_disabled{0.390f, 0.390f, 0.390f, 0.72f};

    ImVec4 accent_a{0.900f, 0.900f, 0.900f, 1.00f};
    ImVec4 accent_b{0.720f, 0.720f, 0.720f, 1.00f};
    ImVec4 accent_c{0.980f, 0.980f, 0.980f, 1.00f};
    ImVec4 accent_warm{0.820f, 0.820f, 0.820f, 1.00f};
    ImVec4 accent_soft{0.900f, 0.900f, 0.900f, 0.14f};
    ImVec4 accent_line{0.900f, 0.900f, 0.900f, 0.62f};
    ImVec4 toggle_on{0.780f, 0.780f, 0.780f, 1.00f};

    ImVec4 success{0.760f, 0.760f, 0.760f, 1.00f};
    ImVec4 warning{0.700f, 0.700f, 0.700f, 1.00f};
    ImVec4 danger{0.840f, 0.840f, 0.840f, 1.00f};
    ImVec4 info{0.820f, 0.820f, 0.820f, 1.00f};

    ImVec4 shadow{0.00f, 0.00f, 0.00f, 0.32f};
};

struct Radius {
    float sm = 3.0f;
    float md = 4.0f;
    float lg = 5.0f;
    float xl = 6.0f;
    float pill = 999.0f;
};

struct Spacing {
    ImVec2 window{16.0f, 14.0f};
    ImVec2 frame{10.0f, 6.0f};
    ImVec2 item{8.0f, 7.0f};
    ImVec2 card{18.0f, 16.0f};
    ImVec2 content{22.0f, 20.0f};
    float header_height = 82.0f;
    float footer_height = 0.0f;
    float sidebar_width = 0.0f;
    float section_gap = 14.0f;
    float hero_height = 64.0f;
    float widget_height = 36.0f;
    float label_column_width = 124.0f;
    float tab_gap = 4.0f;
};

inline Palette &palette() {
    static Palette value{};
    return value;
}

inline Radius &radius() {
    static Radius value{};
    return value;
}

inline Spacing &spacing() {
    static Spacing value{};
    return value;
}

inline ImVec4 with_alpha(ImVec4 c, float alpha) {
    c.w = alpha;
    return c;
}

inline ImVec4 mix(const ImVec4 &a, const ImVec4 &b, float t) {
    return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
}

inline const ImWchar *glyph_ranges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1
        0x0100, 0x017F, // Latin Extended-A, including Turkish glyphs
        0x0400, 0x052F, // Cyrillic + Cyrillic Supplement
        0x2000, 0x206F, // General punctuation
        0x20A0, 0x20CF, // Currency symbols
        0,
    };
    return ranges;
}

inline float dpi_scale();
inline const char *&loaded_font_name() {
    static const char *value = "default";
    return value;
}

inline bool &using_ttf_font() {
    static bool value = false;
    return value;
}

inline const char *&cjk_font_support() {
    static const char *value = "unavailable";
    return value;
}

inline ImFont *&heading_font() {
    static ImFont *value = nullptr;
    return value;
}

inline ImFont *load_windows_font(ImGuiIO &io, const char *file_name, float size_px,
                                 const ImWchar *ranges = glyph_ranges(), bool merge_mode = false) {
    char windows_dir[MAX_PATH]{};
    const UINT windows_dir_len = GetWindowsDirectoryA(windows_dir, MAX_PATH);
    if (!io.Fonts || windows_dir_len == 0 || windows_dir_len >= MAX_PATH)
        return nullptr;

    char font_path[MAX_PATH]{};
    const int written = std::snprintf(font_path, sizeof(font_path), "%s\\Fonts\\%s", windows_dir, file_name);
    if (written <= 0 || written >= static_cast<int>(sizeof(font_path)))
        return nullptr;
    const DWORD attributes = GetFileAttributesA(font_path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return nullptr;

    ImFontConfig config{};
    config.OversampleH = 2;
    config.OversampleV = 1;
    config.PixelSnapH = true;
    config.RasterizerMultiply = 1.0f;
    config.GlyphOffset = ImVec2(0.0f, 0.0f);
    config.MergeMode = merge_mode;
    std::snprintf(config.Name, IM_ARRAYSIZE(config.Name), "%s %.1fpx", file_name, size_px);
    return io.Fonts->AddFontFromFileTTF(font_path, size_px, &config, ranges);
}

inline bool merge_first_windows_font(ImGuiIO &io, const char *const *file_names, size_t file_name_count, float size_px,
                                     const ImWchar *ranges) {
    for (size_t index = 0; index < file_name_count; ++index) {
        if (load_windows_font(io, file_names[index], size_px, ranges, true))
            return true;
    }
    return false;
}

inline void merge_cjk_fallbacks(ImGuiIO &io, float size_px) {
    static const char *const japanese_fonts[] = {"YuGothM.ttc", "meiryo.ttc", "msgothic.ttc"};
    static const char *const chinese_fonts[] = {"msyh.ttc", "simsun.ttc", "msjh.ttc"};
    const bool japanese_loaded = merge_first_windows_font(io, japanese_fonts, IM_ARRAYSIZE(japanese_fonts), size_px,
                                                          io.Fonts->GetGlyphRangesJapanese());
    const bool chinese_loaded = merge_first_windows_font(io, chinese_fonts, IM_ARRAYSIZE(chinese_fonts), size_px,
                                                         io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    cjk_font_support() = japanese_loaded && chinese_loaded ? "Japanese and Chinese"
                         : japanese_loaded                 ? "Japanese only"
                         : chinese_loaded                  ? "Chinese only"
                                                           : "unavailable";
}

inline float font_size_px() {
    float size = std::floor((18.0f * dpi_scale()) + 0.5f);
    if (size < 18.0f)
        size = 18.0f;
    if (size > 28.0f)
        size = 28.0f;
    return size;
}

inline void install_default_font() {
    ImGuiIO &io = ImGui::GetIO();
    if (io.Fonts && io.Fonts->Fonts.Size > 0) {
        if (!io.FontDefault)
            io.FontDefault = io.Fonts->Fonts[0];
        heading_font() = io.FontDefault;
        return;
    }
    if (io.Fonts)
        io.Fonts->TexGlyphPadding = 2;
    loaded_font_name() = "default";
    using_ttf_font() = false;
    const float size_px = font_size_px();
    ImFont *regular = load_windows_font(io, "segoeui.ttf", size_px);
    if (regular) {
        merge_cjk_fallbacks(io, size_px);
        io.FontDefault = regular;
        heading_font() = load_windows_font(io, "seguisb.ttf", size_px + 2.0f);
        if (heading_font())
            merge_cjk_fallbacks(io, size_px + 2.0f);
        else
            heading_font() = regular;
        loaded_font_name() = "segoeui.ttf";
        using_ttf_font() = true;
    } else {
        ImFontConfig config{};
        config.SizePixels = size_px;
        config.OversampleH = 2;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        config.RasterizerMultiply = 1.0f;
        std::snprintf(config.Name, IM_ARRAYSIZE(config.Name), "ImGui default %.1fpx", size_px);
        io.FontDefault = io.Fonts->AddFontDefault(&config);
        heading_font() = io.FontDefault;
    }
    io.FontGlobalScale = 1.0f;
}

inline float dpi_scale() {
    HWND hwnd = GetActiveWindow();
    if (!hwnd)
        hwnd = GetForegroundWindow();

    UINT dpi = 96;
    if (hwnd) {
        dpi = GetDpiForWindow(hwnd);
        if (dpi == 0)
            dpi = 96;
    }

    float scale = static_cast<float>(dpi) / 96.0f;
    if (scale < 1.0f)
        scale = 1.0f;
    if (scale > 2.0f)
        scale = 2.0f;
    return scale;
}

inline float pulse(float speed = 1.0f, float min_value = 0.0f, float max_value = 1.0f) {
    const double wave = (std::sin(ImGui::GetTime() * speed) + 1.0) * 0.5;
    return min_value + static_cast<float>(wave) * (max_value - min_value);
}

inline void gradient_rect(ImDrawList *dl, const ImVec2 &p_min, const ImVec2 &p_max, ImU32 col_left, ImU32 col_right,
                          float rounding = 0.0f) {
    if (rounding <= 0.0f) {
        dl->AddRectFilledMultiColor(p_min, p_max, col_left, col_right, col_right, col_left);
        return;
    }
    const float width = p_max.x - p_min.x;
    const float cap = rounding;
    dl->AddRectFilled(p_min, p_max, col_left, rounding);
    if (width <= cap * 2.0f)
        return;
    dl->AddRectFilled(ImVec2(p_max.x - cap * 2.0f, p_min.y), p_max, col_right, rounding, ImDrawFlags_RoundCornersRight);
    dl->AddRectFilledMultiColor(ImVec2(p_min.x + cap, p_min.y), ImVec2(p_max.x - cap, p_max.y), col_left, col_right,
                                col_right, col_left);
}

inline ImU32 lerp_col(const ImVec4 &a, const ImVec4 &b, float t) {
    return ImGui::GetColorU32(mix(a, b, t));
}

inline void glow_circle(ImDrawList *dl, const ImVec2 &center, float radius, const ImVec4 &color,
                        float alpha_scale = 1.0f) {
    for (int ring = 3; ring >= 1; --ring) {
        const float expand = radius * (0.26f * static_cast<float>(ring));
        const float alpha = (0.07f / static_cast<float>(ring)) * alpha_scale;
        dl->AddCircleFilled(center, radius + expand, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, alpha)), 32);
    }
    dl->AddCircleFilled(center, radius, ImGui::GetColorU32(ImVec4(color.x, color.y, color.z, 0.16f * alpha_scale)), 32);
}

inline void apply() {
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigWindowsMoveFromTitleBarOnly = false;
    install_default_font();

    ImGuiStyle &style = ImGui::GetStyle();
    const Palette &p = palette();
    const Radius &r = radius();
    const Spacing &sp = spacing();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.5f;
    style.WindowPadding = sp.window;
    style.FramePadding = sp.frame;
    style.ItemSpacing = sp.item;
    style.ItemInnerSpacing = ImVec2(9.0f, 7.0f);
    style.IndentSpacing = 20.0f;
    style.ScrollbarSize = 8.0f;
    style.GrabMinSize = 12.0f;
    style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    style.AntiAliasedFill = true;
    style.AntiAliasedLines = true;
    style.AntiAliasedLinesUseTex = true;

    style.WindowRounding = r.xl;
    style.ChildRounding = r.lg;
    style.FrameRounding = r.md;
    style.PopupRounding = r.md;
    style.GrabRounding = r.pill;
    style.ScrollbarRounding = r.pill;
    style.TabRounding = r.md;

    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    ImVec4 *colors = style.Colors;
    colors[ImGuiCol_Text] = p.text_primary;
    colors[ImGuiCol_TextDisabled] = p.text_disabled;
    colors[ImGuiCol_WindowBg] = p.bg_base;
    colors[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_PopupBg] = p.bg_elevated;
    colors[ImGuiCol_Border] = p.border_subtle;
    colors[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);

    colors[ImGuiCol_FrameBg] = p.surface;
    colors[ImGuiCol_FrameBgHovered] = p.surface_hover;
    colors[ImGuiCol_FrameBgActive] = p.surface_active;

    colors[ImGuiCol_TitleBg] = p.bg_elevated;
    colors[ImGuiCol_TitleBgActive] = p.bg_elevated;
    colors[ImGuiCol_TitleBgCollapsed] = p.bg_base;
    colors[ImGuiCol_MenuBarBg] = p.bg_elevated;

    colors[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ScrollbarGrab] = p.surface_hover;
    colors[ImGuiCol_ScrollbarGrabHovered] = p.surface_active;
    colors[ImGuiCol_ScrollbarGrabActive] = p.accent_a;

    colors[ImGuiCol_CheckMark] = p.accent_a;
    colors[ImGuiCol_SliderGrab] = p.accent_a;
    colors[ImGuiCol_SliderGrabActive] = p.accent_b;

    colors[ImGuiCol_Button] = p.surface;
    colors[ImGuiCol_ButtonHovered] = p.surface_hover;
    colors[ImGuiCol_ButtonActive] = p.surface_active;

    colors[ImGuiCol_Header] = with_alpha(p.accent_a, 0.14f);
    colors[ImGuiCol_HeaderHovered] = with_alpha(p.accent_a, 0.22f);
    colors[ImGuiCol_HeaderActive] = with_alpha(p.accent_b, 0.30f);

    colors[ImGuiCol_Separator] = p.border_subtle;
    colors[ImGuiCol_SeparatorHovered] = p.border_strong;
    colors[ImGuiCol_SeparatorActive] = p.accent_a;

    colors[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_ResizeGripHovered] = p.accent_soft;
    colors[ImGuiCol_ResizeGripActive] = p.accent_a;

    colors[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    colors[ImGuiCol_TabHovered] = p.surface_hover;
    colors[ImGuiCol_TabActive] = p.surface_active;

    colors[ImGuiCol_NavHighlight] = p.accent_a;
    colors[ImGuiCol_PlotLines] = p.accent_b;
    colors[ImGuiCol_PlotHistogram] = p.accent_c;
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.55f);
}
} // namespace ModUI::Theme
)URK";
}

std::string WidgetsModule() {
    return R"URK(#pragma once
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <unordered_map>

#include "localization.h"
#include "theme.h"

namespace ModUI::Widgets {
struct AnimState {
    bool initialized = false;
    float hover = 0.0f;
    float active = 0.0f;
    float checked = 0.0f;
    float value = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

inline float clamp01(float value) {
    if (value < 0.0f)
        return 0.0f;
    if (value > 1.0f)
        return 1.0f;
    return value;
}

inline float ease_out_cubic(float value) {
    const float t = clamp01(value);
    const float inv = 1.0f - t;
    return 1.0f - (inv * inv * inv);
}

inline float animation_step(float speed) {
    float dt = ImGui::GetIO().DeltaTime;
    if (dt <= 0.0f)
        dt = 1.0f / 60.0f;
    if (dt > 1.0f / 15.0f)
        dt = 1.0f / 15.0f;
    return 1.0f - std::exp(-speed * dt);
}

inline float animate_to(float current, float target, float speed = 14.0f) {
    return current + (target - current) * animation_step(speed);
}

inline AnimState &anim_state(ImGuiID id) {
    static std::unordered_map<ImGuiID, AnimState> states;
    return states[id];
}

inline ImVec4 mix3(const ImVec4 &base, const ImVec4 &hover, const ImVec4 &active, float hover_t, float active_t) {
    return Theme::mix(Theme::mix(base, hover, clamp01(hover_t)), active, clamp01(active_t));
}

inline float clamp_label_column_width(float available_width) {
    const float desired = Theme::spacing().label_column_width;
    const float max_width = available_width * 0.38f;
    if (max_width < 72.0f)
        return 72.0f;
    return desired < max_width ? desired : max_width;
}

inline bool &labeled_field_table_open() {
    static bool open = false;
    return open;
}

inline void begin_labeled_field(const char *label, const char *id = nullptr) {
    const Theme::Palette &p = Theme::palette();
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float label_width = clamp_label_column_width(available_width);

    ImGui::PushID(id && id[0] ? id : (label ? label : "field"));
    bool &table_open = labeled_field_table_open();
    IM_ASSERT(!table_open && "begin_labeled_field() calls cannot be nested");
    table_open = ImGui::BeginTable("##field", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX);
    if (table_open) {
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, label_width);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, available_width - label_width);
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, p.text_secondary);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label && label[0] ? label : "");
        ImGui::PopStyleColor();
        ImGui::TableNextColumn();
    }
}

inline void end_labeled_field() {
    bool &table_open = labeled_field_table_open();
    if (table_open)
        ImGui::EndTable();
    table_open = false;
    ImGui::PopID();
}

inline bool checkbox(const char *label, bool *value) {
    if (!value)
        return false;
    const Theme::Palette &p = Theme::palette();
    const char *text = label && label[0] ? label : "Option";
    bool changed = false;
    begin_labeled_field(text);
    ImGui::PushID(text);
    const ImGuiID id = ImGui::GetID("##checkbox");
    AnimState &anim = anim_state(id);
    if (!anim.initialized) {
        anim.checked = *value ? 1.0f : 0.0f;
        anim.initialized = true;
    }

    const float square = ImGui::GetFontSize() + 6.0f;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##checkbox", ImVec2(square, square));
    if (ImGui::IsItemClicked()) {
        *value = !*value;
        changed = true;
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
    anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 22.0f);
    anim.checked = animate_to(anim.checked, *value ? 1.0f : 0.0f, 18.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + square, pos.y + square);
    const ImVec4 frame_col = mix3(p.surface, p.surface_hover, p.surface_active, anim.hover, anim.active);
    dl->AddRectFilled(pos, end, ImGui::GetColorU32(frame_col), Theme::radius().sm);

    const float checked_t = ease_out_cubic(anim.checked);
    if (checked_t > 0.001f) {
        const float inset = 3.0f + ((1.0f - checked_t) * 5.0f);
        dl->AddRectFilled(ImVec2(pos.x + inset, pos.y + inset), ImVec2(end.x - inset, end.y - inset),
                          ImGui::GetColorU32(Theme::with_alpha(p.accent_a, 0.95f * checked_t)), Theme::radius().sm);

        const float mark_alpha = clamp01((checked_t - 0.28f) / 0.72f);
        const ImU32 mark_col = ImGui::GetColorU32(Theme::with_alpha(p.text_primary, mark_alpha));
        const float thickness = 2.0f;
        const ImVec2 a(pos.x + square * 0.28f, pos.y + square * 0.53f);
        const ImVec2 b(pos.x + square * 0.43f, pos.y + square * 0.68f);
        const ImVec2 c(pos.x + square * 0.73f, pos.y + square * 0.34f);
        dl->AddLine(a, b, mark_col, thickness);
        dl->AddLine(b, c, mark_col, thickness);
    }
    ImGui::PopID();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
    ImGui::TextUnformatted(ModUI::Localization::translate(*value ? "widget.enabled" : "widget.disabled"));
    ImGui::PopStyleColor();
    end_labeled_field();
    return changed;
}

inline bool button(const char *label, const ImVec2 &size = ImVec2(0.0f, 0.0f), bool primary = false) {
    const char *text = label && label[0] ? label : "Button";
    const Theme::Palette &p = Theme::palette();

    ImGui::PushID(text);
    const ImVec2 label_size = ImGui::CalcTextSize(text);
    const ImVec2 padding(14.0f, 9.0f);
    ImVec2 resolved_size(size.x > 0.0f ? size.x : label_size.x + padding.x * 2.0f,
                         size.y > 0.0f ? size.y : label_size.y + padding.y * 2.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiID id = ImGui::GetID("##button");
    AnimState &anim = anim_state(id);
    if (!anim.initialized)
        anim.initialized = true;

    const bool pressed = ImGui::InvisibleButton("##button", resolved_size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
    anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 24.0f);

    const ImVec4 base_col = primary ? p.accent_a : p.surface_raised;
    const ImVec4 hover_col = primary ? p.accent_b : p.surface_hover;
    const ImVec4 active_col = primary ? Theme::with_alpha(p.accent_b, 0.85f) : p.surface_active;
    const ImVec4 fill_col = mix3(base_col, hover_col, active_col, anim.hover, anim.active);
    const float press_offset = ease_out_cubic(anim.active) * 1.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(pos.x, pos.y + press_offset),
                      ImVec2(pos.x + resolved_size.x, pos.y + resolved_size.y + press_offset),
                      ImGui::GetColorU32(fill_col), Theme::radius().md);
    if (!primary) {
        dl->AddRect(ImVec2(pos.x, pos.y + press_offset),
                    ImVec2(pos.x + resolved_size.x, pos.y + resolved_size.y + press_offset),
                    ImGui::GetColorU32(p.border_subtle), Theme::radius().md);
    }

    const ImVec4 text_col = primary ? p.bg_base : p.text_primary;
    dl->AddText(ImVec2(pos.x + (resolved_size.x - label_size.x) * 0.5f,
                       pos.y + (resolved_size.y - label_size.y) * 0.5f + press_offset),
                ImGui::GetColorU32(text_col), text);
    ImGui::PopID();
    return pressed;
}

inline bool slider_float(const char *label, float *value, float min_value, float max_value,
                         const char *format = "%.2f") {
    if (!value)
        return false;
    const char *text = label && label[0] ? label : "Value";
    const Theme::Palette &p = Theme::palette();
    begin_labeled_field(text);
    ImGui::PushID(text);
    const ImGuiID id = ImGui::GetID("##slider");
    AnimState &anim = anim_state(id);
    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    bool changed = false;

    float normalized = 0.0f;
    if (max_value > min_value)
        normalized = clamp01((*value - min_value) / (max_value - min_value));
    if (!anim.initialized) {
        anim.value = normalized;
        anim.initialized = true;
    }

    ImGui::InvisibleButton("##slider", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (hovered || held)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (held && width > 0.0f && max_value > min_value) {
        const float next_normalized = clamp01((ImGui::GetIO().MousePos.x - pos.x) / width);
        const float next_value = min_value + (max_value - min_value) * next_normalized;
        if (next_value != *value) {
            *value = next_value;
            normalized = next_normalized;
            changed = true;
        }
    }

    anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
    anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 22.0f);
    anim.value = animate_to(anim.value, normalized, held ? 28.0f : 14.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec4 frame_col = mix3(p.surface_raised, p.surface_hover, p.surface_active, anim.hover, anim.active);
    const ImVec2 end(pos.x + width, pos.y + height);
    const float track_h = 6.0f;
    const ImVec2 track_min(pos.x, pos.y + (height - track_h) * 0.5f);
    const ImVec2 track_max(end.x, track_min.y + track_h);
    dl->AddRectFilled(track_min, track_max, ImGui::GetColorU32(frame_col), Theme::radius().pill);

    const float fill_w = width * clamp01(anim.value);
    if (fill_w > 0.5f) {
        dl->AddRectFilled(track_min, ImVec2(pos.x + fill_w, track_max.y),
                          ImGui::GetColorU32(Theme::mix(p.accent_a, p.accent_b, anim.active)), Theme::radius().pill);
    }

    const float grab_radius = 6.0f + ease_out_cubic(anim.hover + anim.active) * 1.5f;
    const ImVec2 grab(pos.x + fill_w, pos.y + height * 0.5f);
    dl->AddCircleFilled(grab, grab_radius + 2.0f, ImGui::GetColorU32(p.bg_elevated));
    dl->AddCircleFilled(grab, grab_radius, ImGui::GetColorU32(Theme::mix(p.accent_a, p.accent_b, anim.active)));

    char value_text[64]{};
    std::snprintf(value_text, sizeof(value_text), format && format[0] ? format : "%.2f", *value);
    const ImVec2 value_size = ImGui::CalcTextSize(value_text);
    dl->AddText(ImVec2(end.x - value_size.x - 10.0f, pos.y + (height - value_size.y) * 0.5f),
                ImGui::GetColorU32(p.text_primary), value_text);
    ImGui::PopID();
    end_labeled_field();
    return changed;
}

inline bool combo(const char *label, int *current, const char *const items[], int item_count,
                  const char *id = nullptr) {
    if (!current || !items || item_count <= 0)
        return false;
    const char *text = label && label[0] ? label : "Mode";
    const Theme::Palette &p = Theme::palette();
    if (*current < 0)
        *current = 0;
    if (*current >= item_count)
        *current = item_count - 1;
    const char *preview = items[*current] && items[*current][0] ? items[*current] : " ";
    begin_labeled_field(text, id);
    ImGui::PushID("##combo");
    const ImGuiID field_id = ImGui::GetID("##combo_field");
    AnimState &anim = anim_state(field_id);
    if (!anim.initialized)
        anim.initialized = true;

    const float width = ImGui::GetContentRegionAvail().x;
    const float height = ImGui::GetFrameHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 end(pos.x + width, pos.y + height);
    const bool popup_open = ImGui::IsPopupOpen("##combo_popup");

    ImGui::InvisibleButton("##combo_field", ImVec2(width, height));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        ImGui::OpenPopup("##combo_popup");
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
    anim.active = animate_to(anim.active, (held || popup_open) ? 1.0f : 0.0f, 22.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, end,
                      ImGui::GetColorU32(mix3(p.surface, p.surface_hover, p.surface_hover, anim.hover, anim.active)),
                      Theme::radius().md);
    dl->AddRect(pos, end, ImGui::GetColorU32(Theme::mix(p.border_subtle, p.border_focus, anim.active)),
                Theme::radius().md);
    dl->AddText(ImVec2(pos.x + 12.0f, pos.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                ImGui::GetColorU32(p.text_primary), preview);

    const float arrow_w = 10.0f;
    const float arrow_h = 6.0f;
    const ImVec2 arrow_center(end.x - 16.0f, pos.y + height * 0.5f + 1.0f);
    const ImU32 arrow_col = ImGui::GetColorU32(p.text_primary);
    dl->AddTriangleFilled(ImVec2(arrow_center.x - arrow_w * 0.5f, arrow_center.y - arrow_h * 0.5f),
                          ImVec2(arrow_center.x + arrow_w * 0.5f, arrow_center.y - arrow_h * 0.5f),
                          ImVec2(arrow_center.x, arrow_center.y + arrow_h * 0.5f), arrow_col);

    ImGui::SetNextWindowPos(ImVec2(pos.x, end.y + 4.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, p.bg_elevated);
    ImGui::PushStyleColor(ImGuiCol_Header, Theme::with_alpha(p.accent_a, 0.20f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, Theme::with_alpha(p.accent_a, 0.30f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, Theme::with_alpha(p.accent_a, 0.38f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, Theme::radius().md);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    if (ImGui::BeginPopup("##combo_popup")) {
        for (int index = 0; index < item_count; ++index) {
            const bool selected = index == *current;
            const char *item = items[index] && items[index][0] ? items[index] : " ";
            if (ImGui::Selectable(item, selected)) {
                *current = index;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(4);
    ImGui::PopID();
    end_labeled_field();
    return changed;
}

inline bool toggle(const char *label, bool *value) {
    if (!value)
        return false;
    const char *text = label && label[0] ? label : "toggle";
    ImGui::PushID(text);
    const Theme::Palette &p = Theme::palette();
    const float row_height = 30.0f;
    const float track_w = 42.0f;
    const float track_h = 22.0f;
    const float row_width = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiID id = ImGui::GetID("##toggle_row");
    AnimState &anim = anim_state(id);
    if (!anim.initialized) {
        anim.checked = *value ? 1.0f : 0.0f;
        anim.initialized = true;
    }

    ImGui::InvisibleButton("##toggle_row", ImVec2(row_width, row_height));
    bool changed = false;
    if (ImGui::IsItemClicked()) {
        *value = !*value;
        changed = true;
    }
    const bool hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
    anim.active = animate_to(anim.active, held ? 1.0f : 0.0f, 22.0f);
    anim.checked = animate_to(anim.checked, *value ? 1.0f : 0.0f, 18.0f);
    ImDrawList *dl = ImGui::GetWindowDrawList();

    const ImVec2 track_pos(pos.x, pos.y + (row_height - track_h) * 0.5f);
    const ImVec2 track_end(track_pos.x + track_w, track_pos.y + track_h);
    const float rh = track_h * 0.5f;
    const ImVec4 off_col = Theme::mix(p.surface_raised, p.surface_hover, anim.hover);
    const ImVec4 track_col = Theme::mix(off_col, p.toggle_on, ease_out_cubic(anim.checked));
    dl->AddRectFilled(track_pos, track_end, ImGui::GetColorU32(track_col), rh);
    const float knob_r = rh - 3.0f;
    const float knob_x = track_pos.x + rh + ((track_w - track_h) * ease_out_cubic(anim.checked));
    const ImVec2 knob_c(knob_x, track_pos.y + rh);
    const float knob_scale = 1.0f - (ease_out_cubic(anim.active) * 0.06f);
    dl->AddCircleFilled(ImVec2(knob_c.x, knob_c.y + 1.0f), knob_r, IM_COL32(0, 0, 0, 55));
    dl->AddCircleFilled(knob_c, knob_r * knob_scale, ImGui::GetColorU32(p.text_primary));

    const float text_y = pos.y + (row_height - ImGui::GetTextLineHeight()) * 0.5f;
    dl->AddText(ImVec2(track_end.x + 12.0f, text_y), ImGui::GetColorU32(p.text_primary), text);
    ImGui::PopID();
    return changed;
}

inline bool tab_button(const char *label, bool active, const char *hint = nullptr, float width = 0.0f) {
    const char *text = label && label[0] ? label : "tab";
    const char *sub = hint && hint[0] ? hint : "";
    const Theme::Palette &p = Theme::palette();
    ImGui::PushID(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 label_size = ImGui::CalcTextSize(text);
    const float height = sub[0] ? 44.0f : 34.0f;
    const float resolved_width = width > 0.0f ? width : label_size.x + 24.0f;
    const ImVec2 size(resolved_width, height);
    const ImGuiID id = ImGui::GetID("##tab");
    AnimState &anim = anim_state(id);
    if (!anim.initialized) {
        anim.checked = active ? 1.0f : 0.0f;
        anim.initialized = true;
    }
    ImGui::InvisibleButton("##tab", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = ImGui::IsItemClicked();
    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    anim.hover = animate_to(anim.hover, hovered ? 1.0f : 0.0f, 16.0f);
    anim.active = animate_to(anim.active, ImGui::IsItemActive() ? 1.0f : 0.0f, 22.0f);
    anim.checked = animate_to(anim.checked, active ? 1.0f : 0.0f, 16.0f);

    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 end(pos.x + size.x, pos.y + size.y);
    const float active_t = ease_out_cubic(anim.checked);
    const ImVec4 hover_fill = Theme::with_alpha(p.surface_hover, anim.hover * 0.58f);
    dl->AddRectFilled(pos, end, ImGui::GetColorU32(hover_fill), Theme::radius().sm);
    if (active_t > 0.001f) {
        dl->AddRectFilled(ImVec2(pos.x + 12.0f, end.y - 2.0f), ImVec2(end.x - 12.0f, end.y),
                          ImGui::GetColorU32(Theme::with_alpha(p.accent_a, active_t)), Theme::radius().pill);
    }

    const float text_alpha = 0.58f + anim.hover * 0.22f + active_t * 0.20f;
    const float label_y = sub[0] ? pos.y + 5.0f : pos.y + (size.y - label_size.y) * 0.5f;
    dl->AddText(ImVec2(pos.x + 12.0f, label_y),
                ImGui::GetColorU32(Theme::with_alpha(p.text_primary, clamp01(text_alpha))), text);
    if (sub[0]) {
        dl->AddText(ImVec2(pos.x + 12.0f, pos.y + 26.0f), ImGui::GetColorU32(p.text_muted), sub);
    }
    ImGui::PopID();
    return pressed;
}

inline void tab_indicator(const char *id_text, const ImVec2 &target_min, const ImVec2 &target_max) {
    const Theme::Palette &p = Theme::palette();
    ImGui::PushID(id_text && id_text[0] ? id_text : "tabs");
    const ImGuiID id = ImGui::GetID("##indicator");
    AnimState &anim = anim_state(id);

    const float target_x = target_min.x + 14.0f;
    const float target_y = target_max.y - 4.0f;
    const float target_w = (target_max.x - target_min.x) - 28.0f;
    const float target_h = 2.0f;
    if (!anim.initialized) {
        anim.x = target_x;
        anim.y = target_y;
        anim.w = target_w;
        anim.h = target_h;
        anim.initialized = true;
    }

    anim.x = animate_to(anim.x, target_x, 18.0f);
    anim.y = animate_to(anim.y, target_y, 18.0f);
    anim.w = animate_to(anim.w, target_w, 18.0f);
    anim.h = animate_to(anim.h, target_h, 18.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(anim.x, anim.y), ImVec2(anim.x + anim.w, anim.y + anim.h),
                                              ImGui::GetColorU32(p.accent_a), Theme::radius().pill);
    ImGui::PopID();
}

inline void key_value(const char *key, const char *value) {
    const Theme::Palette &p = Theme::palette();
    const char *safe_key = key && key[0] ? key : "";
    const char *safe_value = value && value[0] ? value : "<unset>";
    begin_labeled_field(safe_key);
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_primary);
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextWrapped("%s", safe_value);
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    end_labeled_field();
}

inline bool begin_card(const char *title, const char *subtitle = nullptr) {
    const Theme::Palette &p = Theme::palette();
    const Theme::Spacing &sp = Theme::spacing();
    const bool card_style = title && title[0];

    ImGui::PushStyleColor(ImGuiCol_ChildBg, card_style ? p.bg_elevated : ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, Theme::radius().lg);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(sp.card.x + 2.0f, sp.card.y + 2.0f));
    const bool open = ImGui::BeginChild(title && title[0] ? title : "##card", ImVec2(0.0f, 0.0f),
                                        ImGuiChildFlags_AlwaysUseWindowPadding);
    if (open) {
        const ImVec2 c_pos = ImGui::GetWindowPos();
        const ImVec2 c_size = ImGui::GetWindowSize();
        ImDrawList *cdl = ImGui::GetWindowDrawList();
        if (card_style) {
            cdl->AddRect(c_pos, ImVec2(c_pos.x + c_size.x, c_pos.y + c_size.y), ImGui::GetColorU32(p.border_subtle),
                         Theme::radius().lg);
            ImGui::PushStyleColor(ImGuiCol_Text, p.text_primary);
            ImGui::PushFont(Theme::heading_font());
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::PopStyleColor();
        }
        if (subtitle && subtitle[0]) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0.0f, subtitle && subtitle[0] ? 12.0f : 10.0f));
    }
    return open;
}

inline void end_card() {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(1);
}
} // namespace ModUI::Widgets
)URK";
}

std::string LocalizationModule() {
    return R"URK(#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "config/mod_config.h"
#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "sdk/runtime_api.h"

namespace ModUI::Localization {
using Dictionary = std::unordered_map<std::string, std::string>;

struct Replacement {
    const char *name;
    std::string value;
};

inline const Dictionary &english() {
    static const Dictionary values = {
        {"about.title", "About"},
        {"about.default_description", "Small runtime menu for this mod."},
        {"about.author", "Author"},
        {"about.version", "Version"},
        {"about.url", "URL"},
        {"about.project_url", "Project URL"},
        {"about.social", "Social"},
        {"about.github", "GitHub"},
        {"menu.about", "About"},
        {"menu.config", "Config"},
        {"config.controls", "Controls"},
        {"config.show_menu", "Show menu"},
        {"config.toggle_key", "Toggle key"},
        {"config.localization", "Localization"},
        {"config.enable_localization", "Enable localization"},
        {"config.language", "Language"},
        {"widget.enabled", "Enabled"},
        {"widget.disabled", "Disabled"},
        {"example.key_code", "Key code: {code}"},
    };
    return values;
}

inline Dictionary &overrides() {
    static Dictionary values;
    return values;
}
inline std::vector<std::string> &languages() {
    static std::vector<std::string> values{"en"};
    return values;
}
inline std::string &current_language() {
    static std::string value = "en";
    return value;
}
inline std::string &last_error() {
    static std::string value;
    return value;
}
inline bool &initialized() {
    static bool value = false;
    return value;
}

inline void log_warning(const std::string &message) {
    const URK::ModContext *ctx = URK::context();
    if (ctx && ctx->Log)
        ctx->Log("[%s][localization] %s", ModConfig::display_name, message.c_str());
}

inline void report_error(const std::string &message) {
    last_error() = message;
    log_warning(message);
}

inline void skip_whitespace(const std::string &source, size_t &pos) {
    while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos])))
        ++pos;
}

inline void skip_utf8_bom(const std::string &source, size_t &pos) {
    if (pos == 0 && source.size() >= 3 && static_cast<unsigned char>(source[0]) == 0xEF &&
        static_cast<unsigned char>(source[1]) == 0xBB && static_cast<unsigned char>(source[2]) == 0xBF) {
        pos = 3;
    }
}

inline bool append_unicode(std::string &output, unsigned value) {
    if (value <= 0x7f)
        output.push_back(static_cast<char>(value));
    else if (value <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
    return true;
}

inline bool parse_string(const std::string &source, size_t &pos, std::string &output, std::string &error) {
    if (pos >= source.size() || source[pos++] != '"') {
        error = "expected JSON string";
        return false;
    }
    output.clear();
    while (pos < source.size()) {
        const char ch = source[pos++];
        if (ch == '"')
            return true;
        if (static_cast<unsigned char>(ch) < 0x20) {
            error = "control character in JSON string";
            return false;
        }
        if (ch != '\\') {
            output.push_back(ch);
            continue;
        }
        if (pos >= source.size()) {
            error = "unterminated JSON escape";
            return false;
        }
        const char escape = source[pos++];
        switch (escape) {
            case '"':
                output.push_back('"');
                break;
            case '\\':
                output.push_back('\\');
                break;
            case '/':
                output.push_back('/');
                break;
            case 'b':
                output.push_back('\b');
                break;
            case 'f':
                output.push_back('\f');
                break;
            case 'n':
                output.push_back('\n');
                break;
            case 'r':
                output.push_back('\r');
                break;
            case 't':
                output.push_back('\t');
                break;
            case 'u': {
                if (pos + 4 > source.size()) {
                    error = "truncated Unicode escape";
                    return false;
                }
                unsigned value = 0;
                for (int i = 0; i < 4; ++i) {
                    const char hex = source[pos++];
                    value <<= 4;
                    if (hex >= '0' && hex <= '9')
                        value |= static_cast<unsigned>(hex - '0');
                    else if (hex >= 'a' && hex <= 'f')
                        value |= static_cast<unsigned>(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F')
                        value |= static_cast<unsigned>(hex - 'A' + 10);
                    else {
                        error = "invalid Unicode escape";
                        return false;
                    }
                }
                if (value >= 0xd800 && value <= 0xdfff) {
                    error = "Unicode surrogate escapes are unsupported";
                    return false;
                }
                append_unicode(output, value);
                break;
            }
            default:
                error = "invalid JSON escape";
                return false;
        }
    }
    error = "unterminated JSON string";
    return false;
}

inline bool parse_dictionary(const std::string &source, Dictionary &output, std::string &error) {
    size_t pos = 0;
    skip_utf8_bom(source, pos);
    skip_whitespace(source, pos);
    if (pos >= source.size() || source[pos++] != '{') {
        error = "root must be an object";
        return false;
    }
    output.clear();
    skip_whitespace(source, pos);
    while (pos < source.size() && source[pos] != '}') {
        std::string key;
        std::string value;
        if (!parse_string(source, pos, key, error))
            return false;
        skip_whitespace(source, pos);
        if (pos >= source.size() || source[pos++] != ':') {
            error = "expected ':' after JSON key";
            return false;
        }
        skip_whitespace(source, pos);
        if (!parse_string(source, pos, value, error)) {
            error = "all localization values must be strings: " + error;
            return false;
        }
        output[std::move(key)] = std::move(value);
        skip_whitespace(source, pos);
        if (pos < source.size() && source[pos] == ',') {
            ++pos;
            skip_whitespace(source, pos);
            continue;
        }
        if (pos >= source.size() || source[pos] != '}') {
            error = "expected ',' or '}' in JSON object";
            return false;
        }
    }
    if (pos >= source.size() || source[pos++] != '}') {
        error = "unterminated JSON object";
        return false;
    }
    skip_whitespace(source, pos);
    if (pos != source.size()) {
        error = "unexpected content after JSON object";
        return false;
    }
    return true;
}

inline std::filesystem::path locale_directory() {
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&locale_directory), &module)) {
        report_error("cannot resolve the mod DLL path");
        return {};
    }
    std::vector<wchar_t> path(MAX_PATH);
    const DWORD length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) {
        report_error("cannot read the mod DLL path");
        return {};
    }
    return std::filesystem::path(path.data(), path.data() + length).parent_path() / "locales" / ModConfig::mod_id;
}

inline bool read_language(const std::string &language, Dictionary &output, std::string &error) {
    const std::filesystem::path path = locale_directory() / (std::string(language) + ".json");
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cannot open locale file: " + path.string();
        return false;
    }
    std::ostringstream content;
    content << input.rdbuf();
    if (!parse_dictionary(content.str(), output, error)) {
        error = "cannot parse locale file " + path.string() + ": " + error;
        return false;
    }
    return true;
}

inline bool load_language(const char *language) {
    if (!language || !language[0] || std::string(language) == "en") {
        overrides().clear();
        current_language() = "en";
        last_error().clear();
        return true;
    }
    Dictionary parsed;
    std::string error;
    if (!read_language(language, parsed, error)) {
        report_error(error);
        return false;
    }
    overrides() = std::move(parsed);
    current_language() = language;
    last_error().clear();
    return true;
}

inline void initialize() {
    if (initialized() || !ModConfig::enable_localization)
        return;
    initialized() = true;
    languages().assign(1, "en");
    last_error().clear();
    const std::filesystem::path directory = locale_directory();
    if (directory.empty())
        return;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error)) {
        report_error("locale directory is unavailable: " + directory.string());
        return;
    }
    const std::filesystem::directory_iterator entries(directory, error);
    if (error) {
        report_error("cannot enumerate locale directory: " + error.message());
        return;
    }
    for (const auto &entry : entries) {
        std::error_code entry_error;
        if (!entry.is_regular_file(entry_error)) {
            if (entry_error)
                report_error("cannot inspect locale entry: " + entry_error.message());
            continue;
        }
        if (entry.path().extension() != ".json")
            continue;
        const std::string language = entry.path().stem().string();
        if (language.empty() || language == "en")
            continue;
        Dictionary parsed;
        std::string load_error;
        if (read_language(language, parsed, load_error)) {
            languages().push_back(language);
        } else {
            report_error(load_error);
        }
    }
    std::sort(languages().begin() + 1, languages().end());
    if (ModConfig::default_language && std::string(ModConfig::default_language) != "en")
        load_language(ModConfig::default_language);
}

inline const std::vector<std::string> &available_languages() {
    initialize();
    return languages();
}
inline const char *active_language() {
    initialize();
    return current_language().c_str();
}
inline const char *last_error_message() {
    initialize();
    return last_error().c_str();
}
inline bool set_language(const char *language) {
    initialize();
    return ModConfig::enable_localization && load_language(language);
}

inline const char *translate(const char *key) {
    initialize();
    if (!key)
        return "";
    const auto override_it = overrides().find(key);
    if (override_it != overrides().end())
        return override_it->second.c_str();
    const auto english_it = english().find(key);
    return english_it != english().end() ? english_it->second.c_str() : key;
}

inline std::string format(const char *key, std::initializer_list<Replacement> replacements) {
    std::string result = translate(key);
    for (const Replacement &replacement : replacements) {
        if (!replacement.name || !replacement.name[0])
            continue;
        const std::string token = std::string("{") + replacement.name + "}";
        size_t pos = 0;
        while ((pos = result.find(token, pos)) != std::string::npos) {
            result.replace(pos, token.size(), replacement.value);
            pos += replacement.value.size();
        }
    }
    return result;
}
} // namespace ModUI::Localization
)URK";
}

std::string EnglishLocaleModule() {
    return R"URK({
  "about.title": "About",
  "about.default_description": "Small runtime menu for this mod.",
  "about.author": "Author",
  "about.version": "Version",
  "about.url": "URL",
  "about.project_url": "Project URL",
  "about.social": "Social",
  "about.github": "GitHub",
  "menu.about": "About",
  "menu.config": "Config",
  "config.controls": "Controls",
  "config.show_menu": "Show menu",
  "config.toggle_key": "Toggle key",
  "config.localization": "Localization",
  "config.enable_localization": "Enable localization",
  "config.language": "Language",
  "widget.enabled": "Enabled",
  "widget.disabled": "Disabled",
  "example.key_code": "Key code: {code}"
}
)URK";
}

std::string TurkishLocaleModule() {
    return R"URK({
  "about.title": "Hakkında",
  "about.default_description": "Bu mod için küçük bir çalışma zamanı menüsü.",
  "about.author": "Yazar", "about.version": "Sürüm", "about.url": "URL",
  "about.project_url": "Proje Bağlantısı", "about.social": "Sosyal", "about.github": "GitHub",
  "menu.about": "Hakkında", "menu.config": "Ayarlar",
  "config.controls": "Kontroller",
  "config.show_menu": "Menüyü göster", "config.toggle_key": "Menü tuşu", "config.language": "Dil",
  "config.localization": "Yerelleştirme", "config.enable_localization": "Yerelleştirmeyi etkinleştir",
  "widget.enabled": "Açık", "widget.disabled": "Kapalı",
  "example.key_code": "Tuş kodu: {code}"
}
)URK";
}

std::string JapaneseLocaleModule() {
    return R"URK({
  "about.title": "概要",
  "about.default_description": "このMOD用の小さなランタイムメニューです。",
  "about.author": "作成者", "about.version": "バージョン", "about.url": "URL",
  "about.project_url": "プロジェクトURL", "about.social": "ソーシャル", "about.github": "GitHub",
  "menu.about": "概要", "menu.config": "設定",
  "config.controls": "操作",
  "config.show_menu": "メニューを表示", "config.toggle_key": "メニューキー", "config.language": "言語",
  "config.localization": "ローカライゼーション", "config.enable_localization": "ローカライゼーションを有効化",
  "widget.enabled": "有効", "widget.disabled": "無効",
  "example.key_code": "キーコード: {code}"
}
)URK";
}

std::string ChineseLocaleModule() {
    return R"URK({
  "about.title": "关于",
  "about.default_description": "这是此MOD的小型运行时菜单。",
  "about.author": "作者", "about.version": "版本", "about.url": "URL",
  "about.project_url": "项目URL", "about.social": "社交", "about.github": "GitHub",
  "menu.about": "关于", "menu.config": "设置",
  "config.controls": "控制",
  "config.show_menu": "显示菜单", "config.toggle_key": "菜单按键", "config.language": "语言",
  "config.localization": "本地化", "config.enable_localization": "启用本地化",
  "widget.enabled": "已启用", "widget.disabled": "已禁用",
  "example.key_code": "按键代码: {code}"
}
)URK";
}

std::string RussianLocaleModule() {
    return R"URK({
  "about.title": "О моде",
  "about.default_description": "Небольшое меню во время выполнения для этого мода.",
  "about.author": "Автор", "about.version": "Версия", "about.url": "URL",
  "about.project_url": "URL проекта", "about.social": "Соцсети", "about.github": "GitHub",
  "menu.about": "О моде", "menu.config": "Настройки",
  "config.controls": "Управление",
  "config.show_menu": "Показать меню", "config.toggle_key": "Клавиша меню", "config.language": "Язык",
  "config.localization": "Локализация", "config.enable_localization": "Включить локализацию",
  "widget.enabled": "Вкл.", "widget.disabled": "Выкл.",
  "example.key_code": "Код клавиши: {code}"
}
)URK";
}

std::string UkrainianLocaleModule() {
    return R"URK({
  "about.title": "Про мод",
  "about.default_description": "Невелике меню часу виконання для цього мода.",
  "about.author": "Автор", "about.version": "Версія", "about.url": "URL",
  "about.project_url": "URL проєкту", "about.social": "Соцмережі", "about.github": "GitHub",
  "menu.about": "Про мод", "menu.config": "Налаштування",
  "config.controls": "Керування",
  "config.show_menu": "Показати меню", "config.toggle_key": "Клавіша меню", "config.language": "Мова",
  "config.localization": "Локалізація", "config.enable_localization": "Увімкнути локалізацію",
  "widget.enabled": "Увімк.", "widget.disabled": "Вимк.",
  "example.key_code": "Код клавіші: {code}"
}
)URK";
}

std::string SpanishLocaleModule() {
    return R"URK({
  "about.title": "Acerca de",
  "about.default_description": "Pequeño menú en tiempo de ejecución para este mod.",
  "about.author": "Autor", "about.version": "Versión", "about.url": "URL",
  "about.project_url": "URL del proyecto", "about.social": "Social", "about.github": "GitHub",
  "menu.about": "Acerca de", "menu.config": "Configuración",
  "config.controls": "Controles",
  "config.show_menu": "Mostrar menú", "config.toggle_key": "Tecla del menú", "config.language": "Idioma",
  "config.localization": "Localización", "config.enable_localization": "Habilitar localización",
  "widget.enabled": "Activado", "widget.disabled": "Desactivado",
  "example.key_code": "Código de tecla: {code}"
}
)URK";
}

std::string FrenchLocaleModule() {
    return R"URK({
  "about.title": "À propos",
  "about.default_description": "Petit menu d'exécution pour ce mod.",
  "about.author": "Auteur", "about.version": "Version", "about.url": "URL",
  "about.project_url": "URL du projet", "about.social": "Social", "about.github": "GitHub",
  "menu.about": "À propos", "menu.config": "Configuration",
  "config.controls": "Contrôles",
  "config.show_menu": "Afficher le menu", "config.toggle_key": "Touche du menu", "config.language": "Langue",
  "config.localization": "Localisation", "config.enable_localization": "Activer la localisation",
  "widget.enabled": "Activé", "widget.disabled": "Désactivé",
  "example.key_code": "Code de touche : {code}"
}
)URK";
}

std::string AboutTabModule() {
    return R"URK(#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "config/mod_config.h"
#include <Windows.h>
#include <imgui.h>
#include <shellapi.h>
#include <string>

#include "ui/localization.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ModUI::Tabs::About {
inline std::string normalized_link(const char *raw) {
    if (!raw || !raw[0])
        return {};
    std::string link(raw);
    if (link.find("://") == std::string::npos) {
        link.insert(0, "https://");
    }
    return link;
}

inline void open_external_link(const char *raw) {
    const std::string link = normalized_link(raw);
    if (link.empty())
        return;
    ShellExecuteA(nullptr, "open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

inline bool link_text(const char *label, const char *raw_url) {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    const std::string link = normalized_link(raw_url);
    const char *visible = link.empty() ? "<unset>" : link.c_str();
    ImGui::PushID(label && label[0] ? label : "link");

    ImGui::PushStyleColor(ImGuiCol_Text, link.empty() ? p.text_muted : p.info);
    ImGui::TextUnformatted(visible);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (!link.empty()) {
        ImDrawList *dl = ImGui::GetWindowDrawList();
        dl->AddLine(ImVec2(min.x, max.y), ImVec2(max.x, max.y), ImGui::GetColorU32(p.info), 1.0f);
    }
    ImGui::PopStyleColor();

    bool activated = false;
    if (!link.empty() && ImGui::IsItemHovered()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (ImGui::IsItemClicked()) {
            open_external_link(link.c_str());
            activated = true;
        }
    }
    ImGui::PopID();
    return activated;
}

inline bool social_badge(const char *label, const char *raw_url) {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    const std::string link = normalized_link(raw_url);
    if (link.empty())
        return false;

    const char *text = label && label[0] ? label : "Social";
    const ImVec2 text_size = ImGui::CalcTextSize(text);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImVec2 size(text_size.x + 22.0f, 26.0f);
    const ImVec2 end(pos.x + size.x, pos.y + size.y);

    ImGui::InvisibleButton(text, size);
    const bool hovered = ImGui::IsItemHovered();
    const bool pressed = ImGui::IsItemClicked();

    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, end,
                      ImGui::GetColorU32(hovered ? Theme::with_alpha(p.surface_hover, 0.90f)
                                                 : Theme::with_alpha(p.surface_active, 0.76f)),
                      Theme::radius().pill);
    dl->AddText(ImVec2(pos.x + 11.0f, pos.y + 5.0f), ImGui::GetColorU32(p.text_primary), text);

    if (hovered)
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (pressed) {
        open_external_link(link.c_str());
        return true;
    }
    return false;
}

inline void render() {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    const char *description = ModConfig::description[0] ? ModConfig::description
                                                        : ModUI::Localization::translate("about.default_description");

    ModUI::Widgets::key_value(ModUI::Localization::translate("about.author"), ModConfig::author);
    ModUI::Widgets::key_value(ModUI::Localization::translate("about.version"), ModConfig::version);

    ModUI::Widgets::begin_labeled_field(ModUI::Localization::translate("about.url"));
    link_text(ModUI::Localization::translate("about.project_url"), ModConfig::url);
    ModUI::Widgets::end_labeled_field();

    if (ModConfig::social[0]) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ModUI::Widgets::begin_labeled_field(ModUI::Localization::translate("about.social"));
        social_badge(ModUI::Localization::translate("about.github"), ModConfig::social);
        ModUI::Widgets::end_labeled_field();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_secondary);
    ImGui::TextWrapped("%s", description);
    ImGui::PopStyleColor();
}
} // namespace ModUI::Tabs::About
)URK";
}

std::string ConfigTabModule() {
    return R"URK(#pragma once
#include "config/mod_config.h"
#include <cstdio>
#include <imgui.h>
#include <string>
#include <vector>

#include "ui/localization.h"
#include "ui/theme.h"
#include "ui/widgets.h"

namespace ModUI::Tabs::Config {
inline void section_label(const char *text) {
    const ModUI::Theme::Palette &p = ModUI::Theme::palette();
    ImGui::PushStyleColor(ImGuiCol_Text, ModUI::Theme::with_alpha(p.text_primary, 0.62f));
    ImGui::TextUnformatted(text && text[0] ? text : "Section");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0.0f, 5.0f));
}

inline void render_controls(const char *hotkey) {
    section_label(ModUI::Localization::translate("config.controls"));
    ModUI::Widgets::toggle(ModUI::Localization::translate("config.show_menu"), &ModConfig::show_menu);
    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    ModUI::Widgets::key_value(ModUI::Localization::translate("config.toggle_key"), hotkey);
}

inline void render_language_selector() {
    const std::vector<std::string> &languages = ModUI::Localization::available_languages();
    std::vector<const char *> labels;
    labels.reserve(languages.size());
    int current = 0;
    for (size_t i = 0; i < languages.size(); ++i) {
        labels.push_back(languages[i].c_str());
        if (languages[i] == ModUI::Localization::active_language())
            current = static_cast<int>(i);
    }
    if (ModUI::Widgets::combo(ModUI::Localization::translate("config.language"), &current, labels.data(),
                              static_cast<int>(labels.size()), "config.language")) {
        ModUI::Localization::set_language(languages[static_cast<size_t>(current)].c_str());
    }
    const char *error = ModUI::Localization::last_error_message();
    if (error && error[0]) {
        const ModUI::Theme::Palette &p = ModUI::Theme::palette();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, p.danger);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextWrapped("%s", error);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }
}

inline void render() {
    char hotkey[32]{};
    std::snprintf(hotkey, sizeof(hotkey), "0x%02X", ModConfig::menu_toggle_key);
    render_controls(hotkey);
    if (ModConfig::enable_localization) {
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        section_label(ModUI::Localization::translate("config.localization"));
        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        render_language_selector();
    }
}
} // namespace ModUI::Tabs::Config
)URK";
}

std::string UiModule() {
    return R"URK(#pragma once
#include "config/mod_config.h"
#include <imgui.h>

#include "localization.h"
#include "tabs/about_tab.h"
#include "tabs/config_tab.h"
#include "theme.h"
#include "widgets.h"

namespace ModUI {
enum class Tab {
    Config,
    About
};

inline Tab &active_tab() {
    static Tab tab = Tab::Config;
    return tab;
}

inline void initialize_style() {
    Theme::apply();
}

inline void render_menu() {
    if (!ModConfig::show_menu)
        return;
    Localization::initialize();

    const float dpi_scale = Theme::dpi_scale();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float viewport_scale = viewport && viewport->Size.y > 0.0f ? viewport->Size.y / 1080.0f : 1.0f;
    float size_scale = dpi_scale > viewport_scale ? dpi_scale : viewport_scale;
    if (size_scale < 1.0f)
        size_scale = 1.0f;
    if (size_scale > 1.35f)
        size_scale = 1.35f;

    const ImVec2 base_window_size(640.0f, 330.0f);
    const ImVec2 base_min_size(560.0f, 300.0f);
    const ImVec2 base_max_size(900.0f, 680.0f);

    ImGui::SetNextWindowSize(ImVec2(base_window_size.x * size_scale, base_window_size.y * size_scale),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(base_min_size.x * dpi_scale, base_min_size.y * dpi_scale),
                                        ImVec2(base_max_size.x * size_scale, base_max_size.y * size_scale));
    ImGuiWindowClass window_class{};
    window_class.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_NoFocusOnAppearing | ImGuiViewportFlags_NoFocusOnClick |
        ImGuiViewportFlags_NoTaskBarIcon;
    ImGui::SetNextWindowClass(&window_class);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (!ImGui::Begin(ModConfig::display_name, &ModConfig::show_menu,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDocking)) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }
    ImGui::PopStyleVar();

    const Theme::Palette &p = Theme::palette();
    const Theme::Spacing &sp = Theme::spacing();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const ImVec2 win_pos = ImGui::GetWindowPos();
    const ImVec2 win_size = ImGui::GetWindowSize();

    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), ImGui::GetColorU32(p.bg_base),
                      Theme::radius().xl);
    dl->AddRect(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y), ImGui::GetColorU32(p.border_subtle),
                Theme::radius().xl);

    dl->AddRectFilled(win_pos, ImVec2(win_pos.x + win_size.x, win_pos.y + sp.header_height),
                      ImGui::GetColorU32(p.bg_overlay), Theme::radius().xl, ImDrawFlags_RoundCornersTop);
    dl->AddLine(ImVec2(win_pos.x + 1.0f, win_pos.y + sp.header_height),
                ImVec2(win_pos.x + win_size.x - 1.0f, win_pos.y + sp.header_height),
                ImGui::GetColorU32(p.border_subtle));

    ImGui::BeginChild("##header", ImVec2(0.0f, sp.header_height), false,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::SetCursorPos(ImVec2(20.0f, 13.0f));
    ImGui::PushFont(Theme::heading_font());
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_primary);
    ImGui::TextUnformatted(ModConfig::display_name);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_muted);
    ImGui::Text("v%s", ModConfig::version);
    ImGui::PopStyleColor();

    ImGui::SetCursorPos(ImVec2(12.0f, 44.0f));
    const float nav_width = 0.0f;
    if (Widgets::tab_button(Localization::translate("menu.config"), active_tab() == Tab::Config, nullptr, nav_width))
        active_tab() = Tab::Config;
    ImGui::SameLine(0.0f, sp.tab_gap);
    if (Widgets::tab_button(Localization::translate("menu.about"), active_tab() == Tab::About, nullptr, nav_width))
        active_tab() = Tab::About;
    ImGui::EndChild();

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, sp.content);
    ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::PushFont(Theme::heading_font());
    ImGui::PushStyleColor(ImGuiCol_Text, p.text_primary);
    ImGui::TextUnformatted(Localization::translate(active_tab() == Tab::Config ? "menu.config" : "menu.about"));
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    if (active_tab() == Tab::Config) {
        Tabs::Config::render();
    } else {
        Tabs::About::render();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
}
} // namespace ModUI
)URK";
}

std::string HighlightModule() {
    return R"URK(#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <imgui.h>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "sdk/unity/unity.h"

namespace ModUI::Highlight {
using Unity::Camera;
using Unity::Component;
using Unity::DiagnosticSink;
using Unity::GameObject;
using Unity::ProjectionResult;
using Unity::Transform;
using Unity::Vector2;
using Unity::Vector3;

using HighlightId = std::uint32_t;
using ProjectWorldInfoFn = bool (*)(const Vector3 &world, ProjectionResult *projection, void *user);

enum class TargetKind : std::uint8_t {
    None,
    Transform,
    WorldPoint,
    ScreenRect,
};

enum class ResolveState : std::uint8_t {
    Drawn,
    Skipped,
    Failed,
};

enum class UpdateMode : std::uint8_t {
    EveryFrame,
    Budgeted,
    // Reprojects entries only after an explicit dirty/remove request.
    EventDriven,
};

struct UpdatePolicy {
    UpdateMode mode = UpdateMode::Budgeted;
    // Zero disables the per-frame update limit.
    std::size_t max_updates_per_frame = 20;
    std::uint32_t projection_interval_frames = 2;
    std::uint32_t camera_resolve_interval_frames = 30;
    std::uint32_t transform_validation_interval_frames = 30;
    bool use_viewport_projection = false;
};

struct FrameStats {
    std::size_t targets = 0;
    std::size_t projection_updates = 0;
    std::size_t projection_failures = 0;
    std::size_t cached_projection_draws = 0;
};

enum class DebugState : std::uint8_t {
    None,
    Added,
    Rect,
    Label,
    Indicator,
    Offscreen,
    TooClose,
    InvalidRect,
    MissingTransform,
    DeadTransform,
    NoProjection,
    ProjectionFailed,
    Removed,
};

struct Style {
    ImU32 color = IM_COL32(255, 213, 74, 235);
    ImU32 fill_color = IM_COL32(255, 213, 74, 26);
    ImU32 indicator_color = IM_COL32(255, 213, 74, 220);
    ImU32 label_color = IM_COL32(255, 255, 255, 235);
    ImU32 label_bg_color = IM_COL32(18, 18, 18, 205);
    ImU32 label_border_color = IM_COL32(255, 213, 74, 190);
    ImU32 shadow_color = IM_COL32(0, 0, 0, 120);
    float width = 92.0f;
    float height = 120.0f;
    float rounding = 2.0f;
    float thickness = 2.0f;
    float corner_length = 18.0f;
    float shadow_offset = 2.0f;
    float indicator_padding = 24.0f;
    float indicator_length = 84.0f;
    float indicator_thickness = 3.0f;
    float indicator_head_size = 12.0f;
    float indicator_center_gap = 16.0f;
    float indicator_center_dot_radius = 4.0f;
    float hide_within_distance = 0.0f;
    float reference_distance = 12.0f;
    float min_scale = 0.48f;
    float max_scale = 1.18f;
    float label_offset = 10.0f;
    float label_rounding = 4.0f;
    ImVec2 label_padding = ImVec2(8.0f, 4.0f);
    bool filled = false;
    bool draw_box = true;
    bool draw_label = false;
    bool label_above_box = false;
    bool label_show_offscreen = true;
    bool corner_box = false;
    bool shadow = false;
    bool scale_with_distance = true;
    bool offscreen_indicator = true;
    bool draw_behind_indicator = true;
};

struct Entry {
    HighlightId id = 0;
    TargetKind kind = TargetKind::None;
    Transform transform{};
    Vector3 world{};
    ImVec2 screen_min{};
    ImVec2 screen_max{};
    Style style{};
    std::string label{};
    bool enabled = true;
    std::uint8_t failed_frames = 0;
    std::uint32_t last_validation_frame = 0;
    std::uint32_t last_debug_frame = 0;
    DebugState last_debug_state = DebugState::None;
    ProjectionResult last_projection{};
    std::uint32_t last_projection_frame = 0;
    bool projection_dirty = true;
    bool has_projection = false;
};

struct ResolvedDraw {
    bool rect = false;
    bool indicator = false;
    bool label = false;
    ImVec2 min{};
    ImVec2 max{};
    ImVec2 line_from{};
    ImVec2 line_to{};
    ImVec2 arrow_left{};
    ImVec2 arrow_right{};
    ImVec2 label_pos{};
    const char *label_text = nullptr;
    bool label_centered = true;
};

class Manager {
    struct FrameProjectionCache {
        bool default_camera_resolved = false;
        Camera default_camera{};
        bool snapshot_resolved = false;
        bool snapshot_valid = false;
        Vector2 screen_size{};
        Vector2 screen_center{};
        Vector3 camera_position{};
        Vector3 camera_forward{0.0f, 0.0f, 1.0f};
        Vector3 camera_right{1.0f, 0.0f, 0.0f};
        Vector3 camera_up{0.0f, 1.0f, 0.0f};
        bool have_camera_basis = false;
    };

    enum class PendingKind : std::uint8_t {
        AddTransform,
        AddWorldPoint,
        MarkDirty,
        MarkAllDirty,
        SetWorldPoint,
        Remove,
        Clear,
    };

    struct PendingCommand {
        PendingKind kind = PendingKind::MarkDirty;
        HighlightId id = 0;
        Transform transform{};
        Vector3 world{};
        Style style{};
        std::string label{};
    };

  public:
    HighlightId add(GameObject object, Style style = {}) {
        return add(object, nullptr, style);
    }

    HighlightId add(GameObject object, const char *label, Style style = {}) {
        return static_cast<bool>(object) ? add(object.transform(), label, style) : 0;
    }

    HighlightId add(Component component, Style style = {}) {
        return add(component, nullptr, style);
    }

    HighlightId add(Component component, const char *label, Style style = {}) {
        return static_cast<bool>(component) ? add(component.transform(), label, style) : 0;
    }

    HighlightId add(Transform transform, Style style = {}) {
        return add(transform, nullptr, style);
    }

    HighlightId add(Transform transform, const char *label, Style style = {}) {
        if (!static_cast<bool>(transform)) {
            log_text("[highlight] add(transform) rejected: target transform is null");
            return 0;
        }
        return add_transform_entry(allocate_id(), transform, label, style);
    }

    HighlightId enqueue_add(Transform transform, Style style = {}) {
        return enqueue_add(transform, nullptr, style);
    }

    HighlightId enqueue_add(Transform transform, const char *label, Style style = {}) {
        if (!static_cast<bool>(transform)) {
            log_text("[highlight] enqueue_add(transform) rejected: target transform "
                     "is null");
            return 0;
        }
        PendingCommand command{};
        command.kind = PendingKind::AddTransform;
        command.id = allocate_id();
        command.transform = transform;
        command.style = style;
        command.label = safe_label(label);
        const HighlightId id = command.id;
        enqueue(std::move(command));
        return id;
    }

    HighlightId enqueue_add(GameObject object, const char *label, Style style = {}) {
        return static_cast<bool>(object) ? enqueue_add(object.transform(), label, style) : 0;
    }

    HighlightId enqueue_add(GameObject object, Style style = {}) {
        return enqueue_add(object, nullptr, style);
    }

    HighlightId enqueue_add(Component component, const char *label, Style style = {}) {
        return static_cast<bool>(component) ? enqueue_add(component.transform(), label, style) : 0;
    }

    HighlightId enqueue_add(Component component, Style style = {}) {
        return enqueue_add(component, nullptr, style);
    }

  private:
    HighlightId add_transform_entry(HighlightId id, Transform transform, const char *label, Style style) {
        Entry entry{};
        entry.id = id;
        entry.kind = TargetKind::Transform;
        entry.transform = transform;
        entry.style = style;
        entry.label = safe_label(label);
        index_[entry.id] = entries_.size();
        entries_.push_back(entry);
        note_state(entries_.back(), DebugState::Added, nullptr, "transform target registered");
        return entry.id;
    }

  public:
    HighlightId add_world_point(Vector3 world, Style style = {}) {
        return add_world_point(world, nullptr, style);
    }

    HighlightId add_world_point(Vector3 world, const char *label, Style style = {}) {
        return add_world_point_entry(allocate_id(), world, label, style);
    }

    HighlightId enqueue_add_world_point(Vector3 world, Style style = {}) {
        return enqueue_add_world_point(world, nullptr, style);
    }

    HighlightId enqueue_add_world_point(Vector3 world, const char *label, Style style = {}) {
        PendingCommand command{};
        command.kind = PendingKind::AddWorldPoint;
        command.id = allocate_id();
        command.world = world;
        command.style = style;
        command.label = safe_label(label);
        const HighlightId id = command.id;
        enqueue(std::move(command));
        return id;
    }

  private:
    HighlightId add_world_point_entry(HighlightId id, Vector3 world, const char *label, Style style) {
        Entry entry{};
        entry.id = id;
        entry.kind = TargetKind::WorldPoint;
        entry.world = world;
        entry.style = style;
        entry.label = safe_label(label);
        index_[entry.id] = entries_.size();
        entries_.push_back(entry);
        note_state(entries_.back(), DebugState::Added, nullptr, "world point registered");
        return entry.id;
    }

  public:
    HighlightId add_screen_rect(ImVec2 min, ImVec2 max, Style style = {}) {
        return add_screen_rect(min, max, nullptr, style);
    }

    HighlightId add_screen_rect(ImVec2 min, ImVec2 max, const char *label, Style style = {}) {
        Entry entry{};
        entry.id = allocate_id();
        entry.kind = TargetKind::ScreenRect;
        entry.screen_min = min;
        entry.screen_max = max;
        entry.style = style;
        entry.label = safe_label(label);
        index_[entry.id] = entries_.size();
        entries_.push_back(entry);
        note_state(entries_.back(), DebugState::Added, nullptr, "screen rect registered");
        return entry.id;
    }

    HighlightId add_screen_rect(Vector2 min, Vector2 max, Style style = {}) {
        return add_screen_rect(to_imgui(min), to_imgui(max), style);
    }

    HighlightId add_screen_rect(Vector2 min, Vector2 max, const char *label, Style style = {}) {
        return add_screen_rect(to_imgui(min), to_imgui(max), label, style);
    }

    bool remove(HighlightId id) {
        return remove_entry(id);
    }

    void enqueue_remove(HighlightId id) {
        PendingCommand command{};
        command.kind = PendingKind::Remove;
        command.id = id;
        enqueue(std::move(command));
    }

  private:
    bool remove_entry(HighlightId id) {
        const auto found = index_.find(id);
        if (found == index_.end())
            return false;
        Entry &entry = entries_[found->second];
        note_state(entry, DebugState::Removed, nullptr, "highlight removed");
        erase_at(found->second);
        return true;
    }

  public:
    void clear() {
        entries_.clear();
        index_.clear();
        update_cursor_ = 0;
    }

    void enqueue_clear() {
        PendingCommand command{};
        command.kind = PendingKind::Clear;
        enqueue(std::move(command));
    }

    void set_enabled(HighlightId id, bool enabled) {
        if (Entry *entry = find(id)) {
            entry->enabled = enabled;
            if (enabled && entry->kind != TargetKind::ScreenRect)
                entry->projection_dirty = true;
        }
    }

    void set_style(HighlightId id, Style style) {
        if (Entry *entry = find(id)) {
            entry->style = style;
            entry->projection_dirty = true;
        }
    }

    void set_label(HighlightId id, const char *label) {
        if (Entry *entry = find(id))
            entry->label = safe_label(label);
    }

    void set_world_point(HighlightId id, Vector3 world) {
        if (Entry *entry = find(id)) {
            entry->kind = TargetKind::WorldPoint;
            entry->world = world;
            entry->failed_frames = 0;
            entry->enabled = true;
            entry->projection_dirty = true;
        }
    }

    void mark_dirty(HighlightId id) {
        if (Entry *entry = find(id)) {
            entry->projection_dirty = true;
            entry->enabled = true;
        }
    }

    void mark_all_dirty() {
        for (Entry &entry : entries_) {
            if (entry.kind != TargetKind::ScreenRect)
                entry.projection_dirty = true;
        }
    }

    void enqueue_mark_dirty(HighlightId id) {
        PendingCommand command{};
        command.kind = PendingKind::MarkDirty;
        command.id = id;
        enqueue(std::move(command));
    }

    void enqueue_mark_all_dirty() {
        PendingCommand command{};
        command.kind = PendingKind::MarkAllDirty;
        enqueue(std::move(command));
    }

    void enqueue_set_world_point(HighlightId id, Vector3 world) {
        PendingCommand command{};
        command.kind = PendingKind::SetWorldPoint;
        command.id = id;
        command.world = world;
        enqueue(std::move(command));
    }

    void set_screen_rect(HighlightId id, ImVec2 min, ImVec2 max) {
        if (Entry *entry = find(id)) {
            entry->kind = TargetKind::ScreenRect;
            entry->screen_min = min;
            entry->screen_max = max;
            entry->failed_frames = 0;
            entry->enabled = true;
        }
    }

    void set_screen_rect(HighlightId id, Vector2 min, Vector2 max) {
        set_screen_rect(id, to_imgui(min), to_imgui(max));
    }

    void set_screen_center(HighlightId id, ImVec2 center) {
        if (Entry *entry = find(id)) {
            const float half_w = entry->style.width * 0.5f;
            const float half_h = entry->style.height * 0.5f;
            set_screen_rect(id, ImVec2(center.x - half_w, center.y - half_h),
                            ImVec2(center.x + half_w, center.y + half_h));
        }
    }

    void set_screen_center(HighlightId id, Vector2 center) {
        set_screen_center(id, to_imgui(center));
    }

    void set_projector_info(ProjectWorldInfoFn projector, void *user = nullptr) {
        projector_info_ = projector;
        projector_user_ = user;
        mark_all_dirty();
        log_text(projector ? "[highlight] projection-info callback registered"
                           : "[highlight] projection-info callback cleared; "
                             "main-camera projection helper will be used");
    }

    void set_diagnostics(DiagnosticSink sink) {
        diagnostics_ = sink;
    }

    void set_verbose_diagnostics(bool enabled) {
        verbose_diagnostics_ = enabled;
    }

    void set_diagnostic_throttle_frames(std::uint32_t frames) {
        diagnostic_throttle_frames_ = frames ? frames : 1;
    }

    void set_update_policy(UpdatePolicy policy) {
        policy.projection_interval_frames = std::max<std::uint32_t>(1, policy.projection_interval_frames);
        policy.camera_resolve_interval_frames = std::max<std::uint32_t>(1, policy.camera_resolve_interval_frames);
        policy.transform_validation_interval_frames =
            std::max<std::uint32_t>(1, policy.transform_validation_interval_frames);
        update_policy_ = policy;
        mark_all_dirty();
    }

    UpdatePolicy update_policy() const {
        return update_policy_;
    }

    FrameStats last_frame_stats() const {
        return frame_stats_;
    }

    void render(ImDrawList *draw_list = nullptr) {
        if (!draw_list)
            draw_list = ImGui::GetBackgroundDrawList();
        if (!draw_list)
            return;
        apply_pending();
        ++frame_index_;
        frame_stats_ = {};
        frame_stats_.targets = entries_.size();
        FrameProjectionCache frame{};
        update_projection_cache(frame);
        for (std::size_t i = 0; i < entries_.size();) {
            Entry &entry = entries_[i];
            if (!entry.enabled) {
                ++i;
                continue;
            }
            ResolvedDraw draw{};
            const ResolveState state = resolve_draw(entry, draw);
            if (state == ResolveState::Failed) {
                if (!entry.enabled && entry.kind == TargetKind::Transform) {
                    erase_at(i);
                    continue;
                }
                ++i;
                continue;
            }
            if (state == ResolveState::Skipped) {
                ++i;
                continue;
            }
            if (draw.rect)
                draw_rect(draw_list, draw.min, draw.max, entry.style);
            if (draw.indicator)
                draw_indicator(draw_list, draw, entry.style);
            if (draw.label)
                draw_label(draw_list, draw, entry.style);
            if (entry.kind != TargetKind::ScreenRect && entry.last_projection_frame != frame_index_)
                ++frame_stats_.cached_projection_draws;
            ++i;
        }
    }

    std::size_t size() const {
        return entries_.size();
    }

  private:
    HighlightId allocate_id() {
        HighlightId id = next_id_.fetch_add(1, std::memory_order_relaxed);
        if (id == 0)
            id = next_id_.fetch_add(1, std::memory_order_relaxed);
        return id;
    }

    void enqueue(PendingCommand command) {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        pending_.push_back(std::move(command));
    }

    void apply_pending() {
        std::vector<PendingCommand> pending;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending.swap(pending_);
        }
        for (PendingCommand &command : pending) {
            switch (command.kind) {
                case PendingKind::AddTransform:
                    add_transform_entry(command.id, command.transform, command.label.c_str(), command.style);
                    break;
                case PendingKind::AddWorldPoint:
                    add_world_point_entry(command.id, command.world, command.label.c_str(), command.style);
                    break;
                case PendingKind::MarkDirty:
                    mark_dirty(command.id);
                    break;
                case PendingKind::MarkAllDirty:
                    mark_all_dirty();
                    break;
                case PendingKind::SetWorldPoint:
                    set_world_point(command.id, command.world);
                    break;
                case PendingKind::Remove:
                    remove_entry(command.id);
                    break;
                case PendingKind::Clear:
                    clear();
                    break;
            }
        }
    }

    Entry *find(HighlightId id) {
        const auto found = index_.find(id);
        return found != index_.end() ? &entries_[found->second] : nullptr;
    }

    ResolveState resolve_draw(Entry &entry, ResolvedDraw &draw) {
        if (entry.kind == TargetKind::ScreenRect) {
            draw.min = entry.screen_min;
            draw.max = entry.screen_max;
            if (!valid_rect(draw.min, draw.max)) {
                note_state(entry, DebugState::InvalidRect, nullptr, "screen rect is invalid");
                fail(entry, false);
                return ResolveState::Failed;
            }
            draw.rect = entry.style.draw_box;
            if (entry.style.draw_label && !entry.label.empty()) {
                const ImVec2 anchor((draw.min.x + draw.max.x) * 0.5f,
                                    entry.style.label_above_box ? draw.min.y : draw.max.y);
                configure_label(draw, entry, anchor);
            }
            note_state(entry, draw.rect ? DebugState::Rect : DebugState::Label);
            reset_failure(entry);
            return (draw.rect || draw.label) ? ResolveState::Drawn : ResolveState::Skipped;
        }

        if (!entry.has_projection)
            return ResolveState::Skipped;

        const ProjectionResult &projection = entry.last_projection;

        if (projection.on_screen && entry.style.hide_within_distance > 0.0f && projection.distance > 0.0f &&
            projection.distance <= entry.style.hide_within_distance) {
            note_state(entry, DebugState::TooClose, &projection,
                       "target is inside the configured near-distance hide threshold");
            return ResolveState::Skipped;
        }

        if (projection.on_screen) {
            const float scale = screen_scale(entry.style, projection);
            const float half_w = (entry.style.width * scale) * 0.5f;
            const float half_h = (entry.style.height * scale) * 0.5f;
            draw.rect = entry.style.draw_box;
            draw.min = ImVec2(projection.screen.x - half_w, projection.screen.y - half_h);
            draw.max = ImVec2(projection.screen.x + half_w, projection.screen.y + half_h);
            if (draw.rect && !valid_rect(draw.min, draw.max)) {
                note_state(entry, DebugState::InvalidRect, &projection, "projected rect is invalid");
                fail(entry, false);
                return ResolveState::Failed;
            }
            if (entry.style.draw_label && !entry.label.empty()) {
                const ImVec2 anchor(projection.screen.x, entry.style.label_above_box ? draw.min.y : draw.max.y);
                configure_label(draw, entry, anchor);
            }
            note_state(entry, draw.rect ? DebugState::Rect : DebugState::Label, &projection);
            return (draw.rect || draw.label) ? ResolveState::Drawn : ResolveState::Skipped;
        }

        if (entry.style.offscreen_indicator && (projection.in_front || entry.style.draw_behind_indicator)) {
            const Vector2 center = projection.screen_center;
            Vector2 edge = projection.clamped_screen;
            Vector2 start = center + projection.direction * entry.style.indicator_center_gap;
            Vector2 end = edge;
            const float edge_distance = Vector2::distance(start, end);
            if (entry.style.indicator_length > 0.0f && edge_distance > entry.style.indicator_length) {
                end = start + projection.direction * entry.style.indicator_length;
            }
            draw.indicator = true;
            draw.line_from = to_imgui(start);
            draw.line_to = to_imgui(end);
            const Vector2 arrow_base = end - projection.direction * entry.style.indicator_head_size;
            const Vector2 normal{-projection.direction.y, projection.direction.x};
            draw.arrow_left = to_imgui(arrow_base + normal * (entry.style.indicator_head_size * 0.62f));
            draw.arrow_right = to_imgui(arrow_base - normal * (entry.style.indicator_head_size * 0.62f));
            if (entry.style.draw_label && entry.style.label_show_offscreen && !entry.label.empty()) {
                configure_label(draw, entry, to_imgui(end + projection.direction * (entry.style.label_offset + 6.0f)));
            }
            note_state(entry, DebugState::Indicator, &projection,
                       projection.in_front ? "center-anchored offscreen arrow drawn"
                                           : "behind-camera arrow drawn from screen center");
            return ResolveState::Drawn;
        }

        note_state(entry, DebugState::Offscreen, &projection,
                   projection.in_front ? "target is outside the current camera frustum"
                                       : "target is behind the current camera");
        return ResolveState::Skipped;
    }

    void update_projection_cache(FrameProjectionCache &frame) {
        const std::size_t count = entries_.size();
        if (count == 0)
            return;

        const bool unlimited =
            update_policy_.mode == UpdateMode::EveryFrame || update_policy_.max_updates_per_frame == 0;
        const std::size_t budget = unlimited ? count : std::min(update_policy_.max_updates_per_frame, count);
        std::size_t visited = 0;
        std::size_t updated = 0;
        update_cursor_ %= count;

        while (visited < count && updated < budget) {
            const std::size_t index = (update_cursor_ + visited) % count;
            Entry &entry = entries_[index];
            ++visited;
            if (!should_update_projection(entry))
                continue;

            ++updated;
            ++frame_stats_.projection_updates;
            if (!refresh_projection(entry, frame))
                ++frame_stats_.projection_failures;
        }

        update_cursor_ = (update_cursor_ + visited) % count;
    }

    bool should_update_projection(const Entry &entry) const {
        if (!entry.enabled || entry.kind == TargetKind::ScreenRect)
            return false;
        if (!entry.has_projection || entry.projection_dirty)
            return true;
        if (update_policy_.mode == UpdateMode::EventDriven)
            return false;
        if (update_policy_.mode == UpdateMode::EveryFrame)
            return true;
        return frame_index_ - entry.last_projection_frame >= update_policy_.projection_interval_frames;
    }

    bool refresh_projection(Entry &entry, FrameProjectionCache &frame) {
        ProjectionResult projection{};
        Vector3 world = entry.world;
        if (entry.kind == TargetKind::Transform) {
            if (!static_cast<bool>(entry.transform)) {
                note_state(entry, DebugState::MissingTransform, nullptr, "target transform handle is null");
                entry.has_projection = false;
                return fail(entry, true);
            }
            if (frame_index_ - entry.last_validation_frame >= update_policy_.transform_validation_interval_frames) {
                entry.last_validation_frame = frame_index_;
                if (!entry.transform.alive()) {
                    entry.enabled = false;
                    entry.has_projection = false;
                    note_state(entry, DebugState::DeadTransform, nullptr, "target transform is no longer alive");
                    return false;
                }
            }
            world = entry.transform.position();
            entry.world = world;
        }

        if (projector_info_) {
            if (!projector_info_(world, &projection, projector_user_)) {
                note_state(entry, DebugState::ProjectionFailed, nullptr, "projection-info callback returned false");
                entry.has_projection = false;
                return fail(entry, false);
            }
            projection.world = world;
            if (projection.screen_center.nearly_zero())
                projection.screen_center = fallback_screen_center();
            if (projection.direction.nearly_zero()) {
                Vector2 fallback_direction = projection.clamped_screen - projection.screen_center;
                if (fallback_direction.nearly_zero())
                    fallback_direction = projection.screen - projection.screen_center;
                projection.direction =
                    fallback_direction.nearly_zero() ? Vector2{0.0f, -1.0f} : fallback_direction.normalized();
            }
            projection.valid = true;
        } else {
            projection = project_world_cached(frame, world, entry.style.indicator_padding);
            if (!projection.valid) {
                entry.has_projection = false;
                note_state(entry, DebugState::NoProjection, nullptr,
                           "default main-camera projection helper returned no result");
                return fail(entry, false);
            }
        }

        entry.last_projection = projection;
        entry.last_projection_frame = frame_index_;
        entry.projection_dirty = false;
        entry.has_projection = true;
        reset_failure(entry);
        return true;
    }

    ProjectionResult project_world_cached(FrameProjectionCache &frame, Vector3 world, float edge_padding) {
        ProjectionResult result{};
        result.world = world;
        if (!resolve_camera_snapshot(frame))
            return result;

        result.screen_center = frame.screen_center;
        const Vector3 offset = world - frame.camera_position;
        result.distance = offset.magnitude();
        if (frame.have_camera_basis && result.distance > 0.000001f)
            result.facing = Vector3::dot(offset / result.distance, frame.camera_forward);

        result.screen3 = frame.default_camera.WorldToScreenPoint(world);
        result.depth = result.screen3.z;
        result.in_front = result.depth > 0.01f;
        result.screen = {result.screen3.x, frame.screen_size.y - result.screen3.y};

        if (update_policy_.use_viewport_projection) {
            result.viewport = frame.default_camera.WorldToViewportPoint(world);
            result.on_screen = result.in_front && result.viewport.x >= 0.0f && result.viewport.x <= 1.0f &&
                               result.viewport.y >= 0.0f && result.viewport.y <= 1.0f;
        } else {
            result.viewport = {frame.screen_size.x > 0.0f ? result.screen3.x / frame.screen_size.x : 0.0f,
                               frame.screen_size.y > 0.0f ? result.screen3.y / frame.screen_size.y : 0.0f,
                               result.depth};
            result.on_screen = result.in_front && result.screen3.x >= 0.0f && result.screen3.x <= frame.screen_size.x &&
                               result.screen3.y >= 0.0f && result.screen3.y <= frame.screen_size.y;
        }

        Vector2 direction = result.screen - result.screen_center;
        if (!result.on_screen && frame.have_camera_basis && result.distance > 0.000001f) {
            const Vector3 offset_direction = offset / result.distance;
            direction = {Vector3::dot(offset_direction, frame.camera_right),
                         -Vector3::dot(offset_direction, frame.camera_up)};
            if (!result.in_front)
                direction *= -1.0f;
        } else if (!result.in_front) {
            direction *= -1.0f;
        }
        if (direction.nearly_zero())
            direction = {0.0f, -1.0f};

        result.direction = direction.normalized();
        result.clamped_screen = direction_to_screen_edge(result.direction, edge_padding, frame.screen_size);
        result.valid = true;
        return result;
    }

    bool resolve_camera_snapshot(FrameProjectionCache &frame) {
        if (frame.snapshot_resolved)
            return frame.snapshot_valid;
        frame.snapshot_resolved = true;
        frame.default_camera = default_camera(frame);
        if (!static_cast<bool>(frame.default_camera))
            return false;

        const ImVec2 display = ImGui::GetIO().DisplaySize;
        frame.screen_size = {display.x > 0.0f ? display.x : 0.0f, display.y > 0.0f ? display.y : 0.0f};
        if (frame.screen_size.x <= 0.0f || frame.screen_size.y <= 0.0f) {
            const int width = frame.default_camera.pixelWidth();
            const int height = frame.default_camera.pixelHeight();
            frame.screen_size = {static_cast<float>(width > 0 ? width : 0),
                                 static_cast<float>(height > 0 ? height : 0)};
        }
        if (frame.screen_size.x <= 0.0f || frame.screen_size.y <= 0.0f)
            return false;
        frame.screen_center = frame.screen_size * 0.5f;

        const Transform camera_transform = frame.default_camera.transform();
        if (static_cast<bool>(camera_transform)) {
            frame.camera_position = camera_transform.position();
            frame.camera_forward = camera_transform.forward().normalized();
            frame.camera_right = camera_transform.right().normalized();
            frame.camera_up = camera_transform.up().normalized();
            frame.have_camera_basis = true;
        }
        frame.snapshot_valid = true;
        return true;
    }

    static Vector2 direction_to_screen_edge(Vector2 direction, float padding, Vector2 screen_size) {
        const Vector2 center = screen_size * 0.5f;
        Vector2 normalized = direction.normalized();
        if (normalized.nearly_zero())
            normalized = {0.0f, -1.0f};
        const float half_width = std::max(1.0f, screen_size.x * 0.5f - padding);
        const float half_height = std::max(1.0f, screen_size.y * 0.5f - padding);
        float x_scale = 1000000.0f;
        float y_scale = 1000000.0f;
        if (std::fabs(normalized.x) > 0.000001f)
            x_scale = half_width / std::fabs(normalized.x);
        if (std::fabs(normalized.y) > 0.000001f)
            y_scale = half_height / std::fabs(normalized.y);
        return center + normalized * std::min(x_scale, y_scale);
    }

    bool fail(Entry &entry, bool persistent) {
        if (persistent) {
            if (entry.failed_frames < 255)
                ++entry.failed_frames;
            if (entry.failed_frames >= 16)
                entry.enabled = false;
        }
        return false;
    }

    void reset_failure(Entry &entry) {
        entry.failed_frames = 0;
    }

    void erase_at(std::size_t idx) {
        if (idx >= entries_.size())
            return;
        const HighlightId removed = entries_[idx].id;
        const std::size_t last = entries_.size() - 1;
        if (idx != last) {
            entries_[idx] = entries_[last];
            index_[entries_[idx].id] = idx;
        }
        entries_.pop_back();
        index_.erase(removed);
    }

    static ImVec2 to_imgui(Vector2 value) {
        return ImVec2(value.x, value.y);
    }

    static Vector2 fallback_screen_center() {
        const ImVec2 display = ImGui::GetIO().DisplaySize;
        if (display.x > 0.0f && display.y > 0.0f)
            return {display.x * 0.5f, display.y * 0.5f};
        return Unity::Screen::center();
    }

    Camera default_camera(FrameProjectionCache &frame) {
        if (!frame.default_camera_resolved) {
            const bool refresh =
                !static_cast<bool>(cached_camera_) ||
                frame_index_ - last_camera_resolve_frame_ >= update_policy_.camera_resolve_interval_frames;
            if (refresh) {
                cached_camera_ = Camera::main();
                last_camera_resolve_frame_ = frame_index_;
            }
            frame.default_camera = cached_camera_;
            frame.default_camera_resolved = true;
        }
        return frame.default_camera;
    }

    static bool valid_rect(ImVec2 min, ImVec2 max) {
        return max.x > min.x && max.y > min.y && min.x > -100000.0f && min.y > -100000.0f && max.x < 100000.0f &&
               max.y < 100000.0f;
    }

    static void draw_rect(ImDrawList *draw_list, ImVec2 min, ImVec2 max, const Style &style) {
        if (style.shadow) {
            const ImVec2 shadow(style.shadow_offset, style.shadow_offset);
            const ImVec2 shadow_min(min.x + shadow.x, min.y + shadow.y);
            const ImVec2 shadow_max(max.x + shadow.x, max.y + shadow.y);
            if (style.filled) {
                draw_list->AddRectFilled(shadow_min, shadow_max, style.shadow_color, style.rounding);
            }
            if (style.corner_box) {
                draw_corner_box(draw_list, shadow_min, shadow_max, style.shadow_color, style);
            } else {
                draw_list->AddRect(shadow_min, shadow_max, style.shadow_color, style.rounding, 0, style.thickness);
            }
        }
        if (style.filled) {
            draw_list->AddRectFilled(min, max, style.fill_color, style.rounding);
        }
        if (style.corner_box) {
            draw_corner_box(draw_list, min, max, style.color, style);
        } else {
            draw_list->AddRect(min, max, style.color, style.rounding, 0, style.thickness);
        }
    }

    static void draw_indicator(ImDrawList *draw_list, ImVec2 from, ImVec2 to, const Style &style) {
        draw_list->AddLine(from, to, style.indicator_color, style.indicator_thickness);
    }

    static void draw_indicator(ImDrawList *draw_list, const ResolvedDraw &draw, const Style &style) {
        if (style.indicator_center_dot_radius > 0.0f) {
            draw_list->AddCircleFilled(draw.line_from, style.indicator_center_dot_radius, style.indicator_color);
        }
        draw_indicator(draw_list, draw.line_from, draw.line_to, style);
        draw_list->AddTriangleFilled(draw.line_to, draw.arrow_left, draw.arrow_right, style.indicator_color);
    }

    static void draw_label(ImDrawList *draw_list, const ResolvedDraw &draw, const Style &style) {
        if (!draw.label || !draw.label_text || !draw.label_text[0])
            return;
        const ImVec2 text_size = ImGui::CalcTextSize(draw.label_text);
        ImVec2 min = draw.label_pos;
        if (draw.label_centered)
            min.x -= text_size.x * 0.5f;
        const ImVec2 pad = style.label_padding;
        const ImVec2 bg_min(min.x - pad.x, min.y - pad.y);
        const ImVec2 bg_max(min.x + text_size.x + pad.x, min.y + text_size.y + pad.y);
        if ((style.label_bg_color >> IM_COL32_A_SHIFT) != 0)
            draw_list->AddRectFilled(bg_min, bg_max, style.label_bg_color, style.label_rounding);
        if ((style.label_border_color >> IM_COL32_A_SHIFT) != 0)
            draw_list->AddRect(bg_min, bg_max, style.label_border_color, style.label_rounding);
        draw_list->AddText(min, style.label_color, draw.label_text);
    }

    static const char *debug_state_name(DebugState state) {
        switch (state) {
            case DebugState::Added:
                return "added";
            case DebugState::Rect:
                return "rect";
            case DebugState::Label:
                return "label";
            case DebugState::Indicator:
                return "indicator";
            case DebugState::Offscreen:
                return "offscreen";
            case DebugState::TooClose:
                return "too_close";
            case DebugState::InvalidRect:
                return "invalid_rect";
            case DebugState::MissingTransform:
                return "missing_transform";
            case DebugState::DeadTransform:
                return "dead_transform";
            case DebugState::NoProjection:
                return "no_projection";
            case DebugState::ProjectionFailed:
                return "projection_failed";
            case DebugState::Removed:
                return "removed";
            default:
                return "none";
        }
    }

    void note_state(Entry &entry, DebugState state, const ProjectionResult *projection = nullptr,
                    const char *detail = nullptr) {
        const bool emit = diagnostics_ && (verbose_diagnostics_ || state != entry.last_debug_state ||
                                           frame_index_ - entry.last_debug_frame >= diagnostic_throttle_frames_);
        entry.last_debug_state = state;
        entry.last_debug_frame = frame_index_;
        if (!emit)
            return;

        char buffer[768]{};
        if (projection) {
            std::snprintf(buffer, sizeof(buffer),
                          "[highlight] id=%u state=%s enabled=%s detail=%s world=(%.2f, %.2f, "
                          "%.2f) "
                          "screen=(%.2f, %.2f) clamped=(%.2f, %.2f) depth=%.3f distance=%.3f "
                          "facing=%.3f "
                          "inFront=%s onScreen=%s",
                          entry.id, debug_state_name(state), entry.enabled ? "true" : "false", detail ? detail : "",
                          projection->world.x, projection->world.y, projection->world.z, projection->screen.x,
                          projection->screen.y, projection->clamped_screen.x, projection->clamped_screen.y,
                          projection->depth, projection->distance, projection->facing,
                          projection->in_front ? "true" : "false", projection->on_screen ? "true" : "false");
        } else {
            std::snprintf(buffer, sizeof(buffer), "[highlight] id=%u state=%s enabled=%s detail=%s", entry.id,
                          debug_state_name(state), entry.enabled ? "true" : "false", detail ? detail : "");
        }
        diagnostics_(buffer);
    }

    void log_text(const char *text) const {
        if (diagnostics_ && text && text[0])
            diagnostics_(text);
    }

    static std::string safe_label(const char *label) {
        return (label && label[0]) ? std::string(label) : std::string{};
    }

    static float screen_scale(const Style &style, const ProjectionResult &projection) {
        if (!style.scale_with_distance || projection.distance <= 0.001f)
            return 1.0f;
        const float reference = style.reference_distance > 0.001f ? style.reference_distance : 1.0f;
        const float raw = reference / projection.distance;
        const float min_scale = std::min(style.min_scale, style.max_scale);
        const float max_scale = std::max(style.min_scale, style.max_scale);
        return std::clamp(raw, min_scale, max_scale);
    }

    static void configure_label(ResolvedDraw &draw, const Entry &entry, ImVec2 anchor) {
        if (!entry.style.draw_label || entry.label.empty())
            return;
        draw.label = true;
        draw.label_text = entry.label.c_str();
        draw.label_centered = true;
        draw.label_pos = ImVec2(anchor.x, entry.style.label_above_box
                                              ? anchor.y - ImGui::GetTextLineHeight() -
                                                    (entry.style.label_offset + entry.style.label_padding.y * 2.0f)
                                              : anchor.y + entry.style.label_offset);
    }

    static void draw_corner_box(ImDrawList *draw_list, ImVec2 min, ImVec2 max, ImU32 color, const Style &style) {
        const float len = std::max(2.0f, std::min(style.corner_length, std::min(max.x - min.x, max.y - min.y) * 0.5f));
        const float t = style.thickness;
        draw_list->AddLine(min, ImVec2(min.x + len, min.y), color, t);
        draw_list->AddLine(min, ImVec2(min.x, min.y + len), color, t);
        draw_list->AddLine(ImVec2(max.x - len, min.y), ImVec2(max.x, min.y), color, t);
        draw_list->AddLine(ImVec2(max.x, min.y), ImVec2(max.x, min.y + len), color, t);
        draw_list->AddLine(ImVec2(min.x, max.y - len), ImVec2(min.x, max.y), color, t);
        draw_list->AddLine(ImVec2(min.x, max.y), ImVec2(min.x + len, max.y), color, t);
        draw_list->AddLine(ImVec2(max.x - len, max.y), max, color, t);
        draw_list->AddLine(ImVec2(max.x, max.y - len), max, color, t);
    }

    std::vector<Entry> entries_{};
    std::unordered_map<HighlightId, std::size_t> index_{};
    std::atomic<HighlightId> next_id_{1};
    std::mutex pending_mutex_{};
    std::vector<PendingCommand> pending_{};
    std::size_t update_cursor_ = 0;
    std::uint32_t frame_index_ = 0;
    std::uint32_t diagnostic_throttle_frames_ = 60;
    std::uint32_t last_camera_resolve_frame_ = 0;
    bool verbose_diagnostics_ = false;
    UpdatePolicy update_policy_{};
    FrameStats frame_stats_{};
    Camera cached_camera_{};
    ProjectWorldInfoFn projector_info_ = nullptr;
    void *projector_user_ = nullptr;
    DiagnosticSink diagnostics_ = nullptr;
};

inline Manager &manager() {
    static Manager instance;
    return instance;
}

inline HighlightId add(GameObject object, Style style = {}) {
    return manager().add(object, style);
}

inline HighlightId add(GameObject object, const char *label, Style style = {}) {
    return manager().add(object, label, style);
}

inline HighlightId add(Component component, Style style = {}) {
    return manager().add(component, style);
}

inline HighlightId add(Component component, const char *label, Style style = {}) {
    return manager().add(component, label, style);
}

inline HighlightId add(Transform transform, Style style = {}) {
    return manager().add(transform, style);
}

inline HighlightId add(Transform transform, const char *label, Style style = {}) {
    return manager().add(transform, label, style);
}

// Thread-safe variants for callers outside the render hook.
inline HighlightId enqueue_add(GameObject object, Style style = {}) {
    return manager().enqueue_add(object, style);
}

inline HighlightId enqueue_add(GameObject object, const char *label, Style style = {}) {
    return manager().enqueue_add(object, label, style);
}

inline HighlightId enqueue_add(Component component, Style style = {}) {
    return manager().enqueue_add(component, style);
}

inline HighlightId enqueue_add(Component component, const char *label, Style style = {}) {
    return manager().enqueue_add(component, label, style);
}

inline HighlightId enqueue_add(Transform transform, Style style = {}) {
    return manager().enqueue_add(transform, style);
}

inline HighlightId enqueue_add(Transform transform, const char *label, Style style = {}) {
    return manager().enqueue_add(transform, label, style);
}

inline HighlightId add_world_point(Vector3 world, Style style = {}) {
    return manager().add_world_point(world, style);
}

inline HighlightId add_world_point(Vector3 world, const char *label, Style style = {}) {
    return manager().add_world_point(world, label, style);
}

inline HighlightId enqueue_add_world_point(Vector3 world, Style style = {}) {
    return manager().enqueue_add_world_point(world, style);
}

inline HighlightId enqueue_add_world_point(Vector3 world, const char *label, Style style = {}) {
    return manager().enqueue_add_world_point(world, label, style);
}

inline HighlightId add_screen_rect(ImVec2 min, ImVec2 max, Style style = {}) {
    return manager().add_screen_rect(min, max, style);
}

inline HighlightId add_screen_rect(ImVec2 min, ImVec2 max, const char *label, Style style = {}) {
    return manager().add_screen_rect(min, max, label, style);
}

inline HighlightId add_screen_rect(Vector2 min, Vector2 max, Style style = {}) {
    return manager().add_screen_rect(min, max, style);
}

inline HighlightId add_screen_rect(Vector2 min, Vector2 max, const char *label, Style style = {}) {
    return manager().add_screen_rect(min, max, label, style);
}

inline bool remove(HighlightId id) {
    return manager().remove(id);
}

inline void clear() {
    manager().clear();
}

inline void set_label(HighlightId id, const char *label) {
    manager().set_label(id, label);
}

inline void mark_dirty(HighlightId id) {
    manager().mark_dirty(id);
}

inline void mark_all_dirty() {
    manager().mark_all_dirty();
}

inline void enqueue_mark_dirty(HighlightId id) {
    manager().enqueue_mark_dirty(id);
}

inline void enqueue_mark_all_dirty() {
    manager().enqueue_mark_all_dirty();
}

inline void enqueue_set_world_point(HighlightId id, Vector3 world) {
    manager().enqueue_set_world_point(id, world);
}

inline void enqueue_remove(HighlightId id) {
    manager().enqueue_remove(id);
}

inline void enqueue_clear() {
    manager().enqueue_clear();
}

inline void set_update_policy(UpdatePolicy policy) {
    manager().set_update_policy(policy);
}

inline UpdatePolicy update_policy() {
    return manager().update_policy();
}

inline FrameStats last_frame_stats() {
    return manager().last_frame_stats();
}

inline void set_projector_info(ProjectWorldInfoFn projector, void *user = nullptr) {
    manager().set_projector_info(projector, user);
}

inline void set_diagnostics(DiagnosticSink sink) {
    manager().set_diagnostics(sink);
}

inline void set_verbose_diagnostics(bool enabled) {
    manager().set_verbose_diagnostics(enabled);
}

inline void set_diagnostic_throttle_frames(std::uint32_t frames) {
    manager().set_diagnostic_throttle_frames(frames);
}
} // namespace ModUI::Highlight
)URK";
}

std::string Dx11ViewportSwapChainHeaderModule() {
    return R"URK(#pragma once

#include <optional>

#include <dxgi.h>

namespace ModRenderHook {

struct Dx11ViewportSwapChainConfig {
    DXGI_SWAP_CHAIN_DESC descriptor{};
    bool flipModel = false;
};

[[nodiscard]] std::optional<Dx11ViewportSwapChainConfig> make_dx11_viewport_swap_chain_config(
    const DXGI_SWAP_CHAIN_DESC &gameDescriptor);

[[nodiscard]] const char *dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect);

} // namespace ModRenderHook
)URK";
}

std::string Dx11ViewportSwapChainSourceModule() {
    return R"URK(#include "dx11_viewport_swap_chain.h"

#include <algorithm>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool is_known_swap_effect(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] bool is_flip_model(DXGI_SWAP_EFFECT effect) {
    return effect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || effect == DXGI_SWAP_EFFECT_FLIP_DISCARD;
}

} // namespace

std::optional<Dx11ViewportSwapChainConfig> make_dx11_viewport_swap_chain_config(
    const DXGI_SWAP_CHAIN_DESC &gameDescriptor) {
    if (!is_known_swap_effect(gameDescriptor.SwapEffect) || gameDescriptor.BufferDesc.Format == DXGI_FORMAT_UNKNOWN) {
        return std::nullopt;
    }

    Dx11ViewportSwapChainConfig config{};
    config.flipModel = is_flip_model(gameDescriptor.SwapEffect);

    DXGI_SWAP_CHAIN_DESC &viewport = config.descriptor;
    viewport.BufferDesc.Width = 0;
    viewport.BufferDesc.Height = 0;
    viewport.BufferDesc.RefreshRate = {0, 1};
    viewport.BufferDesc.Format = gameDescriptor.BufferDesc.Format;
    viewport.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    viewport.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    viewport.SampleDesc = {1, 0};
    viewport.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    viewport.BufferCount = config.flipModel ? (std::clamp)(gameDescriptor.BufferCount, 2u, 16u)
                                            : (std::clamp)(gameDescriptor.BufferCount, 1u, 16u);
    viewport.OutputWindow = nullptr;
    viewport.Windowed = TRUE;
    viewport.SwapEffect = gameDescriptor.SwapEffect;
    viewport.Flags = 0;
    return config;
}

const char *dxgi_swap_effect_name(DXGI_SWAP_EFFECT effect) {
    switch (effect) {
        case DXGI_SWAP_EFFECT_DISCARD:
            return "DISCARD";
        case DXGI_SWAP_EFFECT_SEQUENTIAL:
            return "SEQUENTIAL";
        case DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL:
            return "FLIP_SEQUENTIAL";
        case DXGI_SWAP_EFFECT_FLIP_DISCARD:
            return "FLIP_DISCARD";
        default:
            return "UNKNOWN";
    }
}

} // namespace ModRenderHook
)URK";
}

std::string Dx11StateGuardHeaderModule() {
    return R"URK(#pragma once
#include <array>
#include <d3d11.h>

namespace ModRenderHook {
class Dx11OutputMergerStateGuard final {
  public:
    explicit Dx11OutputMergerStateGuard(ID3D11DeviceContext *context) noexcept;
    ~Dx11OutputMergerStateGuard();

    Dx11OutputMergerStateGuard(const Dx11OutputMergerStateGuard &) = delete;
    Dx11OutputMergerStateGuard &operator=(const Dx11OutputMergerStateGuard &) = delete;

  private:
    ID3D11DeviceContext *context_{};
    std::array<ID3D11RenderTargetView *, D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> render_targets_{};
    ID3D11DepthStencilView *depth_stencil_{};
};
} // namespace ModRenderHook
)URK";
}

std::string Dx11StateGuardSourceModule() {
    return R"URK(#include "dx11_state_guard.h"

namespace ModRenderHook {
Dx11OutputMergerStateGuard::Dx11OutputMergerStateGuard(ID3D11DeviceContext *context) noexcept : context_(context) {
    if (context_)
        context_->OMGetRenderTargets(static_cast<UINT>(render_targets_.size()), render_targets_.data(),
                                     &depth_stencil_);
}

Dx11OutputMergerStateGuard::~Dx11OutputMergerStateGuard() {
    if (!context_)
        return;

    context_->OMSetRenderTargets(static_cast<UINT>(render_targets_.size()), render_targets_.data(), depth_stencil_);
    for (ID3D11RenderTargetView *render_target : render_targets_)
        if (render_target)
            render_target->Release();
    if (depth_stencil_)
        depth_stencil_->Release();
}
} // namespace ModRenderHook
)URK";
}

std::string Dx12OverlayResourcesHeaderModule() {
    return R"URK(#pragma once

#include <array>
#include <cstdint>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <backends/imgui_impl_dx12.h>
#include <d3d12.h>
#include <dxgi1_4.h>

namespace ModRenderHook {

enum class Dx12BeginFrameStatus {
    ready,
    gpu_busy,
    unavailable,
    reset_failed,
};

struct Dx12FrameSubmission {
    ID3D12GraphicsCommandList *commandList{};
    ID3D12Resource *backBuffer{};
    D3D12_CPU_DESCRIPTOR_HANDLE renderTarget{};
    UINT frameIndex{};
};

class Dx12OverlayResources final {
  public:
    using DiagnosticSink = void (*)(const char *message);

    Dx12OverlayResources() = default;
    ~Dx12OverlayResources();

    Dx12OverlayResources(const Dx12OverlayResources &) = delete;
    Dx12OverlayResources &operator=(const Dx12OverlayResources &) = delete;

    void set_diagnostic_sink(DiagnosticSink sink) noexcept;
    [[nodiscard]] bool capture_command_queue(ID3D12CommandQueue *queue) noexcept;
    [[nodiscard]] bool has_command_queue() const noexcept;
    [[nodiscard]] bool create(IDXGISwapChain *swapChain) noexcept;
    [[nodiscard]] bool wait_for_idle() noexcept;
    void release_device_objects() noexcept;
    void shutdown() noexcept;

    [[nodiscard]] Dx12BeginFrameStatus begin_frame(Dx12FrameSubmission *submission) noexcept;
    [[nodiscard]] bool submit_frame(const Dx12FrameSubmission &submission) noexcept;
    [[nodiscard]] bool complete_frame(const Dx12FrameSubmission &submission) noexcept;

    [[nodiscard]] ID3D12Device *device() const noexcept;
    [[nodiscard]] ID3D12CommandQueue *command_queue() const noexcept;
    [[nodiscard]] ID3D12DescriptorHeap *srv_heap() const noexcept;
    [[nodiscard]] DXGI_FORMAT format() const noexcept;
    [[nodiscard]] int frame_count() const noexcept;

    static void allocate_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                                        D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle);
    static void free_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

  private:
    struct FrameContext {
        ID3D12CommandAllocator *allocator{};
        ID3D12Resource *backBuffer{};
        UINT64 fenceValue{};
    };

    static constexpr UINT kSrvDescriptorCapacity = 256;
    static Dx12OverlayResources *descriptorOwner_;

    [[noreturn]] void descriptor_failure(const char *message) const;
    void report(const char *message) const noexcept;

    DiagnosticSink diagnosticSink_{};
    ID3D12Device *device_{};
    ID3D12CommandQueue *commandQueue_{};
    IDXGISwapChain3 *swapChain_{};
    ID3D12DescriptorHeap *rtvHeap_{};
    ID3D12DescriptorHeap *srvHeap_{};
    ID3D12GraphicsCommandList *commandList_{};
    ID3D12Fence *fence_{};
    HANDLE fenceEvent_{};
    UINT rtvDescriptorSize_{};
    UINT srvDescriptorSize_{};
    UINT64 nextFenceValue_{1};
    DXGI_FORMAT format_{DXGI_FORMAT_UNKNOWN};
    bool synchronizationLost_{};
    std::array<bool, kSrvDescriptorCapacity> srvDescriptors_{};
    std::vector<FrameContext> frames_;
};

} // namespace ModRenderHook
)URK";
}

std::string Dx12OverlayResourcesSourceModule() {
    return R"URK(#include "dx12_overlay_resources.h"

#include <algorithm>
#include <exception>

namespace ModRenderHook {

Dx12OverlayResources *Dx12OverlayResources::descriptorOwner_ = nullptr;

Dx12OverlayResources::~Dx12OverlayResources() {
    shutdown();
}

void Dx12OverlayResources::set_diagnostic_sink(DiagnosticSink sink) noexcept {
    diagnosticSink_ = sink;
}

void Dx12OverlayResources::report(const char *message) const noexcept {
    if (diagnosticSink_)
        diagnosticSink_(message);
}

[[noreturn]] void Dx12OverlayResources::descriptor_failure(const char *message) const {
    report(message);
    std::terminate();
}

bool Dx12OverlayResources::capture_command_queue(ID3D12CommandQueue *queue) noexcept {
    if (!queue || queue->GetDesc().Type != D3D12_COMMAND_LIST_TYPE_DIRECT || commandQueue_)
        return false;
    queue->AddRef();
    commandQueue_ = queue;
    return true;
}

bool Dx12OverlayResources::has_command_queue() const noexcept {
    return commandQueue_ != nullptr;
}

bool Dx12OverlayResources::create(IDXGISwapChain *swapChain) noexcept {
    if (!swapChain || !commandQueue_ || device_ || !frames_.empty())
        return false;

    if (FAILED(swapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void **>(&device_))) || !device_ ||
        FAILED(swapChain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&swapChain_))) ||
        !swapChain_) {
        report("DX12 swap chain, device, or command queue unavailable; UI resources were not created.");
        release_device_objects();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC descriptor{};
    if (FAILED(swapChain->GetDesc(&descriptor)) || descriptor.BufferCount == 0 ||
        descriptor.BufferDesc.Format == DXGI_FORMAT_UNKNOWN) {
        report("DX12 swap chain description is incomplete; UI resources were not created.");
        release_device_objects();
        return false;
    }
    format_ = descriptor.BufferDesc.Format;
    synchronizationLost_ = false;
    rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    srvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDescriptor{};
    rtvDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDescriptor.NumDescriptors = descriptor.BufferCount;
    if (FAILED(device_->CreateDescriptorHeap(&rtvDescriptor, __uuidof(ID3D12DescriptorHeap),
                                              reinterpret_cast<void **>(&rtvHeap_))) ||
        !rtvHeap_) {
        report("DX12 RTV descriptor heap creation failed.");
        release_device_objects();
        return false;
    }

    D3D12_DESCRIPTOR_HEAP_DESC srvDescriptor{};
    srvDescriptor.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvDescriptor.NumDescriptors = kSrvDescriptorCapacity;
    srvDescriptor.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (FAILED(device_->CreateDescriptorHeap(&srvDescriptor, __uuidof(ID3D12DescriptorHeap),
                                              reinterpret_cast<void **>(&srvHeap_))) ||
        !srvHeap_) {
        report("DX12 shader-visible descriptor heap creation failed.");
        release_device_objects();
        return false;
    }

    frames_.resize(descriptor.BufferCount);
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    for (UINT index = 0; index < descriptor.BufferCount; ++index) {
        FrameContext &frame = frames_[index];
        if (FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   __uuidof(ID3D12CommandAllocator),
                                                   reinterpret_cast<void **>(&frame.allocator))) ||
            !frame.allocator ||
            FAILED(swapChain_->GetBuffer(index, __uuidof(ID3D12Resource),
                                         reinterpret_cast<void **>(&frame.backBuffer))) ||
            !frame.backBuffer) {
            report("DX12 frame resource creation failed.");
            release_device_objects();
            return false;
        }
        device_->CreateRenderTargetView(frame.backBuffer, nullptr, rtv);
        rtv.ptr += rtvDescriptorSize_;
    }

    if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, frames_.front().allocator, nullptr,
                                          __uuidof(ID3D12GraphicsCommandList),
                                          reinterpret_cast<void **>(&commandList_))) ||
        !commandList_ || FAILED(commandList_->Close()) ||
        FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                    reinterpret_cast<void **>(&fence_))) ||
        !fence_) {
        report("DX12 command-list or fence creation failed.");
        release_device_objects();
        return false;
    }

    fenceEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!fenceEvent_) {
        report("DX12 fence event creation failed.");
        release_device_objects();
        return false;
    }

    descriptorOwner_ = this;
    return true;
}

bool Dx12OverlayResources::wait_for_idle() noexcept {
    if (synchronizationLost_)
        return false;
    if (!fence_ || !fenceEvent_)
        return true;
    const UINT64 lastSubmitted = nextFenceValue_ > 1 ? nextFenceValue_ - 1 : 0;
    if (lastSubmitted == 0 || fence_->GetCompletedValue() >= lastSubmitted)
        return true;
    if (FAILED(fence_->SetEventOnCompletion(lastSubmitted, fenceEvent_))) {
        report("DX12 fence event registration failed while waiting for overlay resources.");
        return false;
    }
    if (WaitForSingleObject(fenceEvent_, INFINITE) != WAIT_OBJECT_0) {
        report("DX12 fence wait failed while draining overlay resources.");
        return false;
    }
    return true;
}

void Dx12OverlayResources::release_device_objects() noexcept {
    for (FrameContext &frame : frames_) {
        if (frame.backBuffer)
            frame.backBuffer->Release();
        if (frame.allocator)
            frame.allocator->Release();
    }
    frames_.clear();
    if (fenceEvent_) {
        CloseHandle(fenceEvent_);
        fenceEvent_ = nullptr;
    }
    if (fence_) {
        fence_->Release();
        fence_ = nullptr;
    }
    if (commandList_) {
        commandList_->Release();
        commandList_ = nullptr;
    }
    if (srvHeap_) {
        srvHeap_->Release();
        srvHeap_ = nullptr;
    }
    if (rtvHeap_) {
        rtvHeap_->Release();
        rtvHeap_ = nullptr;
    }
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }
    rtvDescriptorSize_ = 0;
    srvDescriptorSize_ = 0;
    nextFenceValue_ = 1;
    format_ = DXGI_FORMAT_UNKNOWN;
    synchronizationLost_ = false;
    srvDescriptors_.fill(false);
}

void Dx12OverlayResources::shutdown() noexcept {
    if (device_ || !frames_.empty())
        (void)wait_for_idle();
    release_device_objects();
    if (commandQueue_) {
        commandQueue_->Release();
        commandQueue_ = nullptr;
    }
    if (descriptorOwner_ == this)
        descriptorOwner_ = nullptr;
}

Dx12BeginFrameStatus Dx12OverlayResources::begin_frame(Dx12FrameSubmission *submission) noexcept {
    if (submission)
        *submission = {};
    if (!submission || synchronizationLost_ || !swapChain_ || !commandList_ || !fence_ || frames_.empty())
        return Dx12BeginFrameStatus::unavailable;

    const UINT frameIndex = swapChain_->GetCurrentBackBufferIndex();
    if (frameIndex >= frames_.size())
        return Dx12BeginFrameStatus::unavailable;
    FrameContext &frame = frames_[frameIndex];
    if (frame.fenceValue != 0 && fence_->GetCompletedValue() < frame.fenceValue)
        return Dx12BeginFrameStatus::gpu_busy;
    if (FAILED(frame.allocator->Reset()) || FAILED(commandList_->Reset(frame.allocator, nullptr)))
        return Dx12BeginFrameStatus::reset_failed;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
    rtv.ptr += static_cast<SIZE_T>(frameIndex) * rtvDescriptorSize_;
    *submission = {commandList_, frame.backBuffer, rtv, frameIndex};
    return Dx12BeginFrameStatus::ready;
}

bool Dx12OverlayResources::submit_frame(const Dx12FrameSubmission &submission) noexcept {
    if (!commandQueue_ || !submission.commandList || submission.frameIndex >= frames_.size() ||
        FAILED(submission.commandList->Close())) {
        return false;
    }
    ID3D12CommandList *commandLists[] = {submission.commandList};
    commandQueue_->ExecuteCommandLists(1, commandLists);
    return true;
}

bool Dx12OverlayResources::complete_frame(const Dx12FrameSubmission &submission) noexcept {
    if (!commandQueue_ || !fence_ || submission.frameIndex >= frames_.size())
        return false;
    const UINT64 fenceValue = nextFenceValue_++;
    if (FAILED(commandQueue_->Signal(fence_, fenceValue))) {
        synchronizationLost_ = true;
        report("DX12 overlay fence signal failed; frame resources will not be reused.");
        return false;
    }
    frames_[submission.frameIndex].fenceValue = fenceValue;
    return true;
}

ID3D12Device *Dx12OverlayResources::device() const noexcept {
    return device_;
}

ID3D12CommandQueue *Dx12OverlayResources::command_queue() const noexcept {
    return commandQueue_;
}

ID3D12DescriptorHeap *Dx12OverlayResources::srv_heap() const noexcept {
    return srvHeap_;
}

DXGI_FORMAT Dx12OverlayResources::format() const noexcept {
    return format_;
}

int Dx12OverlayResources::frame_count() const noexcept {
    return static_cast<int>(frames_.size());
}

void Dx12OverlayResources::allocate_srv_descriptor(ImGui_ImplDX12_InitInfo *,
                                                    D3D12_CPU_DESCRIPTOR_HANDLE *cpuHandle,
                                                    D3D12_GPU_DESCRIPTOR_HANDLE *gpuHandle) {
    Dx12OverlayResources *owner = descriptorOwner_;
    if (!owner || !cpuHandle || !gpuHandle || !owner->srvHeap_ || !owner->srvDescriptorSize_)
        owner ? owner->descriptor_failure("DX12 ImGui SRV descriptor allocation received invalid state.")
              : std::terminate();

    for (UINT index = 0; index < kSrvDescriptorCapacity; ++index) {
        if (owner->srvDescriptors_[index])
            continue;
        owner->srvDescriptors_[index] = true;
        *cpuHandle = owner->srvHeap_->GetCPUDescriptorHandleForHeapStart();
        *gpuHandle = owner->srvHeap_->GetGPUDescriptorHandleForHeapStart();
        cpuHandle->ptr += static_cast<SIZE_T>(index) * owner->srvDescriptorSize_;
        gpuHandle->ptr += static_cast<UINT64>(index) * owner->srvDescriptorSize_;
        return;
    }
    owner->descriptor_failure("DX12 ImGui exhausted its 256-entry shader-visible SRV descriptor heap.");
}

void Dx12OverlayResources::free_srv_descriptor(ImGui_ImplDX12_InitInfo *, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                                D3D12_GPU_DESCRIPTOR_HANDLE) {
    Dx12OverlayResources *owner = descriptorOwner_;
    if (!owner || !owner->srvHeap_ || !owner->srvDescriptorSize_)
        owner ? owner->descriptor_failure("DX12 ImGui SRV descriptor release received invalid state.")
              : std::terminate();

    const SIZE_T first = owner->srvHeap_->GetCPUDescriptorHandleForHeapStart().ptr;
    if (cpuHandle.ptr < first)
        owner->descriptor_failure("DX12 ImGui attempted to release an invalid SRV descriptor.");
    const SIZE_T offset = cpuHandle.ptr - first;
    if (offset % owner->srvDescriptorSize_ != 0)
        owner->descriptor_failure("DX12 ImGui attempted to release an unaligned SRV descriptor.");
    const SIZE_T index = offset / owner->srvDescriptorSize_;
    if (index >= kSrvDescriptorCapacity || !owner->srvDescriptors_[index])
        owner->descriptor_failure("DX12 ImGui attempted to release an unknown SRV descriptor.");
    owner->srvDescriptors_[index] = false;
}

} // namespace ModRenderHook
)URK";
}

std::string DxgiHookDiscoveryHeaderModule() {
    return R"URK(#pragma once

namespace ModRenderHook {

struct DxgiVTableTargets {
    void *present{};
    void *present1{};
    void *resizeBuffers{};
};

using DxgiDiscoveryDiagnosticSink = void (*)(const char *message);

[[nodiscard]] DxgiVTableTargets discover_dxgi_hook_targets(bool preferDx12,
                                                            DxgiDiscoveryDiagnosticSink diagnosticSink);
[[nodiscard]] void *discover_dx12_execute_command_lists_target(DxgiDiscoveryDiagnosticSink diagnosticSink);

} // namespace ModRenderHook
)URK";
}

std::string DxgiHookDiscoverySourceModule() {
    return R"URK(#include "dxgi_hook_discovery.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <iterator>

namespace ModRenderHook {
namespace {

void report(DxgiDiscoveryDiagnosticSink sink, const char *message) {
    if (sink)
        sink(message);
}

[[nodiscard]] bool readable_range(const void *pointer, std::size_t bytes) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!pointer || bytes == 0 || VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    return address >= base && bytes <= (base + memory.RegionSize) - address;
}

[[nodiscard]] bool executable(const void *pointer) {
    MEMORY_BASIC_INFORMATION memory{};
    if (!pointer || VirtualQuery(pointer, &memory, sizeof(memory)) != sizeof(memory) ||
        memory.State != MEM_COMMIT || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const DWORD protection = memory.Protect & 0xff;
    return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
           protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

[[nodiscard]] bool capture_targets(IDXGISwapChain *swapChain, const char *probeName,
                                   DxgiVTableTargets *targets, DxgiDiscoveryDiagnosticSink sink) {
    if (!swapChain || !targets || !readable_range(swapChain, sizeof(void *))) {
        report(sink, "DXGI probe did not return a readable swap chain.");
        return false;
    }
    void **vtable = *reinterpret_cast<void ***>(swapChain);
    if (!readable_range(vtable, sizeof(void *) * 23)) {
        report(sink, "DXGI probe swap-chain vtable is unreadable; UI not installed.");
        return false;
    }
    targets->present = executable(vtable[8]) ? vtable[8] : nullptr;
    targets->present1 = executable(vtable[22]) ? vtable[22] : nullptr;
    targets->resizeBuffers = executable(vtable[13]) ? vtable[13] : nullptr;
    if ((targets->present || targets->present1) && targets->resizeBuffers)
        return true;

    char text[160]{};
    std::snprintf(text, sizeof(text), "%s probe found non-executable swap-chain hook targets; UI not installed.",
                  probeName ? probeName : "DXGI");
    report(sink, text);
    return false;
}

class ProbeWindow final {
  public:
    explicit ProbeWindow(bool preferDx12) {
        std::snprintf(className_, sizeof(className_), "URK_%s_Probe_%lu_%lu_%lu", preferDx12 ? "DX12" : "DX11",
                      GetCurrentProcessId(), GetCurrentThreadId(), GetTickCount());
        windowClass_ = {sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr),
                        nullptr, nullptr, nullptr, nullptr, className_, nullptr};
        registered_ = RegisterClassExA(&windowClass_) != 0;
        if (registered_) {
            window_ = CreateWindowA(className_, className_, WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr,
                                    windowClass_.hInstance, nullptr);
        }
    }

    ~ProbeWindow() {
        if (window_)
            DestroyWindow(window_);
        if (registered_)
            UnregisterClassA(className_, windowClass_.hInstance);
    }

    [[nodiscard]] HWND get() const noexcept { return window_; }
    [[nodiscard]] bool registered() const noexcept { return registered_; }

  private:
    char className_[96]{};
    WNDCLASSEXA windowClass_{};
    HWND window_{};
    bool registered_{};
};

} // namespace

DxgiVTableTargets discover_dxgi_hook_targets(bool preferDx12, DxgiDiscoveryDiagnosticSink sink) {
    DxgiVTableTargets targets{};
    ProbeWindow probe(preferDx12);
    if (!probe.registered()) {
        report(sink, "DXGI probe window class registration failed; UI not installed.");
        return targets;
    }
    if (!probe.get()) {
        report(sink, "DXGI probe window creation failed; UI not installed.");
        return targets;
    }

    if (preferDx12) {
        ID3D12Device *device = nullptr;
        ID3D12CommandQueue *queue = nullptr;
        IDXGIFactory4 *factory = nullptr;
        IDXGISwapChain1 *swapChain1 = nullptr;
        IDXGISwapChain *swapChain = nullptr;
        HRESULT result = D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                           reinterpret_cast<void **>(&device));
        if (SUCCEEDED(result) && device) {
            D3D12_COMMAND_QUEUE_DESC queueDescriptor{};
            queueDescriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            result = device->CreateCommandQueue(&queueDescriptor, __uuidof(ID3D12CommandQueue),
                                                reinterpret_cast<void **>(&queue));
        }
        if (SUCCEEDED(result) && queue)
            result = CreateDXGIFactory1(__uuidof(IDXGIFactory4), reinterpret_cast<void **>(&factory));
        if (SUCCEEDED(result) && factory) {
            DXGI_SWAP_CHAIN_DESC1 descriptor{};
            descriptor.Width = 100;
            descriptor.Height = 100;
            descriptor.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            descriptor.SampleDesc.Count = 1;
            descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            descriptor.BufferCount = 2;
            descriptor.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            result = factory->CreateSwapChainForHwnd(queue, probe.get(), &descriptor, nullptr, nullptr, &swapChain1);
        }
        if (SUCCEEDED(result) && swapChain1)
            result = swapChain1->QueryInterface(__uuidof(IDXGISwapChain), reinterpret_cast<void **>(&swapChain));
        if (SUCCEEDED(result) && swapChain) {
            (void)capture_targets(swapChain, "DX12", &targets, sink);
        } else {
            char text[160]{};
            std::snprintf(text, sizeof(text), "DX12 probe swap-chain creation failed (hr=0x%08X); UI not installed.",
                          static_cast<unsigned>(result));
            report(sink, text);
        }
        if (swapChain)
            swapChain->Release();
        if (swapChain1)
            swapChain1->Release();
        if (factory)
            factory->Release();
        if (queue)
            queue->Release();
        if (device)
            device->Release();
        return targets;
    }

    DXGI_SWAP_CHAIN_DESC descriptor{};
    descriptor.BufferCount = 1;
    descriptor.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    descriptor.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    descriptor.OutputWindow = probe.get();
    descriptor.SampleDesc.Count = 1;
    descriptor.Windowed = TRUE;
    descriptor.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};
    constexpr D3D_DRIVER_TYPE driverTypes[] = {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP};

    ID3D11Device *device = nullptr;
    ID3D11DeviceContext *context = nullptr;
    IDXGISwapChain *swapChain = nullptr;
    D3D_FEATURE_LEVEL createdLevel{};
    HRESULT result = E_FAIL;
    for (D3D_DRIVER_TYPE driverType : driverTypes) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, driverType, nullptr, 0, featureLevels,
                                              static_cast<UINT>(std::size(featureLevels)), D3D11_SDK_VERSION,
                                              &descriptor, &swapChain, &device, &createdLevel, &context);
        if (SUCCEEDED(result) && swapChain)
            break;
        if (swapChain) {
            swapChain->Release();
            swapChain = nullptr;
        }
        if (context) {
            context->Release();
            context = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
    }
    if (SUCCEEDED(result) && swapChain) {
        (void)capture_targets(swapChain, "DX11", &targets, sink);
    } else {
        char text[160]{};
        std::snprintf(text, sizeof(text),
                      "DXGI probe device creation failed (hr=0x%08X); DX11/DX12 overlay unavailable.",
                      static_cast<unsigned>(result));
        report(sink, text);
    }
    if (swapChain)
        swapChain->Release();
    if (context)
        context->Release();
    if (device)
        device->Release();
    return targets;
}

void *discover_dx12_execute_command_lists_target(DxgiDiscoveryDiagnosticSink sink) {
    ID3D12Device *device = nullptr;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device),
                                 reinterpret_cast<void **>(&device))) ||
        !device) {
        report(sink, "DX12 probe device creation failed; DX12 overlay will remain unavailable.");
        return nullptr;
    }
    D3D12_COMMAND_QUEUE_DESC descriptor{};
    descriptor.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue *queue = nullptr;
    const HRESULT result =
        device->CreateCommandQueue(&descriptor, __uuidof(ID3D12CommandQueue), reinterpret_cast<void **>(&queue));
    void *target = nullptr;
    if (SUCCEEDED(result) && queue && readable_range(queue, sizeof(void *))) {
        void **vtable = *reinterpret_cast<void ***>(queue);
        if (readable_range(vtable, sizeof(void *) * 11) && executable(vtable[10])) {
            target = vtable[10];
        } else {
            report(sink, "DX12 ExecuteCommandLists target is unreadable or non-executable.");
        }
    } else {
        report(sink, "DX12 probe command queue creation failed; DX12 overlay will remain unavailable.");
    }
    if (queue)
        queue->Release();
    device->Release();
    return target;
}

} // namespace ModRenderHook
)URK";
}

std::string Win32InputCoordinatesHeaderModule() {
    return R"URK(#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

[[nodiscard]] bool client_mouse_to_desktop(HWND window, POINT *position) noexcept;
[[nodiscard]] bool desktop_mouse_to_imgui(HWND window, bool multiViewport, POINT *position) noexcept;

} // namespace ModRenderHook
)URK";
}

std::string Win32InputCoordinatesSourceModule() {
    return R"URK(#include "win32_input_coordinates.h"

namespace ModRenderHook {

bool client_mouse_to_desktop(HWND window, POINT *position) noexcept {
    return window && position && ClientToScreen(window, position) != FALSE;
}

bool desktop_mouse_to_imgui(HWND window, bool multiViewport, POINT *position) noexcept {
    if (!position)
        return false;
    if (multiViewport)
        return true;
    return window && ScreenToClient(window, position) != FALSE;
}

} // namespace ModRenderHook
)URK";
}

std::string Win32MessagePumpHeaderModule() {
    return R"URK(#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {

struct WindowMessagePumpResult {
    std::uint32_t dispatched = 0;
    std::uint32_t foreignThreadWindows = 0;
    bool backlogRemaining = false;
};

[[nodiscard]] WindowMessagePumpResult pump_owned_window_messages(std::span<const HWND> windows,
                                                                 std::size_t messageBudget = 512);

[[nodiscard]] bool is_imgui_platform_window(HWND window);

} // namespace ModRenderHook
)URK";
}

std::string Win32MessagePumpSourceModule() {
    return R"URK(#include "win32_message_pump.h"

#include <algorithm>
#include <vector>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool contains_window(std::span<const HWND> windows, std::size_t end, HWND candidate) {
    const auto finish = windows.begin() + static_cast<std::ptrdiff_t>(end);
    return std::find(windows.begin(), finish, candidate) != finish;
}

} // namespace

WindowMessagePumpResult pump_owned_window_messages(std::span<const HWND> windows, std::size_t messageBudget) {
    WindowMessagePumpResult result{};
    if (windows.empty() || messageBudget == 0)
        return result;

    const DWORD currentThread = GetCurrentThreadId();
    thread_local std::vector<HWND> ownedWindows;
    ownedWindows.clear();
    ownedWindows.reserve(windows.size());

    for (std::size_t index = 0; index < windows.size(); ++index) {
        const HWND window = windows[index];
        if (!window || !IsWindow(window) || contains_window(windows, index, window))
            continue;
        if (GetWindowThreadProcessId(window, nullptr) != currentThread) {
            ++result.foreignThreadWindows;
            continue;
        }
        ownedWindows.push_back(window);
    }

    MSG message{};
    std::size_t remaining = messageBudget;
    bool madeProgress = true;
    while (remaining != 0 && madeProgress) {
        madeProgress = false;
        for (const HWND window : ownedWindows) {
            if (remaining == 0)
                break;
            if (!PeekMessageW(&message, window, 0, 0, PM_REMOVE))
                continue;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            ++result.dispatched;
            --remaining;
            madeProgress = true;
        }
    }

    if (remaining == 0) {
        for (const HWND window : ownedWindows) {
            if (PeekMessageW(&message, window, 0, 0, PM_NOREMOVE)) {
                result.backlogRemaining = true;
                break;
            }
        }
    }
    return result;
}

bool is_imgui_platform_window(HWND window) {
    if (!window || !IsWindow(window))
        return false;
    DWORD processId = 0;
    GetWindowThreadProcessId(window, &processId);
    return processId == GetCurrentProcessId() && GetPropA(window, "IMGUI_CONTEXT") != nullptr;
}

} // namespace ModRenderHook
)URK";
}

std::string RenderHookHeaderModule() {
    return R"URK(#pragma once

struct URK_ModContext;

namespace ModRenderHook {

bool install(const URK_ModContext *context);
bool uninstall();

} // namespace ModRenderHook
)URK";
}

std::string Win32ViewportPolicyHeaderModule() {
    return R"URK(#pragma once
#include <span>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace ModRenderHook {
struct Win32ViewportPolicyResult {
    HWND window{};
    const char *operation{};
    DWORD error{ERROR_SUCCESS};

    [[nodiscard]] explicit operator bool() const noexcept {
        return error == ERROR_SUCCESS;
    }
};

struct Win32ViewportTopology {
    HWND owner{};
    DWORD windowThread{};
    LONG_PTR extendedStyle{};
};

class Win32ViewportPolicy final {
  public:
    [[nodiscard]] Win32ViewportPolicyResult apply(std::span<const HWND> windows, HWND gameWindow);
    void reset() noexcept;

  private:
    struct ConfiguredWindow {
        HWND window{};
        HWND owner{};
    };

    [[nodiscard]] bool is_configured(HWND window, HWND owner) const noexcept;
    void retain_live_windows(std::span<const HWND> windows);

    std::vector<ConfiguredWindow> configuredWindows_;
};

[[nodiscard]] Win32ViewportTopology query_viewport_topology(HWND window) noexcept;
} // namespace ModRenderHook
)URK";
}

std::string Win32ViewportPolicySourceModule() {
    return R"URK(#include "win32_viewport_policy.h"

#include <algorithm>

namespace ModRenderHook {
namespace {

[[nodiscard]] bool contains_window(std::span<const HWND> windows, HWND candidate) {
    return std::find(windows.begin(), windows.end(), candidate) != windows.end();
}

} // namespace

bool Win32ViewportPolicy::is_configured(HWND window, HWND owner) const noexcept {
    return std::any_of(configuredWindows_.begin(), configuredWindows_.end(), [window, owner](const auto &configured) {
        return configured.window == window && configured.owner == owner;
    });
}

void Win32ViewportPolicy::retain_live_windows(std::span<const HWND> windows) {
    std::erase_if(configuredWindows_, [windows](const ConfiguredWindow &configured) {
        return !configured.window || !IsWindow(configured.window) || !contains_window(windows, configured.window);
    });
}

Win32ViewportPolicyResult Win32ViewportPolicy::apply(std::span<const HWND> windows, HWND gameWindow) {
    retain_live_windows(windows);
    Win32ViewportPolicyResult firstFailure{};

    for (const HWND window : windows) {
        if (!window || !IsWindow(window) || is_configured(window, gameWindow))
            continue;

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR currentStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
        DWORD error = GetLastError();
        if (!currentStyle && error != ERROR_SUCCESS) {
            if (firstFailure)
                firstFailure = {window, "GetWindowLongPtrW(GWL_EXSTYLE)", error};
            continue;
        }

        const LONG_PTR desiredStyle =
            (currentStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
        bool frameChanged = false;
        if (desiredStyle != currentStyle) {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previous = SetWindowLongPtrW(window, GWL_EXSTYLE, desiredStyle);
            error = GetLastError();
            if (!previous && error != ERROR_SUCCESS) {
                if (firstFailure)
                    firstFailure = {window, "SetWindowLongPtrW(GWL_EXSTYLE)", error};
                continue;
            }
            frameChanged = true;
        }

        if (gameWindow && GetWindow(window, GW_OWNER) != gameWindow) {
            SetLastError(ERROR_SUCCESS);
            const LONG_PTR previousOwner =
                SetWindowLongPtrW(window, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(gameWindow));
            error = GetLastError();
            if (!previousOwner && error != ERROR_SUCCESS) {
                if (firstFailure)
                    firstFailure = {window, "SetWindowLongPtrW(GWLP_HWNDPARENT)", error};
                continue;
            }
        }

        if (frameChanged && !SetWindowPos(window, nullptr, 0, 0, 0, 0,
                                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER |
                                              SWP_FRAMECHANGED)) {
            if (firstFailure)
                firstFailure = {window, "SetWindowPos(SWP_FRAMECHANGED)", GetLastError()};
            continue;
        }

        configuredWindows_.push_back({window, gameWindow});
    }
    return firstFailure;
}

void Win32ViewportPolicy::reset() noexcept {
    configuredWindows_.clear();
}

Win32ViewportTopology query_viewport_topology(HWND window) noexcept {
    if (!window || !IsWindow(window))
        return {};
    Win32ViewportTopology result{};
    result.owner = GetWindow(window, GW_OWNER);
    result.windowThread = GetWindowThreadProcessId(window, nullptr);
    result.extendedStyle = GetWindowLongPtrW(window, GWL_EXSTYLE);
    return result;
}
} // namespace ModRenderHook
)URK";
}

std::string RenderHookSourceModule() {
    return R"URK(#include "render_imgui_hook.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "config/mod_config.h"
#include <Windows.h>
#include <array>
#include <atomic>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_win32.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <d3d11.h>
#include <d3d12.h>
#include <deque>
#include <dxgi.h>
#include <dxgi1_4.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <limits>
#include <mutex>
#include <vector>
#include <windowsx.h>

void ImGui_ImplDX11_SetSwapChainDescs(const DXGI_SWAP_CHAIN_DESC *descriptorTemplates, int descriptorTemplateCount);

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 5202)
#endif
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "dx11_viewport_swap_chain.h"
#include "dx11_state_guard.h"
#include "dx12_overlay_resources.h"
#include "dxgi_hook_discovery.h"
#include "sdk/hook_api.h"
#include "sdk/runtime_api.h"
#include "ui/highlight.h"
#include "ui/menu.h"
#include "ui/theme.h"
#include "win32_input_coordinates.h"
#include "win32_message_pump.h"
#include "win32_viewport_policy.h"

namespace ModRenderHook {
using PresentFn = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT);
using Present1Fn = HRESULT(__stdcall *)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
using ResizeBuffersFn = HRESULT(__stdcall *)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);
using ExecuteCommandListsFn = void(__stdcall *)(ID3D12CommandQueue *, UINT, ID3D12CommandList *const *);
using WglSwapBuffersFn = BOOL(WINAPI *)(HDC);

enum class GraphicsBackend {
    none,
    dx11,
    dx12,
    opengl,
};

enum class InputEventKind {
    mouse_position,
    mouse_button,
    mouse_wheel,
    focus,
    raw_message,
    release_all_mouse,
    release_all_input,
};

struct InputEvent {
    InputEventKind kind{};
    HWND hwnd{};
    UINT message{};
    WPARAM wparam{};
    LPARAM lparam{};
    float x{};
    float y{};
    int button{-1};
    bool state{};
};

struct KeyboardButtonState {
    bool down{};
    bool system{};
    LPARAM lparam{};
};

struct MouseButtonState {
    bool down{};
    LPARAM lparam{};
};

inline PresentFn g_present = nullptr;
inline Present1Fn g_present1 = nullptr;
inline ResizeBuffersFn g_resize_buffers = nullptr;
inline ExecuteCommandListsFn g_execute_command_lists = nullptr;
inline WglSwapBuffersFn g_wgl_swap_buffers = nullptr;
inline HWND g_hwnd = nullptr;
inline WNDPROC g_old_wndproc = nullptr;
inline bool g_window_message_registered = false;
inline thread_local bool g_broker_callback_active = false;
inline thread_local bool g_broker_callback_handled = false;
inline ID3D11Device *g_device = nullptr;
inline ID3D11DeviceContext *g_context = nullptr;
inline ID3D11RenderTargetView *g_render_target = nullptr;
inline IDXGISwapChain *g_active_swap_chain = nullptr;
inline Dx12OverlayResources g_dx12_resources;
inline std::atomic_bool g_dx12_queue_captured{false};
inline HGLRC g_gl_context = nullptr;
inline GraphicsBackend g_backend = GraphicsBackend::none;
inline bool g_imgui_ready = false;
inline bool g_present_hooked = false;
inline bool g_present1_hooked = false;
inline bool g_resize_hooked = false;
inline bool g_execute_command_lists_hooked = false;
inline bool g_wgl_swap_buffers_hooked = false;
inline std::atomic_bool g_shutting_down{false};
inline std::atomic_uint g_active_frames{0};
inline std::atomic<DWORD> g_render_thread_id{0};
inline thread_local unsigned g_present_depth = 0;
inline thread_local unsigned g_platform_renderer_depth = 0;
inline std::recursive_mutex g_imgui_mutex;
inline std::mutex g_install_mutex;
inline std::mutex g_input_mutex;
inline std::deque<InputEvent> g_input_events;
inline std::vector<HWND> g_platform_windows;
inline Win32ViewportPolicy g_platform_window_policy;
inline bool g_platform_window_topology_logged = false;
inline ULONGLONG g_platform_message_warning_tick = 0;
inline ULONGLONG g_platform_window_policy_warning_tick = 0;
inline std::array<bool, 5> g_wndproc_mouse_down{};
inline std::array<KeyboardButtonState, 256> g_physical_keyboard{};
inline std::array<KeyboardButtonState, 256> g_game_keyboard{};
inline std::array<bool, 256> g_menu_released_keyboard{};
inline std::array<MouseButtonState, 5> g_physical_mouse{};
inline std::array<MouseButtonState, 5> g_game_mouse{};
inline std::array<bool, 5> g_menu_released_mouse{};
inline std::atomic_uint g_menu_toggle_count{0};
inline std::atomic_bool g_menu_visible{false};
inline std::atomic_bool g_imgui_wants_mouse{false};
inline std::atomic_bool g_imgui_wants_keyboard{false};
inline std::atomic_int g_runtime_mouse_capture_requested{-1};
inline std::atomic_int g_menu_input_requested{-1};
inline std::atomic_int g_menu_input_applied{-1};
inline std::atomic_ullong g_menu_input_request_tick{0};
inline bool g_menu_input_open = false;
inline bool g_game_focus_suspended = false;
inline std::atomic_bool g_cursor_lease{false};
inline std::atomic_bool g_native_cursor_owned{false};
inline std::atomic_int g_native_cursor_requested{0};
inline std::atomic_int g_native_cursor_applied{-1};
inline std::atomic_ullong g_native_cursor_request_tick{0};
inline RECT g_saved_cursor_clip{};
inline HCURSOR g_saved_cursor = nullptr;
inline int g_native_cursor_show_balance = 0;
inline POINT g_virtual_mouse_position{};
inline bool g_virtual_mouse_position_valid = false;
inline bool g_raw_mouse_seen = false;
inline DXGI_FORMAT g_back_buffer_format = DXGI_FORMAT_UNKNOWN;
inline bool g_linear_color_space_support_known = false;
inline bool g_linear_color_space_supported = false;
inline bool g_render_target_diagnostics_logged = false;
inline std::uint64_t g_dx12_busy_frame_skips = 0;
inline ULONGLONG g_dx12_busy_diagnostic_tick = 0;
inline const URK::ModContext *g_mod_context = nullptr;
inline bool g_install_callback_registered = false;
inline bool g_install_complete = false;
inline bool g_install_failed = false;
inline std::uint32_t g_graphics_probe_attempts = 0;
inline constexpr std::uint32_t kMaxGraphicsProbeAttempts = 120;
inline DxgiVTableTargets g_cached_dxgi_targets{};
inline bool g_dxgi_targets_discovered = false;

struct PlatformRendererCallbacks {
    void (*create_window)(ImGuiViewport *) = nullptr;
    void (*destroy_window)(ImGuiViewport *) = nullptr;
    void (*set_window_size)(ImGuiViewport *, ImVec2) = nullptr;
    void (*render_window)(ImGuiViewport *, void *) = nullptr;
    void (*swap_buffers)(ImGuiViewport *, void *) = nullptr;
    bool installed = false;
};
inline PlatformRendererCallbacks g_platform_renderer_callbacks{};

struct PlatformRendererGuard {
    PlatformRendererGuard() {
        ++g_platform_renderer_depth;
    }
    ~PlatformRendererGuard() {
        --g_platform_renderer_depth;
    }
};

struct InputRelay {
    UINT message{};
    WPARAM wparam{};
    LPARAM lparam{};
};

struct RenderFrameGuard {
    bool active = false;
    RenderFrameGuard() {
        if (g_shutting_down.load(std::memory_order_acquire))
            return;
        g_active_frames.fetch_add(1, std::memory_order_acq_rel);
        if (g_shutting_down.load(std::memory_order_acquire)) {
            g_active_frames.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        active = true;
        g_render_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
    }
    ~RenderFrameGuard() {
        if (active)
            g_active_frames.fetch_sub(1, std::memory_order_acq_rel);
    }
};

inline LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);
inline std::intptr_t window_message_callback(void *window, std::uint32_t message, std::uintptr_t wparam,
                                             std::intptr_t lparam, int *handled);
inline void install_on_main_thread();

inline void log(const char *text) {
    const URK::ModContext *ctx = URK::context();
    if (ctx && ctx->Log)
        ctx->Log("[%s][ui] %s", ModConfig::display_name, text ? text : "");
}

inline UINT input_relay_message() {
    static const UINT message = RegisterWindowMessageW(L"URKit.GeneratedImGui.InputRelay.v1");
    static const bool failure_logged = [&] {
        if (!message)
            log("RegisterWindowMessage failed for the ImGui input relay.");
        return !message;
    }();
    (void)failure_logged;
    return message;
}

inline UINT menu_cursor_message() {
    static const UINT message = [] {
        char name[256]{};
        const int length = std::snprintf(name, sizeof(name), "URKit.GeneratedImGui.Cursor.%s",
                                         ModConfig::mod_id ? ModConfig::mod_id : "");
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(name)) {
            log("The mod id is too long to register the ImGui cursor message.");
            return UINT{};
        }
        const UINT registered = RegisterWindowMessageA(name);
        if (!registered)
            log("RegisterWindowMessage failed for the ImGui cursor state.");
        return registered;
    }();
    return message;
}

inline UINT menu_input_message() {
    static const UINT message = [] {
        char name[256]{};
        const int length = std::snprintf(name, sizeof(name), "URKit.GeneratedImGui.Input.%s",
                                         ModConfig::mod_id ? ModConfig::mod_id : "");
        if (length < 0 || static_cast<std::size_t>(length) >= sizeof(name)) {
            log("The mod id is too long to register the ImGui input state message.");
            return UINT{};
        }
        const UINT registered = RegisterWindowMessageA(name);
        if (!registered)
            log("RegisterWindowMessage failed for the ImGui input state.");
        return registered;
    }();
    return message;
}

inline bool readable_range(const void *ptr, std::size_t bytes) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (!ptr || bytes == 0 || VirtualQuery(ptr, &mbi, sizeof(mbi)) != sizeof(mbi) || mbi.State != MEM_COMMIT ||
        (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
        return false;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(mbi.BaseAddress);
    const auto address = reinterpret_cast<std::uintptr_t>(ptr);
    return address >= base && bytes <= (base + mbi.RegionSize) - address;
}

inline bool readable(const void *ptr) {
    return readable_range(ptr, 1);
}

inline bool query_swap_chain_desc(IDXGISwapChain *swap_chain, DXGI_SWAP_CHAIN_DESC *desc) {
    if (!swap_chain || !desc)
        return false;
    std::memset(desc, 0, sizeof(*desc));
    return SUCCEEDED(swap_chain->GetDesc(desc));
}

inline bool is_process_main_window(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd || GetWindow(hwnd, GW_OWNER) != nullptr) {
        return false;
    }
    // Dear ImGui marks every secondary Win32 viewport with this property. An
    // injected process can contain several independent ImGui contexts, so a
    // late-installed hook must not adopt another mod's viewport swap chain.
    if (GetPropA(hwnd, "IMGUI_CONTEXT") != nullptr)
        return false;
    DWORD process_id = 0;
    GetWindowThreadProcessId(hwnd, &process_id);
    return process_id == GetCurrentProcessId();
}

inline bool same_com_identity(IUnknown *left, IUnknown *right) {
    if (!left || !right)
        return false;
    IUnknown *left_identity = nullptr;
    IUnknown *right_identity = nullptr;
    const bool ready =
        SUCCEEDED(left->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&left_identity))) &&
        SUCCEEDED(right->QueryInterface(__uuidof(IUnknown), reinterpret_cast<void **>(&right_identity))) &&
        left_identity && right_identity;
    const bool same = ready && left_identity == right_identity;
    if (left_identity)
        left_identity->Release();
    if (right_identity)
        right_identity->Release();
    return same;
}

inline bool is_active_game_swap_chain(IDXGISwapChain *swap_chain) {
    if (!swap_chain || !g_active_swap_chain)
        return false;
    if (swap_chain == g_active_swap_chain)
        return true;

    // Secondary ImGui swap chains are the common hot path here when several
    // generated mods coexist. Reject them by their Win32 viewport marker before
    // paying for two COM identity queries on every detached-window Present.
    DXGI_SWAP_CHAIN_DESC desc{};
    if (query_swap_chain_desc(swap_chain, &desc) && desc.OutputWindow &&
        GetPropA(desc.OutputWindow, "IMGUI_CONTEXT") != nullptr)
        return false;
    return same_com_identity(swap_chain, g_active_swap_chain);
}

inline void platform_renderer_create_window(ImGuiViewport *viewport) {
    PlatformRendererGuard guard{};
    if (g_platform_renderer_callbacks.create_window)
        g_platform_renderer_callbacks.create_window(viewport);
}

inline void platform_renderer_destroy_window(ImGuiViewport *viewport) {
    PlatformRendererGuard guard{};
    if (g_platform_renderer_callbacks.destroy_window)
        g_platform_renderer_callbacks.destroy_window(viewport);
}

inline void platform_renderer_set_window_size(ImGuiViewport *viewport, ImVec2 size) {
    PlatformRendererGuard guard{};
    if (g_platform_renderer_callbacks.set_window_size)
        g_platform_renderer_callbacks.set_window_size(viewport, size);
}

inline void platform_renderer_render_window(ImGuiViewport *viewport, void *renderArg) {
    PlatformRendererGuard guard{};
    if (g_platform_renderer_callbacks.render_window)
        g_platform_renderer_callbacks.render_window(viewport, renderArg);
}

inline void platform_renderer_swap_buffers(ImGuiViewport *viewport, void *renderArg) {
    PlatformRendererGuard guard{};
    if (g_platform_renderer_callbacks.swap_buffers)
        g_platform_renderer_callbacks.swap_buffers(viewport, renderArg);
}

inline bool install_platform_renderer_isolation() {
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0)
        return true;
    ImGuiPlatformIO &platform = ImGui::GetPlatformIO();
    if (!platform.Renderer_CreateWindow || !platform.Renderer_DestroyWindow || !platform.Renderer_SetWindowSize ||
        !platform.Renderer_RenderWindow || !platform.Renderer_SwapBuffers) {
        log("ImGui renderer did not expose a complete multi-viewport callback "
            "set.");
        return false;
    }
    g_platform_renderer_callbacks = {
        platform.Renderer_CreateWindow, platform.Renderer_DestroyWindow, platform.Renderer_SetWindowSize,
        platform.Renderer_RenderWindow, platform.Renderer_SwapBuffers,   true,
    };
    platform.Renderer_CreateWindow = &platform_renderer_create_window;
    platform.Renderer_DestroyWindow = &platform_renderer_destroy_window;
    platform.Renderer_SetWindowSize = &platform_renderer_set_window_size;
    platform.Renderer_RenderWindow = &platform_renderer_render_window;
    platform.Renderer_SwapBuffers = &platform_renderer_swap_buffers;
    return true;
}

inline void collect_platform_windows() {
    g_platform_windows.clear();
    const ImGuiPlatformIO &platform = ImGui::GetPlatformIO();
    if (platform.Viewports.Size > 1)
        g_platform_windows.reserve(static_cast<std::size_t>(platform.Viewports.Size - 1));
    for (int index = 1; index < platform.Viewports.Size; ++index) {
        const ImGuiViewport *viewport = platform.Viewports[index];
        if (!viewport)
            continue;
        const auto window =
            static_cast<HWND>(viewport->PlatformHandleRaw ? viewport->PlatformHandleRaw : viewport->PlatformHandle);
        if (window)
            g_platform_windows.push_back(window);
    }
}

inline void apply_platform_window_policy() {
    const Win32ViewportPolicyResult result = g_platform_window_policy.apply(g_platform_windows, g_hwnd);
    const ULONGLONG now = GetTickCount64();
    if (!result && now - g_platform_window_policy_warning_tick >= 2000) {
        char text[224]{};
        std::snprintf(text, sizeof(text), "Detached viewport policy failed: operation=%s hwnd=%p error=%lu.",
                      result.operation ? result.operation : "unknown", static_cast<void *>(result.window),
                      static_cast<unsigned long>(result.error));
        log(text);
        g_platform_window_policy_warning_tick = now;
    }
}

inline void pump_platform_window_messages() {
    const WindowMessagePumpResult result = pump_owned_window_messages(g_platform_windows, 128);
    const ULONGLONG now = GetTickCount64();
    if ((result.foreignThreadWindows != 0 || result.backlogRemaining) &&
        now - g_platform_message_warning_tick >= 2000) {
        char text[192]{};
        std::snprintf(text, sizeof(text),
                      "Detached viewport message pressure: foreign-thread "
                      "windows=%u backlog=%s.",
                      result.foreignThreadWindows, result.backlogRemaining ? "yes" : "no");
        log(text);
        g_platform_message_warning_tick = now;
    }
}

inline void validate_platform_window_topology() {
    if (g_platform_window_topology_logged)
        return;
    if (g_platform_windows.empty())
        return;

    const HWND window = g_platform_windows.front();
    const Win32ViewportTopology topology = query_viewport_topology(window);
    const DWORD renderThread = GetCurrentThreadId();
    char text[320]{};
    std::snprintf(text, sizeof(text),
                  "Detached viewport topology: hwnd=%p owner=%p windowThread=%lu "
                  "renderThread=%lu ownerless=%s threadOwned=%s noActivate=%s taskbarHidden=%s.",
                  static_cast<void *>(window), static_cast<void *>(topology.owner),
                  static_cast<unsigned long>(topology.windowThread), static_cast<unsigned long>(renderThread),
                  !topology.owner ? "yes" : "no", topology.windowThread == renderThread ? "yes" : "no",
                  (topology.extendedStyle & WS_EX_NOACTIVATE) != 0 ? "yes" : "no",
                  (topology.extendedStyle & WS_EX_TOOLWINDOW) != 0 &&
                          (topology.extendedStyle & WS_EX_APPWINDOW) == 0
                      ? "yes"
                      : "no");
    log(text);
    g_platform_window_topology_logged = true;
}

inline bool is_srgb_format(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return true;
        default:
            return false;
    }
}

inline bool is_linear_back_buffer_format(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return true;
        default:
            return false;
    }
}

inline const char *dxgi_format_name(DXGI_FORMAT format) {
    switch (format) {
        case DXGI_FORMAT_UNKNOWN:
            return "UNKNOWN";
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return "R8G8B8A8_UNORM";
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            return "R8G8B8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            return "B8G8R8A8_UNORM";
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
            return "B8G8R8A8_UNORM_SRGB";
        case DXGI_FORMAT_B8G8R8X8_UNORM:
            return "B8G8R8X8_UNORM";
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
            return "B8G8R8X8_UNORM_SRGB";
        case DXGI_FORMAT_R10G10B10A2_UNORM:
            return "R10G10B10A2_UNORM";
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return "R16G16B16A16_FLOAT";
        case DXGI_FORMAT_R32G32B32A32_FLOAT:
            return "R32G32B32A32_FLOAT";
        case DXGI_FORMAT_R11G11B10_FLOAT:
            return "R11G11B10_FLOAT";
        default:
            return "other";
    }
}

inline void query_swap_chain_color_space(IDXGISwapChain *swap_chain) {
    g_linear_color_space_support_known = false;
    g_linear_color_space_supported = false;
    IDXGISwapChain3 *swap_chain3 = nullptr;
    if (swap_chain &&
        SUCCEEDED(swap_chain->QueryInterface(__uuidof(IDXGISwapChain3), reinterpret_cast<void **>(&swap_chain3))) &&
        swap_chain3) {
        UINT support = 0;
        if (SUCCEEDED(swap_chain3->CheckColorSpaceSupport(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support))) {
            g_linear_color_space_support_known = true;
            g_linear_color_space_supported = (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
        }
        swap_chain3->Release();
    }
}

inline bool color_space_needs_linear_ui() {
    return false;
}

inline bool render_target_needs_linear_ui(DXGI_FORMAT swap_chain_format) {
    return is_srgb_format(swap_chain_format) || is_srgb_format(g_back_buffer_format) ||
           is_linear_back_buffer_format(swap_chain_format) || is_linear_back_buffer_format(g_back_buffer_format) ||
           color_space_needs_linear_ui();
}

inline void log_render_target_diagnostics(DXGI_FORMAT swap_chain_format) {
    if (g_render_target_diagnostics_logged)
        return;
    g_render_target_diagnostics_logged = true;

    char text[512]{};
    std::snprintf(text, sizeof(text),
                  "DX11 swap chain format=%s backBuffer=%s "
                  "linearColorSpaceSupport=%s linearUi=%s.",
                  dxgi_format_name(swap_chain_format), dxgi_format_name(g_back_buffer_format),
                  g_linear_color_space_support_known ? (g_linear_color_space_supported ? "yes" : "no") : "unavailable",
                  render_target_needs_linear_ui(swap_chain_format) ? "yes" : "no");
    log(text);
}

inline float srgb_to_linear(float value) {
    if (value <= 0.04045f)
        return value / 12.92f;
    return std::pow((value + 0.055f) / 1.055f, 2.4f);
}

inline void linearize_color(ImVec4 &color) {
    color.x = srgb_to_linear(color.x);
    color.y = srgb_to_linear(color.y);
    color.z = srgb_to_linear(color.z);
}

inline void apply_swap_chain_color_space(DXGI_FORMAT format) {
    static bool applied = false;
    const bool needs_linear_ui = render_target_needs_linear_ui(format);
    if (!needs_linear_ui || applied)
        return;
    applied = true;

    ModUI::Theme::Palette &p = ModUI::Theme::palette();
    linearize_color(p.bg_base);
    linearize_color(p.bg_overlay);
    linearize_color(p.bg_elevated);
    linearize_color(p.surface);
    linearize_color(p.surface_raised);
    linearize_color(p.surface_hover);
    linearize_color(p.surface_active);
    linearize_color(p.surface_glass);
    linearize_color(p.border_subtle);
    linearize_color(p.border_strong);
    linearize_color(p.border_focus);
    linearize_color(p.text_primary);
    linearize_color(p.text_secondary);
    linearize_color(p.text_muted);
    linearize_color(p.text_disabled);
    linearize_color(p.accent_a);
    linearize_color(p.accent_b);
    linearize_color(p.accent_c);
    linearize_color(p.accent_soft);
    linearize_color(p.accent_line);
    linearize_color(p.accent_warm);
    linearize_color(p.success);
    linearize_color(p.warning);
    linearize_color(p.danger);
    linearize_color(p.info);
    linearize_color(p.shadow);
    log("DX11 render target expects linear UI colors; UI palette converted to "
        "linear.");
}

inline void log_font_diagnostics(const char *renderer_name, const char *sampler_filter) {
    static bool logged = false;
    if (logged)
        return;
    logged = true;

    const float font_size = ModUI::Theme::font_size_px();
    char text[512]{};
    std::snprintf(text, sizeof(text), "%s font source=%s (%s) CJK=%s size=%.1fpx oversample=2x1 sampler=%s.",
                  renderer_name ? renderer_name : "ImGui", ModUI::Theme::loaded_font_name(),
                  ModUI::Theme::using_ttf_font() ? "TTF" : "ImGui default", ModUI::Theme::cjk_font_support(), font_size,
                  sampler_filter ? sampler_filter : "linear");
    log(text);
}

inline HWND find_main_window() {
    struct SearchState {
        DWORD pid;
        HWND hwnd;
    } state{GetCurrentProcessId(), nullptr};
    EnumWindows(
        [](HWND hwnd, LPARAM param) -> BOOL {
            auto *state = reinterpret_cast<SearchState *>(param);
            DWORD window_pid = 0;
            GetWindowThreadProcessId(hwnd, &window_pid);
            if (window_pid == state->pid && GetWindow(hwnd, GW_OWNER) == nullptr && IsWindowVisible(hwnd)) {
                state->hwnd = hwnd;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&state));
    return state.hwnd;
}

inline void release_render_target() {
    if (g_render_target) {
        g_render_target->Release();
        g_render_target = nullptr;
    }
}

inline void release_device_objects() {
    release_render_target();
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

inline bool install_window_message_handler() {
    if (!g_hwnd)
        return false;
    if (URK::window_message_dispatch_available()) {
        if (!URK::window_message_register(g_hwnd, &window_message_callback))
            return false;
        g_window_message_registered = true;
        g_old_wndproc = nullptr;
        return true;
    }
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous = SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndproc));
    if (!previous && GetLastError() != ERROR_SUCCESS)
        return false;
    g_old_wndproc = reinterpret_cast<WNDPROC>(previous);
    return true;
}

inline void restore_wndproc() {
    if (!g_hwnd)
        return;
    if (g_window_message_registered) {
        if (!URK::window_message_unregister(g_hwnd, &window_message_callback))
            log("Loader window-message callback unregister failed.");
        g_window_message_registered = false;
        return;
    }
    if (!g_old_wndproc)
        return;
    const LONG_PTR current = GetWindowLongPtr(g_hwnd, GWLP_WNDPROC);
    if (current == reinterpret_cast<LONG_PTR>(&wndproc)) {
        SetWindowLongPtr(g_hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_old_wndproc));
    }
    g_old_wndproc = nullptr;
}

inline LRESULT call_previous_window_proc(HWND hwnd, WNDPROC old_wndproc, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_window_message_registered)
        return static_cast<LRESULT>(URK::window_message_call_original(hwnd, message, wparam, lparam));
    return old_wndproc ? CallWindowProc(old_wndproc, hwnd, message, wparam, lparam)
                       : DefWindowProc(hwnd, message, wparam, lparam);
}

inline bool create_render_target(IDXGISwapChain *swap_chain) {
    release_render_target();
    if (!swap_chain || !g_device)
        return false;

    ID3D11Texture2D *back_buffer = nullptr;
    const HRESULT get_buffer =
        swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&back_buffer));
    if (FAILED(get_buffer) || !back_buffer) {
        log("DX11 swap chain back buffer unavailable; UI frame skipped.");
        return false;
    }

    D3D11_TEXTURE2D_DESC back_buffer_desc{};
    back_buffer->GetDesc(&back_buffer_desc);
    g_back_buffer_format = back_buffer_desc.Format;

    const HRESULT create_view = g_device->CreateRenderTargetView(back_buffer, nullptr, &g_render_target);
    back_buffer->Release();
    if (FAILED(create_view) || !g_render_target) {
        log("DX11 render-target-view creation failed; UI frame skipped.");
        return false;
    }
    return true;
}

inline bool is_keyboard_message(UINT message) {
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP ||
           message == WM_CHAR || message == WM_SYSCHAR;
}

inline bool is_keyboard_button_message(UINT message) {
    return message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
}

inline bool is_keyboard_button_down(UINT message) {
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
}

inline bool is_client_mouse_message(UINT message) {
    return message >= WM_MOUSEFIRST && message <= WM_MOUSELAST;
}

inline bool is_mouse_button_message(UINT message) {
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONUP || message == WM_LBUTTONDBLCLK ||
           message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK ||
           message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_MBUTTONDBLCLK ||
           message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK;
}

inline bool is_raw_input_message(UINT message) {
    return message == WM_INPUT;
}

inline int mouse_button_for_message(UINT message, WPARAM wparam) {
    switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
            return 0;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
            return 1;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
            return 2;
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
            return GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? 3 : 4;
        default:
            return -1;
    }
}

inline bool is_mouse_button_down(UINT message) {
    return message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDOWN ||
           message == WM_RBUTTONDBLCLK || message == WM_MBUTTONDOWN || message == WM_MBUTTONDBLCLK ||
           message == WM_XBUTTONDOWN || message == WM_XBUTTONDBLCLK;
}

inline UINT mouse_button_message(int button, bool down) {
    static constexpr UINT down_messages[] = {WM_LBUTTONDOWN, WM_RBUTTONDOWN, WM_MBUTTONDOWN, WM_XBUTTONDOWN,
                                             WM_XBUTTONDOWN};
    static constexpr UINT up_messages[] = {WM_LBUTTONUP, WM_RBUTTONUP, WM_MBUTTONUP, WM_XBUTTONUP, WM_XBUTTONUP};
    if (button < 0 || button >= 5)
        return WM_NULL;
    return down ? down_messages[button] : up_messages[button];
}

inline LPARAM keyboard_transition_lparam(LPARAM source, bool down) {
    constexpr std::uintptr_t metadata_mask = 0x21ff0000u;
    constexpr std::uintptr_t released_mask = 0xc0000000u;
    const std::uintptr_t source_bits = static_cast<std::uintptr_t>(source);
    return static_cast<LPARAM>((source_bits & metadata_mask) | 1u | (down ? 0u : released_mask));
}

inline WPARAM mouse_transition_wparam(int button, const std::array<MouseButtonState, 5> &states) {
    WORD flags = 0;
    if (states[0].down)
        flags |= MK_LBUTTON;
    if (states[1].down)
        flags |= MK_RBUTTON;
    if (states[2].down)
        flags |= MK_MBUTTON;
    if (states[3].down)
        flags |= MK_XBUTTON1;
    if (states[4].down)
        flags |= MK_XBUTTON2;
    if (g_physical_keyboard[VK_SHIFT].down)
        flags |= MK_SHIFT;
    if (g_physical_keyboard[VK_CONTROL].down)
        flags |= MK_CONTROL;
    const WORD xbutton = button == 3 ? XBUTTON1 : button == 4 ? XBUTTON2 : 0;
    return MAKEWPARAM(flags, xbutton);
}

inline void track_physical_input(UINT message, WPARAM wparam, LPARAM lparam) {
    if (is_keyboard_button_message(message) && wparam < g_physical_keyboard.size()) {
        KeyboardButtonState &state = g_physical_keyboard[static_cast<std::size_t>(wparam)];
        state.down = is_keyboard_button_down(message);
        state.system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
        if (state.down)
            state.lparam = lparam;
    }

    if (is_mouse_button_message(message)) {
        const int button = mouse_button_for_message(message, wparam);
        if (button >= 0) {
            MouseButtonState &state = g_physical_mouse[button];
            state.down = is_mouse_button_down(message);
            if (state.down)
                state.lparam = lparam;
        }
    } else if (message == WM_MOUSEMOVE) {
        for (MouseButtonState &state : g_physical_mouse)
            if (state.down)
                state.lparam = lparam;
    }
}

inline void track_game_input(UINT message, WPARAM wparam, LPARAM lparam) {
    if (is_keyboard_button_message(message) && wparam < g_game_keyboard.size()) {
        KeyboardButtonState &state = g_game_keyboard[static_cast<std::size_t>(wparam)];
        state.down = is_keyboard_button_down(message);
        state.system = message == WM_SYSKEYDOWN || message == WM_SYSKEYUP;
        if (state.down)
            state.lparam = lparam;
    }

    if (!is_mouse_button_message(message))
        return;
    const int button = mouse_button_for_message(message, wparam);
    if (button < 0)
        return;
    MouseButtonState &state = g_game_mouse[button];
    state.down = is_mouse_button_down(message);
    if (state.down)
        state.lparam = lparam;
}

inline void clear_tracked_input() {
    g_physical_keyboard.fill(KeyboardButtonState{});
    g_game_keyboard.fill(KeyboardButtonState{});
    g_physical_mouse.fill(MouseButtonState{});
    g_game_mouse.fill(MouseButtonState{});
}

inline void clear_game_input() {
    g_game_keyboard.fill(KeyboardButtonState{});
    g_game_mouse.fill(MouseButtonState{});
}

inline bool any_window_mouse_button_down() {
    for (bool down : g_wndproc_mouse_down)
        if (down)
            return true;
    return false;
}

inline void queue_input_event(const InputEvent &event) {
    std::lock_guard lock(g_input_mutex);
    // A stalled or replaced Present chain must never turn queued Win32 input
    // into unbounded process memory. The next successful frame resynchronizes
    // ImGui from the current physical mouse state.
    if (g_input_events.size() >= 512) {
        g_input_events.clear();
        InputEvent reset{};
        reset.kind = InputEventKind::release_all_input;
        reset.hwnd = event.hwnd;
        g_input_events.push_back(reset);
    }
    if (event.kind == InputEventKind::mouse_position && !g_input_events.empty() &&
        g_input_events.back().kind == InputEventKind::mouse_position) {
        g_input_events.back() = event;
        return;
    }
    g_input_events.push_back(event);
}

inline void queue_release_all_mouse(HWND hwnd) {
    InputEvent event{};
    event.kind = InputEventKind::release_all_mouse;
    event.hwnd = hwnd;
    queue_input_event(event);
}

inline void queue_release_all_input(HWND hwnd) {
    InputEvent event{};
    event.kind = InputEventKind::release_all_input;
    event.hwnd = hwnd;
    queue_input_event(event);
}

inline void log_cursor_error(const char *operation);

inline void queue_mouse_screen_position(HWND hwnd, LONG x, LONG y) {
    InputEvent event{};
    event.kind = InputEventKind::mouse_position;
    event.hwnd = hwnd;
    event.x = static_cast<float>(x);
    event.y = static_cast<float>(y);
    queue_input_event(event);
}

inline bool queue_mouse_client_position(HWND hwnd, LONG x, LONG y) {
    POINT screen{x, y};
    if (!client_mouse_to_desktop(hwnd, &screen)) {
        log_cursor_error("ClientToScreen(mouse)");
        return false;
    }
    g_virtual_mouse_position = screen;
    g_virtual_mouse_position_valid = true;
    queue_mouse_screen_position(hwnd, screen.x, screen.y);
    return true;
}

inline void queue_mouse_button(HWND hwnd, int button, bool down) {
    InputEvent event{};
    event.kind = InputEventKind::mouse_button;
    event.hwnd = hwnd;
    event.button = button;
    event.state = down;
    queue_input_event(event);
}

inline void clear_window_mouse_buttons(HWND hwnd, bool release_capture) {
    g_wndproc_mouse_down.fill(false);
    if (release_capture && GetCapture() == hwnd)
        ReleaseCapture();
    queue_release_all_mouse(hwnd);
}

inline void poll_window_mouse_state(HWND hwnd) {
    if (!hwnd || !IsWindow(hwnd) || GetForegroundWindow() != hwnd || !g_menu_visible.load(std::memory_order_acquire))
        return;

    POINT position{};
    if (GetCursorPos(&position)) {
        g_virtual_mouse_position = position;
        g_virtual_mouse_position_valid = true;
        queue_mouse_screen_position(hwnd, position.x, position.y);
    }

    static constexpr int virtual_keys[5] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
    for (int button = 0; button < 5; ++button) {
        const bool down = (GetAsyncKeyState(virtual_keys[button]) & 0x8000) != 0;
        if (g_wndproc_mouse_down[button] == down)
            continue;
        g_wndproc_mouse_down[button] = down;
        queue_mouse_button(hwnd, button, down);
    }
}

inline void apply_window_input_transition(HWND hwnd, WNDPROC old_wndproc, bool open) {
    if (g_menu_input_open == open) {
        g_menu_visible.store(open, std::memory_order_release);
        g_menu_input_applied.store(open ? 1 : 0, std::memory_order_release);
        return;
    }

    g_menu_input_open = open;
    g_menu_visible.store(open, std::memory_order_release);
    g_menu_input_applied.store(open ? 1 : 0, std::memory_order_release);
    g_imgui_wants_mouse.store(false, std::memory_order_release);
    g_imgui_wants_keyboard.store(false, std::memory_order_release);
    // Keep game focus; capture input only while ImGui needs it.
    clear_window_mouse_buttons(hwnd, true);
    queue_release_all_input(hwnd);
}

inline void drain_input_events() {
    poll_window_mouse_state(g_hwnd);
    std::deque<InputEvent> events;
    {
        std::lock_guard lock(g_input_mutex);
        events.swap(g_input_events);
    }

    ImGuiIO &io = ImGui::GetIO();
    for (const InputEvent &event : events) {
        switch (event.kind) {
            case InputEventKind::mouse_position:
                if (event.x <= -std::numeric_limits<float>::max() || event.y <= -std::numeric_limits<float>::max()) {
                    io.AddMousePosEvent(event.x, event.y);
                    break;
                }
                if ((io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
                    io.AddMousePosEvent(event.x, event.y);
                    break;
                }
                {
                    POINT client{static_cast<LONG>(std::lround(event.x)), static_cast<LONG>(std::lround(event.y))};
                    if (desktop_mouse_to_imgui(event.hwnd, false, &client))
                        io.AddMousePosEvent(static_cast<float>(client.x), static_cast<float>(client.y));
                    else
                        log_cursor_error("ScreenToClient(mouse event)");
                }
                break;
            case InputEventKind::mouse_button:
                if (event.button >= 0 && event.button < 5)
                    io.AddMouseButtonEvent(event.button, event.state);
                break;
            case InputEventKind::mouse_wheel:
                io.AddMouseWheelEvent(event.x, event.y);
                break;
            case InputEventKind::focus:
                io.AddFocusEvent(event.state);
                break;
            case InputEventKind::raw_message:
                ImGui_ImplWin32_WndProcHandler(event.hwnd, event.message, event.wparam, event.lparam);
                break;
            case InputEventKind::release_all_mouse:
                for (int button = 0; button < 5; ++button)
                    io.AddMouseButtonEvent(button, false);
                if (ImGui::GetCurrentContext())
                    ImGui::ClearActiveID();
                break;
            case InputEventKind::release_all_input:
                io.ClearEventsQueue();
                io.ClearInputKeys();
                io.ClearInputMouse();
                if (ImGui::GetCurrentContext())
                    ImGui::ClearActiveID();
                break;
        }
    }
}

inline void publish_imgui_capture_state() {
    const ImGuiIO &io = ImGui::GetIO();
    const bool capture_mouse = io.WantCaptureMouse;
    g_imgui_wants_mouse.store(capture_mouse, std::memory_order_release);
    g_imgui_wants_keyboard.store(io.WantCaptureKeyboard, std::memory_order_release);
    const int desired = capture_mouse ? 1 : 0;
    if (g_runtime_mouse_capture_requested.load(std::memory_order_acquire) != desired &&
        URK::set_menu_mouse_capture(capture_mouse))
        g_runtime_mouse_capture_requested.store(desired, std::memory_order_release);
}

inline void log_cursor_error(const char *operation) {
    char text[192]{};
    std::snprintf(text, sizeof(text), "%s failed for the ImGui menu: Win32 error=%lu.", operation,
                  static_cast<unsigned long>(GetLastError()));
    log(text);
}

inline bool set_native_cursor_open(HWND hwnd, bool open) {
    if (open) {
        SetLastError(ERROR_SUCCESS);
        HCURSOR arrow = LoadCursor(nullptr, IDC_ARROW);
        if (!arrow) {
            log_cursor_error("LoadCursor(IDC_ARROW)");
            return false;
        }
        if (!g_native_cursor_owned.load(std::memory_order_acquire)) {
            RECT clip{};
            SetLastError(ERROR_SUCCESS);
            if (!GetClipCursor(&clip)) {
                log_cursor_error("GetClipCursor");
                return false;
            }
            g_saved_cursor_clip = clip;
        }
        SetLastError(ERROR_SUCCESS);
        if (!ClipCursor(nullptr)) {
            log_cursor_error("ClipCursor(unlock)");
            return false;
        }
        if (!g_native_cursor_owned.load(std::memory_order_acquire)) {
            int display_count = -1;
            do {
                display_count = ShowCursor(TRUE);
                ++g_native_cursor_show_balance;
            } while (display_count < 0);
            g_saved_cursor = SetCursor(arrow);
            g_native_cursor_owned.store(true, std::memory_order_release);
        }
        POINT position{};
        if (GetCursorPos(&position)) {
            g_virtual_mouse_position = position;
            g_virtual_mouse_position_valid = true;
            queue_mouse_screen_position(hwnd, position.x, position.y);
        } else {
            log_cursor_error("GetCursorPos(menu open)");
            g_virtual_mouse_position_valid = false;
        }
        g_raw_mouse_seen = false;
        clear_window_mouse_buttons(hwnd, true);
        return true;
    }

    clear_window_mouse_buttons(hwnd, true);
    g_virtual_mouse_position_valid = false;
    g_raw_mouse_seen = false;
    if (!g_native_cursor_owned.load(std::memory_order_acquire))
        return true;
    SetLastError(ERROR_SUCCESS);
    if (!ClipCursor(&g_saved_cursor_clip)) {
        log_cursor_error("ClipCursor(restore)");
        return false;
    }
    SetCursor(g_saved_cursor);
    g_saved_cursor = nullptr;
    while (g_native_cursor_show_balance > 0) {
        ShowCursor(FALSE);
        --g_native_cursor_show_balance;
    }
    g_native_cursor_owned.store(false, std::memory_order_release);
    return true;
}

inline bool request_window_cursor_state(bool open) {
    if (!g_hwnd || !IsWindow(g_hwnd))
        return false;
    const UINT message = menu_cursor_message();
    if (!message)
        return false;
    const int requested = open ? 1 : 0;
    g_native_cursor_requested.store(requested, std::memory_order_release);
    g_native_cursor_request_tick.store(GetTickCount64(), std::memory_order_release);
    const DWORD window_thread = GetWindowThreadProcessId(g_hwnd, nullptr);
    if (window_thread == GetCurrentThreadId()) {
        if (SendMessageW(g_hwnd, message, open ? 1 : 0, 0) != 0)
            return true;
        g_native_cursor_requested.store(-1, std::memory_order_release);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(g_hwnd, message, open ? 1 : 0, 0)) {
        g_native_cursor_requested.store(-1, std::memory_order_release);
        log_cursor_error("PostMessage(menu cursor state)");
        return false;
    }
    return true;
}

inline bool request_window_input_state(bool open) {
    if (!g_hwnd || !IsWindow(g_hwnd))
        return false;
    const UINT message = menu_input_message();
    if (!message)
        return false;
    const int requested = open ? 1 : 0;
    g_menu_input_requested.store(requested, std::memory_order_release);
    g_menu_input_request_tick.store(GetTickCount64(), std::memory_order_release);
    const DWORD window_thread = GetWindowThreadProcessId(g_hwnd, nullptr);
    if (window_thread == GetCurrentThreadId()) {
        if (SendMessageW(g_hwnd, message, open ? 1 : 0, 0) != 0)
            return true;
        g_menu_input_requested.store(-1, std::memory_order_release);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    if (!PostMessageW(g_hwnd, message, open ? 1 : 0, 0)) {
        g_menu_input_requested.store(-1, std::memory_order_release);
        log_cursor_error("PostMessage(menu input state)");
        return false;
    }
    return true;
}

inline bool acquire_cursor_lease() {
    if (!URK::has_cursor_control())
        return false;
    bool expected = false;
    if (!g_cursor_lease.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return true;
    }
    if (URK::set_menu_cursor_open(true))
        return true;
    g_cursor_lease.store(false, std::memory_order_release);
    log("Failed to acquire the loader menu-cursor lease.");
    return false;
}

inline bool release_cursor_lease() {
    bool expected = true;
    if (!g_cursor_lease.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return true;
    }
    if (URK::set_menu_cursor_open(false))
        return true;
    g_cursor_lease.store(true, std::memory_order_release);
    log("Failed to release the loader menu-cursor lease.");
    return false;
}

inline void sync_menu_state() {
    const bool open = ModConfig::show_menu;
    const ULONGLONG now = GetTickCount64();
    const int input_desired = open ? 1 : 0;
    const bool input_timed_out = now - g_menu_input_request_tick.load(std::memory_order_acquire) >= 500;
    if (g_menu_input_applied.load(std::memory_order_acquire) != input_desired &&
        (g_menu_input_requested.load(std::memory_order_acquire) != input_desired || input_timed_out) &&
        !request_window_input_state(open)) {
        log(open ? "Failed to enable ImGui input routing." : "Failed to disable ImGui input routing.");
    }
    if (open)
        acquire_cursor_lease();
    else
        release_cursor_lease();

    const bool native_open = open && !g_cursor_lease.load(std::memory_order_acquire);
    const int cursor_desired = native_open ? 1 : 0;
    const bool cursor_timed_out = now - g_native_cursor_request_tick.load(std::memory_order_acquire) >= 500;
    if (g_native_cursor_applied.load(std::memory_order_acquire) != cursor_desired &&
        (g_native_cursor_requested.load(std::memory_order_acquire) != cursor_desired || cursor_timed_out) &&
        !request_window_cursor_state(native_open)) {
        log(native_open ? "Failed to acquire native cursor ownership for the ImGui menu."
                        : "Failed to restore native cursor ownership after closing the "
                          "ImGui menu.");
    }
    if (native_open && g_native_cursor_owned.load(std::memory_order_acquire)) {
        SetLastError(ERROR_SUCCESS);
        if (!ClipCursor(nullptr))
            log_cursor_error("ClipCursor(menu reapply)");
    }
}

inline void shutdown_imgui() {
    if (!request_window_input_state(false))
        log("Failed to restore game input during ImGui shutdown.");
    release_cursor_lease();
    if (!request_window_cursor_state(false))
        log("Failed to restore native cursor ownership during ImGui shutdown.");
    if (g_imgui_ready) {
        if (g_backend == GraphicsBackend::dx11)
            ImGui_ImplDX11_Shutdown();
        if (g_backend == GraphicsBackend::dx12)
            ImGui_ImplDX12_Shutdown();
        if (g_backend == GraphicsBackend::opengl)
            ImGui_ImplOpenGL3_Shutdown();
        g_platform_renderer_callbacks = {};
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_imgui_ready = false;
    }
    {
        std::lock_guard lock(g_input_mutex);
        g_input_events.clear();
    }
    restore_wndproc();
    g_hwnd = nullptr;
    g_menu_input_requested.store(-1, std::memory_order_release);
    g_menu_input_applied.store(-1, std::memory_order_release);
    g_native_cursor_requested.store(-1, std::memory_order_release);
    g_native_cursor_applied.store(-1, std::memory_order_release);
    g_platform_windows.clear();
    g_platform_window_policy.reset();
    g_platform_window_topology_logged = false;
    g_platform_message_warning_tick = 0;
    g_platform_window_policy_warning_tick = 0;
    if (g_active_swap_chain) {
        g_active_swap_chain->Release();
        g_active_swap_chain = nullptr;
    }
    release_device_objects();
    g_dx12_resources.shutdown();
    g_dx12_queue_captured.store(false, std::memory_order_release);
    g_dx12_busy_frame_skips = 0;
    g_dx12_busy_diagnostic_tick = 0;
    g_gl_context = nullptr;
}

inline bool is_toggle_key_down(UINT message, WPARAM wparam, LPARAM lparam) {
    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN)
        return false;
    if (static_cast<int>(wparam) != ModConfig::menu_toggle_key)
        return false;
    constexpr LPARAM was_down_mask = 1LL << 30;
    return (lparam & was_down_mask) == 0;
}

inline void queue_menu_toggle() {
    g_menu_toggle_count.fetch_add(1, std::memory_order_acq_rel);
}

inline void relay_input_to_previous(HWND hwnd, WNDPROC old_wndproc, const InputRelay &input) {
    if (!old_wndproc && !g_window_message_registered)
        return;
    const UINT relay_message = input_relay_message();
    if (!relay_message)
        return;
    call_previous_window_proc(hwnd, old_wndproc, relay_message, 0, reinterpret_cast<LPARAM>(&input));
}

inline bool queue_raw_mouse_input(HWND hwnd, LPARAM lparam) {
    RAWINPUTHEADER header{};
    UINT header_size = sizeof(header);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_HEADER, &header, &header_size,
                        sizeof(RAWINPUTHEADER)) == UINT(-1)) {
        log_cursor_error("GetRawInputData(header)");
        return false;
    }
    if (header.dwType != RIM_TYPEMOUSE)
        return false;

    RAWINPUT input{};
    UINT input_size = sizeof(input);
    if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &input, &input_size, sizeof(RAWINPUTHEADER)) ==
        UINT(-1)) {
        log_cursor_error("GetRawInputData(mouse)");
        return false;
    }

    const RAWMOUSE &mouse = input.data.mouse;
    g_raw_mouse_seen = true;
    if ((mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
        const bool virtual_desktop = (mouse.usFlags & MOUSE_VIRTUAL_DESKTOP) != 0;
        const int origin_x = virtual_desktop ? GetSystemMetrics(SM_XVIRTUALSCREEN) : 0;
        const int origin_y = virtual_desktop ? GetSystemMetrics(SM_YVIRTUALSCREEN) : 0;
        const int width = GetSystemMetrics(virtual_desktop ? SM_CXVIRTUALSCREEN : SM_CXSCREEN);
        const int height = GetSystemMetrics(virtual_desktop ? SM_CYVIRTUALSCREEN : SM_CYSCREEN);
        g_virtual_mouse_position = {origin_x + MulDiv(mouse.lLastX, width, 65535),
                                    origin_y + MulDiv(mouse.lLastY, height, 65535)};
    } else {
        if (!g_virtual_mouse_position_valid) {
            if (!GetCursorPos(&g_virtual_mouse_position)) {
                log_cursor_error("GetCursorPos(raw mouse)");
                return false;
            }
        }
        g_virtual_mouse_position.x += mouse.lLastX;
        g_virtual_mouse_position.y += mouse.lLastY;
    }

    const LONG virtualLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG virtualTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const LONG virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const LONG virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (virtualWidth > 0 && virtualHeight > 0) {
        g_virtual_mouse_position.x =
            (std::clamp)(g_virtual_mouse_position.x, virtualLeft, virtualLeft + virtualWidth - 1);
        g_virtual_mouse_position.y =
            (std::clamp)(g_virtual_mouse_position.y, virtualTop, virtualTop + virtualHeight - 1);
    }
    g_virtual_mouse_position_valid = true;
    queue_mouse_screen_position(hwnd, g_virtual_mouse_position.x, g_virtual_mouse_position.y);

    const USHORT buttons = mouse.usButtonFlags;
    if ((buttons & RI_MOUSE_LEFT_BUTTON_DOWN) != 0)
        queue_mouse_button(hwnd, 0, true);
    if ((buttons & RI_MOUSE_LEFT_BUTTON_UP) != 0)
        queue_mouse_button(hwnd, 0, false);
    if ((buttons & RI_MOUSE_RIGHT_BUTTON_DOWN) != 0)
        queue_mouse_button(hwnd, 1, true);
    if ((buttons & RI_MOUSE_RIGHT_BUTTON_UP) != 0)
        queue_mouse_button(hwnd, 1, false);
    if ((buttons & RI_MOUSE_MIDDLE_BUTTON_DOWN) != 0)
        queue_mouse_button(hwnd, 2, true);
    if ((buttons & RI_MOUSE_MIDDLE_BUTTON_UP) != 0)
        queue_mouse_button(hwnd, 2, false);
    if ((buttons & RI_MOUSE_BUTTON_4_DOWN) != 0)
        queue_mouse_button(hwnd, 3, true);
    if ((buttons & RI_MOUSE_BUTTON_4_UP) != 0)
        queue_mouse_button(hwnd, 3, false);
    if ((buttons & RI_MOUSE_BUTTON_5_DOWN) != 0)
        queue_mouse_button(hwnd, 4, true);
    if ((buttons & RI_MOUSE_BUTTON_5_UP) != 0)
        queue_mouse_button(hwnd, 4, false);
    return true;
}

inline void apply_pending_menu_toggle() {
    const unsigned toggles = g_menu_toggle_count.exchange(0, std::memory_order_acq_rel);
    if ((toggles & 1u) == 0)
        return;

    ModConfig::show_menu = !ModConfig::show_menu;
    sync_menu_state();
}

inline LRESULT CALLBACK wndproc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (g_broker_callback_active)
        g_broker_callback_handled = false;
    WNDPROC old_wndproc = g_old_wndproc;
    InputRelay relayed_input{};
    const UINT relay_message = input_relay_message();
    const bool relayed = relay_message && message == relay_message;
    if (relayed) {
        const auto *input = reinterpret_cast<const InputRelay *>(lparam);
        if (!readable_range(input, sizeof(*input))) {
            log("Received an invalid ImGui input relay payload.");
            g_broker_callback_handled = true;
            return FALSE;
        }
        relayed_input = *input;
        message = relayed_input.message;
        wparam = relayed_input.wparam;
        lparam = relayed_input.lparam;
    }

    const bool visible =
        g_menu_visible.load(std::memory_order_acquire) && !g_shutting_down.load(std::memory_order_acquire);
    const bool wants_mouse = visible && g_imgui_wants_mouse.load(std::memory_order_acquire);
    const bool wants_keyboard = visible && g_imgui_wants_keyboard.load(std::memory_order_acquire);
    bool consumed = false;

    const UINT cursor_message = menu_cursor_message();
    if (cursor_message && message == cursor_message) {
        g_broker_callback_handled = true;
        const bool open = wparam != 0;
        if (set_native_cursor_open(hwnd, open)) {
            g_native_cursor_applied.store(open ? 1 : 0, std::memory_order_release);
            return TRUE;
        }
        int requested = open ? 1 : 0;
        g_native_cursor_requested.compare_exchange_strong(requested, -1, std::memory_order_acq_rel,
                                                          std::memory_order_acquire);
        return FALSE;
    }

    const UINT input_message = menu_input_message();
    if (input_message && message == input_message) {
        apply_window_input_transition(hwnd, old_wndproc, wparam != 0);
        g_broker_callback_handled = true;
        return TRUE;
    }

    track_physical_input(message, wparam, lparam);

    if (is_toggle_key_down(message, wparam, lparam)) {
        queue_menu_toggle();
        consumed = true;
    }

    if (message == WM_KILLFOCUS || message == WM_CANCELMODE) {
        const HWND focusDestination = message == WM_KILLFOCUS ? reinterpret_cast<HWND>(wparam) : GetForegroundWindow();
        if (!is_imgui_platform_window(focusDestination)) {
            clear_window_mouse_buttons(hwnd, true);
            if (message == WM_KILLFOCUS)
                clear_tracked_input();
            InputEvent focus{};
            focus.kind = InputEventKind::focus;
            focus.hwnd = hwnd;
            focus.state = false;
            queue_input_event(focus);
        }
    } else if (message == WM_CAPTURECHANGED) {
        const HWND captureDestination = reinterpret_cast<HWND>(lparam);
        if (!is_imgui_platform_window(captureDestination) && any_window_mouse_button_down())
            clear_window_mouse_buttons(hwnd, false);
    } else if (message == WM_SETFOCUS) {
        InputEvent focus{};
        focus.kind = InputEventKind::focus;
        focus.hwnd = hwnd;
        focus.state = true;
        queue_input_event(focus);
    }

    if (is_mouse_button_message(message)) {
        const int button = mouse_button_for_message(message, wparam);
        const bool down = is_mouse_button_down(message);
        const bool was_down = button >= 0 && g_wndproc_mouse_down[button];
        const bool capture_mouse = wants_mouse || was_down;
        if (button >= 0 && down && capture_mouse) {
            if (!any_window_mouse_button_down() && GetCapture() != hwnd)
                SetCapture(hwnd);
            g_wndproc_mouse_down[button] = true;
            SetFocus(hwnd);
        } else if (button >= 0 && !down) {
            g_wndproc_mouse_down[button] = false;
            if (!any_window_mouse_button_down() && GetCapture() == hwnd)
                ReleaseCapture();
        }

        if (visible && (capture_mouse || !down && was_down)) {
            queue_mouse_button(hwnd, button, down);
        }
        if (capture_mouse)
            consumed = true;
    } else if (message == WM_MOUSEMOVE && visible) {
        if (g_cursor_lease.load(std::memory_order_acquire) || !g_raw_mouse_seen) {
            queue_mouse_client_position(hwnd, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
        }
        consumed = wants_mouse || any_window_mouse_button_down();
    } else if (message == WM_INPUT && visible) {
        if (!g_cursor_lease.load(std::memory_order_acquire))
            queue_raw_mouse_input(hwnd, lparam);
        consumed = wants_mouse || any_window_mouse_button_down();
    } else if ((message == WM_MOUSEWHEEL || message == WM_MOUSEHWHEEL) && visible) {
        InputEvent event{};
        event.kind = InputEventKind::mouse_wheel;
        event.hwnd = hwnd;
        const float delta = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        event.x = message == WM_MOUSEHWHEEL ? -delta : 0.0f;
        event.y = message == WM_MOUSEWHEEL ? delta : 0.0f;
        queue_input_event(event);
        consumed = wants_mouse;
    } else if (message == WM_MOUSELEAVE && visible) {
        InputEvent event{};
        event.kind = InputEventKind::mouse_position;
        event.hwnd = hwnd;
        event.x = -std::numeric_limits<float>::max();
        event.y = -std::numeric_limits<float>::max();
        queue_input_event(event);
        consumed = wants_mouse;
    }

    if (wants_mouse && message == WM_SETCURSOR && LOWORD(lparam) == HTCLIENT) {
        SetCursor(LoadCursor(nullptr, IDC_ARROW));
        consumed = true;
    }

    if (visible && is_keyboard_message(message)) {
        InputEvent event{};
        event.kind = InputEventKind::raw_message;
        event.hwnd = hwnd;
        event.message = message;
        event.wparam = wparam;
        event.lparam = lparam;
        queue_input_event(event);
        consumed = wants_keyboard;
    }

    if (relayed || consumed) {
        const InputRelay input{message, wparam, lparam};
        relay_input_to_previous(hwnd, old_wndproc, input);
        g_broker_callback_handled = true;
        return TRUE;
    }

    track_game_input(message, wparam, lparam);
    if (g_broker_callback_active)
        return FALSE;
    return call_previous_window_proc(hwnd, old_wndproc, message, wparam, lparam);
}

inline std::intptr_t window_message_callback(void *window, std::uint32_t message, std::uintptr_t wparam,
                                             std::intptr_t lparam, int *handled) {
    g_broker_callback_active = true;
    const LRESULT result = wndproc(static_cast<HWND>(window), message, wparam, lparam);
    g_broker_callback_active = false;
    if (handled)
        *handled = g_broker_callback_handled ? 1 : 0;
    return static_cast<std::intptr_t>(result);
}

inline bool init_dx11_imgui(IDXGISwapChain *swap_chain) {
    if (g_imgui_ready)
        return true;
    if (!swap_chain) {
        log("DX11 Present called without a swap chain; UI disabled for this "
            "frame.");
        return false;
    }

    if (FAILED(swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&g_device))) || !g_device) {
        log("DX11 device unavailable from swap chain; UI disabled.");
        return false;
    }

    g_device->GetImmediateContext(&g_context);
    if (!g_context) {
        log("DX11 immediate context unavailable; UI disabled.");
        release_device_objects();
        return false;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    if (!query_swap_chain_desc(swap_chain, &desc)) {
        log("DX11 swap chain description unavailable; UI disabled.");
        release_device_objects();
        return false;
    }
    query_swap_chain_color_space(swap_chain);

    g_hwnd = desc.OutputWindow ? desc.OutputWindow : find_main_window();
    if (!g_hwnd) {
        log("DX11 output window unavailable; UI disabled.");
        release_device_objects();
        return false;
    }

    if (!create_render_target(swap_chain)) {
        release_device_objects();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    // Keep detached viewports owned by the game window. This preserves normal
    // minimize/Alt-Tab/z-order behavior in windowed mode without per-frame
    // TOPMOST toggling, while still allowing windows on another monitor.
    io.ConfigViewportsNoDefaultParent = false;
    log_render_target_diagnostics(desc.BufferDesc.Format);
    apply_swap_chain_color_space(desc.BufferDesc.Format);
    ModUI::initialize_style();

    if (!ImGui_ImplWin32_Init(g_hwnd)) {
        log("ImGui Win32 backend initialization failed; UI disabled.");
        ImGui::DestroyContext();
        release_device_objects();
        return false;
    }

    if (!ImGui_ImplDX11_Init(g_device, g_context)) {
        log("ImGui DX11 backend initialization failed; UI disabled.");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        release_device_objects();
        return false;
    }

    const auto viewportSwapChain = make_dx11_viewport_swap_chain_config(desc);
    if (!viewportSwapChain) {
        log("DX11 game swap-chain descriptor cannot be adapted safely for detached "
            "ImGui viewports.");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        release_device_objects();
        return false;
    }
    ImGui_ImplDX11_SetSwapChainDescs(&viewportSwapChain->descriptor, 1);
    {
        char text[224]{};
        std::snprintf(text, sizeof(text),
                      "DX11 detached viewports configured from game swap chain: effect=%s "
                      "buffers=%u format=%u.",
                      dxgi_swap_effect_name(viewportSwapChain->descriptor.SwapEffect),
                      viewportSwapChain->descriptor.BufferCount,
                      static_cast<unsigned>(viewportSwapChain->descriptor.BufferDesc.Format));
        log(text);
    }
    if (!install_platform_renderer_isolation()) {
        log("DX11 multi-monitor callback isolation failed; UI disabled to avoid an "
            "unsafe render path.");
        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        release_device_objects();
        return false;
    }

    if (!install_window_message_handler()) {
        log("Window-message handler installation failed; UI disabled.");
        ImGui_ImplDX11_Shutdown();
        g_platform_renderer_callbacks = {};
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        release_device_objects();
        g_hwnd = nullptr;
        return false;
    }

    swap_chain->AddRef();
    g_active_swap_chain = swap_chain;
    g_backend = GraphicsBackend::dx11;
    g_imgui_ready = true;
    sync_menu_state();
    log_font_diagnostics("DX11 ImGui", "linear");
    log("DX11 ImGui initialized with docking and multi-monitor viewport "
        "support.");
    return true;
}

inline bool create_dx12_device_objects(IDXGISwapChain *swap_chain) {
    g_dx12_resources.set_diagnostic_sink(&log);
    return g_dx12_resources.create(swap_chain);
}

inline bool init_dx12_imgui(IDXGISwapChain *swap_chain) {
    if (g_imgui_ready)
        return g_backend == GraphicsBackend::dx12;
    if (!g_dx12_resources.has_command_queue()) {
        log("DX12 Present arrived before a graphics command queue was captured; UI "
            "will retry.");
        return false;
    }
    DXGI_SWAP_CHAIN_DESC desc{};
    if (!query_swap_chain_desc(swap_chain, &desc)) {
        log("DX12 output window unavailable; UI disabled.");
        g_dx12_resources.release_device_objects();
        return false;
    }
    g_hwnd = desc.OutputWindow ? desc.OutputWindow : find_main_window();
    if (!g_hwnd || !create_dx12_device_objects(swap_chain)) {
        g_dx12_resources.release_device_objects();
        return false;
    }
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |=
        ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoDefaultParent = false;
    ModUI::initialize_style();
    ImGui_ImplDX12_InitInfo dx12_init_info{};
    dx12_init_info.Device = g_dx12_resources.device();
    dx12_init_info.CommandQueue = g_dx12_resources.command_queue();
    dx12_init_info.NumFramesInFlight = g_dx12_resources.frame_count();
    dx12_init_info.RTVFormat = g_dx12_resources.format();
    dx12_init_info.SrvDescriptorHeap = g_dx12_resources.srv_heap();
    dx12_init_info.SrvDescriptorAllocFn = &Dx12OverlayResources::allocate_srv_descriptor;
    dx12_init_info.SrvDescriptorFreeFn = &Dx12OverlayResources::free_srv_descriptor;
    if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplDX12_Init(&dx12_init_info)) {
        log("DX12 ImGui backend initialization failed; UI disabled.");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_dx12_resources.release_device_objects();
        return false;
    }
    if (!install_platform_renderer_isolation()) {
        log("DX12 multi-monitor callback isolation failed; UI disabled to avoid an "
            "unsafe render path.");
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_dx12_resources.release_device_objects();
        g_hwnd = nullptr;
        return false;
    }
    if (!install_window_message_handler()) {
        log("Window-message handler installation failed; UI disabled.");
        ImGui_ImplDX12_Shutdown();
        g_platform_renderer_callbacks = {};
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_dx12_resources.release_device_objects();
        g_hwnd = nullptr;
        return false;
    }
    swap_chain->AddRef();
    g_active_swap_chain = swap_chain;
    g_backend = GraphicsBackend::dx12;
    g_imgui_ready = true;
    sync_menu_state();
    log_font_diagnostics("DX12 ImGui", "linear");
    log("DX12 ImGui initialized with docking and multi-monitor viewport "
        "support.");
    return true;
}

inline bool init_imgui(IDXGISwapChain *swap_chain) {
    if (g_imgui_ready)
        return g_active_swap_chain == swap_chain;
    DXGI_SWAP_CHAIN_DESC desc{};
    if (!query_swap_chain_desc(swap_chain, &desc) || (desc.OutputWindow && !is_process_main_window(desc.OutputWindow)))
        return false;
    ID3D11Device *dx11_device = nullptr;
    const bool is_dx11 =
        SUCCEEDED(swap_chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void **>(&dx11_device))) &&
        dx11_device;
    if (dx11_device)
        dx11_device->Release();
    return is_dx11 ? init_dx11_imgui(swap_chain) : init_dx12_imgui(swap_chain);
}

inline void render_platform_windows() {
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0)
        return;
    PlatformRendererGuard platformGuard{};
    ImGui::UpdatePlatformWindows();
    collect_platform_windows();
    apply_platform_window_policy();
    pump_platform_window_messages();
    validate_platform_window_topology();
    if (!g_platform_windows.empty())
        ImGui::RenderPlatformWindowsDefault();
}

inline void render_dx12_frame(IDXGISwapChain *swap_chain) {
    (void)swap_chain;
    Dx12FrameSubmission submission{};
    const Dx12BeginFrameStatus beginStatus = g_dx12_resources.begin_frame(&submission);
    if (beginStatus == Dx12BeginFrameStatus::gpu_busy) {
        ++g_dx12_busy_frame_skips;
        const ULONGLONG now = GetTickCount64();
        if (now - g_dx12_busy_diagnostic_tick >= 5000) {
            char text[192]{};
            std::snprintf(text, sizeof(text),
                          "DX12 overlay skipped %llu frame(s) while its command allocator was still in GPU use; "
                          "the game render thread was not blocked.",
                          static_cast<unsigned long long>(g_dx12_busy_frame_skips));
            log(text);
            g_dx12_busy_frame_skips = 0;
            g_dx12_busy_diagnostic_tick = now;
        }
        return;
    }
    if (beginStatus == Dx12BeginFrameStatus::reset_failed) {
        log("DX12 command-list reset failed; UI frame skipped.");
        return;
    }
    if (beginStatus != Dx12BeginFrameStatus::ready)
        return;

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    drain_input_events();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = false;
    ModUI::Highlight::manager().render();
    ModUI::render_menu();
    publish_imgui_capture_state();
    ImGui::Render();
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = submission.backBuffer;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    submission.commandList->ResourceBarrier(1, &barrier);
    submission.commandList->OMSetRenderTargets(1, &submission.renderTarget, FALSE, nullptr);
    ID3D12DescriptorHeap *heaps[] = {g_dx12_resources.srv_heap()};
    submission.commandList->SetDescriptorHeaps(1, heaps);
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), submission.commandList);
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    submission.commandList->ResourceBarrier(1, &barrier);
    if (!g_dx12_resources.submit_frame(submission)) {
        log("DX12 overlay command submission failed; UI frame synchronization is unavailable.");
        return;
    }
    render_platform_windows();
    if (!g_dx12_resources.complete_frame(submission))
        log("DX12 overlay completion fence failed; frame resources will not be reused.");
}

inline void render_frame(IDXGISwapChain *swap_chain) {
    RenderFrameGuard guard{};
    if (!guard.active)
        return;
    std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
    if (g_shutting_down.load(std::memory_order_acquire))
        return;
    if (!init_imgui(swap_chain))
        return;
    if (g_backend == GraphicsBackend::dx11 || g_backend == GraphicsBackend::dx12)
        pump_platform_window_messages();
    if (g_backend == GraphicsBackend::dx12) {
        apply_pending_menu_toggle();
        sync_menu_state();
        render_dx12_frame(swap_chain);
        return;
    }
    if (!g_context) {
        log("DX11 immediate context missing during Present; UI frame skipped.");
        return;
    }
    if (!g_render_target && !create_render_target(swap_chain))
        return;
    apply_pending_menu_toggle();
    sync_menu_state();

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    drain_input_events();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = false;
    ModUI::Highlight::manager().render();
    ModUI::render_menu();
    publish_imgui_capture_state();
    ImGui::Render();

    Dx11OutputMergerStateGuard output_merger_state(g_context);
    g_context->OMSetRenderTargets(1, &g_render_target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    render_platform_windows();
}

inline HRESULT __stdcall detour_present(IDXGISwapChain *swap_chain, UINT sync_interval, UINT flags) {
    if (g_platform_renderer_depth != 0)
        return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;
    struct ScopedPresentDepth {
        unsigned *depth = nullptr;
        explicit ScopedPresentDepth(unsigned *value) : depth(value) {
            if (depth)
                ++(*depth);
        }
        ~ScopedPresentDepth() {
            if (depth)
                --(*depth);
        }
        bool reentrant() const {
            return depth && *depth > 1;
        }
    };
    ScopedPresentDepth depth_guard(&g_present_depth);

    if (depth_guard.reentrant()) {
        return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;
    }
    if (!swap_chain || (flags & DXGI_PRESENT_TEST) != 0)
        return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;
    if (g_imgui_ready && !is_active_game_swap_chain(swap_chain))
        return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;

    render_frame(swap_chain);
    return g_present ? g_present(swap_chain, sync_interval, flags) : DXGI_ERROR_INVALID_CALL;
}

inline HRESULT __stdcall detour_present1(IDXGISwapChain1 *swap_chain, UINT sync_interval, UINT flags,
                                         const DXGI_PRESENT_PARAMETERS *parameters) {
    if (g_platform_renderer_depth != 0)
        return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;
    struct ScopedPresentDepth {
        unsigned *depth = nullptr;
        explicit ScopedPresentDepth(unsigned *value) : depth(value) {
            if (depth)
                ++(*depth);
        }
        ~ScopedPresentDepth() {
            if (depth)
                --(*depth);
        }
        bool reentrant() const {
            return depth && *depth > 1;
        }
    };
    ScopedPresentDepth depth_guard(&g_present_depth);

    if (depth_guard.reentrant()) {
        return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;
    }
    if (!swap_chain || (flags & DXGI_PRESENT_TEST) != 0)
        return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;
    if (g_imgui_ready && !is_active_game_swap_chain(swap_chain))
        return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;

    render_frame(swap_chain);
    return g_present1 ? g_present1(swap_chain, sync_interval, flags, parameters) : DXGI_ERROR_INVALID_CALL;
}

inline HRESULT __stdcall detour_resize_buffers(IDXGISwapChain *swap_chain, UINT buffer_count, UINT width, UINT height,
                                               DXGI_FORMAT format, UINT flags) {
    if (g_platform_renderer_depth != 0) {
        return g_resize_buffers ? g_resize_buffers(swap_chain, buffer_count, width, height, format, flags)
                                : DXGI_ERROR_INVALID_CALL;
    }
    std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
    if (g_imgui_ready && !is_active_game_swap_chain(swap_chain)) {
        return g_resize_buffers ? g_resize_buffers(swap_chain, buffer_count, width, height, format, flags)
                                : DXGI_ERROR_INVALID_CALL;
    }
    if (g_shutting_down.load(std::memory_order_acquire)) {
        return g_resize_buffers ? g_resize_buffers(swap_chain, buffer_count, width, height, format, flags)
                                : DXGI_ERROR_INVALID_CALL;
    }
    if (g_backend == GraphicsBackend::dx12 && !g_dx12_resources.wait_for_idle()) {
        log("DX12 ResizeBuffers postponed because overlay GPU work could not be drained safely.");
        return DXGI_ERROR_WAS_STILL_DRAWING;
    }
    if (g_imgui_ready && g_backend == GraphicsBackend::dx11)
        ImGui_ImplDX11_InvalidateDeviceObjects();
    if (g_imgui_ready && g_backend == GraphicsBackend::dx12)
        ImGui_ImplDX12_InvalidateDeviceObjects();
    if (g_backend == GraphicsBackend::dx11)
        release_render_target();
    if (g_backend == GraphicsBackend::dx12) {
        g_dx12_resources.release_device_objects();
    }
    const HRESULT result = g_resize_buffers ? g_resize_buffers(swap_chain, buffer_count, width, height, format, flags)
                                            : DXGI_ERROR_INVALID_CALL;
    if (SUCCEEDED(result) && g_imgui_ready && g_backend == GraphicsBackend::dx12) {
        if (create_dx12_device_objects(swap_chain)) {
            ImGui_ImplDX12_CreateDeviceObjects();
            log("DX12 swap chain resized; UI resources recreated.");
        } else {
            log("DX12 swap chain resized but UI resource recreation failed; retrying "
                "on next Present.");
        }
    } else if (SUCCEEDED(result) && g_imgui_ready) {
        if (create_render_target(swap_chain)) {
            ImGui_ImplDX11_CreateDeviceObjects();
            log("DX11 swap chain resized; UI render target recreated.");
        } else {
            log("DX11 swap chain resized but render target recreation failed; "
                "retrying on next Present.");
        }
    } else if (FAILED(result)) {
        log(g_backend == GraphicsBackend::dx12
                ? "DX12 ResizeBuffers failed; UI resources will be recreated on the next successful resize."
                : "DX11 ResizeBuffers failed; UI render target will be recreated on the next Present.");
    }
    return result;
}

inline void __stdcall detour_execute_command_lists(ID3D12CommandQueue *command_queue, UINT count,
                                                   ID3D12CommandList *const *command_lists) {
    if (g_platform_renderer_depth != 0) {
        if (g_execute_command_lists)
            g_execute_command_lists(command_queue, count, command_lists);
        return;
    }
    if (command_queue && command_queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT &&
        !g_dx12_queue_captured.load(std::memory_order_acquire)) {
        std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
        if (g_dx12_resources.capture_command_queue(command_queue)) {
            g_dx12_queue_captured.store(true, std::memory_order_release);
            log("DX12 graphics command queue captured.");
        }
    }
    if (g_execute_command_lists)
        g_execute_command_lists(command_queue, count, command_lists);
}

inline bool init_opengl_imgui(HDC device_context) {
    if (g_imgui_ready)
        return g_backend == GraphicsBackend::opengl && g_gl_context == wglGetCurrentContext();
    if (!device_context || !wglGetCurrentContext())
        return false;
    g_hwnd = WindowFromDC(device_context);
    if (!g_hwnd) {
        log("OpenGL output window unavailable; UI disabled.");
        return false;
    }
    g_gl_context = wglGetCurrentContext();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_DockingEnable;
    ModUI::initialize_style();
    if (!ImGui_ImplWin32_Init(g_hwnd) || !ImGui_ImplOpenGL3_Init("#version 130")) {
        log("OpenGL ImGui backend initialization failed; UI disabled.");
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_hwnd = nullptr;
        return false;
    }
    if (!install_window_message_handler()) {
        log("Window-message handler installation failed; UI disabled.");
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        g_hwnd = nullptr;
        return false;
    }
    g_backend = GraphicsBackend::opengl;
    g_imgui_ready = true;
    sync_menu_state();
    log_font_diagnostics("OpenGL ImGui", "linear");
    log("OpenGL ImGui initialized with docking support.");
    return true;
}

inline void render_opengl_frame(HDC device_context) {
    RenderFrameGuard guard{};
    if (!guard.active)
        return;
    std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
    if (g_shutting_down.load(std::memory_order_acquire) ||
        (g_backend != GraphicsBackend::none && g_backend != GraphicsBackend::opengl) ||
        !init_opengl_imgui(device_context))
        return;
    apply_pending_menu_toggle();
    sync_menu_state();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    drain_input_events();
    ImGui::NewFrame();
    ImGui::GetIO().MouseDrawCursor = false;
    ModUI::Highlight::manager().render();
    ModUI::render_menu();
    publish_imgui_capture_state();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

inline BOOL WINAPI detour_wgl_swap_buffers(HDC device_context) {
    thread_local unsigned swap_depth = 0;
    ++swap_depth;
    if (swap_depth == 1)
        render_opengl_frame(device_context);
    --swap_depth;
    return g_wgl_swap_buffers ? g_wgl_swap_buffers(device_context) : FALSE;
}

inline bool install() {
    std::lock_guard<std::mutex> install_lock(g_install_mutex);
    g_shutting_down.store(false, std::memory_order_release);
    if (g_present_hooked || g_present1_hooked || g_wgl_swap_buffers_hooked)
        return true;
    if (!URK::hooks::available()) {
        log("hook API unavailable; UI not installed.");
        return false;
    }

    const std::int32_t graphics_device = URK::graphics_device_type();
    const bool probe_native_presentation = graphics_device == URK::graphics_device_unknown;
    const bool want_dx11 = probe_native_presentation || graphics_device == URK::graphics_device_direct3d11;
    const bool want_dx12 = probe_native_presentation || graphics_device == URK::graphics_device_direct3d12;
    const bool want_opengl = probe_native_presentation || graphics_device == URK::graphics_device_opengl2 ||
                             graphics_device == URK::graphics_device_openglcore;

    if (!want_dx11 && !want_dx12 && !want_opengl) {
        char text[160]{};
        std::snprintf(text, sizeof(text),
                      "Unsupported or unavailable Unity graphics device type (%d); "
                      "UI render hook not installed.",
                      static_cast<int>(graphics_device));
        log(text);
        g_install_failed = true;
        return false;
    }

    if (probe_native_presentation)
        log("Unity graphics device type is unavailable; probing native DXGI and "
            "OpenGL presentation hooks.");

    if ((want_dx11 || want_dx12) && !g_dxgi_targets_discovered) {
        g_cached_dxgi_targets = discover_dxgi_hook_targets(want_dx12, &log);
        g_dxgi_targets_discovered = true;
    }
    const DxgiVTableTargets targets = g_cached_dxgi_targets;
    if ((want_dx11 || want_dx12) && (targets.present || targets.present1) && targets.resizeBuffers) {
        if (targets.present) {
            g_present = reinterpret_cast<PresentFn>(targets.present);
            if (!URK::hooks::attach_ex(reinterpret_cast<void **>(&g_present), reinterpret_cast<void *>(&detour_present),
                                       URK::hook_backend_auto)) {
                g_present = nullptr;
                log("DXGI Present hook attach failed; D3D overlay unavailable.");
            } else {
                g_present_hooked = true;
            }
        }
        if (targets.present1 && targets.present1 != targets.present) {
            g_present1 = reinterpret_cast<Present1Fn>(targets.present1);
            if (!URK::hooks::attach_ex(reinterpret_cast<void **>(&g_present1),
                                       reinterpret_cast<void *>(&detour_present1), URK::hook_backend_auto)) {
                g_present1 = nullptr;
                log("DXGI Present1 hook attach failed; D3D12 overlay may be "
                    "unavailable.");
            } else {
                g_present1_hooked = true;
            }
        }
    }

    if (g_present_hooked || g_present1_hooked) {
        g_resize_buffers = reinterpret_cast<ResizeBuffersFn>(targets.resizeBuffers);
        if (URK::hooks::attach_ex(reinterpret_cast<void **>(&g_resize_buffers),
                                  reinterpret_cast<void *>(&detour_resize_buffers), URK::hook_backend_auto)) {
            g_resize_hooked = true;
        } else {
            log("DXGI ResizeBuffers hook attach failed; detaching presentation hooks "
                "for resize safety.");
            if (g_present1_hooked) {
                URK::hooks::detach_ex(reinterpret_cast<void **>(&g_present1),
                                      reinterpret_cast<void *>(&detour_present1));
                g_present1 = nullptr;
                g_present1_hooked = false;
            }
            if (g_present_hooked) {
                URK::hooks::detach_ex(reinterpret_cast<void **>(&g_present), reinterpret_cast<void *>(&detour_present));
                g_present = nullptr;
                g_present_hooked = false;
            }
        }
    } else if ((want_dx11 || want_dx12) && ((!targets.present && !targets.present1) || !targets.resizeBuffers)) {
        log("DXGI Present, Present1, or ResizeBuffers unavailable; D3D overlay "
            "unavailable.");
    }

    if (want_dx12 && (g_present_hooked || g_present1_hooked)) {
        void *execute_target = discover_dx12_execute_command_lists_target(&log);
        if (!execute_target) {
            log("DX12 ExecuteCommandLists unavailable; DX12 overlay unavailable.");
        } else {
            g_execute_command_lists = reinterpret_cast<ExecuteCommandListsFn>(execute_target);
            if (URK::hooks::attach_ex(reinterpret_cast<void **>(&g_execute_command_lists),
                                      reinterpret_cast<void *>(&detour_execute_command_lists),
                                      URK::hook_backend_auto)) {
                g_execute_command_lists_hooked = true;
            } else {
                g_execute_command_lists = nullptr;
                log("DX12 ExecuteCommandLists hook attach failed; DX12 overlay "
                    "unavailable.");
            }
        }
        if (!g_execute_command_lists_hooked) {
            if (probe_native_presentation) {
                log("DX12 command queue hook unavailable during native probing; "
                    "retaining DXGI Present for DX11.");
            } else {
                if (g_resize_hooked) {
                    URK::hooks::detach_ex(reinterpret_cast<void **>(&g_resize_buffers),
                                          reinterpret_cast<void *>(&detour_resize_buffers));
                    g_resize_hooked = false;
                }
                if (g_present1_hooked) {
                    URK::hooks::detach_ex(reinterpret_cast<void **>(&g_present1),
                                          reinterpret_cast<void *>(&detour_present1));
                    g_present1_hooked = false;
                }
                if (g_present_hooked) {
                    URK::hooks::detach_ex(reinterpret_cast<void **>(&g_present),
                                          reinterpret_cast<void *>(&detour_present));
                    g_present_hooked = false;
                }
                g_present = nullptr;
                g_present1 = nullptr;
                g_resize_buffers = nullptr;
            }
        }
    }

    if (want_opengl) {
        HMODULE opengl32 = GetModuleHandleA("opengl32.dll");
        if (opengl32) {
            g_wgl_swap_buffers = reinterpret_cast<WglSwapBuffersFn>(GetProcAddress(opengl32, "wglSwapBuffers"));
            if (g_wgl_swap_buffers &&
                URK::hooks::attach_ex(reinterpret_cast<void **>(&g_wgl_swap_buffers),
                                      reinterpret_cast<void *>(&detour_wgl_swap_buffers), URK::hook_backend_auto)) {
                g_wgl_swap_buffers_hooked = true;
            } else {
                g_wgl_swap_buffers = nullptr;
                log("OpenGL wglSwapBuffers hook attach failed; OpenGL overlay "
                    "unavailable.");
            }
        } else {
            log("opengl32.dll is not loaded by the process; OpenGL overlay "
                "unavailable.");
        }
    }

    if (!g_present_hooked && !g_present1_hooked && !g_wgl_swap_buffers_hooked) {
        log("No supported render presentation hook could be installed.");
        return false;
    }
    if (probe_native_presentation)
        log("Native presentation hooks installed; UI will initialize on the first "
            "compatible render context.");
    else if (want_dx11)
        log("DX11 render hooks installed by generated project.");
    else if (want_dx12)
        log("DX12 render hooks installed by generated project.");
    else
        log("OpenGL render hooks installed by generated project.");
    return true;
}

inline void unregister_install_callback() {
    if (!g_install_callback_registered || !g_mod_context)
        return;
    if (g_mod_context->size >=
            offsetof(URK_ModContext, MainThreadUnregister) + sizeof(g_mod_context->MainThreadUnregister) &&
        g_mod_context->MainThreadUnregister) {
        g_mod_context->MainThreadUnregister(&install_on_main_thread);
    }
    g_install_callback_registered = false;
}

inline void install_on_main_thread() {
    if (g_shutting_down.load(std::memory_order_acquire)) {
        unregister_install_callback();
        return;
    }
    if (g_install_complete || g_install_failed) {
        unregister_install_callback();
        return;
    }

    if (install()) {
        g_install_complete = true;
        unregister_install_callback();
        return;
    }

    ++g_graphics_probe_attempts;
    if (g_install_failed || g_graphics_probe_attempts >= kMaxGraphicsProbeAttempts) {
        if (!g_install_failed)
            log("Unity graphics device type did not become available; UI render hook "
                "not installed.");
        g_install_failed = true;
        unregister_install_callback();
    }
}

bool install(const URK_ModContext *ctx) {
    g_mod_context = ctx;
    URK::set_context(ctx);
    if (g_install_complete)
        return true;
    if (g_install_failed)
        return false;
    if (!ctx) {
        log("mod context is unavailable; UI render hook not installed.");
        g_install_failed = true;
        return false;
    }
    if (ctx->size < offsetof(URK_ModContext, MainThreadRegister) + sizeof(ctx->MainThreadRegister) ||
        !ctx->MainThreadRegister || !URK::has_main_thread()) {
        log("main-thread dispatcher unavailable; installing native presentation "
            "hooks without Unity graphics-device query.");
        if (!install()) {
            log("native presentation hook installation failed; UI render hook not "
                "installed.");
            g_install_failed = true;
            return false;
        }
        g_install_complete = true;
        return true;
    }
    if (!g_install_callback_registered) {
        g_install_callback_registered = ctx->MainThreadRegister(&install_on_main_thread) != 0;
        if (!g_install_callback_registered) {
            log("main-thread render hook install callback registration failed.");
            g_install_failed = true;
            return false;
        }
        log("graphics-specific ImGui hook selection scheduled on the Unity main "
            "thread.");
    }
    return true;
}

bool uninstall() {
    g_shutting_down.store(true, std::memory_order_release);
    unregister_install_callback();
    g_menu_toggle_count.store(0, std::memory_order_release);
    bool all_hooks_detached = true;
    if (g_wgl_swap_buffers_hooked) {
        if (URK::hooks::detach_ex(reinterpret_cast<void **>(&g_wgl_swap_buffers),
                                  reinterpret_cast<void *>(&detour_wgl_swap_buffers))) {
            g_wgl_swap_buffers_hooked = false;
            log("OpenGL wglSwapBuffers hook detached.");
        } else {
            all_hooks_detached = false;
        }
    }
    if (g_execute_command_lists_hooked) {
        if (URK::hooks::detach_ex(reinterpret_cast<void **>(&g_execute_command_lists),
                                  reinterpret_cast<void *>(&detour_execute_command_lists))) {
            g_execute_command_lists_hooked = false;
            log("DX12 ExecuteCommandLists hook detached.");
        } else {
            all_hooks_detached = false;
        }
    }
    if (g_resize_hooked) {
        if (URK::hooks::detach_ex(reinterpret_cast<void **>(&g_resize_buffers),
                                  reinterpret_cast<void *>(&detour_resize_buffers))) {
            g_resize_hooked = false;
            log("DXGI ResizeBuffers hook detached.");
        } else {
            all_hooks_detached = false;
        }
    }
    if (g_present_hooked) {
        if (URK::hooks::detach_ex(reinterpret_cast<void **>(&g_present), reinterpret_cast<void *>(&detour_present))) {
            g_present_hooked = false;
            log("DXGI Present hook detached.");
        } else {
            all_hooks_detached = false;
        }
    }
    if (g_present1_hooked) {
        if (URK::hooks::detach_ex(reinterpret_cast<void **>(&g_present1), reinterpret_cast<void *>(&detour_present1))) {
            g_present1_hooked = false;
            log("DXGI Present1 hook detached.");
        } else {
            all_hooks_detached = false;
        }
    }
    if (!all_hooks_detached) {
        log("Render hook detach was incomplete; skipping ImGui teardown while the "
            "loader retains the module.");
        return false;
    }

    const DWORD current_thread = GetCurrentThreadId();
    const DWORD render_thread = g_render_thread_id.load(std::memory_order_acquire);
    if (current_thread != render_thread) {
        for (unsigned attempt = 0; attempt < 200 && g_active_frames.load(std::memory_order_acquire) != 0; ++attempt) {
            Sleep(1);
        }
        if (g_active_frames.load(std::memory_order_acquire) != 0) {
            log("Render UI shutdown timed out waiting for an active frame; skipping "
                "ImGui teardown to avoid deadlock.");
            g_present = nullptr;
            g_present1 = nullptr;
            g_resize_buffers = nullptr;
            return false;
        }
    }

    {
        std::lock_guard<std::recursive_mutex> lock(g_imgui_mutex);
        shutdown_imgui();
    }
    g_present = nullptr;
    g_present1 = nullptr;
    g_resize_buffers = nullptr;
    g_execute_command_lists = nullptr;
    g_wgl_swap_buffers = nullptr;
    g_render_thread_id.store(0, std::memory_order_release);
    g_install_complete = false;
    g_install_failed = false;
    g_graphics_probe_attempts = 0;
    g_mod_context = nullptr;
    return true;
}
} // namespace ModRenderHook
)URK";
}
