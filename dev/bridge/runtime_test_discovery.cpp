#include "runtime_test_discovery.h"

#include "dev_test.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

namespace URK::DevBridge {
namespace {

using Json = nlohmann::json;

struct TestModule {
    HMODULE module = nullptr;
    std::string name;
    URK_DevTestCountFn count = nullptr;
    URK_DevTestDescribeFn describe = nullptr;
    URK_DevTestRunFn run = nullptr;
};

std::string Utf8(const wchar_t *value) {
    if (!value || !*value)
        return {};
    const int required = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1)
        return {};
    std::string output(static_cast<std::size_t>(required), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, output.data(), required, nullptr, nullptr);
    output.resize(static_cast<std::size_t>(required - 1));
    return output;
}

std::vector<TestModule> EnumerateTestModules() {
    std::vector<TestModule> modules;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
        return modules;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) {
        do {
            const auto count = reinterpret_cast<URK_DevTestCountFn>(GetProcAddress(entry.hModule, "URK_DevTestCount"));
            const auto describe =
                reinterpret_cast<URK_DevTestDescribeFn>(GetProcAddress(entry.hModule, "URK_DevTestDescribe"));
            const auto run = reinterpret_cast<URK_DevTestRunFn>(GetProcAddress(entry.hModule, "URK_DevTestRun"));
            if (count && describe && run)
                modules.push_back({entry.hModule, Utf8(entry.szModule), count, describe, run});
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    std::sort(modules.begin(), modules.end(), [](const TestModule &left, const TestModule &right) {
        return _stricmp(left.name.c_str(), right.name.c_str()) < 0;
    });
    return modules;
}

bool SafeCount(URK_DevTestCountFn function, std::uint32_t *count, DWORD *exceptionCode) noexcept {
    __try {
        *count = function();
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        *count = 0;
        return false;
    }
}

bool SafeDescribe(URK_DevTestDescribeFn function, std::uint32_t index, URK_DevTestDescriptor *descriptor, int *status,
                  DWORD *exceptionCode) noexcept {
    __try {
        *status = function(index, descriptor);
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        *status = 0;
        return false;
    }
}

bool SafeRun(URK_DevTestRunFn function, const char *name, URK_DevTestResult *result, int *status,
             DWORD *exceptionCode) noexcept {
    __try {
        *status = function(name, result);
        return true;
    } __except ((*exceptionCode = GetExceptionCode(), EXCEPTION_EXECUTE_HANDLER)) {
        *status = 0;
        return false;
    }
}

template <std::size_t Size> std::string BoundedString(const char (&value)[Size]) {
    const std::size_t length = strnlen_s(value, Size);
    return std::string(value, length);
}

std::optional<URK_DevTestDescriptor> Describe(const TestModule &module, std::uint32_t index) {
    URK_DevTestDescriptor descriptor{};
    descriptor.version = URK_DEV_TEST_API_VERSION;
    descriptor.size = sizeof(descriptor);
    int status = 0;
    DWORD exceptionCode = 0;
    if (!SafeDescribe(module.describe, index, &descriptor, &status, &exceptionCode) || status == 0 ||
        descriptor.version != URK_DEV_TEST_API_VERSION || descriptor.size < sizeof(descriptor) ||
        BoundedString(descriptor.name).empty())
        return std::nullopt;
    return descriptor;
}

Json DescriptorJson(const TestModule &module, const URK_DevTestDescriptor &descriptor) {
    const std::string name = BoundedString(descriptor.name);
    return {{"name", name},
            {"qualified_name", module.name + "!" + name},
            {"module", module.name},
            {"source_file", BoundedString(descriptor.sourceFile)},
            {"source_line", descriptor.sourceLine},
            {"tags", BoundedString(descriptor.tags)}};
}

struct TestMatch {
    TestModule module;
    std::string name;
};

std::vector<TestMatch> FindMatches(const std::string &qualifiedName) {
    const std::size_t separator = qualifiedName.find('!');
    const std::string requestedModule =
        separator == std::string::npos ? std::string{} : qualifiedName.substr(0, separator);
    const std::string requestedName =
        separator == std::string::npos ? qualifiedName : qualifiedName.substr(separator + 1);
    std::vector<TestMatch> matches;
    for (const TestModule &module : EnumerateTestModules()) {
        if (!requestedModule.empty() && _stricmp(requestedModule.c_str(), module.name.c_str()) != 0)
            continue;
        std::uint32_t count = 0;
        DWORD exceptionCode = 0;
        if (!SafeCount(module.count, &count, &exceptionCode))
            continue;
        count = (std::min)(count, 512u);
        for (std::uint32_t index = 0; index < count; ++index) {
            const std::optional<URK_DevTestDescriptor> descriptor = Describe(module, index);
            if (!descriptor)
                continue;
            const std::string name = BoundedString(descriptor->name);
            if (name == requestedName)
                matches.push_back({module, name});
        }
    }
    return matches;
}

}

nlohmann::json ListRuntimeTests() {
    Json tests = Json::array();
    Json errors = Json::array();
    for (const TestModule &module : EnumerateTestModules()) {
        std::uint32_t count = 0;
        DWORD exceptionCode = 0;
        if (!SafeCount(module.count, &count, &exceptionCode)) {
            errors.push_back({{"module", module.name}, {"exception_code", exceptionCode}});
            continue;
        }
        count = (std::min)(count, 512u);
        for (std::uint32_t index = 0; index < count; ++index) {
            const std::optional<URK_DevTestDescriptor> descriptor = Describe(module, index);
            if (descriptor)
                tests.push_back(DescriptorJson(module, *descriptor));
            else
                errors.push_back({{"module", module.name}, {"index", index}, {"error", "invalid descriptor"}});
        }
    }
    return {{"tests", std::move(tests)}, {"errors", std::move(errors)}};
}

bool RunRuntimeTest(const std::string &qualifiedName, Json *result, std::string *code, std::string *error) {
    const std::vector<TestMatch> matches = FindMatches(qualifiedName);
    if (matches.empty()) {
        *code = "test_not_found";
        *error = "no loaded mod exports runtime test " + qualifiedName;
        return false;
    }
    if (matches.size() != 1) {
        *code = "ambiguous_test";
        *error = "multiple loaded mods export this test; use module.dll!test_name";
        return false;
    }
    URK_DevTestResult testResult{};
    testResult.version = URK_DEV_TEST_API_VERSION;
    testResult.size = sizeof(testResult);
    const auto started = std::chrono::steady_clock::now();
    int status = 0;
    DWORD exceptionCode = 0;
    const bool invoked =
        SafeRun(matches.front().module.run, matches.front().name.c_str(), &testResult, &status, &exceptionCode);
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - started);
    if (!invoked) {
        *code = "test_crashed";
        *error = "runtime test raised native exception " + std::to_string(exceptionCode);
        return false;
    }
    if (status == 0) {
        *code = "test_execution_failed";
        *error = BoundedString(testResult.message);
        if (error->empty())
            *error = "runtime test export reported execution failure";
        return false;
    }
    if (testResult.version != URK_DEV_TEST_API_VERSION || testResult.size < sizeof(testResult)) {
        *code = "invalid_test_result";
        *error = "runtime test returned an incompatible result structure";
        return false;
    }
    const std::uint64_t duration = testResult.durationMicroseconds != 0 ? testResult.durationMicroseconds
                                                                        : static_cast<std::uint64_t>(elapsed.count());
    *result = {{"name", matches.front().name},
               {"qualified_name", matches.front().module.name + "!" + matches.front().name},
               {"module", matches.front().module.name},
               {"passed", testResult.passed != 0},
               {"duration_microseconds", duration},
               {"message", BoundedString(testResult.message)},
               {"details", BoundedString(testResult.details)}};
    return true;
}

}
