// sdk/unreal/runtime/values.h: Places: any value by member and element steps, and how each type reads and writes.

std::string UnrealRuntimeValues() {
    return R"URKUE(#pragma once

#include "core.h"

namespace URK::unreal {

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
} // namespace URK::unreal
)URKUE";
}
