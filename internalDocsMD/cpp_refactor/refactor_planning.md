# Spatial Root — C++ Refactor Dev Plan

**Date:** 2026-03-29
**Source of truth:** `internalDocsMD/cpp_refactor/third_audit.md`
**Scope:** Full refactor: build infrastructure, API hardening, Qt GUI replacement

---

## Overview

Three stages. Each stage gates on human review before the next begins. The agent working any stage should ask for clarification frequently rather than resolve ambiguities independently.

| Stage | Name                                           | Gate                                                             |
| ----- | ---------------------------------------------- | ---------------------------------------------------------------- |
| 1     | Build infrastructure and docs                  | Human review of working build before any code changes            |
| 2     | `EngineSessionCore` hardening for Qt embedding | Human review of API before Qt GUI is built                       |
| 3     | Qt GUI + Python removal                        | Human verification of visual parity before Python GUI deprecated |

---

## Stage 1 — Build Infrastructure and Docs

**Status:** Complete — 2026-03-29. All tasks done. CMake minimum raised to 3.20 (cult_transcoder requires it). AlloLib double-include guards added to component CMakeLists files. Discovery Task B (OSC port=0 bug) identified: `start()` unconditionally creates ParameterServer; guard needed in Stage 2. Discovery Task A (Qt fetch strategy) not needed for Stage 1; recommended approach is `find_package(Qt6)` with system install, not FetchContent (Qt is ~1GB binary).

**Goal:** Replace the Python build system with `init.sh` + `build.sh` + a root `CMakeLists.txt`. Update docs to reflect the C++ binary as the primary interface. No audio engine code changes.

**Completion bar:** `init.sh` + `build.sh` runs from clean, produces all binaries, with no Python toolchain required. `README.md` reflects actual entry points.

### 1.1 Root CMakeLists.txt

Create a root `CMakeLists.txt` that ties all components together via `add_subdirectory()` with option flags. Each component can still be built independently — this is a convenience wrapper for dev, not a structural dependency.

The root `CMakeLists.txt` must declare `cmake_minimum_required(VERSION 3.16)` — required by Qt6. Verify the existing component CMakeLists files are compatible with this minimum; raise to 3.18+ if AlloLib or any submodule requires it.

Option flags (all ON by default except GUI which requires Qt):

```cmake
option(SPATIALROOT_BUILD_ENGINE        "Build spatialroot_realtime engine"     ON)
option(SPATIALROOT_BUILD_OFFLINE       "Build spatialroot_spatial_render"      ON)
option(SPATIALROOT_BUILD_CULT          "Build cult-transcoder CLI"             ON)
option(SPATIALROOT_BUILD_GUI           "Build Qt desktop GUI"                  OFF)
```

`SPATIALROOT_BUILD_GUI` is OFF by default until Qt is confirmed fetched. The agent implementing this should set it ON only after `init.sh` has fetched Qt successfully.

Note: CULT source is not modified. `add_subdirectory(cult_transcoder)` builds the existing CLI binary only. This does not interfere with other projects embedding CULT independently.

### 1.2 init.sh / init.ps1 and build.sh / build.ps1

The build system must support both macOS/Linux and Windows. Provide parallel scripts:

| Script | Platform | Role |
|---|---|---|
| `init.sh` | macOS / Linux | Fetch deps, call `build.sh` |
| `build.sh` | macOS / Linux | CMake configure + build |
| `init.ps1` | Windows | Fetch deps, call `build.ps1` |
| `build.ps1` | Windows | CMake configure + build |

**`init.sh` / `init.ps1`** — runs once to fetch all dependencies and then calls the build script.

Responsibilities:
- Initialize and update all git submodules (`thirdparty/allolib`, `thirdparty/libbw64`, `thirdparty/libadm`, `cult_transcoder/thirdparty/`)
- Fetch Qt via CMake FetchContent or system `find_package(Qt6)` — investigate which is appropriate (see Discovery Task A below)
- Call `build.sh` / `build.ps1` after dependency setup is complete

**`build.sh` / `build.ps1`** — compiles targets. Can be called independently after init has been run once.

Responsibilities:
- Run cmake configure + build on the root `CMakeLists.txt`
- Accept optional arguments to restrict build: `--engine-only`, `--gui-only`, `--offline-only`, or default to all
- These flags map to the CMake option flags above via `-D` arguments

