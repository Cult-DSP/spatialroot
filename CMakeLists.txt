cmake_minimum_required(VERSION 3.20)
# 3.20 minimum: required by cult_transcoder (its CMakeLists.txt declares 3.20).
# Qt6 requires 3.16+; 3.20 satisfies both.

project(spatialroot
    VERSION 0.1.0
    DESCRIPTION "Spatial Root — spatial audio engine, transcoder, and GUI"
    LANGUAGES CXX
)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(SPATIALROOT_INSTALL_RESOURCE_ROOT "${CMAKE_INSTALL_DATADIR}/spatialroot")
if(APPLE)
    set(SPATIALROOT_MACOS_APP_BUNDLE_NAME "Spatial Root.app")
    set(SPATIALROOT_MACOS_APP_RESOURCES_DESTINATION
        "${SPATIALROOT_MACOS_APP_BUNDLE_NAME}/Contents/Resources")
endif()

find_program(GIT_EXECUTABLE git)

function(spatialroot_ensure_submodule_ready path sentinel)
    set(options RECURSIVE)
    cmake_parse_arguments(SR_SUBMOD "${options}" "" "" ${ARGN})

    set(_needs_update OFF)
    if(NOT EXISTS "${sentinel}")
        set(_needs_update ON)
    elseif(GIT_EXECUTABLE)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" submodule status --recursive "${path}"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            RESULT_VARIABLE _status_result
            OUTPUT_VARIABLE _status_output
            ERROR_QUIET
            OUTPUT_STRIP_TRAILING_WHITESPACE
        )
        if(_status_result EQUAL 0 AND _status_output MATCHES "(^|\n)-")
            set(_needs_update ON)
        endif()
    endif()

    if(NOT _needs_update)
        return()
    endif()

    if(NOT GIT_EXECUTABLE)
        message(FATAL_ERROR
            "Required submodule '${path}' is missing or incomplete, and git is not available to repair it automatically.\n"
            "Install git, then re-run CMake or use ./init.sh / ./build.sh."
        )
    endif()

    message(STATUS "Initializing required submodule: ${path}")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" submodule sync --recursive "${path}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _sync_result
    )
    if(NOT _sync_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to sync submodule '${path}'.\n"
            "Try running: git submodule sync --recursive && git submodule update --init --recursive --checkout"
        )
    endif()

    set(_update_cmd "${GIT_EXECUTABLE}" submodule update --init --checkout)
    if(SR_SUBMOD_RECURSIVE)
        list(APPEND _update_cmd --recursive)
    endif()
    list(APPEND _update_cmd "${path}")
    execute_process(
        COMMAND ${_update_cmd}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        RESULT_VARIABLE _update_result
    )
    if(NOT _update_result EQUAL 0 OR NOT EXISTS "${sentinel}")
        message(FATAL_ERROR
            "Failed to initialize required submodule '${path}'.\n"
            "Try running: git submodule update --init --recursive --checkout ${path}"
        )
    endif()
endfunction()

# Default single-config generators to a production-oriented build unless the
# caller explicitly chooses another configuration.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "No build type set — defaulting to Release")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── Build options ─────────────────────────────────────────────────────────────
option(SPATIALROOT_BUILD_ENGINE   "Build spatialroot_realtime engine"    ON)
option(SPATIALROOT_BUILD_OFFLINE  "Build spatialroot_spatial_render"     ON)
option(SPATIALROOT_BUILD_CULT     "Build cult-transcoder CLI"            ON)
option(SPATIALROOT_BUILD_GUI      "Build ImGui + GLFW desktop GUI"       OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    option(SPATIALROOT_BUILD_DEVTOOLS "Build non-shipping developer tools and smoke-test executables" ON)
else()
    option(SPATIALROOT_BUILD_DEVTOOLS "Build non-shipping developer tools and smoke-test executables" OFF)
endif()

# ── libsndfile — vendored, built before AlloLib so Gamma's find module picks it up ──
# AlloLib's bundled Gamma uses find_package(LibSndFile QUIET) with old-style
# find variables (SNDFILE_INCLUDE_DIR, SNDFILE_LIBRARY). Pre-setting these as
# cache variables before add_subdirectory(allolib) causes Gamma's find_library
# call to short-circuit and use the vendored build instead of a system install.
spatialroot_ensure_submodule_ready(
    "thirdparty/libsndfile"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile/CMakeLists.txt"
)
set(BUILD_PROGRAMS       OFF CACHE BOOL "Don't build libsndfile programs")
set(BUILD_EXAMPLES       OFF CACHE BOOL "Don't build libsndfile examples")
set(BUILD_TESTING        OFF CACHE BOOL "Don't build libsndfile tests")
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "No FLAC/Ogg/Vorbis/Opus in libsndfile")
set(ENABLE_MPEG          OFF CACHE BOOL "No MPEG in libsndfile")
set(_spatialroot_skip_install_rules_saved "${CMAKE_SKIP_INSTALL_RULES}")
set(CMAKE_SKIP_INSTALL_RULES ON)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile
    ${CMAKE_CURRENT_BINARY_DIR}/libsndfile-build
    EXCLUDE_FROM_ALL  # prevents cmake_install.cmake from including this subdirectory
)
set(CMAKE_SKIP_INSTALL_RULES "${_spatialroot_skip_install_rules_saved}")
# Pre-populate the find variables so Gamma resolves to our vendored target.
set(SNDFILE_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile/include"
    CACHE PATH "libsndfile include dir (vendored)" FORCE)
