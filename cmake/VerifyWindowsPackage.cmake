cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED PACKAGE_ROOT)
    message(FATAL_ERROR "PACKAGE_ROOT is required")
endif()

file(TO_CMAKE_PATH "${PACKAGE_ROOT}" PACKAGE_ROOT)

set(_executables
    "${PACKAGE_ROOT}/SpatialRoot.exe"
    "${PACKAGE_ROOT}/cult-transcoder.exe"
    "${PACKAGE_ROOT}/spatialroot_spatial_render.exe"
)

foreach(_exe IN LISTS _executables)
    if(NOT EXISTS "${_exe}")
        message(FATAL_ERROR "Missing executable: ${_exe}")
    endif()
endforeach()

set(_runtime_dlls
    "${PACKAGE_ROOT}/msvcp140.dll"
    "${PACKAGE_ROOT}/vcruntime140.dll"
    "${PACKAGE_ROOT}/vcruntime140_1.dll"
)

foreach(_dll IN LISTS _runtime_dlls)
    if(NOT EXISTS "${_dll}")
        message(FATAL_ERROR "Missing app-local VC runtime DLL: ${_dll}")
    endif()
endforeach()

file(GET_RUNTIME_DEPENDENCIES
    EXECUTABLES
        ${_executables}
    DIRECTORIES
        "${PACKAGE_ROOT}"
    RESOLVED_DEPENDENCIES_VAR _resolved
    UNRESOLVED_DEPENDENCIES_VAR _unresolved
    PRE_EXCLUDE_REGEXES
        "api-ms-win-.*"
        "ext-ms-.*"
        "HvsiFileTrust\\.dll"
        "wpaxholder\\.dll"
    POST_EXCLUDE_REGEXES
        ".*/Windows/System32/.*"
        ".*/Windows/SystemApps/.*"
        ".*/Windows/WinSxS/.*"
)

if(_unresolved)
    list(JOIN _unresolved "\n  " _unresolved_text)
    message(FATAL_ERROR "Unresolved runtime dependencies:\n  ${_unresolved_text}")
endif()

message(STATUS "Resolved non-system runtime dependencies:")
foreach(_dep IN LISTS _resolved)
    message(STATUS "  ${_dep}")
endforeach()

set(_required_stage_runtime_hits 0)
foreach(_dep IN LISTS _resolved)
    if(_dep MATCHES ".*/(msvcp140|vcruntime140|vcruntime140_1)\\.dll$")
        math(EXPR _required_stage_runtime_hits "${_required_stage_runtime_hits} + 1")
    endif()
endforeach()

if(_required_stage_runtime_hits LESS 3)
    message(FATAL_ERROR
        "Runtime dependency scan did not resolve all required VC runtime DLLs from the staged package root.")
endif()
