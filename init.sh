#!/bin/bash
# init.sh — One-time dependency setup for spatialroot (macOS / Linux)
#
# Run once after cloning to initialize all git submodules and build all
# C++ components. Subsequent builds can use build.sh directly.
#
# Usage:
#   ./init.sh          # Initialize deps and build everything
#   ./init.sh --help   # Show this message
#
# No Python toolchain required.

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${PROJECT_ROOT}"

submodule_is_registered() {
    local path="$1"
    [ -f "${PROJECT_ROOT}/.gitmodules" ] &&
        git config -f "${PROJECT_ROOT}/.gitmodules" --get-regexp '^submodule\..*\.path$' 2>/dev/null |
        awk '{print $2}' | grep -Fxq "$path"
}

submodule_has_missing_recursive() {
    local path="$1"
    git submodule status --recursive "$path" 2>/dev/null | grep -q '^-'
}

ensure_submodule() {
    local path="$1"
    local sentinel="$2"
    local recursive="${3:-no}"

    if [ -e "$sentinel" ] && ! submodule_has_missing_recursive "$path"; then
        echo "✓ $path already initialized"
        return
    fi

    echo "Fetching $path..."
    git submodule sync --recursive "$path"
    if [ "$recursive" = "yes" ]; then
        git submodule update --init --recursive --depth 1 --checkout "$path"
    else
        git submodule update --init --depth 1 --checkout "$path"
    fi
    echo "✓ $path initialized"
}

if [ "${1}" = "--help" ] || [ "${1}" = "-h" ]; then
    echo "Usage: ./init.sh"
    echo ""
    echo "Initializes git submodules and builds all C++ components."
    echo "Run once after cloning. Subsequent builds: ./build.sh"
    exit 0
fi

echo "============================================================"
echo "spatialroot Initialization (macOS / Linux)"
echo "============================================================"
echo ""

# ── Step 1: Check build tools ─────────────────────────────────────────────────
echo "Step 1: Checking build tools..."

PLATFORM="$(uname -s)"

if ! command -v cmake &>/dev/null; then
    echo "✗ cmake not found. Install CMake 3.20+ and try again."
    echo "  macOS:  brew install cmake"
    echo "  Linux:  sudo apt install cmake  (or equivalent)"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -1 | awk '{print $3}')
echo "✓ cmake ${CMAKE_VERSION} found"

if ! command -v git &>/dev/null; then
    echo "✗ git not found. Install git and try again."
    exit 1
fi
echo "✓ git found"

# ── C++ compiler + platform system deps ──────────────────────────────────────
if [ "${PLATFORM}" = "Darwin" ]; then
    # On macOS all required frameworks (CoreAudio, OpenGL, etc.) ship with the OS.
    # The only prerequisite is Xcode Command Line Tools.
    if xcode-select -p &>/dev/null 2>&1; then
        echo "✓ Xcode Command Line Tools found"
    elif command -v c++ &>/dev/null || command -v clang++ &>/dev/null; then
        echo "✓ C++ compiler found"
    else
        echo "✗ Xcode Command Line Tools not found."
        echo "  Install: xcode-select --install"
        echo "  Then re-run ./init.sh"
        exit 1
    fi
