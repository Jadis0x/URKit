# Writing Mods with URKit

This is not an API catalogue. It is the order of work I recommend if you want
to get a first mod running and still have a project you can maintain afterward.
Follow it from the beginning and you will first see an untouched project load,
then find a real object in the game, read its state, and finally turn that work
into a small feature with a menu.

All game, assembly, namespace, class, field, property, and method names in the
examples are fictional. Replace them with metadata taken from your target game.
The example code cannot work unchanged because those types do not exist in your
game.

Names such as `ModRuntime`, `ModHooks`, `ModLog`, and `Unity::` are part of the
generated project or public SDK contract, so those names remain unchanged. The
feature, wrapper, cache, and hook names used in this guide are illustrative.

A URKit mod is not a normal DLL that should be injected directly. The generated
DLL is a URKit loader plugin. Load it through one of the URKit proxies or
through `URKitInjector.dll`.

## Start with the mental model

A mod has five different jobs:

1. URKit loads the DLL and connects the lifecycle callbacks.
2. `ModRuntime` provides a place where Unity work can run on the main thread.
3. Game bindings describe the game's managed types as small C++ wrappers.
4. Feature modules decide what the mod should do.
5. The menu collects user intent; it does not operate on Unity objects itself.

The practical data flow looks like this:

```text
ImGui menu
    |  plain C++ request / atomic flag
    v
feature module  <----  thin hook notification
    |                    (only when needed)
    v
game binding
    |
    v
URKit Unity API -> Mono or IL2CPP -> game
```

This separation is not decoration. The render callback and Unity update
callback do not have to run on the same thread. Keeping a `Unity::Object` from
the menu and using it from another thread can produce intermittent crashes.
Larger working mods let the UI publish plain requests and perform managed Unity
work from `ModRuntime::update()`.

Keep these four rules in mind while reading the rest:

- Make Unity calls from `ModRuntime::update()` or another URKit main-thread
  callback.
- Find scene objects once and cache them; do not repeat a global search every
  frame.
- Clear cached Unity handles when the scene changes.
- Check `Unity::last_error()` immediately after a failed or suspicious call.

## 1. Tools and project generation

On Windows x64 you need:

- CMake 3.28 or newer;
- LLVM/Clang;
- Ninja;
- `urk-sdk.exe` from the URKit distribution;
- the target Unity game.

Generate a project from the UI or from a terminal:

```powershell
./urk-sdk.exe `
  --game-exe C:\Games\SampleGame\SampleGame.exe `
  --backend auto `
  --name FirstSteps
```

`auto` inspects the game directory and selects Mono or IL2CPP. If you already
know the backend, use `mono` or `il2cpp`. A project generated for the wrong
backend will not load. Do not try to hide that mismatch in code; regenerate the
project for the correct backend.

The project is written to:

```text
<GameDirectory>/urk-sdk-output/FirstSteps/project
```

Build it once before editing anything:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

For a build you intend to distribute:

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

CMake copies the resulting DLL to the game's `Mods` directory. After adding a
new `.cpp` or `.h` file, run the configure command again. Source discovery is
recursive under `mod/`, but CMake still needs to regenerate its source list.

### First checkpoint

Do not add a feature yet. Start the game and open `URKit_logs.log` beside the
game executable.

Confirm all four points:

- Did URKit start?
- Was the mod DLL discovered?
- Does the selected backend match the game?
- Did the mod initialize?

If no log file exists, the problem is not your feature code. The game most
likely did not import the selected proxy DLL. Do not place several proxies in
the same directory and do not rename a proxy.

## 2. Know which files you own

A generated project looks busy, but daily mod work only touches a small part of
it.

```text
project/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- sdk/                         generated URKit public API
`-- mod/
    |-- config/
    |   `-- mod_config.h         mod identity and small user settings
    |-- generated/               loader entry points; do not edit
    |-- hooks/
    |   |-- mod_hooks.cpp        hook ownership and installation
    |   `-- mod_hooks.h
    |-- lifecycle/
    |   |-- mod_runtime.cpp      your main starting point
    |   `-- mod_runtime.h
    |-- support/
    |   `-- mod_log.*            shared logging
    `-- ui/                       ImGui menu, theme, and tabs
```

File ownership matters:

| Area | What to do |
| --- | --- |
| `sdk/` | Read and use it, but do not edit it. Regeneration replaces it. |
| `mod/generated/` | Do not edit it. It owns loader ABI and callback plumbing. |
| `mod/lifecycle/mod_runtime.*` | Connect your feature lifecycle here. |
| `mod/hooks/mod_hooks.*` | Own hook installation and removal here. |
| `mod/config/mod_config.h` | Keep mod metadata and small settings here. |
| `mod/ui/theme.h` | Change the visual theme here. |
| Your own files under `mod/` | Put bindings, state, and features here. |

`menu.h`, `highlight.h`, `widgets.h`, `localization.h`, and the default tabs
provided by the generator may be overwritten by regeneration. Keep a custom
tab in a separate user-owned file. If you add a small connection to generated
`menu.h`, expect to reapply that connection after regenerating the project.

Do not put the entire mod in `mod_runtime.cpp`. After a few hundred lines it
becomes impossible to tell which code discovers objects, owns settings, or
draws the menu. A healthy project usually grows toward this shape:

```text
mod/
|-- bindings/          small C++ wrappers for the game's managed types
|-- features/          independent mod features
|-- state/             scene caches and plain snapshots
|-- hooks/             short detours
|-- ui/tabs/           ImGui drawing and request creation only
`-- lifecycle/         a thin layer connecting the pieces
```

## 3. Record the metadata you actually need

Accessing a game type usually requires four pieces of information:

| Item | Example | Meaning |
| --- | --- | --- |
| Assembly image | `GameScripts.dll` | Managed assembly containing the type |
| Namespace | `Adventure.Runtime` | C# namespace, not a folder name |
| Class | `HeroVitals` | Managed type name |
| Member | `Energy` | Field, property, or method name |

