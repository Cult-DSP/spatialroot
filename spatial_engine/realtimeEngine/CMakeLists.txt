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
# Guard: when built via the root CMakeLists.txt, allolib is already added there.
if(NOT TARGET al)
    add_subdirectory(
        ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib
        ${CMAKE_CURRENT_BINARY_DIR}/allolib-build
    )
endif()

# ── Core engine library ───────────────────────────────────────────────────
add_library(EngineSessionCore
    src/EngineSession.cpp
    ../src/JSONLoader.cpp
    ../src/LayoutLoader.cpp
    ../src/WavUtils.cpp
)

target_include_directories(EngineSessionCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/renderer
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(EngineSessionCore
    al
    Gamma
)

add_executable(spatialroot_realtime
    src/main.cpp
)

target_link_libraries(spatialroot_realtime
    EngineSessionCore
)

add_executable(internal_validation_runner
    src/internal_validation_runner.cpp
)

target_link_libraries(internal_validation_runner
    EngineSessionCore
)

# ── Stage 2 embedding test ────────────────────────────────────────────────
# Verifies EngineSessionCore links and all V1.1 API methods are callable
# without OSC (oscPort = 0). Does not require audio hardware or test data.
add_executable(embedding_test
    src/embedding_test.cpp
)

target_link_libraries(embedding_test
    EngineSessionCore
)

# ── Install targets for EngineSessionCore (Task 1.3) ─────────────────────
# Allows an embedding host to locate EngineSessionCore via cmake --install.
# AlloLib headers are already transitively exposed via the PUBLIC
# target_include_directories above — a host that links EngineSessionCore
# automatically inherits those include paths.
include(GNUInstallDirs)

install(TARGETS EngineSessionCore
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    src/EngineSession.hpp
    src/RealtimeTypes.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spatialroot
)
# Note: exported CMake target (for find_package support with AlloLib include
# path propagation) is Stage 2 task 2.2. Requires switching PUBLIC
# target_include_directories to BUILD_INTERFACE/INSTALL_INTERFACE generator
# expressions, and resolving the al target export dependency.
