#include "unreal_sdk_generator.h"
#include "mod_project_generator_common.h"
#include "mod_project_generator_profiles.h"
#include "sdk_generator_contract.h"
#include "unreal_type_codegen.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace mpg = ModProjectGenerator;
namespace sdk = SdkGenerator;

#include "templates/unreal/runtime.inl"

std::string UnrealSdkReadme(const std::string &details) {
    std::ostringstream out;
    out << "# URKit - Unreal SDK\n\n"
        << "`unreal_runtime.h` wraps `URK_UnrealApi`: objects, members and functions are resolved by name at "
           "runtime through the engine's own reflection.\n\n"
        << "## The player, threads and keys\n\n"
           "```cpp\n"
           "namespace u = URK::unreal;\n"
           "namespace t = URK::unreal::types;\n\n"
           "void update() {  // game thread, every frame\n"
           "    const t::Character player = t::Character::cast(u::player_pawn());\n"
           "    if (!player)\n"
           "        return;  // a menu, or between maps\n"
           "    player.CharacterMovement().get().MaxWalkSpeed().set(1200.f);\n"
           "    if (u::key_pressed(VK_F5))\n"
           "        player.Jump();\n"
           "}\n\n"
           "// A menu draws on the render thread, where calls are refused: move them over.\n"
           "if (ImGui::Button(\"Jump\"))\n"
           "    u::on_game_thread([] { t::Character::cast(u::player_pawn()).Jump(); });\n"
           "```\n\n"
           "- `world()`, `game_instance()`, `game_state()`, `game_mode()`, `local_player(i)`, "
           "`player_controller(i)`, `player_pawn(i)`: member reads, any thread, cheap each frame; null between "
           "maps. `component<T>(actor)` and `components<T>(actor)` find an actor's components (game thread).\n"
           "- Function calls and changes to engine memory (strings, arrays, texts) run on the game thread: "
           "`update()`, scene callbacks, hooks, or `on_game_thread(work)` from anywhere else. Numbers and "
           "objects read and write anywhere. A refused call says why in the log.\n"
           "- `key_pressed/key_held/key_released(vk)` take Windows virtual-key codes (`'K'`, `VK_F5`, "
           "`VK_LBUTTON`), read while the game has focus, once per frame.\n\n"
        << "## Typed headers\n\n"
        << "Set `DumpTypes=1` under `[Unreal]` in the game's `URKit_config.ini` and play: every map that loads "
           "adds its classes to `" << UnrealTypeCodegen::kDumpFileName << "` beside the game. Generating or updating "
           "the project then writes `types/<Folder>/<Name>.h`, one per class, struct and enum, in a folder that "
           "mirrors its package (`/Script/Engine` is `types/Engine/`, `/Game/Doors/BP_Door` is `types/Game/Doors/`). "
           "`types/INDEX.md` lists every folder, the game's own first. Functions the Blueprint compiler made "
           "(input and bound-event thunks, timeline callbacks) sit in a separate section at the end of a class.\n\n"
           "- Classes are handles and hold names and signatures, never offsets: each access resolves on the live "
           "class, so a game update needs no rebuild. A member the update removed fails at runtime and `get()` "
           "comes back empty.\n"
           "- Structs are values copied into the mod, so their header carries a layout. Before any copy it is "
           "checked against the running game; if an update changed the struct, accesses to it fail and the log "
           "says to regenerate. Members that own engine memory (strings, arrays, texts) are kept as bytes in the "
           "copy and reached in place through the struct's static accessors: `S::Name(member.place())`.\n"
           "- Strings, names and texts read and write as UTF-8 `std::string`; enums by name (`E::Value()`, looked "
           "up in the running game); `ArrayMember`, `SetMember` and `MapMember` add, find, remove and replace "
           "elements; soft and weak references, delegates (checked against their signature) and multicast "
           "delegates are typed too. The engine's own code makes, changes and frees their memory, so changes run "
           "on the game thread (`update()`, scene callbacks, `post_to_game_thread`), as do text and soft path "
           "reads; numbers, names, strings and container reads work anywhere.\n\n"
           "`types/` is rewritten from the dump; do not edit it.\n\n"
           "## Hooking a function\n\n"
           "```cpp\n"
           "static u::FunctionHook damage = t::Actor::hook_ReceiveAnyDamage(\n"
           "    [](t::Actor::ReceiveAnyDamage_Call &call) {  // before; return false to skip the body\n"
           "        call.Damage().set(call.Damage().get().value_or(0) * 2);\n"
           "    },\n"
           "    [](auto &call) { /* after: call.ReturnValue() where there is one */ });\n"
           "```\n\n"
           "Every class header has `hook_<Function>` and `<Function>_Call`, one accessor per parameter; "
           "`call.self()` is the object called on. Untyped: `u::hook(u::find(\"Actor\"), \"ReceiveAnyDamage\", "
           "before, after)` with parameters by name (`call.get<float>(\"Damage\")`).\n\n"
           "- Any number of hooks per function, from any number of mods, run in the order added. The hook is "
           "removed when the `FunctionHook` goes away.\n"
           "- Seen: every call through ProcessEvent (engine events, input, RPCs, timers, other mods' calls) and "
           "every Blueprint-to-Blueprint call. Not seen: a native function Blueprint bytecode calls directly, "
           "and plain C++ calls, which reflection never sees.\n"
           "- Parameters and the return value read and write by name for the length of the callback; strings "
           "and containers through `call.parameter(name)`. Calls made inside a callback are not hooked again.\n\n"
           "## Events\n\n"
           "```cpp\n"
           "static u::Subscription hurt = health.OnDamaged().subscribe([](auto &event) {\n"
           "    // event.<Parameter>() as the delegate declares them; event.self() is the component\n"
           "});\n"
           "```\n\n"
           "- A multicast delegate member (inline or sparse) calls back on every broadcast, as a Blueprint "
           "binding would, until the `Subscription` goes away. Subscribe on the game thread.\n"
           "- Members whose signature the dump has (format 4) give a typed view, `<Signature>_Event`; others "
           "take `[](u::HookedCall &call)` with parameters by name. `u::subscribe(place, callback)` works on any "
           "delegate place.\n"
           "- Each subscription is an object of its own bound to the delegate; the game instance keeps it "
           "until unsubscribed.\n\n"
           "## Spawning\n\n"
           "`spawn_actor(world_context, klass, location, rotation)` makes an actor the way Blueprint's SpawnActor "
           "node does (construction script and BeginPlay run); `destroy_actor(actor)` removes it. Game thread.\n\n"
        << details;
    return out.str();
}

} // namespace

