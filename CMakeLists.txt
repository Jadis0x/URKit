cmake_minimum_required(VERSION 3.28)

if (POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

option(URK_VCPKG_APPLOCAL_DEPS
    "Enable vcpkg app-local dependency copying for generated outputs"
    OFF)

set(VCPKG_APPLOCAL_DEPS ${URK_VCPKG_APPLOCAL_DEPS} CACHE BOOL
    "Automatically copy vcpkg dependencies into target output directories"
    FORCE
)

project(URKit LANGUAGES CXX)

include(CTest)

if (WIN32 AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "URKit supports x64 Windows targets only.")
endif()

# Release binaries use the static Visual C++ runtime.
if (WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING
        "Use the static Microsoft C/C++ runtime" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(FetchContent)

option(URK_BUILD_LOADER "Build the Windows loader DLLs" ${WIN32})
option(URK_BUILD_SDK_TOOL "Build the standalone SDK/project generation tool" ON)
option(URK_WITH_CURL "Enable libcurl-backed HTTPS JSON requests" ON)
option(URK_HARDEN_RELEASE "Enable release hardening for loader DLLs" ON)
option(URK_BUILD_PUBLIC_RELEASE_STAGE "Add the prebuilt release staging target" OFF)
option(URK_BUILD_DEV_MCP "Build the local URKit mod-development MCP server and runtime bridge" ${WIN32})
set(URK_PUBLIC_RELEASE_DIR "${CMAKE_CURRENT_BINARY_DIR}/public-release" CACHE PATH
    "Empty or URKit-managed directory for staged public release files")

if (URK_BUILD_PUBLIC_RELEASE_STAGE AND NOT URK_BUILD_LOADER)
    message(FATAL_ERROR "URK_BUILD_PUBLIC_RELEASE_STAGE requires URK_BUILD_LOADER=ON.")
endif()

if (URK_BUILD_PUBLIC_RELEASE_STAGE AND NOT URK_BUILD_SDK_TOOL)
    message(FATAL_ERROR "URK_BUILD_PUBLIC_RELEASE_STAGE requires URK_BUILD_SDK_TOOL=ON.")
endif()

if (URK_BUILD_PUBLIC_RELEASE_STAGE AND NOT URK_BUILD_DEV_MCP)
    message(FATAL_ERROR "URK_BUILD_PUBLIC_RELEASE_STAGE requires URK_BUILD_DEV_MCP=ON.")
endif()

if (URK_BUILD_DEV_MCP)
    FetchContent_Declare(nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG        v3.12.0
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

function(urk_disable_vs_vcpkg_integration target_name)
    if (MSVC)
        set_target_properties(${target_name} PROPERTIES VS_GLOBAL_VcpkgEnabled "false")
    endif()
endfunction()

include(cmake/URKitSources.cmake)

if (URK_BUILD_DEV_MCP)
    if (NOT WIN32)
        message(FATAL_ERROR "URKit development MCP currently requires Windows.")
    endif()

    add_executable(URKitDevMcp ${URK_DEV_MCP_SERVER_SRC})
    target_compile_features(URKitDevMcp PRIVATE cxx_std_20)
    target_include_directories(URKitDevMcp PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sdk
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk
    )
    target_compile_definitions(URKitDevMcp PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(URKitDevMcp PRIVATE nlohmann_json::nlohmann_json)
    set_target_properties(URKitDevMcp PROPERTIES OUTPUT_NAME "urk-dev-mcp")

    add_library(URKitDevBridge SHARED ${URK_DEV_BRIDGE_SRC})
    target_compile_features(URKitDevBridge PRIVATE cxx_std_20)
    target_include_directories(URKitDevBridge PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/sdk
        ${CMAKE_CURRENT_SOURCE_DIR}/dev/bridge
    )
    target_compile_definitions(URKitDevBridge PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(URKitDevBridge PRIVATE nlohmann_json::nlohmann_json advapi32)
    set_target_properties(URKitDevBridge PROPERTIES PREFIX "" OUTPUT_NAME "URKitDevBridge")

    foreach(target_name IN ITEMS URKitDevMcp URKitDevBridge)
        if (MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc /utf-8)
        endif()
        urk_disable_vs_vcpkg_integration(${target_name})
    endforeach()

    if (BUILD_TESTING)
        add_executable(urk_mcp_protocol_tests
            tests/mcp_protocol_tests.cpp
            ${URK_DEV_MCP_SERVER_LIBRARY_SRC}
        )
        target_include_directories(urk_mcp_protocol_tests PRIVATE . src/sdk sdk)
        target_compile_features(urk_mcp_protocol_tests PRIVATE cxx_std_20)
        target_compile_definitions(urk_mcp_protocol_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(urk_mcp_protocol_tests PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME urkit_mcp_protocol COMMAND urk_mcp_protocol_tests)

        add_executable(urk_mcp_project_service_tests
            tests/mcp_project_service_tests.cpp
            mcp/server/process_runner.cpp
            mcp/server/project_service.cpp
            src/sdk/project_manifest.cpp
        )
        target_include_directories(urk_mcp_project_service_tests PRIVATE . src/sdk sdk)
        target_compile_features(urk_mcp_project_service_tests PRIVATE cxx_std_20)
        target_compile_definitions(urk_mcp_project_service_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_compile_definitions(urk_mcp_project_service_tests PRIVATE
            URK_TEST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
        )
        target_link_libraries(urk_mcp_project_service_tests PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME urkit_mcp_project_service COMMAND urk_mcp_project_service_tests)

        add_executable(urk_runtime_test_discovery_tests
            tests/runtime_test_discovery_tests.cpp
            dev/bridge/runtime_test_discovery.cpp
        )
        target_include_directories(urk_runtime_test_discovery_tests PRIVATE . sdk dev/bridge)
        target_compile_features(urk_runtime_test_discovery_tests PRIVATE cxx_std_20)
        target_compile_definitions(urk_runtime_test_discovery_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        target_link_libraries(urk_runtime_test_discovery_tests PRIVATE nlohmann_json::nlohmann_json)
        add_test(NAME urkit_runtime_test_discovery COMMAND urk_runtime_test_discovery_tests)
    endif()
endif()

if (URK_BUILD_SDK_TOOL)
    set(URK_GENERATED_SDK_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated/sdk)
    set(URK_EMBEDDED_MOD_SDK_HEADER ${URK_GENERATED_SDK_DIR}/embedded_mod_sdk.h)
    set(URK_EMBEDDED_DEV_TEST_HEADER ${URK_GENERATED_SDK_DIR}/embedded_dev_test.h)

    add_custom_command(
        OUTPUT ${URK_EMBEDDED_MOD_SDK_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${URK_SDK_DIR}/mod_sdk.h
            -DOUTPUT=${URK_EMBEDDED_MOD_SDK_HEADER}
            -DSYMBOL=kEmbeddedModSdkHeader
            -DDELIMITER=URKMODSDKH
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedSdkHeader.cmake
        DEPENDS ${URK_SDK_DIR}/mod_sdk.h ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedSdkHeader.cmake
        COMMENT "Embedding canonical sdk/mod_sdk.h for distribution-safe SDK generation"
        VERBATIM
    )

    add_custom_command(
        OUTPUT ${URK_EMBEDDED_DEV_TEST_HEADER}
        COMMAND ${CMAKE_COMMAND}
            -DINPUT=${URK_SDK_DIR}/dev_test.h
            -DOUTPUT=${URK_EMBEDDED_DEV_TEST_HEADER}
            -DSYMBOL=kEmbeddedDevTestHeader
            -DDELIMITER=URKDEVTESTH
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedSdkHeader.cmake
        DEPENDS ${URK_SDK_DIR}/dev_test.h ${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateEmbeddedSdkHeader.cmake
        COMMENT "Embedding canonical sdk/dev_test.h for generated mod runtime tests"
        VERBATIM
    )

    function(urk_add_sdk_executable target_name output_name source_list)
        add_executable(${target_name}
            ${source_list}
            ${URK_COMMON_HEADERS}
            ${URK_EMBEDDED_MOD_SDK_HEADER}
            ${URK_EMBEDDED_DEV_TEST_HEADER}
        )
        target_compile_features(${target_name} PRIVATE cxx_std_20)
        target_include_directories(${target_name} PRIVATE ${URK_INCLUDE_DIRS} ${URK_GENERATED_SDK_DIR})
        target_compile_definitions(${target_name} PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
        set_target_properties(${target_name} PROPERTIES OUTPUT_NAME "${output_name}")
        if (WIN32)
            set_target_properties(${target_name} PROPERTIES WIN32_EXECUTABLE TRUE)
            target_compile_definitions(${target_name} PRIVATE UNICODE _UNICODE)
            target_link_libraries(${target_name} PRIVATE comctl32 comdlg32 dwmapi ole32 shell32 uxtheme)
        endif()
        if (MSVC)
            target_compile_options(${target_name} PRIVATE /W4 /permissive- /EHsc /utf-8)
            target_link_options(${target_name} PRIVATE
                "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>"
                "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
                "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
                "$<$<CONFIG:Release>:/LTCG>"
            )
            set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        endif()
        urk_disable_vs_vcpkg_integration(${target_name})
    endfunction()

    urk_add_sdk_executable(URKitSdkTool "urk-sdk" "${URK_SDK_TOOL_SRC}")
    urk_add_sdk_executable(URKitUpdater "urk-updater" "${URK_UPDATER_SRC}")
    if (WIN32)
        target_link_libraries(URKitUpdater PRIVATE bcrypt winhttp)
    endif()

endif()

if (NOT URK_BUILD_LOADER)
    message(STATUS "Windows proxy loader targets are disabled. Source layout validation completed.")
    return()
endif()

if (NOT WIN32)
    message(FATAL_ERROR "URKit targets Windows. Configure with -DURK_BUILD_LOADER=OFF for SDK tool builds.")
endif()

# Microsoft Detours
FetchContent_Declare(detours
    GIT_REPOSITORY https://github.com/microsoft/Detours.git
    GIT_TAG        v4.0.1
)
FetchContent_MakeAvailable(detours)

if (URK_WITH_CURL)
    set(URK_SAVED_BUILD_TESTING ${BUILD_TESTING})
    set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CURL_STATIC_CRT ON CACHE BOOL "" FORCE)
    set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)
    set(CURL_USE_OPENSSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
    set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
    set(CURL_ZLIB OFF CACHE STRING "" FORCE)
    set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
    set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
    set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
    set(HTTP_ONLY ON CACHE BOOL "" FORCE)
    set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
    set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
    set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(curl
        GIT_REPOSITORY https://github.com/curl/curl.git
        GIT_TAG        curl-8_21_0
    )
    FetchContent_MakeAvailable(curl)
    set(BUILD_TESTING ${URK_SAVED_BUILD_TESTING} CACHE BOOL "" FORCE)
    if (TARGET CURL::libcurl)
        set(URK_CURL_TARGET CURL::libcurl)
    elseif (TARGET libcurl)
        set(URK_CURL_TARGET libcurl)
    elseif (TARGET libcurl_static)
        set(URK_CURL_TARGET libcurl_static)
    else()
        message(FATAL_ERROR "libcurl target was not created by curl FetchContent")
    endif()
endif()

add_library(detours STATIC
    ${detours_SOURCE_DIR}/src/detours.cpp
    ${detours_SOURCE_DIR}/src/modules.cpp
    ${detours_SOURCE_DIR}/src/disasm.cpp
    ${detours_SOURCE_DIR}/src/image.cpp
    ${detours_SOURCE_DIR}/src/creatwth.cpp
)

target_include_directories(detours PUBLIC ${detours_SOURCE_DIR}/src)

if (MSVC)
    target_compile_options(detours PRIVATE /W0)
    if (NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        target_compile_options(detours PRIVATE /Zc:gotoScope-)
    endif()
endif()
urk_disable_vs_vcpkg_integration(detours)

function(urk_configure_runtime_compilation target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_include_directories(${target} PRIVATE ${URK_INCLUDE_DIRS})
    target_compile_definitions(${target} PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
        "$<$<CONFIG:Release>:URK_RELEASE_BUILD=1>"
    )

    if (URK_WITH_CURL)
        target_compile_definitions(${target} PRIVATE URK_WITH_CURL=1 CURL_STATICLIB)
    endif()
    if (MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /EHsc
            /utf-8
        )
        if (URK_HARDEN_RELEASE)
            target_compile_options(${target} PRIVATE
                "$<$<CONFIG:Release>:/guard:cf>"
            )
        endif()
        set_property(TARGET ${target} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    endif()
    urk_disable_vs_vcpkg_integration(${target})
endfunction()

add_library(URKitRuntimeObjects OBJECT
    ${URK_COMMON_SRC}
    ${URK_COMMON_HEADERS}
)
urk_configure_runtime_compilation(URKitRuntimeObjects)
target_link_libraries(URKitRuntimeObjects PRIVATE detours)
if (URK_WITH_CURL)
    target_link_libraries(URKitRuntimeObjects PRIVATE ${URK_CURL_TARGET})
endif()

function(add_urk_proxy target output proxy_source definition_file)
    add_library(${target} SHARED
        ${proxy_source}
        ${definition_file}
        ${URK_LOADER_ENTRY_SRC}
        $<TARGET_OBJECTS:URKitRuntimeObjects>
    )
    urk_configure_runtime_compilation(${target})

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output}"
        ARCHIVE_OUTPUT_NAME "${target}"
        PREFIX ""
    )

    target_link_libraries(${target} PRIVATE detours crypt32 gdiplus ole32 version comdlg32 shell32)
    if (URK_WITH_CURL)
        target_link_libraries(${target} PRIVATE ${URK_CURL_TARGET})
    endif()

    if (MSVC)
        target_link_options(${target} PRIVATE
            "/DEF:${definition_file}"
            "/DEPENDENTLOADFLAG:0x800"
            "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>"
            "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
            "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
            "$<$<CONFIG:Release>:/LTCG>"
        )
        if (URK_HARDEN_RELEASE)
            target_link_options(${target} PRIVATE
                "$<$<CONFIG:Release>:/DEBUG:NONE>"
                "$<$<CONFIG:Release>:/DYNAMICBASE>"
                "$<$<CONFIG:Release>:/NXCOMPAT>"
                "$<$<CONFIG:Release>:/GUARD:CF>"
            )
            if (CMAKE_SIZEOF_VOID_P EQUAL 8)
                target_link_options(${target} PRIVATE
                    "$<$<CONFIG:Release>:/HIGHENTROPYVA>"
                )
            endif()
        endif()
    endif()
endfunction()

function(add_urk_injected_loader target output)
    add_library(${target} SHARED
        ${URK_LOADER_ENTRY_SRC}
        $<TARGET_OBJECTS:URKitRuntimeObjects>
    )
    urk_configure_runtime_compilation(${target})

    set_target_properties(${target} PROPERTIES
        OUTPUT_NAME "${output}"
        PREFIX ""
    )

    target_link_libraries(${target} PRIVATE detours crypt32 gdiplus ole32 version comdlg32 shell32)
    target_compile_definitions(${target} PRIVATE URK_INJECTED_LOADER=1)
    if (URK_WITH_CURL)
        target_link_libraries(${target} PRIVATE ${URK_CURL_TARGET})
    endif()

    if (MSVC)
        target_link_options(${target} PRIVATE
            "$<$<NOT:$<CONFIG:Debug>>:/INCREMENTAL:NO>"
            "$<$<NOT:$<CONFIG:Debug>>:/OPT:REF>"
            "$<$<NOT:$<CONFIG:Debug>>:/OPT:ICF>"
            "$<$<CONFIG:Release>:/LTCG>"
        )
        if (URK_HARDEN_RELEASE)
            target_link_options(${target} PRIVATE
                "$<$<CONFIG:Release>:/DEBUG:NONE>"
                "$<$<CONFIG:Release>:/DYNAMICBASE>"
                "$<$<CONFIG:Release>:/NXCOMPAT>"
                "$<$<CONFIG:Release>:/GUARD:CF>"
            )
            if (CMAKE_SIZEOF_VOID_P EQUAL 8)
                target_link_options(${target} PRIVATE
                    "$<$<CONFIG:Release>:/HIGHENTROPYVA>"
                )
            endif()
        endif()
    endif()
endfunction()

add_urk_proxy(URKit version ${URK_VERSION_PROXY_SRC} ${URK_VERSION_DEF})
add_urk_proxy(URKitWinHttp winhttp ${URK_WINHTTP_PROXY_SRC} ${URK_WINHTTP_DEF})
add_urk_proxy(URKitWinMm winmm ${URK_WINMM_PROXY_SRC} ${URK_WINMM_DEF})
add_urk_injected_loader(URKitInjected URKitInjector)

if (URK_BUILD_PUBLIC_RELEASE_STAGE)
    add_custom_target(URKitPublicRelease
        COMMAND ${CMAKE_COMMAND}
            -DOUTPUT_DIR=${URK_PUBLIC_RELEASE_DIR}
            -DSDK_TOOL=$<TARGET_FILE:URKitSdkTool>
            -DUPDATER=$<TARGET_FILE:URKitUpdater>
            -DVERSION_LOADER=$<TARGET_FILE:URKit>
            -DWINHTTP_LOADER=$<TARGET_FILE:URKitWinHttp>
            -DWINMM_LOADER=$<TARGET_FILE:URKitWinMm>
            -DINJECTED_LOADER=$<TARGET_FILE:URKitInjected>
            -DDEV_MCP=$<TARGET_FILE:URKitDevMcp>
            -DDEV_BRIDGE=$<TARGET_FILE:URKitDevBridge>
            -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/StagePublicRelease.cmake
        DEPENDS URKitSdkTool URKitUpdater URKit URKitWinHttp URKitWinMm URKitInjected URKitDevMcp URKitDevBridge
        COMMENT "Staging prebuilt URKit binaries"
        VERBATIM
    )
endif()

set(URK_DEPLOY_DIR "" CACHE PATH "Target game folder for copying a proxy loader")
set(URK_DEPLOY_PROXY "version" CACHE STRING "Proxy loader copied to URK_DEPLOY_DIR")
set_property(CACHE URK_DEPLOY_PROXY PROPERTY STRINGS version winhttp winmm)

if (URK_DEPLOY_DIR)
    if (URK_DEPLOY_PROXY STREQUAL "version")
        set(URK_DEPLOY_TARGET URKit)
    elseif (URK_DEPLOY_PROXY STREQUAL "winhttp")
        set(URK_DEPLOY_TARGET URKitWinHttp)
    elseif (URK_DEPLOY_PROXY STREQUAL "winmm")
        set(URK_DEPLOY_TARGET URKitWinMm)
    else()
        message(FATAL_ERROR "URK_DEPLOY_PROXY must be version, winhttp, or winmm.")
    endif()

    add_custom_command(TARGET ${URK_DEPLOY_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${URK_DEPLOY_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:${URK_DEPLOY_TARGET}>"
            "${URK_DEPLOY_DIR}/${URK_DEPLOY_PROXY}.dll"
        COMMENT "Copying ${URK_DEPLOY_PROXY}.dll to ${URK_DEPLOY_DIR}"
    )
endif()