Use an empty namespace for a type in the global C# namespace:

```cpp
Unity::TypeRef type{
    "GameScripts.dll",
    "",
    "RoundDirector"
};
```

### Mono and IL2CPP

Mono games expose managed assemblies that can be inspected directly. IL2CPP
games require the native output and metadata to be considered together. If you
have the URKit IL2CPP Explorer package, inspecting types, fields, properties,
and method signatures at runtime is the most reliable starting point.

Keep a small metadata note for every type your mod uses:

```text
Object tag    : MainCharacter
Image         : GameScripts.dll
Namespace     : Adventure.Runtime
Class         : HeroVitals
Property      : Energy -> System.Single
Property      : IsReady -> System.Boolean
Method        : Refill(System.Single) -> System.Void
```

This simple note ends a great deal of guesswork. In particular, verify these
details instead of assuming them:

- A field and a property are not interchangeable.
- Static and instance members use different access paths.
- `System.Int32` and `System.Single` are different layouts.
- Overloads with the same name may also have the same parameter count.
- A C# namespace and a Unity hierarchy path are unrelated concepts.

Game updates can change any of this metadata. If lookup fails after an update,
verify the metadata again. Removing the error check only hides the real cause.

## 4. Understand the Unity object model

Most scene objects follow this shape:

```text
GameObject "Hero"
|-- Transform                  present on every GameObject
|-- HeroVitals                 game-specific MonoBehaviour
|-- CharacterController       Unity component
`-- Animator                  Unity component
```

A `GameObject` is the scene container. Components hold behaviour and state.
`Transform` is also a component; it owns hierarchy, position, rotation, and
scale.

URKit wrappers do not own Unity objects:

```cpp
Unity::GameObject actor;
```

This does not copy a Unity object. It only stores a borrowed handle to an object
owned by Unity. There are two different checks:

```cpp
if (!actor) {
  // The wrapper has no handle.
}

if (actor && !actor.alive()) {
  // A handle exists, but Unity has destroyed the object.
}
```

Scene transitions commonly produce both cases. Call `alive()` on long-lived
cached objects and reset wrappers to `{}` from scene-change callbacks.

## 5. First real probe: log a scene object

Your first goal is not a feature. It is proving that managed Unity access works
from the correct callback.

Use this as the initial shape of `mod/lifecycle/mod_runtime.cpp`:

```cpp
#include "mod_runtime.h"

#include "support/mod_log.h"
#include "sdk/runtime_api.h"
#include "sdk/runtime_bootstrap.h"
#include "sdk/unity/unity.h"

namespace {
bool g_probe_finished = false;
bool g_wait_reported = false;
}

namespace ModRuntime {

bool start(const URK_ModContext* context) {
  URK::set_context(context);

  if (!URK::initialize_backend(context)) {
    ModLog::error("Unity backend could not be initialized");
    return false;
  }

  ModLog::info("runtime is ready");
  return true;
}

void update() {
  if (g_probe_finished)
    return;

  Unity::clear_error();
  Unity::GameObject candidate =
      Unity::GameObject::FindWithTag("MainCharacter");

  if (!candidate) {
    if (!g_wait_reported) {
      const char* detail = Unity::last_error();
      ModLog::warn("main character is not ready: %s",
                   detail && detail[0] ? detail : "no matching object");
      g_wait_reported = true;
    }
    return;
  }

  const Unity::Vector3 position = candidate.transform().position();
  if (const char* detail = Unity::last_error(); detail && detail[0]) {
    ModLog::warn("position read failed: %s", detail);
    g_probe_finished = true;
    return;
  }

  ModLog::info("found %s at %.2f, %.2f, %.2f",
               candidate.name().c_str(),
               position.x, position.y, position.z);
  g_probe_finished = true;
}

void on_scene_loaded(const URK_SceneInfo* scene) {
  if (!scene || scene->size < sizeof(URK_SceneInfo))
    return;

  ModLog::info("scene loaded: %s",
               scene->name[0] ? scene->name : "<unnamed>");
  g_probe_finished = false;
  g_wait_reported = false;
}

void on_scene_changed(const URK_SceneInfo*, const URK_SceneInfo*) {
  g_probe_finished = false;
  g_wait_reported = false;
}

void on_object_destroy_requested(const URK_ObjectDestroyRequest*) {
}

void stop() {
  g_probe_finished = false;
  g_wait_reported = false;
}

} // namespace ModRuntime
```

`MainCharacter` is a placeholder tag. If the game does not define it,
`FindWithTag` can report an error. Success at this stage means seeing the scene
name, object name, and position in the log. Do not add a menu, hooks, and five
features until this probe works.

The probe repeats its search because the character can spawn after the scene
callback. It stops once the object is found. A real feature should add a search
interval and a proper cache, as shown later.

## 6. Choose the right object search

There is no universal search call. Use the most stable fact you know about the
object.

### Name or hierarchy path

```cpp
Unity::GameObject altar = Unity::GameObject::Find("World/Temple/Altar");
```

This usually finds active objects only. Names are not required to be unique. A
full hierarchy path is safer than a bare name.

If you already have the parent, prefer a relative lookup:

```cpp
Unity::Transform socket = actor.transform().Find("Rig/HandSocket");
```

### Tag

Find one active object:

```cpp
Unity::GameObject actor =
    Unity::GameObject::FindWithTag("MainCharacter");
```

Find every active object with the same tag:

```cpp
const auto pickups =
    Unity::GameObject::FindGameObjectsWithTag("Pickup");

for (const Unity::GameObject& pickup : pickups) {
  if (!pickup.alive())
    continue;

  ModLog::info("pickup: %s", pickup.name().c_str());
}
```

### Component type

For a built-in Unity type:

```cpp
Unity::Camera camera =
    Unity::Object::FindObjectOfType<Unity::Camera>();
