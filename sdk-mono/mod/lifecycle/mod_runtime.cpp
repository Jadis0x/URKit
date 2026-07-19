#include "mod_runtime.h"

#include "support/mod_log.h"

#include "sdk/runtime_api.h"
#include "sdk/runtime_bootstrap.h"
#include "sdk/unity/unity.h"

namespace ModRuntime {
bool start(const URK_ModContext* ctx) {
  URK::set_context(ctx);
  if (!URK::initialize_backend(ctx)) {
    ModLog::error("Mono runtime API initialization failed");
    return false;
  }

  ModLog::info("runtime ready: backend=Mono main_thread=%s scene_events=%s", URK::has_main_thread() ? "yes" : "no", URK::has_scene_events() ? "yes" : "no");
  return true;
}

void update() {
  // Main-thread Unity work goes here. This callback is registered by mod_lifecycle.cpp when the loader exposes URK_RUNTIME_CAP_MAIN_THREAD.
}

void on_scene_loaded(const URK_SceneInfo* scene) {
  if (!scene || scene->size < sizeof(URK_SceneInfo)) return;
  ModLog::info("scene loaded: name=%s buildIndex=%d handle=%d", scene->name, scene->buildIndex, scene->handle);
}

void on_scene_changed(const URK_SceneInfo* previousScene, const URK_SceneInfo* currentScene) {
  if (!previousScene || !currentScene || previousScene->size < sizeof(URK_SceneInfo) || currentScene->size < sizeof(URK_SceneInfo)) return;
  ModLog::info("scene changed: %s -> %s", previousScene->name, currentScene->name);
}

void on_object_destroy_requested(const URK_ObjectDestroyRequest* request) {
  if (!request || request->size < sizeof(URK_ObjectDestroyRequest)) return;
  ModLog::info("object destroy requested: name=%s type=%s instanceId=%d delay=%.3f immediate=%s",
               request->name, request->typeName, request->instanceId, request->delaySeconds,
               (request->flags & URK_OBJECT_DESTROY_REQUEST_IMMEDIATE) ? "yes" : "no");
}

void stop() {
  ModLog::info("runtime stopped");
}
} // namespace ModRuntime
