# URKit Mod Author Guide

This guide explains how to create a URKit mod without requiring knowledge of
the loader internals or every part of the Unity engine.

The generated `sdk/mod_sdk.h` is still the final word on the ABI. If a future
SDK changes a function and this guide has not caught up yet, trust the header.

## What URKit is

URKit is a native C++ modding kit for Windows x64 Unity games. It works with
Mono and IL2CPP games and gives both backends the same everyday `Unity::`
interface.

Anyone can create and publish a mod with it. The loader is closed-source and is
distributed as a compiled binary. Its job is limited: start the correct Unity
backend, load compatible mod DLLs, and provide services such as hooks,
main-thread callbacks, input, cursor control, and HTTPS. URKit helps your mod
talk to the game; it does not own, approve, or take responsibility for the mod
you build.

A URKit mod is a loader plugin, not a standalone injectable DLL. Do not inject
the generated DLL directly.

## Install URKit

The public Windows x64 package contains `urk-sdk.exe`, the proxy DLLs
`version.dll`, `winhttp.dll`, and `winmm.dll`, plus the optional proxy-free
loader `URKitInjected.dll`.

- Keep `urk-sdk.exe` wherever you create mod projects.
- Put one proxy DLL in the game folder: choose the filename imported by that
  game's executable. Do not rename it or copy all three proxies into one game.
- Put the mod DLL produced by your generated project in the game's `Mods`
  folder.

If the game has no suitable proxy import, `URKitInjected.dll` is the dedicated
proxy-free loader DLL for compatible external loading workflows. When it is
loaded into a supported x64 game, it reads `<GameDir>/URKit_config.ini` and
loads mods from `<GameDir>/Mods` by default. URKit does not ship an injector.
This loads the URKit loader, not a generated mod DLL; generated mods must
continue to be installed in `Mods`.

The released files are self-contained. No Visual C++ redistributable or other
third-party DLL is required on the target PC.

The Explorer-enabled package contains
`Mods/URK_Il2cpp_UnityRuntimeExplorer.dll`. Copy that `Mods` folder to an
IL2CPP game's folder after installing the matching proxy. It is not compatible
with Mono games.

## Generate a project

Open `urk-sdk.exe`, select the game's `.exe`, choose a backend, and enter the
mod name. `Auto` is normally fine: `GameAssembly.dll` means IL2CPP, while a
Mono runtime beside the game means Mono.

The command-line form is useful for scripts:

```powershell
./urk-sdk.exe --game-exe C:\Games\Example\Example.exe --backend auto --name MyMod
```

Add `--localization` to create editable locale files. The project appears at
`<GameDir>/urk-sdk-output/<Project>/project`.

You need Windows x64, CMake 3.28 or newer, LLVM/Clang, and Ninja. From the
generated directory:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

Use `clang-release` when you are ready to ship. Re-run the configure command
after adding a new source file under `mod/`.

## Where to write the mod

The generator deliberately separates your files from its files.

| File | Put this here |
|---|---|
| `mod/lifecycle/mod_runtime.cpp` | Game lookup, cached state, main-thread updates, and scene handling. Start here. |
| `mod/hooks/mod_hooks.cpp` | Hook targets, detours, install state, and detach logic. |
| `mod/lifecycle/mod_network.cpp` | Network startup and your request policy. |
| `mod/support/mod_log.cpp` | Shared logging behaviour. |
| `mod/config/mod_config.h` | Name, author, version, URL, menu key, and simple settings. |
| `mod/ui/theme.h` | Persistent ImGui colours, spacing, radii, and fonts. |
| Any new file under `mod/` | Your own features. These files are discovered on reconfigure. |

The lifecycle is small on purpose:

```cpp
namespace ModRuntime {
bool start(const URK_ModContext* ctx); // once, after the backend is ready
void update();                         // Unity main thread
void on_scene_loaded(const URK_SceneInfo* scene);
void on_scene_changed(const URK_SceneInfo* previous,
                      const URK_SceneInfo* current);
void on_object_destroy_requested(const URK_ObjectDestroyRequest* request);
void stop();                           // release everything you own
}
```

Files under `sdk/`, `mod/generated/`, generated UI support, CMake profiles, and
editor settings may be replaced on regeneration. Keep hand-written work out of
them. Existing user-owned lifecycle, hook, config, log, theme, and locale files
are preserved.

## The Unity model in two minutes

If Unity is new to you, keep these four types straight:

- An `Object` is the base managed Unity object. Assets, components, materials,
  and GameObjects all eventually come from it.
- A `GameObject` is an item in a scene hierarchy: a player, camera, door,
  canvas, light, and so on. On its own it is mostly a container.
- A `Component` gives a GameObject behaviour or data. `Camera`, `Renderer`,
  `Collider`, `Animator`, and game scripts are components.
- Every GameObject owns a `Transform`. The transform stores position, rotation,
  scale, parent, and children.

In normal URKit code the flow looks like this:

```cpp
#include "sdk/unity/unity.h"

Unity::GameObject player = Unity::GameObject::FindWithTag("Player");
if (!player)
    return;

Unity::Transform transform = player.transform();
Unity::Vector3 position = transform.position();
position.y += 1.0f;
transform.set_position(position);
```

The wrappers only hold managed handles. `if (object)` checks whether the handle
is non-null; `object.alive()` also asks Unity whether a previously referenced
object has been destroyed. A lookup returning an empty wrapper is normal during
loading or after a scene change.

## Three rules that prevent most crashes

First, do Unity work on the Unity main thread. `ModRuntime::update()` and the
scene callbacks are the right places. A hook may run on another thread; queue
the result and apply Unity changes from `update()`.

Second, treat every lookup as fallible. Log `Unity::last_error()` when a result
should have existed:

```cpp
Unity::GameObject player = Unity::GameObject::Find("PlayerRoot");
if (!player) {
    ModLog::warn("PlayerRoot not found: %s",
                 Unity::last_error() ? Unity::last_error() : "no detail");
    return;
}
```

`Unity::clear_error()` clears the current helper diagnostic. Each helper call
also clears stale errors before doing its own work.

Third, rebuild scene-owned state after a scene change and release it in
`stop()`. Do not assume a managed handle survives a scene load. Use
`Unity::Inspect::PinObject`, `WeakObject`, and `FreeObjectHandle` only when you
really need a GC handle; they do not make a destroyed Unity object alive again.

