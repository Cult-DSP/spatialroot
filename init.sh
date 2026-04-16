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
echo ""

# ── Step 2: Initialize allolib submodule ──────────────────────────────────────
echo "Step 2: Initializing allolib submodule..."

ALLOLIB_INCLUDE="${PROJECT_ROOT}/thirdparty/allolib/include"
if [ -d "${ALLOLIB_INCLUDE}" ]; then
    echo "✓ thirdparty/allolib already initialized"
else
    echo "Fetching thirdparty/allolib (shallow, depth=1)..."
    git submodule update --init --recursive --depth 1 thirdparty/allolib
    echo "✓ thirdparty/allolib initialized"
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

# cult_transcoder owns its own libbw64 submodule (required before CMake configure)
LIBBW64_HEADER="${PROJECT_ROOT}/cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp"
if [ -f "${LIBBW64_HEADER}" ]; then
    echo "✓ cult_transcoder/thirdparty/libbw64 already initialized"
else
    echo "Fetching cult_transcoder/thirdparty/libbw64..."
    git -C "${PROJECT_ROOT}/cult_transcoder" submodule update --init --depth 1 thirdparty/libbw64
    echo "✓ cult_transcoder/thirdparty/libbw64 initialized"
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
