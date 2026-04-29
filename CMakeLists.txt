cmake_minimum_required(VERSION 3.20)
# 3.20 minimum: required by cult_transcoder (its CMakeLists.txt declares 3.20).
# Qt6 requires 3.16+; 3.20 satisfies both.

project(spatialroot
    VERSION 0.1.0
    DESCRIPTION "Spatial Root — spatial audio engine, transcoder, and GUI"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
# SPATIALROOT_BUILD_GUI is OFF by default until gui/imgui/ is created in Stage 3.

# ── libsndfile — vendored, built before AlloLib so Gamma's find module picks it up ──
# AlloLib's bundled Gamma uses find_package(LibSndFile QUIET) with old-style
# find variables (SNDFILE_INCLUDE_DIR, SNDFILE_LIBRARY). Pre-setting these as
# cache variables before add_subdirectory(allolib) causes Gamma's find_library
# call to short-circuit and use the vendored build instead of a system install.
set(BUILD_PROGRAMS       OFF CACHE BOOL "Don't build libsndfile programs")
set(BUILD_EXAMPLES       OFF CACHE BOOL "Don't build libsndfile examples")
set(BUILD_TESTING        OFF CACHE BOOL "Don't build libsndfile tests")
set(ENABLE_EXTERNAL_LIBS OFF CACHE BOOL "No FLAC/Ogg/Vorbis/Opus in libsndfile")
set(ENABLE_MPEG          OFF CACHE BOOL "No MPEG in libsndfile")
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/libsndfile
    ${CMAKE_CURRENT_BINARY_DIR}/libsndfile-build
)
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
    set(ALLOLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build AlloLib examples")
    set(ALLOLIB_BUILD_TESTS    OFF CACHE BOOL "Don't build AlloLib tests")
    set(AL_VERBOSE_OUTPUT      OFF CACHE BOOL "Disable verbose output")
    # Workaround for AlloLib's old internal cmake_minimum_required usage
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/internal/cult-allolib
        ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
    )
endif()

# ── Components ────────────────────────────────────────────────────────────────
if(SPATIALROOT_BUILD_CULT)
    add_subdirectory(cult_transcoder)
endif()

if(SPATIALROOT_BUILD_ENGINE)
    add_subdirectory(spatial_engine/realtimeEngine)
endif()

if(SPATIALROOT_BUILD_OFFLINE)
    add_subdirectory(spatial_engine/spatialRender)
endif()

if(SPATIALROOT_BUILD_GUI)
    # gui/imgui/ does not yet exist — created in Stage 3.
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gui/imgui/CMakeLists.txt)
        message(FATAL_ERROR
            "SPATIALROOT_BUILD_GUI=ON but gui/imgui/CMakeLists.txt does not exist.\n"
            "The ImGui + GLFW GUI is implemented in Stage 3 of the refactor.\n"
            "Set SPATIALROOT_BUILD_GUI=OFF or wait until Stage 3 is complete."
        )
    endif()
    add_subdirectory(gui/imgui)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== spatialroot build configuration ===")
message(STATUS "  ENGINE   (spatialroot_realtime)       : ${SPATIALROOT_BUILD_ENGINE}")
message(STATUS "  OFFLINE  (spatialroot_spatial_render) : ${SPATIALROOT_BUILD_OFFLINE}")
message(STATUS "  CULT     (cult-transcoder)            : ${SPATIALROOT_BUILD_CULT}")
message(STATUS "  GUI      (ImGui + GLFW desktop app)   : ${SPATIALROOT_BUILD_GUI}")
message(STATUS "=======================================")
message(STATUS "")
