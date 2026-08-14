#include "process_runner.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <vector>

namespace URK::DevMcp {
namespace {

class Handle {
  public:
    Handle() = default;
    explicit Handle(HANDLE value) : value_(value) {
    }
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    Handle(Handle &&other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }
    Handle &operator=(Handle &&other) noexcept {
        if (this != &other) {
            if (value_ && value_ != INVALID_HANDLE_VALUE)
                CloseHandle(value_);
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    HANDLE Get() const {
        return value_;
    }
    HANDLE Release() {
        HANDLE value = value_;
        value_ = nullptr;
        return value;
    }

  private:
    HANDLE value_ = nullptr;
};

class ThreadAttributes {
  public:
    ThreadAttributes() = default;
    bool Initialize(const std::array<HANDLE, 2> &handles) {
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        if (bytes == 0)
            return false;
        storage_.resize(bytes);
        list_ = reinterpret_cast<PPROC_THREAD_ATTRIBUTE_LIST>(storage_.data());
        if (!InitializeProcThreadAttributeList(list_, 1, 0, &bytes)) {
            list_ = nullptr;
            return false;
        }
        if (!UpdateProcThreadAttribute(list_, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       const_cast<HANDLE *>(handles.data()), sizeof(handles), nullptr, nullptr)) {
            DeleteProcThreadAttributeList(list_);
            list_ = nullptr;
            return false;
        }
        return true;
    }
    ~ThreadAttributes() {
        if (list_)
            DeleteProcThreadAttributeList(list_);
    }
    ThreadAttributes(const ThreadAttributes &) = delete;
    ThreadAttributes &operator=(const ThreadAttributes &) = delete;
    PPROC_THREAD_ATTRIBUTE_LIST Get() const {
        return list_;
    }

  private:
    std::vector<unsigned char> storage_;
    PPROC_THREAD_ATTRIBUTE_LIST list_ = nullptr;
};

std::wstring QuoteArgument(std::wstring_view argument) {
    if (argument.empty())
        return L"\"\"";
    if (argument.find_first_of(L" \t\"") == std::wstring_view::npos)
        return std::wstring(argument);
    std::wstring result = L"\"";
    std::size_t backslashes = 0;
    for (const wchar_t value : argument) {
        if (value == L'\\') {
            ++backslashes;
            continue;
        }
        if (value == L'\"') {
            result.append(backslashes * 2 + 1, L'\\');
            result.push_back(L'\"');
            backslashes = 0;
            continue;
        }
        result.append(backslashes, L'\\');
        backslashes = 0;
        result.push_back(value);
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}

std::wstring BuildCommandLine(const std::wstring &executable, const std::vector<std::wstring> &arguments) {
    std::wstring command = QuoteArgument(executable);
    for (const std::wstring &argument : arguments) {
        command.push_back(L' ');
        command += QuoteArgument(argument);
    }
    return command;
}

std::string WindowsError(DWORD code) {
    std::array<char, 512> buffer{};
    const DWORD length = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0,
                                        buffer.data(), static_cast<DWORD>(buffer.size()), nullptr);
    if (length == 0)
        return "Windows error " + std::to_string(code);
    std::string message(buffer.data(), length);
    while (!message.empty() && (message.back() == '\r' || message.back() == '\n' || message.back() == ' '))
        message.pop_back();
    return message + " (" + std::to_string(code) + ")";
}

std::string ReadBoundedOutput(const std::filesystem::path &path) {
    constexpr std::streamoff kMaximumBytes = 256 * 1024;
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return {};
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    const std::streamoff start = size > kMaximumBytes ? size - kMaximumBytes : 0;
    input.seekg(start, std::ios::beg);
    std::string output(static_cast<std::size_t>(size - start), '\0');
    input.read(output.data(), static_cast<std::streamsize>(output.size()));
    output.resize(static_cast<std::size_t>(input.gcount()));
    if (start > 0)
        output.insert(0, "<output truncated>\n");
    return output;
}

}

void ProcessCancellation::Cancel() {
    std::lock_guard lock(mutex_);
    requested_ = true;
    if (process_)
        TerminateProcess(static_cast<HANDLE>(process_), ERROR_CANCELLED);
}

bool ProcessCancellation::Requested() const {
    std::lock_guard lock(mutex_);
    return requested_;
}

ProcessResult RunProcess(const std::filesystem::path &workingDirectory, const std::wstring &executable,
                         const std::vector<std::wstring> &arguments, std::chrono::milliseconds timeout,
                         ProcessCancellation *cancellation) {
    ProcessResult result;
    if (cancellation && cancellation->Requested()) {
        result.cancelled = true;
        return result;
    }
    std::array<wchar_t, MAX_PATH> temporaryDirectory{};
    if (GetTempPathW(static_cast<DWORD>(temporaryDirectory.size()), temporaryDirectory.data()) == 0) {
        result.error = WindowsError(GetLastError());
        return result;
    }
    std::array<wchar_t, MAX_PATH> temporaryFile{};
    if (GetTempFileNameW(temporaryDirectory.data(), L"urk", 0, temporaryFile.data()) == 0) {
        result.error = WindowsError(GetLastError());
        return result;
    }
    const std::filesystem::path outputPath(temporaryFile.data());
    Handle output(CreateFileW(outputPath.c_str(), GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr));
    if (output.Get() == INVALID_HANDLE_VALUE) {
        result.error = WindowsError(GetLastError());
        std::error_code cleanup;
        std::filesystem::remove(outputPath, cleanup);
        return result;
    }
    if (!SetHandleInformation(output.Get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        result.error = WindowsError(GetLastError());
        output = Handle{};
        std::error_code cleanup;
        std::filesystem::remove(outputPath, cleanup);
        return result;
    }

    Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, nullptr));
    if (input.Get() == INVALID_HANDLE_VALUE ||
        !SetHandleInformation(input.Get(), HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT)) {
        result.error = WindowsError(GetLastError());
        output = Handle{};
        std::error_code cleanup;
        std::filesystem::remove(outputPath, cleanup);
        return result;
    }
    ThreadAttributes attributes;
    if (!attributes.Initialize({output.Get(), input.Get()})) {
        result.error = WindowsError(GetLastError());
        output = Handle{};
        std::error_code cleanup;
        std::filesystem::remove(outputPath, cleanup);
        return result;
    }

    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
    startup.StartupInfo.hStdOutput = output.Get();
    startup.StartupInfo.hStdError = output.Get();
    startup.StartupInfo.hStdInput = input.Get();
    startup.lpAttributeList = attributes.Get();
    PROCESS_INFORMATION process{};
    std::wstring commandLine = BuildCommandLine(executable, arguments);
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');
    const BOOL created = CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr,
                                        workingDirectory.c_str(), &startup.StartupInfo, &process);
    if (!created) {
        result.error = WindowsError(GetLastError());
        output = Handle{};
        std::error_code cleanup;
        std::filesystem::remove(outputPath, cleanup);
        return result;
    }
    result.started = true;
    Handle processHandle(process.hProcess);
    Handle threadHandle(process.hThread);
    if (cancellation) {
        std::lock_guard lock(cancellation->mutex_);
        cancellation->process_ = processHandle.Get();
        if (cancellation->requested_)
            TerminateProcess(processHandle.Get(), ERROR_CANCELLED);
    }
    const DWORD waitMilliseconds =
        static_cast<DWORD>((std::min)(timeout.count(), static_cast<long long>(MAXDWORD - 1)));
    const DWORD wait = WaitForSingleObject(processHandle.Get(), waitMilliseconds);
    if (wait == WAIT_TIMEOUT) {
        result.timedOut = true;
        TerminateProcess(processHandle.Get(), ERROR_TIMEOUT);
        WaitForSingleObject(processHandle.Get(), 5000);
    } else if (wait != WAIT_OBJECT_0) {
        result.error = WindowsError(GetLastError());
    }
    if (!GetExitCodeProcess(processHandle.Get(), &result.exitCode))
        result.error = WindowsError(GetLastError());
    if (cancellation) {
        std::lock_guard lock(cancellation->mutex_);
        cancellation->process_ = nullptr;
        result.cancelled = cancellation->requested_;
    }
    output = Handle{};
    result.output = ReadBoundedOutput(outputPath);
    std::error_code cleanup;
    std::filesystem::remove(outputPath, cleanup);
    return result;
}

}
