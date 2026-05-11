# Development History

**Last Updated:** May 10, 2026
**Note:** Newest entries at top, oldest at bottom.

---

## Release-Hardening Audit — API Doc Sync (May 10, 2026)

**Status:** Complete. Documentation-only update.

**What changed:**

- `PUBLIC_DOCS/API.md`: Updated to reflect the current dB-based runtime parameters, setter names (`setMasterGainDb` etc.), lifecycle notes (configureRuntime safe before/after `start()`), and `getRuntimeParams()` / `resetRuntimeParams()`.
- `internalDocs/API.md`: Aligned struct names (`EngineOptions`, `SceneInput`, `LayoutInput`, `RuntimeParams`) and diagnostic types (`DiagnosticEvents`).
- `internalDocs/API/new_context.md`: Removed machine-specific test paths and updated struct naming references.

## TRANSCODE Tab UI Cleanup — Workflow Consolidation (May 10, 2026)

**Status:** Complete. No backend changes.

**Motivation:** TRANSCODE tab had a nested horizontal sub-tab row ("ADM to LUSID Scene", "ADM WAV to LUSID Package", "LUSID to ADM Export") creating a visually cluttered stacked-tab appearance. Desired a cleaner Spatial Root-style workflow panel with fewer visual tiers.

**What changed:**

- `source/gui/imgui/src/App.hpp`:
  - Reduced `kTcWorkflowNames[]` from 3 to 2: "ADM/BW64 to LUSID" and "LUSID to ADM/BW64"
  - Added `kTcOutputTypeNames[]` for output type selector: "Scene JSON only", "Full LUSID package"
  - Added `mTcOutputType` state variable to track output type selection
  - Unified the two ADM-to-LUSID workflows (formerly separate tabs) into a single UI with output type dispatch

- `source/gui/imgui/src/App.cpp`:
  - Refactored `renderTranscodeTab()`: removed nested `ImGui::BeginTabBar("##tc_workflow")` sub-tab bar
  - Replaced with button-based workflow selector at the top: `[ADM/BW64 to LUSID]` / `[LUSID to ADM/BW64]`
  - Added top-level description: "Convert ADM, BW64/WAV, and LUSID assets for Spatial Root workflows."
  - Added output type dropdown inside "ADM/BW64 to LUSID" workflow (Scene JSON only vs Full LUSID package)
  - Output type selection routes to appropriate backend command:
    - Scene JSON only → `cult-transcoder transcode` (preserved)
    - Full LUSID package → `cult-transcoder package-adm-wav` (preserved)
  - LUSID to ADM/BW64 workflow uses `cult-transcoder adm-author` (preserved)
  - Updated all form labels to sentence-case: "Source", "Detected format", "Output type", "Output path", "Options", "Keep temp files for debugging"
  - Button labels changed to "Convert" and "Export ADM/BW64" (not all-caps)
  - Status display shows "Status: Idle/Running/Complete/Failed"

- Backend preservation:
  - All three cult-transcoder subcommands remain wired: `transcode`, `package-adm-wav`, `adm-author`
  - All existing state fields retained (though workflow 1 fields now unused in UI but remain in class)
  - File browse behavior, temp-file toggle, format detection, LFE mode all preserved

---

## Runtime Parameter Staging, Reset, and Single-Source Defaults (May 10, 2026)

**Status:** Complete. Covers `EngineSession` API, CLI, GUI, and docs.

**Motivation:** Runtime controls (gain, focus, mix) were disabled in the GUI before playback started and reset to hardcoded defaults on every Start click. Default values were duplicated across `RuntimeParams`, CLI argument parsing, and `resetRuntimeToDefaults()` in App.cpp. `configureRuntime()` mixed parameter setup with output routing setup, making it unsafe to call before `applyLayout()`.

**What changed:**

- `EngineSession.hpp` / `EngineSession.cpp`:
  - Added `RuntimeParams::defaults()` — single canonical source of default values.
  - Added file-local helpers: `clampDb`, `clampFocus`, `dbToLinear`, `linearToDb` (defensive: zero/negative linear → -60 dB, not -inf).
  - Added private `sanitizeRuntimeParams()`, `applyRuntimeParamsToConfig()`, `configureOutputRouting()`.
  - Added public `getRuntimeParams()` — reads atomics, converts linear→dB, re-clamps; reflects setters, OSC, and `configureRuntime` changes.
  - Added public `resetRuntimeParams()` — equivalent to `configureRuntime(RuntimeParams::defaults())`.
  - Refactored `configureRuntime()`: now only writes gain/focus/mix atomics + syncs OSC if running. No longer touches `mSpatializer` or `OutputRemap`.
  - Moved output routing setup (remap CSV scaffolding + layout-derived routing) to `configureOutputRouting()`, called at the end of `applyLayout()`. `configureRuntime()` is now safe to call before `applyLayout()`.
  - Updated `start()`: initializes OSC param values from `getRuntimeParams()` instead of reading atomics directly.
  - Updated individual setters to use `clampDb`/`clampFocus`/`dbToLinear` helpers consistently.

- `main.cpp`: CLI now initializes `RuntimeParams` from `RuntimeParams::defaults()` and overrides from flags, eliminating duplicated literal defaults.

- `App.cpp`:
  - Removed `BeginDisabled` guard around runtime controls — sliders/inputs are always editable as staged values before Run.
  - Input callbacks always clamp; setters called only when running.
  - Added "Reset Parameters" SmallButton inline with the RUNTIME CONTROLS header. Before Run: resets GUI staged values via `resetRuntimeToDefaults()`. After Run: calls `mSession->resetRuntimeParams()` and syncs GUI from `mSession->getRuntimeParams()`.
  - Removed `resetRuntimeToDefaults()` call from `onStart()` — staged values are now preserved when playback starts.
  - Updated `resetRuntimeToDefaults()` to use `RuntimeParams::defaults()` instead of hardcoded literals.

- `internalDocs/API_internal.md`, `REALTIME_ENGINE.md`, `AGENTS.md`: updated to reflect new API methods, routing refactor, defaults source, and OSC sync semantics.

**OSC sync note:** `configureRuntime()` and `resetRuntimeParams()` sync OSC-visible parameter values when the OSC server is running. Individual setters (`setMasterGainDb` etc.) do not sync OSC values — they write atomics only. `getRuntimeParams()` reflects OSC changes because both OSC callbacks and setters write the same atomics.

---

## Offline ADM Rendering — Phase 3B/3C: CULT Orchestration + Source Validation (May 10, 2026)

