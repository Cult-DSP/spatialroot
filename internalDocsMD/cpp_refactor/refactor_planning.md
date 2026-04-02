# Spatial Root — C++ Refactor Dev Plan

**Date:** 2026-03-29
**Source of truth:** `internalDocsMD/cpp_refactor/third_audit.md`
**Scope:** Full refactor: build infrastructure, API hardening, Qt GUI replacement

---

## Onboarding — Read This First (Context for New Agent)

**Last updated: 2026-03-31.** Stages 1 and 2 are complete. Stage 3 GUI is built and verified working. Polish items are mostly done. The next human-gated action is feature parity verification, then Python removal. Three issues are open and must be resolved before that gate.

### Where things stand right now

**What is built and working:**
- `init.sh` / `build.sh` replace the Python build system entirely.
- `spatialroot_realtime` C++ binary is the primary engine CLI.
- `EngineSessionCore` static library exposes the full V1.1 API (runtime setters, OSC port=0 guard, typed ElevationMode).
- `spatialroot_gui` — Dear ImGui + GLFW desktop GUI — builds, launches, and is visually verified against the Python prototype. It links `EngineSessionCore` directly (no subprocess, no OSC dependency for local control).

**Files that matter for the GUI:**

| File | Role |
|---|---|
| `gui/imgui/src/App.hpp` | App class declaration — all state members |
| `gui/imgui/src/App.cpp` | All UI rendering and engine lifecycle logic — read this first |
| `gui/imgui/src/main.cpp` | GLFW setup, ImGui init, render loop |
| `gui/imgui/src/FileDialog_macOS.mm` | NSOpenPanel file pickers + macOS Dock icon setter |
| `gui/imgui/CMakeLists.txt` | GUI build — includes xxd embed step for miniLogo.png |
| `spatial_engine/realtimeEngine/src/EngineSession.hpp` | Engine public API — read before touching lifecycle code |

### Open issues status

**Issue A — Engine restart ✅ FIXED (2026-03-31)**

`mSession` changed to `std::unique_ptr<EngineSession>` in `App.hpp`. Initialized via `std::make_unique<EngineSession>()` in the `App` constructor. `doLaunchEngine()` resets it with `mSession = std::make_unique<EngineSession>()` before every launch. All call sites updated to `mSession->`.

---

**Issue B — Executable name ✅ FIXED (2026-03-31)**

`set_target_properties(spatialroot_gui PROPERTIES OUTPUT_NAME "Spatial Root")` added to `gui/imgui/CMakeLists.txt`. `run.sh` and `run.ps1` updated to reference the new binary name (quoted). Binary confirmed named `Spatial Root` at link step.

---

**Issue C — macOS Dock icon ⏭ DEFERRED to Phase 5**

The in-app Dock tile icon via `[NSApp setApplicationIconImage:]` works while running. The desktop/Finder file icon does not update — this requires packaging as a `.app` bundle. Deferred as a Phase 5 item. See Phase 5 future work below for investigation notes.

---

### What NOT to do

- Do not start Python removal (Stage 3.2 / Stage 4.2) until the human explicitly confirms feature parity.
- Do not modify `spatial_engine/realtimeEngine/src/` engine source — all changes are GUI-side only.
- Do not rebuild the GUI from scratch — all changes are surgical edits to `App.cpp`, `App.hpp`, `main.cpp`, `FileDialog_macOS.mm`, and `CMakeLists.txt`.

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
- [x] Polish refinements (section 3.0 backlog — items 1–4, 6 done; item 5 partial — in-app logo done, dock icon unresolved; 7/8/9 deferred to V1.1)
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

