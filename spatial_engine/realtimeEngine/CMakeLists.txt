cmake_minimum_required(VERSION 3.12)

# Workaround for allolib's old CMake version requirement
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(spatialroot_realtime_engine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── AlloLib configuration ─────────────────────────────────────────────────
# Disable AlloLib features we don't need for the realtime engine
set(ALLOLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build AlloLib examples")
set(ALLOLIB_BUILD_TESTS    OFF CACHE BOOL "Don't build AlloLib tests")
set(AL_VERBOSE_OUTPUT      OFF CACHE BOOL "Disable verbose output")

# AlloLib lives at <project_root>/thirdparty/allolib
# From realtimeEngine/, that is ../../thirdparty/allolib
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib
    ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
)

# ── Executable ────────────────────────────────────────────────────────────
# Phase 1: minimal main + shared utilities (JSONLoader, LayoutLoader, WavUtils)
# These are the same files the offline renderer uses, shared from spatial_engine/src/
add_executable(spatialroot_realtime
    src/main.cpp
    src/EngineSession.cpp
    ../src/JSONLoader.cpp
    ../src/LayoutLoader.cpp
    ../src/WavUtils.cpp
)

# ── Include paths ─────────────────────────────────────────────────────────
target_include_directories(spatialroot_realtime PRIVATE
    # AlloLib headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib/include
    # Shared spatial_engine/src headers (JSONLoader.hpp, LayoutLoader.hpp, WavUtils.hpp)
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    # Offline renderer headers (SpatialRenderer.hpp) — referenced in comments only
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/renderer
    # Realtime engine headers (RealtimeEngine.hpp, RealtimeTypes.hpp, etc.)
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# ── Link libraries ────────────────────────────────────────────────────────
# 'al' is the AlloLib target; 'Gamma' is the audio DSP library bundled with it
target_link_libraries(spatialroot_realtime
    al
    Gamma
)
