#!/bin/bash
# build.sh — CMake configure + build for spatialroot
#
# Usage:
#   ./build.sh                  # Build all components (engine, offline, cult)
#   ./build.sh --engine-only    # Build spatialroot_realtime only
#   ./build.sh --offline-only   # Build spatialroot_spatial_render only
#   ./build.sh --cult-only      # Build cult-transcoder only
#
# Run init.sh once before the first build to initialize submodules.
# Subsequent builds can call build.sh directly.

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# ── Argument parsing ──────────────────────────────────────────────────────────
BUILD_ENGINE=ON
BUILD_OFFLINE=ON
BUILD_CULT=ON
BUILD_GUI=OFF  # OFF until Qt is confirmed and Stage 3 GUI is implemented

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
        --help|-h)
            echo "Usage: ./build.sh [--engine-only | --offline-only | --cult-only]"
            exit 0
            ;;
        *)
            echo "Unknown argument: $arg"
            echo "Usage: ./build.sh [--engine-only | --offline-only | --cult-only]"
            exit 1
            ;;
    esac
done

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
    "${PROJECT_ROOT}"

echo ""
echo "Building (${NUM_CORES} cores)..."
cmake --build "${BUILD_DIR}" --parallel "${NUM_CORES}"

echo ""
echo "============================================================"
echo "✓ Build complete!"
echo ""
if [ "${BUILD_ENGINE}" = "ON" ]; then
    echo "  spatialroot_realtime       : ${BUILD_DIR}/spatial_engine/realtimeEngine/spatialroot_realtime"
fi
if [ "${BUILD_OFFLINE}" = "ON" ]; then
    echo "  spatialroot_spatial_render : ${BUILD_DIR}/spatial_engine/spatialRender/spatialroot_spatial_render"
fi
if [ "${BUILD_CULT}" = "ON" ]; then
    echo "  cult-transcoder            : ${BUILD_DIR}/cult_transcoder/cult-transcoder"
fi
echo "============================================================"
echo ""
