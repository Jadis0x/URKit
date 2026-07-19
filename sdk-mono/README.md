# BlankMono

URKit Mono native mod starter for Windows x64. It requires a compatible URKit loader context and is not a standalone injectable DLL.

## Build

Build with the Clang preset:

```powershell
cmake --preset clang-debug
cmake --build --preset clang-debug --parallel
```

Use the `clang-release` preset for release builds. Release preset names end in `-release`. Builds deploy to the selected game's `Mods` directory.

## Edit these files

- `mod/lifecycle/mod_runtime.cpp`: game/runtime and main-thread work.
- `mod/hooks/mod_hooks.cpp`: exact, validated hook installation.
- `mod/lifecycle/mod_network.cpp`: HTTPS setup and policy.
- `mod/support/mod_log.cpp`: shared logging.
- `mod/config/mod_config.h` and `mod/ui/theme.h`: metadata and styling.

Files under `sdk/`, `mod/generated/`, generated UI support, and the build profiles are refreshed by the generator. User-owned files and new sources under `mod/` are preserved.

## Rules

- `sdk/mod_sdk.h` is the ABI source of truth. Check version, size, backend, capability, and function pointers before use.
- Include `sdk/unity/unity.h` for normal Unity work. Use exact overload helpers when required and keep Unity calls on the main thread.
- Resolve stable metadata once instead of repeating lookups in update/render callbacks.
- Detach hooks, callbacks, coroutines, workers, and UI before unload.
- DX11, DX12, and OpenGL overlays are supported; Vulkan is not.
- Mono and IL2CPP generated projects use runtime API helpers. No offline metadata or dump-generated headers are emitted.

Start troubleshooting with `URKit_logs.log` beside the game executable.
