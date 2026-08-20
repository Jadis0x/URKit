// Internal runtime and lifecycle templates. Included by mod_project_generator_common.cpp.

std::string ModSdkModule() {
    return R"URKCOMMONSDK(#pragma once

#include "sdk/mod_sdk.h"
#include <cstddef>
#include <cstdint>

namespace URK {
using GetModInfoFn = ::URK_GetModInfoFn;
using HookBackend = ::URK_HookBackend;
using HookOptions = ::URK_HookOptions;
using Il2CppApi = ::URK_Il2CppApi;
using ModContext = ::URK_ModContext;
using ModInfo = ::URK_ModInfo;
using MonoApi = ::URK_MonoApi;
using NetworkApi = ::URK_NetworkApi;
using NetworkHeader = ::URK_NetworkHeader;
using NetworkHttpMethod = ::URK_NetworkHttpMethod;
using NetworkRequest = ::URK_NetworkRequest;
using NetworkResponse = ::URK_NetworkResponse;
using NetworkResultFlags = ::URK_NetworkResultFlags;
using RuntimeApi = ::URK_RuntimeApi;
using WindowMessageCallback = ::URK_WindowMessageCallback;
using RuntimeBackend = ::URK_RuntimeBackend;
using RuntimeCapabilityFlags = ::URK_RuntimeCapabilityFlags;
using RuntimeModuleKind = ::URK_RuntimeModuleKind;
using SceneInfo = ::URK_SceneInfo;
using CursorLockState = ::URK_CursorLockState;
using CursorState = ::URK_CursorState;
using OnSceneLoadedFn = ::URK_OnSceneLoadedFn;
using OnSceneChangedFn = ::URK_OnSceneChangedFn;
using ObjectDestroyRequest = ::URK_ObjectDestroyRequest;
using ObjectDestroyRequestFlags = ::URK_ObjectDestroyRequestFlags;
using OnObjectDestroyRequestedFn = ::URK_OnObjectDestroyRequestedFn;

inline constexpr RuntimeBackend runtime_backend_unknown = URK_RUNTIME_BACKEND_UNKNOWN;
inline constexpr RuntimeBackend runtime_backend_mono = URK_RUNTIME_BACKEND_MONO;
inline constexpr RuntimeBackend runtime_backend_il2cpp = URK_RUNTIME_BACKEND_IL2CPP;

inline constexpr HookBackend hook_backend_auto = URK_HOOK_BACKEND_AUTO;
inline constexpr HookBackend hook_backend_detours = URK_HOOK_BACKEND_DETOURS;

inline constexpr std::uint64_t runtime_cap_none = URK_RUNTIME_CAP_NONE;
inline constexpr std::uint64_t runtime_cap_mono_api = URK_RUNTIME_CAP_MONO_API;
inline constexpr std::uint64_t runtime_cap_il2cpp_api = URK_RUNTIME_CAP_IL2CPP_API;
inline constexpr std::uint64_t runtime_cap_hooks = URK_RUNTIME_CAP_HOOKS;
inline constexpr std::uint64_t runtime_cap_main_thread = URK_RUNTIME_CAP_MAIN_THREAD;
inline constexpr std::uint64_t runtime_cap_scene_events = URK_RUNTIME_CAP_SCENE_EVENTS;
inline constexpr std::uint64_t runtime_cap_cursor_control = URK_RUNTIME_CAP_CURSOR_CONTROL;
inline constexpr std::uint64_t runtime_cap_network = URK_RUNTIME_CAP_NETWORK;
inline constexpr std::uint64_t runtime_cap_input = URK_RUNTIME_CAP_INPUT;
inline constexpr std::uint64_t runtime_cap_graphics_device = URK_RUNTIME_CAP_GRAPHICS_DEVICE_TYPE;
inline constexpr std::uint64_t runtime_cap_object_destroy_request_events =
    URK_RUNTIME_CAP_OBJECT_DESTROY_REQUEST_EVENTS;
inline constexpr std::uint64_t runtime_cap_steam_identity = URK_RUNTIME_CAP_STEAM_IDENTITY;
inline constexpr std::int32_t graphics_device_unknown = URK_GRAPHICS_DEVICE_UNKNOWN;
inline constexpr std::int32_t graphics_device_direct3d11 = URK_GRAPHICS_DEVICE_D3D11;
inline constexpr std::int32_t graphics_device_direct3d12 = URK_GRAPHICS_DEVICE_D3D12;
inline constexpr std::int32_t graphics_device_opengl2 = URK_GRAPHICS_DEVICE_OPENGL2;
inline constexpr std::int32_t graphics_device_openglcore = URK_GRAPHICS_DEVICE_OPENGL_CORE;
inline constexpr NetworkHttpMethod network_http_get = URK_NETWORK_HTTP_GET;
inline constexpr NetworkHttpMethod network_http_post = URK_NETWORK_HTTP_POST;
inline constexpr NetworkHttpMethod network_http_put = URK_NETWORK_HTTP_PUT;
inline constexpr NetworkHttpMethod network_http_update = URK_NETWORK_HTTP_UPDATE;
inline constexpr NetworkHttpMethod network_http_patch = URK_NETWORK_HTTP_PATCH;
inline constexpr NetworkHttpMethod network_http_delete = URK_NETWORK_HTTP_DELETE;
inline constexpr std::uint32_t network_result_none = URK_NETWORK_RESULT_NONE;
inline constexpr std::uint32_t network_result_body_truncated = URK_NETWORK_RESULT_BODY_TRUNCATED;
inline constexpr std::uint32_t network_result_error_truncated = URK_NETWORK_RESULT_ERROR_TRUNCATED;
inline constexpr CursorLockState cursor_lock_none = URK_CURSOR_LOCK_NONE;
inline constexpr CursorLockState cursor_lock_locked = URK_CURSOR_LOCK_LOCKED;
inline constexpr CursorLockState cursor_lock_confined = URK_CURSOR_LOCK_CONFINED;
inline const ModContext *&ContextSlot() {
    static const ModContext *ctx = nullptr;
    return ctx;
}

inline void set_context(const ModContext *ctx) {
    ContextSlot() = ctx;
}
inline const ModContext *context() {
    return ContextSlot();
}

inline bool context_has_field(std::size_t fieldEnd) {
    const auto *ctx = context();
    return ctx && ctx->size >= fieldEnd;
}

inline std::uint64_t runtime_capabilities() {
    const auto *ctx = context();
    if (!ctx || ctx->version < URK_SDK_VERSION ||
        !context_has_field(offsetof(ModContext, runtimeCapabilities) + sizeof(ctx->runtimeCapabilities)))
        return runtime_cap_none;
    const std::uint64_t contextCapabilities = ctx->runtimeCapabilities;
    if (context_has_field(offsetof(ModContext, runtime) + sizeof(ctx->runtime)) && ctx->runtime &&
        ctx->runtime->version >= 1 &&
        ctx->runtime->size >= offsetof(RuntimeApi, capabilities) + sizeof(ctx->runtime->capabilities) &&
        ctx->runtime->capabilities) {
        return contextCapabilities & ctx->runtime->capabilities();
    }
    return contextCapabilities;
}

inline bool has_runtime_capability(std::uint64_t capability) {
    return (runtime_capabilities() & capability) != 0;
}
inline bool has_mono_api() {
    return has_runtime_capability(runtime_cap_mono_api);
}
inline bool has_il2cpp_api() {
    return has_runtime_capability(runtime_cap_il2cpp_api);
}
inline bool has_hooks() {
    return has_runtime_capability(runtime_cap_hooks);
}
inline bool has_main_thread() {
    return has_runtime_capability(runtime_cap_main_thread);
}
inline bool has_scene_events() {
    return has_runtime_capability(runtime_cap_scene_events);
}
inline bool has_object_destroy_request_events() {
    return has_runtime_capability(runtime_cap_object_destroy_request_events);
}
inline bool has_cursor_control() {
    return has_runtime_capability(runtime_cap_cursor_control);
}
inline bool has_network() {
    return has_runtime_capability(runtime_cap_network);
}
inline bool has_input() {
    return has_runtime_capability(runtime_cap_input);
}
inline bool has_graphics_device() {
    return has_runtime_capability(runtime_cap_graphics_device);
}
inline bool has_steam_identity() {
    return has_runtime_capability(runtime_cap_steam_identity);
}

inline bool runtime_api_has_field(std::size_t fieldEnd) {
    const auto *ctx = context();
    return ctx && ctx->runtime && ctx->runtime->size >= fieldEnd;
}
inline bool is_main_thread() {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, is_main_thread) + sizeof(ctx->runtime->is_main_thread)) &&
           ctx->runtime->is_main_thread && ctx->runtime->is_main_thread() != 0;
}
inline std::int32_t graphics_device_type() {
    const auto *ctx = context();
    return has_graphics_device() &&
                   runtime_api_has_field(offsetof(RuntimeApi, graphics_device_type) +
                                         sizeof(ctx->runtime->graphics_device_type)) &&
                   ctx->runtime->graphics_device_type
               ? ctx->runtime->graphics_device_type()
               : graphics_device_unknown;
}
inline bool steam_id64(char *output, std::size_t output_size) {
    if (!output || output_size == 0)
        return false;
    output[0] = '\0';
    const auto *ctx = context();
    return has_steam_identity() &&
           runtime_api_has_field(offsetof(RuntimeApi, steam_id64) + sizeof(ctx->runtime->steam_id64)) &&
           ctx->runtime->steam_id64 && ctx->runtime->steam_id64(output, output_size) != 0;
}
inline bool window_message_dispatch_available() {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, window_message_call_original) +
                                 sizeof(ctx->runtime->window_message_call_original)) &&
           ctx->runtime->window_message_register && ctx->runtime->window_message_unregister &&
           ctx->runtime->window_message_call_original;
}
inline bool window_message_register(void *window, WindowMessageCallback callback) {
    const auto *ctx = context();
    return window_message_dispatch_available() && window && callback &&
           ctx->runtime->window_message_register(window, callback) != 0;
}
inline bool window_message_unregister(void *window, WindowMessageCallback callback) {
    const auto *ctx = context();
    return window_message_dispatch_available() && window && callback &&
           ctx->runtime->window_message_unregister(window, callback) != 0;
}
inline std::intptr_t window_message_call_original(void *window, std::uint32_t message, std::uintptr_t wparam,
                                                  std::intptr_t lparam) {
    const auto *ctx = context();
    return window_message_dispatch_available()
               ? ctx->runtime->window_message_call_original(window, message, wparam, lparam)
               : 0;
}
inline bool current_scene(SceneInfo *scene) {
    if (!scene)
        return false;
    scene->size = sizeof(SceneInfo);
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, scene_current) + sizeof(ctx->runtime->scene_current)) &&
           ctx->runtime->scene_current && ctx->runtime->scene_current(scene) != 0;
}
inline bool set_menu_cursor_open(bool open) {
    const auto *ctx = context();
    static const unsigned char owner_token = 0;
    if (runtime_api_has_field(offsetof(RuntimeApi, menu_cursor_set_open_owned) +
                              sizeof(ctx->runtime->menu_cursor_set_open_owned)) &&
        ctx->runtime->menu_cursor_set_open_owned)
        return ctx->runtime->menu_cursor_set_open_owned(&owner_token, open ? 1 : 0) != 0;
    return runtime_api_has_field(offsetof(RuntimeApi, menu_cursor_set_open) +
                                 sizeof(ctx->runtime->menu_cursor_set_open)) &&
           ctx->runtime->menu_cursor_set_open && ctx->runtime->menu_cursor_set_open(open ? 1 : 0) != 0;
}
inline bool set_menu_mouse_capture(bool capture) {
    const auto *ctx = context();
    static const unsigned char owner_token = 0;
    return runtime_api_has_field(offsetof(RuntimeApi, menu_mouse_capture_set_owned) +
                                 sizeof(ctx->runtime->menu_mouse_capture_set_owned)) &&
           ctx->runtime->menu_mouse_capture_set_owned &&
           ctx->runtime->menu_mouse_capture_set_owned(&owner_token, capture ? 1 : 0) != 0;
}
inline bool cursor_state_get(CursorState *state) {
    if (!state)
        return false;
    state->size = sizeof(CursorState);
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, cursor_state_get) + sizeof(ctx->runtime->cursor_state_get)) &&
           ctx->runtime->cursor_state_get && ctx->runtime->cursor_state_get(state) != 0;
}
inline bool cursor_state_set(bool visible, CursorLockState lockState) {
    CursorState state{};
    state.size = sizeof(state);
    state.visible = visible ? 1 : 0;
    state.lockState = static_cast<std::int32_t>(lockState);
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, cursor_state_set) + sizeof(ctx->runtime->cursor_state_set)) &&
           ctx->runtime->cursor_state_set && ctx->runtime->cursor_state_set(&state) != 0;
}
inline bool cursor_state_set(const CursorState *state) {
    if (!state || state->size < sizeof(CursorState))
        return false;
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, cursor_state_set) + sizeof(ctx->runtime->cursor_state_set)) &&
           ctx->runtime->cursor_state_set && ctx->runtime->cursor_state_set(state) != 0;
}
inline bool input_get_key(std::int32_t keyCode) {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, input_get_key) + sizeof(ctx->runtime->input_get_key)) &&
           ctx->runtime->input_get_key && ctx->runtime->input_get_key(keyCode) != 0;
}
inline bool input_get_key_down(std::int32_t keyCode) {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, input_get_key_down) + sizeof(ctx->runtime->input_get_key_down)) &&
           ctx->runtime->input_get_key_down && ctx->runtime->input_get_key_down(keyCode) != 0;
}
inline bool input_get_key_up(std::int32_t keyCode) {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, input_get_key_up) + sizeof(ctx->runtime->input_get_key_up)) &&
           ctx->runtime->input_get_key_up && ctx->runtime->input_get_key_up(keyCode) != 0;
}
inline bool input_get_mouse_button(std::int32_t button) {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, input_get_mouse_button) +
                                 sizeof(ctx->runtime->input_get_mouse_button)) &&
           ctx->runtime->input_get_mouse_button && ctx->runtime->input_get_mouse_button(button) != 0;
}
inline bool input_get_mouse_button_down(std::int32_t button) {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, input_get_mouse_button_down) +
                                 sizeof(ctx->runtime->input_get_mouse_button_down)) &&
           ctx->runtime->input_get_mouse_button_down && ctx->runtime->input_get_mouse_button_down(button) != 0;
}
inline bool input_get_mouse_button_up(std::int32_t button) {
    const auto *ctx = context();
    return runtime_api_has_field(offsetof(RuntimeApi, input_get_mouse_button_up) +
                                 sizeof(ctx->runtime->input_get_mouse_button_up)) &&
           ctx->runtime->input_get_mouse_button_up && ctx->runtime->input_get_mouse_button_up(button) != 0;
}

