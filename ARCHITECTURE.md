# URKit architecture

URKit has four main parts: proxy loader DLLs, a proxy-free loader DLL, a shared
runtime, and the SDK project generator. The proxy and proxy-free entry forms
reuse one object-library build of the runtime; only the proxy forwarding source
and export definition differ.

## Source map

| Path | Responsibility |
|---|---|
| `sdk/mod_sdk.h` | Public ABI, versions, capabilities, contexts, and API tables. |
| `src/proxy/` | `version.dll`, `winhttp.dll`, and `winmm.dll` forwarding entry points. |
| `src/platform/dllmain.cpp` | Minimal process attach/detach entry point. |
| `src/core/loader.cpp` | Startup flow and backend selection. |
| `src/core/loader/` | Runtime backends, mod loading, events, paths, and Steam identity. |
| `src/core/` | Configuration, logging, hooks, lifecycle, networking, and shared state. |
| `src/unity/` | Mono and IL2CPP export/metadata adapters. |
| `src/ui/` | Loader splash UI. |
| `src/tools/sdk_tool/` | `urk-sdk.exe` command-line and Win32 UI. |
| `src/sdk/` | SDK/project writers and output validation. |
| `src/sdk/templates/` | Generated runtime, Unity, and optional UI source. |
| `mcp/` | Client-neutral stdio MCP server, project services, and bounded bridge protocol. |
| `dev/bridge/` | Optional in-game development bridge and runtime-test discovery. |
| `sdk/dev_test.h` | Fixed C ABI for mod-owned runtime tests. |
| `cmake/` | Source lists, embedded header generation, and release staging. |

## Runtime flow

1. A selected proxy forwards to the real Windows DLL, or an external workflow
   loads the dedicated `URKitInjector.dll` loader into the game.
2. A process-wide owner marker ensures that only one URKit entry DLL starts the
   shared runtime when a game loads more than one supported proxy.
3. Startup qualifies the current process from loaded Unity modules. Launcher,
   web, and helper processes remain forwarding-only even when game files are in
   the same directory.
4. The injected loader asks the user for the config `.ini` and one or more mod DLLs;
   proxy startup retains its normal executable-directory configuration flow.
5. Loader startup reads configuration and waits for Mono or IL2CPP.
6. The backend attaches safely, resolves public runtime APIs, and configures
   available services.
7. The selected native DLLs receive a validated `URK_ModContext` through
   `ModInitEx`.
8. The runtime owns callback dispatch, hook records, and orderly shutdown.

Development MCP project operations run out of process against one generated
project manifest. Runtime requests enter through a current-user local named
pipe, wait in a bounded queue, and execute from the DevBridge main-thread
callback. Native mods expose tests through fixed discovery and execution
exports; the bridge does not retain callbacks across requests.

Capabilities are explicit. A missing event pump, input service, or backend API
stays unavailable instead of being inferred from private layouts.
Native mod discovery runs before runtime-event activation, so processes without
mod candidates and safe-mode sessions remain free of Unity function hooks.

Public context and API structures are versioned and append-only. Loader-internal
lifecycle, backend discovery, and export validation changes do not alter the
native mod ABI.

## Ownership rules

- `sdk/mod_sdk.h` is the ABI source of truth. API tables are append-only.
- Runtime state and loader-owned resources stay under `src/core`.
- Mono/IL2CPP-specific lookup code stays in `src/unity` or its backend adapter.
- Generated files are overwritten; user-owned generated-project files are
  created only when missing.
- Proxy files contain forwarding code only. Shared behavior belongs in the
  `URKitRuntimeObjects` object library.

When changing the ABI, update `sdk/mod_sdk.h`, context builders, capability
checks, generated bootstrap code, and release notes together.

## Concurrency

The loader starts outside `DllMain`. Runtime worker threads use generation
tokens during shutdown, managed calls use attach scopes, and callbacks are
removed before a mod module is freed. Code running under the event mutex must
remain short and must not call user code.

## Build layout

- `CMakePresets.json`: Clang and MSVC Debug/Release builds.
- `.clangd`: clangd index configuration.
- `cmake/URKitSources.cmake`: the canonical root source list.

Common runtime sources compile once into `URKitRuntimeObjects`, then link into
the three proxies and `URKitInjector.dll`. This keeps loader behavior identical
while avoiding duplicated compilation work.