**Status:** Complete. `--adm` without `--positions` now mirrors the realtime ADM architecture end-to-end.

**Motivation:** The offline renderer accepted `--adm` but required a separately-provided `--positions` scene file. The realtime path invokes CULT Transcoder to generate `scene.lusid.json` from the ADM metadata automatically. The offline path should mirror this: CULT generates the scene, the original ADM WAV remains the multichannel audio payload, and source node IDs map to WAV channels using the established CULT convention.

**What changed:**

- `source/spatial_engine/spatialRender/main.cpp`
  - `--adm` **without** `--positions`: invokes `cult-transcoder transcode` automatically to generate `scene.lusid.json` in a unique temp directory, then passes the generated scene through the existing render path. The original ADM WAV is the audio payload throughout.
  - `--adm` **with** `--positions`: uses the provided scene file directly. CULT is **not** invoked. This is the legacy/direct path for users who have already run CULT separately.
  - CULT invocation: `--in-format adm_wav --out-format lusid_json --lfe-mode hardcoded --report <temp>/reports/transcode_report.json`
  - CULT binary resolution order: `--cult-transcoder` CLI flag → `CULT_TRANSCODER` env var → `build/internal/cult_transcoder/` (cwd-relative) → `internal/cult_transcoder/build/` (cwd-relative) → executable-relative equivalents. Failure prints all searched paths.
  - Temp directory created under `std::filesystem::temp_directory_path()`; deleted on success unless `--keep-temp-dir`; always preserved on failure with path printed to stderr.
  - Post-load source validation: after `loadSourcesFromADM()`, every source declared in the LUSID scene is verified against the loaded audio map. Missing sources produce a hard failure that names each unmappable source, the ADM file path, and the expected convention.
  - Error messages for all failure paths now include the relevant file path(s) and temp dir location.
  - New flags: `--cult-transcoder PATH`, `--keep-temp-dir`.
  - Updated `--help` to document both `--adm` modes, the CULT search order, temp-dir behavior, and the source mapping convention.

**Offline ADM source mapping convention** (applies to the offline renderer only; not a general ADM or LUSID rule):

- `"N.1"` maps to 0-based WAV channel `N-1` (e.g. `"1.1"` → ch0, `"2.1"` → ch1)
- `"LFE"` maps to WAV channel 3 when the file has ≥ 4 channels — matches `--lfe-mode hardcoded` passed to CULT
- Sources that cannot be mapped using this convention are a hard failure (not silently skipped)

**Explicit non-change:** No realtime engine files modified. `WavUtils.hpp`, `WavUtils.cpp`, `SpatialRenderer`, `OfflineOutputRouteMap`, and all Phase 2 device-indexed output behavior are unchanged.

---

## OfflineOutputRouteMap Phase 1 (May 10, 2026)

**Status:** Complete. Offline-only routing helper added; no realtime or offline render behavior change.

**Motivation:** The repository already had an offline renderer, but its channel model diverged from the realtime engine's two-space routing semantics. Main speakers were still rendered to consecutive output indices even when the layout used sparse `deviceChannel` assignments.

**What changed:**

- Added `source/spatial_engine/spatialRender/OfflineOutputRouteMap.hpp`
- Added `source/spatial_engine/spatialRender/OfflineOutputRouteMap.cpp`
- Added an offline CLI diagnostic path in `source/spatial_engine/spatialRender/main.cpp`:
  - `--print-output-route-map`
  - `--validate-layout-only`

**Behavior:** `OfflineOutputRouteMap` builds an offline-owned compact-internal-channel to sparse-device-output routing table from `SpeakerLayoutData`, includes subwoofers in the same map, derives `outputChannelCount` as `max(deviceChannel) + 1`, accepts non-contiguous device channels, reports silent gaps as warnings, and rejects negative or duplicate `deviceChannel` assignments.

**Explicit non-change:** `SpatialRenderer` still renders with its pre-existing consecutive main-speaker output behavior in this phase. The new route map is diagnostic/scaffolding for Phase 2 wiring only.

---

## OfflineOutputRouteMap Phase 2 (May 10, 2026)

**Status:** Complete. Offline render output now uses the offline-owned route map for device-indexed WAV channel assignment.

**Motivation:** Phase 1 established the offline route-map helper, but the actual offline renderer still wrote main speakers to consecutive WAV channels while only partially honoring sparse subwoofer `deviceChannel` assignments. That diverged from the repo’s compact-internal-bus to sparse-output-bus model.

**What changed:**

- `source/spatial_engine/spatialRender/main.cpp` now builds and validates `OfflineOutputRouteMap` before offline audio rendering begins.
- `source/spatial_engine/spatialRender/SpatialRenderer.hpp`
- `source/spatial_engine/spatialRender/SpatialRenderer.cpp`
  - render to a compact internal bus sized to `numSpeakers + numSubwoofers`
  - place LFE/subwoofer output on compact internal subwoofer channels instead of sparse final output indices
  - scatter the compact internal bus to the final device-indexed WAV bus using `OfflineOutputRouteMap.routes`

**Behavior:** Offline WAV output now preserves layout `deviceChannel` assignments for both main speakers and subwoofers. Final output width is `max(deviceChannel) + 1`, non-contiguous device channels are supported, and unmapped output channels are present and silent.

**Explicit non-change:** This phase does not alter realtime engine behavior, DBAP/LBAP math, LUSID parsing, direct-speaker semantics, GUI behavior, or the public engine API.

---

## Engine Failure Diagnostics in GUI Log (May 10, 2026)

**Status:** Complete. No behavior change on success; richer log output on failure.

**Motivation:** When engine startup failed (bad scene file, missing audio, wrong device, invalid layout), the GUI engine log showed only a one-line high-level error (e.g. `loadScene failed: No source files could be loaded`). The detailed diagnostic output from `Streaming`, `RealtimeBackend`, `JSONLoader`, and `LayoutLoader` appeared only in the process terminal (stdout/stderr), making remote or GUI-only debugging much harder.

**Approach:** `StageCapture` — a RAII stream-tee that installs `TeeStreamBuf` on both `std::cout` and `std::cerr` for the duration of each startup stage. All existing terminal output is preserved unchanged. On failure, the captured text is formatted into a structured `=== Failure diagnostics ===` block and stored in `EngineSession::mFailureDiagnostics`. The GUI retrieves it via the new `getFailureDiagnostics()` API and appends it line-by-line to `mEngineLog`.

