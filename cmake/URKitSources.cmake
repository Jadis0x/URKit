set(URK_ROOT_DIR "${CMAKE_CURRENT_LIST_DIR}/..")
set(URK_SRC_DIR "${URK_ROOT_DIR}/src")
set(URK_SDK_DIR "${URK_ROOT_DIR}/sdk")

function(urk_validate_sources)
    foreach(source_path IN LISTS ARGN)
        if (NOT EXISTS "${source_path}")
            message(FATAL_ERROR "URKit source list references a missing file: ${source_path}")
        endif()
    endforeach()
endfunction()

set(URK_INCLUDE_DIRS
    ${URK_SRC_DIR}
    ${URK_SRC_DIR}/platform
    ${URK_SRC_DIR}/core
    ${URK_SRC_DIR}/core/loader
    ${URK_SRC_DIR}/unity
    ${URK_SRC_DIR}/ui
    ${URK_SRC_DIR}/sdk
    ${URK_SRC_DIR}/proxy
    ${URK_SDK_DIR}
)

set(URK_LOADER_ENTRY_SRC
    ${URK_SRC_DIR}/platform/dllmain.cpp
)

set(URK_CORE_SRC
    ${URK_SRC_DIR}/core/config_public.cpp
    ${URK_SRC_DIR}/core/hooks.cpp
    ${URK_SRC_DIR}/core/hook_manager.cpp
    ${URK_SRC_DIR}/core/loader.cpp
    ${URK_SRC_DIR}/core/loader/loader_paths.cpp
    ${URK_SRC_DIR}/core/loader/loader_selection.cpp
    ${URK_SRC_DIR}/core/loader/loader_lifecycle.cpp
    ${URK_SRC_DIR}/core/loader/process_qualification.cpp
    ${URK_SRC_DIR}/core/loader/il2cpp_mod_context.cpp
    ${URK_SRC_DIR}/core/loader/il2cpp_runtime_backend.cpp
    ${URK_SRC_DIR}/core/loader/main_thread_dispatcher.cpp
    ${URK_SRC_DIR}/core/loader/mod_context.cpp
    ${URK_SRC_DIR}/core/loader/mono_mod_context.cpp
    ${URK_SRC_DIR}/core/loader/mono_runtime_backend.cpp
    ${URK_SRC_DIR}/core/loader/mono_runtime_bootstrap.cpp
    ${URK_SRC_DIR}/core/loader/native_mod_loader.cpp
    ${URK_SRC_DIR}/core/loader/runtime_backend.cpp
    ${URK_SRC_DIR}/core/loader/runtime_discovery.cpp
    ${URK_SRC_DIR}/core/loader/runtime_events.cpp
    ${URK_SRC_DIR}/core/loader/steam_identity.cpp
    ${URK_SRC_DIR}/core/loader/window_message_dispatcher.cpp
    ${URK_SRC_DIR}/core/logger.cpp
    ${URK_SRC_DIR}/core/mod_api.cpp
    ${URK_SRC_DIR}/core/mod_api_mono.cpp
    ${URK_SRC_DIR}/core/mod_lifecycle.cpp
    ${URK_SRC_DIR}/core/network_http.cpp
    ${URK_SRC_DIR}/core/platform_paths.cpp
    ${URK_SRC_DIR}/core/runtime_state.cpp
    ${URK_SRC_DIR}/core/runtime_wait.cpp
)

set(URK_UNITY_SRC
    ${URK_SRC_DIR}/unity/il2cpp_api.cpp
    ${URK_SRC_DIR}/unity/mono_api.cpp
)

set(URK_UI_SRC
    ${URK_SRC_DIR}/ui/intro.cpp
)

set(URK_SDK_GENERATOR_SRC
    ${URK_SRC_DIR}/sdk/mod_project_generator_common.cpp
    ${URK_SRC_DIR}/sdk/mono_sdk_generator.cpp
    ${URK_SRC_DIR}/sdk/il2cpp_project_generator.cpp
    ${URK_SRC_DIR}/sdk/sdk_generator_contract.cpp
    ${URK_SRC_DIR}/sdk/project_manifest.cpp
)

set(URK_PROJECT_UPDATER_SRC
    ${URK_SRC_DIR}/sdk/project_updater.cpp
    ${URK_SRC_DIR}/sdk/updater_self_update.cpp
)

set(URK_SDK_TOOL_SRC
    ${URK_SRC_DIR}/tools/win32_tool_ui.h
    ${URK_SRC_DIR}/tools/sdk_tool/main.cpp
    ${URK_SDK_GENERATOR_SRC}
)

set(URK_UPDATER_SRC
    ${URK_SRC_DIR}/tools/win32_tool_ui.h
    ${URK_SRC_DIR}/tools/updater_tool/main.cpp
    ${URK_SDK_GENERATOR_SRC}
    ${URK_PROJECT_UPDATER_SRC}
)

