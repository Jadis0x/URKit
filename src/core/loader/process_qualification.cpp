#include "process_qualification.h"

#include "loader_lifecycle.h"
#include "runtime_discovery.h"

#include <windows.h>

namespace {
RuntimeModuleSnapshot Refresh(ProcessQualification *qualification) {
    const RuntimeModuleSnapshot snapshot = RuntimeDiscovery_Snapshot();
    qualification->unityPlayerLoaded = snapshot.unityPlayerLoaded;
    qualification->il2cppLoaded = snapshot.il2cppLoaded;
    qualification->monoLoaded = snapshot.monoLoaded;
    qualification->unrealDetected = snapshot.unrealDetected;
    qualification->isSupportedRuntime = snapshot.IsSupportedProcess();
    return snapshot;
}
} // namespace

ProcessQualification ProcessQualification_WaitForRuntime(unsigned timeoutMs) {
    ProcessQualification qualification;
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;
    for (;;) {
        const RuntimeModuleSnapshot snapshot = Refresh(&qualification);
        if (qualification.isSupportedRuntime) {
            qualification.reason = RuntimeDiscovery_QualificationReason(snapshot);
            return qualification;
        }
        if (LoaderLifecycle_StopRequested()) {
            qualification.reason = "loader stop requested";
            return qualification;
        }
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            qualification.reason = "no supported runtime in the current process";
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
