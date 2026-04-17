#!/usr/bin/env bash
# =============================================================================
# scripts/shallow-submodules.sh
#
# PURPOSE
#   Re-initialize all spatialroot git submodules as shallow clones (--depth 1)
#   to reduce the clone/build footprint.
#
#   The primary target is internal/cult-allolib, whose full git history can
#   occupy significant space in .git/modules/internal/cult-allolib while the
#   working tree itself is much smaller.  After running this script the history
#   store shrinks to a few MB of pack objects while the pinned commit on disk
#   is unchanged.
#
# WHAT IT DOES
#   For each submodule:
#     1. Checks whether it is already shallow (is-shallow-repository).
#        If yes, skips it (idempotent).
#     2. Records the currently-pinned commit SHA.
#     3. Deinits and removes the cached .git/modules entry.
#     4. Re-initializes with `git submodule update --init --depth 1`.
#        Git will fetch only the single commit that HEAD points to.
#     5. Prints a before/after .git/modules size comparison.
#
# USAGE
#   ./scripts/shallow-submodules.sh            # all submodules
#   ./scripts/shallow-submodules.sh allolib    # allolib only (substring match)
#
# SAFETY
#   - Idempotent: safe to run multiple times.
#   - Does NOT change the pinned commit — the working tree is identical before
#     and after.
#   - Does NOT affect remote tracking or push ability.
#   - Shallow history cannot be deepened later without a `git fetch --unshallow`
#     inside the submodule.  That is intentional.
#
# COMPATIBILITY
#   Default init.sh and CI both use full checkout by default; this script is
#   an explicit opt-in for developers who want to reduce local disk usage.
#   After running this script, subsequent `git submodule update` invocations
#   will remain shallow as long as .gitmodules has `shallow = true` for each
#   submodule (set by this script's companion change to .gitmodules).
#
# =============================================================================

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

FILTER="${1:-}"   # optional substring filter, e.g. "allolib"

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

hr() { printf '%s\n' "────────────────────────────────────────────────────────"; }

dir_size() {
    # Returns human-readable size of a path, or "0" if it does not exist.
    if [ -e "$1" ]; then
        du -sh "$1" 2>/dev/null | cut -f1
    else
        echo "0"
    fi
}

is_shallow() {
    # Returns 0 (true) if the git repo at $1 is a shallow clone.
    local repo="$1"
    local result
    result=$(git -C "$repo" rev-parse --is-shallow-repository 2>/dev/null || echo "false")
    [ "$result" = "true" ]
}

# ---------------------------------------------------------------------------
# Collect submodule paths from .gitmodules
# ---------------------------------------------------------------------------

mapfile -t SUBMODULE_PATHS < <(
    git config -f .gitmodules --get-regexp 'submodule\..*\.path' \
        | awk '{print $2}'
)

if [ ${#SUBMODULE_PATHS[@]} -eq 0 ]; then
    echo "No submodules found in .gitmodules — nothing to do."
    exit 0
fi

echo ""
echo "spatialroot — shallow-submodules.sh"
hr
echo "Repo root : $REPO_ROOT"
echo "Submodules: ${SUBMODULE_PATHS[*]}"
[ -n "$FILTER" ] && echo "Filter    : '$FILTER'"
echo ""

TOTAL_SAVED=0
ANY_PROCESSED=false

for SUBPATH in "${SUBMODULE_PATHS[@]}"; do

    # Apply optional filter
    if [ -n "$FILTER" ] && [[ "$SUBPATH" != *"$FILTER"* ]]; then
        continue
    fi

    ANY_PROCESSED=true
    SUBNAME=$(git config -f .gitmodules --get "submodule.${SUBPATH}.url" 2>/dev/null || echo "$SUBPATH")
    MODULES_DIR="$REPO_ROOT/.git/modules/$SUBPATH"

    hr
    echo "Submodule : $SUBPATH"
    echo "Remote    : $SUBNAME"
    echo ""

    # -----------------------------------------------------------------
    # 1. Already shallow?
    # -----------------------------------------------------------------
    if [ -d "$REPO_ROOT/$SUBPATH/.git" ] || [ -f "$REPO_ROOT/$SUBPATH/.git" ]; then
        WORK_GIT="$REPO_ROOT/$SUBPATH"
    else
        WORK_GIT=""
    fi

    if [ -n "$WORK_GIT" ] && is_shallow "$WORK_GIT"; then
        echo "  ✓ Already shallow — skipping."
        echo ""
        continue
    fi

    # -----------------------------------------------------------------
    # 2. Record current pinned commit
    # -----------------------------------------------------------------
    PINNED_SHA=""
    if [ -n "$WORK_GIT" ]; then
        PINNED_SHA=$(git -C "$WORK_GIT" rev-parse HEAD 2>/dev/null || echo "")
    fi
    if [ -z "$PINNED_SHA" ]; then
        PINNED_SHA=$(git ls-files -s "$SUBPATH" 2>/dev/null | awk '{print $2}' || echo "")
    fi
    echo "  Pinned commit : ${PINNED_SHA:-unknown}"

    # -----------------------------------------------------------------
    # 3. Size before
    # -----------------------------------------------------------------
    SIZE_BEFORE=$(dir_size "$MODULES_DIR")
    echo "  .git/modules size before : $SIZE_BEFORE"

    # -----------------------------------------------------------------
    # 4. Deinit + remove cached modules entry
    # -----------------------------------------------------------------
    echo "  Deinitializing submodule..."
    git submodule deinit -f "$SUBPATH" 2>/dev/null || true

    if [ -d "$MODULES_DIR" ]; then
        echo "  Removing cached .git/modules entry (~$SIZE_BEFORE)..."
        rm -rf "$MODULES_DIR"
    fi

    # Also remove the working tree entry if it still exists as a stale dir
    if [ -d "$REPO_ROOT/$SUBPATH" ] && [ -z "$(ls -A "$REPO_ROOT/$SUBPATH" 2>/dev/null)" ]; then
        rmdir "$REPO_ROOT/$SUBPATH" 2>/dev/null || true
    fi

    # -----------------------------------------------------------------
    # 5. Re-initialize with depth 1
    # -----------------------------------------------------------------
    echo "  Re-initializing with --depth 1..."
    git submodule update --init --depth 1 -- "$SUBPATH"

    # -----------------------------------------------------------------
    # 6. Size after
    # -----------------------------------------------------------------
    SIZE_AFTER=$(dir_size "$MODULES_DIR")
    echo ""
    echo "  .git/modules size after  : $SIZE_AFTER"
    echo "  ✓ Done — $SUBPATH is now shallow."
    echo ""

done

if [ "$ANY_PROCESSED" = false ]; then
    echo "No submodules matched filter '$FILTER'."
fi

hr
echo ""
echo "All done."
echo ""
echo "Notes:"
echo "  • Future clones will be shallow if .gitmodules has 'shallow = true'"
echo "    for each submodule (see companion change)."
echo "  • To deepen a submodule later:"
echo "      git -C internal/cult-allolib fetch --unshallow"
echo "  • To verify shallow status:"
echo "      git -C internal/cult-allolib rev-parse --is-shallow-repository"
echo ""