```

For a game type before writing a wrapper:

```cpp
Unity::Object director =
    Unity::Object::FindObjectOfType<Unity::Object>(
        "GameScripts.dll",
        "Adventure.Runtime",
        "RoundDirector");
```

### Inactive objects and scene roots

Find active and inactive GameObjects in loaded scenes:

```cpp
const std::vector<Unity::GameObject> objects =
    Unity::SceneManager::FindSceneGameObjects(true);
```

Use explicit filters when you need more control:

```cpp
const auto flags = static_cast<std::uint32_t>(
    Unity::ObjectFilterFlags::IncludeInactive);

const auto objects =
    Unity::SceneManager::FindSceneGameObjectsFiltered(flags);
```

The `FindObjectsOfTypeAll` family is broader than the current scene. It can
include assets, hidden objects, inactive objects, or persistent objects. Do not
treat its result as "everything in this scene" without filtering it.

You can also walk loaded scenes and roots yourself:

```cpp
for (const Unity::Scene& scene : Unity::SceneManager::GetLoadedScenes()) {
  for (const Unity::GameObject& root : scene.GetRootGameObjects()) {
    ModLog::info("%s :: %s",
                 scene.name().c_str(),
                 root.name().c_str());
  }
}
```

### Quick choice table

| What you know | First choice |
| --- | --- |
| A unique, stable tag | `FindWithTag` |
| The full hierarchy path | `GameObject::Find` |
| A stable component type | `FindObjectOfType` |
| The parent is already known | `Transform::Find` |
| An inactive object is required | `FindSceneGameObjects(true)` |
| Assets or persistent objects are required | `FindObjectsOfTypeAll` plus filtering |

## 7. Get components and write small game bindings

Built-in components can be retrieved with typed wrappers:

```cpp
Unity::Transform transform = actor.transform();
Unity::Animator animator = actor.GetComponent<Unity::Animator>();
Unity::Rigidbody body = actor.GetComponent<Unity::Rigidbody>();
```

Search children or parents when needed:

```cpp
Unity::Renderer visual =
    actor.GetComponentInChildren<Unity::Renderer>(true);

Unity::Animator owner =
    visual.GetComponentInParent<Unity::Animator>(true);
```

For a one-off access to a game-specific component:

```cpp
Unity::Object vitals = actor.GetComponent(
    "GameScripts.dll",
    "Adventure.Runtime",
    "HeroVitals");
```

If you use a type in more than one place, stop repeating strings and write a
binding. Create `mod/bindings/hero_vitals.h`:

```cpp
#pragma once

#include "sdk/unity/unity.h"

namespace DemoBindings {

class HeroVitals final : public Unity::MonoBehaviour {
public:
  HeroVitals() = default;
  explicit HeroVitals(void* handle)
      : Unity::MonoBehaviour(handle) {
  }

  static constexpr Unity::TypeRef unity_type() {
    return {
        "GameScripts.dll",
        "Adventure.Runtime",
        "HeroVitals"
    };
  }

  float energy() const {
    return GetProperty<float>("Energy");
  }

  void set_energy(float value) const {
    SetProperty("Energy", value);
  }

  bool ready() const {
    return GetProperty<bool>("IsReady");
  }

  void refill(float amount) const {
    CallExact<void>("Refill", {"System.Single"}, amount);
  }
};

} // namespace DemoBindings
```

Typed component access now works:

```cpp
DemoBindings::HeroVitals vitals =
    actor.GetComponent<DemoBindings::HeroVitals>();
```

The C++ base must match the real managed kind:

| Managed type | C++ wrapper base |
| --- | --- |
| `MonoBehaviour` subclass | `Unity::MonoBehaviour` |
| Other `Component` subclass | `Unity::Component` or the nearest wrapper |
| `ScriptableObject` subclass | `Unity::ScriptableObject` |
| Ordinary managed reference type | `Unity::Object` |

C++ inheritance does not cast the managed object or create a C# subclass. It
only describes which wrapper operations are valid for the handle. Do not derive
an ordinary data class from `MonoBehaviour` just to gain helper methods.

## 8. Read fields, properties, and methods correctly

### Instance field

```cpp
Unity::clear_error();
const int charges = component.GetField<int>("charges");

if (const char* detail = Unity::last_error(); detail && detail[0]) {
  ModLog::warn("charges read failed: %s", detail);
}
```

Write an instance field:

```cpp
Unity::clear_error();
component.SetField("charges", 3);

if (const char* detail = Unity::last_error(); detail && detail[0]) {
  ModLog::warn("charges write failed: %s", detail);
}
```

### Static field

```cpp
const Unity::TypeRef rules_type{
    "GameScripts.dll",
    "Adventure.Runtime",
    "DifficultyRules"
};

const float scale =
    Unity::Object::GetStaticField<float>(rules_type, "GlobalScale");

Unity::Object::SetStaticField(rules_type, "GlobalScale", 1.25f);
```

### Property

```cpp
const bool active = component.GetProperty<bool>("IsActive");
component.SetProperty("IsActive", true);
```

A property getter or setter is a managed method call. `SetProperty` fails for a
read-only property. Calling `GetProperty` for a field, or `GetField` for a
property, is not a valid fallback.

### Method

For an unambiguous method:

```cpp
component.Call<void>("ResetState");
```

Prefer an exact signature when overloads are possible:

```cpp
component.CallExact<void>(
    "SetMultiplier",
    {"System.Single", "System.Boolean"},
    1.5f,
    true);
```

The parameter list does not include the return type. Use complete managed type
names:

| C++ | Managed signature |
| --- | --- |
| `bool` | `System.Boolean` |
| `int` | `System.Int32` |
| `float` | `System.Single` |
| `double` | `System.Double` |
| `std::string_view` | `System.String` |
| `Unity::Vector3` | `UnityEngine.Vector3` |

Request an appropriate wrapper when a method returns a managed object:

```cpp
Unity::GameObject target =
    component.Call<Unity::GameObject>("CurrentTarget");
