#!/usr/bin/env bash
# run.sh — launch the Spatial Root GUI
#
# Must be run from the project root (the directory containing this script).
# The GUI binary receives --root pointing to this directory so that speaker
# layout presets and cult-transcoder resolve correctly.
#
# Usage:
#   ./run.sh              # launch GUI with project root = repo root
#   ./run.sh --help       # pass flags through to the GUI binary

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${SCRIPT_DIR}/build/gui/imgui/Spatial Root"

if [ ! -f "${BINARY}" ]; then
    echo "Error: \"Spatial Root\" not found at:"
    echo "  ${BINARY}"
    echo ""
    echo "Build the GUI first:"
    echo "  ./build.sh --gui"
    exit 1
fi

exec "${BINARY}" --root "${SCRIPT_DIR}" "$@"
