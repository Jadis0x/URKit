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

// Once per session, until the menu is first opened; drawn without a window, so it takes no input.
inline void render_menu_hint() {
    // Time on screen; a frame's share is capped, so a loading stall does not use it up.
    static float age = 0.0f;
    static bool done = false;
    if (done || ModConfig::menu_toggle_key == 0)
        return;
    if (ModConfig::show_menu) {
        done = true;
        return;
    }
    constexpr float kVisible = 8.0f;
    constexpr float kFade = 1.0f;
    const float delta = ImGui::GetIO().DeltaTime;
    age += delta < 0.1f ? delta : 0.1f;
    if (age >= kVisible) {
        done = true;
        return;
    }
    const float alpha = age > kVisible - kFade ? (kVisible - age) / kFade : 1.0f;
    const auto faded = [alpha](ImVec4 color) {
        color.w *= alpha;
        return ImGui::GetColorU32(color);
    };
    Localization::initialize();
    const std::string text = Localization::format(
        "menu.hint", {{"name", ModConfig::display_name}, {"key", Widgets::key_name(ModConfig::menu_toggle_key)}});
    const Theme::Palette &p = Theme::palette();
    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const float pad = 10.0f * Theme::dpi_scale();
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 min(viewport->Pos.x + pad * 2.0f, viewport->Pos.y + pad * 2.0f);
    const ImVec2 max(min.x + size.x + pad * 2.0f, min.y + size.y + pad * 2.0f);
    ImDrawList *dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled(min, max, faded(p.bg_overlay), Theme::radius().xl);
    dl->AddRect(min, max, faded(p.border_subtle), Theme::radius().xl);
    dl->AddText(ImVec2(min.x + pad, min.y + pad), faded(p.text_primary), text.c_str());
}

inline void render_menu() {
    render_menu_hint();
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


