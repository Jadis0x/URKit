#include "logger.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>
#include <windows.h>

static FILE *g_file = nullptr;
static bool g_console = false;
static HANDLE g_consoleOutput = INVALID_HANDLE_VALUE;
static bool g_consoleColors = false;
static WORD g_consoleDefaultAttributes = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
static std::mutex g_logMutex;
static std::wstring g_logPath;
static ULONGLONG g_lastFileFlushTick = 0;
static constexpr ULONGLONG kInfoFlushIntervalMs = 250;

static std::wstring ExecutableDirectory() {
    std::vector<wchar_t> path(512);
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!length)
            return {};
        if (length < path.size() - 1) {
            std::wstring result(path.data(), length);
            const size_t slash = result.find_last_of(L"\\/");
            return slash == std::wstring::npos ? std::wstring() : result.substr(0, slash + 1);
        }
        path.resize(path.size() * 2);
    }
}

enum class LogSeverity {
    Info,
    Warning,
    Error,
    Success,
};

static bool HasSeverityTag(const char *message, const char *tag) {
    const size_t tagLength = strlen(tag);
    for (const char *cursor = message; *cursor; ++cursor) {
        if (*cursor != '[')
            continue;

        size_t index = 0;
        for (; index < tagLength && cursor[index]; ++index) {
            const char character = cursor[index];
            const char uppercaseCharacter = character >= 'a' && character <= 'z' ? character - ('a' - 'A') : character;
            if (uppercaseCharacter != tag[index])
                break;
        }
        if (index == tagLength)
            return true;
    }
    return false;
}

static LogSeverity DetectLogSeverity(const char *message) {
    if (HasSeverityTag(message, "[ERROR]"))
        return LogSeverity::Error;
    if (HasSeverityTag(message, "[WARNING]") || HasSeverityTag(message, "[WARN]"))
        return LogSeverity::Warning;
    if (HasSeverityTag(message, "[SUCCESS]") || HasSeverityTag(message, "[LOADED]") ||
        HasSeverityTag(message, "[OK]"))
        return LogSeverity::Success;
    return LogSeverity::Info;
}

static WORD ConsoleAttributesForSeverity(LogSeverity severity) {
    switch (severity) {
    case LogSeverity::Error:
        return FOREGROUND_RED | FOREGROUND_INTENSITY;
    case LogSeverity::Warning:
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    case LogSeverity::Info:
        return FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    case LogSeverity::Success:
        return FOREGROUND_GREEN | FOREGROUND_INTENSITY;
    }

    return g_consoleDefaultAttributes;
}

static void SetConsoleAttributes(WORD attributes) {
    if (g_consoleColors)
        SetConsoleTextAttribute(g_consoleOutput, attributes);
}

static void ConsoleWriteLine(const char *prefix, const char *message, LogSeverity severity) {
    if (!g_console)
        return;

    SetConsoleAttributes(FOREGROUND_INTENSITY);
    fputs(prefix, stdout);
    SetConsoleAttributes(ConsoleAttributesForSeverity(severity));
    fputs(message, stdout);
    fputc('\n', stdout);
    SetConsoleAttributes(g_consoleDefaultAttributes);
    fflush(stdout);
}

void Log_Init(bool showConsole, const std::string &logDirectory) {
    const bool consoleRequested = showConsole;
    DWORD consoleError = ERROR_SUCCESS;
    DWORD consoleColorError = ERROR_SUCCESS;
    errno_t fileError = 0;
    {
        std::lock_guard<std::mutex> lock(g_logMutex);
        if (showConsole) {
            if (AllocConsole() || GetLastError() == ERROR_ACCESS_DENIED) {
                FILE *dummy = nullptr;
                if (freopen_s(&dummy, "CONOUT$", "w", stdout) == 0) {
                    SetConsoleOutputCP(CP_UTF8);
                    SetConsoleTitleA("URKit");
                    g_console = true;
                    g_consoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
                    CONSOLE_SCREEN_BUFFER_INFO consoleInfo{};
                    if (g_consoleOutput != INVALID_HANDLE_VALUE &&
                        GetConsoleScreenBufferInfo(g_consoleOutput, &consoleInfo)) {
                        g_consoleDefaultAttributes = consoleInfo.wAttributes;
                        g_consoleColors = true;
                    } else {
                        consoleColorError = GetLastError();
                    }
                } else {
                    consoleError = GetLastError();
                }
            } else {
                consoleError = GetLastError();
            }
        }

        if (logDirectory.empty()) {
            g_logPath = ExecutableDirectory() + L"URKit_logs.log";
        } else {
            g_logPath = (std::filesystem::path(logDirectory) / L"URKit_logs.log").wstring();
        }
        fileError = _wfopen_s(&g_file, g_logPath.c_str(), L"w");
        g_lastFileFlushTick = GetTickCount64();
    }

    if (g_file) {
        Log("[logger] initialized: file='%ls', consoleRequested=%s, "
            "consoleActive=%s.",
            g_logPath.c_str(), consoleRequested ? "yes" : "no", g_console ? "yes" : "no");
    } else {
        Log("[logger][ERROR] cannot open '%ls' (errno=%d); diagnostics will only "
            "be sent "
            "to the console/debugger.",
            g_logPath.c_str(), static_cast<int>(fileError));
    }
    if (consoleRequested && !g_console) {
        Log("[logger][WARNING] console requested but unavailable (Win32 "
            "error=%lu).",
            consoleError);
    }
    if (g_console && !g_consoleColors) {
        Log("[logger][WARNING] console colors unavailable (Win32 error=%lu); "
            "console output will remain uncolored.",
            consoleColorError);
    }
}

void Log_Shutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_file) {
        fclose(g_file);
        g_file = nullptr;
    }
    if (g_console) {
        SetConsoleAttributes(g_consoleDefaultAttributes);
        FreeConsole();
        g_console = false;
        g_consoleOutput = INVALID_HANDLE_VALUE;
        g_consoleColors = false;
    }
}

void Log(const char *fmt, ...) {
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    const int messageLength = vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    if (messageLength < 0) {
        strcpy_s(msg, "[logger][ERROR] failed to format log message");
    } else if (messageLength >= static_cast<int>(sizeof(msg))) {
        constexpr char suffix[] = "... [truncated]";
        memcpy(msg + sizeof(msg) - sizeof(suffix), suffix, sizeof(suffix));
    }

    SYSTEMTIME timestamp;
    GetLocalTime(&timestamp);
    char prefix[192];
    snprintf(prefix, sizeof(prefix),
             "[%04u-%02u-%02u %02u:%02u:%02u.%03u] [URKit] [pid:%lu "
             "tid:%lu] ",
             timestamp.wYear, timestamp.wMonth, timestamp.wDay, timestamp.wHour, timestamp.wMinute, timestamp.wSecond,
             timestamp.wMilliseconds, GetCurrentProcessId(), GetCurrentThreadId());

    char line[2240];
    snprintf(line, sizeof(line), "%s%s\n", prefix, msg);
    const LogSeverity severity = DetectLogSeverity(msg);

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_console) {
        ConsoleWriteLine(prefix, msg, severity);
    }
    if (g_file) {
        fputs(line, g_file);
        const ULONGLONG now = GetTickCount64();
        if (severity != LogSeverity::Info || now - g_lastFileFlushTick >= kInfoFlushIntervalMs) {
            fflush(g_file);
            g_lastFileFlushTick = now;
        }
    }
    OutputDebugStringA(line);
}