**Thread safety:** Capture is only active during single-threaded startup stages. In `start()`, capture is explicitly restored before `mStreaming->startLoader()` to avoid racing with the background loader thread. No capture occurs during audio callback execution.

**Files changed:**

- `source/spatial_engine/realtimeEngine/src/EngineSession.hpp` — added `getFailureDiagnostics()` and `storeFailureDiagnostics()` declarations; added `mFailureDiagnostics` member
- `source/spatial_engine/realtimeEngine/src/EngineSession.cpp` — added `TeeStreamBuf`, `StageCapture`; wired into `loadScene`, `applyLayout`, `start`; added `getFailureDiagnostics()` and `storeFailureDiagnostics()` implementations
- `source/gui/imgui/src/App.hpp` — added `appendFailureDiagnostics()` declaration
- `source/gui/imgui/src/App.cpp` — added `appendFailureDiagnostics()` implementation; called at all five failure points in `doLaunchEngine()`

**Failure log format:**

```
=== Failure diagnostics ===
Stage: load scene (ADM streaming)
Scene: /path/to/scene.lusid.json
ADM: /path/to/source.wav
Error: No source channels could be loaded from ADM.
Terminal output:
[Streaming] FATAL: Failed to open ADM file.
  ...
=== End failure diagnostics ===
```

**Acceptance:** Successful runs produce the same engine log as before. Failed runs now include the same diagnostic detail visible in the terminal. Terminal output is unchanged.

**Build validated:** `[100%] Built target spatialroot_gui`.

---

## GUI Transcoder Tab — Three-Workflow Redesign (May 8, 2026)

**Status:** Complete. Replaces the single ADM→LUSID panel with a three-workflow tab bar exposing the full current CULT transcoder CLI surface.

**Motivation:** The previous Transcoder tab only exposed `cult-transcoder transcode` (ADM→LUSID Scene). CULT now supports two additional subcommands (`package-adm-wav`, `adm-author`) that were inaccessible from the GUI.

**Files changed:**

- `source/gui/imgui/src/App.hpp` — added workflow state members and constants for all three workflows
- `source/gui/imgui/src/App.cpp` — `renderTranscodeTab()` rebuilt with `BeginTabBar("##tc_workflow")`

**Three sub-tabs now exposed:**

1. **ADM to LUSID Scene** (`cult-transcoder transcode`) — converts ADM XML or ADM WAV/BWF metadata to `scene.lusid.json`
2. **ADM WAV to LUSID Package** (`cult-transcoder package-adm-wav`) — extracts ADM, converts metadata, splits interleaved audio into a LUSID package directory
3. **LUSID to ADM Export** (`cult-transcoder adm-author`) — authors LUSID package material into Logic-compatible ADM BWF/WAV + sidecar ADM XML

**Each tab has:** file/folder pickers with type-appropriate browse dialogs, inline validation warnings for missing required fields, run button disabled until required fields are filled, live command preview, run-status indicator. Experimental `adm-author` options (`--dbmd-source`, `--metadata-post-data`) gated behind a collapsible section with amber warning text. `--stdout-report` added to all workflows so report JSON appears in the shared log.

**Architecture preserved:** subprocess CLI calls via `SubprocessRunner`, shared `mTcRunner`/`mTcLog`/status. No new integration style introduced.

**Build validated:** `[100%] Built target spatialroot_gui`.

---

## Offline Renderer Source Split (May 8, 2026)

**Status:** Complete. Non-destructive source-layout separation only.

- Moved offline renderer implementation from `source/spatial_engine/src/` into `source/spatial_engine/spatialRender/`
- New offline-owned paths:
  - `source/spatial_engine/spatialRender/main.cpp`
  - `source/spatial_engine/spatialRender/SpatialRenderer.hpp`
  - `source/spatial_engine/spatialRender/SpatialRenderer.cpp`
- Shared infrastructure intentionally stayed in `source/spatial_engine/src/`:
  - `JSONLoader.*`
  - `LayoutLoader.*`
  - `WavUtils.*`
- `source/spatial_engine/realtimeEngine/` was preserved as the active runtime engine with no intended behavior changes
- `source/spatial_engine/spatialRender/CMakeLists.txt` was updated to build from the relocated offline sources while continuing to link shared implementations from `../src/`
- Ambiguity intentionally preserved: some realtime logic was historically adapted from `SpatialRenderer`, but the realtime engine remains self-contained and should not depend on offline internals

---

## Repository Layout Reorganization (May 8, 2026)

**Status:** Non-destructive path cleanup complete.

- Active application code moved under `source/`: `source/gui/`, `source/spatial_engine/`, and `source/scripts/`
- Bundled internal toolchain components moved under `internal/`: `internal/cult_transcoder/` and `internal/LUSID/`
- Root CMake, nested CMake files, launcher scripts, GUI path helpers, and onboarding/build docs were updated to resolve from the new layout
- Submodule paths were preserved via tracked moves, including root `.gitmodules` updates for `internal/cult_transcoder` and `internal/LUSID`
- No engine, GUI, transcoder, or schema logic was intentionally refactored during this pass; changes were limited to path resolution and repository organization
- One pre-existing untracked local artifact remains at `source/spatial_engine/realtimeEngine/build/`; it moved with the directory rename and was left untouched to avoid destructive cleanup of local generated files

---

## Temporary Session Cache Cleanup (May 7, 2026)

**Status:** Complete in the ImGui GUI layer.

- Added `SpatialRootPaths` as the centralized cache/temp-session path utility using platform app-cache defaults and `std::filesystem`
- Generated GUI-owned ADM→LUSID sessions now live under `<cacheRoot>/temp-sessions/session_<timestamp>_<shortid>/`
- Each owned temp session writes `.spatialroot_temp_session` plus `manifest.json`; generated `scene.lusid.json` and `reports/` are added only when produced by the workflow
- App shutdown cleanup deletes only owned temp sessions that are inside the temp-sessions root and carry the marker file
- `Save Generated Scene...` and `Save Diagnostic Files...` copy temp-session contents to a user-selected durable location; the original temp session still remains eligible for deletion
- Added `--keep-temp-sessions` and GUI `Keep temporary generated files for debugging`
- Broad `processedData` cleanup was intentionally avoided. Deletion applies only to Spatial Root temp sessions the GUI created and can prove it owns

---

## Pause / Stop Click Transient Fixes (May 7, 2026)

**Status:** Fixes 11.1–11.5 patched and confirmed. Residual intermittent soft clicks deferred.

**Problem:** Pausing produced an audible click. Stop was comparatively graceful (CoreAudio hardware buffer drain), but could also click at high amplitude. Rapid pause/resume toggling clicked. Subsequent pauses (after resume and re-pause) clicked even when the first pause was clean.