```

For a managed array:

```cpp
const auto markers =
    component.CallArrayExact<Unity::Transform>("GetMarkers", {});
```

### Why a zero result is dangerous

When `GetField<int>` fails, the returned `0` looks exactly like a legitimate
value of `0`. The same ambiguity exists for `false`, `0.0f`, an empty string,
or an empty vector. Keep error handling beside the call:

```cpp
Unity::clear_error();
const float value = component.GetProperty<float>("Energy");
const char* detail = Unity::last_error();

if (detail && detail[0]) {
  // Do not use value; the read failed.
}
```

Another Unity call can replace the previous error, so inspect it immediately.

## 8.1 Inspect an unfamiliar type at runtime

If you only know the object or class name, do not guess whether a member is a
field, property, or method. `sdk/unity/unity.h` includes the inspection helpers:

```cpp
const Unity::TypeRef unknown_type{
    "GameScripts.dll",
    "Adventure.Runtime",
    "RoundDirector"
};

Unity::Inspect::DumpFields(unknown_type, [](const char* line) {
  ModLog::info("%s", line);
});

Unity::Inspect::DumpProperties(unknown_type, [](const char* line) {
  ModLog::info("%s", line);
});

Unity::Inspect::DumpMethods(unknown_type, [](const char* line) {
  ModLog::info("%s", line);
});
```

Capture this output once, select the correct member, and use a typed wrapper in
normal feature code. Enumerating every member on every update is unnecessary
reflection work.

Common inspection helpers include:

| Helper | Purpose |
| --- | --- |
| `TypeOf(object)` | Resolve an object's runtime type |
| `DescribeObject(object)` | Return type and object-reference information |
| `Fields(type/object)` | Enumerate field metadata |
| `Properties(type/object)` | Enumerate property metadata |
| `Methods(type/object)` | Enumerate method and parameter metadata |
| `ReadField` / `SetField` | Read or write a selected field |
| `ReadProperty` / `SetProperty` | Call a selected getter or setter |
| `InvokeMethod` | Invoke selected method metadata |
| `ReadArrayElement` / `SetArrayElement` | Access a supported array element |

`ValueInfo` is a tagged result. Check `readable`, `kind`, and the matching value
field before treating it as an integer, float, string, or object.

## 8.2 Everyday Unity helpers

The generated `sdk/unity/unity_components.h` is the source of truth for the
wrappers in your SDK version. If the SDK changes, inspect your generated header
instead of relying on an old example.

### One-shot input toggle

```cpp
static bool enabled = false;

if (Unity::Input::GetKeyDown(Unity::KeyCode::F8)) {
  enabled = !enabled;
  ModLog::info("feature: %s", enabled ? "on" : "off");
}
```

`GetKey` remains true while the key is held. `GetKeyDown` is usually correct for
a toggle, and `GetKeyUp` is useful when release matters. Mouse equivalents are
`GetMouseButton`, `GetMouseButtonDown`, and `GetMouseButtonUp`.

### Time

```cpp
const float frame_seconds = Unity::Time::deltaTime();
const float real_frame_seconds = Unity::Time::unscaledDeltaTime();
```

`deltaTime` is affected by the game's `timeScale`. Use `unscaledDeltaTime` for
menu animation or timers that must keep moving while the game is paused. If a
feature calls `set_timeScale`, preserve the previous value and restore it when
the feature is disabled.

### Screen and camera projection

```cpp
Unity::Camera camera =
    Unity::Object::FindObjectOfType<Unity::Camera>();

if (camera) {
  const Unity::ProjectionResult projection =
      Unity::project_world(camera, world_position, 12.0f);
}
```

Related helpers include:

- `Unity::Screen::width()`, `height()`, and `dpi()`;
- `screen_size(camera)` and `screen_center(camera)`;
- `project_world(camera, point, padding)`;
- `project_transform(camera, transform, padding)`;
- `screen_contains` and `clamp_to_screen`;
- `direction_to_screen_edge`;
- `world_visible`.

Unity screen coordinates and ImGui overlay coordinates use opposite Y
directions. The URKit projection helpers perform that conversion.

### Transform

```cpp
Unity::Transform transform = actor.transform();
const Unity::Vector3 old_position = transform.position();

transform.set_position({
    old_position.x,
    old_position.y + 1.0f,
    old_position.z
});
```

Use `position` and `rotation` for world space. Use local position, rotation, and
scale when coordinates should be relative to the parent. When changing a
parent, choose the `SetParent(parent, worldPositionStays)` argument deliberately.

### Create, clone, and destroy objects

```cpp
Unity::GameObject marker = Unity::GameObject::Create("Practice Marker");
if (!marker) {
  ModLog::error("marker creation failed: %s", Unity::last_error());
  return;
}

marker.transform().set_position({0.0f, 2.0f, 0.0f});
```

Clone an existing object:

```cpp
Unity::GameObject clone = Unity::Object::Instantiate(marker);
```

Remove it:

```cpp
Unity::Object::Destroy(marker);
marker = {};
```

`Destroy` follows Unity's normal delayed destruction path. Use
`DestroyImmediate` only when immediate semantics are genuinely required. Keep
ownership of objects created by your mod in the feature that created them and
clean them up during scene reset or shutdown.

### Built-in wrapper groups

| Area | Example wrappers |
| --- | --- |
| Core | `Object`, `GameObject`, `Component`, `MonoBehaviour`, `Transform`, `Scene` |
| Rendering | `Camera`, `Light`, `Renderer`, `Mesh`, `Material`, `Shader`, `Texture2D` |
| Physics | `Collider`, `Rigidbody`, `Rigidbody2D` |
| Animation/audio | `Animator`, `AudioSource` |
| Unity UI | `Canvas`, `Image`, `Text`, `Button`, `Toggle`, `Slider`, `ScrollRect` |
| TextMesh Pro | `TextMeshProUGUI`, `TmpInputField`, `TmpDropdown` |
| Layout | `RectTransform`, layout groups, `ContentSizeFitter` |
| Assets | `AssetBundle`, `Sprite` |

If a Unity API is missing from a built-in wrapper, create a small wrapper and
use `GetProperty` or `CallExact`, just as you would for a game type.

## 9. Build a complete feature, not a pile of calls

The next example implements a small feature from end to end. In the fictional
game, it raises the local character's energy back to a floor when the value
drops too low.

The code is split into three responsibilities:

```text
mod/
|-- bindings/
|   `-- hero_vitals.h       managed type description only
|-- features/
|   |-- energy_assist.h     plain public feature interface
|   `-- energy_assist.cpp   Unity work, cache, and status
`-- ui/tabs/
    `-- practice_panel.h    ImGui and user requests only
