std::string ModLifecycleHeader() {
    return R"URK(#pragma once

#include "sdk/mod_sdk.h"

namespace ModLifecycle {
bool initialize(const URK_ModContext *ctx);
void on_scene_loaded(const URK_SceneInfo *scene);
void on_scene_changed(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene);
void on_object_destroy_requested(const URK_ObjectDestroyRequest *request);
void shutdown();
} // namespace ModLifecycle
)URK";
}

std::string ModLifecycleSource(const ModuleProjectOptions &options) {
    const std::string apiField = BackendApiField(options);
    const std::string apiName = options.backendNamespace == "URK::mono" ? "Mono" : "IL2CPP";
    std::ostringstream out;
    out << "#include \"mod_lifecycle.h\"\n\n"
        << "#include \"lifecycle/mod_runtime.h\"\n"
        << "#include \"hooks/mod_hooks.h\"\n"
        << "#include \"support/mod_log.h\"\n\n"
        << "#include \"lifecycle/mod_network.h\"\n\n"
        << "#include \"sdk/mod_async.h\"\n\n"
        << "#include <cstddef>\n"
        << "#include <utility>\n\n"
        << "#include \"sdk/runtime_api.h\"\n"
        << "#include \"" << BackendRuntimeHeader(options) << "\"\n\n"
        << "namespace {\n"
        << "const URK_ModContext* g_ctx = nullptr;\n"
        << "bool g_update_registered = false;\n"
        << "bool g_network_initialized = false;\n"
        << "bool g_runtime_started = false;\n"
        << "bool g_initialized = false;\n\n"
        << "URK::coroutines::FlowState g_coroutines{};\n\n"
        << "constexpr std::size_t field_end(std::size_t offset, std::size_t size) {\n"
        << "  return offset + size;\n"
        << "}\n\n"
        << "bool context_has_field(const URK_ModContext* ctx, std::size_t end) {\n"
        << "  return ctx && ctx->size >= end;\n"
        << "}\n\n"
        << "void runtime_update() {\n"
        << "  URK::coroutines::tick(g_coroutines);\n"
        << "  ModRuntime::update();\n"
        << "}\n\n"
        << "bool validate_required_backend(const URK_ModContext* ctx) {\n"
        << "  if (!ctx) return false;\n"
        << "  if (ctx->version < URK_SDK_VERSION) {\n"
        << "    ModLog::error(\"mod context SDK version is too old: got=%d required>=%d\", ctx->version, URK_SDK_VERSION);\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (ctx->size < sizeof(URK_ModContext)) {\n"
        << "    ModLog::error(\"mod context table is too small: got=%u required>=%zu\", ctx->size, sizeof(URK_ModContext));\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, runtimeBackend), "
           "sizeof(ctx->runtimeBackend)))) {\n"
        << "    ModLog::error(\"mod context is too small for runtimeBackend; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (ctx->runtimeBackend != "
        << (options.requiredBackendConstant.empty() ? "URK::runtime_backend_unknown" : options.requiredBackendConstant)
        << ") {\n"
        << "    ModLog::error(\"required backend mismatch: generated for " << EscapeString(options.backendDisplayName)
        << " but loader runtimeBackend=%u; refusing to initialize\", ctx->runtimeBackend);\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, runtimeCapabilities), "
           "sizeof(ctx->runtimeCapabilities)))) {\n"
        << "    ModLog::error(\"mod context is too small for runtimeCapabilities; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if ((ctx->runtimeCapabilities & "
        << (options.requiredCapabilityConstant.empty() ? "URK::runtime_cap_none" : options.requiredCapabilityConstant)
        << ") == 0) {\n"
        << "    ModLog::error(\"required " << EscapeString(options.backendDisplayName)
        << " API capability is missing; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!context_has_field(ctx, field_end(offsetof(URK_ModContext, " << apiField << "), sizeof(ctx->"
        << apiField << ")))) {\n"
        << "    ModLog::error(\"mod context is too small for " << apiName << " API table; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (!ctx->" << apiField << ") {\n"
        << "    ModLog::error(\"required " << apiName << " API table is missing; refusing to initialize\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  if (ctx->" << apiField << "->version < URK_"
        << (options.backendNamespace == "URK::mono" ? "MONO" : "IL2CPP")
        << "_API_VERSION || ctx->" << apiField << "->size < sizeof(*ctx->" << apiField << ")) {\n"
        << "    ModLog::error(\"required " << apiName
        << " API table is incompatible: version=%d size=%u\", ctx->" << apiField << "->version, ctx->"
        << apiField << "->size);\n"
        << "    return false;\n"
        << "  }\n"
        << "  return true;\n"
        << "}\n"
        << "} // namespace\n\n"
        << "namespace ModAsync {\n"
        << "URK::coroutines::FlowState& flow() { return g_coroutines; }\n"
        << "void spawn(URK::coroutines::Task task) {\n"
        << "  URK::coroutines::spawn(g_coroutines, std::move(task));\n"
        << "}\n"
        << "void cancel_all() { URK::coroutines::cancel_all(g_coroutines); }\n"
        << "} // namespace ModAsync\n\n"
        << "namespace ModLifecycle {\n"
        << "bool initialize(const URK_ModContext* ctx) {\n"
        << "  if (!ctx) return false;\n"
        << "  if (g_initialized) {\n"
        << "    ModLog::error(\"ModInitEx was invoked more than once; refusing duplicate initialization\");\n"
        << "    return false;\n"
        << "  }\n"
        << "  g_ctx = ctx;\n"
        << "  ModLog::initialize(ctx);\n"
        << "  if (!validate_required_backend(ctx)) { g_ctx = nullptr; ModLog::shutdown(); return false; }\n\n"
        << "  URK::set_context(ctx);\n"
        << "  if (!ModNetwork::init(ctx)) { URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }\n"
        << "  g_network_initialized = true;\n"
        << "  if (!ModRuntime::start(ctx)) { ModNetwork::shutdown(); g_network_initialized = false; URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }\n"
        << "  g_runtime_started = true;\n"
        << "  if (!ModHooks::install(ctx)) { ModRuntime::stop(); g_runtime_started = false; ModNetwork::shutdown(); g_network_initialized = false; URK::set_context(nullptr); g_ctx = nullptr; ModLog::shutdown(); return false; }\n\n"
        << "  const bool can_register_update = "
           "context_has_field(ctx, field_end(offsetof(URK_ModContext, MainThreadRegister), "
           "sizeof(ctx->MainThreadRegister))) && ctx->MainThreadRegister;\n"
        << "  if (can_register_update) {\n"
        << "    g_update_registered = ctx->MainThreadRegister(&runtime_update) != 0;\n"
        << "    if (!g_update_registered)\n"
        << "      ModLog::warn(\"main-thread update registration failed\");\n"
        << "  } else {\n"
        << "    ModLog::warn(\"main-thread dispatcher unavailable; update() will not be called automatically\");\n"
        << "  }\n\n"
        << "  g_initialized = true;\n"
        << "  ModLog::info(\"mod initialized\");\n"
        << "  return true;\n"
        << "}\n\n"
        << "void on_scene_loaded(const URK_SceneInfo* scene) {\n"
        << "  ModRuntime::on_scene_loaded(scene);\n"
        << "}\n\n"
        << "void on_scene_changed(const URK_SceneInfo* previousScene,\n"
        << "                      const URK_SceneInfo* currentScene) {\n"
        << "  ModRuntime::on_scene_changed(previousScene, currentScene);\n"
        << "}\n\n"
        << "void on_object_destroy_requested(const URK_ObjectDestroyRequest* request) {\n"
        << "  ModRuntime::on_object_destroy_requested(request);\n"
        << "}\n\n"
        << "void shutdown() {\n"
        << "  if (!g_initialized && !g_runtime_started && !g_network_initialized) return;\n"
        << "  if (g_update_registered && g_ctx && "
           "context_has_field(g_ctx, field_end(offsetof(URK_ModContext, MainThreadUnregister), "
           "sizeof(g_ctx->MainThreadUnregister))) && "
           "g_ctx->MainThreadUnregister) {\n"
        << "    g_ctx->MainThreadUnregister(&runtime_update);\n"
        << "  }\n"
        << "  g_update_registered = false;\n\n"
        << "  ModAsync::cancel_all();\n"
        << "  ModHooks::uninstall();\n"
        << "  if (g_runtime_started) ModRuntime::stop();\n"
        << "  if (g_network_initialized) ModNetwork::shutdown();\n"
        << "  g_runtime_started = false;\n"
        << "  g_network_initialized = false;\n"
        << "  g_initialized = false;\n"
        << "  ModLog::info(\"mod shutdown\");\n"
        << "  ModLog::shutdown();\n"
        << "  URK::set_context(nullptr);\n"
        << "  g_ctx = nullptr;\n"
        << "}\n"
        << "} // namespace ModLifecycle\n";
    return out.str();
}

