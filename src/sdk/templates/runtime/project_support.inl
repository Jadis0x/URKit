std::string ConfigModule(const ModuleProjectOptions &options) {
    std::ostringstream out;
    out << "#pragma once\n\n"
        << "namespace ModConfig {\n"
        << "inline constexpr const char* project_name = \"" << EscapeString(options.projectName) << "\";\n"
        << "// Stable namespace for this mod's deployed resources. Do not change it after release.\n"
        << "inline constexpr const char* mod_id = \"" << EscapeString(options.modId) << "\";\n"
        << "inline constexpr const char* display_name = \"" << EscapeString(options.projectName) << "\";\n"
        << "inline constexpr const char* author = \"\";\n"
        << "inline constexpr const char* version = \"0.1.0\";\n"
        << "inline constexpr const char* url = \"\";\n"
        << "inline constexpr const char* social = \"\";\n"
        << "inline constexpr const char* description = \"" << EscapeString(options.description) << "\";\n"
        << "inline constexpr bool is_il2cpp_backend = "
        << (options.backendNamespace == "URK::il2cpp" ? "true" : "false") << ";\n";
    for (const std::string &line : options.configExtraLines)
        out << line << "\n";
    out << "inline bool show_menu = true;\n"
        << "// English is used as the fixed language when localization support is not generated.\n"
        << "inline bool enable_localization = " << (options.enableLocalization ? "true" : "false") << ";\n"
        << "inline constexpr const char* default_language = \"en\";\n"
        << "inline bool enable_unity_log_hook = true;\n"
        << "// Win32 virtual-key code used by the generated ImGui WndProc toggle.\n"
        << "// Default: VK_TAB (0x09). Change this value to customize the menu key.\n"
        << "inline int menu_toggle_key = 0x09;\n";
    out << "} // namespace ModConfig\n";
    return out.str();
}

std::string BackendRuntimeHeader(const ModuleProjectOptions &options) {
    return options.backendNamespace == "URK::mono" ? "sdk/mono/mono_runtime.h" : "sdk/il2cpp/il2cpp_runtime.h";
}

std::string BackendHelperHeader(const ModuleProjectOptions &options) {
    return options.backendNamespace == "URK::mono" ? "sdk/mono/mono_helpers.h" : "sdk/il2cpp/il2cpp_helpers.h";
}

std::string BackendApiField(const ModuleProjectOptions &options) {
    return options.backendNamespace == "URK::mono" ? "mono" : "il2cpp";
}

std::string RuntimeBootstrapModule(const ModuleProjectOptions &options) {
    std::ostringstream out;
    out << "#pragma once\n\n"
        << "#include \"runtime_api.h\"\n"
        << "#include \"" << BackendRuntimeHeader(options) << "\"\n\n"
        << "namespace URK {\n"
        << "inline bool initialize_backend(const ModContext* context) {\n"
        << "  return " << options.backendNamespace << "::init(context);\n"
        << "}\n"
        << "} // namespace URK\n";
    return out.str();
}

std::string ModLogHeader() {
    return R"URK(#pragma once

#include "sdk/mod_sdk.h"

namespace ModLog {
void initialize(const URK_ModContext *ctx);
const URK_ModContext *context();
void info(const char *fmt, ...);
void warn(const char *fmt, ...);
void error(const char *fmt, ...);
void success(const char *fmt, ...);
void shutdown();
} // namespace ModLog
)URK";
}

std::string ModLogSource() {
    return R"URK(#include "mod_log.h"

#include "config/mod_config.h"

#include <cstdarg>
#include <cstdio>

namespace {
const URK_ModContext *g_ctx = nullptr;

void write_log(const char *level, const char *fmt, va_list args) {
    if (!g_ctx || !g_ctx->Log)
        return;

    char message[1600]{};
    if (fmt && fmt[0])
        std::vsnprintf(message, sizeof(message), fmt, args);

    if (level && level[0])
        g_ctx->Log("[%s][%s] %s", ModConfig::display_name, level, message);
    else
        g_ctx->Log("[%s] %s", ModConfig::display_name, message);
}
} // namespace

namespace ModLog {
void initialize(const URK_ModContext *ctx) {
    g_ctx = ctx;
}
const URK_ModContext *context() {
    return g_ctx;
}

void info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write_log("info", fmt, args);
    va_end(args);
}

void warn(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write_log("warn", fmt, args);
    va_end(args);
}

void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write_log("error", fmt, args);
    va_end(args);
}

void success(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    write_log("success", fmt, args);
    va_end(args);
}

void shutdown() {
    g_ctx = nullptr;
}
} // namespace ModLog
)URK";
}

std::string ModHooksHeader() {
    return R"URK(#pragma once

#include "sdk/mod_sdk.h"

namespace ModHooks {
bool install(const URK_ModContext *ctx);
void uninstall();
} // namespace ModHooks
)URK";
}