**Root causes and fixes (`RealtimeBackend.hpp`):**

- **11.1 — memset wipes fade:** The late early-return after Step 4 ran `std::memset(outBuffer, 0, ...)` on the fade-completing block, erasing the 8ms ramp that Step 4 had just computed. Hardware received silence for the entire block → click. Fix: removed the memset. Step 4's multiply-by-zero already zeroes the buffer correctly.

- **11.2 — stop() no fade:** `stop()` called `mAudioIO.stop()` directly with no prior fade. Fix: arm `mConfig.paused = true` before the hard stop, sleep 50ms unconditionally, then call `mAudioIO.stop()`, then reset `mConfig.paused = false`.

- **11.3 — resume mid-fade forced reset:** The resume branch in Step C set `mPauseFade = 0.0f` unconditionally, causing a gain jump when a fade-out was mid-ramp. Fix: removed the forced reset; step computed as `(1.0f - mPauseFade) / fadeFrames` so the ramp continues from current value.

- **11.4 — stop-after-recent-pause conditional sleep:** The initial Bug 11.2 fix put the 50ms sleep inside `if (!paused)`, so it was skipped if a fade was already in progress. Fix: moved sleep outside the guard — unconditional.

- **11.5 — Spatializer anchor caching:** During steady pause the full render path still ran, updating `mPrevSafePos` / `mPrevGuardFired` / `mPrevWasFastMover` on every block with the frozen `currentFrame` position. After many paused blocks these anchors were over-fit to the paused position; resume → re-pause produced a transient. Fix: new fast-path early-return before Step 1 — if fully paused (fade complete), memset + return without calling the Spatializer.

**Deferred:** Some intermittent soft clicks persist, likely related to the 50ms exponential smoother having residual gain on the first steady-paused block, or sub-block timing of rapid toggle events. Not reliably reproducible. See `REALTIME_ENGINE.md § Deferred` for full notes.

---

## Normalized DBAP — Fast-Mover Continuity Fix (May 7, 2026)

**Status:** Fix confirmed working at translab. Bug 10.1 closed.

**Problem:** After the April 17 normalized DBAP upgrade, fast-moving sources produced audible pops or gain steps. Root cause: `mPrevSafePos[si]` was always written as the block-center guard-resolved position (`safePos`), even for fast-mover blocks whose last rendered audio corresponded to the last sub-step (near `positionEnd`). Under normalized DBAP, the dominant speaker gain can jump 4× (from `1/sqrt(N) ≈ 0.25` equidistant to `≈1.0` near-speaker) across the normalization basin boundary. When `positionEnd` and block-center straddle that boundary, the Bug 9.1 `doBlend` anchor on the following block was wrong — it injected a discontinuity rather than smoothing one.

**Fix (Spatializer.hpp):**

- Fast-mover loop now captures `lastSubSafePos = subSafePos` at `j == kNumSubSteps - 1`
- Fast-mover branch writes its own state immediately after the loop: `mPrevSafePos[si] = lastSubSafePos`, `mPrevGuardFired[si] = 0`, `mPrevWasFastMover[si] = 1`
- Normal-path state update guarded by `!isFastMover`; clears `mPrevWasFastMover[si] = 0`
- New `mPrevWasFastMover` per-source state vector added (same pattern as `mPrevGuardFired`)

**Key insight:** Normalized DBAP moved the dangerous gain discontinuity from the speaker surface (where the proximity guard operates) to the normalization basin boundary (where one speaker becomes clearly dominant). The guard still functions correctly but is no longer the primary discontinuity risk. Any cross-block anchor mismatch that was previously sub-threshold under the old smooth gain function can now be audible.

**Full diagnosis + code locations:** `internalDocs/engine_testing/5-7-engine_fix.md`  
**Bug audit entry:** `internalDocs/engine_testing/4_1_bug_audit.md` — Bug 10 / Bug 10.1  
**Deferred follow-up:** Bug 10 (per-sub-step guard variability) — endpoint pre-guarding. Implement if pops persist.

---

## DBAP Normalization / cult-allolib Integration (April 17, 2026)

**Status:** Complete through normalization, fork integration, and auto-compensation removal. Runtime validation remains the next live phase.

- `thirdparty/allolib` was removed and replaced by the internal fork at `internal/cult-allolib`
- `al_Dbap.cpp` now uses max-scaled L2 normalization in both `renderSample()` and `renderBuffer()`, enforcing `sum(v_k^2) = 1`
- DBAP focus now has a minimum supported value of `0.1`, clamped across construction, runtime configuration, direct setters, and OSC callbacks
- Focus auto-compensation was removed from engine, GUI, CLI, OSC, and current API-facing docs because normalized DBAP no longer needs a corrective gain layer
- `internalDocs/DBAP/dbapMath.md` was established as the DBAP math source of truth; onboarding and plan docs were updated to match

Historical note: older entries below may mention `autoCompensation`, `mPendingAutoComp`, or `thirdparty/allolib` because they describe the pre-normalization engine state at the time those entries were written.

---

## Phase 6 — C++ Refactor Complete (March 29–31, 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed.

**Status:** All three stages complete as of 2026-03-31. Python removed from primary workflow.

**What was built:**

- `init.sh` / `build.sh` replace the Python build system entirely
- `spatialroot_realtime` C++ binary is the primary engine CLI
- `EngineSessionCore` static library exposes the full V1.1 API (runtime setters, `oscPort=0` guard, typed `ElevationMode`)
- `spatialroot_gui` — Dear ImGui + GLFW desktop GUI — builds, launches, and links `EngineSessionCore` directly (no subprocess, no OSC dependency for local control)

**Deferred items:**

- macOS Dock icon: in-app Dock tile icon via `[NSApp setApplicationIconImage:]` works while running. The desktop/Finder file icon does not update — requires packaging as a `.app` bundle. Deferred to a future phase.
- `SPATIALROOT_BUILD_GUI=ON` flag: GUI build disabled in CI until `source/gui/imgui/CMakeLists.txt` integration is verified.

### Stage 3 — ImGui + GLFW GUI

**Key entries from `refactor_log.md`:**

**Native file dialogs (NSOpenPanel) + Browse buttons + device dropdown** (03-30):

