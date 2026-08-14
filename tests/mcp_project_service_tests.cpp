#include "mcp/server/project_service.h"
#include "mcp/server/process_runner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

namespace fs = std::filesystem;

void Require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

void Write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << text;
    Require(static_cast<bool>(output), "test fixture file is written");
}

}

int main(int argc, char **argv) {
    if (argc == 2 && std::string(argv[1]) == "--wait-child") {
        Sleep(30000);
        return 0;
    }
    std::array<wchar_t, 32768> executablePath{};
    const DWORD executableLength =
        GetModuleFileNameW(nullptr, executablePath.data(), static_cast<DWORD>(executablePath.size()));
    Require(executableLength > 0 && executableLength < executablePath.size(), "test executable path is available");
    URK::DevMcp::ProcessCancellation cancellation;
    std::thread canceller([&cancellation] {
        Sleep(50);
        cancellation.Cancel();
    });
    const URK::DevMcp::ProcessResult cancelled =
        URK::DevMcp::RunProcess(fs::current_path(), std::wstring(executablePath.data(), executableLength),
                                {L"--wait-child"}, std::chrono::seconds(10), &cancellation);
    canceller.join();
    Require(cancelled.started && cancelled.cancelled, "spawned process cancellation is observable");

    const fs::path root = fs::temp_directory_path() / ("urkit-mcp-test-" + std::to_string(GetCurrentProcessId()));
    const fs::path game = root / "game";
    std::error_code cleanup;
    fs::remove_all(root, cleanup);
    fs::create_directories(game / "Mods");
    Write(root / ".urk" / "project.ini", "schema_version=1\n"
                                         "sdk_version=30\n"
                                         "backend=mono\n"
                                         "project_name=SampleMod\n"
                                         "game_directory=" +
                                             game.generic_string() +
                                             "\n"
                                             "mods_directory=Mods\n"
                                             "enable_localization=0\n");
    Write(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.28)\n"
                                   "project(SampleMod LANGUAGES CXX)\n"
                                   "add_library(SampleMod SHARED sample.cpp)\n"
                                   "add_custom_command(TARGET SampleMod POST_BUILD\n"
                                   "  COMMAND ${CMAKE_COMMAND} -E make_directory \"" +
                                       (game / "Mods").generic_string() +
                                       "\"\n"
                                       "  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SampleMod> \"" +
                                       (game / "Mods").generic_string() +
                                       "\"\n"
                                       ")\n");
    Write(root / "sample.cpp", "extern \"C\" __declspec(dllexport) int sample_value() { return 1; }\n");
    const nlohmann::json presets = {
        {"version", 3},
        {"configurePresets",
         {{{"name", "clang-debug"},
           {"generator", "Ninja"},
           {"binaryDir", "${sourceDir}/out/build/${presetName}"},
           {"cacheVariables", {{"CMAKE_BUILD_TYPE", "Debug"}, {"CMAKE_CXX_COMPILER", URK_TEST_CXX_COMPILER}}}}}},
        {"buildPresets", {{{"name", "clang-debug"}, {"configurePreset", "clang-debug"}}}}};
    Write(root / "CMakePresets.json", presets.dump());
    Write(game / "URKit_logs.log", "one\ntwo\nthree\n");

    URK::DevMcp::ProjectService service(root);
    const URK::DevMcp::ServiceResult info = service.ProjectInfo();
    Require(info.ok && info.value["project_name"] == "SampleMod", "project manifest is inspected");
    const URK::DevMcp::ServiceResult logs = service.ReadLogs(2);
    Require(logs.ok && logs.value["text"] == "two\nthree\n", "log tail is bounded by lines");
    const URK::DevMcp::ServiceResult built = service.Build("clang-debug");
    Require(built.ok && built.value.value("deployed", false), "mod is built and deployed through CMake");
    const URK::DevMcp::ServiceResult deployed = service.Deploy("clang-debug");
    Require(deployed.ok && fs::is_regular_file(game / "Mods" / "SampleMod.dll"), "artifact is deployed");
    const URK::DevMcp::ServiceResult invalid = service.Deploy("unknown");
    Require(!invalid.ok && invalid.code == "invalid_preset", "undeclared preset is rejected");

    fs::remove_all(root, cleanup);
    return 0;
}
#include <array>
#include <chrono>
