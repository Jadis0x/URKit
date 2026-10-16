#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>

namespace UrkApp {

enum class LogLevel {
    Debug,
    Info,
    Success,
    Warning,
    Error,
};

struct LogEntry {
    std::chrono::system_clock::time_point time;
    LogLevel level = LogLevel::Info;
    std::string source;
    std::string message;
};

// Writes for one subsystem ("generate", "update", "loader", ...).
using LogSink = std::function<void(LogLevel level, const std::string &message)>;

const char *LogLevelName(LogLevel level);
std::string FormatLogTime(std::chrono::system_clock::time_point time);
// "14:02:11.482  INFO   generate  message"
std::string FormatLogEntry(const LogEntry &entry);

// Process-wide activity log. Every entry also goes to %LOCALAPPDATA%\URKit\logs\urk.log.
class ActivityLog {
  public:
    static ActivityLog &Instance();

    void Write(LogLevel level, const std::string &source, const std::string &message);
    LogSink SinkFor(std::string source);
    // Called on the writing thread; the GUI marshals it to the UI thread itself.
    void SetListener(std::function<void(const LogEntry &)> listener);
    std::filesystem::path FilePath() const;
    std::filesystem::path Directory() const;

  private:
    ActivityLog();
    void OpenFileLocked();

    mutable std::mutex mutex_;
    std::function<void(const LogEntry &)> listener_;
    std::filesystem::path directory_;
    std::filesystem::path path_;
    bool fileOpened_ = false;
    bool fileFailed_ = false;
    void *file_ = nullptr;
};

std::filesystem::path LocalDataDirectory();

} // namespace UrkApp