```

`hero_vitals.h` was created in the previous section.

### Feature interface

Create `mod/features/energy_assist.h`:

```cpp
#pragma once

#include <string>

namespace EnergyAssist {

struct ViewState {
  bool enabled = false;
  bool actor_found = false;
  float last_energy = 0.0f;
  std::string message;
};

// Safe to call from any thread. Does not touch Unity.
void ask_enabled(bool enabled);

// Call only from ModRuntime::update().
void advance();

// Call on scene changes and during shutdown.
void forget_scene();

// Plain C++ snapshot read by the menu.
ViewState view();

} // namespace EnergyAssist
```

There is no `Unity::Object` in this header. The menu never sees a managed
handle.

### Feature implementation

Create `mod/features/energy_assist.cpp`:

```cpp
#include "energy_assist.h"

#include "bindings/hero_vitals.h"
#include "support/mod_log.h"
#include "sdk/unity/unity.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <utility>

namespace EnergyAssist {
namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr float kEnergyFloor = 40.0f;
constexpr auto kSearchDelay = 750ms;

std::atomic_bool g_requested_enabled{false};
DemoBindings::HeroVitals g_vitals;
Clock::time_point g_next_search{};

std::mutex g_view_mutex;
ViewState g_view;

bool unity_failed() {
  const char* detail = Unity::last_error();
  return detail && detail[0];
}

void publish(bool enabled,
             bool actor_found,
             float energy,
             std::string message) {
  std::lock_guard lock(g_view_mutex);
  g_view.enabled = enabled;
  g_view.actor_found = actor_found;
  g_view.last_energy = energy;
  g_view.message = std::move(message);
}

bool locate_actor(Clock::time_point now) {
  if (now < g_next_search)
    return false;

  g_next_search = now + kSearchDelay;
  Unity::clear_error();

  Unity::GameObject owner =
      Unity::GameObject::FindWithTag("MainCharacter");

  if (!owner) {
    publish(true, false, 0.0f,
            unity_failed() ? Unity::last_error()
                           : "character has not spawned yet");
    return false;
  }

  g_vitals = owner.GetComponent<DemoBindings::HeroVitals>();
  if (!g_vitals) {
    publish(true, false, 0.0f,
            unity_failed() ? Unity::last_error()
                           : "HeroVitals is missing");
    return false;
  }

  ModLog::info("energy assist found its target");
  return true;
}

} // namespace

void ask_enabled(bool enabled) {
  g_requested_enabled.store(enabled, std::memory_order_release);
}

void advance() {
  const bool enabled =
      g_requested_enabled.load(std::memory_order_acquire);

  if (!enabled) {
    publish(false, g_vitals && g_vitals.alive(), 0.0f, "disabled");
    return;
  }

  if (!g_vitals || !g_vitals.alive()) {
    g_vitals = {};
    if (!locate_actor(Clock::now()))
      return;
  }

  Unity::clear_error();
  const bool ready = g_vitals.ready();
  if (unity_failed()) {
    publish(true, false, 0.0f, Unity::last_error());
    g_vitals = {};
    return;
  }

  if (!ready) {
    publish(true, true, 0.0f, "character is not ready");
    return;
  }

  Unity::clear_error();
  float energy = g_vitals.energy();
  if (unity_failed()) {
    publish(true, false, 0.0f, Unity::last_error());
    g_vitals = {};
    return;
  }

  if (energy < kEnergyFloor) {
    Unity::clear_error();
    g_vitals.set_energy(kEnergyFloor);
    if (unity_failed()) {
      publish(true, true, energy, Unity::last_error());
      return;
    }
    energy = kEnergyFloor;
  }

  publish(true, true, energy, "running");
}

void forget_scene() {
  g_vitals = {};
  g_next_search = {};
  publish(g_requested_enabled.load(std::memory_order_acquire),
          false,
          0.0f,
          "waiting for scene");
}

ViewState view() {
  std::lock_guard lock(g_view_mutex);
  return g_view;
}

} // namespace EnergyAssist
```

Notice what this implementation does:

- A failed tag search is retried every 750 ms, not every frame.
- The Unity wrapper remains on the main-thread side of the module.
- The menu receives a copy of `ViewState`.
- A failed managed call clears the cached handle so discovery can retry.
- The setting, cache, and observable state have one owner.

### Connect it to the lifecycle

Include the feature in `mod/lifecycle/mod_runtime.cpp` and add the calls to the
existing function bodies:

```cpp
#include "features/energy_assist.h"

void ModRuntime::update() {
  EnergyAssist::advance();
}

void ModRuntime::on_scene_loaded(const URK_SceneInfo*) {
  EnergyAssist::forget_scene();
}

void ModRuntime::on_scene_changed(
    const URK_SceneInfo*,
    const URK_SceneInfo*) {
  EnergyAssist::forget_scene();
}

