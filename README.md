# URKit

[![GitHub Release](https://img.shields.io/github/v/release/Jadis0x/URKit?label=Release)](https://github.com/Jadis0x/URKit/releases/latest)
[![Downloads](https://img.shields.io/github/downloads/Jadis0x/URKit/total?label=Downloads)](https://github.com/Jadis0x/URKit/releases)
[![GitHub Stars](https://img.shields.io/github/stars/Jadis0x/URKit?style=flat&label=Stars)](https://github.com/Jadis0x/URKit/stargazers)
[![Discord](https://img.shields.io/badge/Discord-Join%20Server-5865F2?logo=discord&logoColor=white)](https://discord.com/invite/XC7RUpGp6e)
[![Support](https://img.shields.io/badge/Support-GitHub%20Issues-blue?logo=github)](https://github.com/Jadis0x/URKit/issues)
[![License](https://img.shields.io/github/license/Jadis0x/URKit)](https://github.com/Jadis0x/URKit/blob/main/LICENSE)

URKit is a native C++ modding toolkit for Windows x64 Unity games. It supports
Mono and IL2CPP through one loader ABI, and gives you Unity object access,
managed method calls, hooks, lifecycle callbacks, networking, and an ImGui
overlay, generated straight into a buildable CMake project.

<img src="showcase/ss1.png" width="550">

## What's in a release

- `urk-sdk.exe`: generates a Mono or IL2CPP mod project.
- `urk-updater.exe`: updates a generated project without touching your own files.
- `version.dll`, `winhttp.dll`, `winmm.dll`: proxy loaders. Install the one the game actually imports.
- `URKitInjector.dll`: a proxy-free loader for manual injection workflows.
- `urk-dev-mcp.exe` + `URKitDevBridge.dll`: optional MCP server for AI coding assistants (build, deploy, runtime diagnostics).

Put the proxy DLL next to the game executable, and built mods in the game's
`Mods` directory. Only install one proxy, and don't rename it.

## Quick start

```powershell
./urk-sdk.exe --game-exe C:\Games\Example\Example.exe --backend auto --name MyMod
cd <GameDir>\urk-sdk-output\MyMod\project
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

That builds a debug DLL and drops it in the game's `Mods` directory. You need
CMake 3.28+, Ninja, and either LLVM/Clang or the MSVC toolchain (VS 2022 Build
Tools or newer). Use `clang-release` / `msvc-release` when you're ready to
ship, and `msvc-debug` if you'd rather build with `cl.exe`.

Launch the game and check `URKit_logs.log` next to the executable. You
should see the mod initialize. If that file doesn't exist, the game isn't
importing the proxy you installed.

## Learn the API

New to URKit? Start with [Getting Started](docs/GETTING_STARTED.md). It
walks through writing an actual mod, from "hello log" to reading and changing
game state, step by step.

For a video walkthrough, check out the [URKit SDK Tutorial Series](https://youtube.com/playlist?list=PLP9lUXoova70&si=ATR-n7l7tVEliZlN).

Once you know the basics, [docs/SDK_HANDBOOK.md](docs/SDK_HANDBOOK.md) is the
full reference: object search, threading rules, hooks, the render/highlight
pipeline, caching, and a diagnostics chapter for when something doesn't work.
[ARCHITECTURE.md](ARCHITECTURE.md) covers how URKit itself is put together,
and [docs/DEV_MCP.md](docs/DEV_MCP.md) covers the AI-assistant MCP server.

Updating an existing project, migrating a hand-patched SDK, or automating the
updater from a script: that's all in the handbook's first chapter.

## Security software and false positives

The proxy loaders and `URKitInjector.dll` load into a game process and can
install API hooks, which is normal mod-loader behavior, but it can trip
antivirus heuristics. A detection alone doesn't mean a release is malicious.

Only download releases from this repository, and check the published SHA-256
digest. Release binaries are self-signed for tamper identification, not as a
trust signal: Windows won't trust that certificate by default. Don't disable
your antivirus to run URKit. If you get a false positive, report it to the
vendor (or open an issue here) with the detection name, the file, and its
SHA-256 digest.

URKit is available under the [MIT License](LICENSE).
