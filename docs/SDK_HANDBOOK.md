# URKit SDK Handbook

This handbook is the API usage reference for generated URKit mods. It covers
runtime ownership, Unity object access, managed members, threading, hooks,
unload safety, and the generated native overlay. Code examples use fictional
game metadata and compile against the generated SDK surface.

Game, assembly, namespace, class, field, property, and method names in the
examples are fictional. Replace them with metadata from the target game.

`ModRuntime`, `ModHooks`, `ModLog`, and `Unity::` belong to the generated project
or public SDK. Feature, wrapper, cache, and hook names are illustrative.

A generated mod DLL is a URKit loader plugin. Load it through a URKit proxy or
`URKitInjector.dll`; do not inject it directly.

## Core API workflow

A feature that reads or modifies Unity state follows this API flow:

```text
find GameObject
    -> get Component from that GameObject
    -> read one field/property
    -> cache the result on the Unity main thread
    -> add feature logic
    -> publish plain data to the menu/highlight renderer
```

The primary handle types and their operations are:

| Handle | Meaning | Primary operations |
| --- | --- | --- |
| `Unity::GameObject` | A scene container | `object.GetComponent<T>()` |
| `Unity::Component` | Behaviour/state attached to a GameObject | `component.gameObject()` |
| `Unity::Transform` | Hierarchy and world/local pose | `transform.position()` |
| `Unity::Object` | Generic managed Unity object handle | field/property/method helpers |
| `HighlightId` | A native overlay entry, not a Unity object | `enqueue_set_world_point()` |

The following example resolves a GameObject and retrieves an attached built-in
component:

```cpp
Unity::clear_error();

Unity::GameObject player =
    Unity::GameObject::FindWithTag("MainCharacter");
if (!player) {
  const char* detail = Unity::last_error();
  ModLog::warn("player lookup failed: %s",
               detail && detail[0] ? detail : "not spawned yet");
  return;
}

Unity::Animator animator = player.GetComponent<Unity::Animator>();
if (!animator) {
  const char* detail = Unity::last_error();
  ModLog::warn("Animator is not attached to %s: %s",
               player.name().c_str(),
               detail && detail[0] ? detail : "component not found");
  return;
}

// `animator` is the Animator attached to `player`.
// `animator.gameObject()` returns the same owning GameObject.
```