5. ⚠️ **Logo image — partially done, macOS dock icon unresolved.**

   **In-app header logo (App.cpp):** `miniLogo.png` embedded as a C byte array at build time via `xxd -i` (CMake custom command → `miniLogo_data.h` in the build directory — included only in `App.cpp` to avoid duplicate symbols; `main.cpp` uses `extern` declarations). `App::App()` uses `stbi_load_from_memory(miniLogo_png, miniLogo_png_len, ...)` and uploads as an OpenGL texture (`mLogoTexId`). Rendered via `ImGui::Image()` at text line height. `⊙` fallback if texture upload fails. Texture released in destructor. **Status: believed correct but not visually confirmed.**

   **macOS Dock / app-switcher icon:** `setMacOSAppIconFromData(data, len)` added to `FileDialog_macOS.mm` — feeds embedded PNG bytes into `NSData → NSImage → [NSApp setApplicationIconImage:]`, called from `main.cpp` after `glfwInit()`. **Status: icon still does not appear on the executable on the desktop. Root cause unknown.** Possible angles to investigate next time:
   - GLFW on macOS may be overriding the NSApplication delegate or resetting the icon after `glfwInit()` returns — try calling after `glfwCreateWindow()` instead.
   - The app is not packaged as a `.app` bundle — macOS may require a bundle + `Info.plist` + `.icns` for a persistent Finder/desktop icon.
   - `[NSApp setApplicationIconImage:]` affects the Dock tile while running but not the file icon in Finder — a `.app` bundle with `MACOSX_BUNDLE` in CMake and a proper `.icns` is needed for the latter.
   - `miniLogo.png` may need to be converted to `.icns` (multi-resolution icon format) for best results.

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

## Phase 5 — Future Work (not blocking parity or Python removal)

| Item | Notes |
|---|---|
| **macOS `.app` bundle** | Add `MACOSX_BUNDLE` to `gui/imgui/CMakeLists.txt`, provide `Info.plist`, convert `miniLogo.png` to `.icns`. Required for a persistent Finder/desktop file icon. `[NSApp setApplicationIconImage:]` already sets the Dock tile while running — the bundle is only needed for the static file icon. Try calling `setMacOSAppIconFromData` after `glfwCreateWindow()` first (GLFW may reset the icon during window creation). |
| **Dynamic GLFW window title** | Currently static `"Spatial Root — Real-Time Engine"`. Could reflect source filename or engine state. Minor QoL. |
| **`allowedFileTypes` deprecation** | `pickFile` in `FileDialog_macOS.mm` uses deprecated `allowedFileTypes` (macOS 12+). `#pragma` suppressor in place. Future fix: `allowedContentTypes` with `UTType`. |
| **OSC enable/disable toggle** | `oscPort=9009` always-on for V1. A UI toggle deferred — parity with Python GUI maintained. |

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

---

## Stage 4 — Pre-Deletion Documentation, Python Removal, and Docs Update

**Status:** Not started. Gate: human confirms Stage 3 feature parity before 4.2 or 4.3 begin. Task 4.1 (Python documentation) may begin without that gate.

These three tasks are intended to run in new context windows, one at a time, each with a human verification step before the next begins. Onboarding prompts for each task are at the bottom of this file.

| Task | Name | Gate |
|---|---|---|
| 4.1 | Document useful Python information to `usefulPyInfo.md` | Human reviews the doc before Python deletion begins |
| 4.2 | Delete all Python source, GUI, and venv | Human confirms feature parity + 4.1 doc is approved |
| 4.3 | Update `AGENTS.md`, `devHistory.md`, `realtime_master.md` | Human reviews after 4.2 is complete |

### 4.1 — Document useful Python information

**Output file:** `internalDocsMD/cpp_refactor/usefulPyInfo.md`

Read every Python file listed below and extract information that is **not already captured in the C++ source, CMake, or existing docs**, and that could be useful for future development, debugging, or reference. This is a read-and-document task — do not modify any Python files.

**Files to read (project Python only — exclude `spatialroot/` venv):**

- `realtimeMain.py` — CLI entry point: argument names, defaults, help text
- `runPipeline.py` — offline pipeline entry point and source-type detection logic
- `gui/realtimeGUI/` — entire Python GUI directory (all `.py` files): UX flows, parameter labels, default values, any logic not replicated in the C++ GUI
- `gui/pipeline_runner.py`, `gui/background.py` — subprocess patterns
- `gui/widgets/` — all widget files: any UI conventions worth preserving
- `src/analyzeADM/analyzeMetadata.py`, `src/analyzeADM/checkAudioChannels.py` — ADM parsing details
- `src/analyzeRender.py`, `src/packageADM/splitStems.py` — offline pipeline logic
- `src/config/configCPP.py` (and any `configCPP_posix.py`, `configCPP_windows.py`) — build flags, environment setup
- `utils/deleteData.py`, `utils/getExamples.py` — utility scripts