inline void log(const char *text) {
    const auto *ctx = context();
    if (ctx && ctx->Log)
        ctx->Log("%s", text ? text : "");
}
} // namespace URK
)URKCOMMONSDK";
}

std::string HooksRuntimeModule() {
    return R"URKCOMMONHOOKS(#pragma once

#include "runtime_api.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace URK::hooks {
inline bool available() {
    const URK::ModContext *ctx = URK::context();
    return URK::has_runtime_capability(URK::runtime_cap_hooks) && ctx && (ctx->HookAttach || ctx->HookAttachEx);
}
inline bool backend_available(URK_HookBackend backend) {
    const URK::ModContext *ctx = URK::context();
    if (!available())
        return false;
    if (backend == URK::hook_backend_auto)
        return true;
    return ctx->HookBackendAvailable ? ctx->HookBackendAvailable(static_cast<std::uint32_t>(backend)) != 0 : false;
}
inline bool attach_ex(void **original, void *detour, const URK_HookOptions *options) {
    const URK::ModContext *ctx = URK::context();
    if (!original || !detour || !available())
        return false;
    const bool has_options = options != nullptr;
    if (has_options && options->size < sizeof(URK_HookOptions))
        return false;
    const auto backend = has_options ? static_cast<URK_HookBackend>(options->backend) : URK::hook_backend_auto;
    if (!backend_available(backend))
        return false;
    if (ctx->HookAttachEx)
        return ctx->HookAttachEx(original, detour, options) != 0;
    return backend == URK::hook_backend_auto && ctx->HookAttach ? ctx->HookAttach(original, detour) != 0 : false;
}
inline bool attach_ex(void **original, void *detour, URK_HookBackend backend) {
    URK_HookOptions options{};
    options.size = sizeof(options);
    options.backend = static_cast<std::uint32_t>(backend);
    return attach_ex(original, detour, &options);
}
inline bool attach(void **original, void *detour) {
    return attach_ex(original, detour, static_cast<const URK_HookOptions *>(nullptr));
}
inline bool detach_ex(void **original, void *detour) {
    const URK::ModContext *ctx = URK::context();
    if (!original || !detour || !URK::has_runtime_capability(URK::runtime_cap_hooks) || !ctx)
        return false;
    if (ctx->HookDetachEx)
        return ctx->HookDetachEx(original, detour) != 0;
    return ctx->HookDetach ? ctx->HookDetach(original, detour) != 0 : false;
}
inline bool detach(void **original, void *detour) {
    return detach_ex(original, detour);
}
template <class T> inline T as(void *value) {
    return reinterpret_cast<T>(value);
}

class HookSet {
  public:
    static constexpr std::size_t max_entries = 64;
    template <class T> bool add(T *original, T detour) {
        return add_raw(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour));
    }
    template <class T> bool add(T *original, T detour, URK_HookBackend backend) {
        return add_raw(reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour), backend);
    }
    bool add_raw(void **original, void *detour) {
        if (full() || !original || !detour || !URK::hooks::attach(original, detour))
            return false;
        entries_[count_++] = {original, detour};
        return true;
    }
    bool add_raw(void **original, void *detour, const URK_HookOptions *options) {
        if (full() || !original || !detour || !URK::hooks::attach_ex(original, detour, options))
            return false;
        entries_[count_++] = {original, detour};
        return true;
    }
    bool add_raw(void **original, void *detour, URK_HookBackend backend) {
        if (full() || !original || !detour || !URK::hooks::attach_ex(original, detour, backend))
            return false;
        entries_[count_++] = {original, detour};
        return true;
    }
    void detach_all() {
        while (count_ > 0) {
            auto entry = entries_[--count_];
            detach(entry.original, entry.detour);
        }
    }
    std::size_t size() const {
        return count_;
    }
    constexpr std::size_t capacity() const {
        return max_entries;
    }
    bool full() const {
        return count_ >= max_entries;
    }

  private:
    struct Entry {
        void **original;
        void *detour;
    };
    Entry entries_[max_entries]{};
    std::size_t count_ = 0;
};
} // namespace URK::hooks
)URKCOMMONHOOKS";
}