**Before writing any scripts:** read both `src/config/configCPP_posix.py` and `src/config/configCPP_windows.py` and audit what each does beyond invoking CMake. If either performs platform-specific setup (tool detection, environment variables, PATH manipulation, submodule init), the replacement scripts must cover those steps.

Both script pairs replace `src/config/configCPP*.py`, `configCPP_posix.py`, `configCPP_windows.py`, and `configCPP.py`. Those Python files are removed in Stage 3.

**Discovery Task A — Qt fetch strategy:**
Investigate whether Qt should be fetched via `FetchContent` in CMake or via a system `find_package(Qt6)` with installation instructions. Factors: Qt license, binary size, CI compatibility, developer machine assumptions. Flag findings to user before implementing.

### 1.3 CMake install() target for EngineSessionCore

Add `install()` targets to `spatial_engine/realtimeEngine/CMakeLists.txt`:

- `EngineSessionCore` static library
- Public headers: `EngineSession.hpp`, `RealtimeTypes.hpp`
- AlloLib include path exposed via an exported CMake target (see Stage 2 task 2.1 for implementation detail)

Note: "build infrastructure" — this is a CMake change, not an audio engine source change.

### 1.4a engine.sh — keep as-is

`engine.sh` is a fast dev script for clean rebuilds of the realtime engine only (`rm -rf build/` → cmake → make). It is separate from `build.sh` and is intentionally kept. Do not modify or remove it.

### 1.5 README.md rewrite

- Document `spatialroot_realtime` as the primary CLI surface with its actual flags
- Document `init.sh` + `build.sh` as the build path, replacing all references to Python build scripts
- Remove references to `realtimeMain.py`, `runRealtime.py`, `runPipeline.py`
- Fix OSC port: code uses 9009, README currently says 12345 — code is authoritative
- Document the two-step ADM workflow: `cult-transcoder transcode ...` then `spatialroot_realtime ...`
- Note that a C++ Qt GUI is in development as the replacement for the Python GUI
- Do not remove references to the Python GUI yet — that happens in Stage 3

### 1.6 API.md — constraints and 64-channel limit

Add a documented constraints section to `PUBLIC_DOCS/API.md` covering:

- The staged setup sequence is non-negotiable
- `shutdown()` is terminal — construct a new `EngineSession` to restart
- OSC server ownership: `mParamServer` is internal, not shareable with the host
- Shutdown order: OSC server → audio backend → streaming (violating this causes deadlock on macOS CoreAudio)
- The `uint64_t` bitmasks in `EngineStatus` implicitly cap the engine at 64 output channels

---

## Stage 2 — EngineSessionCore Hardening for Embedding

**Status:** Complete (pending human review) — 2026-03-30. Tasks 2.1 (runtime setters), 2.3 (OSC port=0 guard), 2.4 (elevationMode type fix), embedding test (option A), and 2.5 (API.md) all done. Task 2.2 (full install export with EXPORT/find_package) deferred — the add_subdirectory path already propagates include dirs correctly for the embedding test use case.

**GUI framework decision (replaces Qt):** Dear ImGui + GLFW. Qt cannot be used as a git submodule (multi-GB source, complex bootstrap). All dependencies must be open-source submodules. Dear ImGui (MIT, ~5MB) + GLFW (zlib, ~1MB) are the replacement. This changes Stage 3 from "Qt GUI" to "ImGui + GLFW GUI". The architectural requirements (link EngineSessionCore, staged lifecycle, runtime setters, update() via timer/loop, no OSC dependency) are unchanged.

**Goal:** Expose a direct runtime setter surface on `EngineSession` so a Qt host can control live parameters without OSC. Fix the `oscPort = 0` guard. Fix the `elevationMode` type. Stabilize the public API surface for the Qt host.

**Completion bar:** A minimal C++ embedding test (not the full Qt GUI) compiles, links `EngineSessionCore`, calls the full lifecycle, sets parameters at runtime, and reads `queryStatus()`. No OSC dependency in this test.

### 2.1 Runtime setter methods on EngineSession

Add the following public methods to `EngineSession.hpp` and implement in `EngineSession.cpp`. These are V1.1 additions — update the "Out of Scope for V1" section in `API.md` accordingly.