## GameObject operations

### Finding objects

```cpp
auto player = Unity::GameObject::Find("PlayerRoot");
auto enemy = Unity::GameObject::FindWithTag("Enemy");
auto enemies = Unity::GameObject::FindGameObjectsWithTag("Enemy");
```

`Find` follows Unity's name/path lookup and normally sees active objects.
`FindWithTag` returns one active object with the tag.
`FindGameObjectsWithTag` returns all active matches. Tags must exist in the
game; an unknown tag can produce a managed exception and an empty result.

For inactive scene objects, walk scene roots instead:

```cpp
for (Unity::GameObject root : Unity::SceneManager::GetLoadedSceneRoots()) {
    // Inspect the hierarchy from each root.
}

auto allSceneObjects = Unity::SceneManager::FindSceneGameObjects(true);
```

`FindSceneGameObjectsFiltered(flags)` and
`GetLoadedSceneRootsFiltered(flags)` accept `ObjectFilterFlags` to include
inactive, hidden, or DontDestroyOnLoad objects. `FindSceneGameObjects(true)` is
the convenient include-inactive form.

### Name, tag, activity, scene, and transform

Every GameObject inherits `Object::name()`, `ToString()`,
`runtime_class_name()`, `GetInstanceID()`, `hideFlags()`, and `alive()`.
GameObject adds:

- `tag()` reads the tag;
- `activeSelf()` reads its own active flag;
- `activeInHierarchy()` tells whether it is actually active through its
  parents;
- `SetActive(bool)` enables or disables it;
- `scene()` returns the owning `Scene`;
- `transform()` returns its Transform.

There is no special name setter because the general property helper already
does it:

```cpp
player.SetProperty("name", "Debug Player");
player.SetProperty("tag", "Player");
```

### Components

Typed component calls are the cleanest option for built-in wrappers:

```cpp
Unity::Animator animator = player.GetComponent<Unity::Animator>();
Unity::Collider collider = player.GetComponentInChildren<Unity::Collider>(true);
auto renderers = player.GetComponentsInChildren<Unity::Renderer>(true);

if (!player.HasComponent<Unity::AudioSource>())
    player.AddComponent<Unity::AudioSource>();

Unity::AudioSource audio = player.GetOrAddComponent<Unity::AudioSource>();
```

The complete family is `GetComponent`, `GetComponentInChildren`,
`GetComponentInParent`, `GetComponents`, `GetComponentsInChildren`,
`GetComponentsInParent`, `AddComponent`, `HasComponent`, and
`GetOrAddComponent`. The same helpers are available on `Component` and forward
through its GameObject.

For a game-specific script, give the assembly image, namespace, and class:

```cpp
Unity::Object health = player.GetComponent(
    "Assembly-CSharp.dll", "Game.Characters", "Health");
```

The string overload `GetComponent<T>("Full.Type.Name")` calls Unity's legacy
string lookup. Prefer the metadata form above when you know the assembly and
namespace.

### Creating, cloning, and destroying

```cpp
auto marker = Unity::GameObject::Create("My Marker");
auto uiRoot = Unity::GameObject::CreateUi("My Canvas Root");
auto clone = Unity::Object::Instantiate(player);

Unity::Object::DontDestroyOnLoad(marker);
Unity::Object::Destroy(marker);          // normal deferred destroy
Unity::Object::Destroy(clone, 2.0f);     // delayed destroy
```

`Create()`/`New()` make a GameObject; `CreateUi()` creates one with a
`RectTransform`. `Instantiate` has overloads for parent, world-space parenting,
position/rotation, and position/rotation/parent. `DestroyImmediate` also exists
but should be rare during play; it can invalidate state while code is still
walking it.

## Object lookup and lifetime operations

`Unity::Object` can find managed instances by type:

```cpp
auto oneCamera = Unity::Object::FindObjectOfType<Unity::Camera>();
auto cameras = Unity::Object::FindObjectsOfType<Unity::Camera>();
auto sorted = Unity::Object::FindObjectsByType<Unity::Camera>(
    Unity::FindObjectsSortMode::InstanceID);
auto everyMaterial = Unity::Object::FindObjectsOfTypeAll<Unity::Material>();
```

The full set is:

- `FindObjectOfType` / `FindObjectsOfType` for normal loaded instances;
- `FindObjectsByType` when Unity exposes the newer sorted API;
- `FindObjectOfTypeAll` / `FindObjectsOfTypeAll` for Resources-style lookup,
  including objects regular scene lookup may omit;
- `FindObject`, `FindInstance`, `FindInstances`, and `FindAllInstances` as
  readable aliases.

Each has a typed overload and an assembly/namespace/class overload. Do not run
these broad searches every frame. Find once after the scene becomes ready,
validate with `alive()`, and refresh when the scene changes.

`ScriptableObject::CreateInstance(TypeRef)` and its typed/string overloads
create ScriptableObjects. `Object::Destroy`, `DestroyImmediate`,
`DontDestroyOnLoad`, and all `Instantiate` overloads work on any compatible
wrapper.

## Transform operations

```cpp
Unity::Transform t = player.transform();

Unity::Vector3 world = t.position();
Unity::Vector3 local = t.localPosition();
Unity::Vector3 angles = t.eulerAngles();
Unity::Quaternion rotation = t.rotation();

t.set_position({10.0f, 2.0f, -4.0f});
t.set_localPosition({0.0f, 1.0f, 0.0f});
t.set_eulerAngles({0.0f, 90.0f, 0.0f});
t.set_localScale({1.2f, 1.2f, 1.2f});
```

World-space helpers are `position`, `set_position`, `rotation`,
`set_rotation`, and `eulerAngles`/`set_eulerAngles`. Local helpers are
`localPosition`, `set_localPosition`, `localScale`, and `set_localScale`.
Read-only direction/scale helpers are `forward`, `right`, `up`, and
`lossyScale`.

Hierarchy helpers are `parent`, `set_parent`, `SetParent`, `root`,
`childCount`, `GetChild`, and `Find`. `Transform::Find("Body/Head")` accepts a
path relative to that transform. Prefer `SetParent(parent,
worldPositionStays)` when the difference between local and world position
matters.

`Vector2` and `Vector3` provide arithmetic, `magnitude`, `sqr_magnitude`,
`normalized`, `normalize`, `nearly_zero`, `dot`, and `distance`; `Vector3`
also provides `cross`. URKit also maps `Quaternion`, `Vector4`, `Color`,
`Color32`, integer vectors, `Rect`, `Bounds`, and `Ray`.

