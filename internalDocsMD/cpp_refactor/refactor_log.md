# Spatial Root — C++ Refactor Log

Granular change log for the C++ refactor. Append an entry after every discrete action.
See `refactor_planning.md` for stage-level status updates.

---

## Entry format

```
### [Stage N — Task description]
**Date:** MM-DD
**Files changed:** list each file
**What was done:** one or two sentences
**Notes:** anything surprising, a decision made, or a constraint discovered
```

---

## Log

<!-- Agent: append entries below this line. Do not edit above it. -->

### [Stage 3 — Native file dialogs (NSOpenPanel), Browse buttons, source hint, device dropdown]

**Date:** 03-30
**Files changed:** `gui/imgui/src/FileDialog.hpp`, `gui/imgui/src/FileDialog.cpp`, `gui/imgui/src/FileDialog_macOS.mm` (created), `gui/imgui/src/App.hpp`, `gui/imgui/src/App.cpp`, `gui/imgui/CMakeLists.txt`, `run.sh` (created), `run.ps1` (created)
**What was done:** Replaced the broken `osascript + popen` file dialog approach with a proper `NSOpenPanel` Objective-C++ implementation (`FileDialog_macOS.mm`). Added Browse buttons to SOURCE (any file/folder via `pickFileOrDirectory`), LAYOUT (.json filter), REMAP CSV (.csv filter), and TRANSCODE INPUT (.wav/.xml filter). Replaced the overlapping source hint with an inline "ADM" / "LUSID" green tag next to the SOURCE label. Replaced the device InputText with a Scan button + combo populated via `al::AudioDevice` enumeration. Added `run.sh` / `run.ps1` launch scripts.
**Notes:** `osascript` blocked the main thread and the dialog appeared behind the GLFW window. `NSOpenPanel` (`FileDialog_macOS.mm`) runs its own Cocoa modal event loop and integrates correctly since GLFW already initialises `NSApplication`. `FileDialog.cpp` now handles Windows (`GetOpenFileName`) and Linux (zenity) only; macOS is conditional via `target_sources` in CMakeLists. `al::AudioDevice::numDevices()` initialises PortAudio on first call — safe because PortAudio is reference-counted; engine start will re-init without conflict. Human verified: GUI launches, file dialogs work, device scan works, ADM/LUSID detection shows inline.

### [Stage 3 — init.sh / build.sh: imgui + glfw submodule init and --gui flag]

