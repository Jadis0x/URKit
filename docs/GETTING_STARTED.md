# Getting Started

This is a walkthrough, not a reference. It takes you from a freshly generated
project to a mod that finds the player, reads and changes a game value,
shows a checkbox for it in the menu, and draws an overlay box on a target
in the world. Every step builds on the last, and every
one of them actually runs. Copy the code instead of just reading it.

Game names, tags, and managed types below are fictional. Swap them for
whatever your target game actually uses; the handbook's
[type inspection section](SDK_HANDBOOK.md#81-inspect-an-unfamiliar-type-at-runtime)
shows how to find those names when you don't know them yet.

Once you're comfortable with this, [SDK_HANDBOOK.md](SDK_HANDBOOK.md) has the
full picture: every object-search variant, hooks, the render/highlight
pipeline, coroutines, and a diagnostics chapter for when a call quietly fails.

## What you need

- CMake 3.28+ and Ninja
- LLVM/Clang, or the MSVC toolchain from VS 2022 Build Tools or newer
- `urk-sdk.exe` from a URKit release
- the game you're modding, installed

## 1. Generate a project

```powershell
./urk-sdk.exe --game-exe C:\Games\SampleGame\SampleGame.exe --backend auto --name FirstMod
```

`auto` looks at the game folder and picks Mono or IL2CPP for you. If you
already know which one the game uses, pass `--backend mono` or `--backend
il2cpp` directly. The project lands at:

```text
C:\Games\SampleGame\urk-sdk-output\FirstMod\project
```

Build it before you change anything, just to confirm the toolchain works:

```powershell
cd C:\Games\SampleGame\urk-sdk-output\FirstMod\project
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

(Using MSVC instead? Run this from an x64 Visual Studio Developer PowerShell
and use `msvc-debug`.)

The build copies the DLL straight into the game's `Mods` folder.

## 2. Confirm it actually loads

Launch the game. Then open `URKit_logs.log`, next to the game executable, and
look for a line saying the mod initialized. If the file isn't there at all,
the game isn't importing the proxy DLL you installed. Double check you put
the right one (`version.dll`, `winmm.dll`, or `winhttp.dll`) next to the
executable, and that there's only one.

Keep this log open in a text editor while you work. Every step below writes
something to it.

## 3. Say hello from your own code

Open `mod/lifecycle/mod_runtime.cpp`. This file is yours: URKit generates it
once and never touches it again on updates. `start()` runs once, right after
your mod initializes (a Mono project has the same shape, just with "Mono"
instead of "IL2CPP" in the log line):

```cpp
bool start(const URK_ModContext *ctx) {
    URK::set_context(ctx);
    if (!URK::initialize_backend(ctx)) {
        ModLog::error("IL2CPP runtime API initialization failed");
        return false;
    }

    ModLog::info("FirstMod is alive");
    return true;
}
```

Add that `ModLog::info("FirstMod is alive")` line, rebuild
(`cmake --build --preset clang-debug --parallel`; no need to re-run
`cmake --preset` unless you added a new file), relaunch the game, and look
for it in the log. If it's there, your build-edit-reload loop works and
you're ready for actual game logic.

## 4. Toggle something with a key

`update()` in the same file runs every frame, on the same thread Unity runs
on, which is where Unity calls belong. Add a key toggle:

```cpp
void update() {
    static bool enabled = false;

    if (Unity::Input::available() && Unity::Input::GetKeyDown(Unity::KeyCode::F8)) {
        enabled = !enabled;
        ModLog::info("FirstMod: %s", enabled ? "on" : "off");
    }
}
```

`GetKeyDown` fires once on press, unlike `GetKey`, which stays true the whole
time the key is held. Rebuild, press F8 in-game, watch the log flip between
on and off. You now have a feature flag, and the next two sections make it
do something.

## 5. Find the player and read a value

This is where most mods actually start: get a handle to something in the
scene, then read one of its fields. Two calls, and a mandatory error check.
Skip the error check and a failed lookup silently looks like a legitimate
zero.

```cpp
Unity::clear_error();

Unity::GameObject player = Unity::GameObject::FindWithTag("Player");
if (!player) {
    ModLog::warn("player lookup failed: %s", Unity::last_error());
    return;
}

Unity::Object vitals = player.GetComponent("GameScripts.dll", "Game.Player", "PlayerVitals");
if (!vitals) {
    ModLog::warn("PlayerVitals missing: %s", Unity::last_error());
    return;
}

const float health = vitals.GetField<float>("health");
ModLog::info("current health: %.1f", health);
```

The three strings in `GetComponent` aren't a file path or a hierarchy path.
They're the managed assembly, namespace, and class name of the component
you're looking for. `GetField<float>("health")` fails the exact same way a
real `0.0f` health would look, which is why the error check above it isn't
optional. `Unity::last_error()` tells you what actually went wrong: no
`Player`-tagged object yet, no `PlayerVitals` component, wrong field name,
and each one means something different for what you fix next.

Drop this in `update()`, gated by your F8 flag, so it only runs when you
actually want it:

```cpp
if (enabled) {
    Unity::GameObject player = Unity::GameObject::FindWithTag("Player");
    // ...
}
```

## 6. Change something

Reading is half the loop. Writing a field works the same way:

```cpp
vitals.SetField("health", 999.0f);
```

For behavior instead of state (healing instead of setting a number directly),
call the method instead, and use `TryCallExact` so a bad call tells you why
it failed instead of just doing nothing:

```cpp
if (!vitals.TryCallExact("Heal", {"System.Single"}, 500.0f)) {
    ModLog::error("Heal failed: %s", Unity::last_error());
}
```

`{"System.Single"}` is the parameter list: one `float` argument, using its
full managed type name. A successful call means the method ran without
throwing; it doesn't guarantee the game accepted the value (some games clamp
or reject it), so if that distinction matters, read the field back
afterward.

## 7. Put a checkbox in the menu

Feature flags flipped by a hotkey are fine for testing, but a real menu entry
is one `ImGui::Checkbox` away. Create `mod/ui/tabs/first_mod_panel.h`:

```cpp
#pragma once

#include <imgui.h>

namespace FirstModPanel {

inline bool enabled = false;

inline void draw() {
    ImGui::Checkbox("God mode", &enabled);
}

} // namespace FirstModPanel
```

Include it from the preserved `mod/ui/menu.h`, and draw it in the tab content
area you want it in:

```cpp
#include "tabs/first_mod_panel.h"

// inside the content area of ModUI::render_menu():
FirstModPanel::draw();
```

Then swap your F8-flag checks in `update()` for `FirstModPanel::enabled`, and
drop the hotkey code; you don't need both. Rebuild, open the menu, and the
checkbox drives the same feature the key used to.

## 8. Draw a box on it

You can find the player and read its state. Now put something on screen at
its position, without a Unity Canvas, a shader, or a single line of DirectX.

Include the header the generator already put in your project:

```cpp
#include "ui/highlight.h"
```

Then, in `update()`:

```cpp
Unity::GameObject player = Unity::GameObject::FindWithTag("Player");
if (player) {
    ModUI::Highlight::enqueue_add_world_point(player.transform().position(), "Player");
}
```

Rebuild, launch, and there's a yellow box floating at the player's position,
with an arrow at the screen edge when the player walks off-camera. That's the
whole minimum.

Which is also where the first mistake lives, so read the next bit before you
get attached to those three lines.

### One marker, moved, not a new one every frame

`update()` runs every frame, so the code above creates a *new* marker sixty
times a second. A few seconds in you've got hundreds of stale boxes stacked
on top of each other and the overlay is chewing frames for nothing.

A highlight isn't a draw call you repeat. It's an entry you create once and
then move. Creating it hands you an ID; you keep that ID and push new
positions into it.

The ID has to outlive the frame, so it goes at file scope. Put it just inside
`namespace ModRuntime {`, above `update()`:

```cpp
namespace {
ModUI::Highlight::HighlightId g_player_marker = 0;
} // namespace

void update() {
    Unity::clear_error();

    Unity::GameObject player = Unity::GameObject::FindWithTag("Player");
    if (!player || !player.alive()) {
        // Player gone: drop the marker, forget the ID.
        if (g_player_marker != 0) {
            ModUI::Highlight::enqueue_remove(g_player_marker);
            g_player_marker = 0;
        }
        return;
    }

    const Unity::Vector3 world = player.transform().position();

    if (g_player_marker == 0) {
        g_player_marker = ModUI::Highlight::enqueue_add_world_point(world, "Player");
        if (g_player_marker == 0)
            ModLog::warn("player marker could not be created");
        return;
    }

    ModUI::Highlight::enqueue_set_world_point(g_player_marker, world);
}
```

`enqueue_add_world_point` once, `enqueue_set_world_point` from then on. If you
ever see markers multiplying, this is the line you got wrong.

Zero is the "no marker" value, which is why the code checks the ID instead of
a separate `bool`. And when the player disappears, resetting `g_player_marker`
to zero matters as much as the `enqueue_remove` did: hang onto a dead ID and
the next scene's first marker gets moved by code that thinks it's still
talking about the last scene's player.

### Why "enqueue"

You're calling this from `update()`, on Unity's main thread. The overlay is
drawn later, on the game's render thread, inside the presentation hook. The
`enqueue_` calls exist to cross that gap: they copy plain data into a
mutex-protected queue that the render side drains at the start of its frame.

So you read Unity on the thread that owns Unity, and the renderer gets a
`Vector3` that can't be destroyed underneath it. That's the whole contract,
and it's why the world-point calls take a position instead of a `Transform`.

There are `add_*` variants without the prefix. Those mutate render-owned state
immediately and are meant for code already running inside the render callback.
From `update()`, use `enqueue_`.

### Make it look like something

The default style is a plain box. `Style` is a struct you fill in and hand
over at creation:

```cpp
ModUI::Highlight::Style enemy_style() {
    ModUI::Highlight::Style style{};
    style.color = IM_COL32(255, 82, 82, 235);       // red border
    style.fill_color = IM_COL32(255, 82, 82, 30);   // faint red fill
    style.filled = true;
    style.corner_box = true;                        // corner brackets, not a full rect
    style.draw_label = true;
    style.label_above_box = true;
    style.width = 70.0f;                            // box size at reference_distance
    style.height = 150.0f;
    style.scale_with_distance = true;               // shrink as the target gets further
    style.hide_within_distance = 3.0f;              // stop drawing when it's in your face
    return style;
}

// at creation:
g_marker = ModUI::Highlight::enqueue_add_world_point(world, "Enemy", enemy_style());
```

Build the style once and pass it in when you create the entry. Rebuilding an
identical struct every frame and pushing it back through `set_style` is pure
waste; the manager already has it.

`width` and `height` are the box size at `reference_distance`, not pixels on
screen. With `scale_with_distance` on, that box grows and shrinks between
`min_scale` and `max_scale` as the target moves, which is what makes a set of
markers read as depth instead of as flat stickers.

### More than one target

Same idea, one ID per target. The only new problem is matching this frame's
objects to last frame's IDs, and Unity hands you the key for free:

```cpp
#include <unordered_map>
#include <unordered_set>

namespace {
std::unordered_map<int, ModUI::Highlight::HighlightId> g_enemy_markers;
}

void update_enemy_markers() {
    Unity::clear_error();

    const std::vector<Unity::GameObject> enemies =
        Unity::GameObject::FindGameObjectsWithTag("Enemy");
    if (const char* detail = Unity::last_error(); detail && detail[0]) {
        ModLog::warn("enemy search failed: %s", detail);
        return;
    }

    std::unordered_set<int> seen;
    seen.reserve(enemies.size());

    for (Unity::GameObject enemy : enemies) {
        if (!enemy || !enemy.alive())
            continue;

        const int id = enemy.GetInstanceID();
        seen.insert(id);

        const Unity::Vector3 world = enemy.transform().position();
        auto marker = g_enemy_markers.find(id);
        if (marker == g_enemy_markers.end()) {
            const auto created =
                ModUI::Highlight::enqueue_add_world_point(world, "Enemy", enemy_style());
            if (created != 0)
                g_enemy_markers.emplace(id, created);
            continue;
        }
        ModUI::Highlight::enqueue_set_world_point(marker->second, world);
    }

    // Anything that stopped showing up is dead, despawned, or unloaded.
    for (auto it = g_enemy_markers.begin(); it != g_enemy_markers.end();) {
        if (seen.contains(it->first)) {
            ++it;
            continue;
        }
        ModUI::Highlight::enqueue_remove(it->second);
        it = g_enemy_markers.erase(it);
    }
}
```

`FindGameObjectsWithTag` walks the whole scene, so don't call it every frame
for a big scene. Once every few frames, or off a spawn/despawn hook, is
enough; the markers keep drawing from their cached projections in between.
[Design caches around the work they avoid](SDK_HANDBOOK.md#11-design-caches-around-the-work-they-avoid)
goes into that.

### Clean up on scene change

Markers are native state. They don't know a level ended. `mod_runtime.cpp`
already has the callback, generated with a guard and a log line; add the
cleanup to it:

```cpp
void on_scene_changed(const URK_SceneInfo *previousScene, const URK_SceneInfo *currentScene) {
    if (!previousScene || !currentScene || previousScene->size < sizeof(URK_SceneInfo) ||
        currentScene->size < sizeof(URK_SceneInfo))
        return;
    ModLog::info("scene changed: %s -> %s", previousScene->name, currentScene->name);

    ModUI::Highlight::enqueue_clear();
    g_enemy_markers.clear();
    g_player_marker = 0;
}
```

`enqueue_clear()` drops every entry the manager owns. Clearing your own ID
bookkeeping in the same breath is the part people forget, and it's the part
that produces the confusing bug: markers that appear in the new scene at
positions from the old one.

### When nothing shows up

Open `URKit_logs.log` and work down, in this order:

1. Does a line say a DX11, DX12, or OpenGL render hook was installed? No line
   means the overlay never started, and nothing about your code is the
   problem yet.
2. Does the mod's menu draw? If the menu is there and the highlight isn't,
   the whole graphics path already works. Skip to 3.
3. Is your `HighlightId` non-zero? Zero means creation failed.
4. Is there a main camera? Projection needs `Camera::main()`. No camera, no
   box, and the manager skips the entry rather than drawing it at a made-up
   position.
5. Is the target actually on screen and in front of the camera? Turn
   `offscreen_indicator` on and look for the edge arrow before you assume
   nothing is being drawn.
6. Still stuck: turn the manager's own diagnostics on and let it tell you
   which state each entry is in.

```cpp
// in ModRuntime::start()
ModUI::Highlight::set_diagnostics([](const char* line) {
    ModLog::info("%s", line ? line : "");
});
ModUI::Highlight::set_verbose_diagnostics(true);
```

It reports the cases you'd otherwise guess at: dead Transform, no projection,
projection failure, off-screen, too close, removed. Turn it back off before
you ship.

The [handbook's highlight chapter](SDK_HANDBOOK.md#12-highlights-and-the-native-render-pipeline)
covers the rest: what the render hook does per back end, how to attach a
marker to a `Transform` instead of a position and what that costs you, update
policies for when you have a lot of markers, and the full style reference.

## Where to go from here

You've now got the full loop: find an object, read it, change it, and
control it from the menu. From here, [SDK_HANDBOOK.md](SDK_HANDBOOK.md)
covers what this guide skipped on purpose:

- [Choose the right object search](SDK_HANDBOOK.md#6-choose-the-right-object-search): name, tag, hierarchy, or component type, and when a global scene search is the wrong tool.
- [Design caches around the work they avoid](SDK_HANDBOOK.md#11-design-caches-around-the-work-they-avoid): don't repeat a scene search every frame.
- [Add hooks only when normal calls are not enough](SDK_HANDBOOK.md#14-add-hooks-only-when-normal-calls-are-not-enough): for when polling in `update()` genuinely isn't enough.
- [Highlights and the native render pipeline](SDK_HANDBOOK.md#12-highlights-and-the-native-render-pipeline): what step 8 is standing on, plus Transform-anchored markers, update policies, and the full style reference.
- [Diagnostics by API layer](SDK_HANDBOOK.md#17-diagnostics-by-api-layer): a symptom-to-cause table for when something silently doesn't work.

And when you're ready to organize more than a toggle and a checkbox,
[Organize a feature](SDK_HANDBOOK.md#9-organize-a-feature) shows the module
structure this guide deliberately skipped so you could see the raw API first.