## Camera, screen, and projection

```cpp
Unity::Camera camera = Unity::Camera::main();
if (!camera)
    camera = Unity::Camera::current();

Unity::Vector3 screen3 = camera.WorldToScreenPoint(player.transform().position());
Unity::ProjectionResult p = Unity::project_transform(camera, player.transform());
```

Camera exposes `main`, `current`, `fieldOfView`/`set_fieldOfView`,
`nearClipPlane`, `farClipPlane`, `aspect`, `pixelWidth`, `pixelHeight`,
`WorldToScreenPoint`, `ScreenToWorldPoint`, `WorldToViewportPoint`,
`ViewportToWorldPoint`, and `ScreenPointToRay`.

`Unity::Screen` provides `width`, `height`, `dpi`, `size`, `center`,
`contains`, and `clamp`. The higher-level projection helpers are:

- `project_world` and `project_transform`, with optional Camera and edge
  padding;
- `world_to_overlay`, which converts to ImGui's top-left screen coordinates;
- `world_visible`, which checks on-screen and facing state;
- `direction_to_screen_edge`, which places an off-screen indicator.

`ProjectionResult` includes world, screen, viewport and clamped positions,
direction, screen center, depth, distance, facing, `in_front`, `on_screen`, and
`valid`. Use these helpers for overlays instead of manually flipping Y and
guessing whether a point is behind the camera.

## Fields, properties, and method calls

This is the escape hatch for game-specific scripts. Wrap a managed handle in
`Unity::Object`, then use the actual managed member names.

```cpp
Unity::Object health = player.GetComponent(
    "Assembly-CSharp.dll", "Game.Characters", "Health");
if (!health)
    return;

float current = health.GetField<float>("currentHealth");
health.SetField("currentHealth", 100.0f);

bool invulnerable = health.GetProperty<bool>("Invulnerable");
health.SetProperty("Invulnerable", true);

health.Call<void>("Heal", 25.0f);
```

Use the C++ type that matches the managed type. Common mappings are `bool` to
`System.Boolean`, `int` to `System.Int32`, `float` to `System.Single`,
`double` to `System.Double`, strings to `System.String`, Unity wrappers to
managed object references, and URKit structs to their Unity value types.

`SetField` and `StaticSetField` support both value types and managed reference
types, including `nullptr` for a reference field. Check `Unity::last_error()`
when a write fails. Very large or invalid managed strings are rejected instead
of being copied from unsafe memory.

### Which call helper to use

| Helper | Use it for |
|---|---|
| `Call<Ret>(name, args...)` | An instance method whose parameter types URKit can infer, or a method that is unambiguous by argument count. |
| `CallExact<Ret>(name, {types...}, args...)` | An overloaded instance method. The strings are full managed parameter type names. |
| `GetField<T>` / `SetField` | An instance field. |
| `StaticGetField<T>` / `StaticSetField` | A static field on a `TypeRef`. |
| `GetProperty<T>` / `SetProperty` | An instance property; these call `get_...` and `set_...`. |
| `CallArrayExact<T>` | A method returning an object/reference array. |
| `CallStringArrayExact` | A parameterless method returning `string[]`. |
| `SetReferenceArrayProperty` | A property whose value is an array of wrapper references. |
| `InvokeGeneric<Ret>` | A genuine generic instance method. It uses reflection and is intentionally heavier. |

Use `CallExact` whenever two overloads have the same parameter count:

```cpp
health.CallExact<void>(
    "SetOwner",
    {"Game.Characters.Player", "System.Boolean"},
    playerScript,
    true);
```

Static built-in operations are already exposed by wrappers such as
`Camera::main`, `Shader::Find`, and `Animator::StringToHash`. For an arbitrary
game-specific static method, use `Unity::Inspect::Methods(TypeRef)` and
`Unity::Inspect::InvokeMethod({}, method, arguments)`, or the backend's exact
metadata/runtime API when writing a hook. There is intentionally no unsafe
“call address and hope” helper.

### Generic methods

`InvokeGeneric` needs managed `System.Type` objects for its generic type
arguments:

```cpp
Unity::TypeRef itemType{
    "Assembly-CSharp.dll", "Game.Inventory", "InventoryItem"};
Unity::TypeObject itemTypeObject{itemType.resolve_type_object()};

Unity::Object item = inventory.InvokeGeneric<Unity::Object>(
    "GetItem", {itemTypeObject}, 42);
```

The helper finds the generic method by name and ordinary argument count,
creates its closed generic form through reflection, boxes value arguments,
and invokes it. It requires at least one generic type. Do not put it in a hot
per-frame loop; cache the result or use exact backend metadata for a performance
critical path.

## Reflection and inspection

Include `sdk/unity/unity_inspect.h` for explorers, debug panels, or a mod that
does not know a game's members ahead of time.

`Unity::Inspect` provides:

- type/object description: `DescribeClass`, `TypeOf`, `DescribeObject`, and
  `ExpandValue`;
- metadata lists: `Fields`, `Methods`, and `Properties`, optionally including
  inherited members;
- reads: `ReadField`, `ReadProperty`, and `ReadArrayElement`;
- writes: `SetField`, `SetProperty`, and `SetArrayElement`;
- invocation: `InvokeMethod`, including static methods when the selected
  `MethodInfo::is_static` is true;
- handles: `PinObject`, `PinValue`, `WeakObject`, `ResolveObjectHandle`, and
  `FreeObjectHandle`;
- diagnostics: `DumpFields`, `DumpMethods`, and `DumpProperties`.

Values are returned as `ValueInfo` with a `ValueKind`: unavailable, null,
boolean, signed/unsigned integer, floating point, string, object reference,
array reference, enum, or value type. This layer is deliberately descriptive
and safe. For known members, the typed `GetField`, `SetField`, `CallExact`, and
property helpers are shorter and faster.

## Rendering, materials, assets, physics, animation, and audio

These wrappers follow Unity's names closely. The list below is also the quick
API index for the non-UI helpers.

- `Material`: `shader`, `color`, `mainTexture` and their setters; `GetFloat`,
  `SetFloat`, `GetColor`, `SetColor`, `GetTexture`, `SetTexture`, and
  `HasProperty`.