| Method                                      | What it writes                                                        |
| ------------------------------------------- | --------------------------------------------------------------------- |
| `void setMasterGain(float gain)`            | `mConfig.masterGain` (linear 0.0–1.0)                                 |
| `void setDbapFocus(float focus)`            | `mConfig.dbapFocus` + sets `mPendingAutoComp` if auto-comp enabled    |
| `void setSpeakerMixDb(float dB)`            | `mConfig.loudspeakerMix` (dB→linear: `powf(10.f, dB/20.f)`)           |
| `void setSubMixDb(float dB)`                | `mConfig.subMix` (dB→linear: `powf(10.f, dB/20.f)`)                   |
| `void setAutoCompensation(bool enable)`     | `mConfig.focusAutoCompensation` + sets `mPendingAutoComp` if enabling |
| `void setElevationMode(ElevationMode mode)` | `mConfig.elevationMode` (cast to int)                                 |
| `void setPaused(bool isPaused)`             | already exists — no change                                            |

All writes use `std::memory_order_relaxed` — identical to the existing OSC callback implementations in `EngineSession.cpp`. The threading model does not change. `update()` already handles `mPendingAutoComp` recomputation from the main thread.

These methods are safe to call after `start()` and before `shutdown()`. Calling them before `start()` is a no-op on the atomics and harmless but has no effect on the engine — `configureRuntime()` remains the correct pre-start parameter path.

**Qt host contract (required integration note):** The Qt host must call `update()` regularly from the main thread while the session is running. This should be driven by a `QTimer` with an appropriate polling interval (e.g. 50ms). `queryStatus()` and `consumeDiagnostics()` should also be called in the same timer callback. The Qt host must use the standard staged lifecycle: `configureEngine()` → `loadScene()` → `applyLayout()` → `configureRuntime()` → `start()`, with runtime setters used only after successful `start()`.

### 2.2 AlloLib include path via exported CMake target

`EngineSession.hpp` includes AlloLib headers at include time. Any host that links `EngineSessionCore` — including the Stage 3 Qt GUI and the Stage 2 embedding test — must also resolve AlloLib's include path.

Solve this by exporting AlloLib's include path through the `EngineSessionCore` CMake target using `target_include_directories` with `PUBLIC` or `INTERFACE` scope. A host that does `target_link_libraries(myapp EngineSessionCore)` should automatically inherit the AlloLib include paths with no additional CMake configuration required.

In practice this means adding something like:

```cmake
target_include_directories(EngineSessionCore
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${allolib_INCLUDE_DIRS}   # or the AlloLib CMake target interface
)
```

Investigate the exact AlloLib CMake target name and include interface before implementing — AlloLib may already export a target that can be linked transitively. Flag to user if the AlloLib CMake structure makes this non-trivial.

### 2.3 OSC port = 0 guard

**Discovery Task B:** Investigate whether `al::ParameterServer` on port 0 behaves as a no-op (no server started) or binds an OS-assigned ephemeral port. The current `start()` implementation unconditionally creates the `ParameterServer` regardless of `mOscPort`. `API.md` Quick Start already documents `oscPort = 0; // disable OSC` as the way to disable it — but the implementation may not honor this.

If AlloLib does not handle port 0 as a no-op: add an explicit guard in `start()`:

```cpp
if (mOscPort > 0) {
    // create and register ParameterServer
}
```

This task must be resolved before Stage 2 is marked complete, since the Qt embedding test should be runnable with `oscPort = 0`.

### 2.4 EngineOptions::elevationMode type fix

Change `EngineOptions::elevationMode` from `int` to `ElevationMode` enum (defined in `RealtimeTypes.hpp`). This is a small breaking API change — do it before the Qt GUI is written so the GUI can use the typed enum directly.

Update `API.md` table for `EngineOptions` accordingly.

### 2.5 API.md V1.1 additions

Update `PUBLIC_DOCS/API.md`:

- Remove runtime setters from the "Out of Scope for V1" list
- Add a "Runtime Parameter Control" section documenting all setter methods with ranges matching the OSC param ranges in `OscParams` (gain: 0.1–3.0, focus: 0.2–5.0, speakerMixDb: ±10, subMixDb: ±10)
- Document the `update()` / `QTimer` integration requirement for Qt hosts
- Document `oscPort = 0` behavior (once Discovery Task B is resolved)
- Document embedding instructions: required include paths, CMake link target, AlloLib include path requirement

---

## Stage 3 — ImGui + GLFW GUI + Python Removal

**Status:** Polish refinements complete (2026-03-31). All high and medium priority backlog items done. Awaiting human re-verification before Python removal (Stage 3.2).

