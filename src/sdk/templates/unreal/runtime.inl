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

// --- Places: any value, however deep -------------------------------------------
// A member (or a call frame's parameter) and steps into it: array elements,
// set and map slots, map keys and values, struct members. The loader resolves
// a place again on every use, so a container that reallocated is never read
// through a stale address. Strings, texts and containers are changed by the
// engine's own code on the game thread; see URK_UnrealApi version 3.

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

// An element with no mirror to copy it into (a struct whose layout the
// generator could not express): counted, inserted, removed, and reached in
// place through at(), never copied.
struct Opaque {};

// An enum value by name. Generated enum types list names only; each name is
// looked up in the running game's enum, so a patch that renumbers it changes
// nothing. A value read from the game carries its number instead.
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

    // Any kind through a place: strings, texts, enums, containers. What the
    // loader makes here is the frame's, and goes back to the engine with it.
    Place parameter(const char *name) const { return Place(frame_, name); }
    template <typename T> bool set_value(const char *parameter, const T &value) {
        return frame_ && parameter && Traits<T>::set(Place(frame_, parameter), value);
    }
    template <typename T> std::optional<T> get_value(const char *parameter) const {
        if (!frame_ || !parameter)
            return std::nullopt;
        return Traits<T>::get(Place(frame_, parameter));
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

// FName, FString and FText members as UTF-8. Writes go through the engine
// (game thread): a name through its name table, a string into an engine buffer,
// a text through its own conversion - the old value released the same way.
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

// A TArray member. Elements are made, released and moved by the engine's own
// code on the game thread; reads work anywhere. at(i) reaches into an element
// (a struct element's members through its mirror's place accessors).
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

// A single-cast delegate member. bind() is refused unless the function exists
// on the object with the delegate's signature.
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
