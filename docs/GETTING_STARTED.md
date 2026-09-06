# Getting Started

This is a walkthrough, not a reference. It takes you from a freshly generated
project to a mod that finds the player, reads and changes a game value, and
shows a checkbox for it in the menu. Every step builds on the last, and every
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

## Where to go from here

You've now got the full loop: find an object, read it, change it, and
control it from the menu. From here, [SDK_HANDBOOK.md](SDK_HANDBOOK.md)
covers what this guide skipped on purpose:

- [Choose the right object search](SDK_HANDBOOK.md#6-choose-the-right-object-search): name, tag, hierarchy, or component type, and when a global scene search is the wrong tool.
- [Design caches around the work they avoid](SDK_HANDBOOK.md#11-design-caches-around-the-work-they-avoid): don't repeat a scene search every frame.
- [Add hooks only when normal calls are not enough](SDK_HANDBOOK.md#14-add-hooks-only-when-normal-calls-are-not-enough): for when polling in `update()` genuinely isn't enough.
- [Highlights and the native render pipeline](SDK_HANDBOOK.md#12-highlights-and-the-native-render-pipeline): drawing world-space overlays (ESP-style boxes, markers) through the generated DirectX/OpenGL hook.
- [Diagnostics by API layer](SDK_HANDBOOK.md#17-diagnostics-by-api-layer): a symptom-to-cause table for when something silently doesn't work.

And when you're ready to organize more than a toggle and a checkbox,
[Organize a feature](SDK_HANDBOOK.md#9-organize-a-feature) shows the module
structure this guide deliberately skipped so you could see the raw API first.