- Replaced broken `osascript + popen` file dialog with `NSOpenPanel` Objective-C++ (`FileDialog_macOS.mm`)
- Browse buttons on SOURCE, LAYOUT, REMAP CSV, TRANSCODE INPUT fields
- Inline "ADM"/"LUSID" green tag next to SOURCE label
- Device scan: `al::AudioDevice` enumeration via Scan button + combo
- `osascript` blocked main thread and appeared behind GLFW window; `NSOpenPanel` runs its own Cocoa modal

**ImGui + GLFW CMake target** (03-30): Created `source/gui/imgui/CMakeLists.txt`. Uses `imgui_impl_opengl3_loader.h` — no GLAD or GLEW needed. Links `EngineSessionCore + glfw + OpenGL::GL`.

**App class** (`App.hpp`/`App.cpp`) (03-30): `AppState` enum, `EngineSession mSession` (unique_ptr for restart), UI state, runtime controls (gain/focus/spkMixDb/subMixDb/autoComp/elevMode), `SubprocessRunner` for cult-transcoder invocation, thread-safe log. Full `tick()` → `tickEngine()` + `renderUI()`. `doLaunchEngine()` implements 5-stage lifecycle. `findCultTranscoder()` checks `build/internal/cult_transcoder/cult-transcoder` then `internal/cult_transcoder/build/cult-transcoder`. Historical note: the `autoComp` control mentioned here was removed on 2026-04-17 after DBAP normalization landed.

**Aesthetic overhaul** (03-30): Menlo 13.5px font. `StyleColorsLight()` base + dark/cream palette. Workflow breadcrumb header. Four bordered card layout for engine tab: INPUT CONFIGURATION (186px), TRANSPORT (108px), RUNTIME CONTROLS (220px), ENGINE LOG.

**Engine restart fix** (03-31): `mSession` → `std::unique_ptr<EngineSession>`. `doLaunchEngine()` resets via `std::make_unique<EngineSession>()` before each launch.

**Executable name fix** (03-31): `set_target_properties(spatialroot_gui PROPERTIES OUTPUT_NAME "Spatial Root")` in CMakeLists. `run.sh`/`run.ps1` updated to reference quoted binary name.

### Stage 2 — EngineSessionCore Hardening

**GUI framework decision**: Dear ImGui + GLFW chosen over Qt. Qt cannot be used as a git submodule (multi-GB source, complex bootstrap). All dependencies must be open-source submodules. ImGui (MIT, ~5 MB) + GLFW (zlib, ~1 MB).

**Task 2.1 — Runtime setters** (03-30): Added six V1.1 setter methods to `EngineSession.hpp/.cpp`: `setMasterGain`, `setDbapFocus`, `setSpeakerMixDb`, `setSubMixDb`, `setAutoCompensation`, `setElevationMode`. All use `std::memory_order_relaxed`. `setDbapFocus` and `setAutoCompensation(true)` set `mPendingAutoComp`. Historical note: `setAutoCompensation()` and the pending-auto-comp path were removed on 2026-04-17 after normalized DBAP made them obsolete.

**Task 2.3 — OSC port=0 guard** (03-30): `al::ParameterServer` with `port=0` binds to OS-assigned ephemeral port — does NOT disable OSC. Added `if (mOscPort > 0)` guard in `start()`.

**Task 2.4 — `ElevationMode` type fix** (03-30): Changed `EngineOptions::elevationMode` from `int` to `ElevationMode` enum with default `ElevationMode::RescaleAtmosUp` (struct subsequently renamed to `EngineConfig`).

**Embedding test** (03-30): `embedding_test.cpp` — compile+link verification, no test data. Calls full V1.1 API surface, expects `loadScene()` to fail gracefully with non-existent path.

### Stage 1 — Build Infrastructure

**Root CMakeLists.txt** (03-29): Option flags: `SPATIALROOT_BUILD_ENGINE`, `SPATIALROOT_BUILD_OFFLINE`, `SPATIALROOT_BUILD_CULT`, `SPATIALROOT_BUILD_GUI`. `cmake_minimum_required` raised to 3.20 (cult_transcoder requires it). AlloLib added once at root with `if(NOT TARGET al)` guard.

**`init.sh` + `build.sh`** (03-29): Pure bash. `init.sh`: check cmake/git, init allolib submodule, init cult_transcoder + nested libbw64, call build.sh. `build.sh`: `cmake --build` uniformly for generator independence. `--engine-only`, `--offline-only`, `--cult-only` flags. Old `init.sh` called Python `configCPP_posix.py::setupCppTools()` — fully replaced.

**`init.ps1` + `build.ps1`** (03-29): PowerShell equivalents. Windows multi-config generators place binaries under `build/Release/`.

**README.md rewrite** (03-29): `spatialroot_realtime` as primary CLI with actual flags. `init.sh + build.sh` documented. OSC port fixed (9009, not 12345 as README previously claimed). Two-step ADM workflow documented.

### Architecture Audits (March 29, 2026)

**`third_audit.md`** (supersedes secondary): Resolved the open GUI/runtime-control replacement question. C++ Qt GUI embedding `EngineSessionCore` directly was the original plan (since abandoned for ImGui). Direct in-process runtime setters required. OSC demoted to secondary. `RealtimeConfig` already uses `std::atomic` fields — only missing a thin public setter surface on `EngineSession`.

**`secondary_audit.md`**: Pre-refactor assessment. "The three surfaces (library API, CLI, GUI) are not cleanly separated at the process boundary." GUI launches Python which launches the binary. No top-level CMake. Build system lives in Python. OSC port discrepancy: README says 12345, code uses 9009 — code is authoritative.

**`initial_audit.md`**: Original baseline audit. Python stack assessment:

- `runRealtime.py` — top-level launcher; calls `setupCppTools()` then invokes cult-transcoder + `spatialroot_realtime` as subprocesses
- Python GUI (`gui/realtimeGUI/`) — PySide6, 3 process-hops from audio thread: GUI → Python QProcess → runRealtime.py → C++ binary
- Python LUSID library (`internal/LUSID/src/`) — parallel Python implementation of what JSONLoader.cpp does; maintenance liability
- Python venv at repo root — 1.6 GB, only PySide6 and python-osc actually needed
- `EngineSession` already implements exact lifecycle described in API.md; CMake builds `EngineSessionCore` static library

---

## Python Offline Pipeline v3.9 (March 9, 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. **Removed in Phase 6 (2026-03-31).**

**Status at removal:** Python orchestration layer (`runPipeline.py`) was modernized to use cult-transcoder preprocessing and gained `--adm` flag for direct ADM input. Both ADM and LUSID package input modes were functional. Removed along with all Python tooling in Phase 6.

**Old pipeline flow:**