- `Shader`: `Find`, `PropertyToID`, `isSupported`, `maximumLOD`,
  `set_maximumLOD`, and `renderQueue`.
- `Texture`: `width`, `height`, `anisoLevel`, `set_anisoLevel`, `mipMapBias`,
  and `set_mipMapBias`. `Texture2D` adds `mipmapCount`, `GetPixel`, `SetPixel`,
  `Resize`, and `Apply`.
- `Renderer`: bounds/local bounds, enable/visibility/force-off flags, shadow
  and occlusion settings, sorting layer/order, material/shared material,
  material arrays, motion vectors, light probes, and reflection probes.
- `SkinnedMeshRenderer`: mesh, bones, root bone, blend-shape weights, quality,
  off-screen updates, motion vectors, and `BakeMesh`. `MeshRenderer` inherits
  Renderer. `MeshFilter` and `MeshCollider` expose mesh/shared mesh; collider
  also exposes `convex`.
- `Light`: type, colour/temperature, intensity/bounce, range/spot angles,
  cookie, shadow settings, culling mask, and render mode.
- `Collider`: `bounds`, `enabled`, and `set_enabled`.
- `Rigidbody`: `velocity`, `set_velocity`, `angularVelocity`, and its setter.
  `Rigidbody2D` exposes the 2D velocity and scalar angular velocity equivalents.
- `Animator`: speed, root motion, delta movement/rotation, state flags, layer
  information, culling/update mode, avatar/controller, float/int/bool/trigger
  parameters, `Play`, `CrossFade`, `StringToHash`, `Update`, and `Rebind`.
- `AudioSource`: clip, volume, pitch, spatial blend, playback time, loop, mute,
  play-on-awake, `isPlaying`, `Play`, `PlayDelayed`, `PlayOneShot`, `Pause`,
  `UnPause`, and `Stop`.
- `AssetBundle`: `LoadFromFile`, `Contains`, typed/untyped `LoadAsset`,
  `LoadAllAssets`, `GetAllAssetNames`, `GetAllScenePaths`, and `Unload`.
- `Mesh` and `Sprite` are lightweight object wrappers so they can be passed to
  the other typed helpers.

Example:

```cpp
Unity::Renderer renderer = player.GetComponentInChildren<Unity::Renderer>();
Unity::Material material = renderer.material(); // Unity may instantiate a copy
if (material && material.HasProperty("_Color"))
    material.SetColor("_Color", {1.0f, 0.25f, 0.1f, 1.0f});
```

Be deliberate about `material()` versus `sharedMaterial()`: Unity's material
property can create a per-renderer instance. Use the shared version only when
changing every renderer that uses that asset is actually intended.

## Unity UI and the generated ImGui menu

URKit has two UI layers and they solve different problems.

The generated ImGui overlay is your mod menu. It supports DX11, DX12, and
OpenGL and is independent of the game's Canvas hierarchy. Toggle visibility
through `ModConfig::show_menu`; `ModConfig::menu_toggle_key` is a Win32 virtual
key and defaults to Tab. Edit `mod/ui/theme.h` for its look. The generated
render hook calls `ModUI::render_menu()` for you.

The `Unity::` UI wrappers edit or create the game's own Canvas UI. Use
`GameObject::CreateUi` or `CreateOverlayCanvas`:

```cpp
Unity::CanvasRoot root = Unity::CreateOverlayCanvas("My Mod Canvas");
if (!root)
    return;

root.scaler.set_uiScaleMode(Unity::CanvasScaleMode::ScaleWithScreenSize);
root.scaler.set_referenceResolution({1920.0f, 1080.0f});
Unity::Object::DontDestroyOnLoad(root.gameObject);
```

The Unity UI wrappers cover:

- hierarchy/layout: `RectTransform`, `LayoutRebuilder`, `LayoutElement`,
  horizontal/vertical/grid layout groups, `ContentSizeFitter`, and
  `AspectRatioFitter`;
- canvas/input: `Canvas`, `CanvasRenderer`, `CanvasGroup`, `CanvasScaler`,
  `GraphicRaycaster`, `EventSystem`, `BaseInputModule`,
  `StandaloneInputModule`, and `InputSystemUIInputModule`;
- graphics/text: `Graphic`, `Image`, `RawImage`, legacy `Text`, and
  `TextMeshProUGUI`;
- controls: `Selectable`, `Button`, `Toggle`, `Slider`, `Scrollbar`,
  `Dropdown`, `InputField`, `TmpInputField`, and `TmpDropdown`;
- clipping/scrolling: `Mask`, `RectMask2D`, and `ScrollRect`.

Getters and setters mirror Unity property names. Controls also expose the
important methods: `Button::Click`; without-notify setters for toggle, slider,
scrollbar, dropdown, and input fields; dropdown show/hide/clear/refresh;
input-field activate/deactivate/select-all; scroll stop; selection and pointer
checks on EventSystem; and immediate/marked layout rebuilds.

`EventSystem::EnsureStandalone()` returns the current EventSystem or creates a
legacy standalone one. Do not create a second event system when the game
already has one.

For the generated ImGui menu, `mod/ui/widgets.h` includes `checkbox`, `button`,
`slider_float`, `combo`, `toggle`, `tab_button`, `tab_indicator`, `key_value`,
`begin_card`, and `end_card`, plus the labelled-field and animation helpers.
`mod/ui/localization.h` provides `available_languages`, `active_language`,
`set_language`, `translate`, `format`, and `last_error_message`. Theme helpers
provide the palette/radius/spacing accessors, font setup, DPI scale, pulse,
gradient rectangle, glow circle, colour interpolation, and `apply`.

## Highlight overlays

The highlight helper is meant for ESP-style boxes, labels, world markers, and
off-screen arrows without making every mod author rebuild projection and
thread coordination.

Include it from code that owns highlight state:

```cpp
#include "ui/highlight.h"

namespace {
ModUI::Highlight::HighlightId g_player_highlight = 0;
}
```

### Add a target

`GameObject`, `Component`, and `Transform` targets follow the transform's world
position. World-point targets store a position directly. Screen rectangles are
already in ImGui coordinates and need no camera projection.

```cpp
ModUI::Highlight::Style style{};
style.draw_box = true;
style.corner_box = true;
style.draw_label = true;
style.offscreen_indicator = true;
style.color = IM_COL32(255, 90, 70, 235);

g_player_highlight = ModUI::Highlight::enqueue_add(
    player, "Player", style);
```