**Verified working:**
- Native NSOpenPanel file dialogs (FileDialog_macOS.mm) for all browse buttons
- SOURCE field accepts any file/directory, rejects incompatible with inline error
- Inline "ADM" / "LUSID" green tag next to SOURCE label
- `mSourceHint` inline diagnostic shown below SOURCE field (green = valid, red = error)
- Device dropdown with Scan button via `al::AudioDevice` enumeration
- Dark background + warm cream card panels (StyleColorsLight base + full palette override)
- Menlo 13.5px monospace font
- Header: miniLogo.png rendered as OpenGL texture (⊙ fallback), app name, workflow breadcrumb (collision-guarded), state badge (right)
- macOS Dock / app-switcher icon set from miniLogo.png via `NSApp setApplicationIconImage`
- ENGINE tab: four bordered card sections (INPUT CONFIGURATION, TRANSPORT, RUNTIME CONTROLS, ENGINE LOG)
- TRANSCODE tab: three bordered card sections (TRANSCODE CONFIGURATION, TRANSCODE CONTROL, TRANSCODE LOG)
- Transcode log rendered from snapshot copy — background thread can append freely during render
- `run.sh` / `run.ps1` launch scripts

**Goal:** Build the C++ Dear ImGui + GLFW GUI as the replacement for the Python PySide6 GUI. After human verification of full feature parity, remove the Python GUI and all Python launch/build infrastructure.

**Completion bar:**

- [x] ImGui GUI implementation complete (all source files written)
- [x] Native file dialogs, device scan, source detection, transport controls — human verified
- [x] Aesthetic theme verified — builds and looks solid against reference prototype
- [x] Polish refinements (section 3.0 backlog — items 1–6 done; 7/8/9 deferred to V1.1)
- [ ] Human confirms full feature parity with Python GUI (re-verify after polish)
- [ ] Python GUI and wrappers removed
- [ ] `spatialroot/` venv removed
- [ ] Zero Python runtime dependency for the core engine

### 3.0 GUI refinement backlog (post-verification)

After the initial aesthetic verification, the following refinements are recommended. Read `gui/imgui/src/App.cpp` and `gui/imgui/src/main.cpp` before attempting any of these — they are all surgical edits, not rewrites.

**High priority — correctness / usability: ✅ DONE (2026-03-31)**

1. ✅ **`mSourceHint` rendered.** `ImGui::TextColored` added below the SOURCE InputText in `renderEngineTab()`. Green for valid ADM/LUSID detections, red for errors/unrecognised paths. Card height raised from 186 → 206px to accommodate the additional line.

2. ✅ **Transcode log mutex fixed.** `renderTranscodeTab()` now copies `mTcLog` into a local `std::deque<LogEntry>` under the lock, releases the mutex, then renders from the snapshot. Background thread can append freely during frame render.

3. ✅ **`(optional)` hint on REMAP CSV fixed.** Removed the overflowing trailing `SameLine + TextDisabled`. Replaced with `ImGui::SetTooltip` on the Browse button — appears on hover, no layout impact.

4. ✅ **Card height calibrated.** INPUT CONFIGURATION raised 186 → 206px for the hint line. Other cards (TRANSPORT 108, RUNTIME CONTROLS 220, TRANSCODE CONFIGURATION 162, TRANSCODE CONTROL 80) computed against style constants (FontSize=13.5, FramePadding.y=4, ItemSpacing.y=6, WindowPadding.y=10, border=2) and confirmed correct.

**Medium priority — polish: ✅ DONE (2026-03-31)**

5. ✅ **Logo image.** `miniLogo.png` loaded via `stb_image` in `App::App()` as an OpenGL texture (`mLogoTexId`). Rendered in the header via `ImGui::Image()` at line height. `⊙` symbol remains as fallback if file not found. Texture released in destructor. stb include path added to `gui/imgui/CMakeLists.txt`. macOS Dock / app-switcher icon also set from the same PNG via `setMacOSAppIcon()` in `FileDialog_macOS.mm`, called from `main.cpp` after `glfwInit()`.

6. ✅ **Header breadcrumb collision fixed.** `ImGui::GetItemRectMax().x` captures the right edge of the last left-side header item. Breadcrumb is skipped (not rendered) if `crumbX ≤ leftEnd + 8px`. State badge always renders via its own `SameLine(absolute)` regardless.

**Deferred to V1.1 (low priority — not blocking feature parity):**