void ModRuntime::stop() {
  EnergyAssist::forget_scene();
}
```

Those functions may already be defined inside `namespace ModRuntime` in the
generated file. Add the calls to the existing bodies; do not define a second
`update()`. Keep the context and backend initialization in `start()`.

### Add a menu panel

Create `mod/ui/tabs/practice_panel.h`:

```cpp
#pragma once

#include "features/energy_assist.h"

#include <imgui.h>

namespace PracticePanel {

inline void draw() {
  EnergyAssist::ViewState state = EnergyAssist::view();

  bool enabled = state.enabled;
  if (ImGui::Checkbox("Energy assist", &enabled))
    EnergyAssist::ask_enabled(enabled);

  ImGui::Separator();
  ImGui::Text("Target: %s", state.actor_found ? "found" : "waiting");
  ImGui::Text("Energy: %.1f", state.last_energy);
  ImGui::TextWrapped("State: %s", state.message.c_str());
}

} // namespace PracticePanel
```

To make the panel visible, include it from generated `mod/ui/menu.h` and call
`PracticePanel::draw()` in the desired content area. Regeneration may replace
this small connection. The panel itself remains safe because it lives in a
separate user-owned file.

For a first test, draw it below the current tab content:

```cpp
#include "tabs/practice_panel.h"

// In the content area of ModUI::render_menu():
active_entry.render();
PracticePanel::draw();
```

You can later add a dedicated value to the `Tab` enum and a tab entry. First
prove that the feature works; polishing navigation is a separate task.

## 10. Respect the menu/main-thread boundary

The following code is short, but it is not reliable:

```cpp
// Bad: managed Unity call from the render callback.
if (ImGui::Button("Refill"))
  g_vitals.Call<void>("Refill");
```

The render callback can run on another thread, and `g_vitals` can become stale
during a scene transition. The safe pattern is to enqueue a request from the UI
and process it in `update()`.

An `std::atomic_bool` is enough for a single toggle. Use a bounded queue for
commands with parameters:

```cpp
struct Command {
  enum class Kind { Refill, SetScale } kind;
  float value = 0.0f;
};

std::mutex g_command_mutex;
std::vector<Command> g_commands;

void enqueue(Command command) {
  std::lock_guard lock(g_command_mutex);
  constexpr std::size_t kMaximumCommands = 32;
  if (g_commands.size() >= kMaximumCommands) {
    ModLog::warn("feature command queue is full");
    return;
  }
  g_commands.push_back(command);
}
```

At the start of `update()`, move the queue into a local vector while holding the
lock briefly:

```cpp
std::vector<Command> take_commands() {
  std::lock_guard lock(g_command_mutex);
  std::vector<Command> result;
  result.swap(g_commands);
  return result;
}
```

Perform Unity calls after releasing the lock. Do not keep a mutex locked across
a managed call. When a queue is full, report the failure or define an explicit
"latest value wins" policy. Do not silently discard work.

### Snapshot rule

Values that can cross to the render side include:

- `bool`, integer, and floating-point values;
- copied `std::string` values;
- value-only structs such as `Unity::Vector2` and `Vector3`;
- immutable `shared_ptr<const std::vector<...>>` snapshots made from them.

Do not expose these to the render side:

- `Unity::GameObject`, `Transform`, `Object`, or component wrappers;
- raw managed object pointers;
- temporary managed string buffers;
- an unlocked reference to a container modified on the main thread.

## 11. Design caches around the work they avoid

Global searches, reflection, and managed property calls all have a cost. A
small mod may hide that cost, while an overlay tracking hundreds of objects
will not.

A useful cache can run different jobs at different rates:

| Work | Example interval |
| --- | --- |
| Discover new objects | 500-1000 ms |
| Validate cached handles | 200-500 ms |
| Capture fast position state | 30-100 ms |
| Refresh slower names or state | 500-2000 ms |

These numbers are not requirements. Measure how often your data changes and
choose intervals accordingly.

Keep runtime state separate from published state:

```cpp
struct RuntimeEntry {
  int instance_id = 0;
  Unity::GameObject owner;
  Unity::Transform transform;
  DemoBindings::HeroVitals vitals;
  unsigned missed_scans = 0;
};

struct ActorSnapshot {
  int instance_id = 0;
  std::string label;
  Unity::Vector3 position{};
  float energy = 0.0f;
  bool valid = false;
};
```

`RuntimeEntry` stays on the main thread. `ActorSnapshot` can be copied to the
render thread.

### Mark dirty instead of scanning inside a hook

A hook that notices a possible new object should not perform a full scan. Set
an atomic dirty flag instead:

```cpp
std::atomic_bool g_rescan_requested{true};

void notice_possible_change() noexcept {
  g_rescan_requested.store(true, std::memory_order_release);
}
```

The main-thread update reads the flag and scans at an appropriate point. The
hook stays short, and reflection or allocation remains in one owner.

### Log spam is also a performance bug

If a property fails every frame, do not write hundreds of identical log lines
per second. Keep the failure visible, but throttle repeated messages:

```cpp
if (now >= next_error_log) {
  ModLog::warn("actor scan failed: %s", Unity::last_error());
  next_error_log = now + std::chrono::seconds(5);
}
```

The latest error can remain visible in the UI snapshot between log messages.
Throttling repeated output is not swallowing an error; it prevents the same
known failure from consuming disk and frame time.

## 12. Highlights and simple world overlays

Generated projects include `mod/ui/highlight.h` for world points, Unity
objects, and screen rectangles. Do not recreate a highlight every frame. Keep
its ID, update it only when needed, and remove it when the target disappears.

```cpp
#include "ui/highlight.h"

