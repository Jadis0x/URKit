#pragma once

#include "sdk/mod_sdk.h"

namespace ModHooks {
bool install(const URK_ModContext* ctx);
void uninstall();
} // namespace ModHooks
