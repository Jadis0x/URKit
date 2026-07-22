# URKit

URKit is a native C++ modding SDK for Windows x64 Unity games. It supports
both Mono and IL2CPP and gives a mod the runtime services it needs without
requiring game-specific source code in the mod project.

The SDK generates a ready-to-build project around the game you select. From
there, you can find Unity objects, read and change managed state, call methods,
install hooks, run work on Unity's main thread, and add an in-game ImGui menu.

![URKit showcase](showcase/ss1.png)

URKit mods are plugins loaded by the URKit loader. A generated mod DLL is not a
standalone program and must not be injected directly.

## Quick start

Download the latest Windows x64 release and extract it somewhere convenient.
The package contains:

- `urk-sdk.exe` - project generator and SDK tool
- `version.dll`, `winhttp.dll`, `winmm.dll` - loader proxies
- `URKitInjector.dll` - optional loader for external loading workflows
- SDK documentation and licenses

In the game directory, copy exactly one proxy DLL. Use the filename that the
game executable imports; do not rename a proxy and do not copy all three into
the same directory.

Then start `urk-sdk.exe`, select the game's executable, choose `Auto`, `Mono`,
or `IL2CPP`, and enter a project name. The generated project is written to:

```text
<GameDir>/urk-sdk-output/<Project>/project
```

The command-line form is useful when creating projects from scripts:

```powershell
.\urk-sdk.exe `
  --game-exe "C:\Games\Example\Example.exe" `
  --backend auto `
  --name MyMod
```

Add `--localization` if the mod needs editable locale JSON files.

## Build a generated mod

Generated projects require:

- Windows x64
- CMake 3.28 or newer
- LLVM/Clang
- Ninja

Open a terminal in the generated project directory and run:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

Use `clang-release` when building a mod for distribution:

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --parallel
```

The generated project is configured to deploy the mod DLL to the selected
game's `Mods` directory. If you copy the DLL manually, place it in:

```text
<GameDir>/Mods/
```

## Where to start coding

Most mods begin in `mod/lifecycle/mod_runtime.cpp`:

- `start()` runs once after the selected Unity backend is ready.
- `update()` runs on Unity's main thread.
- `on_scene_loaded()` and `on_scene_changed()` are useful for rebuilding
  scene-owned state.
- `stop()` releases hooks, callbacks, handles, workers, and other resources.

Keep application code under `mod/`. The generator owns `sdk/`,
`mod/generated/`, generated UI support, and build/editor configuration files;
those files may be refreshed when the project is regenerated.

Useful starter files include:

```text
mod/lifecycle/mod_runtime.cpp  Runtime lifecycle and Unity work
mod/hooks/mod_hooks.cpp        Hook installation and removal
mod/support/mod_log.cpp        Shared logging helpers
mod/config/mod_config.h        Mod metadata and settings
mod/ui/theme.h                 ImGui styling
```

## What the SDK provides

- Common Unity wrappers for Mono and IL2CPP
- GameObject, Component, Transform, Scene, Camera, physics, animation, audio,
  asset, and Unity UI helpers
- Field and property access, overload resolution, arrays, generics, and managed
  method invocation
- Main-thread callbacks, scene events, input, cursor ownership, coroutines,
  and Steam identity helpers
- Hook helpers with unload-safe ownership tracking
- Optional ImGui overlays for DX11, DX12, and OpenGL
- HTTPS JSON helpers backed by Windows Schannel
- Runtime diagnostics and `Unity::last_error()` reporting

Vulkan overlays are not supported. URKit does not include an offline offset
database or game-specific layouts; compatibility depends on the game, Unity
version, backend, and runtime configuration.

## Optional injected loader

`URKitInjector.dll` is a loader DLL for external loading workflows. It loads
the URKit runtime and lets the user select a configuration file and mod DLLs.
It is not an injector supplied by URKit, and it is not a replacement for the
generated mod DLL installation rules. Generated mods remain loader plugins.

For normal use, the proxy loader is simpler: put the correct proxy beside the
game executable and put mod DLLs in `Mods`.

## Troubleshooting

The loader writes `URKit_logs.log` beside the game executable.

- No log file usually means the selected proxy was not loaded. Check the
  proxy filename and its location.
- If the loader starts but no mod appears, check the `Mods` directory, the
  backend selected by the generator, and the log file.
- `Auto` selects IL2CPP when `GameAssembly.dll` is present and otherwise checks
  for a Mono runtime.
- A mod DLL built for 32-bit Windows cannot load into a 64-bit game.
- Retest after game or Unity updates; internal layouts and managed metadata can
  change.

## Documentation

- [URKit SDK Handbook](docs/SDK_HANDBOOK.md)
- [MIT license](LICENSE)

The public SDK is MIT-licensed. The precompiled loader binaries are distributed
under their own binary license, included in the release package. Loader source
code is not part of this repository.
