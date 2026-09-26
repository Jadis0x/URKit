// sdk/unreal/runtime/core.h: The API table, objects by handle, lookup and struct layout checks.

std::string UnrealRuntimeCore() {
    return R"URKUE(#pragma once


#include "../../runtime_api.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
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
    // Each call walks every object, so the first gets room and a second is rare.
    std::vector<Handle> handles(256);
    std::size_t count = a->instances_of(klass.handle(), handles.data(), handles.size(), exact ? 1 : 0);
    if (count > handles.size()) {
        handles.resize(count);
        count = a->instances_of(klass.handle(), handles.data(), handles.size(), exact ? 1 : 0);
    }
    handles.resize(count < handles.size() ? count : handles.size());
    std::vector<Object> objects;
    objects.reserve(handles.size());
    for (const Handle handle : handles)
        objects.emplace_back(handle);
    return objects;
}

// --- Struct values -------------------------------------------------------------
// Mirrors carry a layout checked against the live struct; a mismatch fails once, loudly.

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
} // namespace URK::unreal
)URKUE";
}
