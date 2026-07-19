#include "mod_network.h"

#include "sdk/runtime_api.h"

namespace ModNetwork {
bool init(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  return true;
}

void shutdown() {}
} // namespace ModNetwork
