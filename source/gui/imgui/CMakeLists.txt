# source/gui/imgui/CMakeLists.txt
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
    # Initial Linux packaging target is X11/XWayland. Keep Wayland off until we
    # intentionally add the scanner/tooling and validate a Wayland release path.
    if(UNIX AND NOT APPLE)
        set(GLFW_BUILD_X11     ON  CACHE BOOL "" FORCE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
    add_subdirectory(${GLFW_DIR} ${CMAKE_CURRENT_BINARY_DIR}/glfw-build)
endif()

# ── OpenGL (cross-platform via CMake's built-in finder) ───────────────────────
# OpenGL::GL → -framework OpenGL (macOS), opengl32 (Windows), -lGL (Linux)
find_package(OpenGL REQUIRED)

# ── Embed miniLogo.png as a C byte array (path-independent) ──────────────────
# Generated via a portable CMake script so Windows does not depend on `xxd`.
# Used by App.cpp (GL texture via stbi_load_from_memory) and
# FileDialog_macOS.mm (Dock icon via NSData). Avoids any runtime path lookup.
set(MINILOGO_DATA_H "${CMAKE_CURRENT_BINARY_DIR}/miniLogo_data.h")
add_custom_command(
    OUTPUT  "${MINILOGO_DATA_H}"
    COMMAND "${CMAKE_COMMAND}"
            -DINPUT="${CMAKE_CURRENT_SOURCE_DIR}/src/miniLogo.png"
            -DOUTPUT="${MINILOGO_DATA_H}"
            -DVAR_NAME=miniLogo_png
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedBinaryAsHeader.cmake"
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
    src/DefaultLayoutManager.cpp
    src/SpatialRootPaths.cpp
    src/StartupLogger.cpp
    src/SubprocessRunner.cpp
    src/FileDialog.cpp
    src/stb_image_impl.cpp
)

add_executable(spatialroot_gui
    ${APP_SOURCES}
    ${IMGUI_SOURCES}
)

if(APPLE)
    set(SPATIALROOT_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/SpatialRootApp.icns")
    set_source_files_properties(${SPATIALROOT_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(spatialroot_gui PRIVATE ${SPATIALROOT_ICON})
endif()

if(WIN32)
    # Avoid a space in the exe name on Windows; "Spatial Root.exe" causes quoting
    # pain in shell scripts and some file dialogs.
    set_target_properties(spatialroot_gui PROPERTIES OUTPUT_NAME "SpatialRoot")
else()
    set_target_properties(spatialroot_gui PROPERTIES OUTPUT_NAME "Spatial Root")
endif()

if(APPLE)
    set_target_properties(spatialroot_gui PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "Spatial Root"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "SpatialRootApp.icns"
        MACOSX_BUNDLE_INFO_PLIST
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
    )
endif()

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
    ${CMAKE_SOURCE_DIR}/internal/cult-allolib/external/stb/stb  # stb_image.h for logo texture
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
    target_link_libraries(spatialroot_gui PRIVATE Comdlg32 Shell32)
endif()

if(APPLE)
    install(TARGETS spatialroot_gui
        BUNDLE DESTINATION .
        COMPONENT SpatialRootRuntime
    )
    install(FILES "${SPATIALROOT_ICON}"
        DESTINATION "${SPATIALROOT_MACOS_APP_RESOURCES_DESTINATION}"
        COMPONENT SpatialRootRuntime
    )
elseif(WIN32)
    # Flat Windows portable package: exe at package root, resources under resources/.
    install(TARGETS spatialroot_gui
        RUNTIME DESTINATION .
        COMPONENT SpatialRootRuntime
    )
else()
    install(TARGETS spatialroot_gui
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT SpatialRootRuntime
    )
endif()

if(APPLE)
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/source/speaker_layouts"
        DESTINATION "${SPATIALROOT_MACOS_APP_RESOURCES_DESTINATION}"
        COMPONENT SpatialRootRuntime
    )
elseif(WIN32)
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/source/speaker_layouts"
        DESTINATION "resources"
        COMPONENT SpatialRootRuntime
    )
    install(FILES
        "${CMAKE_SOURCE_DIR}/README.md"
        "${CMAKE_SOURCE_DIR}/LICENSE"
        DESTINATION .
        COMPONENT SpatialRootRuntime
    )
else()
    install(
        DIRECTORY "${CMAKE_SOURCE_DIR}/source/speaker_layouts"
        DESTINATION "${SPATIALROOT_INSTALL_RESOURCE_ROOT}"
        COMPONENT SpatialRootRuntime
    )
endif()
