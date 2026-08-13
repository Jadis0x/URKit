#pragma once

#include "config.h"

struct RuntimeBackendDescriptor {
    const char *name;
    bool implemented;
    bool (*run)(Config &config);
};

const RuntimeBackendDescriptor &RuntimeBackend_Select(const Config &config);
bool RuntimeBackend_Run(const RuntimeBackendDescriptor &backend, Config &config);

const RuntimeBackendDescriptor &RuntimeBackend_Mono();
const RuntimeBackendDescriptor &RuntimeBackend_Il2Cpp();
