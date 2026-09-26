// sdk/unreal/runtime/calls.h: Calling functions, and hooking them and delegates.

std::string UnrealRuntimeCalls() {
    return R"URKUE(#pragma once

#include "values.h"

namespace URK::unreal {

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
} // namespace URK::unreal
)URKUE";
}