Use the `enqueue_` functions from `ModRuntime::update()`, gameplay hooks, and
worker callbacks. The overlay render hook owns the live highlight collection
and may run concurrently; queued commands are applied at the beginning of its
next frame. Direct `add`, `remove`, `clear`, `set_label`, and Manager mutation
functions are for code already executing on the render/overlay thread.

The thread-safe target functions are:

```cpp
enqueue_add(gameObject_or_component_or_transform, label, style);
enqueue_add_world_point(world, label, style);
enqueue_mark_dirty(id);
enqueue_mark_all_dirty();
enqueue_set_world_point(id, newWorld);
enqueue_remove(id);
enqueue_clear();
```

Each add returns a non-zero `HighlightId` on success. Store it; the ID is how
you update or remove that entry. A transform that is destroyed is detected and
eventually disabled. Still remove entries yourself when your feature no longer
owns them.

### Style

`Style` is intentionally explicit:

- box: `color`, `fill_color`, `width`, `height`, `rounding`, `thickness`,
  `corner_length`, `filled`, `draw_box`, `corner_box`, and `shadow` with
  `shadow_color`/`shadow_offset`;
- label: `draw_label`, `label_color`, `label_bg_color`,
  `label_border_color`, `label_above_box`, `label_show_offscreen`,
  `label_offset`, `label_rounding`, and `label_padding`;
- indicator: `offscreen_indicator`, `draw_behind_indicator`,
  `indicator_color`, `indicator_padding`, `indicator_length`,
  `indicator_thickness`, `indicator_head_size`, `indicator_center_gap`, and
  `indicator_center_dot_radius`;
- distance: `hide_within_distance`, `scale_with_distance`,
  `reference_distance`, `min_scale`, and `max_scale`.

If `draw_label` is false, passing a label is harmless but nothing is drawn.
`hide_within_distance` is useful for removing noisy markers close to the
camera. Distance scaling uses `reference_distance / actual_distance`, clamped
between `min_scale` and `max_scale`.

### Update policy

Projection calls cross the native/managed boundary, so a few hundred targets
should not all be recalculated blindly every frame.

```cpp
ModUI::Highlight::UpdatePolicy policy{};
policy.mode = ModUI::Highlight::UpdateMode::Budgeted;
policy.max_updates_per_frame = 20;       // 0 means unlimited
policy.projection_interval_frames = 2;
policy.camera_resolve_interval_frames = 30;
policy.transform_validation_interval_frames = 30;
policy.use_viewport_projection = false;
ModUI::Highlight::set_update_policy(policy);
```

- `EveryFrame` is fine for a small list and gives the most immediate motion.
- `Budgeted` spreads projection work across frames and draws the last cached
  result while an entry waits. It is the default.
- `EventDriven` only reprojects new or explicitly dirty entries. Call
  `enqueue_mark_dirty(id)` when a target moves and
  `enqueue_mark_all_dirty()` when the camera or projection changes.

`projection_interval_frames` limits how frequently one target is refreshed.
The camera and transform validation intervals reduce repeated managed lookups.
`use_viewport_projection` changes the projection path for custom camera
viewports.

### World points, screen rectangles, and advanced control

```cpp
auto pointId = ModUI::Highlight::enqueue_add_world_point(
    {12.0f, 1.5f, -4.0f}, "Loot", style);
ModUI::Highlight::enqueue_set_world_point(pointId, newPosition);
```

`add_screen_rect` accepts `ImVec2` or `Unity::Vector2` min/max coordinates and
must be called on the render thread. Through `manager()` the render thread can
also call `set_enabled`, `set_style`, `set_label`, `set_world_point`,
`set_screen_rect`, `set_screen_center`, `mark_dirty`, `mark_all_dirty`,
`remove`, and `clear`.

`set_projector_info(callback, user)` replaces the default world projection for
games with an unusual camera. The callback fills a complete
`ProjectionResult`. Pass `nullptr` to return to the built-in projector.

For tuning:

```cpp
ModUI::Highlight::FrameStats stats =
    ModUI::Highlight::last_frame_stats();
// targets, projection_updates, projection_failures, cached_projection_draws
```

`set_diagnostics`, `set_verbose_diagnostics`, and
`set_diagnostic_throttle_frames` report states such as missing/dead transforms,
projection failures, off-screen results, invalid rectangles, and removals
without flooding the log.

Always remove your IDs or call `enqueue_clear()` during feature shutdown.
Highlight entries do not transfer ownership of Unity objects to the overlay.

## Hooks

Hooks are the sharpest tool in the SDK. A correct hook needs four things: the
exact managed overload, the exact native calling signature, a validated
executable target, and a matching detach before unload. If any one of these is
unknown, leave the feature disabled and log why.

The generated `mod/hooks/mod_hooks.cpp` already owns a
`URK::hooks::HookSet g_hooks`. `HookSet::add` attaches and records a hook;
`detach_all()` removes recorded hooks in reverse order. It holds at most 64
entries and exposes `size`, `capacity`, and `full`.

The low-level hook helpers are `available`, `backend_available`, `attach`,
`attach_ex`, `detach`, `detach_ex`, and `as<T>`. `attach_ex` accepts either a
`URK_HookOptions` or a backend (`hook_backend_auto`, Detours, or SafetyHook when
available). In ordinary mod code, use `HookSet` or the managed IL2CPP helper.

### Work out the signature first

Do not select a method by name alone. Record:

1. assembly image, namespace, class, method name;
2. every managed parameter type in order;
3. return type;
4. whether it is static or instance;
5. the backend's native ABI, including hidden arguments.

An instance method receives `this`. IL2CPP compiled methods normally include a
trailing `MethodInfo*` argument; model it as `void*` unless you need the
metadata. Static methods omit `this`. Value types and struct returns can change
the native signature, so verify them rather than copying a nearby hook.

### Mono hook pattern

This example hooks `Game.Player.TakeDamage(System.Single) -> System.Void`.
Place it in `mod_hooks.cpp` of a Mono project; replace every identity and the
function type with the game's real method.