std::string ModEntrySource() {
    return R"URK(#include "mod_lifecycle.h"

#include "config/mod_config.h"

#if defined(_WIN32)
#define URK_EXPORT __declspec(dllexport)
#else
#define URK_EXPORT __attribute__((visibility("default")))
#endif

extern "C" URK_EXPORT const URK_ModInfo *URKGetModInfo() {
    static const URK_ModInfo info{
        ModConfig::project_name, ModConfig::display_name, ModConfig::author,
        ModConfig::version,      ModConfig::url,          ModConfig::description,
    };
    return &info;
}

extern "C" URK_EXPORT int ModInitEx(const URK_ModContext *ctx) {
    return ModLifecycle::initialize(ctx) ? 1 : 0;
}

extern "C" URK_EXPORT void OnSceneLoaded(const URK_SceneInfo *scene) {
    ModLifecycle::on_scene_loaded(scene);
}

extern "C" URK_EXPORT void OnSceneChanged(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene) {
    ModLifecycle::on_scene_changed(previousScene, currentScene);
}

extern "C" URK_EXPORT void OnObjectDestroyRequested(const URK_ObjectDestroyRequest *request) {
    ModLifecycle::on_object_destroy_requested(request);
}

extern "C" URK_EXPORT void ModShutdown() {
    ModLifecycle::shutdown();
}
)URK";
}