namespace UnrealSdkGenerator {

std::string SanitizeProjectName(const std::string &projectName, const std::string &fallback) {
    return mpg::Identifier(projectName, fallback.c_str());
}

bool HasUsableOutput(const std::string &directory) {
    std::error_code ec;
    const fs::path root(directory);
    for (const fs::path &file : {root / "unreal_runtime.h", root / "README.md"}) {
        if (!fs::is_regular_file(file, ec) || fs::file_size(file, ec) == 0 || ec)
            return false;
    }
    return true;
}

std::string TypeDumpPath(const std::string &gameDirectory) {
    return gameDirectory.empty() ? std::string() : (fs::path(gameDirectory) / UnrealTypeCodegen::kDumpFileName).string();
}

bool Generate(const std::string &directory, const std::string &reportDetails, const std::string &typeDumpPath,
              std::string *error) {
    sdk::OutputPlan plan;
    plan.root = fs::path(directory);
    plan.files = {
        {"unreal_runtime.h", {}, UnrealRuntimeModule(), mpg::OutputFilePolicy::GeneratedOverwrite, true, true},
        {"README.md", {}, UnrealSdkReadme(reportDetails), mpg::OutputFilePolicy::GeneratedOverwrite, true, false},
    };
    std::error_code ec;
    if (!typeDumpPath.empty() && fs::is_regular_file(typeDumpPath, ec)) {
        std::vector<UnrealTypeCodegen::Header> headers;
        if (!UnrealTypeCodegen::Build(typeDumpPath, &headers, error))
            return false;
        for (UnrealTypeCodegen::Header &header : headers)
            plan.files.push_back({fs::path("types") / header.fileName, {}, std::move(header.contents),
                                  mpg::OutputFilePolicy::GeneratedOverwrite, true, false, false});
    }
    sdk::OutputResult output;
    return sdk::PublishOutputPlanAtomically(plan, &output, error);
}

bool GenerateModProject(const std::string &projectRoot, const std::string &unrealSdkRoot,
                        const std::string &commonIncludeRoot, const std::string &rawProjectName,
                        const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                        std::string *error) {
    const fs::path root(projectRoot);
    const fs::path sdkRoot(unrealSdkRoot);
    const auto profile = mpg::UnrealBackendProfile();
    fs::path sdkHeaderPath;
    if (!commonIncludeRoot.empty())
        sdkHeaderPath = fs::path(commonIncludeRoot) / "mod_sdk.h";

    if (!HasUsableOutput(sdkRoot.string())) {
        if (error)
            *error = "Unreal SDK is missing or incomplete; generate Unreal SDK before project";
        return false;
    }

    sdk::OutputPlan backendFiles;
    backendFiles.root = root;
    backendFiles.files = {
        {profile.sdkSubdirectory / "unreal_runtime.h",
         sdkRoot / "unreal_runtime.h",
         {},
         mpg::OutputFilePolicy::GeneratedOverwrite,
         true,
         true},
    };
    // types/ is copied whole when the SDK was staged elsewhere; stale headers go.
    std::error_code ec;
    const fs::path projectTypes = root / profile.sdkSubdirectory / "types";
    if (!fs::equivalent(sdkRoot, root / profile.sdkSubdirectory, ec)) {
        fs::remove_all(projectTypes, ec);
        for (fs::recursive_directory_iterator it(sdkRoot / "types", ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec))
                continue;
            backendFiles.files.push_back({profile.sdkSubdirectory / "types" / it->path().lexically_relative(sdkRoot / "types"),
                                          it->path(),
                                          {},
                                          mpg::OutputFilePolicy::GeneratedOverwrite,
                                          true,
                                          false});
        }
    }
    if (!sdk::WriteOutputPlan(backendFiles, nullptr, error))
        return false;

    auto options = mpg::MakeModuleProjectOptions(profile, root, SanitizeProjectName(rawProjectName), sdkHeaderPath);
    options.deployDirectory = (fs::path(gameDirectory) / modsDirectory).string();
    options.enableLocalization = enableLocalization;
    return mpg::WriteModuleProject(options, error);
}

} // namespace UnrealSdkGenerator
