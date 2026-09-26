// sdk/unreal/runtime/typed.h: Typed access for the generated headers in types/, and their macros.

std::string UnrealRuntimeTyped() {
    return R"URKUE(#pragma once

#include "world.h"

namespace URK::unreal {

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
