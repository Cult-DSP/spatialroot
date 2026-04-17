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

# ── Step 2: Initialize cult-allolib submodule ─────────────────────────────────
echo "Step 2: Initializing cult-allolib submodule..."

CULT_ALLOLIB_INCLUDE="${PROJECT_ROOT}/internal/cult-allolib/include"
if [ -d "${CULT_ALLOLIB_INCLUDE}" ]; then
    echo "✓ internal/cult-allolib already initialized"
else
    echo "Fetching internal/cult-allolib (shallow, depth=1)..."
    git submodule update --init --recursive --depth 1 internal/cult-allolib
    echo "✓ internal/cult-allolib initialized"
fi
echo ""

# ── Step 3: Initialize cult_transcoder submodule ──────────────────────────────
echo "Step 3: Initializing cult_transcoder submodule..."

CULT_CMAKE="${PROJECT_ROOT}/cult_transcoder/CMakeLists.txt"
if [ -f "${CULT_CMAKE}" ]; then
    echo "✓ cult_transcoder already initialized"
else
    echo "Fetching cult_transcoder..."
    git submodule update --init --depth 1 cult_transcoder
    echo "✓ cult_transcoder initialized"
fi

# cult_transcoder owns nested vendored deps that must be present before configure.
LIBBW64_HEADER="${PROJECT_ROOT}/cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp"
R8BRAIN_HEADER="${PROJECT_ROOT}/cult_transcoder/thirdparty/r8brain/CDSPResampler.h"
if [ -f "${LIBBW64_HEADER}" ] && [ -f "${R8BRAIN_HEADER}" ]; then
    echo "✓ cult_transcoder nested submodules already initialized"
else
    echo "Fetching cult_transcoder nested submodules recursively..."
    git -C "${PROJECT_ROOT}/cult_transcoder" submodule update --init --recursive --depth 1
    echo "✓ cult_transcoder nested submodules initialized"
fi
echo ""

# ── Step 4: Initialize libsndfile submodule ──────────────────────────────────
echo "Step 4: Initializing libsndfile submodule..."

LIBSNDFILE_CMAKE="${PROJECT_ROOT}/thirdparty/libsndfile/CMakeLists.txt"
if [ -f "${LIBSNDFILE_CMAKE}" ]; then
    echo "✓ thirdparty/libsndfile already initialized"
else
    echo "Fetching thirdparty/libsndfile..."
    git submodule update --init thirdparty/libsndfile
    echo "✓ thirdparty/libsndfile initialized"
fi
echo ""

# ── Step 5: Initialize Dear ImGui submodule (optional — needed for GUI build) ─
# Only initialized when thirdparty/imgui has been added via:
#   git submodule add https://github.com/ocornut/imgui.git thirdparty/imgui
IMGUI_DIR="${PROJECT_ROOT}/thirdparty/imgui"
if [ -f "${PROJECT_ROOT}/.gitmodules" ] && grep -q "thirdparty/imgui" "${PROJECT_ROOT}/.gitmodules" 2>/dev/null; then
    if [ -f "${IMGUI_DIR}/imgui.h" ]; then
        echo "✓ thirdparty/imgui already initialized"
    else
        echo "Fetching thirdparty/imgui..."
        git submodule update --init --depth 1 thirdparty/imgui
        echo "✓ thirdparty/imgui initialized"
    fi
else
    echo "ℹ  thirdparty/imgui not registered (GUI build not enabled)"
fi
echo ""

# ── Step 6: Initialize GLFW submodule (optional — needed for GUI build) ───────
# Only initialized when thirdparty/glfw has been added via:
#   git submodule add https://github.com/glfw/glfw.git thirdparty/glfw
GLFW_DIR="${PROJECT_ROOT}/thirdparty/glfw"
if [ -f "${PROJECT_ROOT}/.gitmodules" ] && grep -q "thirdparty/glfw" "${PROJECT_ROOT}/.gitmodules" 2>/dev/null; then
    if [ -f "${GLFW_DIR}/CMakeLists.txt" ]; then
        echo "✓ thirdparty/glfw already initialized"
    else
        echo "Fetching thirdparty/glfw..."
        git submodule update --init --depth 1 thirdparty/glfw
        echo "✓ thirdparty/glfw initialized"
    fi
else
    echo "ℹ  thirdparty/glfw not registered (GUI build not enabled)"
fi
echo ""

# ── Step 7: Build all C++ components ─────────────────────────────────────────
echo "Step 7: Building all C++ components..."
echo ""
"${PROJECT_ROOT}/build.sh" --gui "$@"

echo ""
echo "============================================================"
echo "✓ Initialization complete!"
echo ""
echo "Binaries:"
echo "  spatialroot_realtime       : build/spatial_engine/realtimeEngine/spatialroot_realtime"
echo "  spatialroot_spatial_render : build/spatial_engine/spatialRender/spatialroot_spatial_render"
echo "  cult-transcoder            : build/cult_transcoder/cult-transcoder"
echo ""
echo "For quick dev rebuilds of the realtime engine only:"
echo "  ./engine.sh"
echo ""
echo "For subsequent full builds:"
echo "  ./build.sh"
echo "============================================================"
echo ""