**What to capture in `usefulPyInfo.md`:**

- OSC parameter names and ranges (anything not already in `PUBLIC_DOCS/API.md`)
- Default parameter values that differ from engine defaults
- Source type detection logic (ADM vs LUSID heuristics, file/directory checks)
- ADM workflow details (preprocessing steps, intermediate file paths, CLI invocations)
- Any GUI UX behaviour that may not be replicated in the ImGui GUI (e.g. error states, validation, confirmation dialogs)
- Build flags or environment variables from `configCPP*.py` that are not in `build.sh`
- Any known workarounds, edge cases, or `# TODO` / `# FIXME` comments
- Any CLI flags or argument patterns not documented elsewhere

**What NOT to capture:**

- Code that is already superseded by the C++ engine or cult-transcoder
- Third-party or venv code
- PySide6 styling/theming that has no C++ equivalent

**After completing the doc:** stop and ask the human to review `usefulPyInfo.md` before proceeding.

### 4.2 — Delete all Python source, GUI, and venv

**Gate:** Human confirms Stage 3 feature parity AND has approved the `usefulPyInfo.md` doc from task 4.1.

This task is mechanical deletion. A weaker model is appropriate. Read the deletion list carefully — do not delete anything not on this list.

**Delete in order:**

1. Python entry points:
   - `realtimeMain.py`
   - `runPipeline.py`
   - `runRealtime.py` (if it exists)

2. Python GUI:
   - `gui/realtimeGUI/` (entire directory)
   - `gui/pipeline_runner.py`
   - `gui/background.py`
   - `gui/utils/` (entire directory if only Python files remain)
   - `gui/widgets/` (entire directory if only Python files remain)

3. Python pipeline and analysis:
   - `src/analyzeADM/` (entire directory)
   - `src/packageADM/` (entire directory)
   - `src/analyzeRender.py`
   - `src/createRender.py` (if it exists)
   - `src/createFromLUSID.py` (if it exists)

4. Python build system:
   - `src/config/configCPP.py`
   - `src/config/configCPP_posix.py` (if it exists)
   - `src/config/configCPP_windows.py` (if it exists)
   - `src/config/` (entire directory if empty after deletion)

5. Python LUSID library:
   - `LUSID/src/` (entire directory)
   - `LUSID/tests/` (entire directory)

6. Utilities:
   - `utils/deleteData.py`
   - `utils/getExamples.py`
   - `utils/` (entire directory if empty after deletion)

7. Python runtime:
   - `requirements.txt`
   - `spatialroot/` venv (entire directory)

8. After deletion: check if any `__init__.py` files or empty `__pycache__` directories remain and remove them.

**Verify after deletion:** `init.sh && build.sh --engine-only` completes without error. `./run.sh` launches the GUI. No Python runtime is required for either.

**After completing deletion:** stop and ask the human to verify before proceeding to 4.3.

### 4.3 — Update AGENTS.md, devHistory.md, and realtime_master.md

**Gate:** Task 4.2 complete (Python removed) and human has confirmed.

Update three docs to reflect the completed C++ refactor. Read each file in full before editing. These are internal agent docs — write for a future agent or developer, not for end users. Be accurate and concise; do not pad with filler.

---

**`internalDocsMD/AGENTS.md`**

This file describes the project architecture and toolchain for agents. It currently describes a Python-centric workflow (PySide6 GUI, `runRealtime.py`, `runPipeline.py`, `configCPP*.py`). Update it to reflect the current state:

- Replace all references to `runRealtime.py` and `runPipeline.py` as entry points with `spatialroot_realtime` (CLI binary)
- Replace all references to `configCPP*.py` / Python build system with `init.sh` + `build.sh`
- Replace all references to the PySide6 GUI (`gui/realtimeGUI/`) with the ImGui + GLFW GUI (`gui/imgui/`)
- Update the architecture section: the GUI now embeds `EngineSessionCore` directly (no subprocess, no OSC dependency for local control). Runtime parameters are set via direct C++ setter methods (`setMasterGain`, `setDbapFocus`, `setSpeakerMixDb`, `setSubMixDb`, `setAutoCompensation`, `setElevationMode`).
- Update the runtime control plane section: OSC is now a secondary/optional surface (port 9009 by default, disabled with `oscPort=0`). The primary control surface is the direct C++ API.
- Remove any "§Python Virtual Environment" section or mark it deleted.
- Add a Phase 6 banner at the top noting the C++ refactor completion date (2026-03-31) and linking to `internalDocsMD/cpp_refactor/refactor_planning.md`.
- Do not rewrite sections that remain accurate (e.g. cult-transcoder integration, LUSID format, engine agent phases 1–8).

---

**`internalDocsMD/devHistory.md`**

Add a new milestone section for the C++ refactor. This file uses a milestone + description format. Add:

- **Phase 4: C++ Refactor and ImGui GUI** (date: 2026-03-31)
- Cover: Python build system replaced by `init.sh`/`build.sh`; `EngineSessionCore` static library hardened with V1.1 API (runtime setters, typed `ElevationMode`, `oscPort=0` guard); Python PySide6 GUI replaced by Dear ImGui + GLFW GUI linking `EngineSessionCore` directly; Python GUI, entry points, LUSID library, and venv removed.
- Note: `spatialroot_realtime` is now the primary CLI; `EngineSession::shutdown()` is terminal (construct a new instance to restart).

---

**`internalDocsMD/Realtime_Engine/agentDocsv1/realtime_master.md`**

This file describes the original engine agent architecture. Much of it remains accurate (AlloLib backend, DBAP spatializer, streaming, threading model). Update only the sections that are now stale:

- **GUI section** (currently "PySide6, Keep PySide6"): mark as superseded. The GUI is now Dear ImGui + GLFW, embedding `EngineSessionCore` directly in-process. No `QProcess`. No subprocess.
- **Runtime Control Plane section** (currently "al::Parameter + ParameterServer (OSC)"): update to note that OSC is now a secondary surface. Primary control is direct C++ setter methods on `EngineSession`. The parameter names and ranges are unchanged and OSC still works on port 9009.
- **Python Entry Point section** (currently describes `runRealtime.py`): mark as removed. `spatialroot_realtime` CLI binary is the direct entry point. The GUI launches it via `EngineSession` in-process, not as a subprocess.
- **Next Major Task section** (currently "Pipeline Refactor (C++-first realtime)"): mark as complete. The refactor described there is done.
- Add a note at the top of the file: "Updated 2026-03-31 — C++ refactor complete. See `internalDocsMD/cpp_refactor/refactor_planning.md` for full history."

**After completing all three doc updates:** stop and ask the human to review before committing.

---

## Onboarding Prompts — Stage 4 Tasks

Each prompt below is self-contained. Copy the relevant one into a new context window for the task.

---

### Onboarding Prompt — Task 4.1: Document Useful Python Information