else
    # Linux — collect all missing system deps before exiting so the user sees
    # everything in one pass and can install with a single apt command.
    MISSING_DEPS=()

    # C++ compiler
    if command -v g++ &>/dev/null; then
        echo "✓ g++ found"
    elif command -v clang++ &>/dev/null; then
        echo "✓ clang++ found"
    else
        echo "✗ C++ compiler not found (g++ / clang++)"
        MISSING_DEPS+=("build-essential")
    fi

    # OpenGL dev headers — required by AlloLib (find_package(OpenGL REQUIRED))
    if pkg-config --exists gl 2>/dev/null || [ -f /usr/include/GL/gl.h ]; then
        echo "✓ OpenGL headers found"
    else
        echo "✗ OpenGL headers not found"
        MISSING_DEPS+=("libgl-dev")
    fi

    # ALSA dev headers — required by rtaudio / rtmidi
    if pkg-config --exists alsa 2>/dev/null || [ -f /usr/include/alsa/asoundlib.h ]; then
        echo "✓ ALSA headers found"
    else
        echo "✗ ALSA headers not found"
        MISSING_DEPS+=("libasound2-dev")
    fi

    # X11 base headers — required by GLFW
    if [ -f /usr/include/X11/Xlib.h ]; then
        echo "✓ X11 headers found"
    else
        echo "✗ X11 headers not found"
        MISSING_DEPS+=("libx11-dev")
    fi

    # X11 extension headers — Xrandr, Xinerama, Xcursor, Xi (all required by GLFW)
    X11_EXT_OK=1
    [ -f /usr/include/X11/extensions/Xrandr.h ]   || X11_EXT_OK=0
    [ -f /usr/include/X11/extensions/Xinerama.h ] || X11_EXT_OK=0
    [ -f /usr/include/X11/Xcursor/Xcursor.h ]     || X11_EXT_OK=0
    [ -f /usr/include/X11/extensions/XInput.h ]   || X11_EXT_OK=0
    if [ "${X11_EXT_OK}" -eq 1 ]; then
        echo "✓ X11 extension headers found (Xrandr, Xinerama, Xcursor, Xi)"
    else
        echo "✗ X11 extension headers missing (Xrandr / Xinerama / Xcursor / Xi)"
        MISSING_DEPS+=("libxrandr-dev" "libxinerama-dev" "libxcursor-dev" "libxi-dev")
    fi

    if [ ${#MISSING_DEPS[@]} -gt 0 ]; then
        echo ""
        echo "Missing system dependencies. Install with:"
        echo "  sudo apt install ${MISSING_DEPS[*]}"
        echo "  (or the equivalent package names for your distro)"
        echo ""
        echo "Then re-run ./init.sh"
        exit 1
    fi
fi
echo ""

# ── Step 2: Initialize LUSID submodule ────────────────────────────────────────
echo "Step 2: Initializing LUSID submodule..."
ensure_submodule "internal/LUSID" "${PROJECT_ROOT}/internal/LUSID/README.md"
echo ""

# ── Step 3: Initialize cult-allolib submodule ─────────────────────────────────
echo "Step 3: Initializing cult-allolib submodule..."
ensure_submodule "internal/cult-allolib" "${PROJECT_ROOT}/internal/cult-allolib/include" "yes"
echo ""

# ── Step 4: Initialize cult_transcoder submodule ──────────────────────────────
echo "Step 4: Initializing cult_transcoder submodule..."
ensure_submodule "internal/cult_transcoder" "${PROJECT_ROOT}/internal/cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp" "yes"
echo ""

# ── Step 5: Initialize libsndfile submodule ──────────────────────────────────
echo "Step 5: Initializing libsndfile submodule..."
ensure_submodule "thirdparty/libsndfile" "${PROJECT_ROOT}/thirdparty/libsndfile/CMakeLists.txt"
echo ""

# ── Step 6: Initialize Dear ImGui submodule (optional — needed for GUI build) ─
IMGUI_DIR="${PROJECT_ROOT}/thirdparty/imgui"
GUI_IMG_SUBMODULE=0
GUI_GLFW_SUBMODULE=0
if submodule_is_registered "thirdparty/imgui"; then
    GUI_IMG_SUBMODULE=1
    echo "Step 6: Initializing Dear ImGui submodule..."
    ensure_submodule "thirdparty/imgui" "${IMGUI_DIR}/imgui.h"
else
    echo "ℹ  thirdparty/imgui not registered"
fi
echo ""

# ── Step 7: Initialize GLFW submodule (optional — needed for GUI build) ───────
GLFW_DIR="${PROJECT_ROOT}/thirdparty/glfw"
if submodule_is_registered "thirdparty/glfw"; then
    GUI_GLFW_SUBMODULE=1
    echo "Step 7: Initializing GLFW submodule..."
    ensure_submodule "thirdparty/glfw" "${GLFW_DIR}/CMakeLists.txt"
else
    echo "ℹ  thirdparty/glfw not registered"
fi
echo ""

# ── Step 8: Build all C++ components ─────────────────────────────────────────
echo "Step 8: Building all C++ components..."
echo ""
BUILD_ARGS=("$@")
GUI_BUILD_ENABLED=0
if [ "${GUI_IMG_SUBMODULE}" -eq 1 ] && [ "${GUI_GLFW_SUBMODULE}" -eq 1 ]; then
    GUI_BUILD_ENABLED=1
    BUILD_ARGS=(--gui "${BUILD_ARGS[@]}")
    echo "GUI submodules detected — building GUI target too."
else
    echo "GUI submodules are not fully registered — building non-GUI targets only."
    echo "Register both thirdparty/imgui and thirdparty/glfw in .gitmodules to enable GUI builds."
fi
echo ""
"${PROJECT_ROOT}/build.sh" "${BUILD_ARGS[@]}"

echo ""
echo "============================================================"
echo "✓ Initialization complete!"
echo ""
echo "Binaries:"
echo "  spatialroot_realtime       : build/source/spatial_engine/realtimeEngine/spatialroot_realtime"
echo "  spatialroot_spatial_render : build/source/spatial_engine/spatialRender/spatialroot_spatial_render"
echo "  cult-transcoder            : build/internal/cult_transcoder/cult-transcoder"
if [ "${GUI_BUILD_ENABLED}" -eq 1 ]; then
    echo "  spatialroot_gui            : build/source/gui/imgui/Spatial Root"
fi
echo ""
echo "For quick dev rebuilds of the realtime engine only:"
echo "  ./build.sh --engine-only"
echo ""
echo "For subsequent full builds:"
if [ "${GUI_BUILD_ENABLED}" -eq 1 ]; then
    echo "  ./build.sh --gui"
else
    echo "  ./build.sh"
fi
echo "============================================================"
echo ""
