# URKit SDK Handbook

This is the API reference for generated URKit mods: object access, managed
members, threading, hooks, unload safety, and the native overlay. It's not
meant to be read top to bottom. New to URKit? Read
[Getting Started](GETTING_STARTED.md) first, then come back here when you
need a specific answer.

Every game name, assembly, namespace, class, field, and method below is made
up. Swap them for whatever your target game actually uses. `ModRuntime`,
`ModHooks`, `ModLog`, and `Unity::` are the real generated/SDK names though;
everything else (feature names, wrapper names, cache names) is just an
example.

One thing worth repeating: a generated mod DLL is a URKit loader plugin, not
something you inject on its own. Load it through a URKit proxy or
`URKitInjector.dll`.

## Core API workflow

Almost every feature you'll write reduces to the same flow:

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

Here's that flow's first two steps, resolving a GameObject and getting a
built-in component off it:

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
[GameObject and Component APIs](#7-gameobject-and-component-apis).

Highlights (world-space overlay boxes/markers) are a separate pipeline, don't
confuse it with the one above:

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

A mod splits into five jobs:

1. URKit loads the DLL and wires up the lifecycle callbacks.
2. `ModRuntime` gives you a place to run Unity work on the main thread.
3. Game bindings wrap the game's managed types in small C++ classes.
4. Feature modules decide what the mod actually does.
5. The menu just collects user intent. It never touches a Unity object
   directly.

Data flows one way:

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
- Use `Unity::is_main_thread()` when code can also be reached from an ImGui,
  window-message, network, or worker callback. `Unity::require_main_thread()`
  records an actionable `last_error()` message for early returns.
- Find scene objects once and cache them; do not repeat a global search every
  frame.
- Validate long-lived cached handles with `alive()` and rediscover them after
  destruction.
- Treat scene callbacks as optional cache-invalidation hints. Clear scene-owned
  handles from those callbacks when they are available, but do not depend on a
  callback to discover an object or notice its destruction.
- Check `Unity::last_error()` immediately after a failed or suspicious call.

## 1. Tools and project generation

On Windows x64, you'll need:

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

### Update an existing project

`urk-updater.exe` updates an existing generated `project` directory. It
checks the backend and SDK ABI version first, then shows you the exact
URKit-managed file list: `+` for a file that'll be added, `~` for one that'll
be refreshed. **Update Project** shows that same list in a confirmation
dialog, backs up URKit-managed files under `.urk/backups/`, then rewrites the
generated SDK, lifecycle, hook, and build support files. `mod/ui/` is seeded
once for new projects and left alone on every update after that, same as the
rest of your own files.

```powershell
./urk-updater.exe --project C:\Games\Example\urk-sdk-output\FirstSteps\project --check
./urk-updater.exe --project C:\Games\Example\urk-sdk-output\FirstSteps\project --update
./urk-updater.exe --project C:\Path\To\CustomMod --stage-sdk
./urk-updater.exe --check-updater
```

`--check` returns exit code `10` when an update is available and leaves the
project untouched. The updater never downgrades a project generated by a newer
SDK. Legacy projects without `.urk/project.ini` are supported when their
generated SDK folders and `URK_DEPLOY_DIR` setting are intact.

### Generated-file ownership and migrations

Every project this SDK version generates carries `.urk/generated-files.ini`:
a SHA-256 baseline for each replaceable generated file. An ordinary update
only touches files that still match that baseline. Edit a generated file
yourself, and the updater won't overwrite it: it stages a full candidate
under `.urk/updates/` and marks the conflict with `!` instead.

No ledger means no assumptions. A project without one gets its first update
staged for you to migrate by hand, so a custom SDK patch never gets silently
clobbered. Got a custom project with both `sdk/mono` and `sdk/il2cpp`? Use
`--stage-sdk`, it builds independent candidates without needing a standard
URKit manifest or touching the project at all.

The desktop updater also has a **Check Updater** button. It checks the latest
official GitHub release, asks for confirmation, verifies the published SHA-256
digest of `urk-updater.exe`, installs it through a helper process, and restarts.
When publishing a release, include the staged `urk-updater.exe` asset and keep
its version in `src/sdk/updater_version.h` synchronized with the release tag.

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

CMake copies the resulting DLL straight into the game's `Mods` directory. One
gotcha: adding a new `.cpp` or `.h` file means running the configure command
again. Source discovery under `mod/` is recursive, but CMake still has to
regenerate its source list to notice the new file.

### Verify the generated project

Before writing any feature code, start the game and open `URKit_logs.log`
beside the game executable. Look for four things: did URKit start, was the
mod DLL discovered, does the selected backend match the game, and did the
mod initialize.

No log file at all means the game isn't importing the proxy DLL you
installed. Install exactly one proxy, and don't rename it.

## 2. Know which files you own

Most of your actual work happens in a small corner of the generated project:

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
get seeded once and then left alone on every regeneration after that. Still
worth keeping larger features in their own files anyway, it makes comparing
and adopting upstream UI changes a deliberate choice instead of a merge
headache.

`mod_runtime.cpp` should stay about lifecycle coordination, nothing else. As
the project grows, push object discovery, settings, and UI code into their
own modules:

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

Getting at a game type usually comes down to four pieces of information:

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
games require the native output and metadata to be considered together. Don't
have an external decompiler or dumper for the target game? The generated SDK
ships its own runtime inspector for exactly this: see
[Inspect an unfamiliar type at runtime](#81-inspect-an-unfamiliar-type-at-runtime).

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

A few things worth double-checking before you trust any of it: a field and a
property aren't interchangeable, static and instance members use different
access paths, `System.Int32` and `System.Single` are different layouts,
overloads with the same name can still share a parameter count, and a C#
namespace has nothing to do with a Unity hierarchy path.

A game update can silently change any of this. If a lookup that used to work
suddenly fails, re-verify the metadata before you do anything else. Deleting
the error check just hides the real cause; it doesn't fix it.

## 4. Understand the Unity object model

Most scene objects look like this:

```text
GameObject "Hero"
|-- Transform                  present on every GameObject
|-- HeroVitals                 game-specific MonoBehaviour
|-- CharacterController       Unity component
`-- Animator                  Unity component
```

`GameObject` is the scene container. Components hold the behaviour and
state. `Transform` is a component too; it owns hierarchy, position,
rotation, and scale.

URKit wrappers don't own the Unity objects they point to:

```cpp
Unity::GameObject actor;
```

That's a borrowed handle, not a copy of anything Unity owns. Which means
there are two separate things to check, not one:

```cpp
if (!actor) {
  // The wrapper has no handle.
}

if (actor && !actor.alive()) {
  // A handle exists, but Unity has destroyed the object.
}
```

Scene transitions cause both of these a lot, but they're not the only cause:
an object can get destroyed and replaced with the active scene never
changing at all. Call `alive()` on anything you cache for long, and reset
wrappers to `{}` from scene-change callbacks when the game gives you those.

## 5. Verify Unity access

Before writing any real feature, confirm managed Unity access actually works
from the right callback. Use this as the starting shape of
`mod/lifecycle/mod_runtime.cpp`:

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

`MainCharacter` is a placeholder tag; if the game doesn't define it,
`FindWithTag` just reports an error. Don't move on until the scene name,
object name, and position actually show up in the log.

Seeing `URK_SceneInfo::buildIndex` as `-1`? That's normal on an IL2CPP player
that stripped Unity's optional build-index binding. Scene-loaded and
active-scene-changed callbacks still work fine; just use the scene name and
handle instead of the index.

Why does the probe keep searching instead of stopping after one try? Because
the character can spawn after the scene callback already fired. Real code
needs a search interval and a cache, which is exactly what's next.

### Scene callbacks are hints, not object-readiness signals

Some games keep one scene active for the whole session and spawn gameplay
objects much later. Waiting for a scene callback before you start looking is
the wrong instinct here. Poll on a bounded interval, cache what you find, and
fall back to discovery again the moment `alive()` says no. Scene callbacks
only get to invalidate the cache early; they don't get to gate discovery.

```cpp
#include <chrono>

#include "sdk/unity/unity.h"

namespace {
using Clock = std::chrono::steady_clock;
Unity::GameObject g_character;
Clock::time_point g_next_lookup{};

void forget_character() {
  g_character = {};
  g_next_lookup = Clock::time_point{};
}
} // namespace

void ModRuntime::update() {
  const Clock::time_point now = Clock::now();

  if (g_character && !g_character.alive())
    forget_character();

  if (!g_character && now >= g_next_lookup) {
    g_next_lookup = now + std::chrono::milliseconds(750);
    g_character = Unity::GameObject::FindWithTag("MainCharacter");
  }

  if (g_character) {
    // Perform feature work here on the Unity main thread.
  }
}

void ModRuntime::on_scene_loaded(const URK_SceneInfo*) {
  forget_character();
}

void ModRuntime::on_scene_changed(
    const URK_SceneInfo*,
    const URK_SceneInfo*) {
  forget_character();
}
```

One catch: `OnSceneLoaded` and `OnSceneChanged` being resolved only proves the
exports exist in the mod DLL, not that the runtime will actually call them.
Check `URK::has_scene_events()` for that. If it says no, the polling path
above still works fine on its own. Either way, capture `Unity::last_error()`
after a failed lookup, and throttle the log output instead of spamming it.

## 6. Choose the right object search

There's no one-size-fits-all search call here. Pick based on the most stable
fact you actually know about the object.

### Name or hierarchy path

```cpp
Unity::GameObject altar = Unity::GameObject::Find("World/Temple/Altar");
```

This usually only finds active objects, and names aren't guaranteed unique. A
full hierarchy path beats a bare name every time.

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

`FindObjectsOfTypeAll` casts a much wider net than "the current scene": it can
pull in assets, hidden objects, inactive objects, persistent objects, all of
it. Don't treat its raw result as "everything in this scene" until you've
filtered it down.

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

Here's how GameObject, Component, and Transform handles relate to each other:

```text
GameObject "Hero"                    Unity::GameObject hero
|-- Transform                        hero.transform()
|-- Animator                         hero.GetComponent<Unity::Animator>()
|-- Rigidbody                        hero.GetComponent<Unity::Rigidbody>()
`-- HeroVitals                       hero.GetComponent<HeroVitals>()
```

`GetComponent` only ever looks at the GameObject you called it on. No scene
search, no automatic walk up to parents or down to children. What comes
back is another non-owning wrapper, and a null one just means nothing
matched.

### Attached built-in component

Built-in components already carry `unity_type()` metadata in the generated
SDK, so use the typed form directly:

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

Every GameObject has a Transform, so just call `actor.transform()`.
`actor.GetComponent<Unity::Transform>()` technically works too, it just
doesn't buy you anything.

### Attached game-specific component

No typed wrapper yet? Give it the managed type identity directly:

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

Those three strings describe a managed type, full stop. Not a file path, not
a Unity hierarchy path, not a GameObject name. The call resolves that type,
then asks the target GameObject whether it has one attached.

### Component on a child or parent

Reach for child/parent search only once you've confirmed the component isn't
on the current GameObject:

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

Same quirk Unity itself has: the search includes the starting GameObject, so
"children" and "parents" here don't mean strict descendants or ancestors
only. The boolean just decides whether inactive GameObjects get considered;
it doesn't turn a local lookup into a scene-wide one.

Game-specific type instead of a built-in? Same metadata triplet, untyped
form:

```cpp
Unity::Object weapon = actor.GetComponentInChildren(
    "GameScripts.dll", "Adventure.Items", "EquippedWeapon", true);

Unity::Object controller = actor.GetComponentInParent(
    "GameScripts.dll", "Adventure.Runtime", "PlayerController", true);
```

### One component versus every component

`GetComponent` gives you the first match and stops there. Reach for the
plural APIs whenever a GameObject or hierarchy could plausibly hold more than
one:

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

These plural queries try Unity's native-backed `GetComponentsInternal` first.
That matters on a stripped IL2CPP player, where a public `GetComponents*`
method can still show up in metadata with no callable body behind it. The SDK
keeps the public overloads around as a fallback for exactly that case, and
if both paths fail, `Unity::last_error()` tells you about both failures, not
just the last one.

When a GameObject can hold unrelated component types, ask for the default
`Object` wrapper instead of a specific one. URKit queries Unity with
`UnityEngine.Component` and hands back each mismatched component as a plain
`Object`:

```cpp
for (const Unity::Object& component : actor.GetComponents<>()) {
  ModLog::info("%s", component.runtime_class_name().c_str());
}
```

Same rule for the rooted and hierarchy variants: pass a concrete wrapper like
`Renderer` and its Unity type becomes the filter, same as above.

The vector-returning overloads keep the familiar API, rooting the managed
result array only long enough to decode it. If your loop makes several
managed calls per iteration, hold onto the rooted lease for the whole loop
instead:

```cpp
auto renderers =
    actor.GetComponentsInChildrenRooted<Unity::Renderer>(true);

if (!renderers) {
  ModLog::warn("component scan failed: %s", Unity::last_error());
  return;
}

for (const Unity::Renderer& renderer : renderers) {
  if (renderer.alive())
    ModLog::info("renderer: %s", renderer.name().c_str());
}
```

`GetComponentsRooted`, `GetComponentsInChildrenRooted`, and
`GetComponentsInParentRooted` return move-only RAII leases: destroy or reset
one and its strong GC handle goes with it. A valid lease can still be empty,
so test the lease itself, not its contents, to tell "Unity found nothing"
apart from "the API call failed." The lease keeps managed references
reachable, but Unity can still destroy the underlying native object out from
under it, so `alive()` doesn't go away just because you're holding a lease.

And don't call `GetComponentsInChildren` every frame to rediscover a
hierarchy that hasn't changed. Cache the wrappers, check `alive()` on the
long-lived ones, and only rebuild after a scene or hierarchy change.

### GameObject and Component conversion

This relationship goes both directions:

```cpp
Unity::GameObject owner = animator.gameObject();
Unity::Transform owner_transform = animator.transform();
Unity::Rigidbody sibling = animator.GetComponent<Unity::Rigidbody>();
```

Calling `GetComponent` on a `Component` just delegates to its owning
GameObject, so the last line above asks for a sibling `Rigidbody`, not
something nested inside the Animator.

### Component lookup reference

| Situation | Call |
| --- | --- |
| Built-in component on this object | `object.GetComponent<Unity::Animator>()` |
| Custom component on this object, no wrapper yet | `object.GetComponent(image, namespace, class)` |
| One component somewhere below | `object.GetComponentInChildren<T>(includeInactive)` |
| One component somewhere above | `object.GetComponentInParent<T>(includeInactive)` |
| Every local match | `object.GetComponents<T>()` |
| Every match below or above | `GetComponentsInChildren<T>()` / `GetComponentsInParent<T>()` |
| Multi-call iteration with managed lifetime protection | `GetComponentsInChildrenRooted<T>()` or the matching rooted variant |
| Owning GameObject from a component | `component.gameObject()` |
| Sibling component from a component | `component.GetComponent<T>()` |

### Null component result

Got null back? One of these five is almost always the reason: the target
GameObject isn't what you think it is, the component actually lives on a
child or parent, the assembly image, namespace, or class name is wrong, the
object or component hasn't spawned yet (or is inactive), or a cached
GameObject is still pointing at a previous scene.

Grab the error right next to the lookup, before some other Unity call gets a
chance to overwrite it. With the typed binding from the next subsection, that
looks like this:

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

Treat an empty result as a failed lookup until you've verified the exact
object identity, hierarchy, and managed type metadata, not before. Never turn
it into a made-up default value just to move on.

### Write a typed game binding

Using a game-specific type in more than one place? Stop retyping the same
three strings everywhere and write a binding instead. Create
`mod/bindings/hero_vitals.h`:

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

Typed component access just works now:

```cpp
DemoBindings::HeroVitals vitals =
    actor.GetComponent<DemoBindings::HeroVitals>();
```

Just make sure the C++ base actually matches the real managed kind:

| Managed type | C++ wrapper base |
| --- | --- |
| `MonoBehaviour` subclass | `Unity::MonoBehaviour` |
| Other `Component` subclass | `Unity::Component` or the nearest wrapper |
| `ScriptableObject` subclass | `Unity::ScriptableObject` |
| Ordinary managed reference type | `Unity::Object` |

This C++ inheritance doesn't cast the managed object or create a C# subclass;
it just tells the wrapper which operations are valid for that handle. Don't
derive an ordinary data class from `MonoBehaviour` just to borrow its helper
methods.

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

Under the hood a property getter or setter is just a managed method call.
`SetProperty` on a read-only property fails, as it should. And `GetProperty`
on an actual field (or `GetField` on an actual property) isn't a fallback
that happens to work; it's just wrong.

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

For a `void` method, reach for `TryCallExact` instead: its return value
actually tells you whether lookup, marshalling, or the managed call itself
failed.

```cpp
if (!Unity::require_main_thread("Health.Heal") ||
    !component.TryCallExact("Heal", {"System.Single"}, 15.0f)) {
  ModLog::error("Heal failed: %s", Unity::last_error());
  return;
}
```

A successful call just means the managed method returned without throwing.
That's it, it doesn't prove anything about game state: the method might
clamp the value, reject it silently, or have its result overwritten later
the same frame. If the state change actually matters to your feature, read
the field or property back afterward and check.

The parameter list never includes the return type, just the arguments, and
it wants the full managed type name for each one:

| C++ | Managed signature |
| --- | --- |
| `bool` | `System.Boolean` |
| `int` | `System.Int32` |
| `float` | `System.Single` |
| `double` | `System.Double` |
| `std::string_view` | `System.String` |
| `Unity::Vector3` | `UnityEngine.Vector3` |

If a method hands back a managed object, ask for the right wrapper type:

```cpp
Unity::GameObject target =
    component.Call<Unity::GameObject>("CurrentTarget");
```

For a managed array:

```cpp
const auto markers =
    component.CallArrayExact<Unity::Transform>("GetMarkers", {});
```

### Static method or property

`StaticGetField` and `StaticSetField` only cover fields. A static property
getter, a static factory method, or any other static method without a
dedicated wrapper goes through `URK::Unity::detail::InvokeStatic<Ret>(type,
methodName, args...)` instead. This is the same internal call the SDK itself
uses for `Object::Instantiate` and `Object::Destroy`.

A C# property compiles to a getter method with a `get_` prefix (and a setter
with `set_`), so a static property reads exactly like any other static
method call. This example reads `Photon.Bolt.BoltNetwork.IsServer` in a game
built on the Photon Bolt networking library:

```cpp
// bolt.dll
// Photon.Bolt.BoltNetwork
// public static bool IsServer => BoltCore.isServer;

inline constexpr Unity::TypeRef kBoltNetworkType{
    "bolt.dll", "Photon.Bolt", "BoltNetwork"};

bool IsHost() {
    Unity::clear_error();
    const bool is_server =
        URK::Unity::detail::InvokeStatic<bool>(kBoltNetworkType, "get_IsServer");

    if (const char* error = Unity::last_error(); error && error[0]) {
        ModLog::warn("BoltNetwork.get_IsServer failed: %s", error);
        return false;
    }
    return is_server;
}
```

Define the type once with `TypeRef`, then call the generated getter by name.
The same pattern works for any static property once you know its managed
type.

A static factory method that returns an object works the same way, just with
a real return type instead of `bool`. Here's a complete example built on
that: creating a Photon Bolt event and sending it to another player. The
target game defines `Photon.Bolt.ReviveEvent` in `bolt.user.dll`:

```csharp
public static ReviveEvent Create();

public BoltEntity Player { get; set; }
public BoltEntity Reviver { get; set; }
```

```cpp
void Revive(Unity::GameObject target_player, Unity::GameObject local_player) {
    if (!target_player || !local_player)
        return;

    Unity::Object target_entity =
        target_player.GetComponent("bolt.dll", "Photon.Bolt", "BoltEntity");
    Unity::Object reviver_entity =
        local_player.GetComponent("bolt.dll", "Photon.Bolt", "BoltEntity");

    if (!target_entity || !reviver_entity)
        return;

    constexpr Unity::TypeRef revive_event_type{
        "bolt.user.dll", "Photon.Bolt", "ReviveEvent"};

    Unity::Object revive_event =
        URK::Unity::detail::InvokeStatic<Unity::Object>(revive_event_type, "Create");
    if (!revive_event)
        return;

    revive_event.CallExact<void>("set_Player", {"Photon.Bolt.BoltEntity"}, target_entity);
    revive_event.CallExact<void>("set_Reviver", {"Photon.Bolt.BoltEntity"}, reviver_entity);
    revive_event.Call<void>("Send");
}
```

`target_player` is whoever you want to revive. `local_player` is your own
player's GameObject; URKit has no notion of "the local player", so resolving
and caching that handle is on you, and how you do it depends entirely on the
game. The managed side of this call is four steps: `ReviveEvent.Create()`
through `InvokeStatic`, the two property setters through `CallExact` (a C#
property setter compiles to `set_<Name>`, the same way a getter compiles to
`get_<Name>`), and `Send()` through a plain `Call`.

### Why a zero result is dangerous

A failed `GetField<int>` returns `0`, which looks exactly like a genuine `0`.
Same story for `false`, `0.0f`, an empty string, an empty vector: the failure
case and the legitimate value are indistinguishable unless you check. Keep
the error check right next to the call:

```cpp
Unity::clear_error();
const float value = component.GetProperty<float>("Energy");
const char* detail = Unity::last_error();

if (detail && detail[0]) {
  // Do not use value; the read failed.
}
```

And check it right away, the next Unity call you make can overwrite it.

## 8.1 Inspect an unfamiliar type at runtime

Only know the object or class name? Don't guess whether a member is a field,
property, or method, `sdk/unity/unity.h` ships inspection helpers that just
tell you:

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

Run this once, pick the member you actually need, then switch to a typed
wrapper in real feature code. Enumerating everything on every `update()` is
just wasted reflection work.

The rest of the inspection helpers:

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

`ValueInfo` comes back tagged, not typed: check `readable` and `kind` before
you treat it as an integer, float, string, or object.

## 8.2 Everyday Unity helpers

Your generated `sdk/unity/unity_components.h` is the real source of truth for
whatever wrappers your SDK version has. If something here doesn't match, trust
your generated header over this page.

### One-shot input toggle

```cpp
static bool enabled = false;
static bool input_unavailable_reported = false;

if (!Unity::Input::available()) {
  if (!input_unavailable_reported) {
    ModLog::warn("Unity legacy input is unavailable in this player");
    input_unavailable_reported = true;
  }
} else if (Unity::Input::GetKeyDown(Unity::KeyCode::F8)) {
  enabled = !enabled;
  ModLog::info("feature: %s", enabled ? "on" : "off");
}
```

`GetKey` stays true the whole time the key's held down; `GetKeyDown` is
usually what you want for a toggle, and `GetKeyUp` matters when release is
the trigger. Mouse has the same three: `GetMouseButton`,
`GetMouseButtonDown`, `GetMouseButtonUp`.

Check `Unity::Input::available()` before you trust a `false`, though: the
boolean helpers also return `false` when legacy input just isn't available at
all. If it's not, fall back to a menu action, or have a native input module
publish a plain request that `ModRuntime::update()` picks up.

### Time

```cpp
const float frame_seconds = Unity::Time::deltaTime();
const float real_frame_seconds = Unity::Time::unscaledDeltaTime();
```

`deltaTime` moves with the game's `timeScale`; reach for `unscaledDeltaTime`
for menu animation or any timer that needs to keep running while the game is
paused. If a feature ever calls `set_timeScale`, save the previous value and
put it back when the feature turns off.

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

Unity screen space and ImGui's overlay space disagree about which way Y
points; the URKit projection helpers handle that conversion for you.

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

Use `position` and `rotation` for world space, and their local equivalents
when you actually want parent-relative coordinates. Reparenting? Think about
the `SetParent(parent, worldPositionStays)` argument before you pass it, both
values do something different and only one of them is probably what you
want.

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

`Destroy` follows Unity's normal delayed path; only reach for
`DestroyImmediate` when you genuinely need it to happen right now. And
whatever your mod creates, keep it owned by the feature that created it, and
clean it up on scene reset or shutdown; don't let it become an orphan.

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

Missing a Unity API from a built-in wrapper? Write a small wrapper of your
own and use `GetProperty` or `CallExact`, exactly like you would for a game
type.

## 9. Organize a feature

Time for a real feature, end to end. In our fictional game, it tops the local
character's energy back up to a floor whenever it drops too low.

Three files, three responsibilities:

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

Notice there's no `Unity::Object` anywhere in that header. The menu never
gets to see a managed handle at all.

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

A few things worth noticing here: the failed tag search retries every
750 ms, not every frame; the Unity wrapper never leaves the main-thread side
of the module; the menu only ever sees a copy of `ViewState`; a failed
managed call clears the cached handle so discovery gets another shot; and the
setting, the cache, and the observable state each have exactly one owner.

### Connect it to the lifecycle

Pull the feature into `mod/lifecycle/mod_runtime.cpp` by adding calls into
the existing function bodies:

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

These functions likely already exist inside `namespace ModRuntime` in the
generated file, so add the calls into the existing bodies. Don't define a
second `update()`, and leave the context/backend initialization where it is,
in `start()`.

`forget_scene()` invalidates the cache early, but it's not the only path
that does: `EnergyAssist::advance()` still has to reject a cached component
that fails `alive()` and fall back to its timed discovery. That's what keeps
the feature working correctly in a game that never changes scenes, or one
that doesn't expose URKit's scene-event capability at all.

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

Making it show up just means including it from the preserved `mod/ui/menu.h`
and calling `PracticePanel::draw()` wherever you want it. Regeneration might
undo that one-line connection, but never the panel itself, it lives in its
own user-owned file.

The quickest wiring: draw it right below the current tab's content.

```cpp
#include "tabs/practice_panel.h"

// In the content area of ModUI::render_menu():
active_entry.render();
PracticePanel::draw();
```

You can give it its own `Tab` enum value and a real tab entry later. Prove
the feature works first; navigation polish can wait.

## 10. Menu and main-thread boundary

This looks fine. It isn't:

```cpp
// Bad: managed Unity call from the render callback.
if (ImGui::Button("Refill"))
  g_vitals.Call<void>("Refill");
```

The render callback can run on a different thread, and `g_vitals` can go
stale mid scene-transition. The safe version enqueues a request from the UI
and lets `update()` process it instead.

A single toggle only needs an `std::atomic_bool`. Anything with parameters
wants a bounded queue:

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

Then at the top of `update()`, swap the whole queue out into a local vector
while barely holding the lock at all:

```cpp
std::vector<Command> take_commands() {
  std::lock_guard lock(g_command_mutex);
  std::vector<Command> result;
  result.swap(g_commands);
  return result;
}
```

Make the Unity calls after the lock's released, never while holding a mutex
across a managed call. And if the queue fills up, say so, or pick an explicit
"latest value wins" policy. Either is fine. Quietly dropping the work isn't.

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

Global searches, reflection, managed property calls, none of it is free. A
tiny mod can get away with ignoring that. An overlay tracking hundreds of
objects can't.

A good cache doesn't run everything at the same rate:

| Work | Example interval |
| --- | --- |
| Discover new objects | 500-1000 ms |
| Validate cached handles | 200-500 ms |
| Capture fast position state | 30-100 ms |
| Refresh slower names or state | 500-2000 ms |

Don't treat those numbers as gospel; measure how fast your own data actually
changes and set intervals from that.

Keep the runtime state and the published state as two separate structs:

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

`RuntimeEntry` never leaves the main thread. `ActorSnapshot` is the one
that's safe to copy to the render side.

### Mark dirty instead of scanning inside a hook

If a hook notices something that might be a new object, resist the urge to
scan right there. Flip an atomic dirty flag instead:

```cpp
std::atomic_bool g_rescan_requested{true};

void notice_possible_change() noexcept {
  g_rescan_requested.store(true, std::memory_order_release);
}
```

The main-thread update reads that flag and scans when it's actually a good
time to. The hook stays short, and reflection or allocation stays owned by
one place.

### Log spam is also a performance bug

A property that fails every frame doesn't need hundreds of identical log
lines per second to prove it. Keep the failure visible, just throttle it:

```cpp
if (now >= next_error_log) {
  ModLog::warn("actor scan failed: %s", Unity::last_error());
  next_error_log = now + std::chrono::seconds(5);
}
```

The UI snapshot can still show the latest error between log lines, so
throttling isn't hiding anything. It's just refusing to let one known
failure eat your disk and frame time.

## 12. Highlights and the native render pipeline

Every generated project ships `mod/ui/highlight.h`. It draws a box, a fill,
a label, and an off-screen direction indicator, and it can anchor any of
that to:

- a world-position snapshot;
- a `Transform`, `GameObject`, or `Component` target;
- an already projected screen rectangle.

Don't read "highlight" as a Unity outline effect, it isn't one. Nothing about
a target's `Renderer`, materials, shaders, render layer, or GameObject ever
changes. It's ImGui draw commands, submitted by the generated native render
hook straight to the game's graphics back end.

### The two calls it comes down to

Everything below is background. The part you actually write is this, from
`ModRuntime::update()` on the Unity main thread:

```cpp
#include "ui/highlight.h"

namespace {
ModUI::Highlight::HighlightId g_marker = 0;
}

// create once, get an ID back
g_marker = ModUI::Highlight::enqueue_add_world_point(world, "Target", style);

// then move that entry for the rest of its life
ModUI::Highlight::enqueue_set_world_point(g_marker, world);

// and drop it when the thing it points at is gone
ModUI::Highlight::enqueue_remove(g_marker);
g_marker = 0;
```

A highlight is an entry you own, not a draw call you repeat. Calling
`enqueue_add_world_point` every frame is the one mistake worth naming up
front: it creates a fresh marker each time instead of moving the one you
already have, and the overlay fills up with stacked duplicates.

The `enqueue_` prefix is the thread boundary. Those calls copy plain data into
a mutex-protected queue that the render thread drains at the start of its
frame, which is what lets you read Unity where Unity lives and still draw from
the presentation hook. The unprefixed `add`/`remove`/`set_*` variants mutate
render-owned state immediately and belong to code already running inside the
render callback.

[Getting Started step 8](GETTING_STARTED.md#8-draw-a-box-on-it) builds this up
end to end: one marker on the player, styling it, one ID per enemy across a
whole scene, scene-change cleanup, and what to check when nothing appears.

### Where a highlight is actually rendered

On a DirectX game the generated project installs DXGI presentation hooks, and
the highlight travels this path end to end:

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

Which means the overlay is native DirectX output, composited straight into
the same back buffer the game presents. No Unity Canvas involved, and it
never exists in the Unity scene hierarchy at all. It lands on ImGui's
background draw list by default, so it renders over the 3D scene and behind
the mod's own ImGui menu.

None of this means your feature code needs to touch DirectX. The generated
`mod/hooks/render_imgui_hook.cpp` owns device discovery, swap-chain hooks,
ImGui frame creation, render targets, resize handling, and shutdown. Your
feature code just owns highlight IDs and publishes plain target data.

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

`ResizeBuffers` tears down the render target and ImGui device objects before
the resize happens, then rebuilds them once it succeeds. Don't cache a
DirectX back-buffer or render-target pointer in your own feature code; it
won't survive that.

### What happens on DirectX 12

D3D12 doesn't let you skip explicit command submission, so the generated hook
also grabs the game's direct `ID3D12CommandQueue` through
`ExecuteCommandLists`. Per presented back buffer:

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

The D3D12 overlay simply won't render until both the DXGI presentation hook
and the direct command queue are in place. Check the log to tell which one:
presentation hook failure, command-queue hook failure, device-object
failure, and per-frame fence/command-list failure all get logged as
distinct cases.

### Back-end selection and support

`URK::graphics_device_type()` reports Unity's graphics device when the
runtime exposes it. Here's what the generated render hook currently handles:

| Reported device | Generated overlay path |
| --- | --- |
| Direct3D 11 | DXGI `Present`/`Present1` + ImGui DX11 |
| Direct3D 12 | DXGI + direct command queue + ImGui DX12 |
| OpenGL 2 / OpenGL Core | `wglSwapBuffers` + ImGui OpenGL3 |
| Unknown | Probe native DXGI and OpenGL presentation paths |
| Vulkan or another unsupported device | No generated highlight/menu render hook |

"Unknown" doesn't mean "assume DX11." It means the generated project probes
the native presentation APIs itself and only initializes once it actually
sees a compatible context. Check `URKit_logs.log` for the path it picked;
don't guess from the game's launcher options.

### Main-thread world snapshots

The pattern that's actually predictable: read Unity state during
`ModRuntime::update()`, then enqueue nothing but a copied `Unity::Vector3`.
Scene-object ownership stays in feature code, and the render side only ever
sees plain data.

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

Call `ObjectiveMarker::clear()` on scene-change and shutdown. The ID is
native state, sure, but its meaning belongs to the feature, so don't let a
stale ID from a previous scene quietly become this scene's marker.

Calling `enqueue_set_world_point` once per main-thread update for a moving
target is exactly what it's for. Calling `enqueue_add_world_point` every
frame instead is a bug: that creates a fresh entry each time instead of
moving the one you already have.

### Transform target thread contract

The API also takes a `GameObject`, `Component`, or `Transform` directly:

```cpp
const auto id = ModUI::Highlight::enqueue_add(
    objective_transform, "Objective", style);
```

Queuing the handle is thread-safe. That's not the same thing as "safe to
touch Unity from," though, and the generated manager does exactly that
later: it validates the Transform, reads its position, resolves
`Camera::main()`, and projects the world position while building the render
frame, all from the native presentation callback.

So only pass a Transform target when you actually know this specific game
and runtime tolerate Unity calls from that callback. The safe default stays
the world-snapshot pattern from above: read the Transform on the Unity main
thread, enqueue just the copied position. If a title is touchy even about
camera projection happening off the Unity main thread, project it yourself
on the main thread and publish a screen rectangle instead, or build a
projector entirely off a synchronized plain-data camera snapshot.

And don't pass wrappers through arbitrary UI state just because `enqueue_add`
happens to accept them. The queue only protects its own native command
vector; it can't extend a Unity object's lifetime or resurrect a destroyed
scene object.

### Static world points and screen rectangles

A fixed world location doesn't need a Unity handle at all:

```cpp
const auto checkpoint = ModUI::Highlight::enqueue_add_world_point(
    Unity::Vector3{12.0f, 2.0f, -5.0f},
    "Checkpoint",
    style);
```

Screen rectangles already speak ImGui screen coordinates. There's no queued
version of adding one, so `add_screen_rect` is a direct call, meant for code
already running in the render callback:

```cpp
const auto id = ModUI::Highlight::add_screen_rect(
    ImVec2{100.0f, 80.0f},
    ImVec2{260.0f, 300.0f},
    "Target",
    style);
```

Don't call that from `ModRuntime::update()`. For anything crossing threads,
stick to queued world points, or add your own feature-owned plain-data
command that the render callback consumes instead.

### Projection and coordinates

By default a world entry gets projected with the current main camera. Screen
size comes from ImGui first, falling back to the camera's pixel size. And
since Unity's screen-space Y grows upward while ImGui's grows downward, the
manager flips it before drawing, so you don't have to.

Projection gives you more than a point, too: depth, whether the target's in
front of the camera, whether it's on screen, the clamped edge position,
direction from screen center, and distance. That's what the style options
use for distance scaling, near-distance hiding, labels, and off-screen
arrows.

If `Camera::main()` comes back null, the camera hasn't spawned yet, the tag
is wrong, or projection itself fails, the manager just skips the entry
rather than drawing garbage. Go diagnose the camera. Don't paper over a
failed projection with a fake `(0, 0)`.

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

Build the `Style` once, when the marker is created. Rebuilding and
resubmitting an unchanged style every frame is just waste.

### Update policies and cost

The projection policy decides how often world entries actually get
refreshed:

```cpp
ModUI::Highlight::UpdatePolicy policy{};
policy.mode = ModUI::Highlight::UpdateMode::Budgeted;
policy.max_updates_per_frame = 20;
policy.projection_interval_frames = 2;
policy.camera_resolve_interval_frames = 30;
policy.transform_validation_interval_frames = 30;

ModUI::Highlight::set_update_policy(policy);
```

Set this once, in `ModRuntime::start()`, since it runs before
`ModHooks::install()` brings the render hook up in the generated lifecycle.
`set_update_policy` isn't a queued per-frame command, it mutates the manager's
configuration directly.

| Mode | Behaviour | Suitable use |
| --- | --- | --- |
| `EveryFrame` | Every eligible target is reprojected every render frame | A small set of fast-moving markers |
| `Budgeted` | Refreshes up to the configured budget and reuses cached projections | General-purpose overlays |
| `EventDriven` | Refreshes only when dirty or explicitly moved | Static/event-driven markers |

Setting `max_updates_per_frame` to `0` removes the per-frame limit; it
doesn't turn updates off. If you're tuning a lot of markers, watch
`last_frame_stats()`: a high cached-draw count is normal in budgeted mode,
projection failures are not.

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

Never call `manager().render()` from a feature or the menu. Rendering twice
in one frame doesn't just waste work, it breaks the generated hook's
ownership model.

### Highlight diagnostics API

Turn diagnostics on temporarily while you're bringing an overlay up:

```cpp
ModUI::Highlight::set_diagnostics([](const char* line) {
  ModLog::info("%s", line ? line : "");
});
ModUI::Highlight::set_verbose_diagnostics(true);
ModUI::Highlight::set_diagnostic_throttle_frames(120);
```

Same deal as the update policy: register these in `ModRuntime::start()`,
since they're direct manager configuration, not queued mutations.

Turn verbose mode off again for release unless the feature genuinely
needs it left on. The manager reports states like a missing or dead
Transform, no projection, projection failure, an invalid rectangle,
off-screen, too close, and removal.

When something's not showing up, work down this list:

1. Does the log say a DX11, DX12, or OpenGL render hook was installed?
2. Did ImGui initialize on a compatible game swap chain/context?
3. On DX12, was a direct command queue captured?
4. Is the `HighlightId` non-zero and still owned by the feature?
5. Does the manager's target count increase?
6. Is `Camera::main()` valid and is the point in front of the camera?
7. Are projection failures increasing in `last_frame_stats()`?
8. Is the style actually configured to draw a box, label, or indicator?

Menu and highlight both invisible? Start with the native render hook. Menu
visible but no highlight? The DirectX/ImGui path already works, so look at
marker ownership, projection, camera, and style instead.

## 13. Use coroutines to spread work across frames

The generated lifecycle ticks frame-based coroutines through `ModAsync`, so
you don't need a worker thread just to wait a few frames on the Unity main
thread.

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

Coroutine work advances out of the `ModRuntime::update()` flow, and shutdown
cancels tasks automatically. That said, if a coroutine captured a borrowed
Unity handle before suspending, call `alive()` again once it resumes; time
passed, and the object might not have.

A coroutine won't turn blocking I/O or heavy CPU work into something
non-blocking, either. Run that on a real worker and hand only plain results
back to the main thread.

## 14. Add hooks only when normal calls are not enough

Reach for a hook only once normal Unity API access genuinely can't do what
you need. Normal access already covers:

- `ModRuntime::update()` polling;
- field and property access;
- managed method calls;
- scene callbacks;
- object-destroy request callbacks.

A hook actually earns its place when you need the exact moment a managed
method runs, when a parameter or return value has to change mid-call, or
when polling would just miss a short-lived event entirely.

Before you install one, get four things right: the runtime backend, static
versus instance method semantics, every parameter and the return type, and
the native ABI. Miss one of those and a bad hook doesn't politely fail, it
can crash the process on the spot.

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

For an IL2CPP instance method, `self` comes first. Generated native methods
usually carry a trailing `MethodInfo*` too, shown here as `void* method_info`.
But value-type instance methods, struct returns, and some Unity/IL2CPP
versions can differ on ABI details, so verify the real native signature.
Don't just copy this typedef and assume.

Notice the detour does nothing but set a flag and call the original, no scan,
no allocation, no ImGui work in sight. `ModRuntime::update()` reads that flag
later and marks the feature cache dirty on its own time.

### Mono difference

A compiled Mono method skips IL2CPP's trailing `method_info` argument
entirely:

```cpp
using TickFn = void(*)(void* self, float delta);
```

The Mono installation flow is:

1. Resolve the exact method with `URK::mono::helpers::require_method_exact`.
2. Get the native target with `URK::mono::compile_method`.
3. Attach it with `URK::hooks::attach_ex`.
4. Remove it with the same original/detour pair through `detach_ex`.

Don't reach for IL2CPP helpers in a Mono project, or a Mono ABI in an IL2CPP
one; they're not interchangeable.

### Centralize hook ownership

Give every hook one owner, `mod/hooks/mod_hooks.cpp`:

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

`URK::hooks::HookSet` can hold several raw targets at once. If a later
required hook fails, detach the ones you already installed, and make
`install()` safe to call twice. If detach itself fails, don't clear your
state and claim success anyway; unloading the DLL at that point might not be
safe at all.

### Pick a hook backend

`URK_HOOK_BACKEND_DETOURS` is the default, and it handles ordinary function
entry points fine. For a target Detours can't rewrite, there's
`URK_HOOK_BACKEND_SAFETYHOOK`: check
`URK::hooks::backend_available(URK::hook_backend_safetyhook)` first, then
select it via `URK::hooks::attach_ex(&original, detour,
URK::hook_backend_safetyhook)`.

One target, one backend, always. Whichever hook attaches first locks it in,
and a later attach naming a different backend just gets rejected. Hooks on
the same target chain in attach order: each detour calls the trampoline it
was handed, which reaches the previously attached detour and eventually the
original code. So a second mod hooking the same function doesn't fail, it
just joins the chain.

### Hook in the middle of a function

Some targets just don't have a usable entry point: an inlined body, a hybrid
native backend, a branch you only want to observe halfway through. A
mid-function hook attaches to an arbitrary instruction boundary instead and
hands the callback the whole register file.

```cpp
#include "sdk/hook_api.h"

namespace {

URK::hooks::MidHook g_damage_clamp;

void on_damage(URK_HookRegisters* regs, void* user) {
  auto* state = static_cast<ModState*>(user);
  // xmm0 holds the incoming damage float at this address.
  if (regs->xmm[0].f32[0] > state->cap)
    regs->xmm[0].f32[0] = state->cap;
}

} // namespace

bool attach(const URK_ModContext* context) {
  if (!URK::hooks::mid_available())
    return false;
  return g_damage_clamp.attach(reinterpret_cast<void*>(target_address), &on_damage, &g_state);
}
```

Rules that matter:

- Check `URK::hooks::mid_available()` first. It reports false on a loader built
  without SafetyHook, and on a loader older than SDK 32.
- Writes to the register struct are copied back, so the callback can change
  `rax`, `rcx`, the `xmm` bytes, and `rip`.
- `rsp` is read-only. To move the stack, write `trampoline_rsp` and make sure
  the address you want to resume at sits on top of it.
- On entry `rip` points at a trampoline holding the displaced instruction(s),
  not at the address you hooked.
- The callback runs on whatever thread hit the address, and it runs on every
  hit. Keep it short and do not block; queue work to the main thread instead.
- `URK::hooks::MidHook` detaches in its destructor. The loader also releases
  any mid hook owned by a mod module when that module unloads.
- The loader has a fixed pool of 128 mid hook slots for the whole process.

A mid hook can also read managed instance state with no separate managed call
at all: just combine `this` from a GPR with `field_offset`. Here's that
tested against a small, real IL2CPP build, on a method shaped like this:

```csharp
public class SimpleHookTest : MonoBehaviour {
  public int score = 0;

  void SpawnTarget() {
    GameObject target = GameObject.CreatePrimitive(PrimitiveType.Cube);
    // ...
  }
}
```

`score` is a sibling field on the instance, not a local inside `SpawnTarget`,
which matters: a mid hook can't read a local the method hasn't computed yet
at the address it's attached to. So the hook goes at `SpawnTarget`'s entry,
where `this` is already valid:

```cpp
void on_spawn(URK_HookRegisters* regs, void*) {
  auto* self = reinterpret_cast<uint8_t*>(regs->rcx); // instance methods pass `this` in rcx
  int score = *reinterpret_cast<int*>(self + URK::il2cpp::field_offset(g_score_field));
  ModLog::info("score at spawn: %d", score);
}

g_mid.attach(URK::il2cpp::method_pointer(spawn_target_method), &on_spawn);
```

It fired on every single call and read the live score correctly every time:

```text
[SafetyHook mid] SpawnTarget() call #1 this=000001DA7FCEF740 score=0 rip=00007FF8F6DF01A2
[SafetyHook mid] SpawnTarget() call #2 this=000001DA7FCEF740 score=10 rip=00007FF8F6DF01A2
[SafetyHook mid] SpawnTarget() call #3 this=000001DA7FCEF740 score=10 rip=00007FF8F6DF01A2
```

`this` stays constant (one `SimpleHookTest` instance in the scene), `score`
moves as the player scores, and `rip` never changes because the hook always
resumes at the same trampoline address. A separate inline hook
(`URK_HOOK_BACKEND_SAFETYHOOK`) on a small, unrelated method attached and ran
fine in the same test. One caveat did turn up, though: inline-hooking
`SpawnTarget` itself, a bigger method full of embedded constants and calls,
with the SafetyHook backend made the game misbehave after a while. Mid-hooking
that exact same method didn't. Until that's root-caused, prefer Detours or a
mid hook on methods shaped like that.

## 15. Persist settings

Small constants and runtime settings are fine living in `mod_config.h`. Just
resolve a persistent settings path relative to the mod DLL, not to the
process's current working directory, or you'll find it somewhere you didn't
expect.

A configuration layer you can trust does all of this:

- treat a missing file as a valid first-run state;
- validate parsed types and numeric ranges;
- report malformed lines instead of silently inventing values;
- write to a temporary file first;
- flush successfully before atomically replacing the real file;
- retain a useful last error for the log or UI.

Don't write it every frame, though. Save when a checkbox or slider actually
changes, or on orderly shutdown. If dragging a slider triggers too many
writes, save on edit completion or after a short debounce instead.

If the generated lifecycle already has a configuration store loading and
saving, extend that one. Don't add a second owner for the same settings.

## 16. Strings, arrays, and managed lifetime

Pass a `std::string_view` through a high-level wrapper and it creates the
managed string that call needs, automatically:

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

Never hang onto a raw Mono or IL2CPP string-buffer pointer. Copy the UTF-8
value out and use the matching helper to free runtime-owned temporary storage
when it's needed.

Wrapper handles are always borrowed. If a managed object genuinely needs to
outlive scene ownership and ordinary managed references, that's what the
backend GC-handle API is for, freed on shutdown. Don't reach for GC handles
on your first feature, though, when plain scene ownership already covers it.

For array-returning Unity searches, reach for the SDK's scoped leases instead
of juggling one handle per result. `FindObjectsOfTypeRooted<T>()`,
`FindObjectsOfTypeAllRooted<T>()`, and the rooted component-query variants
all keep the managed result array alive behind one move-only RAII owner, and
free it automatically once that owner goes out of scope.

Never guess the layout of a managed value type; use the SDK's own
definitions for `Vector3`, `Quaternion`, `Color`, and the rest. For a custom
game struct, verify size, alignment, and field layout yourself before writing
a matching C++ type.

## 17. Diagnostics by API layer

Work through the API layers in this order. Each stage rules out one source
of failure before the next subsystem even enters the picture:

1. Generated project compilation.
2. Loader DLL discovery.
3. `ModRuntime::start()` execution.
4. `ModRuntime::update()` delivery and runtime capability inspection.
5. GameObject lookup, without requiring a scene callback first.
6. Optional scene callback delivery when `URK::has_scene_events()` is true.
7. Component lookup.
8. Field or property read.
9. Field write or managed method call.
10. Plain menu request publication.
11. Managed/native hook installation, when normal API access is insufficient.

Stack five new layers on at once and a crash becomes much harder to pin
down.

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

### A scene callback never fires

- The loader's `scene events=yes/no` capability line is checked.
- `URK::has_scene_events()` is checked; resolved mod exports do not guarantee
  runtime event delivery.
- Discovery still runs from `ModRuntime::update()` and cached wrappers use
  `alive()` because an object can spawn or die without a scene change.

On IL2CPP, seeing `URK_SceneInfo::buildIndex == -1` is expected and doesn't
by itself mean scene events are unavailable. It just means the optional
build-index binding got stripped, use the scene name and handle instead.

### Unity input always returns false

- `Unity::Input::available()` is checked before reading a key or mouse button.
- The loader's `input=yes/no` capability line is checked.
- Input polling occurs from `ModRuntime::update()`, not from the render thread.

An unavailable input service and a key nobody's pressing look identical:
both return `false`. Confirm availability first, before you go chasing key
state.

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

Deleting an error check, adding an empty catch, or using an address you never
validated doesn't fix any of this. It just hides the failure until it shows
up later, as a crash you now have no way to diagnose.

## 18. Release validation

Before you ship: a clean Release configure/build, first launch, game
shutdown, and a scene transition if the target game has one. Also test a
late object spawn or replacement that happens without any scene transition
at all. Then check these hold:

- Supported runtime unload detaches all hooks.
- Shutdown clears highlights, commands, and caches.
- Scene-owned handles do not survive into a later scene.
- Destroyed or replaced handles are rediscovered when the active scene does not
  change.
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
3. Confirm `ModRuntime::update()` runs and record the runtime capabilities.
4. Log the scene name when scene events are available; do not block object
   discovery on that callback.
5. Find one GameObject.
6. Find one component.
7. Read one value and add error handling.
8. Write a small wrapper for that type.
9. Move the feature into its own `.h/.cpp` module.
10. Add caching, `alive()` validation, and optional scene-event reset.
11. Publish plain requests from the menu and process them in `update()`.
12. Return status to the menu through a snapshot.
13. Measure before choosing polling intervals.
14. Add a hook only if normal calls are insufficient.
15. Test scene transitions, same-scene object replacement, and shutdown.

That's really the whole job: resolve the right object and member, do Unity
work on the right thread, and let go of everything you own when you shut
down.
