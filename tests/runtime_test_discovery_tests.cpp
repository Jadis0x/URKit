#include "dev/bridge/runtime_test_discovery.h"
#include "dev_test.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {

void Require(bool condition, const char *message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}

extern "C" __declspec(dllexport) uint32_t URK_DevTestCount() {
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestDescribe(uint32_t index, URK_DevTestDescriptor *descriptor) {
    if (index != 0 || !descriptor || descriptor->version != URK_DEV_TEST_API_VERSION ||
        descriptor->size < sizeof(*descriptor))
        return 0;
    strcpy_s(descriptor->name, "fixture.pass");
    strcpy_s(descriptor->sourceFile, __FILE__);
    descriptor->sourceLine = __LINE__;
    strcpy_s(descriptor->tags, "fixture");
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestRun(const char *name, URK_DevTestResult *result) {
    if (!name || std::strcmp(name, "fixture.pass") != 0 || !result || result->version != URK_DEV_TEST_API_VERSION ||
        result->size < sizeof(*result))
        return 0;
    result->passed = 1;
    strcpy_s(result->message, "fixture passed");
    result->details[0] = '\0';
    return 1;
}

int main() {
    const nlohmann::json listed = URK::DevBridge::ListRuntimeTests();
    bool found = false;
    for (const nlohmann::json &test : listed["tests"])
        if (test.value("name", std::string{}) == "fixture.pass")
            found = true;
    Require(found, "exported runtime test is discovered");

    nlohmann::json result;
    std::string code;
    std::string error;
    Require(URK::DevBridge::RunRuntimeTest("fixture.pass", &result, &code, &error), "exported runtime test executes");
    Require(result.value("passed", false) && result.value("message", std::string{}) == "fixture passed",
            "runtime test result is preserved");
    return 0;
}
