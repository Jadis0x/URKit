#pragma once

#include "runtime_api.h"
#include "sdk/mono/mono_runtime.h"

namespace URK {
inline bool initialize_backend(const ModContext* context) {
  return URK::mono::init(context);
}
} // namespace URK