**Date:** 03-30
**Files changed:** `init.sh`, `build.sh`
**What was done:** Added Step 4 (Dear ImGui submodule init via `thirdparty/imgui`) and Step 5 (GLFW submodule init via `thirdparty/glfw`) to `init.sh`, each guarded by `.gitmodules` grep checks. Added `--gui` flag to `build.sh` argument parser that sets `BUILD_GUI=ON`, updated header comment to document `--gui`, and added `build/gui/imgui/spatialroot_gui` to the build summary output.
**Notes:** Submodule guards use `grep -q "thirdparty/imgui"` in `.gitmodules` so the steps are silently skipped if the submodules have not been registered yet (graceful for users who haven't run `git submodule add`).

---

### [Stage 3 — gui/imgui/CMakeLists.txt: ImGui + GLFW GUI CMake target]

**Date:** 03-30
**Files changed:** `gui/imgui/CMakeLists.txt` (created)
**What was done:** Created CMakeLists.txt for the `spatialroot_gui` executable. Adds GLFW via `add_subdirectory` guarded with `if(NOT TARGET glfw)`. Finds OpenGL via `find_package(OpenGL REQUIRED)`. Compiles ImGui core sources + GLFW/OpenGL3 backends + `imgui_stdlib.cpp`. Links `EngineSessionCore + glfw + OpenGL::GL + Threads::Threads`. Defines `GL_SILENCE_DEPRECATION` on Apple.
**Notes:** Uses the Dear ImGui built-in `imgui_impl_opengl3_loader.h` — no GLAD or GLEW needed. `glViewport`, `glClearColor`, `glClear` are GL 1.0 functions exposed by `GLFW/glfw3.h` directly.

---

### [Stage 3 — SubprocessRunner: cross-platform background subprocess]

**Date:** 03-30
**Files changed:** `gui/imgui/src/SubprocessRunner.hpp` (created), `gui/imgui/src/SubprocessRunner.cpp` (created)
**What was done:** `SubprocessRunner` runs a subprocess on a background thread and streams stdout+stderr (merged via `2>&1`) to an `OutputCallback` one line at a time. `start()` returns false if already running. `isRunning()`, `exitCode()`, `wait()` interface. `buildCommand()` double-quotes and backslash-escapes all tokens. Uses `popen`/`_popen` on POSIX/Windows. `WEXITSTATUS(ret)` on POSIX for correct exit code extraction.
**Notes:** `OutputCallback` is called from the background thread — callers must protect any shared state (e.g. lock a mutex before appending to a shared log deque). Chose `popen` over `posix_spawn` for simplicity and cross-platform coverage; good enough for V1 use case (cult-transcoder invocation).

---

### [Stage 3 — App.hpp: App class declaration and state machine]

**Date:** 03-30
**Files changed:** `gui/imgui/src/App.hpp` (created)
**What was done:** Declared `AppState` enum (`Idle, Transcoding, Running, Paused, Error`), `LogEntry` struct, and `App` class. Contains: `EngineSession mSession`, all UI state (source path, layout, remap, device, buffer size, layout preset), runtime controls (`mGain`, `mFocus`, `mSpkMixDb`, `mSubMixDb`, `mAutoComp`, `mElevationMode`), two `SubprocessRunner` instances for ADM flow and standalone transcode tab, `mEngineLog` (main-thread only), `mTcLog + mTcLogMutex` (thread-safe). Static constant arrays for buffer sizes, layout names/paths, elevation mode names, and transcode format values.
**Notes:** DEV NOTE added regarding OSC toggle: `oscPort=9009` always-on for V1 (matches Python GUI default); evaluate adding a UI toggle in a future iteration. `mEngineLog` is main-thread only — no mutex needed. `mTcLog` is written from `SubprocessRunner` background thread — protected by `mTcLogMutex`.

---

### [Stage 3 — main.cpp: GLFW window + Dear ImGui setup + render loop]

**Date:** 03-30
**Files changed:** `gui/imgui/src/main.cpp` (created)
**What was done:** Sets up GLFW with OpenGL 3.3 Core Profile (`GLFW_OPENGL_FORWARD_COMPAT` on Apple). Initialises Dear ImGui with dark theme + custom `ImGuiStyle` (rounded corners, `WindowBg = {0.08,0.08,0.08,1}`). `io.IniFilename = nullptr` (no imgui.ini persistence). Render loop uses `glfwWaitEventsTimeout(0.05)` for ~20 Hz minimum tick rate. GLFW window close callback lambda calls `app.requestShutdown()`. Parses `--root <path>` CLI arg (defaults to `"."`).
**Notes:** `glfwWaitEventsTimeout(0.05)` satisfies the API.md "50ms polling interval" contract for `update()`, `queryStatus()`, and `consumeDiagnostics()`. Window close callback is mandatory for clean macOS CoreAudio teardown (constraint #4 from `api_mismatch_ledger.md`). **SUPERSEDED** styling: initial scaffold had a minimal dark override; see "Stage 3 — Aesthetic overhaul" entry for the current full `StyleColorsLight` + cream/dark palette.

---

### [Stage 3 — App.cpp: full ImGui application implementation]

**Date:** 03-30
**Files changed:** `gui/imgui/src/App.cpp` (created)
**What was done:** Full implementation (~530 lines). `tick()` → `tickEngine()` + `renderUI()`. `tickEngine()` runs `queryStatus/update/consumeDiagnostics`, logs diagnostic events, handles `isExitRequested`, checks transcoder completion and calls `doLaunchEngine()`. `renderUI()` full-screen window with ENGINE + TRANSCODE tab bar. `renderEngineTab()`: input config section (source + layout preset + remap + device + buffer size), transport buttons (START/STOP/PAUSE/RESUME with `BeginDisabled`), status section (time/CPU/RMS/xruns — only when running), runtime controls section (slider+InputFloat pairs for gain/focus/spkMix/subMix + autoComp checkbox + elevMode combo — disabled when not running), engine log child window. `renderTranscodeTab()`: cult-transcoder inputs, TRANSCODE button, status label, thread-safe log render. `doLaunchEngine()`: full 5-stage lifecycle (`configureEngine → loadScene → applyLayout → configureRuntime → start`) + `setElevationMode()` sync. `onStart()`: validates inputs, `resetRuntimeToDefaults()`, branches ADM vs LUSID. `findCultTranscoder()`: checks `build/cult_transcoder/cult-transcoder` then `cult_transcoder/build/cult-transcoder`; appends `.exe` on Windows. `transcodeOutputPath()`: outputs to `processedData/stageForRender/<stem>.lusid.json`. `appendTcLog()`: thread-safe, color-codes error/warn/ok lines.
**Notes:** Immediate-mode setter pattern: ImGui widget return values directly trigger `mSession.setXxx()` calls — no debounce or prev-value tracking needed. Source detection uses `std::filesystem`. **SUPERSEDED:** the initial scaffold had no file dialogs and no device dropdown; both were added in the "Stage 3 — Native file dialogs" entry above. Current state: NSOpenPanel Browse buttons on all path fields, device Scan+Combo via `al::AudioDevice`.

### [Stage 2 — Task 2.1: Runtime setter methods on EngineSession]

**Date:** 03-30
**Files changed:** `spatial_engine/realtimeEngine/src/EngineSession.hpp`, `spatial_engine/realtimeEngine/src/EngineSession.cpp`
**What was done:** Added six V1.1 runtime setter method declarations to `EngineSession.hpp` and implementations to `EngineSession.cpp`: `setMasterGain`, `setDbapFocus`, `setSpeakerMixDb`, `setSubMixDb`, `setAutoCompensation`, `setElevationMode`. All implementations mirror the existing OSC callbacks in `start()` exactly, using `std::memory_order_relaxed` throughout. `setDbapFocus` and `setAutoCompensation(true)` set `mPendingAutoComp` for deferred main-thread recomputation via `update()`.
**Notes:** Implementations verified against OSC callback block (lines 189–223 of `EngineSession.cpp`). `setSpeakerMixDb`/`setSubMixDb` apply the same `powf(10.0f, dB/20.0f)` conversion used in `configureRuntime()` and the OSC spkMixDb/subMixDb callbacks.

---

### [Stage 2 — Task 2.4: EngineOptions::elevationMode type fix]

**Date:** 03-30
**Files changed:** `spatial_engine/realtimeEngine/src/EngineSession.hpp`, `spatial_engine/realtimeEngine/src/EngineSession.cpp`, `spatial_engine/realtimeEngine/src/main.cpp`
**What was done:** Changed `EngineOptions::elevationMode` from `int` to `ElevationMode` enum with default `ElevationMode::RescaleAtmosUp`. Updated `configureEngine()` in `EngineSession.cpp` to use `static_cast<int>(opts.elevationMode)` for the atomic store. Updated `main.cpp` to cast the parsed int to `ElevationMode` via `static_cast<ElevationMode>(std::max(0, std::min(2, elModeInt)))`.
**Notes:** clangd reports C++11-extensions warnings for the default member initializer using the enum — these are false positives; the project builds as C++17. Same clangd limitation noted in Stage 1 log.

---

### [Stage 2 — Embedding test]

**Date:** 03-30
**Files changed:** `spatial_engine/realtimeEngine/src/embedding_test.cpp` (created), `spatial_engine/realtimeEngine/CMakeLists.txt`
**What was done:** Wrote `embedding_test.cpp` (option A — compile+link verification, no test data required). The test calls `configureEngine()` with `oscPort=0`, expects `loadScene()` to fail gracefully with a non-existent path, calls all six V1.1 setter methods, calls `queryStatus()` and `consumeDiagnostics()`, then calls `shutdown()`. Added `embedding_test` executable target to `realtimeEngine/CMakeLists.txt` linking `EngineSessionCore`.
**Notes:** Option A chosen per user decision — test data not required, primary value is compile+link verification of the full V1.1 API surface.

---

### [Stage 2 — Task 2.5: API.md V1.1 additions]

**Date:** 03-30
**Files changed:** `PUBLIC_DOCS/API.md`
**What was done:** Removed runtime setters from "Out of Scope for V1". Added method documentation for all six setter methods. Added "Runtime Parameter Control (V1.1)" section with setter method table (ranges matching OSC param ranges), `update()` polling loop contract for GUI hosts, and `oscPort=0` behavior documentation. Updated `EngineOptions` table: `elevationMode` field now documents `ElevationMode` enum type instead of `int`.
**Notes:** OSC param ranges from `OscParams` in `EngineSession.cpp`: gain 0.1–3.0, focus 0.2–5.0, spkMixDb/subMixDb ±10.

---

### [Stage 2 → Stage 3 handoff — root CMakeLists.txt and onboarding prompt update]

**Date:** 03-30
**Files changed:** `CMakeLists.txt`, `internalDocsMD/cpp_refactor/refactor_planning.md`
**What was done:** Updated root `CMakeLists.txt`: replaced all `gui/qt/` references with `gui/imgui/`, removed `find_package(Qt6)` call, updated option description and status message to "ImGui + GLFW". Updated onboarding prompt "Current state" and "What you should do right now" sections to reflect Stage 2 complete and Stage 3 starting.
**Notes:** The Stage 3 task sections (3.1, 3.2, 3.3) in refactor_planning.md still use Qt terminology — the next agent is instructed to treat them as the requirements spec and adapt to ImGui/GLFW equivalents. Key mappings: gui/qt/ → gui/imgui/, QTimer → GLFW render loop, QProcess → popen/posix_spawn, QMainWindow::closeEvent → glfwSetWindowCloseCallback.

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
**What was done:** Added install() for EngineSessionCore static library archive and public headers (EngineSession.hpp, RealtimeTypes.hpp). Also renamed the first "── Executable ──" comment to "── Core engine library ──" to fix duplicate section heading.
**Notes:** The full exported CMake target (EXPORT / install(EXPORT) / find_package support) was deferred to Stage 2 task 2.2. Two blockers: (1) PUBLIC target_include_directories use source-relative paths that CMake rejects in install-time exports — requires switching to BUILD_INTERFACE/INSTALL_INTERFACE generator expressions; (2) `al` (AlloLib) is not in an export set, so the export chain cannot be completed until the AlloLib include path strategy is resolved (Stage 2 task 2.2). Build verified working: init.sh + build.sh produce all three binaries from clean with no Python dependency.

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

### [Stage 2 — Discovery Task B: OSC port=0 guard]

**Date:** 2026-03-30
**Files changed:** `spatial_engine/realtimeEngine/src/EngineSession.cpp`
**What was done:** Investigated `al::ParameterServer` port=0 behavior by reading AlloLib source. `ParameterServer::listen()` checks `if (oscPort < 0)` — port 0 falls through and calls `osc::Recv::open(0, ...)`. UDP bind to port 0 succeeds on macOS with OS-assigned ephemeral port. Result: port=0 does NOT fail `start()`, but does NOT disable OSC either — it starts a server on a random port. Added `if (mOscPort > 0)` guard wrapping the entire ParameterServer + OscParams block in `start()`.
**Notes:** IDE diagnostics (clangd) show false errors because clangd lacks CMake include paths; actual compile is unaffected. `shutdown()` is already guarded with `if (mParamServer)` — no change needed there. Stage 2 Tasks 2.1, 2.2, 2.4, 2.5, and the embedding test remain for the next agent.

---

### [Stage 3 — Aesthetic overhaul: dark/cream theme, Menlo font, card layout, transcode tab cards]

**Date:** 03-30
**Files changed:** `gui/imgui/src/main.cpp`, `gui/imgui/src/App.cpp`, `README.md`
**What was done:** `main.cpp`: loaded Menlo 13.5px (falls back to ImGui default), applied `StyleColorsLight()` base then full dark/cream palette override (dark `WindowBg`, cream `ChildBg`/`FrameBg`, dark text). Added workflow breadcrumb header ("ADM → LUSID → Spatial Render") centred, state badge right-aligned. `renderEngineTab()`: rewrote as four bordered `BeginChild` cards — INPUT CONFIGURATION (186px), TRANSPORT (108px with integrated status row), RUNTIME CONTROLS (220px with uppercase labels), ENGINE LOG (remaining height). `renderTranscodeTab()`: restructured into three cards — TRANSCODE CONFIGURATION (162px), TRANSCODE CONTROL (80px with right-aligned status badge matching TRANSPORT card style), TRANSCODE LOG (remaining height). `README.md`: updated Qt references to ImGui+GLFW, added C++ GUI section with build/launch instructions, added `run.sh`/`run.ps1` to build scripts table, updated project structure tree.
**Notes:** Card heights calculated from FontSize + FramePadding*2 + ItemSpacing.y ≈ 28px/row. `GrabRounding=10.f` gives circular slider grabs. `WindowRounding=0.f` / `ChildRounding=4.f` keeps outer window sharp while cards are slightly rounded. Pending human build verification.

---

### [Stage 1 — API.md constraints and embedding instructions]

**Date:** 2026-03-29
**Files changed:** `PUBLIC_DOCS/API.md`
**What was done:** Added Constraints section documenting: staged setup requirement, shutdown() is terminal, OSC ownership, shutdown order, 64-channel bitmask limit, update() main-thread requirement. Added Embedding Instructions section with CMake and include path guidance. Updated "Out of Scope for V1" to note runtime setters are planned for V1.1.
**Notes:** The 64-channel limit and OSC ownership constraint were previously undocumented. Embedding instructions reflect that AlloLib headers are already transitively exposed via the existing PUBLIC target_include_directories.
