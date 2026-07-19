#pragma once

namespace ModConfig {
inline constexpr const char* project_name = "BlankMono";
// Stable namespace for this mod's deployed resources. Do not change it after release.
inline constexpr const char* mod_id = "BlankMono_8584472b";
inline constexpr const char* display_name = "BlankMono";
inline constexpr const char* author = "";
inline constexpr const char* version = "0.1.0";
inline constexpr const char* url = "";
inline constexpr const char* social = "";
inline constexpr const char* description = "Generated URKit Mono native mod";
inline constexpr bool is_il2cpp_backend = false;
inline bool show_menu = true;
// English is used as the fixed language when localization support is not generated.
inline bool enable_localization = false;
inline constexpr const char* default_language = "en";
inline bool enable_unity_log_hook = true;
// Win32 virtual-key code used by the generated ImGui WndProc toggle.
// Default: VK_TAB (0x09). Change this value to customize the menu key.
inline int menu_toggle_key = 0x09;
} // namespace ModConfig