```
You are documenting useful information from the Python source files in the Spatial Root project before they are deleted in an upcoming step.

## Context

Spatial Root has completed a full C++ refactor (Stages 1–3). The Python GUI, entry points, pipeline scripts, and build system have been replaced by:
- `spatialroot_realtime` C++ binary (primary CLI)
- `EngineSessionCore` static library with a direct C++ API
- Dear ImGui + GLFW GUI (`gui/imgui/`) embedding `EngineSessionCore` directly
- `init.sh` / `build.sh` replacing `configCPP*.py`
- `cult-transcoder` handling all ADM transcoding

The Python files will be deleted in the next task. Before deletion, extract any information that is not already captured in the C++ source, CMake files, or existing docs, and that could be useful for future development.

## Your task

Create `internalDocsMD/cpp_refactor/usefulPyInfo.md` by reading and extracting from the Python files below.

## Files to read (exclude `spatialroot/` venv directory)

- `realtimeMain.py`
- `runPipeline.py`
- `runRealtime.py` (if it exists)
- `gui/realtimeGUI/` (all .py files in this directory and subdirectories)
- `gui/pipeline_runner.py`, `gui/background.py`
- `gui/widgets/` (all .py files)
- `src/analyzeADM/analyzeMetadata.py`, `src/analyzeADM/checkAudioChannels.py`
- `src/analyzeRender.py`, `src/packageADM/splitStems.py`
- `src/config/configCPP.py` (and any configCPP_posix.py, configCPP_windows.py)
- `utils/deleteData.py`, `utils/getExamples.py`

## What to extract

- OSC parameter names and value ranges not already in `PUBLIC_DOCS/API.md`
- Default parameter values that differ from the C++ engine defaults in `RealtimeTypes.hpp`
- Source type detection logic (ADM vs LUSID heuristics)
- Any GUI UX behaviour not replicated in the ImGui GUI (error states, validation flows, edge cases)
- Build flags or environment variables from configCPP scripts not captured in `build.sh`
- Known workarounds, edge cases, `# TODO` / `# FIXME` comments that may still be relevant
- CLI argument names and defaults not documented in `PUBLIC_DOCS/API.md`

## What NOT to extract

- Code already superseded by C++ or cult-transcoder
- Third-party or venv code
- PySide6 styling that has no C++ analogue

## Source of truth files (read before starting)

- `spatial_engine/realtimeEngine/src/EngineSession.hpp` — V1.1 API (runtime setters, lifecycle)
- `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` — parameter defaults
- `PUBLIC_DOCS/API.md` — current public documentation
- `internalDocsMD/cpp_refactor/refactor_planning.md` — full refactor history

## Rules

- Read each Python file before extracting from it.
- Do NOT modify any Python files.
- Do NOT delete anything.
- When done, stop and ask the human to review `internalDocsMD/cpp_refactor/usefulPyInfo.md` before any Python deletion proceeds.
```

---

### Onboarding Prompt — Task 4.2: Delete All Python

```
You are removing all Python source, GUI, and runtime files from the Spatial Root project. This is a mechanical deletion task.

## Context

Spatial Root has completed a full C++ refactor. The Python files listed below have been superseded and are safe to delete:
- The Python PySide6 GUI is replaced by `gui/imgui/` (Dear ImGui + GLFW)
- `runRealtime.py` / `realtimeMain.py` are replaced by `spatialroot_realtime` binary
- `configCPP*.py` is replaced by `init.sh` / `build.sh`
- The Python LUSID library is replaced by the C++ engine reading LUSID JSON directly
- Useful information from these files has already been extracted to `internalDocsMD/cpp_refactor/usefulPyInfo.md`

## Prerequisite check

Before deleting anything, verify:
1. `internalDocsMD/cpp_refactor/usefulPyInfo.md` exists (Task 4.1 output)
2. `build/gui/imgui/Spatial Root` binary exists (Stage 3 built and working)

If either check fails, stop and ask the human before proceeding.

## Deletion list (delete only these — nothing else)

1. `realtimeMain.py`
2. `runPipeline.py`
3. `runRealtime.py` (if it exists — skip if not found)
4. `gui/realtimeGUI/` (entire directory)
5. `gui/pipeline_runner.py`
6. `gui/background.py`
7. `gui/utils/` (entire directory — verify only Python files remain first)
8. `gui/widgets/` (entire directory — verify only Python files remain first)
9. `src/analyzeADM/` (entire directory)
10. `src/packageADM/` (entire directory)
11. `src/analyzeRender.py`
12. `src/createRender.py` (if it exists)
13. `src/createFromLUSID.py` (if it exists)
14. `src/config/configCPP.py`
15. `src/config/configCPP_posix.py` (if it exists)
16. `src/config/configCPP_windows.py` (if it exists)
17. `src/config/` (entire directory only if empty after step 16)
18. `LUSID/src/` (entire directory)
19. `LUSID/tests/` (entire directory)
20. `utils/deleteData.py`
21. `utils/getExamples.py`
22. `utils/` (entire directory only if empty after step 21)
23. `requirements.txt`
24. `spatialroot/` (entire venv directory)
25. Any orphaned `__pycache__/` directories within the above paths