std::string UnityLogHookModule(const ModuleProjectOptions &options) {
    std::ostringstream out;
    out << "#pragma once\n\n"
        << "#include \"config/mod_config.h\"\n"
        << "#include \"support/mod_log.h\"\n\n"
        << "#include \"sdk/runtime_api.h\"\n"
        << "#include \"sdk/hook_api.h\"\n"
        << "#include \"" << BackendRuntimeHeader(options) << "\"\n"
        << "#include \"" << BackendHelperHeader(options) << "\"\n\n"
        << "#include <cstdio>\n"
        << "#include <cstring>\n"
        << "#include <string>\n\n"
        << "namespace ModUnityLogHook {\n";

    if (options.backendNamespace == "URK::il2cpp") {
        out << R"URK(using DebugLogFn = void(__fastcall *)(void *message, void *method_info);

inline DebugLogFn g_originals[3]{};
inline bool g_installed = false;

enum class LogLevel {
    info,
    warning,
    error
};

inline std::string message_text(void *message) {
    if (!message)
        return "<null>";

    auto *object = static_cast<URK::il2cpp::Object *>(message);
    const auto *klass = URK::il2cpp::object_get_class(object);
    const char *namespc = klass ? URK::il2cpp::class_get_namespace(klass) : nullptr;
    const char *name = klass ? URK::il2cpp::class_get_name(klass) : nullptr;
    if (namespc && name && std::strcmp(namespc, "System") == 0 && std::strcmp(name, "String") == 0) {
        return URK::il2cpp::helpers::to_utf8(static_cast<URK::il2cpp::String *>(message),
                                             "<unreadable Unity log message>");
    }

    char fallback[192]{};
    std::snprintf(fallback, sizeof(fallback), "<%s%s%s object at %p>", namespc && namespc[0] ? namespc : "",
                  namespc && namespc[0] ? "." : "", name && name[0] ? name : "unknown", message);
    return fallback;
}

inline void write(LogLevel level, void *message) {
    const std::string text = message_text(message);
    switch (level) {
        case LogLevel::warning:
            ModLog::warn("[Unity] %s", text.c_str());
            break;
        case LogLevel::error:
            ModLog::error("[Unity] %s", text.c_str());
            break;
        default:
            ModLog::info("[Unity] %s", text.c_str());
            break;
    }
}

inline void __fastcall detour_log(void *message, void *method_info) {
    write(LogLevel::info, message);
    if (g_originals[0])
        g_originals[0](message, method_info);
}

inline void __fastcall detour_warning(void *message, void *method_info) {
    write(LogLevel::warning, message);
    if (g_originals[1])
        g_originals[1](message, method_info);
}

inline void __fastcall detour_error(void *message, void *method_info) {
    write(LogLevel::error, message);
    if (g_originals[2])
        g_originals[2](message, method_info);
}

inline constexpr const char *k_method_names[] = {"Log", "LogWarning", "LogError"};
inline DebugLogFn k_detours[] = {&detour_log, &detour_warning, &detour_error};

inline bool attach(const char *image_name, const char *method_name, DebugLogFn *original, DebugLogFn detour) {
    return Il2CppHook::attach(image_name, "UnityEngine", "Debug", method_name, {"System.Object"}, original, detour,
                              [](const char *text) { ModLog::warn("%s", text ? text : ""); });
}

inline void detach(DebugLogFn *original, DebugLogFn detour) {
    if (*original)
        URK::hooks::detach_ex(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour));
    *original = nullptr;
}

inline bool try_install_for_image(const char *image_name) {
    for (std::size_t index = 0; index < 3; ++index) {
        if (attach(image_name, k_method_names[index], &g_originals[index], k_detours[index]))
            continue;
        g_originals[index] = nullptr;
        while (index > 0) {
            --index;
            detach(&g_originals[index], k_detours[index]);
        }
        return false;
    }
    return true;
}

inline bool install(const URK_ModContext *ctx) {
    URK::set_context(ctx);
    if (!ctx || !ModConfig::enable_unity_log_hook)
        return true;
    if (g_installed)
        return true;
    if (!URK::il2cpp::init(ctx) || !URK::hooks::available()) {
        ModLog::warn("IL2CPP Unity log hooks are unavailable");
        return false;
    }

    g_installed = try_install_for_image("UnityEngine.CoreModule.dll") || try_install_for_image("UnityEngine.dll");
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
)URK";
    } else {
        out << R"URK(using DebugLogFn = void (*)(void *message);

inline DebugLogFn g_originals[3]{};
inline bool g_installed = false;

enum class LogLevel {
    info,
    warning,
    error
};

inline std::string message_text(void *message) {
    if (!message)
        return "<null>";

    auto *object = static_cast<URK::mono::Object *>(message);
    const auto *klass = URK::mono::object_get_class(object);
    const char *namespc = klass ? URK::mono::class_get_namespace(klass) : nullptr;
    const char *name = klass ? URK::mono::class_get_name(klass) : nullptr;
    if (namespc && name && std::strcmp(namespc, "System") == 0 && std::strcmp(name, "String") == 0) {
        return URK::mono::helpers::to_utf8(static_cast<URK::mono::String *>(message), "<unreadable Unity log message>");
    }

    char fallback[192]{};
    std::snprintf(fallback, sizeof(fallback), "<%s%s%s object at %p>", namespc && namespc[0] ? namespc : "",
                  namespc && namespc[0] ? "." : "", name && name[0] ? name : "unknown", message);
    return fallback;
}

inline void write(LogLevel level, void *message) {
    const std::string text = message_text(message);
    switch (level) {
        case LogLevel::warning:
            ModLog::warn("[Unity] %s", text.c_str());
            break;
        case LogLevel::error:
            ModLog::error("[Unity] %s", text.c_str());
            break;
        default:
            ModLog::info("[Unity] %s", text.c_str());
            break;
    }
}

inline void detour_log(void *message) {
    write(LogLevel::info, message);
    if (g_originals[0])
        g_originals[0](message);
}

inline void detour_warning(void *message) {
    write(LogLevel::warning, message);
    if (g_originals[1])
        g_originals[1](message);
}

inline void detour_error(void *message) {
    write(LogLevel::error, message);
    if (g_originals[2])
        g_originals[2](message);
}

inline constexpr const char *k_method_names[] = {"Log", "LogWarning", "LogError"};
inline DebugLogFn k_detours[] = {&detour_log, &detour_warning, &detour_error};

inline bool attach(const char *image_name, const char *method_name, DebugLogFn *original, DebugLogFn detour) {
    const char *parameter_types[] = {"System.Object"};
    const URK::mono::Method *method = nullptr;
    if (!URK::mono::helpers::require_method_exact(image_name, "UnityEngine", "Debug", method_name, parameter_types, 1,
                                                  "System.Void", method,
                                                  [](const char *text) { ModLog::warn("%s", text ? text : ""); })) {
        return false;
    }

    void *target = URK::mono::compile_method(method);
    if (!target) {
        ModLog::warn("Mono could not compile UnityEngine.Debug::%s", method_name);
        return false;
    }

    *original = reinterpret_cast<DebugLogFn>(target);
    if (!URK::hooks::attach_ex(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour),
                               URK::hook_backend_auto)) {
        *original = nullptr;
        ModLog::warn("Mono UnityEngine.Debug::%s hook attach failed", method_name);
        return false;
    }
    return true;
}

inline void detach(DebugLogFn *original, DebugLogFn detour) {
    if (*original)
        URK::hooks::detach_ex(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour));
    *original = nullptr;
}

inline bool try_install_for_image(const char *image_name) {
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

inline bool install(const URK_ModContext *ctx) {
    URK::set_context(ctx);
    if (!ctx || !ModConfig::enable_unity_log_hook)
        return true;
    if (g_installed)
        return true;
    if (!URK::mono::init(ctx) || !URK::hooks::available()) {
        ModLog::warn("Mono Unity log hooks are unavailable");
        return false;
    }

    g_installed = try_install_for_image("UnityEngine.CoreModule.dll") || try_install_for_image("UnityEngine.dll");
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
)URK";
    }

    out << "} // namespace ModUnityLogHook\n";
    return out.str();
}

