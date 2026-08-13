#pragma once

#include "mono_api.h"
#include "runtime_state.h"

struct MonoRuntimeBootstrapResult {
    MonoDomain *domain = nullptr;
    MonoThread *attachedThread = nullptr;
};

MonoRuntimeBootstrapResult MonoRuntimeBootstrap_Attach(MonoApi &mono, RuntimeState &state, int timeoutMs);