set(URK_SDK_TEMPLATE_FILES
    ${URK_SRC_DIR}/sdk/templates/mod_project_generator_runtime.inl
    ${URK_SRC_DIR}/sdk/templates/mod_project_generator_ui.inl
    ${URK_SRC_DIR}/sdk/templates/mod_project_generator_unity.inl
)
set_source_files_properties(${URK_SDK_TEMPLATE_FILES} PROPERTIES HEADER_FILE_ONLY TRUE)
list(APPEND URK_SDK_TOOL_SRC ${URK_SDK_TEMPLATE_FILES})
list(APPEND URK_UPDATER_SRC ${URK_SDK_TEMPLATE_FILES})

set(URK_COMMON_SRC
    ${URK_CORE_SRC}
    ${URK_UNITY_SRC}
    ${URK_UI_SRC}
)

set(URK_VERSION_PROXY_SRC ${URK_SRC_DIR}/proxy/version_proxy.cpp)
set(URK_WINHTTP_PROXY_SRC ${URK_SRC_DIR}/proxy/winhttp_proxy.cpp)
set(URK_WINMM_PROXY_SRC ${URK_SRC_DIR}/proxy/winmm_proxy.cpp)

set(URK_VERSION_DEF ${URK_SRC_DIR}/proxy/version.def)
set(URK_WINHTTP_DEF ${URK_SRC_DIR}/proxy/winhttp.def)
set(URK_WINMM_DEF ${URK_SRC_DIR}/proxy/winmm.def)

set(URK_HEADER_CANDIDATES
    ${URK_SRC_DIR}/core/config.h
    ${URK_SRC_DIR}/core/callback_guard.h
    ${URK_SRC_DIR}/core/hooks.h
    ${URK_SRC_DIR}/core/hook_manager.h
    ${URK_SRC_DIR}/core/loader.h
    ${URK_SRC_DIR}/core/loader/loader_paths.h
    ${URK_SRC_DIR}/core/loader/loader_selection.h
    ${URK_SRC_DIR}/core/loader/loader_lifecycle.h
    ${URK_SRC_DIR}/core/loader/process_qualification.h
    ${URK_SRC_DIR}/core/loader/cursor_lease_registry.h
    ${URK_SRC_DIR}/unity/il2cpp_api.h
    ${URK_SRC_DIR}/core/loader/main_thread_dispatcher.h
    ${URK_SRC_DIR}/core/loader/mod_context.h
    ${URK_SRC_DIR}/core/loader/mono_runtime_bootstrap.h
    ${URK_SRC_DIR}/core/loader/native_mod_loader.h
    ${URK_SRC_DIR}/core/loader/runtime_backend.h
    ${URK_SRC_DIR}/core/loader/runtime_discovery.h
    ${URK_SRC_DIR}/core/loader/runtime_events.h
    ${URK_SRC_DIR}/core/loader/steam_identity.h
    ${URK_SRC_DIR}/core/loader/window_message_dispatcher.h
    ${URK_SRC_DIR}/core/logger.h
    ${URK_SRC_DIR}/core/mod_api.h
    ${URK_SRC_DIR}/core/mod_api_internal.h
    ${URK_SRC_DIR}/core/mod_lifecycle.h
    ${URK_SRC_DIR}/core/mod_lifecycle_intercept.h
    ${URK_SRC_DIR}/core/network_http.h
    ${URK_SRC_DIR}/core/platform_paths.h
    ${URK_SRC_DIR}/core/runtime_state.h
    ${URK_SRC_DIR}/core/runtime_wait.h

    ${URK_SRC_DIR}/sdk/mod_project_generator_common.h
    ${URK_SRC_DIR}/sdk/mod_project_generator_profiles.h
    ${URK_SRC_DIR}/sdk/il2cpp_sdk_generator.h
    ${URK_SRC_DIR}/sdk/mono_sdk_generator.h
    ${URK_SRC_DIR}/sdk/sdk_generator_contract.h
    ${URK_SRC_DIR}/sdk/project_manifest.h
    ${URK_SRC_DIR}/sdk/project_updater.h
    ${URK_SRC_DIR}/sdk/updater_self_update.h
    ${URK_SRC_DIR}/sdk/updater_version.h

    ${URK_SRC_DIR}/unity/mono_api.h
    ${URK_SRC_DIR}/unity/il2cpp_export_policy.h


    ${URK_SDK_DIR}/mod_sdk.h
)

set(URK_COMMON_HEADERS)

foreach(header_path IN LISTS URK_HEADER_CANDIDATES)
    if (EXISTS "${header_path}")
        list(APPEND URK_COMMON_HEADERS ${header_path})
    endif()
endforeach()

urk_validate_sources(${URK_COMMON_SRC})
urk_validate_sources(${URK_SDK_TOOL_SRC})
urk_validate_sources(${URK_UPDATER_SRC})
