# URKit Development MCP

`urk-dev-mcp.exe` provides a client-neutral development loop for generated URKit
mods. Codex, Claude, and other local MCP hosts launch the same stdio server.
Project operations stay outside the game. Runtime operations pass through a
current-user, local-only Windows named pipe to `URKitDevBridge.dll`.

The stdio server supports the MCP `2026-07-28` per-request metadata model and
the `2025-11-25`, `2025-06-18`, `2025-03-26`, and `2024-11-05` initialization
model for older clients.

## Build

```powershell
cmake --preset clang-release
cmake --build --preset clang-release --target URKitDevMcp URKitDevBridge --parallel
```

The outputs are `urk-dev-mcp.exe` and `URKitDevBridge.dll` in the configured
build directory. Copy `URKitDevBridge.dll` to the target game's `Mods`
directory. Keep it out of public gameplay installations.

## Client configuration

The server requires the root of one generated URKit mod project. Use
`--game-pid` when more than one game with DevBridge is running.

Codex CLI:

```powershell
codex mcp add urkit-dev -- "C:\absolute\path\to\urk-dev-mcp.exe" --project "C:\absolute\path\to\generated\project"
```

Claude Desktop and generic JSON-based MCP hosts:

```json
{
  "mcpServers": {
    "urkit-dev": {
      "command": "C:\\absolute\\path\\to\\urk-dev-mcp.exe",
      "args": [
        "--project",
        "C:\\absolute\\path\\to\\generated\\project"
      ]
    }
  }
}
```

Multiple running games:

```json
{
  "command": "C:\\absolute\\path\\to\\urk-dev-mcp.exe",
  "args": [
    "--project",
    "C:\\absolute\\path\\to\\generated\\project",
    "--game-pid",
    "12345"
  ]
}
```

## Tools

| Tool | Responsibility |
| --- | --- |
| `project_info` | Project, SDK, backend, CMake presets, game directory, and deployment state. |
| `build_mod` | Configure and build with a declared preset, then verify the generated DLL was deployed. |
| `deploy_mod` | Copy an existing preset artifact to the manifest-owned `Mods` directory. |
| `read_logs` | Return a bounded tail of `URKit_logs.log`. |
| `runtime_status` | Backend, capabilities, process, main-thread, and current-scene state. |
| `list_runtime_tests` | Discover test exports from loaded native mods. |
| `run_runtime_test` | Execute one test on the Unity main thread. |

`build_mod` defaults to `clang-debug`. The `preset` argument must exist in both
`configurePresets` and `buildPresets`. Build output is bounded and returned with
the exact failure state. A successful compile without the expected deployed DLL
is reported as `deployment_missing`.

## Runtime tests

Generated projects include `sdk/dev_test.h`. A mod publishes a test collection
through three C exports. Test names must be unique across loaded mods, or callers
must use `module.dll!test_name`.

```cpp
#include "sdk/dev_test.h"
#include "sdk/runtime_api.h"

#include <cstring>

extern "C" __declspec(dllexport) uint32_t URK_DevTestCount() {
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestDescribe(
    uint32_t index,
    URK_DevTestDescriptor* descriptor) {
    if (index != 0 || !descriptor ||
        descriptor->version != URK_DEV_TEST_API_VERSION ||
        descriptor->size < sizeof(*descriptor))
        return 0;
    strcpy_s(descriptor->name, "mod.runtime_ready");
    strcpy_s(descriptor->sourceFile, __FILE__);
    descriptor->sourceLine = __LINE__;
    strcpy_s(descriptor->tags, "smoke,runtime");
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestRun(
    const char* name,
    URK_DevTestResult* result) {
    if (!name || std::strcmp(name, "mod.runtime_ready") != 0 || !result ||
        result->version != URK_DEV_TEST_API_VERSION ||
        result->size < sizeof(*result))
        return 0;
    result->passed = URK::context() && URK::has_main_thread();
    strcpy_s(result->message,
             result->passed ? "Mod runtime is ready."
                            : "Mod runtime or main-thread dispatch is unavailable.");
    result->details[0] = '\0';
    return 1;
}
```

The bridge caps each module at 512 tests. Native exceptions are reported as
`test_crashed`; incompatible result structures are rejected. A test that ran
correctly but failed its assertion returns structured evidence with
`passed: false` and an MCP tool error.

`URKitDevBridge.dll` exports `urkit.bridge_ready`, so the bridge and protocol can
be verified before a mod adds its own test exports.

## Security and lifecycle

- Project and game paths come from the server command line and
  `.urk/project.ini`; tool arguments cannot select arbitrary directories.
- Build commands invoke CMake directly and accept only declared preset names.
- The named pipe rejects remote clients and permits the creating Windows user
  and SYSTEM.
- Messages, queues, logs, build output, and test counts are bounded.
- MCP cancellation terminates an active CMake child process and suppresses the cancelled response.
- Unity and mod test calls run on the Unity main thread.
- The bridge discovery record is removed during `ModShutdown`.
- Loaded DLL replacement failures remain visible. The server does not claim hot
  reload support.

Discovery records are stored under:

```text
%LOCALAPPDATA%\URK\DevBridge\bridges
```

If runtime tools report `bridge_unavailable`, verify that the game is running,
`URKitDevBridge.dll` is in `Mods`, and `URKit_logs.log` contains the DevBridge
startup entry. Use `--game-pid` when several records are active.
