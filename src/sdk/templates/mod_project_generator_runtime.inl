// Runtime templates are split along public API and generated source ownership
// boundaries so an ABI change does not require editing lifecycle or mod files.

#include "runtime/runtime_api.inl"
#include "runtime/hooks_api.inl"
#include "runtime/network_api.inl"
#include "runtime/project_support.inl"
#include "runtime/events_async.inl"
#include "runtime/lifecycle.inl"
