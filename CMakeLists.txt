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

# ── Build options ─────────────────────────────────────────────────────────────
option(SPATIALROOT_BUILD_ENGINE   "Build spatialroot_realtime engine"    ON)
option(SPATIALROOT_BUILD_OFFLINE  "Build spatialroot_spatial_render"     ON)
option(SPATIALROOT_BUILD_CULT     "Build cult-transcoder CLI"            ON)
option(SPATIALROOT_BUILD_GUI      "Build Qt desktop GUI"                 OFF)
# SPATIALROOT_BUILD_GUI is OFF by default until Qt is confirmed installed.
# Enable only after running init.sh which fetches Qt (see Discovery Task A).

# ── AlloLib — shared dependency for ENGINE and OFFLINE ───────────────────────
# Added once at the root level so that both add_subdirectory(realtimeEngine)
# and add_subdirectory(spatialRender) can use the same `al` CMake target
# without a duplicate-target error. Each component CMakeLists guards its own
# add_subdirectory(allolib) with if(NOT TARGET al).
if(SPATIALROOT_BUILD_ENGINE OR SPATIALROOT_BUILD_OFFLINE)
    set(ALLOLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build AlloLib examples")
    set(ALLOLIB_BUILD_TESTS    OFF CACHE BOOL "Don't build AlloLib tests")
    set(AL_VERBOSE_OUTPUT      OFF CACHE BOOL "Disable verbose output")
    # Workaround for AlloLib's old internal cmake_minimum_required usage
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/allolib
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
    # gui/qt/ does not yet exist — created in Stage 3.
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/gui/qt/CMakeLists.txt)
        message(FATAL_ERROR
            "SPATIALROOT_BUILD_GUI=ON but gui/qt/CMakeLists.txt does not exist.\n"
            "The Qt GUI is implemented in Stage 3 of the refactor.\n"
            "Set SPATIALROOT_BUILD_GUI=OFF or wait until Stage 3 is complete."
        )
    endif()
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
    add_subdirectory(gui/qt)
endif()

# ── Summary ───────────────────────────────────────────────────────────────────
message(STATUS "")
message(STATUS "=== spatialroot build configuration ===")
message(STATUS "  ENGINE   (spatialroot_realtime)       : ${SPATIALROOT_BUILD_ENGINE}")
message(STATUS "  OFFLINE  (spatialroot_spatial_render) : ${SPATIALROOT_BUILD_OFFLINE}")
message(STATUS "  CULT     (cult-transcoder)            : ${SPATIALROOT_BUILD_CULT}")
message(STATUS "  GUI      (Qt desktop app)             : ${SPATIALROOT_BUILD_GUI}")
message(STATUS "=======================================")
message(STATUS "")