7. **Window title is static.** GLFW title `"Spatial Root — Real-Time Engine"` never changes. Could reflect source filename or engine state. Minor QoL — deferred.

8. **`pickFile` uses deprecated `allowedFileTypes` on macOS 12+.** `#pragma` suppressor already in place. Future fix: `allowedContentTypes` with `UTType`. Low urgency.

9. **OSC toggle.** `oscPort=9009` always-on for V1 (matches Python GUI). Deferred — parity maintained.

10. **Python GUI removal (Stage 3.2).** Do not start until human has verified full feature parity with the Python GUI at `gui/realtimeGUI/`. See section 3.2 below for the removal checklist.

---

### 3.1 Qt GUI — gui/qt/

Create `gui/qt/` as the new Qt application directory. Add to root `CMakeLists.txt` under `SPATIALROOT_BUILD_GUI`.

**Architectural requirements (non-negotiable):**

- Links `EngineSessionCore` as a static library
- Uses the standard `EngineSession` lifecycle (configureEngine → loadScene → applyLayout → configureRuntime → start)
- Runtime parameter control via Stage 2 setter methods only — no OSC dependency for local use
- Drives `update()`, `queryStatus()`, and `consumeDiagnostics()` via a `QTimer` from the main Qt thread
- Does not share or expose `mParamServer` — OSC server is internal to `EngineSession`
- Handles `isExitRequested` from `queryStatus()` to trigger clean shutdown on device loss

**Feature parity target** (match the existing Python PySide6 GUI at `gui/realtimeGUI/`):

- Scene file, layout file, ADM file selection
- Audio device selection
- Master gain, DBAP focus, speaker mix trim, sub mix trim controls
- Auto-compensation toggle
- Elevation mode selection
- Pause / resume transport
- Status display: playback time, CPU load, xrun count, RMS levels
- Diagnostic event display

**ADM workflow in the Qt GUI:**
The engine accepts an `admFile` path in `SceneInput` for direct ADM streaming, but the scene JSON (`scenePath`) must be produced by `cult-transcoder` first — the engine does not call CULT internally. When the user selects an ADM file, the Qt GUI must:
1. Invoke `cult-transcoder transcode <adm.wav>` as a `QProcess` subprocess and wait for completion
2. Pass the resulting LUSID JSON path as `scenePath` and the original ADM file path as `admFile` into `loadScene()`

The `cult-transcoder` binary location should follow from the build output path established in Stage 1. Flag to user if the binary path needs to be configurable at runtime.

**Internal Qt design** (e.g. widget vs QML, layout, visual design) is left to the implementing agent. Human visual similarity verification is the acceptance bar.

**OSC in the Qt app:** The Qt app may pass `oscPort = 9009` (default) to keep OSC running for remote/debug use, or `oscPort = 0` to disable it. This is a runtime decision for the user — the GUI should expose an option or default to enabled. Flag to user for decision.

### 3.2 Python removal (after Qt GUI verification)

Remove in order:

**Python entry points and GUI:**

- `runRealtime.py`
- `realtimeMain.py`
- `gui/realtimeGUI/` (entire directory)
- `src/config/configCPP*.py`, `configCPP_posix.py`, `configCPP_windows.py`

**Python LUSID library and offline pipeline:**

- `LUSID/src/`
- `LUSID/tests/`
- `src/analyzeADM/`
- `src/packageADM/`
- `src/analyzeRender.py`, `src/createRender.py`, `src/createFromLUSID.py`
- `runPipeline.py`

**Python runtime:**

- `requirements.txt` — remove entirely
- `spatialroot/` venv — remove entirely

**After removal:** Verify `init.sh` + `build.sh` still works from clean. Verify `spatialroot_realtime` CLI still works directly.

### 3.3 Remaining open questions for Stage 3

- Should the Qt app expose an OSC enable/disable toggle in the UI, or always start with OSC enabled?
- What is the post-refactor CLI entry point for a user who does not use the Qt GUI? (`build.sh --engine-only` + direct binary invocation, or a `run.sh` convenience wrapper)
- Offline renderer invocation path post-refactor — TBD, deferred unless directly required

---

## What Does NOT Change in This Refactor