`GetComponent` never performs a global scene search. It searches the component
list attached to that specific GameObject. If the component is on a child or a
parent, use the corresponding `InChildren` or `InParent` call described in
[Get components without guessing](#7-get-components-without-guessing).

For highlights, remember this separate pipeline:

```text
Unity main-thread feature code
    -> enqueue a world-position snapshot
    -> generated highlight command queue
    -> ImGui background draw list
    -> DirectX 11 / DirectX 12 / OpenGL render backend
    -> game back buffer
```

A highlight is not a Unity `Renderer`, material, shader, outline component, or
extra GameObject. On DirectX games it is drawn by the generated native render
hook directly into the game's swap-chain back buffer. The full rendering and
threading contract is documented in
[Highlights and the native render pipeline](#12-highlights-and-the-native-render-pipeline).

## Runtime model

A mod has five different jobs:

1. URKit loads the DLL and connects the lifecycle callbacks.
2. `ModRuntime` provides a place where Unity work can run on the main thread.
3. Game bindings describe the game's managed types as small C++ wrappers.
4. Feature modules decide what the mod should do.
5. The menu collects user intent; it does not operate on Unity objects itself.

The data flow is:

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

The render callback and Unity update callback may run on different threads.
Passing a `Unity::Object` through UI state can therefore produce intermittent
crashes. Publish plain requests from the UI and perform managed Unity work in
`ModRuntime::update()`.

Core rules:

- Make Unity calls from `ModRuntime::update()` or another URKit main-thread
  callback.
- Find scene objects once and cache them; do not repeat a global search every
  frame.
- Clear cached Unity handles when the scene changes.
- Check `Unity::last_error()` immediately after a failed or suspicious call.

## 1. Tools and project generation

On Windows x64 you need:

- CMake 3.28 or newer;
- Ninja;
- either LLVM/Clang or the MSVC toolchain from Visual Studio 2022 Build Tools
  or newer;
- `urk-sdk.exe` from the URKit distribution;
- the target Unity game.

Generate a project from the UI or from a terminal:

```powershell
./urk-sdk.exe `
  --game-exe C:\Games\SampleGame\SampleGame.exe `
  --backend auto `
  --name FirstSteps
```

`auto` inspects the game directory and selects Mono or IL2CPP. Use `mono` or
`il2cpp` when the backend is known. A project generated for the wrong backend
will not load; regenerate it with the correct backend.

The project is written to:

```text
<GameDirectory>/urk-sdk-output/FirstSteps/project
```

Build it once before editing anything:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

Or use MSVC from an x64 Visual Studio Developer PowerShell, where `cl.exe` is
available:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
```

For a build you intend to distribute:

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

The equivalent MSVC release preset is `msvc-release`.

CMake copies the resulting DLL to the game's `Mods` directory. After adding a
new `.cpp` or `.h` file, run the configure command again. Source discovery is
recursive under `mod/`, but CMake still needs to regenerate its source list.

### Verify the generated project

Before adding features, start the game and open `URKit_logs.log` beside the game
executable. Confirm that:

- Did URKit start?
- Was the mod DLL discovered?
- Does the selected backend match the game?
- Did the mod initialize?

If no log file exists, verify that the game imports the selected proxy DLL.
Install one proxy only and keep its original filename.

## 2. Know which files you own

Most mod work is confined to a small part of the generated project:

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

Keep `mod_runtime.cpp` focused on lifecycle coordination. Move object discovery,
settings, and UI code into dedicated modules as the project grows:

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

Record the metadata for every type the mod uses:

```text
Object tag    : MainCharacter
Image         : GameScripts.dll
Namespace     : Adventure.Runtime
Class         : HeroVitals
Property      : Energy -> System.Single
Property      : IsReady -> System.Boolean
Method        : Refill(System.Single) -> System.Void
```

Verify these details explicitly:

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

## 5. Verify Unity access

Start by confirming that managed Unity access works from the correct callback.

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
`FindWithTag` reports an error. Continue once the scene name, object name, and
position appear in the log.

The probe repeats its search because the character can spawn after the scene
callback. Production code should add a search interval and cache, as shown
later.

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

## 7. GameObject and Component APIs

GameObject, Component, and Transform handles are related as follows:

```text
GameObject "Hero"                    Unity::GameObject hero
|-- Transform                        hero.transform()
|-- Animator                         hero.GetComponent<Unity::Animator>()
|-- Rigidbody                        hero.GetComponent<Unity::Rigidbody>()
`-- HeroVitals                       hero.GetComponent<HeroVitals>()
```

`GameObject::GetComponent` looks only on the receiving GameObject. It does not
search the scene and it does not automatically inspect children or parents.
The return value is another non-owning wrapper. A null wrapper means no matching
component was returned.

### Attached built-in component

Built-in components already have `unity_type()` metadata in the generated SDK,
so use the typed form:

```cpp
Unity::GameObject actor =
    Unity::GameObject::FindWithTag("MainCharacter");
if (!actor)
  return;

Unity::Transform transform = actor.transform();
Unity::Animator animator = actor.GetComponent<Unity::Animator>();
Unity::Rigidbody body = actor.GetComponent<Unity::Rigidbody>();

if (!animator) {
  const char* detail = Unity::last_error();
  ModLog::warn("Animator missing: %s",
               detail && detail[0] ? detail : "not attached to actor");
  return;
}
```

Every GameObject has a Transform, so `actor.transform()` is the clearest form
for it. `actor.GetComponent<Unity::Transform>()` is also valid, but adds no
value in normal code.

### Attached game-specific component

When no typed wrapper exists, supply the managed type identity:

```cpp
Unity::clear_error();
Unity::Object vitals = actor.GetComponent(
    "GameScripts.dll",       // managed assembly image
    "Adventure.Runtime",    // C# namespace; use "" for global namespace
    "HeroVitals");          // exact managed class name

if (!vitals) {
  const char* detail = Unity::last_error();
  ModLog::warn("HeroVitals lookup failed: %s",
               detail && detail[0] ? detail : "component not attached");
  return;
}
```

These three strings describe a managed type. They are not a file-system path,
a Unity hierarchy path, or a GameObject name. The call resolves that type and
then asks the target GameObject for the attached component of that type.

### Component on a child or parent

Use child/parent search only when the component is not attached to the current
GameObject:

```cpp
// `true` includes inactive child GameObjects.
Unity::Renderer visual =
    actor.GetComponentInChildren<Unity::Renderer>(true);

if (!visual) {
  ModLog::warn("no Renderer exists below the actor hierarchy");
  return;
}

// Start at the Renderer's GameObject and walk toward the root.
Unity::Animator owner =
    visual.GetComponentInParent<Unity::Animator>(true);
```

The searches include the starting GameObject as Unity does; “children” and
“parents” do not mean “strict descendants/ancestors only.” The Boolean controls
whether inactive GameObjects participate. It does not turn a same-object lookup
into a global scene search.

For a game-specific type, the untyped variants use the same metadata triplet:

```cpp
Unity::Object weapon = actor.GetComponentInChildren(
    "GameScripts.dll", "Adventure.Items", "EquippedWeapon", true);

Unity::Object controller = actor.GetComponentInParent(
    "GameScripts.dll", "Adventure.Runtime", "PlayerController", true);
```

### One component versus every component

`GetComponent` returns the first matching component. Use the plural APIs when a
GameObject or hierarchy can contain several matches:

```cpp
const std::vector<Unity::Renderer> local_renderers =
    actor.GetComponents<Unity::Renderer>();

const std::vector<Unity::Renderer> all_renderers =
    actor.GetComponentsInChildren<Unity::Renderer>(true);

for (const Unity::Renderer& renderer : all_renderers) {
  if (!renderer || !renderer.alive())
    continue;
  ModLog::info("renderer owner: %s",
               renderer.gameObject().name().c_str());
}
```

Do not call `GetComponentsInChildren` every frame just to rediscover an
unchanged hierarchy. Cache the wrappers, validate long-lived entries with
`alive()`, and rebuild the cache after scene or hierarchy changes.

### GameObject and Component conversion

The relationship works in both directions:

```cpp
Unity::GameObject owner = animator.gameObject();
Unity::Transform owner_transform = animator.transform();
Unity::Rigidbody sibling = animator.GetComponent<Unity::Rigidbody>();
```

Calling `GetComponent` on a `Component` delegates to its owning GameObject. The
last line therefore asks for a sibling `Rigidbody`, not a component nested
inside the Animator.

### Component lookup reference

| Situation | Call |
| --- | --- |
| Built-in component on this object | `object.GetComponent<Unity::Animator>()` |
| Custom component on this object, no wrapper yet | `object.GetComponent(image, namespace, class)` |
| One component somewhere below | `object.GetComponentInChildren<T>(includeInactive)` |
| One component somewhere above | `object.GetComponentInParent<T>(includeInactive)` |
| Every local match | `object.GetComponents<T>()` |
| Every match below or above | `GetComponentsInChildren<T>()` / `GetComponentsInParent<T>()` |
| Owning GameObject from a component | `component.gameObject()` |
| Sibling component from a component | `component.GetComponent<T>()` |

### Null component result

A null result usually means one of these facts is wrong:

1. The target GameObject is not the object you think it is.
2. The component lives on a child or parent instead of the same object.
3. The assembly image, namespace, or class name is wrong.
4. The object/component has not spawned or is inactive.
5. A cached GameObject belongs to a previous scene.

Keep the error adjacent to the lookup so another Unity call cannot overwrite
it. After defining the typed binding in the next subsection, the check looks
like this:

```cpp
Unity::clear_error();
DemoBindings::HeroVitals vitals =
    actor.GetComponent<DemoBindings::HeroVitals>();
const char* detail = Unity::last_error();

if (!vitals) {
  ModLog::warn("HeroVitals missing on %s: %s",
               actor.name().c_str(),
               detail && detail[0] ? detail : "no matching component");
  return;
}
```

An empty result is a lookup failure until the exact object identity, hierarchy,
and managed type metadata have been verified. It must not be converted into a
made-up default value.

### Write a typed game binding

If you use a game-specific type in more than one place, stop repeating strings
and write a binding. Create `mod/bindings/hero_vitals.h`:

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

## 9. Organize a feature

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

For a minimal menu integration, draw the panel below the current tab content:

```cpp
#include "tabs/practice_panel.h"

// In the content area of ModUI::render_menu():
active_entry.render();
PracticePanel::draw();
```

You can later add a dedicated value to the `Tab` enum and a tab entry. First
prove that the feature works; polishing navigation is a separate task.

## 10. Menu and main-thread boundary

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

## 12. Highlights and the native render pipeline

Generated projects include `mod/ui/highlight.h`. It can draw a box, fill,
label, and off-screen direction indicator for:

- a world-position snapshot;
- a `Transform`, `GameObject`, or `Component` target;
- an already projected screen rectangle.

The name “highlight” does not mean a Unity outline effect. The module does not
change a target's `Renderer`, materials, shaders, render layer, or GameObject.
It builds ImGui draw commands and the generated native render hook submits
those commands to the game's graphics back end.

### Where a highlight is actually rendered

On a DirectX game the generated project installs DXGI presentation hooks. The
end-to-end path is:

```text
ModRuntime::update()                         Unity main thread
    |
    | enqueue_add_world_point / enqueue_set_world_point
    v
Highlight pending-command queue             mutex-protected native data
    |
    | drained after ImGui::NewFrame()
    v
Highlight::Manager::render()                 render/Present thread
    |
    | ImGui::GetBackgroundDrawList()
    v
ImGui draw data
    |
    +--> D3D11: ImGui_ImplDX11_RenderDrawData
    |
    `--> D3D12: ImGui_ImplDX12_RenderDrawData
             inside a generated command list
    v
game swap-chain back buffer
    |
    v
original Present / Present1
```

This means the overlay is native DirectX output composited into the same back
buffer the game presents. It is not rendered by a Unity Canvas and it does not
exist in the Unity scene hierarchy. The default draw list is ImGui's background
draw list: highlights appear over the 3D scene and behind the mod's ImGui menu.

Feature modules do not need to contain DirectX code. The generated
`mod/hooks/render_imgui_hook.cpp` owns device discovery, swap-chain hooks,
ImGui frame creation, render-target handling, resize handling, and shutdown.
Feature code owns highlight IDs and publishes plain target data.

### What happens on DirectX 11

For D3D11, the generated hook:

1. hooks `IDXGISwapChain::Present` and/or `Present1` plus `ResizeBuffers`;
2. obtains the `ID3D11Device` and immediate `ID3D11DeviceContext` from the game
   swap chain;
3. creates an `ID3D11RenderTargetView` for the current back buffer;
4. starts an ImGui Win32/DX11 frame;
5. asks the highlight manager and menu to emit ImGui draw data;
6. binds the back-buffer render target and calls
   `ImGui_ImplDX11_RenderDrawData`;
7. calls the game's original `Present`/`Present1`.

`ResizeBuffers` invalidates the render target and ImGui device objects before
the resize, then rebuilds them after a successful resize. A mod feature must
not cache DirectX back-buffer or render-target pointers itself.

### What happens on DirectX 12

D3D12 requires explicit command submission. The generated hook therefore also
captures the game's direct `ID3D12CommandQueue` through
`ExecuteCommandLists`. For each presented back buffer it:

1. gets the current back-buffer index from `IDXGISwapChain3`;
2. waits for that frame context's fence when the GPU still owns it;
3. resets the frame command allocator and generated graphics command list;
4. starts an ImGui Win32/DX12 frame and builds highlight/menu draw data;
5. transitions the back buffer from `PRESENT` to `RENDER_TARGET`;
6. binds the generated RTV and shader-visible SRV descriptor heap;
7. records `ImGui_ImplDX12_RenderDrawData` into the command list;
8. transitions the back buffer back to `PRESENT`;
9. executes the command list on the captured direct queue and signals a fence;
10. returns to the game's original `Present`/`Present1`.

The D3D12 overlay cannot render until both a compatible DXGI presentation hook
and the direct command queue are available. The log distinguishes presentation
hook failure, command-queue hook failure, device-object failure, and per-frame
fence/command-list failure.

### Back-end selection and support

`URK::graphics_device_type()` reports the Unity graphics device when the
runtime exposes it. The generated render hook currently recognizes:

| Reported device | Generated overlay path |
| --- | --- |
| Direct3D 11 | DXGI `Present`/`Present1` + ImGui DX11 |
| Direct3D 12 | DXGI + direct command queue + ImGui DX12 |
| OpenGL 2 / OpenGL Core | `wglSwapBuffers` + ImGui OpenGL3 |
| Unknown | Probe native DXGI and OpenGL presentation paths |
| Vulkan or another unsupported device | No generated highlight/menu render hook |

Unknown does not mean “assume DX11.” It tells the generated project to probe
the native presentation APIs and initialize only after a compatible context is
observed. Check `URKit_logs.log` for the selected path; do not infer it from the
game's launcher option alone.

### Main-thread world snapshots

The most predictable pattern is to read Unity state during
`ModRuntime::update()`, then enqueue only a copied `Unity::Vector3`. This keeps
scene-object ownership in feature code and makes the render-side input plain
data.

```cpp
// mod/features/objective_marker.h
#pragma once

#include "sdk/unity/unity.h"

namespace ObjectiveMarker {

void update(Unity::Transform objective);
void clear();

} // namespace ObjectiveMarker
```

```cpp
// mod/features/objective_marker.cpp
#include "objective_marker.h"

#include "support/mod_log.h"
#include "ui/highlight.h"

namespace ObjectiveMarker {
namespace {

ModUI::Highlight::HighlightId g_marker = 0;

ModUI::Highlight::Style objective_style() {
  ModUI::Highlight::Style style{};
  style.color = IM_COL32(255, 213, 74, 235);
  style.fill_color = IM_COL32(255, 213, 74, 26);
  style.draw_box = true;
  style.filled = false;
  style.corner_box = true;
  style.draw_label = true;
  style.label_above_box = true;
  style.offscreen_indicator = true;
  style.width = 92.0f;
  style.height = 120.0f;
  return style;
}

} // namespace

void update(Unity::Transform objective) {
  // Call this function from ModRuntime::update().
  if (!objective || !objective.alive()) {
    clear();
    return;
  }

  Unity::clear_error();
  const Unity::Vector3 world = objective.position();
  if (const char* detail = Unity::last_error(); detail && detail[0]) {
    ModLog::warn("objective position read failed: %s", detail);
    clear();
    return;
  }

  if (g_marker == 0) {
    g_marker = ModUI::Highlight::enqueue_add_world_point(
        world, "Objective", objective_style());
    if (g_marker == 0)
      ModLog::warn("objective highlight could not be allocated");
    return;
  }

  ModUI::Highlight::enqueue_set_world_point(g_marker, world);
}

void clear() {
  if (g_marker != 0)
    ModUI::Highlight::enqueue_remove(g_marker);
  g_marker = 0;
}

} // namespace ObjectiveMarker
```

Call `ObjectiveMarker::clear()` from scene-change and shutdown paths. The ID is
native state, but its meaning is owned by the feature; do not let a previous
scene's ID silently become the new scene's marker.

`enqueue_set_world_point` may legitimately run once per main-thread update for
a moving target. Do not call `enqueue_add_world_point` every frame: that creates
new entries instead of moving the existing one.

### Transform target thread contract

The API also accepts a `GameObject`, `Component`, or `Transform`:

```cpp
const auto id = ModUI::Highlight::enqueue_add(
    objective_transform, "Objective", style);
```

This queues the handle safely, but “thread-safe queue” and “Unity main-thread
access” are different guarantees. The generated manager later validates the
Transform, reads its position, resolves `Camera::main()`, and projects the world
position while building the render frame. That work occurs in the native
presentation callback.

Use Transform targets only when the target game's/runtime's Unity calls are
known to be valid from that callback. The portable default is the world-snapshot
pattern above: read the Transform on the Unity main thread and enqueue the copied
position. If a title is sensitive even to camera projection outside Unity's
main thread, project on the main thread and publish a screen rectangle, or
provide a projector based entirely on a synchronized plain-data camera
snapshot.

Do not pass wrappers through arbitrary UI state just because `enqueue_add`
accepts them. The queue protects its native command vector; it cannot extend a
Unity object's lifetime or make a destroyed scene object valid.

### Static world points and screen rectangles

A fixed world location has no Unity handle:

```cpp
const auto checkpoint = ModUI::Highlight::enqueue_add_world_point(
    Unity::Vector3{12.0f, 2.0f, -5.0f},
    "Checkpoint",
    style);
```

Screen rectangles are already in ImGui screen coordinates. Direct calls such
as `add_screen_rect` belong to code executing in the render callback because
there is no queued screen-rectangle add operation in the current public helper
set:

```cpp
const auto id = ModUI::Highlight::add_screen_rect(
    ImVec2{100.0f, 80.0f},
    ImVec2{260.0f, 300.0f},
    "Target",
    style);
```

Do not call that direct API from `ModRuntime::update()`. For cross-thread
features, prefer queued world points or add a feature-owned plain-data command
that the render callback consumes.

### Projection and coordinates

By default, a world entry is projected with the current main camera. The
manager obtains screen size from ImGui and falls back to the camera pixel size.
Unity's screen-space Y axis grows upward, whereas ImGui's grows downward, so the
manager flips Y before drawing.

Projection produces more than a point: it records depth, whether the target is
in front, whether it is on screen, the clamped edge position, direction from
screen center, and distance. The style uses this data for distance scaling,
near-distance hiding, labels, and off-screen arrows.

If `Camera::main()` is null, the camera has not spawned, the tag is different,
or projection fails, the manager skips the entry instead of drawing an invalid
rectangle. Diagnose the camera; do not replace the failed projection with a
fake `(0, 0)` position.

### Style reference

The commonly changed style fields are:

| Field | Effect |
| --- | --- |
| `color`, `fill_color` | Box border and fill colors |
| `draw_box`, `filled`, `corner_box`, `shadow` | Box presentation |
| `width`, `height`, `rounding`, `thickness` | Box geometry |
| `draw_label`, `label_above_box` | Label visibility and placement |
| `offscreen_indicator` | Edge arrow for an off-screen target |
| `draw_behind_indicator` | Also show direction for behind-camera targets |
| `hide_within_distance` | Suppress on-screen box inside a near distance |
| `scale_with_distance`, `min_scale`, `max_scale` | Distance-based size |
| `indicator_padding`, `indicator_length` | Edge margin and arrow length |

Build one `Style` when the marker is created. Do not reconstruct and submit an
unchanged style every frame.

### Update policies and cost

Projection policy controls how frequently world entries are refreshed:

```cpp
ModUI::Highlight::UpdatePolicy policy{};
policy.mode = ModUI::Highlight::UpdateMode::Budgeted;
policy.max_updates_per_frame = 20;
policy.projection_interval_frames = 2;
policy.camera_resolve_interval_frames = 30;
policy.transform_validation_interval_frames = 30;

ModUI::Highlight::set_update_policy(policy);
```

Configure this once in `ModRuntime::start()`. In the generated lifecycle,
`ModRuntime::start()` runs before `ModHooks::install()` installs the render
hook. `set_update_policy` mutates manager configuration directly; it is not a
queued per-frame command.

| Mode | Behaviour | Suitable use |
| --- | --- | --- |
| `EveryFrame` | Every eligible target is reprojected every render frame | A small set of fast-moving markers |
| `Budgeted` | Refreshes up to the configured budget and reuses cached projections | General-purpose overlays |
| `EventDriven` | Refreshes only when dirty or explicitly moved | Static/event-driven markers |

`max_updates_per_frame == 0` disables the per-frame limit; it does not disable
updates. Watch `last_frame_stats()` when tuning many markers. A high cached-draw
count is expected in budgeted mode; projection failures are not.

### Highlight API ownership table

| Operation | Preferred caller | Reason |
| --- | --- | --- |
| `enqueue_add_world_point` | Unity main thread / feature code | Copies plain world data into pending queue |
| `enqueue_set_world_point` | Unity main thread / feature code | Moves an existing entry without recreating it |
| `enqueue_remove`, `enqueue_clear` | Unity main thread / lifecycle | Defers mutation to render owner |
| `enqueue_add(Transform/GameObject/Component)` | Main thread, with caveat above | Queue is safe; later wrapper access follows render-callback contract |
| `add`, `remove`, `add_screen_rect`, direct manager setters | Render callback only | Mutates render-owned entry storage immediately |
| Policy and diagnostic configuration | `ModRuntime::start()` before render-hook installation | One-time direct manager configuration |
| `manager().render()` | Generated render hook only | Already called once inside the ImGui frame |

Do not call `manager().render()` from a feature or menu. Rendering twice in one
frame duplicates work and breaks the generated hook's ownership model.

### Highlight diagnostics API

Enable diagnostics temporarily while bringing up an overlay:

```cpp
ModUI::Highlight::set_diagnostics([](const char* line) {
  ModLog::info("%s", line ? line : "");
});
ModUI::Highlight::set_verbose_diagnostics(true);
ModUI::Highlight::set_diagnostic_throttle_frames(120);
```

Register these in `ModRuntime::start()` for the same reason as the update
policy: they are direct manager configuration, not queued mutations.

Disable verbose mode for release unless the feature genuinely needs it. The
manager reports states such as missing/dead Transform, no projection,
projection failure, invalid rectangle, off-screen, too close, and removal.

Use this failure ladder:

1. Does the log say a DX11, DX12, or OpenGL render hook was installed?
2. Did ImGui initialize on a compatible game swap chain/context?
3. On DX12, was a direct command queue captured?
4. Is the `HighlightId` non-zero and still owned by the feature?
5. Does the manager's target count increase?
6. Is `Camera::main()` valid and is the point in front of the camera?
7. Are projection failures increasing in `last_frame_stats()`?
8. Is the style actually configured to draw a box, label, or indicator?

If the menu and highlight are both invisible, investigate the native render
hook first. If the menu is visible but the highlight is not, the DirectX/ImGui
path is already working; investigate marker ownership, projection, camera, and
style instead.

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

Hooks are required only when normal Unity API access cannot provide the needed
behaviour. Normal API access covers:

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

## 15. Persist settings

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

## 17. Diagnostics by API layer

Validate the API layers in this order. Each stage isolates the source of a
failure before the next subsystem is introduced:

1. Generated project compilation.
2. Loader DLL discovery.
3. `ModRuntime::start()` execution.
4. Scene callback delivery.
5. GameObject lookup.
6. Component lookup.
7. Field or property read.
8. Field write or managed method call.
9. `ModRuntime::update()` feature integration.
10. Plain menu request publication.
11. Managed/native hook installation, when normal API access is insufficient.

Adding five layers at once makes a crash much harder to localize.

### The mod does not load

- `URKit_logs.log` exists beside the game executable.
- The installed proxy filename matches an import of the game executable.
- Exactly one URKit proxy is installed.
- The mod DLL is located under `Mods`.
- The generated backend matches the target game.
- Debug and Release outputs use the generated build configuration.

### An object is not found

- Object name and capitalization match the Unity object name.
- Object spawn time precedes the lookup.
- Active/inactive state matches the selected search API.
- The target scene is loaded.
- The target tag exists in the game.
- Hierarchy or component-type search is used when name lookup is not stable.
- `Unity::last_error()` is captured immediately after the lookup.

### A component is not found

- Target location is identified: same GameObject, child, or parent.
- Assembly image, namespace, and class name match the managed type.
- `unity_type()` returns that exact type identity.
- Wrapper base type matches the managed inheritance kind.
- Type metadata is revalidated after a game update.

### A field always reads as zero

- Zero is distinguished from an API failure through an immediate
  `Unity::last_error()` check.
- Field/property kind matches the selected API.
- Static/instance ownership matches the selected API.
- Managed value type and enum layouts match the target metadata.

### A method call fails

- Exact overload and complete managed parameter names match the target method.
- The parameter list excludes the return type.
- Static/instance ownership matches the selected API.
- Managed exceptions are captured through `Unity::last_error()`.

### The menu works but the button does nothing

- UI code publishes a request queue entry or atomic value.
- Feature processing occurs in `ModRuntime::update()`.
- Main-thread update registration succeeds.
- Scene reset invalidates and rediscoveries scene-owned handles.
- Rendered state uses a main-thread snapshot.

### A hook fails or crashes the game

- Runtime backend matches the hook helper API.
- Resolved managed method and overload match target metadata.
- Native parameter and return ABI are verified.
- IL2CPP signatures include trailing method information when required.
- Original function pointer is called only after successful attachment.
- Hook installation is idempotent.
- Shutdown detaches hooks in reverse ownership order.

Removing an error check, adding an empty catch, or using an unvalidated address
does not solve the problem. It only makes the failure less observable and the
eventual crash harder to diagnose.

## 18. Release validation

Release validation includes a clean Release configure/build, first launch, scene
transition, and game shutdown. Validate the following invariants:

- Supported runtime unload detaches all hooks.
- Shutdown clears highlights, commands, and caches.
- Scene-owned handles do not survive into a later scene.
- Closing the menu returns input and cursor ownership to the game.
- Global searches and reflection do not execute every frame.
- Queues are bounded.
- Failures remain observable without repeated log spam.
- Metadata records include every assembly image, class, field, property, and method.
- Metadata matches the current game version.
- The release package contains only required files.

## API integration sequence

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
14. Test scene transitions and shutdown.

A reliable URKit mod resolves the correct object and member, performs Unity work
on the correct thread, and releases everything it owns during shutdown.