set(SNDFILE_LIBRARY   SndFile::sndfile CACHE STRING "libsndfile library (vendored)" FORCE)
set(SNDFILE_LIBRARIES SndFile::sndfile CACHE STRING "libsndfile libraries (vendored)" FORCE)
set(SNDFILE_FOUND     TRUE CACHE BOOL "libsndfile found (vendored)" FORCE)

# ── cult-allolib — shared dependency for ENGINE and OFFLINE ──────────────────
# Added once at the root level so that both add_subdirectory(realtimeEngine)
# and add_subdirectory(spatialRender) can use the same `al` CMake target
# without a duplicate-target error. Each component CMakeLists guards its own
# add_subdirectory(cult-allolib) with if(NOT TARGET al).
if(SPATIALROOT_BUILD_ENGINE OR SPATIALROOT_BUILD_OFFLINE)
    spatialroot_ensure_submodule_ready(
        "internal/cult-allolib"
        "${CMAKE_CURRENT_SOURCE_DIR}/internal/cult-allolib/include"
        RECURSIVE
    )
    set(ALLOLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build AlloLib examples")
    set(ALLOLIB_BUILD_TESTS    OFF CACHE BOOL "Don't build AlloLib tests")
    set(AL_VERBOSE_OUTPUT      OFF CACHE BOOL "Disable verbose output")
    set(CULT_ALLOLIB_ENABLE_AUDIO      ON  CACHE BOOL "Build cult-allolib audio support")
    set(CULT_ALLOLIB_ENABLE_OSC        ON  CACHE BOOL "Build cult-allolib OSC support")
    set(CULT_ALLOLIB_ENABLE_PARAMETERS ON  CACHE BOOL "Build cult-allolib parameter support")
    set(CULT_ALLOLIB_ENABLE_SPATIAL    ON  CACHE BOOL "Build cult-allolib spatial support")
    set(CULT_ALLOLIB_ENABLE_GRAPHICS   OFF CACHE BOOL "Disable cult-allolib graphics by default")
    set(CULT_ALLOLIB_ENABLE_APP        OFF CACHE BOOL "Disable cult-allolib app domains by default")
    set(CULT_ALLOLIB_ENABLE_VR         OFF CACHE BOOL "Disable cult-allolib VR support by default")
    set(CULT_ALLOLIB_ENABLE_MIDI       OFF CACHE BOOL "Disable cult-allolib MIDI by default")
    set(CULT_ALLOLIB_ENABLE_SERIAL     OFF CACHE BOOL "Disable cult-allolib serial APIs by default")
    set(CULT_ALLOLIB_ENABLE_EXAMPLES   OFF CACHE BOOL "Disable cult-allolib examples by default")
    set(CULT_ALLOLIB_ENABLE_TESTS      OFF CACHE BOOL "Disable cult-allolib tests by default")
    set(CULT_ALLOLIB_ENABLE_DOCS       OFF CACHE BOOL "Disable cult-allolib docs by default")
    # Workaround for AlloLib's old internal cmake_minimum_required usage
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    set(_spatialroot_skip_install_rules_saved "${CMAKE_SKIP_INSTALL_RULES}")
    set(CMAKE_SKIP_INSTALL_RULES ON)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/internal/cult-allolib
        ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
        EXCLUDE_FROM_ALL  # prevents cmake_install.cmake from including this subdirectory
    )
    set(CMAKE_SKIP_INSTALL_RULES "${_spatialroot_skip_install_rules_saved}")