| Component                                                                   | Reason                                         |
| --------------------------------------------------------------------------- | ---------------------------------------------- |
| `spatial_engine/realtimeEngine/src/` (all C++ except setter additions)      | Core engine — no changes                       |
| `spatial_engine/src/` (JSONLoader, LayoutLoader, WavUtils, SpatialRenderer) | Shared loaders and offline renderer — retained |
| `cult_transcoder/` source                                                   | Standalone submodule — not modified            |
| `spatial_engine/speaker_layouts/`                                           | Layout JSON files — retained                   |
| `thirdparty/allolib`                                                        | AlloLib dependency — retained                  |
| `thirdparty/libbw64`, `thirdparty/libadm`                                   | EBU submodules — retained                      |
| `processedData/`, `sourceData/`                                             | Test data — retained                           |

---

## Key File Reference

| Purpose                                | File                                                  |
| -------------------------------------- | ----------------------------------------------------- |
| Refactor audit (source of truth)       | `internalDocsMD/cpp_refactor/third_audit.md`          |
| Engine public API header               | `spatial_engine/realtimeEngine/src/EngineSession.hpp` |
| Engine implementation                  | `spatial_engine/realtimeEngine/src/EngineSession.cpp` |
| Runtime config + threading model       | `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` |
| Engine API documentation               | `PUBLIC_DOCS/API.md`                                  |
| Engine CMake                           | `spatial_engine/realtimeEngine/CMakeLists.txt`        |
| API constraint ledger                  | `internalDocsMD/API/api_mismatch_ledger.md`           |
| Current Python GUI (feature reference) | `gui/realtimeGUI/`                                    |
| Python build system (to be replaced)   | `src/config/configCPP*.py`                            |

---

---

# Agent Onboarding Prompt

Copy this prompt into a new context window to hand off implementation.

---

```
You are implementing the C++ refactor for Spatial Root, a spatial audio engine project.

## Your primary source of truth

Read these files first, in order, before doing anything else:

1. internalDocsMD/cpp_refactor/third_audit.md          — architectural audit, resolved decisions
2. internalDocsMD/cpp_refactor/refactor_planning.md    — this dev plan (staged tasks, completion bars)
3. PUBLIC_DOCS/API.md                                  — current public API documentation
4. spatial_engine/realtimeEngine/src/EngineSession.hpp — current public API header
5. spatial_engine/realtimeEngine/src/EngineSession.cpp — current implementation
6. spatial_engine/realtimeEngine/src/RealtimeTypes.hpp — threading model and config types
7. internalDocsMD/API/api_mismatch_ledger.md           — documented hard constraints on the API
8. spatial_engine/realtimeEngine/src/main.cpp          — reference for lifecycle and polling loop usage

Do not start work until you have read all eight files.

## Architecture summary

Spatial Root is a C++ spatial audio engine. The refactor goal is to:
- Replace the Python build system with init.sh/init.ps1 + build.sh/build.ps1 + a root CMakeLists.txt (macOS/Linux and Windows both supported) — **DONE (Stage 1)**
- Harden EngineSessionCore for direct embedding (setter methods, oscPort=0 guard, type fix, install targets)
- Add runtime setter methods to EngineSession so a GUI host can control live parameters without OSC
- Build a C++ **Dear ImGui + GLFW** desktop GUI that embeds EngineSessionCore directly (same process, direct API calls)
- Remove all Python GUI, launch wrappers, and build infrastructure after ImGui GUI reaches parity

**GUI framework decision:** Dear ImGui + GLFW (NOT Qt). Qt cannot be used as a git submodule. All dependencies must be open-source submodules. Dear ImGui (MIT, ~5MB) + GLFW (zlib, ~1MB) replace Qt. The Stage 3 GUI is an ImGui application, not a Qt application. All architectural requirements remain the same.

The chosen architecture: the ImGui GUI links EngineSessionCore, calls the standard staged lifecycle, and uses direct C++ setter methods for runtime parameter control. OSC is demoted to an optional secondary surface.

CULT (cult_transcoder submodule) is NOT to be modified in this refactor.

## Staged work

The refactor has three stages. Complete one stage, then STOP and ask for human review before proceeding to the next. Do not combine stages.

Stage 1 — Build infrastructure and docs
Stage 2 — EngineSessionCore hardening for Qt embedding
Stage 3 — Qt GUI + Python removal

Full task details for each stage are in refactor_planning.md.

## Stage completion bars

- Stage 1 done when: init.sh + build.sh runs from clean, produces all binaries, no Python toolchain required. README.md reflects actual entry points. **— COMPLETE**
- Stage 2 done when: a minimal C++ embedding test compiles and links EngineSessionCore, calls the full lifecycle, sets runtime parameters via the new setter methods, reads queryStatus(), with oscPort = 0 (no OSC dependency).
- Stage 3 done when: Dear ImGui + GLFW GUI launches, plays a scene, all parameters are live-controllable. Human verifies full feature parity with the Python GUI. Python GUI and all Python infrastructure removed.

## Git workflow

All work targets the `cpp` branch. Do not create new branches. Do not make any git commits — the human will review all changes and commit manually after verifying each stage. Do not run git add, git commit, or git push at any point.

## How to work

- Ask for clarification frequently. When you hit an ambiguity that could affect architecture or file structure, stop and ask before proceeding. Do not resolve architectural ambiguities independently.
- Before modifying any file, read it first.
- Do not modify CULT source (cult_transcoder/).
- Do not modify the audio engine's core DSP logic (Streaming, Pose, Spatializer, RealtimeBackend, OutputRemap). The only engine-side changes in Stage 2 are adding setter methods to EngineSession.hpp/.cpp and fixing the oscPort guard and elevationMode type.
- Prefer editing existing files to creating new ones.
- When you complete a task within a stage, say so clearly so progress is trackable.
- At the end of each stage, summarize what was done and what the human needs to verify before you proceed.

## Progress documentation (required)

Maintain two running documents as you work. Do not skip this — it is how the next context window picks up without re-auditing the repo.

**internalDocsMD/cpp_refactor/refactor_log.md** — granular change log. After every discrete action (file created, file deleted, CMake target added, method implemented, doc updated), append an entry. Format:

```

