cmake_minimum_required(VERSION 3.12)

# Workaround for allolib's old CMake version requirement
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(spatialroot_spatial_render)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable unnecessary AlloLib features
set(ALLOLIB_BUILD_EXAMPLES OFF CACHE BOOL "Don't build AlloLib examples")
set(ALLOLIB_BUILD_TESTS OFF CACHE BOOL "Don't build AlloLib tests")
set(AL_VERBOSE_OUTPUT OFF CACHE BOOL "Disable verbose output")

# AlloLib is in thirdparty at project root (two levels up from spatial_engine/spatialRender)
# Guard: when built via the root CMakeLists.txt, allolib is already added there.
if(NOT TARGET al)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib
        ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
    )
endif()

# Source files are in spatial_engine/src (one level up from spatialRender)
# SpatialRenderer is in renderer subdirectory
add_executable(spatialroot_spatial_render
    ../src/main.cpp
    ../src/renderer/SpatialRenderer.cpp
    ../src/JSONLoader.cpp
    ../src/LayoutLoader.cpp
    ../src/WavUtils.cpp
)

# Include AlloLib headers and renderer for SpatialRenderer.hpp
target_include_directories(spatialroot_spatial_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/renderer
)

target_link_libraries(spatialroot_spatial_render
    al
    Gamma
    SndFile::sndfile
)
