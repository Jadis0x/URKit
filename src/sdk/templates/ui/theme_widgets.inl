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


