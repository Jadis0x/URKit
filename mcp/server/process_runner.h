#pragma once

#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace URK::DevMcp {

struct ProcessResult {
    bool started = false;
    bool timedOut = false;
    bool cancelled = false;
    unsigned long exitCode = 0;
    std::string output;
    std::string error;
};

class ProcessCancellation {
  public:
    ProcessCancellation() = default;
    ProcessCancellation(const ProcessCancellation &) = delete;
    ProcessCancellation &operator=(const ProcessCancellation &) = delete;

    void Cancel();
    bool Requested() const;

  private:
    friend ProcessResult RunProcess(const std::filesystem::path &, const std::wstring &,
                                    const std::vector<std::wstring> &, std::chrono::milliseconds,
                                    ProcessCancellation *);

    mutable std::mutex mutex_;
    void *process_ = nullptr;
    bool requested_ = false;
};

ProcessResult RunProcess(const std::filesystem::path &workingDirectory, const std::wstring &executable,
                         const std::vector<std::wstring> &arguments, std::chrono::milliseconds timeout,
                         ProcessCancellation *cancellation = nullptr);

}
