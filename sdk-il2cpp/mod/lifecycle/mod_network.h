#pragma once

#include "sdk/mod_sdk.h"

namespace ModNetwork {
bool init(const URK_ModContext* ctx);
void shutdown();
} // namespace ModNetwork