namespace MarkerFeature {
namespace {
ModUI::Highlight::HighlightId g_marker = 0;
}

void show(Unity::Transform anchor) {
  if (!anchor || !anchor.alive())
    return;

  ModUI::Highlight::Style style{};
  style.draw_box = true;
  style.draw_label = true;
  style.offscreen_indicator = true;

  if (g_marker == 0) {
    g_marker = ModUI::Highlight::enqueue_add(
        anchor,
        "Objective",
        style);
  } else {
    ModUI::Highlight::enqueue_mark_dirty(g_marker);
  }
}

void clear() {
  if (g_marker != 0)
    ModUI::Highlight::enqueue_remove(g_marker);
  g_marker = 0;
}
} // namespace MarkerFeature
```

The `enqueue_*` family is the safe choice from the main thread. Code already
inside the render callback can use the manager's direct API.

Clear IDs when scenes change. During shutdown, verify that every highlight
owned by your feature is removed. Do not call setters every frame when the
label, style, and position have not changed.

A plain world point does not need a `Transform`:

```cpp
const auto id = ModUI::Highlight::enqueue_add_world_point(
    Unity::Vector3{12.0f, 2.0f, -5.0f},
    "Checkpoint",
    style);
```

Capture transform positions into main-thread snapshots. Do not call
`Transform::position()` from the render thread.

## 13. Use coroutines to spread work across frames

The generated lifecycle ticks frame-based coroutines through `ModAsync`. You do
not need a worker thread just to wait a few frames on the Unity main thread.

```cpp
#include "sdk/mod_async.h"
#include "sdk/coroutines.h"
#include "support/mod_log.h"

#include <chrono>

URK::coroutines::Task delayed_notice() {
  co_await URK::coroutines::next_frame();
  co_await URK::coroutines::wait_for(std::chrono::milliseconds(500));
  ModLog::info("half a second passed on the mod flow");
}

void begin_sequence() {
  ModAsync::spawn(delayed_notice());
}
```

Coroutine work advances from the `ModRuntime::update()` flow. The generated
lifecycle cancels tasks during shutdown. Even so, if a coroutine captures a
borrowed Unity handle, call `alive()` after resuming.

A coroutine does not make blocking I/O or heavy CPU work non-blocking. Run
those jobs on an appropriate worker and move only plain results back to the
main thread.

## 14. Add hooks only when normal calls are not enough

Most first features do not need a hook. Start with:

- `ModRuntime::update()` polling;
- field and property access;
- managed method calls;
- scene callbacks;
- object-destroy request callbacks.

A hook becomes useful when:

- you need the exact moment a managed method runs;
- a parameter or return value must be changed;
- polling misses a short-lived event.

Four things must be correct before installing a hook:

1. The runtime backend.
2. Static versus instance method semantics.
3. Every parameter and the return type.
4. The native ABI.

A bad hook does not always return a tidy error. It can crash the process
immediately.

### IL2CPP managed method hook

Assume this fictional managed method:

```text
GameScripts.dll
Adventure.Runtime.CrateSensor
System.Void Tick(System.Single)
```

Create `mod/hooks/crate_sensor_hook.h`:

```cpp
#pragma once

#include "support/mod_log.h"
#include "sdk/hook_api.h"
#include "sdk/il2cpp/il2cpp_helpers.h"
#include "sdk/il2cpp/il2cpp_runtime.h"

#include <atomic>

namespace CrateSensorHook {

using TickFn =
    void(__fastcall*)(void* self, float delta, void* method_info);

inline TickFn g_next = nullptr;
inline bool g_attached = false;
inline std::atomic_bool g_observed{false};

inline void diagnostic(const char* text) {
  ModLog::warn("crate hook: %s", text ? text : "");
}

inline void __fastcall detour(
    void* self,
    float delta,
    void* method_info) {
  g_observed.store(true, std::memory_order_release);

  if (g_next)
    g_next(self, delta, method_info);
}

inline bool attach(const URK_ModContext* context) {
  if (g_attached)
    return true;

  URK::set_context(context);
  if (!URK::il2cpp::init(context) || !URK::hooks::available()) {
    ModLog::error("required IL2CPP hook services are unavailable");
    return false;
  }

  g_attached = Il2CppHook::attach(
      "GameScripts.dll",
      "Adventure.Runtime",
      "CrateSensor",
      "Tick",
      {"System.Single"},
      &g_next,
      &detour,
      &diagnostic);

  if (!g_attached)
    g_next = nullptr;

  return g_attached;
}

inline bool detach() {
  if (!g_attached)
    return true;

  const bool removed = URK::hooks::detach_ex(
      reinterpret_cast<void**>(&g_next),
      reinterpret_cast<void*>(&detour));

  if (!removed) {
    ModLog::error("crate sensor hook could not be detached");
    return false;
  }

  g_next = nullptr;
  g_attached = false;
  return true;
}

} // namespace CrateSensorHook
```

For an IL2CPP instance method, `self` is the first argument. Generated native
methods commonly carry a trailing `MethodInfo*`, represented here as
`void* method_info`. Value-type instance methods, struct returns, and some
Unity/IL2CPP versions can have different ABI details. Verify the native
signature instead of copying this typedef blindly.

The detour only sets a flag and calls the original. It performs no scan,
allocation, or ImGui work. `ModRuntime::update()` can read the flag and mark a
feature cache dirty.

### Mono difference

A compiled Mono method does not use IL2CPP's trailing `method_info` argument:

```cpp
using TickFn = void(*)(void* self, float delta);
```

The Mono installation flow is:

1. Resolve the exact method with `URK::mono::helpers::require_method_exact`.
2. Get the native target with `URK::mono::compile_method`.
3. Attach it with `URK::hooks::attach_ex`.
4. Remove it with the same original/detour pair through `detach_ex`.

Do not use IL2CPP helpers in a Mono project or a Mono ABI in an IL2CPP project.

### Centralize hook ownership

Let `mod/hooks/mod_hooks.cpp` own every hook:

```cpp
#include "mod_hooks.h"
#include "crate_sensor_hook.h"
#include "support/mod_log.h"

