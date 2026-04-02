# gui/imgui/CMakeLists.txt
# Dear ImGui + GLFW OpenGL3 desktop GUI for Spatial Root (Stage 3).
#
# Submodule prerequisites — add once, then init.sh handles initialization:
#   git submodule add https://github.com/ocornut/imgui.git thirdparty/imgui
#   git submodule add https://github.com/glfw/glfw.git    thirdparty/glfw
#
# Build: ./build.sh --gui  (or -DSPATIALROOT_BUILD_GUI=ON with cmake)

set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/thirdparty/imgui")
set(GLFW_DIR  "${CMAKE_SOURCE_DIR}/thirdparty/glfw")

# ── GLFW ──────────────────────────────────────────────────────────────────────
if(NOT TARGET glfw)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GLFW_DIR} ${CMAKE_CURRENT_BINARY_DIR}/glfw-build)
endif()

# ── OpenGL (cross-platform via CMake's built-in finder) ───────────────────────
# OpenGL::GL → -framework OpenGL (macOS), opengl32 (Windows), -lGL (Linux)
find_package(OpenGL REQUIRED)

# ── Embed miniLogo.png as a C byte array (path-independent) ──────────────────
# xxd -i generates miniLogo_png[] / miniLogo_png_len in miniLogo_data.h.
# Used by App.cpp (GL texture via stbi_load_from_memory) and
# FileDialog_macOS.mm (Dock icon via NSData). Avoids any runtime path lookup.
set(MINILOGO_DATA_H "${CMAKE_CURRENT_BINARY_DIR}/miniLogo_data.h")
add_custom_command(
    OUTPUT  "${MINILOGO_DATA_H}"
    COMMAND xxd -i miniLogo.png "${MINILOGO_DATA_H}"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/miniLogo.png"
    COMMENT "Embedding miniLogo.png as C byte array"
)
add_custom_target(miniLogo_embed DEPENDS "${MINILOGO_DATA_H}")

# ── Dear ImGui sources ────────────────────────────────────────────────────────
# Core ImGui + GLFW/OpenGL3 backends + imgui_stdlib (std::string InputText)
set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
)

# ── Application sources ───────────────────────────────────────────────────────
set(APP_SOURCES
    src/main.cpp
    src/App.cpp
    src/SubprocessRunner.cpp
    src/FileDialog.cpp
)

add_executable(spatialroot_gui
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
)

set_target_properties(spatialroot_gui PROPERTIES OUTPUT_NAME "Spatial Root")

add_dependencies(spatialroot_gui miniLogo_embed)

# macOS: OpenGL is deprecated (but fully functional on all current hardware).
# GL_SILENCE_DEPRECATION suppresses the Xcode/compiler deprecation warnings.
# AppKit is required for NSOpenPanel (FileDialog_macOS.mm).
if(APPLE)
    target_compile_definitions(spatialroot_gui PRIVATE GL_SILENCE_DEPRECATION)
    target_sources(spatialroot_gui PRIVATE src/FileDialog_macOS.mm)
    target_link_libraries(spatialroot_gui PRIVATE "-framework AppKit")
endif()

target_include_directories(spatialroot_gui PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${IMGUI_DIR}/misc/cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/thirdparty/allolib/external/stb/stb  # stb_image.h for logo texture
    ${CMAKE_CURRENT_BINARY_DIR}                               # miniLogo_data.h (generated)
)

# Threads: required by SubprocessRunner (std::thread background subprocess)
find_package(Threads REQUIRED)

target_link_libraries(spatialroot_gui PRIVATE
    EngineSessionCore       # spatial audio engine (static lib + AlloLib headers)
    glfw
    OpenGL::GL
    Threads::Threads
)

# Windows: GetOpenFileName lives in Comdlg32 (FileDialog.cpp)
if(WIN32)
    target_link_libraries(spatialroot_gui PRIVATE Comdlg32)
endif()
