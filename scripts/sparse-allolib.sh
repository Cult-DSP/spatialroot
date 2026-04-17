#!/usr/bin/env bash
# =============================================================================
# scripts/sparse-allolib.sh
#
# ⚠️  OPT-IN ONLY — DO NOT RUN IN CI OR init.sh ⚠️
#
# PURPOSE
#   Apply a sparse-checkout filter inside internal/cult-allolib to limit the
#   working tree to the paths spatialroot actually uses (Keep list) plus the
#   components retained for near-term real-time audio development (Likely-
#   Future list).  This removes ~14 MB of graphics/UI/window source files
#   from disk while leaving all audio, math, spatial, system, OSC, and
#   app-domain files intact.
#
#   NOTE: Phase 6 of the cult-allolib plan will prune unneeded files from the
#   fork permanently.  This script is the interim option for developers who
#   want a smaller working tree before that pruning pass runs.
#
# WHY OPT-IN ONLY
#   Sparse checkout state lives inside the submodule's git store
#   (.git/modules/internal/cult-allolib/info/sparse-checkout).  If the pinned
#   commit is bumped via `git submodule update`, git will re-materialize the
#   full tree UNLESS the sparse patterns are reapplied.  AlloLib's own
#   CMakeLists.txt unconditionally lists every source file, so a trimmed tree
#   will cause a CMake configure error unless you either:
#     (a) patch cult-allolib's CMakeLists.txt to skip missing files, OR
#     (b) accept that build will be limited to the spatialroot subset only.
#
#   Default init.sh and CI use a FULL working-tree checkout.  This script is
#   for developers who understand the trade-off and want faster local builds.
#
# WHAT IT REMOVES FROM THE WORKING TREE
#   (does not delete from git history — only hides from checkout)
#   • include/al/graphics/    — OpenGL graphics
#   • src/graphics/           — OpenGL graphics sources
#   • include/al/ui/          — Parameter/preset GUI (Qt-based in spatialroot)
#   • src/ui/                 — GUI sources
#   • include/al/sphere/      — AlloSphere-specific projection
#   • src/sphere/             — AlloSphere sources
#   • external/glfw/          — Window creation (4.5 MB)
#   • external/imgui/         — Dear ImGui (5.1 MB)
#   • external/stb/           — Image/font loading (2.0 MB)
#   • external/glad/          — OpenGL loader (336 KB)
#   • external/serial/        — Serial port (324 KB)
#   • external/dr_libs/       — Audio file decoding alt (744 KB)
#   • Window/ImGui IO sources  — al_Window*, al_Imgui*
#
# WHAT IT KEEPS (Keep + Likely-Future lists)
#   • include/al/sound/       — spatializers (DBAP, VBAP, LBAP, Speaker…)
#   • include/al/math/        — Vec, Mat, Quat, Spherical…
#   • include/al/spatial/     — Pose, HashSpace
#   • include/al/io/          — AudioIOData, AudioIO
#   • include/al/app/         — App, AudioDomain, SimulationDomain…
#   • include/al/system/      — Thread, Time, PeriodicThread
#   • include/al/protocol/    — OSC
#   • include/al/scene/       — PolySynth, DynamicScene, SynthVoice
#   • include/al/types/       — Color (small, pulled by graphics chain)
#   • src/sound/              — spatializer implementations
#   • src/spatial/            — Pose, HashSpace
#   • src/io/al_AudioIO*.cpp, al_AudioIOData.cpp
#   • src/app/                — App + audio/sim domains
#   • src/system/             — Thread, Time, PeriodicThread
#   • src/math/               — StdRandom
#   • src/protocol/           — OSC
#   • src/scene/              — PolySynth, DynamicScene…
#   • src/types/              — Color
#   • external/Gamma/         — DSP library (linked today)
#   • external/rtaudio/       — Audio device backend
#   • external/rtmidi/        — MIDI
#   • external/oscpack/       — OSC transport
#   • external/json/          — nlohmann (used by CMake + JSONLoader)
#   • external/cpptoml/       — config file parsing
#   • CMakeLists.txt, readme.md, LICENSE, .github/, .travis.yml
#   • external/CMakeLists.txt
#
# USAGE
#   ./scripts/sparse-allolib.sh          # apply sparse checkout
#   ./scripts/sparse-allolib.sh --reset  # restore full checkout
#
# RESTORE FULL CHECKOUT
#   To undo and restore all files:
#     cd internal/cult-allolib
#     git sparse-checkout disable
#   Or from repo root:
#     ./scripts/sparse-allolib.sh --reset
#
# =============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ALLOLIB_DIR="$REPO_ROOT/internal/cult-allolib"

MODE="${1:-apply}"   # "apply" or "--reset"

# ---------------------------------------------------------------------------
# Guard: submodule must be initialized
# ---------------------------------------------------------------------------
if [ ! -f "$ALLOLIB_DIR/CMakeLists.txt" ]; then
    echo "ERROR: internal/cult-allolib does not appear to be initialized."
    echo "Run:  git submodule update --init --recursive internal/cult-allolib"
    exit 1
fi

# ---------------------------------------------------------------------------
# Reset mode — restore full working tree
# ---------------------------------------------------------------------------
if [ "$MODE" = "--reset" ]; then
    echo "Restoring full working tree in internal/cult-allolib..."
    git -C "$ALLOLIB_DIR" sparse-checkout disable 2>/dev/null || \
        git -C "$ALLOLIB_DIR" read-tree -mu HEAD
    echo "✓ Full checkout restored."
    echo "  You may need to re-run cmake to pick up restored files."
    exit 0
