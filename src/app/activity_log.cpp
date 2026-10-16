#include "activity_log.h"

#include "project_version.h"

#include <cstdio>
#include <ctime>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

namespace UrkApp {
namespace {

namespace fs = std::filesystem;

constexpr std::uintmax_t kRotateBytes = 2 * 1024 * 1024;

std::tm LocalTime(std::chrono::system_clock::time_point time) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(time);
    std::tm local{};
    localtime_s(&local, &seconds);
    return local;
}

} // namespace

const char *LogLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Success:
            return "OK";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

std::string FormatLogTime(std::chrono::system_clock::time_point time) {
    const std::tm local = LocalTime(time);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count() % 1000;
    char buffer[32]{};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min, local.tm_sec,
                  static_cast<int>(millis));
    return buffer;
}

std::string FormatLogEntry(const LogEntry &entry) {
    char prefix[64]{};
    std::snprintf(prefix, sizeof(prefix), "%s  %-5s  %-8s  ", FormatLogTime(entry.time).c_str(),
                  LogLevelName(entry.level), entry.source.c_str());
    return prefix + entry.message;
}

fs::path LocalDataDirectory() {
    PWSTR known = nullptr;
    fs::path base;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &known)) && known)
        base = fs::path(known);
    if (known)
        CoTaskMemFree(known);
    if (base.empty()) {
        std::error_code ec;
        base = fs::temp_directory_path(ec);
    }
    return base / L"URKit";
}

ActivityLog &ActivityLog::Instance() {
    static ActivityLog log;
    return log;
}

ActivityLog::ActivityLog() : directory_(LocalDataDirectory() / L"logs"), path_(directory_ / L"urk.log") {}

fs::path ActivityLog::FilePath() const {
    return path_;
}

fs::path ActivityLog::Directory() const {
    return directory_;
}

void ActivityLog::SetListener(std::function<void(const LogEntry &)> listener) {
    std::lock_guard lock(mutex_);
    listener_ = std::move(listener);
}

void ActivityLog::OpenFileLocked() {
    fileOpened_ = true;
    std::error_code ec;
    fs::create_directories(directory_, ec);
    if (fs::exists(path_, ec) && fs::file_size(path_, ec) > kRotateBytes && !ec) {
        fs::path previous = path_;
        previous += L".1";
        fs::remove(previous, ec);
        fs::rename(path_, previous, ec);
    }
    FILE *file = nullptr;
    if (_wfopen_s(&file, path_.c_str(), L"ab") != 0 || !file) {
        fileFailed_ = true;
        return;
    }
    file_ = file;
    const LogEntry header{std::chrono::system_clock::now(), LogLevel::Info, "session",
                          "urk.exe " + std::string(UrkVersion::kReleaseVersion) + " started, pid " +
                              std::to_string(GetCurrentProcessId())};
    std::fprintf(file, "\n%s\n", FormatLogEntry(header).c_str());
    std::fflush(file);
}

void ActivityLog::Write(LogLevel level, const std::string &source, const std::string &message) {
    LogEntry entry{std::chrono::system_clock::now(), level, source, message};
    std::function<void(const LogEntry &)> listener;
    {
        std::lock_guard lock(mutex_);
        if (!fileOpened_)
            OpenFileLocked();
        if (file_) {
            auto *file = static_cast<FILE *>(file_);
            std::fprintf(file, "%s\n", FormatLogEntry(entry).c_str());
            std::fflush(file);
        }
        listener = listener_;
    }
    if (listener)
        listener(entry);
}

LogSink ActivityLog::SinkFor(std::string source) {
    return [this, source = std::move(source)](LogLevel level, const std::string &message) {
        Write(level, source, message);
    };
}

} // namespace UrkApp