std::string NetworkRuntimeModule() {
    return R"URKCOMMONNETWORK(#pragma once

#include "runtime_api.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace URK::network {
struct Header {
    std::string name;
    std::string value;
};

struct Response {
    bool completed = false;
    std::int32_t status = 0;
    std::string body;
    std::string error;
    bool body_truncated = false;
    bool error_truncated = false;

    bool ok() const {
        return completed && status >= 200 && status < 300;
    }
};

inline bool available() {
    const URK::ModContext *ctx = URK::context();
    return ctx && URK::has_network() && ctx->size >= offsetof(URK::ModContext, network) + sizeof(ctx->network) &&
           ctx->network &&
           ctx->network->size >= offsetof(URK::NetworkApi, json_request) + sizeof(ctx->network->json_request) &&
           ctx->network->json_request;
}

inline Response request_json(URK::NetworkHttpMethod method, std::string_view url, std::string_view json_body = {},
                             const std::vector<Header> &headers = {}, std::uint32_t timeout_ms = 5000,
                             std::string_view pinned_public_key = {}, std::size_t response_capacity = 65536) {
    Response response;
    if (!available()) {
        response.error = "URKit network API is unavailable";
        return response;
    }
    if (url.rfind("https://", 0) != 0) {
        response.error = "HTTPS URL is required";
        return response;
    }

    if (response_capacity == 0)
        response_capacity = 1;
    if (response_capacity > 1024 * 1024)
        response_capacity = 1024 * 1024;

    std::string url_storage(url);
    std::string body_storage(json_body);
    std::string pin_storage(pinned_public_key);
    std::vector<URK::NetworkHeader> raw_headers;
    raw_headers.reserve(headers.size());
    for (const Header &header : headers)
        raw_headers.push_back({header.name.c_str(), header.value.c_str()});

    std::string body_buffer(response_capacity, '\0');
    std::array<char, 1024> error_buffer{};

    URK::NetworkRequest request{};
    request.size = sizeof(request);
    request.method = static_cast<std::uint32_t>(method);
    request.url = url_storage.c_str();
    request.jsonBody = body_storage.empty() ? nullptr : body_storage.c_str();
    request.headers = raw_headers.empty() ? nullptr : raw_headers.data();
    request.headerCount = raw_headers.size();
    request.timeoutMs = timeout_ms;
    request.pinnedPublicKey = pin_storage.empty() ? nullptr : pin_storage.c_str();

    URK::NetworkResponse raw_response{};
    raw_response.size = sizeof(raw_response);
    raw_response.body = body_buffer.data();
    raw_response.bodyCapacity = body_buffer.size();
    raw_response.error = error_buffer.data();
    raw_response.errorCapacity = error_buffer.size();

    const URK::ModContext *ctx = URK::context();
    response.completed = ctx->network->json_request(&request, &raw_response) != 0;
    response.status = raw_response.statusCode;
    response.body_truncated = (raw_response.flags & URK::network_result_body_truncated) != 0;
    response.error_truncated = (raw_response.flags & URK::network_result_error_truncated) != 0;
    if (raw_response.body && raw_response.bodyCapacity) {
        const std::size_t copied = raw_response.bodyLength < raw_response.bodyCapacity ? raw_response.bodyLength
                                                                                       : raw_response.bodyCapacity - 1;
        response.body.assign(raw_response.body, copied);
    }
    if (raw_response.error)
        response.error = raw_response.error;
    return response;
}

inline Response get_json(std::string_view url, const std::vector<Header> &headers = {}, std::uint32_t timeout_ms = 5000,
                         std::string_view pinned_public_key = {}) {
    return request_json(URK::network_http_get, url, {}, headers, timeout_ms, pinned_public_key);
}

inline Response post_json(std::string_view url, std::string_view body, const std::vector<Header> &headers = {},
                          std::uint32_t timeout_ms = 5000, std::string_view pinned_public_key = {}) {
    return request_json(URK::network_http_post, url, body, headers, timeout_ms, pinned_public_key);
}

inline Response put_json(std::string_view url, std::string_view body, const std::vector<Header> &headers = {},
                         std::uint32_t timeout_ms = 5000, std::string_view pinned_public_key = {}) {
    return request_json(URK::network_http_put, url, body, headers, timeout_ms, pinned_public_key);
}

inline Response update_json(std::string_view url, std::string_view body, const std::vector<Header> &headers = {},
                            std::uint32_t timeout_ms = 5000, std::string_view pinned_public_key = {}) {
    return request_json(URK::network_http_update, url, body, headers, timeout_ms, pinned_public_key);
}

inline Response patch_json(std::string_view url, std::string_view body, const std::vector<Header> &headers = {},
                           std::uint32_t timeout_ms = 5000, std::string_view pinned_public_key = {}) {
    return request_json(URK::network_http_patch, url, body, headers, timeout_ms, pinned_public_key);
}

inline Response delete_json(std::string_view url, const std::vector<Header> &headers = {},
                            std::uint32_t timeout_ms = 5000, std::string_view pinned_public_key = {}) {
    return request_json(URK::network_http_delete, url, {}, headers, timeout_ms, pinned_public_key);
}
} // namespace URK::network
)URKCOMMONNETWORK";
}

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

