cmake_minimum_required(VERSION 3.28)

if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(CMAKE_CXX_COMPILER clang++ CACHE STRING "Clang compiler")

set(VCPKG_APPLOCAL_DEPS OFF CACHE BOOL "Generated mods keep dependencies linked into the mod DLL when possible" FORCE)

project(BlankMono LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "Use the static Microsoft C/C++ runtime" FORCE)
endif()

if(NOT WIN32)
    message(FATAL_ERROR "Generated URKit starter mods target Windows native Unity processes.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Generated URKit starter mods support x64 targets only.")
endif()

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies as shared libraries" FORCE)

function(urk_disable_vs_vcpkg_integration target_name)
    if(MSVC)
        set_target_properties(${target_name} PROPERTIES VS_GLOBAL_VcpkgEnabled "false")
    endif()
endfunction()

include(FetchContent)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.2b-docking
)
FetchContent_MakeAvailable(imgui)
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx12.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
set_target_properties(imgui PROPERTIES CXX_SCAN_FOR_MODULES OFF)
if(MSVC)
    target_compile_options(imgui PRIVATE /W0 /permissive- /EHsc /utf-8)
endif()
urk_disable_vs_vcpkg_integration(imgui)

add_library(BlankMono SHARED)

target_include_directories(BlankMono PRIVATE . mod)

set(URK_PROJECT_SOURCES
        mod/support/mod_log.cpp
        mod/hooks/mod_hooks.cpp
        mod/lifecycle/mod_network.cpp
        mod/lifecycle/mod_runtime.cpp
        mod/generated/mod_lifecycle.cpp
        mod/generated/mod_entry.cpp
        sdk/runtime_api.h
        sdk/runtime_bootstrap.h
        sdk/hook_api.h
        sdk/network_api.h
        sdk/events.h
        sdk/coroutines.h
        sdk/mod_async.h
        sdk/unity/unity.h
        sdk/unity/unity_types.h
        sdk/unity/unity_invoke.h
        sdk/unity/unity_components.h
        sdk/unity/unity_inspect.h
        sdk/unity/unity_shortcuts.h
        mod/ui/theme.h
        mod/ui/localization.h
        mod/ui/widgets.h
        mod/ui/tabs/about_tab.h
        mod/ui/tabs/config_tab.h
        mod/ui/menu.h
        mod/ui/highlight.h
        mod/hooks/render_imgui_hook.h
        mod/hooks/unity_log_hook.h
        mod/generated/mod_lifecycle.h
        sdk/mono/mono_runtime.h
        sdk/mono/mono_helpers.h
)

# User modules under mod are picked up automatically after CMake reconfigures.
file(GLOB_RECURSE URK_USER_MODULE_FILES CONFIGURE_DEPENDS
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/mod/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/mod/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mod/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/mod/*.cc"
    "${CMAKE_CURRENT_SOURCE_DIR}/mod/*.cxx"
)

list(REMOVE_ITEM URK_USER_MODULE_FILES ${URK_PROJECT_SOURCES})

target_sources(BlankMono
    PRIVATE
        ${URK_PROJECT_SOURCES}
        ${URK_USER_MODULE_FILES}
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES
    ${URK_PROJECT_SOURCES}
    ${URK_USER_MODULE_FILES}
)

target_compile_definitions(BlankMono PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(BlankMono PRIVATE imgui d3d11 d3d12 dxgi opengl32)
if(MSVC)
    target_compile_options(BlankMono PRIVATE /W4 /permissive- /EHsc /utf-8 /bigobj)
    target_link_options(BlankMono PRIVATE
        "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>"
        "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
        "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
    )
endif()
urk_disable_vs_vcpkg_integration(BlankMono)

set(URK_DEPLOY_DIR "" CACHE PATH "Directory where the built mod DLL will be copied")
if(URK_DEPLOY_DIR)
    add_custom_command(TARGET BlankMono POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${URK_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:BlankMono> "${URK_DEPLOY_DIR}"
    )
endif()