### [Stage N — Task description]

**Date:** MM-DD
**Files changed:** list each file
**What was done:** one or two sentences
**Notes:** anything surprising, a decision made, or a constraint discovered

```

**internalDocsMD/cpp_refactor/refactor_planning.md** — high-level stage tracking. When a stage or major task is complete, update the relevant section to mark it done and note any decisions or deviations from the original plan. Do not rewrite the plan — append a short status block under the relevant stage heading, e.g.:

```

**Status:** Complete — [date]. [One sentence on outcome or any deviation.]

```

Both files should be updated before asking for human stage-gate review. The human will read them as part of the review.

## Discovery tasks (resolved)

Discovery Task A (GUI framework): Qt cannot be used as a git submodule (multi-GB source, complex bootstrap). All project dependencies must be open-source git submodules. **Decision: Dear ImGui (MIT, ~5MB) + GLFW (zlib, ~1MB).** Stage 3 GUI is an ImGui application, not a Qt application.

Discovery Task B (oscPort=0 guard): **Resolved and implemented.** `al::ParameterServer` on port 0 binds an OS-assigned ephemeral port (UDP bind with port=0 succeeds on macOS). It does NOT act as a no-op. Guard added in `EngineSession::start()`: `if (mOscPort > 0)` wraps the entire ParameterServer + OscParams block. `shutdown()` was already guarded with `if (mParamServer)` — no change needed there.

## Key naming conventions

- Runtime setter names follow RuntimeParams field names: setMasterGain, setDbapFocus, setSpeakerMixDb, setSubMixDb, setAutoCompensation, setElevationMode
- queryStatus() — not status()
- dbapFocus — not focus
- These are existing code names. Do not rename existing methods or fields.
- The new setter methods are V1.1 additions to the API. Update the "Out of Scope for V1" section of API.md when adding them.

## Qt GUI integration requirements (Stage 3)

- Qt host must drive update(), queryStatus(), and consumeDiagnostics() via a QTimer from the main Qt thread (e.g. 50ms interval)
- Runtime setters must only be called after successful start()
- The standard staged lifecycle is non-negotiable: configureEngine → loadScene → applyLayout → configureRuntime → start
- shutdown() is terminal — to restart, construct a new EngineSession
- The Qt GUI lives at gui/qt/ and is added to the root CMakeLists.txt under the SPATIALROOT_BUILD_GUI option flag
- Feature parity target is the existing Python PySide6 GUI at gui/realtimeGUI/ — read that directory to understand the required feature set before designing the Qt UI
- Read main.cpp before writing the Qt GUI — it is the canonical reference for how update(), queryStatus(), consumeDiagnostics(), and the isExitRequested polling loop are used together in practice
- Shutdown must be triggered explicitly: connect QMainWindow::closeEvent (or equivalent) to call session.shutdown(). The internal shutdown order (OSC server → audio backend → streaming) is handled by EngineSession::shutdown() — the Qt host just needs to call it before the process exits. Failing to call shutdown() before exit causes deadlock on macOS CoreAudio.

## Stage 2 implementation notes

These details are easy to miss when implementing the runtime setter methods. Read EngineSession.cpp lines 183–217 (the OSC callback block inside start()) — the setter implementations must mirror those callbacks exactly.

Key implementation constraints:

- setDbapFocus() and setAutoCompensation(): both must set mPendingAutoComp = true (via mPendingAutoComp.store(true, std::memory_order_relaxed)) when focusAutoCompensation is enabled. update() handles the recomputation from the main thread. Do not call computeFocusCompensation() directly from a setter — it is main-thread-only and must not be called while audio is streaming (see RealtimeTypes.hpp invariant 5).

- setSpeakerMixDb() and setSubMixDb(): store the linear value, not the dB value. Convert before storing: powf(10.f, dB / 20.f). The same conversion is done in configureRuntime() and in the OSC spkMixDb/subMixDb callbacks — be consistent.

- All stores use std::memory_order_relaxed. Do not use stronger ordering — the existing threading model is designed around relaxed loads/stores for these parameters (a one-buffer lag is inaudible and is not a data race).

## Current state when this prompt was last updated (2026-03-30)

**Stages 1 and 2 are complete and verified.**

**Stage 3 GUI is built, verified, and working.** The user has confirmed the GUI launches, file dialogs work, device scan works, ADM/LUSID detection shows inline, transport controls work, and the aesthetic (dark/cream theme, Menlo font, card layout) looks solid. The GUI is ready for polish and refinement.

**What is done in Stage 3:**
- `gui/imgui/CMakeLists.txt` — full CMake target; `.mm` compiled via `target_sources` on Apple; AppKit linked
- `gui/imgui/src/main.cpp` — GLFW + OpenGL 3.3 Core + Dear ImGui render loop; Menlo 13.5px font; `StyleColorsLight` base + full dark/cream palette override; `--root` CLI arg
- `gui/imgui/src/App.hpp` / `App.cpp` — full application; ENGINE + TRANSCODE tabs; four card sections in ENGINE tab (INPUT CONFIGURATION, TRANSPORT, RUNTIME CONTROLS, ENGINE LOG); three card sections in TRANSCODE tab; NSOpenPanel Browse buttons on all path fields; device Scan+Combo via `al::AudioDevice`; complete 5-stage engine lifecycle; `SubprocessRunner` for cult-transcoder; thread-safe transcode log
- `gui/imgui/src/FileDialog.hpp` + `FileDialog_macOS.mm` + `FileDialog.cpp` — NSOpenPanel on macOS; `GetOpenFileName` on Windows; zenity on Linux
- `gui/imgui/src/SubprocessRunner.hpp` / `.cpp` — background subprocess with streamed output
- `run.sh` / `run.ps1` — launch scripts
- Root `CMakeLists.txt`, `init.sh`, `build.sh`, `README.md` — all updated to reference `gui/imgui/`

**What is NOT yet done in Stage 3:**
- GUI polish and refinement — see section 3.0 for prioritised backlog
- Logo image render (`gui/imgui/src/miniLogo.png` exists; `⊙` is the current placeholder)
- `mSourceHint` is populated by `detectSource()` but never rendered — users get no inline feedback on invalid sources
- Stage 3.2: Python GUI removal — only after the user confirms full feature parity

**You are continuing Stage 3 with polish and refinement.** Read section 3.0 for the prioritised backlog. Do not begin Stage 3.2 (Python removal) without explicit user confirmation.

## What you should do right now

1. Read `refactor_log.md` — read the most recent entries first (top of the log). Note: the App.cpp and main.cpp scaffold entries have **SUPERSEDED** notes; the current state is described in the "Stage 3 — Native file dialogs" and "Stage 3 — Aesthetic overhaul" entries.
2. Read `gui/imgui/src/App.cpp` and `gui/imgui/src/main.cpp` to understand the current implementation in full before making any changes.
3. Ask the user which refinement from section 3.0 to tackle first, or whether they have a specific improvement in mind.
4. Work through section 3.0 in priority order unless directed otherwise: `mSourceHint` display → transcode log mutex → `(optional)` hint position → card height calibration → logo image render.
