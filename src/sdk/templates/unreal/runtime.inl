// sdk/unreal/unreal_runtime.h: a thin C++ face over URK_UnrealApi.

std::string UnrealRuntimeModule() {
    return R"URKUE(#pragma once

#include "../runtime_api.h"

#include <array>
#include <atomic>
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

// --- Struct values -------------------------------------------------------------
// A struct is copied by value, so its generated mirror carries a layout. The
// layout is checked against the live struct before any copy: a game update that
// changed it makes accesses fail (and says so once) instead of reading garbage.

struct FieldLayout {
    const char *name;
    std::int32_t offset;
    std::int32_t size;
    std::int32_t dim;
    std::int32_t kind;
    // Bools only; see URK_UnrealPropertyInfo.
    std::uint8_t bool_byte_offset;
    std::uint8_t bool_byte_mask;
    std::uint8_t bool_field_mask;
};

// S is a generated mirror: kName, kPackage, kSize, kFields.
template <typename S> bool layout_matches() {
    static std::atomic<int> state{0}; // 0 not checked, 1 matches, 2 differs
    if (const int known = state.load(std::memory_order_acquire))
        return known == 1;
    const auto *a = api();
    const Object type = find(S::kName, S::kPackage);
    if (!a || !type)
        return false; // Not loaded yet: asked again next time.
    std::string differs;
    if (a->struct_size(type.handle()) != S::kSize)
        differs = "size";
    for (const FieldLayout &field : S::kFields) {
        if (!differs.empty())
            break;
        PropertyInfo info{};
        info.size = sizeof(info);
        std::int32_t offset = -1;
        if (!a->describe_struct_member(type.handle(), field.name, &info, &offset))
            differs = std::string(field.name) + " is gone";
        else if (offset != field.offset || info.element_size != field.size || info.array_dim != field.dim ||
                 info.kind != field.kind ||
                 (field.kind == URK_UNREAL_PROPERTY_BOOL &&
                  (info.bool_byte_offset != field.bool_byte_offset || info.bool_byte_mask != field.bool_byte_mask ||
                   info.bool_field_mask != field.bool_field_mask)))
            differs = std::string(field.name) + " changed";
    }
    int expected = 0;
    if (state.compare_exchange_strong(expected, differs.empty() ? 1 : 2) && !differs.empty())
        URK::log(("[unreal] struct " + std::string(S::kName) + " differs from this game build (" + differs +
                  "); regenerate the SDK. Accesses to it fail until then.")
                     .c_str());
    return differs.empty();
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

    // Generated struct mirrors; the loader checks what the struct may carry.
    template <typename S> bool set_struct(const char *parameter, const S &value) {
        return frame_ && parameter && layout_matches<S>() &&
               api()->call_frame_set(frame_, parameter, &value, S::kSize) != 0;
    }
    template <typename S> std::optional<S> get_struct(const char *parameter) const {
        S value{};
        if (!frame_ || !parameter || !layout_matches<S>() ||
            !api()->call_frame_get(frame_, parameter, &value, S::kSize))
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

// --- Typed access, used by the generated headers in types/ -------------------
// Headers carry names only; each access resolves the member on the live class,
// so a game update that moves offsets needs no rebuild.

// A numeric or bool member. Empty when the object is gone or the member is not.
template <typename T> class Value {
  public:
    Value(const Object &owner, const char *member, std::int32_t index = 0)
        : owner_(owner), member_(member), index_(index) {}

    std::optional<T> get() const {
        if constexpr (std::is_same_v<T, bool>) {
            return owner_.get_bool(member_, index_);
        } else if constexpr (std::is_floating_point_v<T>) {
            const auto value = owner_.get_float(member_, index_);
            return value ? std::optional<T>(static_cast<T>(*value)) : std::nullopt;
        } else {
            const auto value = owner_.get_int(member_, index_);
            return value ? std::optional<T>(static_cast<T>(*value)) : std::nullopt;
        }
    }
    bool set(T value) const {
        if constexpr (std::is_same_v<T, bool>)
            return owner_.set_bool(member_, value, index_);
        else if constexpr (std::is_floating_point_v<T>)
            return owner_.set_float(member_, static_cast<double>(value), index_);
        else
            return owner_.set_int(member_, static_cast<std::int64_t>(value), index_);
    }

  private:
    Object owner_;
    const char *member_;
    std::int32_t index_;
};

// FName and FString members are read-only: they own engine allocations.
class NameValue {
  public:
    NameValue(const Object &owner, const char *member, std::int32_t index = 0)
        : owner_(owner), member_(member), index_(index) {}
    std::optional<std::string> get() const { return owner_.get_name(member_, index_); }

  private:
    Object owner_;
    const char *member_;
    std::int32_t index_;
};

class StringValue {
  public:
    StringValue(const Object &owner, const char *member, std::int32_t index = 0)
        : owner_(owner), member_(member), index_(index) {}
    std::optional<std::string> get() const { return owner_.get_string(member_, index_); }

  private:
    Object owner_;
    const char *member_;
    std::int32_t index_;
};

// An object argument that must be a T. T may be incomplete where this is named.
template <typename T> class Arg {
  public:
    Arg(std::nullptr_t) {}
    template <typename U>
        requires std::is_base_of_v<T, U>
    Arg(const U &object) : handle_(object.handle()) {}
    Handle handle() const { return handle_; }

  private:
    Handle handle_ = null_handle;
};

// An object member typed as T. The loader also refuses a value of the wrong class.
template <typename T> class ObjectMember {
  public:
    ObjectMember(const Object &owner, const char *member, std::int32_t index = 0)
        : owner_(owner), member_(member), index_(index) {}
    template <typename U = T> U get() const { return U(owner_.get_object(member_, index_).handle()); }
    bool set(Arg<T> value) const { return owner_.set_object(member_, Object(value.handle()), index_); }

  private:
    Object owner_;
    const char *member_;
    std::int32_t index_;
};

// A struct-valued member. Writes may change numbers and nested structs, and
// objects of the right class; anything owning an allocation must stay as read.
template <typename S> class StructMember {
  public:
    StructMember(const Object &owner, const char *member, std::int32_t index = 0)
        : owner_(owner), member_(member), index_(index) {}
    std::optional<S> get() const {
        S value{};
        if (!owner_.valid() || !layout_matches<S>() ||
            !api()->read_struct(owner_.handle(), member_, index_, &value, S::kSize))
            return std::nullopt;
        return value;
    }
    bool set(const S &value) const {
        return owner_.valid() && layout_matches<S>() &&
               api()->write_struct(owner_.handle(), member_, index_, &value, S::kSize) != 0;
    }

  private:
    Object owner_;
    const char *member_;
    std::int32_t index_;
};

// An object reference inside a struct mirror: pointer-sized, typed as T.
template <typename T> class StructObject {
  public:
    template <typename U = T> U get() const { return U(handle_); }
    void set(Arg<T> value) { handle_ = value.handle(); }
    Handle handle() const { return handle_; }

  private:
    Handle handle_ = null_handle;
};

template <typename T> std::vector<T> typed_instances(Object klass, bool exact) {
    std::vector<T> typed;
    for (const Object &object : instances_of(klass, exact))
        typed.emplace_back(object.handle());
    return typed;
}
} // namespace URK::unreal

// The statics every generated class carries. Lookups go through the live
// object array each time, so a Blueprint class reloaded with its map is found.
#define URK_UNREAL_TYPE(Self, Base, ReflectedName, Package)                                                          \
    Self() = default;                                                                                                  \
    explicit Self(::URK::unreal::Handle handle) : Base(handle) {}                                                      \
    static constexpr const char *kName = ReflectedName;                                                                \
    static constexpr const char *kPackage = Package;                                                                   \
    static ::URK::unreal::Object static_class() { return ::URK::unreal::find(ReflectedName, Package); }                \
    /* Empty unless the object is a Self. */                                                                           \
    static Self cast(const ::URK::unreal::Object &object) {                                                            \
        return object.is_a(static_class()) ? Self(object.handle()) : Self();                                           \
    }                                                                                                                  \
    static std::vector<Self> instances(bool exact = false) {                                                           \
        return ::URK::unreal::typed_instances<Self>(static_class(), exact);                                            \
    }                                                                                                                  \
    static Self class_default() { return Self(static_class().default_object().handle()); }
)URKUE";
}
