# URKit

[![GitHub Release](https://img.shields.io/github/v/release/Jadis0x/URKit?label=Release)](https://github.com/Jadis0x/URKit/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Jadis0x/URKit/total?label=Downloads)](https://github.com/Jadis0x/URKit/releases)
[![GitHub Stars](https://img.shields.io/github/stars/Jadis0x/URKit?style=flat&label=Stars)](https://github.com/Jadis0x/URKit/stargazers)
[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/XC7RUpGp6e)
[![Support](https://img.shields.io/badge/Support-GitHub%20Issues-blue?logo=github)](https://github.com/Jadis0x/URKit/issues)
[![License](https://img.shields.io/github/license/Jadis0x/URKit)](https://github.com/Jadis0x/URKit/blob/main/LICENSE)

URKit lets you write mods for Windows x64 Unity games in plain C++. You point
it at a game, and it hands you a CMake project that already builds. From
there you can find and change game objects, call the game's own methods,
hook functions, react to scene changes, and draw an ImGui overlay. Mono and
IL2CPP games both work, and your mod code looks the
same for either one.

Unreal Engine support is on the way, but it isn't ready yet. See
[where it stands](#unreal-engine-work-in-progress).

<img src="showcase/ss1.png" width="550">

## What's in a release

- `urk-sdk.exe` makes a new mod project for a game.
- `urk-updater.exe` brings an existing project up to date without touching
  the files you wrote.
- `version.dll`, `winhttp.dll` and `winmm.dll` are the loaders. You only need
  one of them: the one the game actually loads. `version.dll` is a good first
  try.
- `URKitInjector.dll` is a loader for when you'd rather inject it yourself
  than drop a proxy next to the game.
- `urk-dev-mcp.exe` and `URKitDevBridge.dll` are optional. They let AI coding
  assistants build, deploy and inspect your mod.

The loader goes next to the game's executable, and your built mods go in a
`Mods` folder beside it. Install just one loader and keep its name as is.

## Quick start

```powershell
./urk-sdk.exe --game-exe C:\Games\Example\Example.exe --backend auto --name MyMod
cd <GameDir>\urk-sdk-output\MyMod\project
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

`--backend auto` works out whether the game is Mono or IL2CPP for you. The
build puts a debug DLL straight into the game's `Mods` folder.

You'll need CMake 3.28 or newer, Ninja, and either LLVM/Clang or MSVC (VS 2022
Build Tools or newer). Prefer `cl.exe`? Use `msvc-debug`. When you're ready to
share your mod, build with `clang-release` or `msvc-release`.

Now start the game and open `URKit_logs.log` next to its executable. You
should see your mod load. No log file at all usually means the game doesn't
load the loader you picked, so try one of the others.

## Unreal Engine (work in progress)

We're working on Unreal support, and honestly it's not ready for real mods
yet. Unity has an API we can ask about the game; Unreal doesn't, so URKit has
to find everything on its own while the game runs. That part works, but so
far it has only been tried on two games, and the second one already turned
up bugs. Here's where things stand:

| What | Status | Notes |
|---|---|---|
| Finding the engine in a running game | Works | Tried on a UE 5.8 game and a UE 5.4 game |
| Calling game functions, hooking `ProcessEvent` | Works | The 5.4 game needed a fix first |
| Map load / map change events | Works | |
| `update()` every frame | Buggy | In quiet scenes like a main menu it can drop to about once a second |
| Typed headers from the game (`DumpTypes`) | Works on 5.8 | Not tried on other versions yet |
| Numbers, objects, structs, `FString`, `FName`, `TArray` | Works on 5.8 | |
| `TSet` and `TMap` | Mostly works | If an edit fails halfway, the container can be left broken |
| `FText` | 5.8 only | Turned off on 5.4 for now |
| Enums by name, soft and weak references, delegates | Works on 5.8 | |
| Sparse delegates, interfaces, lazy pointers | Not yet | |
| Engines older than 4.25 | Not supported | |

If you want to poke at it anyway: put the loader next to the game's
`-Win64-Shipping.exe`, add this to `URKit_config.ini` in the same folder,
and play through the maps you care about:

```ini
[Unreal]
DumpTypes=1
```

Then make a project with `--backend unreal`. The generated
`sdk/unreal/README.md` explains the rest. If something breaks, an issue with
your `URKit_logs.log` attached helps a lot.

## Learn the API

New to URKit? Start with [Getting Started](docs/GETTING_STARTED.md). It walks
you through a real mod, from printing your first log line to reading and
changing game state.

If you'd rather watch, there's the
[URKit SDK Tutorial Series](https://youtube.com/playlist?list=PLP9lUXoova70&si=ATR-n7l7tVEliZlN)
on YouTube.

Once you know the basics, the [SDK Handbook](docs/SDK_HANDBOOK.md) is the full
reference. It covers finding objects, threading rules, hooks, rendering and
highlights, caching, and what to check when something doesn't work. Its first
chapter is about updating projects, including hand-patched ones and running
the updater from a script.

Curious how URKit works inside? Read [ARCHITECTURE.md](ARCHITECTURE.md). The
AI-assistant server has its own page in [docs/DEV_MCP.md](docs/DEV_MCP.md).

## Building URKit yourself

You'll need the same tools as for a mod project. For the MSVC presets, open a
Developer PowerShell for Visual Studio first so `cl.exe` can be found. Then,
from the repository root:

```powershell
cmake --preset msvc-release
cmake --build out/build/msvc-release
```

`clang-release`, `clang-debug` and `msvc-debug` work too.

## Antivirus warnings

The loaders get into a game's process and hook functions there. That's just
what mod loaders do, but some antivirus software flags it anyway. A warning on
its own doesn't mean a release is malicious.

To stay safe, only download releases from this repository and check the
SHA-256 digest published with each one. The binaries are self-signed so you
can tell if they were tampered with, but Windows won't trust that certificate
by default, and it isn't meant as a seal of approval. Please don't turn off
your antivirus to run URKit. If you hit a false positive, report it to the
antivirus vendor or open an issue here, and include the detection name, the
file and its SHA-256 digest.

## License

URKit is available under the [MIT License](LICENSE).
