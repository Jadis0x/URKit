// sdk/unreal/runtime/world.h: Spawning, loading, the player and its world, threads and keys.

std::string UnrealRuntimeWorld() {
    return R"URKUE(#pragma once

#include "calls.h"

namespace URK::unreal {

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
} // namespace URK::unreal
)URKUE";
}