std::string EventsModule() {
    return R"URK(#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace URK::events {
struct Subscription {
    std::uint64_t id{};
    explicit operator bool() const noexcept {
        return id != 0;
    }
};

template <class... Args> class Signal {
  public:
    using Callback = void (*)(Args..., void *);

    Subscription subscribe(Callback callback, void *user = nullptr) {
        if (!callback)
            return {};
        const Subscription token{next_id_++};
        slots_.push_back({token.id, callback, user, true});
        return token;
    }

    bool unsubscribe(Subscription token) {
        if (!token)
            return false;
        bool removed = false;
        for (Slot &slot : slots_) {
            if (slot.id == token.id && slot.active) {
                slot.active = false;
                removed = true;
            }
        }
        if (emitting_ == 0)
            compact();
        return removed;
    }

    void emit(Args... args) {
        ++emitting_;
        for (Slot &slot : slots_) {
            if (slot.active)
                slot.callback(args..., slot.user);
        }
        --emitting_;
        if (emitting_ == 0)
            compact();
    }

    void clear() {
        for (Slot &slot : slots_)
            slot.active = false;
        if (emitting_ == 0)
            compact();
    }

    bool empty() const {
        return std::none_of(slots_.begin(), slots_.end(), [](const Slot &slot) { return slot.active; });
    }

  private:
    struct Slot {
        std::uint64_t id{};
        Callback callback{};
        void *user{};
        bool active{};
    };

    void compact() {
        slots_.erase(std::remove_if(slots_.begin(), slots_.end(), [](const Slot &slot) { return !slot.active; }),
                     slots_.end());
    }

    std::uint64_t next_id_ = 1;
    std::uint32_t emitting_ = 0;
    std::vector<Slot> slots_;
};

template <class T, class Equal = std::equal_to<T>> class Changed {
  public:
    using SignalType = Signal<const T &, const T &>;

    Changed() = default;
    explicit Changed(T value) : value_(std::move(value)) {
    }

    const T &get() const noexcept {
        return value_;
    }
    SignalType &changed() noexcept {
        return changed_;
    }
    const SignalType &changed() const noexcept {
        return changed_;
    }

    bool assign(T value) {
        if (equal_(value_, value))
            return false;
        T previous = value_;
        value_ = std::move(value);
        changed_.emit(previous, value_);
        return true;
    }

  private:
    T value_{};
    Equal equal_{};
    SignalType changed_{};
};
} // namespace URK::events
)URK";
}