namespace ModHooks {

bool install(const URK_ModContext* context) {
  if (!CrateSensorHook::attach(context))
    return false;

  return true;
}

void uninstall() {
  if (!CrateSensorHook::detach())
    ModLog::error("one or more hooks remain attached");
}

} // namespace ModHooks
```

`URK::hooks::HookSet` can own several raw targets. If a later required hook
fails, detach the earlier ones. Make `install()` idempotent. If detach fails,
do not clear state and claim success; unloading the DLL may not be safe.

## 15. Persist settings deliberately

Small constants and runtime settings can live under `mod_config.h`. Resolve a
persistent settings path relative to the mod DLL, not the process current
working directory.

A robust configuration layer should:

- treat a missing file as a valid first-run state;
- validate parsed types and numeric ranges;
- report malformed lines instead of silently inventing values;
- write to a temporary file first;
- flush successfully before atomically replacing the real file;
- retain a useful last error for the log or UI.

Do not write the configuration every frame. Save when a checkbox or slider
actually changes, or during orderly shutdown. If dragging a slider causes too
many writes, save at edit completion or after a short debounce.

If the generated lifecycle already loads and saves a configuration store,
extend that store instead of adding a second owner for the same settings.

## 16. Strings, arrays, and managed lifetime

Passing `std::string_view` through a high-level wrapper creates the managed
string required for that call:

```cpp
component.CallExact<void>(
    "SetLabel",
    {"System.String"},
    std::string_view{"Practice"});
```

Copy managed string results into `std::string`:

```cpp
const std::string label = component.GetProperty<std::string>("Label");
```

Do not keep a raw Mono or IL2CPP string-buffer pointer. Copy the UTF-8 value and
use the matching helper to free runtime-owned temporary storage when required.

Wrapper handles are borrowed. If a managed object truly must outlive scene
ownership and ordinary managed references, use the backend GC-handle API and
free the handle during shutdown. Do not add GC handles to a first feature when
normal scene ownership is sufficient.

Never guess the layout of a managed value type. Use SDK definitions for
`Vector3`, `Quaternion`, `Color`, and other built-in values. For a custom game
struct, verify size, alignment, and field layout before writing a matching C++
type.

## 17. Debug one layer at a time

Build a feature in this order so every failure has a clear owner:

1. Does the untouched project compile?
2. Does the loader discover the DLL?
3. Does the `start()` log appear?
4. Does a scene callback arrive?
5. Can one GameObject be found?
6. Can one component be found?
7. Can one field or property be read?
8. Does one write or method call work?
9. Is the feature connected to `update()`?
10. Can the menu publish a plain request?
11. Only then, is a hook actually necessary?

Adding five layers at once makes a crash much harder to localize.

### The mod does not load

- Does `URKit_logs.log` exist?
- Is the correct proxy filename installed?
- Is exactly one proxy present?
- Is the mod DLL under `Mods`?
- Does the project backend match the game?
- Are Debug and Release outputs using the generated build configuration?

### An object is not found

- Is the name and capitalization exact?
- Has the object spawned yet?
- Is it inactive?
- Are you in the correct scene?
- Is the tag defined by the game?
- Would a relative hierarchy or component-type search be more stable?
- What does `Unity::last_error()` report?

### A component is not found

- Is it on the same GameObject, a child, or a parent?
- Are image, namespace, and class exact?
- Is `unity_type()` correct?
- Does the wrapper use the correct base class?
- Did a game update move or rename the type?

### A field always reads as zero

- Could zero be the real value?
- Did you read `Unity::last_error()` immediately?
- Is the member a property rather than a field?
- Is it static rather than instance state?
- Are the managed type and enum layout correct?

### A method call fails

- Did you select the exact overload?
- Are complete managed parameter names correct?
- Did you accidentally include the return type in the parameter list?
- Is the method static or instance?
- Did the managed method throw an exception?

### The menu works but the button does nothing

- Does the UI write to the request queue or atomic state?
- Is the feature called from `ModRuntime::update()`?
- Did the loader register the update callback?
- Does the feature rediscover objects after a scene reset?
- Is displayed state coming from a main-thread snapshot?

### A hook fails or crashes the game

- Is the backend correct?
- Did you resolve the exact method and overload?
- Is the native return and parameter ABI verified?
- Does the IL2CPP signature include trailing method information when required?
- Is the original pointer called only after successful attachment?
- Can the hook be installed twice?
- Is shutdown detaching in the correct order?

Removing an error check, adding an empty catch, or using an unvalidated address
does not solve the problem. It only makes the failure less observable and the
eventual crash harder to diagnose.

## 18. Release checklist

Test the release build separately from the debug build.

- Does a clean configure and build succeed?
- Was the mod tested on first launch, scene transition, and game shutdown?
- If runtime unload is supported, do all hooks detach?
- Are highlights, commands, and caches cleared during shutdown?
- Can a scene-owned handle leak into the next scene?
- Does closing the menu return input and cursor ownership to the game?
- Are global searches or reflection accidentally running every frame?
- Are queues bounded?
- Are failures visible without producing log spam?
- Is there a written list of every image, class, field, property, and method?
- Was exact metadata rechecked against the current game version?
- Does the release package contain only the files the mod needs?

## The working order for a new game

The quickest path is not writing the most code at once. Keep this order:

1. Build the untouched generated project.
2. See the mod in the loader log.
3. Log the scene name.
4. Find one GameObject.
5. Find one component.
6. Read one value and add error handling.
7. Write a small wrapper for that type.
8. Move the feature into its own `.h/.cpp` module.
9. Add caching and scene reset.
10. Publish plain requests from the menu and process them in `update()`.
11. Return status to the menu through a snapshot.
12. Measure before choosing polling intervals.
13. Add a hook only if normal calls are insufficient.
14. Test scene transitions and shutdown deliberately.

That is the core of a URKit mod: find the right object on the right thread,
access the correct managed member with the correct type, and own that work
cleanly for the entire lifecycle. Once every link in that chain is verified,
the rest of the SDK becomes a toolbox you can open when needed rather than a
wall of API names to memorize.
