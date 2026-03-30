# Spatial Root — C++ Refactor Log

Granular change log for the C++ refactor. Append an entry after every discrete action.
See `refactor_planning.md` for stage-level status updates.

---

## Entry format

```
### [Stage N — Task description]
**Date:** YYYY-MM-DD
**Files changed:** list each file
**What was done:** one or two sentences
**Notes:** anything surprising, a decision made, or a constraint discovered
```

---

## Log

<!-- Agent: append entries below this line. Do not edit above it. -->

### [Stage 1 — Root CMakeLists.txt]
**Date:** 2026-03-29
**Files changed:** `CMakeLists.txt` (created)
**What was done:** Created root CMakeLists.txt with option flags (SPATIALROOT_BUILD_ENGINE, SPATIALROOT_BUILD_OFFLINE, SPATIALROOT_BUILD_CULT, SPATIALROOT_BUILD_GUI) and add_subdirectory wiring for all components. AlloLib is added once at the root level when ENGINE or OFFLINE is enabled.
**Notes:** Set cmake_minimum_required to 3.20 (not 3.16 as the planning doc suggested) because cult_transcoder/CMakeLists.txt already declares 3.20 — raised to match the most restrictive submodule. SPATIALROOT_BUILD_GUI is OFF by default; gui/qt/ does not yet exist (Stage 3).

---

### [Stage 1 — AlloLib double-include guard]
**Date:** 2026-03-29
**Files changed:** `spatial_engine/realtimeEngine/CMakeLists.txt`, `spatial_engine/spatialRender/CMakeLists.txt`
**What was done:** Wrapped add_subdirectory(allolib) in both component CMakeLists files with `if(NOT TARGET al)` guard. Without this, including both components from the root would define the `al` target twice and fail configuration.
**Notes:** Standalone builds (cmake from within the component directory) still work unchanged; the guard is a no-op when building standalone.

---

### [Stage 1 — EngineSessionCore install() targets]
**Date:** 2026-03-29
**Files changed:** `spatial_engine/realtimeEngine/CMakeLists.txt`
**What was done:** Added CMake install() targets for EngineSessionCore static library, public headers (EngineSession.hpp, RealtimeTypes.hpp), and exported CMake target file (EngineSessionCoreTargets.cmake) under the spatialroot:: namespace.
**Notes:** AlloLib headers are already exposed via PUBLIC target_include_directories — an embedding host inherits them automatically. Also renamed the first "── Executable ──" comment to "── Core engine library ──" to fix duplicate section heading that confused the edit.

---

### [Stage 1 — build.sh + init.sh (macOS/Linux)]
**Date:** 2026-03-29
**Files changed:** `build.sh` (created), `init.sh` (rewritten)
**What was done:** Created build.sh that runs cmake configure + cmake --build on the root CMakeLists.txt with --engine-only / --offline-only / --cult-only argument support. Rewrote init.sh to: (1) check cmake/git, (2) init allolib submodule, (3) init cult_transcoder and its nested libbw64 submodule, (4) call build.sh. No Python dependency.
**Notes:** The old init.sh created a Python venv and called configCPP_posix.py::setupCppTools(). The new init.sh is pure bash. The old posix config used raw `make -jN` for spatial/realtime builds but `cmake --build` for cult; the new build.sh uses `cmake --build` uniformly for generator independence.

---

### [Stage 1 — build.ps1 + init.ps1 (Windows)]
**Date:** 2026-03-29
**Files changed:** `build.ps1` (created), `init.ps1` (rewritten)
**What was done:** Created build.ps1 (PowerShell equivalent of build.sh) and rewrote init.ps1 (submodule init + call build.ps1). No Python dependency.
**Notes:** Windows multi-config generators (Visual Studio) place binaries under build/Release/; binary path summary in build.ps1 reflects this.

---

### [Stage 1 — README.md rewrite]
**Date:** 2026-03-29
**Files changed:** `README.md`
**What was done:** Complete rewrite. spatialroot_realtime documented as the primary CLI with actual flags. init.sh + build.sh documented as the build path. Two-step ADM workflow (cult-transcoder transcode → spatialroot_realtime) documented. OSC port fixed: README previously said 12345, code uses 9009. Qt GUI noted as in development (C++). Python GUI references retained with a note it will be removed in Stage 3.
**Notes:** Removed all references to realtimeMain.py, runRealtime.py, runPipeline.py as primary entry points. Python GUI section retained as a transition note per plan (removal in Stage 3 only).

---

### [Stage 1 — API.md constraints and embedding instructions]
**Date:** 2026-03-29
**Files changed:** `PUBLIC_DOCS/API.md`
**What was done:** Added Constraints section documenting: staged setup requirement, shutdown() is terminal, OSC ownership, shutdown order, 64-channel bitmask limit, update() main-thread requirement. Added Embedding Instructions section with CMake and include path guidance. Updated "Out of Scope for V1" to note runtime setters are planned for V1.1.
**Notes:** The 64-channel limit and OSC ownership constraint were previously undocumented. Embedding instructions reflect that AlloLib headers are already transitively exposed via the existing PUBLIC target_include_directories.