```
ADM WAV → extractMetaData() → XML → parse_adm_xml_to_lusid_scene() → LUSID object
    ↓
channelHasAudio() → containsAudio.json
    ↓
packageForRender() → scene.lusid.json + mono WAV stems
    ↓
runSpatialRender() → spatialroot_spatial_render --sources <folder>
```

**Phase 3.9 modernized flow:**

```
ADM WAV → cult-transcoder transcode → scene.lusid.json
    ↓
spatialroot_spatial_render --adm <file> --positions <json> --layout <json> --out <wav>
```

**Key changes in v3.9:** Removed `containsAudio` analysis (cult-transcoder assumes all channels active). `packageForRender` became optional utility. LUSID package input mode retained for backward compatibility.

**Removed at Phase 6:** `runPipeline.py`, `src/analyzeADM/`, `src/packageADM/`, `src/analyzeRender.py`, `src/config/configCPP*.py`.

---

## Transition from `spatialroot_adm_extract` to `cult-transcoder` (Phase 3, March 2026)

### Old System: `spatialroot_adm_extract` (archived)

- **Purpose:** Extracted ADM XML metadata from BW64/RF64/WAV files.
- **Implementation:** Built using EBU libraries (`libbw64` and `libadm`).
- **Output:** `processedData/currentMetaData.xml`
- **Limitations:** Required a separate build step. Limited to ADM extraction without additional processing.

### New System: `cult-transcoder`

- **Purpose:** Handles ADM extraction and ADM→LUSID transcoding.
- **Implementation:** Integrated into the `cult-transcoder` binary.
- **Usage:** `cult-transcoder transcode --in-format adm_wav`

---

## Python GUI (PySide6) — Phase 10 (February 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. **Removed in Phase 6 (2026-03-31), replaced by Dear ImGui + GLFW GUI.**

### Architecture

- **Framework:** PySide6 (do NOT switch back)
- **Location:** `gui/realtimeGUI/` (removed)
- **Launcher:** `realtimeMain.py` at project root (removed)
- **Process launch:** `runRealtime.py` via `QProcess`
- **IPC:** AlloLib `al::Parameter` + `al::ParameterServer` (OSC port 9009)

**Process model:** GUI → Python `QProcess` → `runRealtime.py` subprocess → C++ binary. Three process-hops from audio thread.

**OSC control surface:** GUI sent OSC via `python-osc` UDP → `al::ParameterServer`. Engine printed `"ParameterServer listening"` sentinel to stdout to signal readiness. GUI monitored stdout, transitioned `LAUNCHING → RUNNING` on sentinel, then called `flush_to_osc()` to push all current slider values.

### Directory Layout

```
gui/realtimeGUI/
├── realtimeGUI.py          — Main window
├── realtime_runner.py      — QProcess wrapper + OSC sender
└── realtime_panels/
    ├── RealtimeInputPanel.py
    ├── RealtimeControlsPanel.py
    ├── RealtimeLogPanel.py
    └── RealtimeTransportPanel.py
```

### Runtime Controls (OSC)

| Parameter      | OSC Address                | Range                              | Default |
| -------------- | -------------------------- | ---------------------------------- | ------- |
| Master Gain    | `/realtime/gain`           | 0.0–1.0 (legacy PySide6 GUI range) | 0.5     |
| DBAP Focus     | `/realtime/focus`          | 0.1–5.0                            | 1.5     |
| Speaker Mix dB | `/realtime/speaker_mix_db` | -10–+10                            | 0.0     |
| Sub Mix dB     | `/realtime/sub_mix_db`     | -10–+10                            | 0.0     |
| Auto-Comp      | `/realtime/auto_comp`      | 0/1                                | 0       |
| Pause/Play     | `/realtime/paused`         | 0/1                                | 0       |

### Important Implementation Notes

**Restart bug workaround:** On restart, slider state must be reset to defaults BEFORE restarting the engine. Without this, `flush_to_osc()` on `engine_ready` pushes stale values from prior run (e.g. `gain=1.5`) into new engine. Fix: call `reset_to_defaults()` before `restart()`.

**Graceful stop:** SIGTERM → wait 3000 ms → SIGKILL.

**OSC debounce:** Slider `valueChanged` debounced at 40ms. Checkbox/combobox changes sent immediately.

**Exit code handling:** 0, -2, 130 all treated as clean exits.

**Source type detection (`_detect_source`):**

1. Not exists → error
2. File ending `.wav` → ADM source
3. Directory containing `scene.lusid.json` → LUSID package
4. Otherwise → unrecognised

### Panel Layout

- **Input Panel:** Source (ADM WAV or LUSID package), Layout (JSON), Remap CSV, Buffer size, Scan audio toggle
- **Transport Panel:** Start, Stop, Kill, Restart, Pause, Play + state indicator
- **Controls Panel:** Sliders for gain/focus/speaker mix/sub mix, auto-comp checkbox, elevation mode
- **Log Panel:** Console output from engine subprocess

---

## Python Build System Migration (February 23, 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. **Superseded by `init.sh`/`build.sh` in Stage 1 of Phase 6.**

Added cross-platform C++ tool building via Python OS-detection router.

**`src/config/configCPP.py`** — OS detection router:

```python
import os
if os.name == "nt":
    from .configCPP_windows import setupCppTools
else:
    from .configCPP_posix import setupCppTools
```

**Build commands at the time:**

| Platform | Tool                         | Build command                                                         |
| -------- | ---------------------------- | --------------------------------------------------------------------- |
| POSIX    | `cult-transcoder`            | `cmake --build`                                                       |
| POSIX    | `spatialroot_spatial_render` | `make -jN` (generator-specific, replaced by cmake --build in Phase 6) |
| POSIX    | `spatialroot_realtime`       | `make -jN` (same)                                                     |
| Windows  | all                          | `cmake --build --config Release`                                      |

**Executable paths at the time:**

| Tool             | POSIX                                                                  | Windows                             |
| ---------------- | ---------------------------------------------------------------------- | ----------------------------------- |
| ADM Extractor    | `src/adm_extract/build/spatialroot_adm_extract`                        | `...spatialroot_adm_extract.exe`    |
| Spatial Renderer | `source/spatial_engine/spatialRender/build/spatialroot_spatial_render` | `...spatialroot_spatial_render.exe` |

All of `src/config/configCPP*.py` removed in Phase 6.

---

## Issues Found During Duration/RF64 Investigation (February 16, 2026)

> From AGENTS.md §0. All items resolved.