```cpp
namespace {
using TakeDamageFn = void(*)(void* self, float amount);
TakeDamageFn g_take_damage = nullptr;

void detour_take_damage(void* self, float amount) {
    ModLog::info("TakeDamage %.2f", amount);
    if (g_take_damage)
        g_take_damage(self, amount);
}

bool install_take_damage_hook() {
    const char* parameters[] = {"System.Single"};
    const URK::mono::Method* method = nullptr;

    if (!URK::mono::helpers::require_method_exact(
            "Assembly-CSharp.dll", "Game", "Player", "TakeDamage",
            parameters, 1, "System.Void", method,
            [](const char* text) { ModLog::warn("%s", text ? text : ""); }))
        return false;

    void* target = URK::mono::compile_method(method);
    if (!target)
        return false;

    g_take_damage = reinterpret_cast<TakeDamageFn>(target);
    return g_hooks.add(&g_take_damage, &detour_take_damage);
}
}
```

Call `install_take_damage_hook()` from `ModHooks::install`. The generated
`g_hooks.detach_all()` in `ModHooks::uninstall` handles cleanup. Exact lookup is
important: `resolve_method(name, argc)` cannot distinguish overloads with the
same argument count.

### IL2CPP hook pattern

For IL2CPP, use `Il2CppHook::attach`. It asks the loader to resolve the managed
descriptor, validate `MethodInfo::methodPointer`, and attach only when the
target is safe for the current runtime.

```cpp
namespace {
using TakeDamageFn = void(__fastcall*)(void* self, float amount,
                                       void* methodInfo);
TakeDamageFn g_take_damage = nullptr;
bool g_take_damage_installed = false;

void __fastcall detour_take_damage(void* self, float amount,
                                   void* methodInfo) {
    ModLog::info("TakeDamage %.2f", amount);
    if (g_take_damage)
        g_take_damage(self, amount, methodInfo);
}

bool install_take_damage_hook() {
    g_take_damage_installed = Il2CppHook::attach(
        "Assembly-CSharp.dll", "Game", "Player", "TakeDamage",
        {"System.Single"},
        &g_take_damage, &detour_take_damage,
        [](const char* text) { ModLog::warn("%s", text ? text : ""); });
    return g_take_damage_installed;
}

void uninstall_take_damage_hook() {
    if (g_take_damage_installed) {
        URK::hooks::detach_ex(
            reinterpret_cast<void**>(&g_take_damage),
            reinterpret_cast<void*>(&detour_take_damage));
    }
    g_take_damage = nullptr;
    g_take_damage_installed = false;
}
}
```

Call the custom uninstall function from `ModHooks::uninstall` before user state
is released. The typed `Il2CppHook::managed(...)`/`attach(spec, ...)` form is
useful when the same descriptor is reused. Lower-level helpers also provide
validated internal-call resolution/hooking and method-pointer diagnostics, but
managed attach should be the first choice.

### Calling the original and re-entry

The `original` variable becomes the trampoline after a successful attach. Call
it when the game should continue its normal behaviour. Do not accidentally call
the managed method through `Unity::Object::Call` from inside its own detour;
that enters the hook again.

Keep detours short. A detour may run on a render, physics, network, or worker
thread. Copy plain data into a synchronized queue and do wrapper calls in
`ModRuntime::update()` unless you have proved the callback is on the Unity main
thread.

Never recover from a failed lookup with a guessed offset. Game updates change
code addresses and private layouts. A disabled feature with a useful log is
better than corrupting the process.

## Scene, time, input, cursor, and runtime helpers

`Unity::SceneManager` exposes `GetActiveScene`, `sceneCount`, `GetSceneAt`,
`GetLoadedScenes`, loaded-root helpers, and scene-GameObject searches. A
`Scene` exposes `IsValid`, `isLoaded`, `buildIndex`, `handle_value`, `name`,
`path`, `isDontDestroyOnLoad`, `GetRootGameObjects`, and `rootCount`.

`Unity::Time` exposes `time`, `deltaTime`, `unscaledDeltaTime`, `timeScale`,
and `set_timeScale`.

`Unity::Input` exposes `available`, `GetKey`, `GetKeyDown`, `GetKeyUp`,
`GetMouseButton`, `GetMouseButtonDown`, and `GetMouseButtonUp`. Each accepts raw
integers or the `KeyCode`/`MouseButton` enums. These route through the loader's
safe input service; test `available()` when input is optional.

The lower-level `URK::` runtime helpers are:

- context: `set_context`, `context`, `context_has_field`,
  `runtime_api_has_field`, `runtime_capabilities`, and
  `has_runtime_capability`;
- capability checks: `has_mono_api`, `has_il2cpp_api`, `has_hooks`,
  `has_main_thread`, `has_scene_events`,
  `has_object_destroy_request_events`, `has_cursor_control`, `has_network`,
  `has_input`, `has_graphics_device`, and `has_steam_identity`;
- services: `graphics_device_type`, `steam_id64`, `current_scene`,
  `set_menu_cursor_open`, `cursor_state_get`, both `cursor_state_set`
  overloads, raw key/mouse helpers, and `log`.

Menu cursor ownership is reference-counted. Pair every
`set_menu_cursor_open(true)` with `set_menu_cursor_open(false)`. Prefer this to
forcing the cursor state every frame. Direct `cursor_state_get/set` is for a
feature that deliberately owns the full visibility and lock state.

Object-destroy callbacks describe a request, not proof that Unity completed
the destroy. `objectAddress` is diagnostic identity only; never dereference it
or turn it back into a live managed wrapper.

For native worker threads, Mono exposes `attach_current_thread`,
`thread_current`, and `thread_detach`. IL2CPP exposes `thread_current`,
`thread_attach`, and `thread_detach`; `il2cpp::helpers::ensure_thread_attached`
is the convenient guarded form. Detach only a thread your code attached, and do
it before that thread exits. Main-thread callbacks are already attached.

The backend helper headers also contain the exact metadata operations used by
the facade: class/image lookup; exact method and field resolution; method,
field, property, type, string, array, object, GC-handle, and runtime-invoke
helpers; plus `require_class`, `require_method`, `require_method_exact`,
`require_field`, `require_property`, `to_utf8`, and last-error diagnostics.
IL2CPP adds managed-hook, method-pointer, and internal-call validation helpers.
They are the lower-level path for inspectors and hooks, not a reason to bypass
the simpler `Unity::` calls in ordinary gameplay code.

## Events, coroutines, and HTTPS

