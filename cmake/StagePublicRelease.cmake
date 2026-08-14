if (NOT DEFINED OUTPUT_DIR OR NOT IS_ABSOLUTE "${OUTPUT_DIR}")
    message(FATAL_ERROR "OUTPUT_DIR must be an absolute path.")
endif()

foreach(required_var SDK_TOOL UPDATER VERSION_LOADER WINHTTP_LOADER WINMM_LOADER INJECTED_LOADER DEV_MCP DEV_BRIDGE SOURCE_DIR)
    if (NOT DEFINED ${required_var} OR "${${required_var}}" STREQUAL "")
        message(FATAL_ERROR "${required_var} is required.")
    endif()
endforeach()

foreach(binary_var SDK_TOOL UPDATER VERSION_LOADER WINHTTP_LOADER WINMM_LOADER INJECTED_LOADER DEV_MCP DEV_BRIDGE)
    if (NOT EXISTS "${${binary_var}}")
        message(FATAL_ERROR "${binary_var} does not exist: ${${binary_var}}")
    endif()
endforeach()

set(release_files
    urk-sdk.exe
    urk-updater.exe
    version.dll
    winhttp.dll
    winmm.dll
    URKitInjector.dll
    urk-dev-mcp.exe
    URKitDevBridge.dll
    LICENSE
    README.md
    THIRD_PARTY_NOTICES.md
)

if (EXISTS "${SOURCE_DIR}/docs/SDK_HANDBOOK.md" OR EXISTS "${SOURCE_DIR}/docs/DEV_MCP.md")
    list(APPEND release_files docs)
endif()

file(MAKE_DIRECTORY "${OUTPUT_DIR}")
file(GLOB existing_entries RELATIVE "${OUTPUT_DIR}" "${OUTPUT_DIR}/*")
foreach(existing_entry IN LISTS existing_entries)
    list(FIND release_files "${existing_entry}" allowed_index)
    if (allowed_index EQUAL -1)
        message(FATAL_ERROR
            "Refusing to stage into a directory containing an unknown file: ${OUTPUT_DIR}/${existing_entry}. "
            "Use an empty directory or remove the unexpected file first.")
    endif()
endforeach()

if (EXISTS "${OUTPUT_DIR}/docs")
    file(GLOB docs_entries RELATIVE "${OUTPUT_DIR}/docs" "${OUTPUT_DIR}/docs/*")
    foreach(docs_entry IN LISTS docs_entries)
        if (NOT docs_entry STREQUAL "SDK_HANDBOOK.md" AND NOT docs_entry STREQUAL "DEV_MCP.md")
            message(FATAL_ERROR
                "Refusing to stage into a docs directory containing an unknown file: "
                "${OUTPUT_DIR}/docs/${docs_entry}")
        endif()
    endforeach()
endif()

function(copy_release_file source destination_name)
    if (NOT EXISTS "${source}")
        message(FATAL_ERROR "Required release file is missing: ${source}")
    endif()
    file(COPY_FILE "${source}" "${OUTPUT_DIR}/${destination_name}" ONLY_IF_DIFFERENT)
endfunction()

copy_release_file("${SDK_TOOL}" urk-sdk.exe)
copy_release_file("${UPDATER}" urk-updater.exe)
copy_release_file("${VERSION_LOADER}" version.dll)
copy_release_file("${WINHTTP_LOADER}" winhttp.dll)
copy_release_file("${WINMM_LOADER}" winmm.dll)
copy_release_file("${INJECTED_LOADER}" URKitInjector.dll)
copy_release_file("${DEV_MCP}" urk-dev-mcp.exe)
copy_release_file("${DEV_BRIDGE}" URKitDevBridge.dll)
copy_release_file("${SOURCE_DIR}/LICENSE" LICENSE)
copy_release_file("${SOURCE_DIR}/README.md" README.md)
copy_release_file("${SOURCE_DIR}/THIRD_PARTY_NOTICES.md" THIRD_PARTY_NOTICES.md)
if (EXISTS "${SOURCE_DIR}/docs/SDK_HANDBOOK.md")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}/docs")
    copy_release_file("${SOURCE_DIR}/docs/SDK_HANDBOOK.md" "docs/SDK_HANDBOOK.md")
endif()
if (EXISTS "${SOURCE_DIR}/docs/DEV_MCP.md")
    file(MAKE_DIRECTORY "${OUTPUT_DIR}/docs")
    copy_release_file("${SOURCE_DIR}/docs/DEV_MCP.md" "docs/DEV_MCP.md")
endif()

message(STATUS "Staged public release in ${OUTPUT_DIR}")