std::string ModAsyncModule() {
    return R"URK(#pragma once

#include "coroutines.h"

namespace ModAsync {
URK::coroutines::FlowState &flow();
void spawn(URK::coroutines::Task task);
void cancel_all();
} // namespace ModAsync
)URK";
}

std::string CoroutinesModule() {
    return R"URK(#pragma once

#include <algorithm>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace URK::coroutines {
class Task;
struct FlowState;

using Clock = std::chrono::steady_clock;
using Predicate = bool (*)(void *);
using ErrorHandler = void (*)(std::exception_ptr, void *);

enum class WaitKind {
    none,
    frame,
    time,
    predicate,
};

struct Promise {
    FlowState *state{};
    WaitKind wait = WaitKind::none;
    std::uint64_t wake_frame{};
    Clock::time_point wake_time{};
    Predicate predicate{};
    void *predicate_user{};
    std::exception_ptr error{};

    Task get_return_object();
    std::suspend_always initial_suspend() noexcept {
        return {};
    }
    std::suspend_always final_suspend() noexcept {
        return {};
    }
    void return_void() noexcept {
    }
    void unhandled_exception() noexcept {
        error = std::current_exception();
    }
};

class Task {
  public:
    using promise_type = Promise;
    using Handle = std::coroutine_handle<promise_type>;

    Task() = default;
    explicit Task(Handle handle) : handle_(handle) {
    }
    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;
    Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {
    }
    Task &operator=(Task &&other) noexcept {
        if (this != &other) {
            reset();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    ~Task() {
        reset();
    }

    explicit operator bool() const noexcept {
        return static_cast<bool>(handle_);
    }

  private:
    friend struct FlowState;
    friend void spawn(FlowState &, Task &&);

    Handle release() noexcept {
        return std::exchange(handle_, {});
    }
    void reset() noexcept {
        if (handle_)
            handle_.destroy();
        handle_ = {};
    }

    Handle handle_{};
};

inline Task Promise::get_return_object() {
    return Task{std::coroutine_handle<Promise>::from_promise(*this)};
}

struct FlowState {
    std::vector<Task::Handle> tasks;
    Clock::time_point now = Clock::now();
    std::uint64_t frame = 0;
    ErrorHandler error_handler{};
    void *error_user{};
};

struct NextFrameAwaiter {
    bool await_ready() const noexcept {
        return false;
    }
    void await_suspend(Task::Handle handle) const noexcept {
        Promise &promise = handle.promise();
        promise.wait = WaitKind::frame;
        promise.wake_frame = promise.state ? promise.state->frame + 1 : 1;
    }
    void await_resume() const noexcept {
    }
};

struct WaitForAwaiter {
    Clock::duration duration{};
    bool await_ready() const noexcept {
        return duration <= Clock::duration::zero();
    }
    void await_suspend(Task::Handle handle) const noexcept {
        Promise &promise = handle.promise();
        promise.wait = WaitKind::time;
        promise.wake_time = promise.state ? promise.state->now + duration : Clock::now() + duration;
    }
    void await_resume() const noexcept {
    }
};

struct WaitUntilAwaiter {
    Predicate predicate{};
    void *user{};
    bool await_ready() const noexcept {
        return predicate && predicate(user);
    }
    void await_suspend(Task::Handle handle) const noexcept {
        Promise &promise = handle.promise();
        promise.wait = WaitKind::predicate;
        promise.predicate = predicate;
        promise.predicate_user = user;
    }
    void await_resume() const noexcept {
    }
};

inline NextFrameAwaiter next_frame() noexcept {
    return {};
}

template <class Rep, class Period> WaitForAwaiter wait_for(std::chrono::duration<Rep, Period> duration) noexcept {
    return {std::chrono::duration_cast<Clock::duration>(duration)};
}

inline WaitUntilAwaiter wait_until(Predicate predicate, void *user = nullptr) noexcept {
    return {predicate, user};
}

inline void set_error_handler(FlowState &state, ErrorHandler handler, void *user = nullptr) noexcept {
    state.error_handler = handler;
    state.error_user = user;
}

inline void spawn(FlowState &state, Task &&task) {
    Task::Handle handle = task.release();
    if (!handle)
        return;
    Promise &promise = handle.promise();
    promise.state = &state;
    promise.wait = WaitKind::none;
    state.tasks.push_back(handle);
}

inline bool ready(const FlowState &state, const Promise &promise) {
    switch (promise.wait) {
        case WaitKind::none:
            return true;
        case WaitKind::frame:
            return state.frame >= promise.wake_frame;
        case WaitKind::time:
            return state.now >= promise.wake_time;
        case WaitKind::predicate:
            return promise.predicate && promise.predicate(promise.predicate_user);
        default:
            return false;
    }
}

inline void cancel_all(FlowState &state) noexcept {
    for (Task::Handle handle : state.tasks) {
        if (handle)
            handle.destroy();
    }
    state.tasks.clear();
}

inline void tick(FlowState &state, Clock::time_point now = Clock::now()) {
    state.now = now;
    ++state.frame;

    const std::size_t count = state.tasks.size();
    for (std::size_t index = 0; index < count && index < state.tasks.size(); ++index) {
        Task::Handle handle = state.tasks[index];
        if (!handle || handle.done() || !ready(state, handle.promise()))
            continue;
        handle.promise().wait = WaitKind::none;
        handle.resume();
        if (handle.promise().error) {
            if (state.error_handler)
                state.error_handler(handle.promise().error, state.error_user);
            else
                std::terminate();
        }
    }

    state.tasks.erase(std::remove_if(state.tasks.begin(), state.tasks.end(),
                                     [](Task::Handle handle) {
                                         if (!handle)
                                             return true;
                                         if (!handle.done() && !handle.promise().error)
                                             return false;
                                         handle.destroy();
                                         return true;
                                     }),
                      state.tasks.end());
}
} // namespace URK::coroutines
)URK";
}

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