| #   | Status   | Severity | Issue                                                                                   | Location                                |
| --- | -------- | -------- | --------------------------------------------------------------------------------------- | --------------------------------------- |
| 1   | ✅ FIXED | Critical | WAV 4 GB header overflow — `SF_FORMAT_WAV` wraps 32-bit size field                      | `WavUtils.cpp`                          |
| 2   | ✅ FIXED | High     | Legacy script trusted corrupted WAV header without cross-check (script removed Phase 6) | (historical)                            |
| 3   | ✅ FIXED | Low      | Stale `DEBUG` print statements left in renderer                                         | `SpatialRenderer.cpp`                   |
| 4   | ✅ FIXED | Medium   | `masterGain` default mismatch — now consistently `0.5`                                  | `SpatialRenderer.hpp`, `main.cpp`, docs |
| 5   | ✅ FIXED | Medium   | `dbap_focus` not forwarded for plain `"dbap"` mode (archived, pre-Phase 6)              | (historical)                            |
| 6   | ✅ FIXED | Medium   | Legacy Python wrapper exposed `master_gain` (wrapper removed Phase 6)                   | (historical)                            |

**Future items (tracked separately):**

- #9 (Info): Large interleaved buffer ~11.3 GB peak for 56ch × 566s. Mitigation: chunked streaming write.
- #10 (Info): Test files only exercise `audio_object` + `LFE` paths; `direct_speaker` untested at render level.

---

## Rendering Development Notes (January 27–28, 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. Core fixes are now part of the renderer — see [SPATIALIZATION.md](SPATIALIZATION.md) for current docs.

### Issue 1: VBAP Zero Blocks

**Root cause:** VBAP requires source direction to be within a valid speaker triplet. Outside triplets → zero output, source becomes inaudible.

**Solution:** Zero-block detection + nearest-speaker fallback:

- Input energy test → render to temp buffer → measure output energy → detect failure
- Retarget: direction 90% toward nearest speaker (90/10 blend)
- Threshold: `kPannerZeroThreshold = 1e-6`

### Issue 2: Fast Mover Blink

**Root cause:** Single direction per 64-sample block. Fast motion can cross triplet/gap boundaries within one block → audible dropout.

**Solution:** Fast-mover detection + sub-stepping:

- Sample directions at 25%/75% through block
- Angular threshold: ~14° (0.25 rad)
- Sub-step at 16-sample hops when threshold exceeded

**Architecture decisions:** Temp buffer approach (correctness first). SLERP verified. DBAP coordinate transform: AlloLib applies internal `(x,y,z) → (x,-z,y)` — compensated automatically.

---

## Old Realtime Design Sketches (Early 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed.

### Original Sample Loop Sketch

```
Inside Sample loop:
→ LUSID SOURCE LOOP
→ [LUSID Reader] && [mono wavs source folder or straight from multichannel file]
→ [source sample, position]
→ [Spatializer] -- fills mc buffer once per sample
→ mc player -- reads from mc buffer once per sample
```

### Early Streaming WAV Design (Double Buffering)

**Problem:** Memory limitation for large multichannel files (2.5 GB+ for 56ch ADM), startup delays, audio dropouts.

**Solution:** Double buffering with background pre-loading (later implemented as `Streaming.hpp`):

- Two pre-allocated buffers alternate PLAYING/LOADING
- Background thread loads chunks asynchronously
- `animate()` monitors 50% consumption, signals next chunk load
- Audio thread reads from PLAYING buffer only

**Performance targets at the time:** Before: 2.5 GB memory, 10–30s startup. After: ~6 MB active working set (2 chunks), <1s startup.

**Chunk size:** ~2.88 MB (1 minute at 48kHz for 56ch). Block-based reads (512-frame blocks). Later evolved to 480k frames (10s buffers at 75% prefetch in Phase 11).

### Deep Research Context Notes

Locked v1 design decisions documented at the time:

- 48kHz sample rate, hard real-time constraints
- Target hardware: AlloSphere (54 speakers), TransLAB (various)
- `framesPerBuffer` TBD (later: 64 default, user-configurable)
- Up to 128 sources simultaneously
- DBAP for v1, block-rate interpolation
- CPU safety: gain state machine (every 1/2/4 blocks, optional Top-K)

---

## API Agent Session History (Early 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. Historical record of the EngineSession API extraction.

### Phase 1 — Session Architecture Extraction

**Goal:** Extract orchestration logic from `main.cpp` into `EngineSession.hpp/.cpp`.

**Key changes:**

1. Created `EngineSession.hpp/.cpp` with lifecycle API: `configureEngine()` → `loadScene()` → `applyLayout()` → `configureRuntime()` → `start()` → `update()` → `shutdown()`
2. Refactored `main.cpp` — removed all heavy agent instantiation; reduced to arg parsing + polling loop
3. Resolved `RealtimeConfig` compilation issue: `std::atomic` fields delete implicit copy constructors — solved by making `EngineSession` default-constructible and populating config via `session.config()` directly
4. Updated `CMakeLists.txt` to include `EngineSession.cpp`

### Phase 2 — Struct Refactoring and Immutability (March 29, 2026)

**Goal:** Evolve past `void` arguments, resolve Mismatch 5 (Error Handlers).

**Key changes:**

1. Defined typed structs: `EngineOptions`, `SceneInput`, `LayoutInput`, `RuntimeParams` _(These structs were renamed to `EngineConfig`, `SceneConfig`, `LayoutConfig`, `RuntimeConfig` before handoff — see "Completed milestones" below.)_
2. Updated all lifecycle method signatures to use const-ref struct injection
3. Added `std::string getLastError() const` for explicit error propagation
4. `main.cpp`: removed all `session.config()` direct mutations; documented struct grouping inline
5. Protected AlloLib parameters via Pimpl `struct OscParams;` — explicit workaround for pointer stability with background OSC threads
6. Replaced generic pausing with strict `setPaused(bool)`

### Phase 2 → Phase 3 Transition

**Decisions:**

1. Created `api_internal_contract.md` to stop future agents hallucinating features based on aspirational Phase 1 texts
2. Formalized `setPaused(bool)` as sole transport control — abandoned `stop()` and `seek()` due to state-corruption risks in mismatch ledger
3. Enforced `update()` as required main-thread tick (compromise to get `computeFocusCompensation()` safely off audio thread without a complex worker pool)

**Completed milestones (as of handoff):**

- `EngineConfig`, `SceneConfig`, `LayoutConfig` extracted
- AlloLib parameter lifetimes isolated into OSC context
- `getLastError()` implemented
- Shutdown sequence ordered: `mParamServer` → `mBackend` → `mStreaming`

