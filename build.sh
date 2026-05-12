#!/bin/bash
# build.sh — CMake configure + build for spatialroot
#
# Usage:
#   ./build.sh                  # Build all components (engine, offline, cult)
#   ./build.sh --engine-only    # Build spatialroot_realtime only
#   ./build.sh --offline-only   # Build spatialroot_spatial_render only
#   ./build.sh --cult-only      # Build cult-transcoder only
#   ./build.sh --gui            # Build all + ImGui + GLFW desktop GUI
#
# Run init.sh once before the first build to initialize submodules.
# The --gui flag requires thirdparty/imgui and thirdparty/glfw submodules
# (see init.sh — they are initialized automatically when registered).
# Subsequent builds can call build.sh directly.

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

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

ensure_submodule_for_build() {
    local path="$1"
    local sentinel="$2"
    local recursive="${3:-no}"

    if [ -e "$sentinel" ] && ! submodule_has_missing_recursive "$path"; then
        return
    fi

    echo "Initializing required submodule: $path"
    git submodule sync --recursive "$path"
    if [ "$recursive" = "yes" ]; then
        git submodule update --init --recursive --depth 1 --checkout "$path"
    else
        git submodule update --init --depth 1 --checkout "$path"
    fi
}

# ── Argument parsing ──────────────────────────────────────────────────────────
BUILD_ENGINE=ON
BUILD_OFFLINE=ON
BUILD_CULT=ON
BUILD_GUI=OFF  # Use --gui flag to enable (requires imgui + glfw submodules)
BUILD_DEVTOOLS=OFF

for arg in "$@"; do
    case "$arg" in
        --engine-only)
            BUILD_OFFLINE=OFF
            BUILD_CULT=OFF
            ;;
        --offline-only)
            BUILD_ENGINE=OFF
            BUILD_CULT=OFF
            ;;
        --cult-only)
            BUILD_ENGINE=OFF
            BUILD_OFFLINE=OFF
            ;;
        --gui)
            BUILD_GUI=ON
            ;;
        --help|-h)
            echo "Usage: ./build.sh [--engine-only | --offline-only | --cult-only | --gui]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: ./build.sh [--engine-only | --offline-only | --cult-only | --gui]"
            exit 1
            ;;
    esac
done

# ── Submodule bootstrap for fresh clones / moved paths ───────────────────────
ensure_submodule_for_build "internal/cult-allolib" "${PROJECT_ROOT}/internal/cult-allolib/include" "yes"
ensure_submodule_for_build "thirdparty/libsndfile" "${PROJECT_ROOT}/thirdparty/libsndfile/CMakeLists.txt"

if [ "${BUILD_CULT}" = "ON" ]; then
    ensure_submodule_for_build "internal/cult_transcoder" "${PROJECT_ROOT}/internal/cult_transcoder/thirdparty/libbw64/include/bw64/bw64.hpp" "yes"
fi

if [ "${BUILD_GUI}" = "ON" ]; then
    if ! submodule_is_registered "thirdparty/imgui" || ! submodule_is_registered "thirdparty/glfw"; then
        echo "✗ GUI build requested, but GUI submodules are not fully registered in .gitmodules."
        echo "  Required: thirdparty/imgui and thirdparty/glfw"
        echo "  Try: ./init.sh"
        echo "  Or build without GUI by omitting --gui."
        exit 1
    fi
    ensure_submodule_for_build "thirdparty/imgui" "${PROJECT_ROOT}/thirdparty/imgui/imgui.h"
    ensure_submodule_for_build "thirdparty/glfw" "${PROJECT_ROOT}/thirdparty/glfw/CMakeLists.txt"
fi

# ── CPU count ─────────────────────────────────────────────────────────────────
if command -v nproc &>/dev/null; then
    NUM_CORES=$(nproc)
elif command -v sysctl &>/dev/null; then
    NUM_CORES=$(sysctl -n hw.logicalcpu)
else
    NUM_CORES=4
fi

echo "============================================================"
echo "spatialroot build"
echo "  Engine   (spatialroot_realtime)       : ${BUILD_ENGINE}"
echo "  Offline  (spatialroot_spatial_render) : ${BUILD_OFFLINE}"
echo "  CULT     (cult-transcoder)            : ${BUILD_CULT}"
echo "  GUI      (spatialroot_gui ImGui+GLFW) : ${BUILD_GUI}"
echo "  Cores                                 : ${NUM_CORES}"
echo "============================================================"
echo ""

mkdir -p "${BUILD_DIR}"

echo "Configuring CMake..."
cmake \
    -B "${BUILD_DIR}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSPATIALROOT_BUILD_ENGINE="${BUILD_ENGINE}" \
    -DSPATIALROOT_BUILD_OFFLINE="${BUILD_OFFLINE}" \
    -DSPATIALROOT_BUILD_CULT="${BUILD_CULT}" \
    -DSPATIALROOT_BUILD_GUI="${BUILD_GUI}" \
    -DSPATIALROOT_BUILD_DEVTOOLS="${BUILD_DEVTOOLS}" \
    "${PROJECT_ROOT}"

echo ""
echo "Building (${NUM_CORES} cores)..."
cmake --build "${BUILD_DIR}" --parallel "${NUM_CORES}"

echo ""
echo "============================================================"
echo "✓ Build complete!"
echo ""
if [ "${BUILD_ENGINE}" = "ON" ]; then
    echo "  spatialroot_realtime       : ${BUILD_DIR}/source/spatial_engine/realtimeEngine/spatialroot_realtime"
fi
if [ "${BUILD_OFFLINE}" = "ON" ]; then
    echo "  spatialroot_spatial_render : ${BUILD_DIR}/source/spatial_engine/spatialRender/spatialroot_spatial_render"
fi
if [ "${BUILD_CULT}" = "ON" ]; then
    echo "  cult-transcoder            : ${BUILD_DIR}/internal/cult_transcoder/cult-transcoder"
fi
if [ "${BUILD_GUI}" = "ON" ]; then
    if [ "$(uname)" = "Darwin" ]; then
        GUI_OUTPUT="${BUILD_DIR}/source/gui/imgui/Spatial Root.app/Contents/MacOS/Spatial Root"
    else
        GUI_OUTPUT="${BUILD_DIR}/source/gui/imgui/Spatial Root"
    fi
    echo "  spatialroot_gui            : ${GUI_OUTPUT}"
fi
echo "============================================================"
echo ""