std::string ModHooksSource(const ModuleProjectOptions &options) {
    std::ostringstream out;
    out << "#include \"mod_hooks.h\"\n\n"
        << "#include \"config/mod_config.h\"\n"
        << "#include \"support/mod_log.h\"\n\n"
        << "#include \"sdk/runtime_api.h\"\n"
        << "#include \"sdk/hook_api.h\"\n"
        << "#include \"render_imgui_hook.h\"\n"
        << "#include \"unity_log_hook.h\"\n"
        << "#include \"" << BackendRuntimeHeader(options) << "\"\n"
        << "#include \"" << BackendHelperHeader(options) << "\"\n";
    out << R"URK(
namespace {
URK::hooks::HookSet g_hooks;
} // namespace

namespace ModHooks {
bool install(const URK_ModContext *ctx) {
    URK::set_context(ctx);
)URK"
        << "  " << options.backendNamespace << "::init(ctx);\n"
        << R"URK(
if (!URK::hooks::available()) {
    ModLog::warn("hook API is unavailable; no hooks were installed");
    return true;
}

// Register validated targets with g_hooks so uninstall() can detach them.
if (ModConfig::enable_unity_log_hook && !ModUnityLogHook::install(ctx))
    ModLog::warn("Unity log hooks were requested but not installed");
if (!ModRenderHook::install(ctx))
    ModLog::warn("ImGui render hook could not be installed; continuing without menu");

ModLog::info("hook registry ready; ImGui render hook initialization requested");
return true;
}

void uninstall() {
    ModUnityLogHook::uninstall();
    if (!ModRenderHook::uninstall())
        ModLog::error("ImGui render hook shutdown was incomplete; see hook diagnostics");
    g_hooks.detach_all();
}
} // namespace ModHooks
)URK";
    return out.str();
}

std::string NetworkInitHeader() {
    return R"URK(#pragma once

#include "sdk/mod_sdk.h"

namespace ModNetwork {
bool init(const URK_ModContext *ctx);
void shutdown();
} // namespace ModNetwork
)URK";
}

std::string NetworkInitSource() {
    return R"URK(#include "mod_network.h"

#include "sdk/runtime_api.h"

namespace ModNetwork {
bool init(const URK_ModContext *ctx) {
    URK::set_context(ctx);
    return true;
}

void shutdown() {
}
} // namespace ModNetwork
)URK";
}

std::string GameRuntimeHeader() {
    return R"URK(#pragma once

#include "sdk/mod_async.h"
#include "sdk/mod_sdk.h"

namespace ModRuntime {
bool start(const URK_ModContext *ctx);
void update();
void on_scene_loaded(const URK_SceneInfo *scene);
void on_scene_changed(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene);
void on_object_destroy_requested(const URK_ObjectDestroyRequest *request);
void stop();
} // namespace ModRuntime
)URK";
}

std::string GameRuntimeSource(const ModuleProjectOptions &options) {
    std::ostringstream out;
    out << "#include \"mod_runtime.h\"\n\n"
        << "#include \"support/mod_log.h\"\n\n"
        << "#include \"sdk/runtime_api.h\"\n"
        << "#include \"sdk/runtime_bootstrap.h\"\n"
        << "#include \"sdk/unity/unity.h\"\n\n"
        << "namespace ModRuntime {\n"
        << "bool start(const URK_ModContext* ctx) {\n"
        << "  URK::set_context(ctx);\n"
        << "  if (!URK::initialize_backend(ctx)) {\n"
        << "    ModLog::error(\"" << EscapeString(options.backendDisplayName)
        << " runtime API initialization failed\");\n"
        << "    return false;\n"
        << "  }\n\n"
        << "  ModLog::info(\"runtime ready: backend=" << EscapeString(options.backendDisplayName)
        << " main_thread=%s scene_events=%s\", "
           "URK::has_main_thread() ? \"yes\" : \"no\", "
           "URK::has_scene_events() ? \"yes\" : \"no\");\n"
        << "  return true;\n"
        << "}\n\n"
        << "void update() {\n"
        << "  // Put Unity work that must run on the main thread here.\n"
        << "}\n\n"
        << "void on_scene_loaded(const URK_SceneInfo* scene) {\n"
        << "  if (!scene || scene->size < sizeof(URK_SceneInfo)) return;\n"
        << "  ModLog::info(\"scene loaded: name=%s buildIndex=%d handle=%d\", "
           "scene->name, scene->buildIndex, scene->handle);\n"
        << "}\n\n"
        << "void on_scene_changed(const URK_SceneInfo* previousScene, "
           "const URK_SceneInfo* currentScene) {\n"
        << "  if (!previousScene || !currentScene || "
           "previousScene->size < sizeof(URK_SceneInfo) || "
           "currentScene->size < sizeof(URK_SceneInfo)) return;\n"
        << "  ModLog::info(\"scene changed: %s -> %s\", previousScene->name, "
           "currentScene->name);\n"
        << "}\n\n";
    out << "void on_object_destroy_requested(const URK_ObjectDestroyRequest* request) {\n"
        << "  if (!request || request->size < sizeof(URK_ObjectDestroyRequest)) return;\n"
        << "  ModLog::info(\"object destroy requested: name=%s type=%s instanceId=%d delay=%.3f immediate=%s\",\n"
        << "               request->name, request->typeName, request->instanceId, request->delaySeconds,\n"
        << "               (request->flags & URK_OBJECT_DESTROY_REQUEST_IMMEDIATE) ? \"yes\" : \"no\");\n"
        << "}\n\n";
    out << "void stop() {\n"
        << "  ModLog::info(\"runtime stopped\");\n"
        << "}\n"
        << "} // namespace ModRuntime\n";
    return out.str();
}


