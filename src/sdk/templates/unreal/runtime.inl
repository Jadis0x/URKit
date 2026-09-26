// sdk/unreal/unreal_runtime.h: a thin C++ face over URK_UnrealApi.

std::string UnrealRuntimeModule() {
    return R"URKUE(#pragma once

#include "../runtime_api.h"

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

// --- Places: any value, however deep -------------------------------------------
// Member or frame parameter plus steps; re-resolved on every use. See URK_UnrealApi v3.

class Place {
  public:
    Place() = default;
    Place(const Object &owner, const char *member, std::int32_t index = 0) {
        place_.object = owner.handle();
        place_.member = member;
        place_.member_index = index;
    }
    Place(URK_UnrealCallFrame *frame, const char *parameter) {
        place_.frame = frame;
        place_.member = parameter;
    }

    Place element(std::int32_t index) const { return step(URK_UNREAL_STEP_ELEMENT, index, nullptr); }
    Place member(const char *name, std::int32_t index = 0) const { return step(URK_UNREAL_STEP_MEMBER, index, name); }
    Place key(std::int32_t slot) const { return step(URK_UNREAL_STEP_KEY, slot, nullptr); }
    Place value(std::int32_t slot) const { return step(URK_UNREAL_STEP_VALUE, slot, nullptr); }

    const URK_UnrealPlace *raw() const { return &place_; }
    bool valid() const { return !broken_ && place_.member && api(); }

    // An element type is described even for an empty container: pass index -1.
    std::optional<PropertyInfo> describe() const {
        PropertyInfo info{};
        info.size = sizeof(info);
        if (!valid() || !api()->place_describe(&place_, &info))
            return std::nullopt;
        return info;
    }
    std::optional<std::int64_t> get_int() const {
        std::int64_t value = 0;
        return valid() && api()->place_read_integer(&place_, &value) ? std::optional<std::int64_t>(value) : std::nullopt;
    }
    bool set_int(std::int64_t value) const { return valid() && api()->place_write_integer(&place_, value) != 0; }
    std::optional<double> get_float() const {
        double value = 0;
        return valid() && api()->place_read_floating(&place_, &value) ? std::optional<double>(value) : std::nullopt;
    }
    bool set_float(double value) const { return valid() && api()->place_write_floating(&place_, value) != 0; }
    std::optional<bool> get_bool() const {
        int value = 0;
        return valid() && api()->place_read_bool(&place_, &value) ? std::optional<bool>(value != 0) : std::nullopt;
    }
    bool set_bool(bool value) const { return valid() && api()->place_write_bool(&place_, value ? 1 : 0) != 0; }
    Handle get_object() const { return valid() ? api()->place_read_object(&place_) : null_handle; }
    bool set_object(Handle value) const { return valid() && api()->place_write_object(&place_, value) != 0; }
    // Names, strings, texts, enum value names, soft reference paths.
    std::optional<std::string> get_text() const {
        if (!valid())
            return std::nullopt;
        std::string buffer(256, '\0');
        std::size_t length = 0;
        if (!api()->place_read_text(&place_, buffer.data(), buffer.size(), &length)) {
            if (length < buffer.size())
                return std::nullopt;
            buffer.assign(length + 1, '\0');
            if (!api()->place_read_text(&place_, buffer.data(), buffer.size(), &length))
                return std::nullopt;
        }
        buffer.resize(length);
        return buffer;
    }
    bool set_text(const std::string &text) const { return valid() && api()->place_write_text(&place_, text.c_str()) != 0; }
    bool get_bytes(void *output, std::size_t size) const {
        return valid() && api()->place_read_bytes(&place_, output, size) != 0;
    }
    bool set_bytes(const void *value, std::size_t size) const {
        return valid() && api()->place_write_bytes(&place_, value, size) != 0;
    }

    std::int32_t count() const { return valid() ? api()->place_count(&place_) : -1; }
    std::vector<std::int32_t> slots() const {
        if (!valid())
            return {};
        std::vector<std::int32_t> slots(64);
        std::int32_t total = api()->place_slots(&place_, slots.data(), static_cast<std::int32_t>(slots.size()));
        if (total > static_cast<std::int32_t>(slots.size())) {
            slots.resize(static_cast<std::size_t>(total));
            total = api()->place_slots(&place_, slots.data(), total);
        }
        slots.resize(total > 0 ? static_cast<std::size_t>(total) : 0);
        return slots;
    }
    bool insert(std::int32_t index, std::int32_t count = 1) const {
        return valid() && api()->place_insert(&place_, index, count) != 0;
    }
    bool remove(std::int32_t index, std::int32_t count = 1) const {
        return valid() && api()->place_remove(&place_, index, count) != 0;
    }
    bool clear() const { return valid() && api()->place_clear(&place_) != 0; }
    std::int32_t find(const URK_UnrealKey &key) const { return valid() ? api()->place_find(&place_, &key) : -1; }
    std::int32_t add(const URK_UnrealKey &key) const { return valid() ? api()->place_add(&place_, &key) : -1; }
    bool bind(Handle object, const char *function) const {
        return valid() && api()->place_bind(&place_, object, function) != 0;
    }

  private:
    Place step(std::int32_t kind, std::int32_t index, const char *name) const {
        Place next = *this;
        if (next.place_.step_count >= URK_UNREAL_PLACE_MAX_STEPS)
            next.broken_ = true;
        else
            next.place_.steps[next.place_.step_count++] = URK_UnrealStep{kind, index, name};
        return next;
    }

    URK_UnrealPlace place_{};
    bool broken_ = false;
};

// A delegate binding: an object and the name of the function it calls.
struct Binding {
    Object object;
    std::string function;
};

// A soft reference by its path; the asset need not be loaded.
struct SoftPath {
    std::string path;
};

// Element without a mirror: counted, inserted, removed, reached via at(), never copied.
struct Opaque {};

// Enum value by name, resolved in the running game; values read from the game keep their number.
template <typename Self> class Enum {
  public:
    using UrkEnumTag = void;
    constexpr Enum() = default;
    constexpr explicit Enum(const char *name) : name_(name) {}
    static Self from_value(std::int64_t value) {
        Self self;
        self.value_ = value;
        self.known_ = true;
        return self;
    }
    static Object enum_object() { return find(Self::kName, Self::kPackage); }
    const char *name_literal() const { return name_; }

    std::optional<std::int64_t> value() const {
        if (known_)
            return value_;
        std::int64_t value = 0;
        const auto *a = api();
        const Object type = enum_object();
        if (!a || !type || !name_ || !a->enum_value(type.handle(), name_, &value))
            return std::nullopt;
        return value;
    }
    std::string name() const {
        if (name_)
            return name_;
        char buffer[256]{};
        const auto *a = api();
        const Object type = enum_object();
        return a && type && a->enum_name(type.handle(), value_, buffer, sizeof(buffer)) ? std::string(buffer)
                                                                                        : std::to_string(value_);
    }
    bool operator==(const Enum &other) const {
        const auto mine = value();
        const auto theirs = other.value();
        return mine && theirs && *mine == *theirs;
    }

  private:
    const char *name_ = nullptr;
    std::int64_t value_ = 0;
    bool known_ = false;
};

template <typename T>
concept StructMirror = requires {
    T::kSize;
    T::kFields;
};
template <typename T>
concept EnumType = requires { typename T::UrkEnumTag; };

// How a C++ value is read from and written to a place, and made a set/map key.
template <typename T> struct Traits;

template <> struct Traits<bool> {
    static std::optional<bool> get(const Place &place) { return place.get_bool(); }
    static bool set(const Place &place, bool value) { return place.set_bool(value); }
    static void key(const bool &value, URK_UnrealKey &key) { key.integer = value ? 1 : 0; }
};

template <typename T>
    requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
struct Traits<T> {
    static std::optional<T> get(const Place &place) {
        const auto value = place.get_int();
        return value ? std::optional<T>(static_cast<T>(*value)) : std::nullopt;
    }
    static bool set(const Place &place, T value) { return place.set_int(static_cast<std::int64_t>(value)); }
    static void key(const T &value, URK_UnrealKey &key) { key.integer = static_cast<std::int64_t>(value); }
};

template <typename T>
    requires std::is_floating_point_v<T>
struct Traits<T> {
    static std::optional<T> get(const Place &place) {
        const auto value = place.get_float();
        return value ? std::optional<T>(static_cast<T>(*value)) : std::nullopt;
    }
    static bool set(const Place &place, T value) { return place.set_float(static_cast<double>(value)); }
    static void key(const T &value, URK_UnrealKey &key) { key.floating = static_cast<double>(value); }
};

// Names, strings (FString, UTF-8, ANSI) and texts, as UTF-8.
template <> struct Traits<std::string> {
    static std::optional<std::string> get(const Place &place) { return place.get_text(); }
    static bool set(const Place &place, const std::string &value) { return place.set_text(value); }
    static void key(const std::string &value, URK_UnrealKey &key) { key.text = value.c_str(); }
};

template <typename T>
    requires std::is_base_of_v<Object, T>
struct Traits<T> {
    static std::optional<T> get(const Place &place) {
        if (!place.valid())
            return std::nullopt;
        return T(place.get_object());
    }
    static bool set(const Place &place, const T &value) { return place.set_object(value.handle()); }
    static void key(const T &value, URK_UnrealKey &key) { key.object = value.handle(); }
};

template <StructMirror T> struct Traits<T> {
    static std::optional<T> get(const Place &place) {
        T value{};
        if (!layout_matches<T>() || !place.get_bytes(&value, T::kSize))
            return std::nullopt;
        return value;
    }
    static bool set(const Place &place, const T &value) {
        return layout_matches<T>() && place.set_bytes(&value, T::kSize);
    }
    static void key(const T &value, URK_UnrealKey &key) {
        key.bytes = &value;
        key.size = T::kSize;
    }
};

template <EnumType T> struct Traits<T> {
    static std::optional<T> get(const Place &place) {
        const auto value = place.get_int();
        return value ? std::optional<T>(T::from_value(*value)) : std::nullopt;
    }
    static bool set(const Place &place, const T &value) {
        if (value.name_literal())
            return place.set_text(value.name_literal());
        const auto number = value.value();
        return number && place.set_int(*number);
    }
    static void key(const T &value, URK_UnrealKey &key) {
        if (value.name_literal())
            key.text = value.name_literal();
        else
            key.integer = value.value().value_or(0);
    }
};

template <> struct Traits<Opaque> {
    static std::optional<Opaque> get(const Place &place) {
        return place.describe() ? std::optional<Opaque>(Opaque{}) : std::nullopt;
    }
    // Default elements are made by insert/add; an Opaque carries no value to write.
    static bool set(const Place &place, const Opaque &) { return place.describe().has_value(); }
};

template <> struct Traits<SoftPath> {
    static std::optional<SoftPath> get(const Place &place) {
        auto path = place.get_text();
        return path ? std::optional<SoftPath>(SoftPath{std::move(*path)}) : std::nullopt;
    }
    static bool set(const Place &place, const SoftPath &value) { return place.set_text(value.path); }
};

template <> struct Traits<Binding> {
    static std::optional<Binding> get(const Place &place) {
        if (!place.valid())
            return std::nullopt;
        return Binding{Object(place.get_object()), place.get_text().value_or(std::string())};
    }
    static bool set(const Place &place, const Binding &value) {
        return place.bind(value.object.handle(), value.function.empty() ? nullptr : value.function.c_str());
    }
};

// An array, a set (its values, in iteration order) or a multicast delegate.
template <typename T> struct Traits<std::vector<T>> {
    static std::optional<std::vector<T>> get(const Place &place) {
        const auto info = place.describe();
        if (!info)
            return std::nullopt;
        std::vector<T> values;
        if (info->kind == URK_UNREAL_PROPERTY_SET) {
            for (const std::int32_t slot : place.slots()) {
                auto value = Traits<T>::get(place.element(slot));
                if (!value)
                    return std::nullopt;
                values.push_back(std::move(*value));
            }
            return values;
        }
        const std::int32_t count = place.count();
        if (count < 0)
            return std::nullopt;
        for (std::int32_t i = 0; i < count; ++i) {
            auto value = Traits<T>::get(place.element(i));
            if (!value)
                return std::nullopt;
            values.push_back(std::move(*value));
        }
        return values;
    }
    static bool set(const Place &place, const std::vector<T> &values) {
        const auto info = place.describe();
        if (!info || !place.clear())
            return false;
        if (info->kind == URK_UNREAL_PROPERTY_SET) {
            // Only kinds the loader can hash and compare are set elements.
            if constexpr (requires(const T &value, URK_UnrealKey &key) { Traits<T>::key(value, key); }) {
                for (const T &value : values) {
                    URK_UnrealKey key{};
                    Traits<T>::key(value, key);
                    if (place.add(key) < 0)
                        return false;
                }
                return true;
            } else {
                return false;
            }
        }
        if (values.empty())
            return true;
        if (!place.insert(0, static_cast<std::int32_t>(values.size())))
            return false;
        for (std::size_t i = 0; i < values.size(); ++i) {
            if (!Traits<T>::set(place.element(static_cast<std::int32_t>(i)), values[i]))
                return false;
        }
        return true;
    }
};

// A map, as its entries in iteration order.
template <typename K, typename V> struct Traits<std::vector<std::pair<K, V>>> {
    static std::optional<std::vector<std::pair<K, V>>> get(const Place &place) {
        std::vector<std::pair<K, V>> entries;
        if (place.count() < 0)
            return std::nullopt;
        for (const std::int32_t slot : place.slots()) {
            auto key = Traits<K>::get(place.key(slot));
            auto value = Traits<V>::get(place.value(slot));
            if (!key || !value)
                return std::nullopt;
            entries.emplace_back(std::move(*key), std::move(*value));
        }
        return entries;
    }
    static bool set(const Place &place, const std::vector<std::pair<K, V>> &entries) {
        if (!place.clear())
            return false;
        for (const auto &[key, value] : entries) {
            URK_UnrealKey raw{};
            Traits<K>::key(key, raw);
            const std::int32_t slot = place.add(raw);
            if (slot < 0 || !Traits<V>::set(place.value(slot), value))
                return false;
        }
        return true;
    }
};

// Parameters of one UFunction call, copied in and out by name.
class FrameView {
  public:
    explicit FrameView(URK_UnrealCallFrame *frame = nullptr) : frame_(frame) {}

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

    // Any kind through a place; what the loader makes here is released with the frame.
    Place parameter(const char *name) const { return Place(frame_, name); }
    template <typename T> bool set_value(const char *parameter, const T &value) {
        return frame_ && parameter && Traits<T>::set(Place(frame_, parameter), value);
    }
    template <typename T> std::optional<T> get_value(const char *parameter) const {
        if (!frame_ || !parameter)
            return std::nullopt;
        return Traits<T>::get(Place(frame_, parameter));
    }

  protected:
    URK_UnrealCallFrame *frame_ = nullptr;
};

// A frame of its own, for call().
class CallFrame : public FrameView {
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

// One hooked call; parameters and return value valid until the callback returns.
class HookedCall : public FrameView {
  public:
    explicit HookedCall(const URK_UnrealHookedCall &call) : FrameView(call.frame), call_(call) {}
    Object object() const { return Object(call_.object); }
    Object function() const { return Object(call_.function); }
    bool after() const { return call_.after != 0; }
    // After only: a before callback skipped the body.
    bool skipped() const { return call_.skipped != 0; }

  private:
    URK_UnrealHookedCall call_;
};

// Before/after callbacks for one function, removed on destruction.
// before returns false to skip the body. Nested calls aren't hooked.
class FunctionHook {
  public:
    using Before = std::function<bool(HookedCall &)>;
    using After = std::function<void(HookedCall &)>;

    FunctionHook() = default;
    FunctionHook(Object function, Before before, After after = {}) {
        const auto *a = api();
        if (!a || !function || (!before && !after))
            return;
        state_ = std::make_unique<State>();
        state_->before = std::move(before);
        state_->after = std::move(after);
        id_ = a->function_hook_add(function.handle(), state_->before ? &RunBefore : nullptr,
                                   state_->after ? &RunAfter : nullptr, state_.get());
        if (id_ == 0)
            state_.reset();
    }
    // Each broadcast of a multicast delegate, as a binding would get it (see subscribe()).
    FunctionHook(const Place &delegate, After on_broadcast) : delegate_(true) {
        const auto *a = api();
        if (!a || !on_broadcast || !delegate.valid())
            return;
        state_ = std::make_unique<State>();
        state_->after = std::move(on_broadcast);
        id_ = a->delegate_subscribe(delegate.raw(), &RunAfter, state_.get());
        if (id_ == 0)
            state_.reset();
    }
    ~FunctionHook() { remove(); }
    FunctionHook(FunctionHook &&other) noexcept
        : state_(std::move(other.state_)), id_(std::exchange(other.id_, 0)), delegate_(other.delegate_) {}
    FunctionHook &operator=(FunctionHook &&other) noexcept {
        if (this != &other) {
            remove();
            state_ = std::move(other.state_);
            id_ = std::exchange(other.id_, 0);
            delegate_ = other.delegate_;
        }
        return *this;
    }
    FunctionHook(const FunctionHook &) = delete;
    FunctionHook &operator=(const FunctionHook &) = delete;

    bool active() const { return id_ != 0; }
    // False when a callback still running elsewhere kept the hook alive.
    bool remove() {
        if (id_ == 0)
            return true;
        const auto *a = api();
        const bool removed = a && (delegate_ ? a->delegate_unsubscribe(id_) : a->function_hook_remove(id_)) != 0;
        id_ = 0;
        // Removed from inside its own callback: that call frees it on return.
        if (removed && state_->running.load(std::memory_order_acquire) > 0)
            state_->orphaned = true;
        if (removed && !state_->orphaned)
            state_.reset();
        else
            state_.release();
        return removed;
    }

  private:
    struct State {
        Before before;
        After after;
        std::atomic<int> running{0};
        bool orphaned = false;
    };

    template <typename Body> static int Run(State *state, Body body) {
        state->running.fetch_add(1, std::memory_order_acq_rel);
        int answer = 1;
        try {
            answer = body() ? 1 : 0;
        } catch (const std::exception &error) {
            URK::log((std::string("[unreal] hook threw: ") + error.what()).c_str());
        } catch (...) {
            URK::log("[unreal] hook threw");
        }
        if (state->running.fetch_sub(1, std::memory_order_acq_rel) == 1 && state->orphaned)
            delete state;
        return answer;
    }
    static int RunBefore(void *user, const URK_UnrealHookedCall *raw) {
        auto *state = static_cast<State *>(user);
        return Run(state, [&] {
            HookedCall call(*raw);
            return state->before(call);
        });
    }
    static int RunAfter(void *user, const URK_UnrealHookedCall *raw) {
        auto *state = static_cast<State *>(user);
        return Run(state, [&] {
            HookedCall call(*raw);
            state->after(call);
            return true;
        });
    }

    std::unique_ptr<State> state_;
    std::uint64_t id_ = 0;
    bool delegate_ = false;
};
using Subscription = FunctionHook;

// By the function's name on a class or an instance's class.
inline FunctionHook hook(Object owner, const char *function, FunctionHook::Before before,
                         FunctionHook::After after = {}) {
    return FunctionHook(owner.function(function), std::move(before), std::move(after));
}

// A multicast delegate's broadcasts until the Subscription goes away. The call's object is the
// delegate's owner; parameters read by name. Starts on the game thread. `unreal::subscribe(
// health.OnDamaged().place(), [](unreal::HookedCall &call) { call.get<float>("Damage"); })`
inline Subscription subscribe(const Place &delegate, FunctionHook::After on_broadcast) {
    return Subscription(delegate, std::move(on_broadcast));
}

// A hooked call seen through its owner's type; the generated <Function>_Call adds the parameters.
template <typename Owner> class TypedCall : public HookedCall {
  public:
    explicit TypedCall(const HookedCall &call) : HookedCall(call) {}
    template <typename U = Owner> U self() const { return U(object().handle()); }
};

// For the generated hook_<Function>: before may return nothing (the body runs) or false to skip it.
template <typename View, typename Before, typename After>
FunctionHook typed_hook(Object function, Before before, After after) {
    FunctionHook::Before first;
    FunctionHook::After second;
    if constexpr (!std::is_null_pointer_v<Before>)
        first = [run = std::move(before)](HookedCall &call) mutable {
            View view(call);
            if constexpr (std::is_void_v<std::invoke_result_t<Before &, View &>>) {
                run(view);
                return true;
            } else {
                return static_cast<bool>(run(view));
            }
        };
    if constexpr (!std::is_null_pointer_v<After>)
        second = [run = std::move(after)](HookedCall &call) mutable {
            View view(call);
            run(view);
        };
    return FunctionHook(function, std::move(first), std::move(second));
}

// --- Spawning -------------------------------------------------------------------

struct Location {
    double x = 0, y = 0, z = 0;
};
// Degrees, as the editor shows them.
struct Rotation {
    double pitch = 0, yaw = 0, roll = 0;
};

namespace detail {
// FTransform by member name: float in UE4, double in UE5, the loader converts.
inline bool SetTransform(const Place &transform, const Location &at, const Rotation &facing) {
    // FRotator::Quaternion.
    constexpr double kHalfRadians = 3.14159265358979323846 / 360.0;
    const double sp = std::sin(facing.pitch * kHalfRadians), cp = std::cos(facing.pitch * kHalfRadians);
    const double sy = std::sin(facing.yaw * kHalfRadians), cy = std::cos(facing.yaw * kHalfRadians);
    const double sr = std::sin(facing.roll * kHalfRadians), cr = std::cos(facing.roll * kHalfRadians);
    const Place translation = transform.member("Translation");
    const Place rotation = transform.member("Rotation");
    const Place scale = transform.member("Scale3D");
    return translation.member("X").set_float(at.x) && translation.member("Y").set_float(at.y) &&
           translation.member("Z").set_float(at.z) && rotation.member("X").set_float(cr * sp * sy - sr * cp * cy) &&
           rotation.member("Y").set_float(-cr * sp * cy - sr * cp * sy) &&
           rotation.member("Z").set_float(cr * cp * sy - sr * sp * cy) &&
           rotation.member("W").set_float(cr * cp * cy + sr * sp * sy) && scale.member("X").set_float(1) &&
           scale.member("Y").set_float(1) && scale.member("Z").set_float(1);
}

// UE5's scale choice, absent before: keep the class's own root scale.
inline bool SetScaleMethod(const FrameView &frame) {
    const Place method = frame.parameter("TransformScaleMethod");
    return !method.describe() || method.set_text("MultiplyWithRoot");
}
} // namespace detail

// Spawns like Blueprint's SpawnActor (construction script, BeginPlay). Game thread; null on failure.
inline Object spawn_actor(Object world_context, Object klass, Location at = {}, Rotation facing = {},
                          Object owner = {}) {
    const Object statics = find("GameplayStatics");
    if (!statics || !world_context || !klass)
        return Object();
    Object actor;
    {
        CallFrame begin(statics.function("BeginDeferredActorSpawnFromClass"));
        if (!begin.set<Handle>("WorldContextObject", world_context.handle()) ||
            !begin.set<Handle>("ActorClass", klass.handle()) || !begin.set<Handle>("Owner", owner.handle()) ||
            !detail::SetTransform(begin.parameter("SpawnTransform"), at, facing) || !detail::SetScaleMethod(begin) ||
            !call(statics.default_object(), begin))
            return Object();
        actor = Object(begin.get<Handle>("ReturnValue").value_or(null_handle));
    }
    if (!actor)
        return Object();
    CallFrame finish(statics.function("FinishSpawningActor"));
    if (!finish.set<Handle>("Actor", actor.handle()) ||
        !detail::SetTransform(finish.parameter("SpawnTransform"), at, facing) || !detail::SetScaleMethod(finish) ||
        !call(statics.default_object(), finish))
        return Object();
    return Object(finish.get<Handle>("ReturnValue").value_or(null_handle));
}

// Game thread. The engine frees it once nothing refers to it any more.
inline bool destroy_actor(Object actor) {
    CallFrame frame(actor.function("K2_DestroyActor"));
    return actor && frame.valid() && call(actor, frame);
}

namespace detail {
// A bare package path names its main asset: "/Game/Doors/BP_Door" -> ".BP_Door" (+ "_C" for its class).
inline std::string AssetPath(const std::string &path, const char *suffix) {
    const std::size_t slash = path.find_last_of('/');
    if (path.find('.', slash == std::string::npos ? 0 : slash) != std::string::npos)
        return path;
    return path + "." + path.substr(slash == std::string::npos ? 0 : slash + 1) + suffix;
}

// KismetSystemLibrary's blocking loads take a soft reference, written as its path.
inline Object LoadByPath(const char *function, const char *parameter, const std::string &path) {
    const Object library = find("KismetSystemLibrary");
    if (!library || path.empty())
        return Object();
    CallFrame frame(library.function(function));
    if (!frame.valid() || !frame.parameter(parameter).set_text(path) || !call(library.default_object(), frame))
        return Object();
    return Object(frame.get<Handle>("ReturnValue").value_or(null_handle));
}
} // namespace detail

// The class at a path, loading it if no map has: "/Game/Doors/BP_Door.BP_Door_C", or just
// "/Game/Doors/BP_Door". Game thread; blocks while it loads; null when there is none.
inline Object load_class(const std::string &path) {
    return detail::LoadByPath("LoadClassAsset_Blocking", "AssetClass", detail::AssetPath(path, "_C"));
}

// Any asset by path, as load_class: "/Game/Items/DA_Stick.DA_Stick" or "/Game/Items/DA_Stick".
inline Object load_object(const std::string &path) {
    return detail::LoadByPath("LoadAsset_Blocking", "Asset", detail::AssetPath(path, ""));
}

// --- The player and its world ---------------------------------------------------
// Plain member reads: any thread, cheap per frame. Null before a map loads or between maps.

// The running engine (GEngine). Found once, with one walk of the object array.
inline Object engine() {
    static std::atomic<Handle> cached{null_handle};
    const Object known(cached.load());
    if (known && known.klass())
        return known;
    // A game may subclass either; the class itself is never the running engine.
    for (const char *name : {"GameEngine", "Engine"}) {
        const std::vector<Object> found = instances_of(find(name));
        if (!found.empty()) {
            cached.store(found.front().handle());
            return found.front();
        }
    }
    return Object();
}

// The world being played: the game viewport's.
inline Object world() { return engine().get_object("GameViewport").get_object("World"); }
inline Object game_instance() { return world().get_object("OwningGameInstance"); }
inline Object game_state() { return world().get_object("GameState"); }
// Only where this process is the server (single player too); null on a client.
inline Object game_mode() { return world().get_object("AuthorityGameMode"); }

// Split screen has more than one; index 0 is the first.
inline Object local_player(std::int32_t index = 0) {
    return Object(Place(game_instance(), "LocalPlayers").element(index).get_object());
}
inline Object player_controller(std::int32_t index = 0) { return local_player(index).get_object("PlayerController"); }
// Null while the controller possesses nothing (menus, respawns).
inline Object player_pawn(std::int32_t index = 0) { return player_controller(index).get_object("Pawn"); }

// An actor's first component of a class, as Blueprint's GetComponentByClass. Game thread.
inline Object component(Object actor, Object klass) {
    CallFrame frame(actor.function("GetComponentByClass"));
    if (!klass || !frame.set<Handle>("ComponentClass", klass.handle()) || !call(actor, frame))
        return Object();
    return Object(frame.get<Handle>("ReturnValue").value_or(null_handle));
}
// Typed: `component<types::HealthComponent>(player)`.
template <typename T> T component(Object actor) { return T::cast(component(actor, T::static_class())); }

// Every component of a class (subclasses included). Game thread.
inline std::vector<Object> components(Object actor, Object klass) {
    CallFrame frame(actor.function("K2_GetComponentsByClass"));
    if (!klass || !frame.set<Handle>("ComponentClass", klass.handle()) || !call(actor, frame))
        return {};
    return frame.get_value<std::vector<Object>>("ReturnValue").value_or(std::vector<Object>{});
}
template <typename T> std::vector<T> components(Object actor) {
    std::vector<T> typed;
    for (const Object &found : components(actor, T::static_class()))
        typed.push_back(T::cast(found));
    return typed;
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

// Runs work on the game thread before the next frame: menus draw on the render thread, where
// calls and engine-memory writes are refused. `on_game_thread([] { player.Jump(); });`
inline bool on_game_thread(std::function<void()> work) {
    if (!work)
        return false;
    auto *queued = new std::function<void()>(std::move(work));
    const bool posted = post_to_game_thread(
        [](void *user) {
            const std::unique_ptr<std::function<void()>> run(static_cast<std::function<void()> *>(user));
            // An exception must not unwind into engine frames.
            try {
                (*run)();
            } catch (...) {
            }
        },
        queued);
    if (!posted)
        delete queued;
    return posted;
}

// --- Keys ------------------------------------------------------------------------
// Windows virtual-key codes ('K', VK_F5, VK_LBUTTON) while the game has focus, sampled once per
// frame: `if (unreal::key_pressed(VK_F5))` in update() fires once per press. Any thread.
#define URK_UNREAL_KEY(field, key)                                                                                    \
    (::URK::runtime_api_has_field(offsetof(::URK::RuntimeApi, field) + sizeof(void *)) &&                             \
     ::URK::context()->runtime->field && ::URK::context()->runtime->field(key) != 0)
inline bool key_held(int key) { return URK_UNREAL_KEY(input_get_key, key); }
// Went down this frame.
inline bool key_pressed(int key) { return URK_UNREAL_KEY(input_get_key_down, key); }
// Went up this frame.
inline bool key_released(int key) { return URK_UNREAL_KEY(input_get_key_up, key); }
#undef URK_UNREAL_KEY

// --- Typed access, used by the generated headers in types/ -------------------
// Members are resolved by name on the live class, so offset changes need no rebuild.

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

// Any member through a place, as the C++ type T its kind maps to.
template <typename T> class Member {
  public:
    Member(const Place &place) : place_(place) {}
    Member(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}
    std::optional<T> get() const { return Traits<T>::get(place_); }
    bool set(const T &value) const { return Traits<T>::set(place_, value); }
    const Place &place() const { return place_; }

  private:
    Place place_;
};

// FName, FString and FText members as UTF-8. Writes go through the engine (game thread).
using NameValue = Member<std::string>;
using StringValue = Member<std::string>;
using TextValue = Member<std::string>;
// An enum member typed by its generated enum: E::Name() values resolve by name.
template <typename E> using EnumMember = Member<E>;

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

// An object reached through a place (a hooked call's parameter), typed as T.
template <typename T> class ObjectPlace {
  public:
    ObjectPlace(const Place &place) : place_(place) {}
    template <typename U = T> U get() const { return U(place_.get_object()); }
    bool set(Arg<T> value) const { return place_.set_object(value.handle()); }
    const Place &place() const { return place_; }

  private:
    Place place_;
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

// Struct member. Writes may change numbers, nested structs and objects; not allocations.
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
    // To reach members the mirror keeps as bytes, through its place accessors.
    Place place() const { return Place(owner_, member_, index_); }

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

// TArray member. Changes run on the game thread; reads anywhere. at(i) reaches an element.
template <typename T> class ArrayMember {
  public:
    ArrayMember(const Place &place) : place_(place) {}
    ArrayMember(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}

    // -1 when the array cannot be read.
    std::int32_t size() const { return place_.count(); }
    Place at(std::int32_t index) const { return place_.element(index); }
    std::optional<T> get(std::int32_t index) const { return Traits<T>::get(at(index)); }
    bool set(std::int32_t index, const T &value) const { return Traits<T>::set(at(index), value); }
    bool insert(std::int32_t index, const T &value) const {
        if (!place_.insert(index, 1))
            return false;
        if (Traits<T>::set(at(index), value))
            return true;
        place_.remove(index, 1);
        return false;
    }
    bool add(const T &value) const {
        const std::int32_t count = size();
        return count >= 0 && insert(count, value);
    }
    // count default elements before index, to fill in place through at().
    bool insert_default(std::int32_t index, std::int32_t count = 1) const { return place_.insert(index, count); }
    bool remove_at(std::int32_t index, std::int32_t count = 1) const { return place_.remove(index, count); }
    bool clear() const { return place_.clear(); }
    std::optional<std::vector<T>> get() const { return Traits<std::vector<T>>::get(place_); }
    bool set(const std::vector<T> &values) const { return Traits<std::vector<T>>::set(place_, values); }
    const Place &place() const { return place_; }

  private:
    Place place_;
};

// A TSet member, keyed as the engine keys it (strings ignore case).
template <typename T> class SetMember {
  public:
    SetMember(const Place &place) : place_(place) {}
    SetMember(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}

    std::int32_t size() const { return place_.count(); }
    bool contains(const T &value) const { return slot(value) >= 0; }
    bool add(const T &value) const {
        URK_UnrealKey key{};
        Traits<T>::key(value, key);
        return place_.add(key) >= 0;
    }
    bool remove(const T &value) const {
        const std::int32_t found = slot(value);
        return found >= 0 && place_.remove(found, 1);
    }
    bool clear() const { return place_.clear(); }
    std::optional<std::vector<T>> values() const { return Traits<std::vector<T>>::get(place_); }
    const Place &place() const { return place_; }

  private:
    std::int32_t slot(const T &value) const {
        URK_UnrealKey key{};
        Traits<T>::key(value, key);
        return place_.find(key);
    }
    Place place_;
};

// A TMap member. set() adds a missing key or replaces the value of one there.
template <typename K, typename V> class MapMember {
  public:
    MapMember(const Place &place) : place_(place) {}
    MapMember(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}

    std::int32_t size() const { return place_.count(); }
    bool contains(const K &key) const { return slot(key) >= 0; }
    std::optional<V> find(const K &key) const {
        const std::int32_t found = slot(key);
        return found >= 0 ? Traits<V>::get(place_.value(found)) : std::nullopt;
    }
    // The value's place, to reach into a struct value.
    std::optional<Place> at(const K &key) const {
        const std::int32_t found = slot(key);
        return found >= 0 ? std::optional<Place>(place_.value(found)) : std::nullopt;
    }
    bool set(const K &key, const V &value) const {
        URK_UnrealKey raw{};
        Traits<K>::key(key, raw);
        const std::int32_t found = place_.add(raw);
        return found >= 0 && Traits<V>::set(place_.value(found), value);
    }
    // The key's value place, the key added (with a default value) if missing.
    std::optional<Place> add(const K &key) const {
        URK_UnrealKey raw{};
        Traits<K>::key(key, raw);
        const std::int32_t found = place_.add(raw);
        return found >= 0 ? std::optional<Place>(place_.value(found)) : std::nullopt;
    }
    bool remove(const K &key) const {
        const std::int32_t found = slot(key);
        return found >= 0 && place_.remove(found, 1);
    }
    bool clear() const { return place_.clear(); }
    std::optional<std::vector<std::pair<K, V>>> entries() const {
        return Traits<std::vector<std::pair<K, V>>>::get(place_);
    }
    std::optional<std::vector<K>> keys() const {
        std::vector<K> keys;
        if (place_.count() < 0)
            return std::nullopt;
        for (const std::int32_t found : place_.slots()) {
            auto key = Traits<K>::get(place_.key(found));
            if (!key)
                return std::nullopt;
            keys.push_back(std::move(*key));
        }
        return keys;
    }
    const Place &place() const { return place_; }

  private:
    std::int32_t slot(const K &key) const {
        URK_UnrealKey raw{};
        Traits<K>::key(key, raw);
        return place_.find(raw);
    }
    Place place_;
};

// A TSoftObjectPtr/TSoftClassPtr member: its path, and its object once loaded.
template <typename T> class SoftMember {
  public:
    SoftMember(const Place &place) : place_(place) {}
    SoftMember(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}
    std::optional<std::string> path() const { return place_.get_text(); }
    bool set_path(const std::string &path) const { return place_.set_text(path); }
    // Empty until the asset is loaded; nothing is loaded by asking.
    template <typename U = T> U get() const { return U(place_.get_object()); }
    bool set(Arg<T> value) const { return place_.set_object(value.handle()); }
    bool clear() const { return place_.clear(); }
    const Place &place() const { return place_; }

  private:
    Place place_;
};

// A TWeakObjectPtr member: empty once its object is gone.
template <typename T> class WeakMember {
  public:
    WeakMember(const Place &place) : place_(place) {}
    WeakMember(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}
    template <typename U = T> U get() const { return U(place_.get_object()); }
    bool set(Arg<T> value) const { return place_.set_object(value.handle()); }
    const Place &place() const { return place_; }

  private:
    Place place_;
};

// An interface reference: the object; the loader fills the interface address.
using InterfaceMember = WeakMember<Object>;

// Single-cast delegate; bind() needs a function with a matching signature.
class DelegateMember {
  public:
    DelegateMember(const Place &place) : place_(place) {}
    DelegateMember(const Object &owner, const char *member, std::int32_t index = 0) : place_(owner, member, index) {}
    std::optional<Binding> get() const { return Traits<Binding>::get(place_); }
    bool bind(const Object &object, const char *function) const { return place_.bind(object.handle(), function); }
    bool clear() const { return place_.clear(); }
    const Place &place() const { return place_; }

  private:
    Place place_;
};

// An inline multicast delegate member: its bindings. add() is AddUnique.
class MulticastMember {
  public:
    MulticastMember(const Place &place) : place_(place) {}
    MulticastMember(const Object &owner, const char *member, std::int32_t index = 0)
        : place_(owner, member, index) {}
    std::int32_t size() const { return place_.count(); }
    std::optional<std::vector<Binding>> bindings() const { return Traits<std::vector<Binding>>::get(place_); }
    bool contains(const Object &object, const std::string &function) const { return index_of(object, function) >= 0; }
    bool add(const Object &object, const char *function) const {
        if (!function || contains(object, function))
            return function != nullptr;
        // Sparse: bindings live in engine storage; the engine adds them.
        const auto info = place_.describe();
        if (info && info->kind == URK_UNREAL_PROPERTY_SPARSE_DELEGATE)
            return place_.bind(object.handle(), function);
        const std::int32_t count = size();
        if (count < 0 || !place_.insert(count, 1))
            return false;
        if (place_.element(count).bind(object.handle(), function))
            return true;
        place_.remove(count, 1);
        return false;
    }
    bool remove(const Object &object, const std::string &function) const {
        const std::int32_t found = index_of(object, function);
        return found >= 0 && place_.remove(found, 1);
    }
    // A C++ callback on each broadcast, until the Subscription goes away. Game thread.
    Subscription subscribe(FunctionHook::After on_broadcast) const {
        return Subscription(place_, std::move(on_broadcast));
    }
    bool clear() const { return place_.clear(); }
    const Place &place() const { return place_; }

  private:
    std::int32_t index_of(const Object &object, const std::string &function) const {
        const auto list = bindings();
        if (!list)
            return -1;
        for (std::size_t i = 0; i < list->size(); ++i) {
            if ((*list)[i].object.handle() == object.handle() && (*list)[i].function == function)
                return static_cast<std::int32_t>(i);
        }
        return -1;
    }
    Place place_;
};

// A multicast delegate whose broadcasts arrive as View: `health.OnDamaged().subscribe(
// [](HealthComponent::Damaged_Event &event) { event.Damage().get(); })`. Game thread.
template <typename View> class EventMember : public MulticastMember {
  public:
    using MulticastMember::MulticastMember;
    template <typename F> Subscription subscribe(F on_broadcast) const {
        return Subscription(place(), [run = std::move(on_broadcast)](HookedCall &call) mutable {
            View view(call);
            run(view);
        });
    }
};

template <typename T> std::vector<T> typed_instances(Object klass, bool exact) {
    std::vector<T> typed;
    for (const Object &object : instances_of(klass, exact))
        typed.emplace_back(object.handle());
    return typed;
}
} // namespace URK::unreal

// Statics of every generated class; looked up live so reloaded Blueprint classes are found.
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
    static Self class_default() { return Self(static_class().default_object().handle()); }                            \
    /* Loads the class when no map has yet (a Blueprint's). Game thread. */                                            \
    static ::URK::unreal::Object load_class() {                                                                         \
        return ::URK::unreal::load_class(std::string(Package) + "." + ReflectedName);                                  \
    }

// hook_<Function>(before, after) in a generated class; View is the call's typed view.
#define URK_UNREAL_HOOK(View, Hook, Function)                                                                          \
    template <typename Before, typename After = std::nullptr_t>                                                        \
    static ::URK::unreal::FunctionHook Hook(Before before, After after = nullptr) {                                    \
        return ::URK::unreal::typed_hook<View>(static_class().function(Function), std::move(before), std::move(after)); \
    }
)URKUE";
}