`URK::events::Signal` provides `subscribe`, `unsubscribe`, `emit`, `clear`, and
`empty`. A subscription is a small ID token. `Changed<T>` provides `get`,
`changed`, and `assign`; it emits the previous and new value only when the value
really changes. Unsubscribe or clear signals before code owning callbacks is
unloaded.

`URK::coroutines` provides `Task`, `FlowState`, `next_frame`, `wait_for`,
`wait_until`, `set_error_handler`, `spawn`, `tick`, and `cancel_all`. Generated
mods normally use `ModAsync::spawn`, `ModAsync::flow`, and
`ModAsync::cancel_all`; lifecycle code ticks and cancels the shared flow.

```cpp
URK::coroutines::Task delayed_message() {
    using namespace std::chrono_literals;
    co_await URK::coroutines::wait_for(2s);
    ModLog::info("two seconds passed");
}

// From initialized mod state:
ModAsync::spawn(delayed_message());
```

Coroutine continuation runs when the flow is ticked; it does not make blocking
work asynchronous. Cancel owned tasks before their referenced state disappears.

`URK::network` provides `available`, `request_json`, `get_json`, `post_json`,
`put_json`, `update_json`, `patch_json`, and `delete_json`. Requests require
HTTPS. A `Response` contains `completed`, HTTP `status`, `body`, `error`,
truncation flags, and `ok()`.

```cpp
auto response = URK::network::post_json(
    "https://example.com/mod/event",
    R"({"event":"started"})",
    {{"Accept", "application/json"}},
    5000);

if (!response.ok())
    ModLog::warn("request failed: %d %s",
                 response.status, response.error.c_str());
```

The request is synchronous. Do not issue it from `update()` or a render hook.
Use a worker, attach that worker before any managed calls, keep timeouts and
response sizes bounded, and never log authorization data. Optional public-key
pinning is supported, but a pin creates its own rotation policy.

## Complete wrapper index

This final list is for discovery. It names the whole public `Unity::` facade so
you can search the generated header without guessing. A derived wrapper also
has every method of its base wrapper.

### Core objects

- `Object`: `handle`, boolean handle check, `alive`, `name`, `ToString`,
  `runtime_class_name`, `hideFlags`, `GetInstanceID`; all `FindObject...`,
  `FindObjects...`, `FindInstance...`, and `FindAllInstances` variants;
  `Instantiate`, `Destroy`, `DestroyImmediate`, `DontDestroyOnLoad`; `Call`,
  `CallExact`, `CallArrayExact`, `CallStringArrayExact`, `InvokeGeneric`,
  `GetField`, `SetField`, `StaticGetField`, `StaticSetField`, `GetProperty`,
  `SetProperty`, and `SetReferenceArrayProperty`.
- `Component`: `gameObject`, `transform`, every `GetComponent...` and
  `GetComponents...` variant, `AddComponent`, `HasComponent`, and
  `GetOrAddComponent`.
- `Behaviour`: `enabled`, `set_enabled`, and `isActiveAndEnabled`.
- `MonoBehaviour`: `useGUILayout` and `set_useGUILayout`.
- `ScriptableObject`: all `CreateInstance` variants.
- `GameObject`: `Find`, `FindWithTag`, `FindGameObjectsWithTag`, `Create`,
  `CreateUi`, `New`, `transform`, `activeSelf`, `activeInHierarchy`,
  `SetActive`, `scene`, `tag`, plus all component helpers.
- `Transform`: world/local position, Euler angles, rotation, local/lossy scale,
  `forward`, `right`, `up`, parent/root, `SetParent`, `childCount`, `GetChild`,
  and `Find`, with setters for mutable properties.
- `Scene`: handle/boolean checks, `IsValid`, `isLoaded`, `buildIndex`,
  `handle_value`, `name`, `path`, `isDontDestroyOnLoad`,
  `GetRootGameObjects`, and `rootCount`.
- `TypeRef`: `resolve_class` and `resolve_type_object`. `TypeObject` exposes its
  handle and a boolean check.
- `CanvasRoot`: `gameObject`, `rectTransform`, `canvas`, `scaler`, `raycaster`,
  and a boolean readiness check. `CreateOverlayCanvas` builds it.

### World, rendering, physics, animation, audio, and assets

- `Camera`: `main`, `current`, `fieldOfView`/setter, clip planes, aspect, pixel
  size, `WorldToScreenPoint`, `ScreenToWorldPoint`, `WorldToViewportPoint`,
  `ViewportToWorldPoint`, and `ScreenPointToRay`.
- `Light`: type, colour, colour temperature/use flag, intensity, bounce,
  range, spot/inner spot angles, cookie/size, shadows/strength/resolution/bias,
  normal bias, near plane, culling mask, and render mode, with setters.
- `Renderer`: `bounds`, `localBounds`/setter, enabled/force-off/visibility,
  receive-shadows, dynamic occlusion, sorting layer/order, material and shared
  material, material arrays, shadow casting, motion vectors, light probes, and
  reflection probes, with the applicable setters.
- `SkinnedMeshRenderer`: shared mesh, bones, root bone, blend-shape count and
  weights, quality, off-screen update, forced matrix recalculation, skinned
  motion vectors, and `BakeMesh`. `MeshRenderer` adds no new named members.
- `MeshFilter`: mesh/shared mesh and setters. `Collider`: bounds and enabled.
  `MeshCollider`: shared mesh, convex, and their setters.
- `Rigidbody`: velocity and angular velocity with setters. `Rigidbody2D` has
  the corresponding 2D velocity and scalar angular velocity helpers.
- `Texture`: width, height, anisotropic level, mip bias, and setters.
  `Texture2D`: mip count, `GetPixel`, `SetPixel`, `Resize`, and `Apply`.
- `Shader`: `Find`, `PropertyToID`, `isSupported`, maximum LOD/setter, and
  render queue.
- `Material`: shader, colour, main texture and setters; `GetFloat`, `SetFloat`,
  `GetColor`, `SetColor`, `GetTexture`, `SetTexture`, and `HasProperty`.
- `Animator`: speed, root-motion settings/deltas, human/root/initialized flags,
  layer count, culling/update modes, fire-events, avatar/controller, all
  float/int/bool/trigger parameter helpers, layer name/index/weight helpers,
  `Play`, `CrossFade`, `StringToHash`, `Update`, and `Rebind`.
- `AudioSource`: clip, volume, pitch, spatial blend, time, loop, mute,
  play-on-awake, `isPlaying`, `Play`, `PlayDelayed`, `PlayOneShot`, `Pause`,
  `UnPause`, and `Stop`.