**Demoted/retired concepts:**

- Dynamic scene reloading — unsafe with current buffer architecture
- Arbitrary playhead seeking — unsafe with ring buffers
- CLI-only debugging flags — left in `main.cpp`, not ported to `EngineSession`

---

## Initial GUI Prototype Notes (Early 2026)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. Notes from the very first ImGui GUI prototype before aesthetic iteration.

**Engine issues found:**

- Engine doesn't restart when stopped or source file changed — time continues from previous instance
- Engine needs to be fully reset between tracks — tracks don't play after the first track
- These led to the `unique_ptr<EngineSession>` restart fix in Stage 3

**v2 requirements:**

- ADM/LUSID detected green text should be displayed next to "source"
- Device still needs dropdown menu and scanning logic
- Aesthetic: "could be sleeker"

---

## Python Source Reference (usefulPyInfo.md)

> Historical sources were consolidated into this file; pre-consolidation subfolder files were removed. Reference document capturing Python source details before removal. All described files removed in Phase 6.

### Removed Python Files

| File                                   | Role                                                                  |
| -------------------------------------- | --------------------------------------------------------------------- |
| `runRealtime.py`                       | Top-level launcher — called cult-transcoder then spatialroot_realtime |
| `realtimeMain.py`                      | Alternate CLI entry point                                             |
| `runPipeline.py`                       | Offline pipeline orchestrator                                         |
| `src/config/configCPP.py`              | OS detection router                                                   |
| `src/config/configCPP_posix.py`        | POSIX CMake/make orchestration                                        |
| `src/config/configCPP_windows.py`      | Windows CMake orchestration                                           |
| `gui/realtimeGUI/`                     | PySide6 desktop GUI                                                   |
| `gui/realtimeGUI/realtime_runner.py`   | QProcess wrapper + OSC sender                                         |
| `src/analyzeADM/checkAudioChannels.py` | Per-channel audio activity scan                                       |
| `src/packageADM/splitStems.py`         | Mono WAV stem splitter                                                |
| `src/analyzeRender.py`                 | PDF render analysis                                                   |
| `internal/LUSID/src/`                  | Python LUSID library (scene.py, xml_etree_parser.py, parser.py)       |

### cult-transcoder CLI Invocation (historical reference)

```bash
internal/cult_transcoder/build/cult-transcoder transcode \
    --in         <adm_wav_path> \
    --in-format  adm_wav \
    --out        processedData/stageForRender/scene.lusid.json \
    --out-format lusid_json \
    [--report    <report_json_path>] \
    [--lfe-mode  hardcoded|speaker-label]
```

Binary path on Windows: check both `build/Release/cult-transcoder.exe` (VS) and `build/cult-transcoder.exe` (Ninja).

### Engine Launch Sentinel (OSC timing fix)

```python
_ENGINE_READY_SENTINEL = "ParameterServer listening"
```

Sequence: GUI → LAUNCHING → scan stdout for sentinel → RUNNING → `flush_to_osc()`. Launch timeout: 3000 ms for process start. See [REALTIME_ENGINE.md § Threading and Safety](REALTIME_ENGINE.md#threading-and-safety) for current implementation.

### Per-Channel Audio Activity Scan (removed Phase 3)

`--scan_audio` flag, `exportAudioActivity()`, `containsAudio.json` — removed 2026-03-04. Superseded by cult-transcoder (assumes all channels active). Algorithm: chunked per-channel RMS scan, 30 chunks per channel, chunk_size=48000, threshold -100 dBFS. Added ~14s startup time.

### Stem Splitting (splitStems.py — offline only)

Mono WAV naming: `{chanNumber}.1.wav` (1-indexed), LFE → `LFE.wav`. LFE detection: hardcoded channel 4 (`_DEV_LFE_HARDCODED = True`). Output: `processedData/stageForRender/`. Prior existing WAVs deleted before splitting.

### processedData Directory Structure (at time of Python pipeline)

Directories created before any subprocess launch:

```
processedData/                   — output root for all pipeline artifacts
processedData/stageForRender/    — cult-transcoder writes scene.lusid.json here
```

---

## Key Milestones

| Phase    | Date           | Description                                                                                                                                                                                                                                                                                                                                                 |
| -------- | -------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Phase 1  | January 2026   | Initial ADM extraction pipeline using `spatialroot_adm_extract`                                                                                                                                                                                                                                                                                             |
| Phase 2  | February 2026  | Codebase audit; `spatialroot_adm_extract` deprecated                                                                                                                                                                                                                                                                                                        |
| Phase 3  | March 4, 2026  | Transitioned to `cult-transcoder`; removed per-channel audio scan                                                                                                                                                                                                                                                                                           |
| Phase 4  | March 7, 2026  | `cult-transcoder` gains `--lfe-mode` flag; ADM profile detection (Atmos, Sony360RA)                                                                                                                                                                                                                                                                         |
| Phase 5  | March 7, 2026  | TRANSCODE UI added to PySide6 GUI (superseded by ImGui GUI in Phase 6)                                                                                                                                                                                                                                                                                      |
| Phase 6  | March 31, 2026 | C++ refactor complete. Python GUI/entrypoints/build/venv removed. ImGui + GLFW GUI shipped.                                                                                                                                                                                                                                                                 |
| Phase 7  | April 17, 2026 | Normalized DBAP (`sum(v_k²)=1`). `thirdparty/allolib` → `internal/cult-allolib`. Auto-compensation removed.                                                                                                                                                                                                                                                 |
| Bug 10.1 | May 7, 2026    | Fast-mover continuity anchor fix for normalized DBAP (`mPrevSafePos` written as last sub-step position).                                                                                                                                                                                                                                                    |
| Phase 8  | May 10, 2026   | Persistent default speaker layout + cross-platform app settings paths. `DefaultLayoutManager` added to GUI layer. Settings dir (`~/Library/Application Support/Spatial Root/` etc.) is strictly separate from session temp cache. Atomic writes, non-fatal startup fallback, GUI controls: Set as Default / Clear Default / status display.                 |
| Phase 9  | May 10, 2026   | Offline Render tab added to Dear ImGui GUI. Wraps `spatialroot_spatial_render` via the existing `SubprocessRunner`. ADM WAV mode (experimental) and LUSID Package mode both supported. GUI does not invoke CULT directly; the offline renderer owns CULT invocation, temp dir lifecycle, and source mapping validation. Realtime engine behavior unchanged. |
