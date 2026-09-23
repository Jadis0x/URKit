// sdk/unreal/unreal_runtime.h: a thin C++ face over URK_UnrealApi.

std::string UnrealRuntimeModule() {
    return R"URKUE(#pragma once

#include "../runtime_api.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace URK::unreal {
using Handle = ::URK_UnrealObject;
using PropertyInfo = ::URK_UnrealPropertyInfo;
inline constexpr Handle null_handle = URK_UNREAL_NULL_OBJECT;

inline const URK_UnrealApi *&ApiSlot() {
    static const URK_UnrealApi *api = nullptr;
    return api;
}

inline bool usable(const URK_UnrealApi *a) {
    return a && a->version >= URK_UNREAL_API_VERSION && a->size >= sizeof(URK_UnrealApi);
}

inline bool init(const URK::ModContext *ctx) {
    URK::set_context(ctx);
    ApiSlot() = nullptr;
    if (!ctx || ctx->version < URK_SDK_VERSION || ctx->size < sizeof(URK_ModContext) ||
        ctx->runtimeBackend != URK::runtime_backend_unreal || !URK::has_unreal_api() || !usable(ctx->unreal))
        return false;
    ApiSlot() = ctx->unreal;
    return true;
}

inline const URK_UnrealApi *api() {
    return URK::has_unreal_api() && usable(ApiSlot()) ? ApiSlot() : nullptr;
}

// False until the loader's bootstrap has found the engine's globals.
inline bool available() {
    const auto *a = api();
    return a && a->is_available() != 0;
}

struct EngineVersion {
    std::int32_t major = 0;
    std::int32_t minor = 0;
    std::int32_t patch = 0;
};

inline EngineVersion engine_version() {
    EngineVersion version;
    if (const auto *a = api())
        a->engine_version(&version.major, &version.minor, &version.patch);
    return version;
}

class Object {
  public:
    Object() = default;
    explicit Object(Handle handle) : handle_(handle) {}

    Handle handle() const { return handle_; }
    bool valid() const { return handle_ != null_handle && api(); }
    explicit operator bool() const { return valid(); }
    bool operator==(const Object &other) const { return handle_ == other.handle_; }

    std::string name() const {
        char buffer[256]{};
        return valid() && api()->name_of(handle_, buffer, sizeof(buffer)) ? std::string(buffer) : std::string();
    }
    Object klass() const { return valid() ? Object(api()->class_of(handle_)) : Object(); }
    Object outer() const { return valid() ? Object(api()->outer_of(handle_)) : Object(); }
    bool is_a(Object klass) const { return valid() && api()->is_a(handle_, klass.handle_) != 0; }
    // For a class or struct object.
    bool is_child_of(Object base) const { return valid() && api()->is_child_of(handle_, base.handle_) != 0; }
    Object default_object() const { return valid() ? Object(api()->default_object_of(handle_)) : Object(); }
    // On a class or an instance (its class); searches the supers too.
    Object function(const char *name) const {
        return valid() && name ? Object(api()->find_function(handle_, name)) : Object();
    }

    std::optional<PropertyInfo> describe(const char *member) const {
        PropertyInfo info{};
        info.size = sizeof(info);
        if (!valid() || !member || !api()->describe_property(handle_, member, &info))
            return std::nullopt;
        return info;
    }

    std::optional<std::int64_t> get_int(const char *member, std::int32_t index = 0) const {
        std::int64_t value = 0;
        if (!valid() || !member || !api()->read_integer(handle_, member, index, &value))
            return std::nullopt;
        return value;
    }
    std::optional<double> get_float(const char *member, std::int32_t index = 0) const {
        double value = 0;
        if (!valid() || !member || !api()->read_floating(handle_, member, index, &value))
            return std::nullopt;
        return value;
    }
    std::optional<bool> get_bool(const char *member, std::int32_t index = 0) const {
        int value = 0;
        if (!valid() || !member || !api()->read_bool(handle_, member, index, &value))
            return std::nullopt;
        return value != 0;
    }
    Object get_object(const char *member, std::int32_t index = 0) const {
        return valid() && member ? Object(api()->read_object(handle_, member, index)) : Object();
    }
    std::optional<std::string> get_name(const char *member, std::int32_t index = 0) const {
        char buffer[256]{};
        if (!valid() || !member || !api()->read_name(handle_, member, index, buffer, sizeof(buffer)))
            return std::nullopt;
        return std::string(buffer);
    }
    std::optional<std::string> get_string(const char *member, std::int32_t index = 0) const {
        char buffer[4096]{};
        if (!valid() || !member || !api()->read_string(handle_, member, index, buffer, sizeof(buffer)))
            return std::nullopt;
        return std::string(buffer);
    }

    bool set_int(const char *member, std::int64_t value, std::int32_t index = 0) const {
        return valid() && member && api()->write_integer(handle_, member, index, value) != 0;
    }
    bool set_float(const char *member, double value, std::int32_t index = 0) const {
        return valid() && member && api()->write_floating(handle_, member, index, value) != 0;
    }
    bool set_bool(const char *member, bool value, std::int32_t index = 0) const {
        return valid() && member && api()->write_bool(handle_, member, index, value ? 1 : 0) != 0;
    }
    bool set_object(const char *member, Object value, std::int32_t index = 0) const {
        return valid() && member && api()->write_object(handle_, member, index, value.handle_) != 0;
    }

  private:
    Handle handle_ = null_handle;
};

inline Object find(const char *name) {
    const auto *a = api();
    return a && name ? Object(a->find_object(name)) : Object();
}

inline Object find(const char *name, const char *outer) {
    const auto *a = api();
    return a && name && outer ? Object(a->find_object_in_outer(name, outer)) : Object();
}

inline std::vector<Object> instances_of(Object klass, bool exact = false) {
    const auto *a = api();
    if (!a || !klass)
        return {};
    std::vector<Handle> handles(a->instances_of(klass.handle(), nullptr, 0, exact ? 1 : 0));
    const std::size_t written = a->instances_of(klass.handle(), handles.data(), handles.size(), exact ? 1 : 0);
    handles.resize(written < handles.size() ? written : handles.size());
    std::vector<Object> objects;
    objects.reserve(handles.size());
    for (const Handle handle : handles)
        objects.emplace_back(handle);
    return objects;
}

// Parameters of one UFunction call, copied in and out by name.
class CallFrame {
  public:
    explicit CallFrame(Object function) {
        if (const auto *a = api(); a && function)
            frame_ = a->call_frame_create(function.handle());
    }
    ~CallFrame() {
        if (const auto *a = api(); a && frame_)
            a->call_frame_destroy(frame_);
    }
    CallFrame(const CallFrame &) = delete;
    CallFrame &operator=(const CallFrame &) = delete;

    bool valid() const { return frame_ != nullptr; }
    URK_UnrealCallFrame *raw() const { return frame_; }

    template <typename T> bool set(const char *parameter, const T &value) {
        static_assert(std::is_trivially_copyable_v<T>, "parameters are copied bytewise");
        return frame_ && parameter && api()->call_frame_set(frame_, parameter, &value, sizeof(T)) != 0;
    }
    template <typename T> std::optional<T> get(const char *parameter) const {
        static_assert(std::is_trivially_copyable_v<T>, "parameters are copied bytewise");
        T value{};
        if (!frame_ || !parameter || !api()->call_frame_get(frame_, parameter, &value, sizeof(T)))
            return std::nullopt;
        return value;
    }

  private:
    URK_UnrealCallFrame *frame_ = nullptr;
};

// Runs on the game thread; needs install_process_event_hook() first.
inline bool call(Object object, CallFrame &frame) {
    const auto *a = api();
    return a && object && frame.valid() && a->call(object.handle(), frame.raw()) != 0;
}

inline bool install_process_event_hook() {
    const auto *a = api();
    return a && a->hook_install() != 0;
}
inline bool process_event_hook_installed() {
    const auto *a = api();
    return a && a->hook_installed() != 0;
}
inline bool remove_process_event_hook() {
    const auto *a = api();
    return a && a->hook_remove() != 0;
}

// The observer runs inside ProcessEvent; returning 0 drops the call.
inline void observe_process_event(URK_UnrealProcessEventObserverFn observer, void *user = nullptr) {
    if (const auto *a = api())
        a->process_event_observe(observer, user);
}

// Zero until the hook has seen enough calls to tell which thread is the game's.
inline std::uint32_t game_thread_id() {
    const auto *a = api();
    return a ? a->game_thread_id() : 0;
}

inline bool post_to_game_thread(URK_UnrealPostedWorkFn work, void *user = nullptr) {
    const auto *a = api();
    return a && work && a->post_to_game_thread(work, user) != 0;
}
} // namespace URK::unreal
)URKUE";
}