- `AssetBundle`: `LoadFromFile`, `Contains`, `LoadAsset`, `LoadAllAssets`,
  `GetAllAssetNames`, `GetAllScenePaths`, and `Unload`.
- `Mesh` and `Sprite`: typed Object wrappers with no extra named members.

### Unity UI

- `RectTransform`: anchored 2D/3D position, min/max anchors, pivot, size delta,
  offsets, rect, their setters, `SetInsetAndSizeFromParentEdge`, and
  `SetSizeWithCurrentAnchors`.
- `Canvas`: `ForceUpdateCanvases`, render mode, world camera, plane distance,
  pixel-perfect, scale factor, sorting order, override sorting, target display,
  and setters.
- `CanvasRenderer`: cull/setter, `GetAlpha`, and `SetAlpha`.
- `CanvasGroup`: alpha, interactable, blocks-raycasts, ignore-parent-groups,
  and setters.
- `CanvasScaler`: scale mode, reference resolution, screen-match mode,
  width/height match, scale factor, reference pixels per unit, and setters.
- `Graphic`: colour, material, raycast target, and setters.
  `GraphicRaycaster`: reversed-graphics flag, blocking objects/mask, and setters.
- `Selectable`: interactable, transition, target graphic, setters,
  `IsInteractable`, and `Select`.
- `Image`: sprite/override sprite, image type, preserve aspect, fill amount,
  fill method/origin/clockwise, and setters. `RawImage`: texture and UV rect
  with setters.
- `Text`: text setter, font, size, style, alignment, rich-text flag, line
  spacing, best-fit flag, and setters. `TextMeshProUGUI`: text, colour, size,
  font style, alignment, word wrapping, rich text, and setters.
- `Button`: image, `onClick`, and `Click`.
- `Toggle`: is-on, graphic, `onValueChanged`, setters, and
  `SetIsOnWithoutNotify`.
- `Slider`: value, min/max, whole-numbers, direction, `onValueChanged`, setters,
  and `SetValueWithoutNotify`.
- `Scrollbar`: value, size, step count, direction, `onValueChanged`, setters,
  and `SetValueWithoutNotify`.
- `Dropdown`: value, caption/item text and images, template transform,
  `onValueChanged`, setter, without-notify setter, `ClearOptions`,
  `RefreshShownValue`, `Show`, and `Hide`.
- `InputField`: text, character limit, content/line type, read-only,
  placeholder, text component, change/end events, setters,
  `SetTextWithoutNotify`, `ActivateInputField`, `DeactivateInputField`, and
  `SelectAll`.
- `TmpInputField`: text, character limit, content/line type, read-only,
  change/end events, setters, `SetTextWithoutNotify`, activation/deactivation,
  and `SelectAll`. `TmpDropdown`: value/event, setter, without-notify setter,
  `Show`, and `Hide`.
- `Mask`: show-mask-graphic and setter. `RectMask2D`: padding/setter,
  `canvasRect`, and `PerformClipping`.
- `ScrollRect`: content, viewport, horizontal/vertical flags, movement type,
  elasticity, inertia, deceleration, sensitivity, horizontal/vertical/combined
  normalized positions, setters, and `StopMovement`.
- `LayoutElement`: ignore-layout, min/preferred/flexible width and height,
  priority, and setters.
- `HorizontalLayoutGroup` and `VerticalLayoutGroup`: spacing, child alignment,
  child size-control/force-expand flags, and setters. `GridLayoutGroup`: cell
  size, spacing, start corner/axis, constraint/count, and setters.
- `ContentSizeFitter`: horizontal/vertical fit and setters.
  `AspectRatioFitter`: aspect mode/ratio and setters.
- `EventSystem`: `EnsureStandalone`, `current`, first/current selected object,
  send-navigation flag, pixel drag threshold, setters,
  `SetSelectedGameObject`, and `IsPointerOverGameObject`.
- `BaseInputModule`: event system. `StandaloneInputModule`: input actions per
  second and repeat delay with setters. `InputSystemUIInputModule` adds no
  named members.
- `LayoutRebuilder`: `ForceRebuildLayoutImmediate` and
  `MarkLayoutForRebuild`.

### Static groups and value helpers

- `Debug`: `Log`, `LogWarning`, and `LogError`.
- `Screen`: `width`, `height`, `dpi`, `size`, `center`, `contains`, and
  `clamp`.
- `SceneManager`: `GetActiveScene`, `sceneCount`, `GetSceneAt`,
  `GetLoadedScenes`, both loaded-root helpers, and both scene-GameObject search
  helpers.
- `Time`: `time`, `deltaTime`, `unscaledDeltaTime`, `timeScale`, and
  `set_timeScale`.
- `Input`: availability and held/down/up helpers for keys and mouse buttons.
- Projection: `direction_to_screen_edge`, both `project_world` overloads, both
  `project_transform` overloads, both `world_to_overlay` overloads, and both
  `world_visible` overloads.
- `Vector2`: arithmetic, squared/ordinary magnitude, normalization,
  near-zero, dot, and distance. `Vector3` adds cross. Other mapped values are
  Quaternion, Vector4, Color/Color32, integer vectors, Rect, Bounds, and Ray.
- Enums cover object filtering/sorting, keys/buttons, rendering and probes,
  animator/light modes, and every wrapped UI layout/control mode.

If a member is not wrapped by name, use the general Object calls. If the type
is not wrapped, use `TypeRef` plus the string component/object overloads. The
facade is meant to cover the common path without preventing game-specific
work.

## Troubleshooting

Start with `URKit_logs.log` beside the game executable. Check the selected
proxy, detected backend, runtime readiness, then the first exact helper or hook
diagnostic. For a Unity wrapper failure, print `Unity::last_error()`; for a
backend metadata failure, print `URK::mono::last_error()` or
`URK::il2cpp::last_error()`.

When reporting a problem, include the game version, Unity version, Mono or
IL2CPP, selected proxy, URKit version/hash, Debug or Release, and the smallest
relevant log section. “It crashed” is hard to fix; “this exact overload stopped
resolving after game build X” is usually enough to start.

Before shipping, test startup, a scene change, menu input/cursor behaviour,
your hooks, and a clean shutdown. Confirm every hook, callback, coroutine,
worker, highlight, and Unity object owned by the mod is released or detached.