endif()

# ── Components ────────────────────────────────────────────────────────────────
if(SPATIALROOT_BUILD_CULT)
    spatialroot_ensure_submodule_ready(
        "internal/cult_transcoder"
        "${CMAKE_CURRENT_SOURCE_DIR}/internal/cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp"
        RECURSIVE
    )
    add_subdirectory(internal/cult_transcoder)
endif()

if(SPATIALROOT_BUILD_ENGINE)
    add_subdirectory(source/spatial_engine/realtimeEngine)
endif()

if(SPATIALROOT_BUILD_OFFLINE)
    add_subdirectory(source/spatial_engine/spatialRender)
endif()

if(SPATIALROOT_BUILD_GUI)
    spatialroot_ensure_submodule_ready(
        "thirdparty/imgui"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui.h"
    )
    spatialroot_ensure_submodule_ready(
        "thirdparty/glfw"
        "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/glfw/CMakeLists.txt"
    )
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/source/gui/imgui/CMakeLists.txt)
        message(FATAL_ERROR
            "SPATIALROOT_BUILD_GUI=ON but source/gui/imgui/CMakeLists.txt does not exist.\n"
            "The ImGui + GLFW GUI is implemented in Stage 3 of the refactor.\n"
            "Set SPATIALROOT_BUILD_GUI=OFF or wait until Stage 3 is complete."
        )
    endif()
    add_subdirectory(source/gui/imgui)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== spatialroot build configuration ===")
message(STATUS "  ENGINE   (spatialroot_realtime)       : ${SPATIALROOT_BUILD_ENGINE}")
message(STATUS "  OFFLINE  (spatialroot_spatial_render) : ${SPATIALROOT_BUILD_OFFLINE}")
message(STATUS "  CULT     (cult-transcoder)            : ${SPATIALROOT_BUILD_CULT}")
message(STATUS "  GUI      (ImGui + GLFW desktop app)   : ${SPATIALROOT_BUILD_GUI}")
message(STATUS "  DEVTOOLS (validation/smoke helpers)   : ${SPATIALROOT_BUILD_DEVTOOLS}")
message(STATUS "=======================================")
message(STATUS "")

if(WIN32 AND SPATIALROOT_BUILD_GUI)
    # Strategy B: bundle the official Microsoft VC runtime app-locally for the
    # portable ZIP. This keeps the first Windows artifact double-clickable on a
    # clean Windows 10/11 machine without requiring a manual VC++ redist install.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT SpatialRootRuntime)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)

    if(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
        message(STATUS "Windows app-local runtime files:")
        foreach(_runtime_lib IN LISTS CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS)
            message(STATUS "  ${_runtime_lib}")
        endforeach()
    else()
        message(WARNING "InstallRequiredSystemLibraries did not report any Windows runtime files.")
    endif()
endif()

# ── CPack ZIP packaging (Windows x64 portable) ────────────────────────────────
# Only configured on Windows. macOS packaging uses hdiutil (see internalDocs/PACKAGING.md).
# CPack is gated on SPATIALROOT_BUILD_GUI because the app exe is the package root entry point.
# Produces: SpatialRoot-alpha-v1-windows-x64.zip
# Run: cpack --config build-windows/CPackConfig.cmake -G ZIP -C Release
if(WIN32 AND SPATIALROOT_BUILD_GUI)
    set(CPACK_PACKAGE_NAME        "SpatialRoot")
    set(CPACK_PACKAGE_VENDOR      "CultDSP")
    set(CPACK_PACKAGE_VERSION     "alpha-v1")
    set(CPACK_PACKAGE_FILE_NAME   "SpatialRoot-alpha-v1-windows-x64")
    set(CPACK_GENERATOR           "ZIP")
    # No top-level directory inside the ZIP — extraction gives a flat tree the
    # user can place in any directory they choose.
    set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
    set(CPACK_COMPONENTS_ALL      SpatialRootRuntime)
    set(CPACK_COMPONENT_SPATIALROOTRUNTIME_REQUIRED TRUE)
    include(CPack)
endif()
