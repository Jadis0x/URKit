#include "process_qualification.h"

#include "loader_lifecycle.h"
#include "runtime_discovery.h"

#include <windows.h>

namespace {
void Refresh(ProcessQualification *qualification) {
    const RuntimeModuleSnapshot snapshot = RuntimeDiscovery_Snapshot();
    qualification->unityPlayerLoaded = snapshot.unityPlayerLoaded;
    qualification->il2cppLoaded = snapshot.il2cppLoaded;
    qualification->monoLoaded = snapshot.monoLoaded;
    qualification->isUnityProcess = snapshot.IsUnityProcess();
}
} // namespace

ProcessQualification ProcessQualification_WaitForUnity(unsigned timeoutMs) {
    ProcessQualification qualification;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        Refresh(&qualification);
        if (qualification.isUnityProcess) {
            qualification.reason = RuntimeDiscovery_UnityReason(
                {qualification.unityPlayerLoaded, qualification.il2cppLoaded, qualification.monoLoaded});
            return qualification;
        }
        if (LoaderLifecycle_StopRequested()) {
            qualification.reason = "loader stop requested";
            return qualification;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            qualification.reason = "no Unity runtime module loaded in the current process";
            return qualification;
        }

        const DWORD remaining = static_cast<DWORD>((deadline - now > 50) ? 50 : deadline - now);
        HANDLE stopEvent = LoaderLifecycle_StopEvent();
        if (stopEvent && WaitForSingleObject(stopEvent, remaining) == WAIT_OBJECT_0) {
            qualification.reason = "loader stop requested";
            return qualification;
        }
        if (!stopEvent)
            Sleep(remaining);
    }
}
