#include "dev_bridge.h"
#include "dev_test.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstddef>
#include <cstring>
#include <string>

namespace {

const URK_ModContext *g_context = nullptr;

void RuntimeTick() {
    URK::DevBridge::Tick();
}

const URK_ModInfo g_modInfo{"URKitDevBridge",
                            "URKit Development Bridge",
                            "Jadis0x",
                            "0.1.0",
                            "",
                            "Local runtime testing bridge for URKit mod development."};

}

extern "C" __declspec(dllexport) const URK_ModInfo *URK_GetModInfo() {
    return &g_modInfo;
}

extern "C" __declspec(dllexport) int ModInitEx(const URK_ModContext *context) {
    const std::size_t requiredSize =
        offsetof(URK_ModContext, MainThreadUnregister) + sizeof(context->MainThreadUnregister);
    if (!context || context->size < requiredSize || !context->MainThreadRegister || !context->MainThreadUnregister)
        return 0;
    std::string error;
    if (!URK::DevBridge::Start(context, &error)) {
        if (context->Log)
            context->Log("[DevBridge][ERROR] %s", error.c_str());
        return 0;
    }
    if (!context->MainThreadRegister(&RuntimeTick)) {
        URK::DevBridge::Stop();
        if (context->Log)
            context->Log("[DevBridge][ERROR] main-thread callback registration failed");
        return 0;
    }
    g_context = context;
    if (context->Log)
        context->Log("[DevBridge] runtime bridge started for PID %lu", GetCurrentProcessId());
    return 1;
}

extern "C" __declspec(dllexport) void ModShutdown() {
    if (g_context && g_context->MainThreadUnregister)
        g_context->MainThreadUnregister(&RuntimeTick);
    URK::DevBridge::Stop();
    g_context = nullptr;
}

extern "C" __declspec(dllexport) uint32_t URK_DevTestCount() {
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestDescribe(uint32_t index, URK_DevTestDescriptor *descriptor) {
    if (index != 0 || !descriptor || descriptor->version != URK_DEV_TEST_API_VERSION ||
        descriptor->size < sizeof(*descriptor))
        return 0;
    strcpy_s(descriptor->name, "urkit.bridge_ready");
    strcpy_s(descriptor->sourceFile, "URKitDevBridge.dll");
    descriptor->sourceLine = 0;
    strcpy_s(descriptor->tags, "smoke,bridge");
    return 1;
}

extern "C" __declspec(dllexport) int URK_DevTestRun(const char *name, URK_DevTestResult *result) {
    if (!name || std::strcmp(name, "urkit.bridge_ready") != 0 || !result ||
        result->version != URK_DEV_TEST_API_VERSION || result->size < sizeof(*result))
        return 0;
    result->passed = g_context && URK::DevBridge::Running();
    strcpy_s(result->message, result->passed ? "URKit DevBridge is running." : "URKit DevBridge is not running.");
    result->details[0] = '\0';
    return 1;
}
