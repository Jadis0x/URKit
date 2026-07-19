#pragma once

#include "config/mod_config.h"
#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/hook_api.h"
#include "sdk/mono/mono_runtime.h"
#include "sdk/mono/mono_helpers.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace ModUnityLogHook {
using DebugLogFn = void(*)(void* message);

inline DebugLogFn g_originals[3]{};
inline bool g_installed = false;

enum class LogLevel { info, warning, error };

inline std::string message_text(void* message) {
  if (!message)
    return "<null>";

  auto* object = static_cast<URK::mono::Object*>(message);
  const auto* klass = URK::mono::object_get_class(object);
  const char* namespc = klass ? URK::mono::class_get_namespace(klass) : nullptr;
  const char* name = klass ? URK::mono::class_get_name(klass) : nullptr;
  if (namespc && name && std::strcmp(namespc, "System") == 0 &&
      std::strcmp(name, "String") == 0) {
    return URK::mono::helpers::to_utf8(
        static_cast<URK::mono::String*>(message), "<unreadable Unity log message>");
  }

  char fallback[192]{};
  std::snprintf(fallback, sizeof(fallback), "<%s%s%s object at %p>",
                namespc && namespc[0] ? namespc : "",
                namespc && namespc[0] ? "." : "", name && name[0] ? name : "unknown",
                message);
  return fallback;
}

inline void write(LogLevel level, void* message) {
  const std::string text = message_text(message);
  switch (level) {
  case LogLevel::warning: ModLog::warn("[Unity] %s", text.c_str()); break;
  case LogLevel::error: ModLog::error("[Unity] %s", text.c_str()); break;
  default: ModLog::info("[Unity] %s", text.c_str()); break;
  }
}

inline void detour_log(void* message) {
  write(LogLevel::info, message);
  if (g_originals[0])
    g_originals[0](message);
}

inline void detour_warning(void* message) {
  write(LogLevel::warning, message);
  if (g_originals[1])
    g_originals[1](message);
}

inline void detour_error(void* message) {
  write(LogLevel::error, message);
  if (g_originals[2])
    g_originals[2](message);
}

inline constexpr const char* k_method_names[] = {"Log", "LogWarning", "LogError"};
inline DebugLogFn k_detours[] = {&detour_log, &detour_warning, &detour_error};

inline bool attach(const char* image_name, const char* method_name,
                   DebugLogFn* original, DebugLogFn detour) {
  const char* parameter_types[] = {"System.Object"};
  const URK::mono::Method* method = nullptr;
  if (!URK::mono::helpers::require_method_exact(image_name,
                                                "UnityEngine",
                                                "Debug",
                                                method_name,
                                                parameter_types,
                                                1,
                                                "System.Void",
                                                method,
                                                [](const char* text) {
                                                  ModLog::warn("%s", text ? text : "");
                                                })) {
    return false;
  }

  void* target = URK::mono::compile_method(method);
  if (!target) {
    ModLog::warn("Mono could not compile UnityEngine.Debug::%s", method_name);
    return false;
  }

  *original = reinterpret_cast<DebugLogFn>(target);
  if (!URK::hooks::attach_ex(reinterpret_cast<void**>(original),
                             reinterpret_cast<void*>(detour),
                             URK::hook_backend_auto)) {
    *original = nullptr;
    ModLog::warn("Mono UnityEngine.Debug::%s hook attach failed", method_name);
    return false;
  }
  return true;
}

inline void detach(DebugLogFn* original, DebugLogFn detour) {
  if (*original)
    URK::hooks::detach_ex(reinterpret_cast<void**>(original),
                          reinterpret_cast<void*>(detour));
  *original = nullptr;
}

inline bool try_install_for_image(const char* image_name) {
  for (std::size_t index = 0; index < 3; ++index) {
    if (attach(image_name, k_method_names[index], &g_originals[index], k_detours[index]))
      continue;
    while (index > 0) {
      --index;
      detach(&g_originals[index], k_detours[index]);
    }
    return false;
  }
  return true;
}

inline bool install(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  if (!ctx || !ModConfig::enable_unity_log_hook)
    return true;
  if (g_installed)
    return true;
  if (!URK::mono::init(ctx) || !URK::hooks::available()) {
    ModLog::warn("Mono Unity log hooks are unavailable");
    return false;
  }

  g_installed = try_install_for_image("UnityEngine.CoreModule.dll") ||
                try_install_for_image("UnityEngine.dll");
  if (!g_installed)
    ModLog::warn("Unity Log/LogWarning/LogError hooks were not installed");
  return g_installed;
}

inline void uninstall() {
  if (g_installed) {
    for (std::size_t index = 3; index > 0; --index)
      detach(&g_originals[index - 1], k_detours[index - 1]);
  }
  g_installed = false;
}
} // namespace ModUnityLogHook
