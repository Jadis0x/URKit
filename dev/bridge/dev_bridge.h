#pragma once

#include "mod_sdk.h"

#include <string>

namespace URK::DevBridge {

bool Start(const URK_ModContext *context, std::string *error);
void Tick();
void Stop();
bool Running();

}