## After deletion

Run: `./init.sh` — must complete without error.
Run: `./run.sh` — GUI must launch.

If either fails, stop and report the error before proceeding.

When done, stop and ask the human to verify the deletion before proceeding to Task 4.3.
```

---

### Onboarding Prompt — Task 4.3: Update Internal Docs

```
You are updating three internal agent documentation files to reflect the completed C++ refactor of Spatial Root. This is a documentation-only task — do not modify any source code or build files.

## Context

Spatial Root has completed a full C++ refactor (2026-03-31). The Python GUI, entry points, LUSID library, and venv have been removed. The project now runs entirely on C++:
- `spatialroot_realtime` — primary CLI binary
- `EngineSessionCore` — static library, V1.1 API with direct runtime setter methods
- `gui/imgui/` — Dear ImGui + GLFW GUI, embeds EngineSessionCore in-process
- `init.sh` / `build.sh` — replaces Python build system
- `cult-transcoder` — standalone transcoding tool (unchanged)

## Source of truth (read these first)

1. `internalDocsMD/cpp_refactor/refactor_planning.md` — full refactor history and decisions
2. `spatial_engine/realtimeEngine/src/EngineSession.hpp` — V1.1 public API
3. `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` — types and config
4. `gui/imgui/src/App.cpp` — canonical GUI implementation reference
5. `PUBLIC_DOCS/API.md` — public API documentation

## Files to update

### 1. `internalDocsMD/AGENTS.md`

Read the full file before editing. Update:
- Add a Phase 6 banner at the top: "Phase 6 (2026-03-31): C++ refactor complete — see `internalDocsMD/cpp_refactor/refactor_planning.md`."
- All references to `runRealtime.py` / `realtimeMain.py` as entry points → `spatialroot_realtime` binary
- All references to `configCPP*.py` / Python build → `init.sh` + `build.sh`
- All references to the PySide6 GUI → ImGui + GLFW GUI (`gui/imgui/`)
- Architecture section: GUI now embeds EngineSessionCore in-process; runtime parameters use direct C++ setter methods, not OSC
- Runtime control plane: OSC is secondary (port 9009 default, disabled with oscPort=0); primary surface is the direct C++ API
- Python Virtual Environment section: mark as removed
- Do not rewrite sections that remain accurate (cult-transcoder, LUSID format, engine agent phases 1–8)

### 2. `internalDocsMD/devHistory.md`

Read the full file before editing. Append a new milestone section:

**Phase 4: C++ Refactor and ImGui GUI (2026-03-31)**
Cover: Python build system replaced by init.sh/build.sh; EngineSessionCore V1.1 API (runtime setters, typed ElevationMode, oscPort=0 guard); PySide6 GUI replaced by Dear ImGui + GLFW GUI linking EngineSessionCore directly (no subprocess); Python GUI, entry points, LUSID library, and venv removed.
Key fact: `EngineSession::shutdown()` is terminal — construct a new instance to restart (now handled by the GUI via `std::unique_ptr<EngineSession>`).

### 3. `internalDocsMD/Realtime_Engine/agentDocsv1/realtime_master.md`

Read the full file before editing. Update only stale sections — do not rewrite accurate sections:
- Add a note at the top: "Updated 2026-03-31 — C++ refactor complete. See `internalDocsMD/cpp_refactor/refactor_planning.md`."
- GUI section (currently "Keep PySide6"): mark as superseded. GUI is now Dear ImGui + GLFW, in-process, no QProcess.
- Runtime Control Plane section: OSC is now secondary. Primary control is direct C++ setter methods on EngineSession. Parameter names and ranges are unchanged.
- Python Entry Point section: mark `runRealtime.py` as removed. `spatialroot_realtime` is the CLI. GUI uses in-process EngineSession.
- "Next Major Task" section: mark the described pipeline refactor as complete (2026-03-31).

## Rules

- Read each file in full before editing.
- Do not modify source code or build files.
- Do not rewrite sections that are still accurate.
- When done, stop and ask the human to review all three files before any commits.
```
