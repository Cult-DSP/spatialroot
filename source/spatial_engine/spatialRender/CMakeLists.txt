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

# cult-allolib is in internal at project root.
# From source/spatial_engine/spatialRender/, that is ../../../internal/cult-allolib.
# Guard: when built via the root CMakeLists.txt, cult-allolib is already added there.
if(NOT TARGET al)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../internal/cult-allolib
        ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
    )
endif()

add_executable(spatialroot_spatial_render
    main.cpp
    OfflineOutputRouteMap.cpp
    SpatialRenderer.cpp
    ../src/JSONLoader.cpp
    ../src/LayoutLoader.cpp
    ../src/WavUtils.cpp
)

target_include_directories(spatialroot_spatial_render PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../internal/cult-allolib/include
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(spatialroot_spatial_render
    al
    Gamma
    SndFile::sndfile
)
