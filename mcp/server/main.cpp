#include "mcp_server.h"

#include "src/project_version.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string_view>

int wmain(int argc, wchar_t **argv) {
    SetConsoleCP(CP_UTF8);
    SetConsoleOutputCP(CP_UTF8);
    std::filesystem::path projectRoot = std::filesystem::current_path();
    std::optional<std::uint32_t> gamePid;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view argument = argv[index];
        if (argument == L"--help") {
            std::cerr << "URKit Development MCP " << UrkVersion::kReleaseVersion << "\n"
                         "Usage: urk-dev-mcp.exe [--project <path>] [--game-pid <pid>]\n";
            return 0;
        }
        if (argument == L"--project" && index + 1 < argc) {
            projectRoot = std::filesystem::path(argv[++index]);
            continue;
        }
        if (argument == L"--game-pid" && index + 1 < argc) {
            const wchar_t *value = argv[++index];
            wchar_t *end = nullptr;
            errno = 0;
            const unsigned long parsed = std::wcstoul(value, &end, 10);
            if (errno == ERANGE || !end || *end != L'\0' || parsed == 0 ||
                parsed > (std::numeric_limits<std::uint32_t>::max)()) {
                std::cerr << "--game-pid requires a positive integer\n";
                return 2;
            }
            gamePid = static_cast<std::uint32_t>(parsed);
            continue;
        }
        std::wcerr << L"Unknown or incomplete argument: " << argument << L'\n';
        return 2;
    }
    std::cerr << "URKit Development MCP " << UrkVersion::kReleaseVersion << "\n"
              << "Project: " << projectRoot.string() << '\n'
              << "Target : " << (gamePid ? "game PID " + std::to_string(*gamePid) : "automatic bridge discovery")
              << "\nTransport: stdio\n";
    URK::DevMcp::McpServer server(std::move(projectRoot), gamePid);
    return server.Run();
}