fi

# ---------------------------------------------------------------------------
# Apply mode
# ---------------------------------------------------------------------------
echo ""
echo "⚠️  sparse-allolib.sh — OPT-IN sparse checkout mode"
echo "   This is NOT run by init.sh or CI."
echo "   See script header for details and restore instructions."
echo ""

SIZE_BEFORE=$(du -sh "$ALLOLIB_DIR" 2>/dev/null | cut -f1)
echo "Working tree size before: $SIZE_BEFORE"
echo ""

# ---------------------------------------------------------------------------
# Build sparse-checkout pattern list
# ---------------------------------------------------------------------------
# git sparse-checkout uses "cone mode" by default in modern git (≥2.25).
# We use non-cone (--no-cone) for explicit file/dir patterns.
# ---------------------------------------------------------------------------

PATTERNS=(
    # Root files always needed
    "CMakeLists.txt"
    "readme.md"
    "LICENSE"
    ".gitmodules"
    ".gitattributes"
    ".travis.yml"
    "allosystem_diff.txt"

    # GitHub CI
    ".github/"

    # External CMake orchestrator
    "external/CMakeLists.txt"

    # --- KEEP: directly used today ---
    # Math
    "include/al/math/"
    "src/math/"

    # Spatial geometry
    "include/al/spatial/"
    "src/spatial/"

    # Sound / spatializers
    "include/al/sound/"
    "src/sound/"

    # IO — AudioIOData (used as spatializer buffer type)
    "include/al/io/al_AudioIOData.hpp"
    "include/al/io/al_AudioIO.hpp"
    "src/io/al_AudioIO.cpp"
    "src/io/al_AudioIOData.cpp"

    # System — thread/time (pulled by spatializer chain)
    "include/al/system/"
    "src/system/"

    # Types — Color (small; included by al_Spatializer chain on some paths)
    "include/al/types/"
    "src/types/"

    # Gamma DSP — linked today
    "external/Gamma/"

    # nlohmann/json — used by AlloLib CMake and spatialroot JSONLoader
    "external/json/"

    # --- LIKELY-FUTURE: real-time audio engine ---

    # App framework + audio/sim domains
    "include/al/app/"
    "src/app/"

    # Additional IO headers for real-time audio
    "include/al/io/al_File.hpp"
    "include/al/io/al_PersistentConfig.hpp"
    "include/al/io/al_Toml.hpp"

    # OSC / network
    "include/al/protocol/"
    "src/protocol/"
    "external/oscpack/"

    # Scene / voice management
    "include/al/scene/"
    "src/scene/"

    # RtAudio — cross-platform audio device backend
    "external/rtaudio/"

    # RtMidi — MIDI triggers / sync
    "external/rtmidi/"

    # cpptoml — config files
    "external/cpptoml/"
)

# ---------------------------------------------------------------------------
# Apply sparse checkout
# ---------------------------------------------------------------------------

# Ensure git supports sparse-checkout
GIT_VERSION=$(git --version | awk '{print $3}')
echo "git version: $GIT_VERSION"

# Initialize sparse-checkout in non-cone mode
git -C "$ALLOLIB_DIR" sparse-checkout init --no-cone

# Write patterns
SPARSE_FILE="$REPO_ROOT/.git/modules/internal/cult-allolib/info/sparse-checkout"
# Fallback location if modules dir layout differs
if [ ! -d "$(dirname "$SPARSE_FILE")" ]; then
    SPARSE_FILE="$ALLOLIB_DIR/.git/info/sparse-checkout"
fi

printf '%s\n' "${PATTERNS[@]}" > "$SPARSE_FILE"
echo "Wrote $(wc -l < "$SPARSE_FILE") sparse-checkout patterns to:"
echo "  $SPARSE_FILE"
echo ""

# Apply patterns to working tree
git -C "$ALLOLIB_DIR" sparse-checkout reapply

SIZE_AFTER=$(du -sh "$ALLOLIB_DIR" 2>/dev/null | cut -f1)
echo ""
echo "Working tree size after: $SIZE_AFTER  (was: $SIZE_BEFORE)"
echo ""
echo "✓ Sparse checkout applied."
echo ""
echo "⚠️  IMPORTANT NOTES:"
echo "   1. cult-allolib's CMakeLists.txt lists ALL source files unconditionally."
echo "      CMake will error on missing files (graphics/ui/sphere/stb/glad)."
echo "      To build spatialroot after sparse checkout, you must either:"
echo "        a) Use ALLOLIB_USE_DUMMY_AUDIO + skip graphics in your cmake flags"
echo "        b) Or wait for Phase 6 pruning (planned removal of unneeded files)"
echo "      The current spatialroot CMakeLists.txt does NOT patch this — full"
echo "      cult-allolib checkout is the supported build path."
echo ""
echo "   2. If you update the pinned cult-allolib commit, rerun this script to"
echo "      reapply sparse patterns after \`git submodule update\`."
echo ""
echo "   To restore a full checkout at any time:"
echo "     ./scripts/sparse-allolib.sh --reset"
echo "   Or:"
echo "     cd internal/cult-allolib && git sparse-checkout disable"
echo ""
