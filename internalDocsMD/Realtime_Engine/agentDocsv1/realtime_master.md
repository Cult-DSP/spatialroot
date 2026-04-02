# Real-Time Spatial Audio Engine – Agent Overview

## Phase 6 Update (2026-03-31) — C++ Refactor Complete

- **Canonical entrypoints:** `spatialroot_realtime` (CLI) and `gui/imgui/` (Dear ImGui + GLFW desktop GUI).
- **Removed:** `runRealtime.py` (Python launcher) and the PySide6 realtime GUI (`gui/realtimeGUI/`) were removed in Phase 6.
- **GUI architecture:** ImGui GUI embeds `EngineSessionCore` in-process (no `QProcess`, no subprocess).
- **Runtime control plane:** Primary control is direct C++ setter methods on the embedded session/core. OSC remains available as an optional secondary surface (default port 9009; disable with `oscPort=0`).

> Everything below is retained for archaeology as the Phase 1–10 prototype planning/completion log (updated 2026-02-26). Treat it as **archived** guidance unless explicitly referenced for historical context.

## Implementation Decisions (Archived — updated 2026-02-26)

> These decisions were made during initial planning and override any conflicting
> assumptions in the agent sub-documents. Sub-documents remain useful for
> detailed design guidance but should be read in light of the notes below.

### Development Model – Sequential, Not Concurrent

The agent architecture segments responsibilities but **agents are implemented
one at a time, sequentially**, not in parallel. Each agent is:

1. Implemented based on the logical architecture order.
2. Tested in isolation and in integration with previously completed agents.
3. Documented (this file and the agent's own `.md` are updated).
4. The updated docs are handed to a **new context window** for the next agent task.

This ensures each step is stable before the next begins.

### Audio Backend – AlloLib

Continue using **AlloLib's AudioIO** (already a dependency via
`thirdparty/allolib`). No PortAudio or JACK for v1.

### Build System & File Location

- The real-time engine lives in **`spatial_engine/realtimeEngine/`**.
- It gets its own `CMakeLists.txt` (mirroring `spatialRender/CMakeLists.txt`).
- Links against the same AlloLib in `thirdparty/allolib`.

### Code Reuse Strategy – Header-Based Core, Reference Old CPP

- The real-time engine's core logic goes into **header files** (`.hpp`) inside
  `spatial_engine/realtimeEngine/`.
- Code may be **copied and adapted** from the offline `SpatialRenderer.cpp`
  (DBAP coordinate transforms, elevation sanitization, direction interpolation,
  LFE routing, gain logic, etc.).
- The old offline `.cpp` file stays untouched — the headers reference it in
  comments for provenance but do not `#include` it.
- Goal: the offline renderer continues to compile and work exactly as before.

### GUI – Dedicated Realtime GUI Entry (PySide6) (Archived)

> **Archived:** This PySide6/QProcess GUI approach was implemented for the Phase 10 prototype and later superseded/removed in Phase 6 in favor of the ImGui + GLFW GUI (`gui/imgui/`).

- Keep PySide6 (Qt) and **do not switch to ImGui** (ImGui remains reference-only).
- Do **not** bloat the existing offline GUI. Create a parallel realtime entry:
  - `gui/realtimeGUI/realtimeGUI.py` (new)
  - optional `gui/realtimeGUI/` folder with panels + `realtime_runner.py`
- The realtime GUI launches `runRealtime.py` via `QProcess` (same pattern as `pipeline_runner.py`).

### Runtime Control Plane (stability priority) (Archived)

> **Archived:** Phase 6 uses direct in-process setters as the primary control plane; OSC/ParameterServer-style control is secondary/optional.

- Prefer AlloLib **`al::Parameter` / `ParameterBundle` + `ParameterServer` (OSC)** for live runtime controls.
- GUI sends OSC updates; engine reads parameters in a RT-safe way (audio thread reads, heavy work on main/control thread).
- This control-plane contract is intended to **survive the later pipeline refactor**.

### Python Entry Point – `runRealtime.py` (Archived)

> **Archived:** `runRealtime.py` was removed in Phase 6. The canonical entrypoint is now the `spatialroot_realtime` binary (CLI) and the ImGui GUI.

- **`runRealtime.py`** at the project root mirrors `runPipeline.py` — it
  accepts the **same inputs** (ADM WAV file or LUSID package directory +
  speaker layout) and runs the **same preprocessing pipeline** (ADM extract
  → parse to LUSID → write scene.lusid.json).
- For **ADM sources**: preprocessing writes scene.lusid.json only (no stem
  splitting), then launches the C++ engine with `--adm` pointing to the
  original multichannel WAV for direct streaming.
- For **LUSID packages**: validates and launches with `--sources` pointing
  to the mono files folder.
- Two pipeline entry points:
  - `run_realtime_from_ADM(source_adm, layout)` — ADM preprocessing + direct streaming
  - `run_realtime_from_LUSID(package_dir, layout)` — direct launch from mono files
- CLI uses `checkSourceType()` to auto-detect ADM vs LUSID input, same
  pattern as `runPipeline.py`.
- No `--channels` parameter — channel count is derived from the speaker
  layout by the C++ engine's `Spatializer::init()`.
- Keeps everything **segmented** — the offline pipeline is never touched, and
  `runRealtime.py` can be debugged independently.
- The Qt GUI will call `runRealtime.py` via `QProcess` (same pattern as
  `pipeline_runner.py` calls `runPipeline.py`).

### Target Milestone – Replicate Pipeline in Real-Time

The first working version must:

1. Accept the same inputs as the offline pipeline: **ADM WAV file** or
   **LUSID package directory** + speaker layout JSON. Run the same
   preprocessing (ADM extract → parse → package) before launching.
2. Parse the LUSID scene (reuse existing `LUSID/` Python package — this part
   is straightforward and safe).
3. Stream the mono stems from disk (double-buffered, real-time safe).
4. Spatialize with DBAP in the AlloLib audio callback (reusing proven gain
   math from `SpatialRenderer.cpp`).
5. Route LFE to subwoofer channels (same logic as offline).
6. Output to hardware speakers via AlloLib AudioIO.
7. (Archived; removed in Phase 6) Be launchable from `runRealtime.py` and from the Qt GUI.

This effectively **replicates the offline pipeline but plays back in
real-time** instead of writing a WAV file.

### Agent Implementation Order

Based on the architecture's data-flow dependencies, the planned order is:

| Phase | Agent(s)                  | Why this order                                        | Status      |
| ----- | ------------------------- | ----------------------------------------------------- | ----------- |
| 1     | **Backend Adapter**       | Need audio output before anything else is audible     | ✅ Complete |
| 2     | **Streaming**             | Need audio data to feed the mixer                     | ✅ Complete |
| 3     | **Pose and Control**      | Need positions before spatialization                  | ✅ Complete |
| 4     | **Spatializer (DBAP)**    | Core mixing — depends on 1-3                          | ✅ Complete |
| —     | **ADM Direct Streaming**  | Optimization: skip stem splitting for ADM sources     | ✅ Complete |
| 5     | **LFE Router**            | ~~Runs in audio callback after spatializer~~          | ⏭️ Skipped  |
| 6     | **Compensation and Gain** | Loudspeaker/sub mix sliders + focus auto-compensation | ✅ Complete |
| 7     | **Output Remap**          | Final channel shuffle before hardware                 | ✅ Complete |
| —     | **Audio Scan Toggle**     | `scan_audio=False` default in `runRealtime.py`        | ✅ Complete |
| 8     | **Threading and Safety**  | Harden all inter-thread communication                 | ✅ Complete |

9 - update init.sh and files in src/config to handle the updated realtime engine and tooling. ✅ Complete
| 10 | **GUI Agent** | (Archived prototype) PySide6 realtimeGUI + QProcess + OSC/ParameterServer | ✅ Complete |

11 - update main project read me and relevant documentation - in progress
12 - polish tasks:

- default folder for audio - sourceData ✅ Complete
- default speaker layout drop down selections. -- translab and allosphere - based on offline gui implementaiton. ✅ Complete
- default remap CSV dropdown with Allosphere example ✅ Complete

> **Note:** Phases 1-4 together form the minimum audible prototype (sound
> comes out of speakers). Phases 5-7 add correctness. Phase 8 hardens
> reliability. Phase 9 adds the user interface.

### Next Major Task (after Phase 10 prototype): Pipeline Refactor (C++-first realtime)

✅ **Completed in Phase 6 (2026-03-31).** The C++ realtime executable is the canonical entrypoint, and Python-era wrapper/tooling and PySide6 GUI paths are removed.

### Phase 10 Completion Log (GUI Agent) — ✅ Complete (Feb 26 2026)

**Design doc:** `internalDocsMD/Realtime_Engine/agentDocs/agent_gui_UPDATED_v3.md`
**AlloLib IPC reference:** `internalDocsMD/Realtime_Engine/agentDocs/allolib_parameters_reference.md`

#### Files to modify (C++ engine + Python)

| File                                                    | Change                                                                                                                              |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | Add `std::atomic<bool> paused{false}` + doc comment                                                                                 |
| `spatial_engine/realtimeEngine/src/main.cpp`            | Add ParameterServer, 6 al::Parameter objects, registerChangeCallbacks, `--osc_port` CLI flag, `pendingAutoComp` flag, shutdown call |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Add pause guard at top of `processBlock()`                                                                                          |
| `runRealtime.py`                                        | Add `remap_csv` + `osc_port` args to `_launch_realtime_engine()`; update CLI parsing                                                |
| `requirements.txt`                                      | Add `python-osc`                                                                                                                    |

#### Implementation order (within Phase 10)

1. **C++ engine changes first** — `RealtimeTypes.hpp` + `RealtimeBackend.hpp` + `main.cpp` (ParameterServer + pause). Build and verify OSC control works from CLI (`oscsend` or python-osc test script).
2. **`runRealtime.py` updates** — add `--remap` and `--osc_port` passthrough. Test CLI.
3. **`RealtimeRunner` + `RealtimeConfig`** — QProcess wrapper, OSC sender, state machine. Test independently.
4. **Panels** — InputPanel, TransportPanel, ControlsPanel, LogPanel (in that order — each adds on top of the previous).
5. **`RealtimeWindow` + `realtimeMain.py`** — assemble panels, wire signals, launch.
6. **End-to-end test** against testing checklist in `agent_gui_UPDATED_v3.md §9`.

#### What was built

**C++ engine changes:**

| File                                                    | Change                                                                                                                                                                                                                                                                                                               |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | Added `std::atomic<bool> paused{false}` to `RealtimeConfig` with full threading doc comment and memory-ordering table entry                                                                                                                                                                                          |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Added pause guard at top of `processBlock()` — relaxed load, zero channels + early return, RT-safe                                                                                                                                                                                                                   |
| `spatial_engine/realtimeEngine/src/main.cpp`            | Phase 10 banner; `al::Parameter` × 4 + `al::ParameterBool` × 2; `al::ParameterServer` on `--osc_port` (default 9009); `registerChangeCallback` for all 6 params; `pendingAutoComp` flag + main-loop consumer; `--focus` CLI flag; `paramServer.stopServer()` first in shutdown; status line now shows PAUSED/PLAYING |

**Python changes:**

| File               | Change                                                                                                                                                                                                                                         |
| ------------------ | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `runRealtime.py`   | `remap_csv=None` + `osc_port=9009` kwargs in `_launch_realtime_engine()`; `--remap` + `--osc_port` appended to `cmd`; threaded through `run_realtime_from_ADM()` + `run_realtime_from_LUSID()`; CLI `--remap` / `--osc_port` named-arg parsing |
| `requirements.txt` | Added `python-osc`                                                                                                                                                                                                                             |

**GUI files created:**

| File                                                        | Purpose                                                                                                                                                                                  |
| ----------------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `realtimeMain.py`                                           | Project-root launcher — loads `gui/styles.qss`, creates `RealtimeWindow`, runs `QApplication`                                                                                            |
| `gui/realtimeGUI/__init__.py`                               | Package marker (one line)                                                                                                                                                                |
| `gui/realtimeGUI/realtimeGUI.py`                            | `RealtimeWindow(QMainWindow)` — header bar, scroll area, assembles all four panels, wires runner signals                                                                                 |
| `gui/realtimeGUI/realtime_runner.py`                        | `RealtimeConfig` dataclass; `RealtimeRunnerState` enum; `DebouncedOSCSender` (40 ms QTimer); `RealtimeRunner(QObject)` — QProcess + `SimpleUDPClient`, full state machine, graceful stop |
| `gui/realtimeGUI/realtime_panels/__init__.py`               | Package marker                                                                                                                                                                           |
| `gui/realtimeGUI/realtime_panels/RealtimeInputPanel.py`     | Source/layout/remap/buffer/scan inputs; inline ADM vs LUSID auto-detection hint; browse dialogs                                                                                          |
| `gui/realtimeGUI/realtime_panels/RealtimeTransportPanel.py` | Start/Stop/Kill/Restart/Pause/Play; colour-coded status pill; Copy Command; OSC port label                                                                                               |
| `gui/realtimeGUI/realtime_panels/RealtimeControlsPanel.py`  | `_ParamRow` (slider ↔ spinbox, debounced vs immediate sends); gain/focus/spkMix/subMix rows + auto-comp checkbox; `reset_to_defaults()` on each Start                                    |
| `gui/realtimeGUI/realtime_panels/RealtimeLogPanel.py`       | `QPlainTextEdit` (monospace, read-only); 2000-line cap; auto-scroll; Clear button                                                                                                        |

**Build result:** `make -j4` → zero errors. `--help` output confirms `--focus` and `--osc_port` flags. All Python imports verified clean.

---

### Phase 10 Bug-Fix — OSC Runtime Controls (Feb 26 2026)

**Symptom:** "sliders move but values are not updated" + Pause/Play had no
effect on a freshly launched engine.

**Root cause:** `_on_started` (fired by `QProcess::started`) promoted the
runner to `RUNNING` the moment `runRealtime.py` spawned as a Python process.
`runRealtime.py` does scene loading, WAV opening, and C++ binary exec before
`al::ParameterServer` calls `bind()` on port 9009. OSC messages sent in that
window were silently dropped (UDP, no ACK, no error).

**Fix — 3 files:**

| File                                                       | Change                                                                                                                                       |
| ---------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `gui/realtimeGUI/realtime_runner.py`                       | `_on_started` stays in `LAUNCHING`; `_on_stdout` probes for `"ParameterServer listening"`; on match → `RUNNING` + emit `engine_ready` Signal |
| `gui/realtimeGUI/realtime_panels/RealtimeControlsPanel.py` | Added `flush_to_osc()` — sends all 5 current control values as immediate OSC                                                                 |
| `gui/realtimeGUI/realtimeGUI.py`                           | `runner.engine_ready.connect(controls_panel.flush_to_osc)`                                                                                   |

**Sentinel string** (printed by `main.cpp` after `paramServer.serverRunning()` succeeds):

```
[Main] ParameterServer listening on 127.0.0.1:<port>
```

**State machine change:**

```
IDLE → LAUNCHING  (on Start button)
LAUNCHING → RUNNING  (on stdout sentinel — NOT on QProcess::started)
RUNNING → PAUSED  (on Pause click)
PAUSED → RUNNING  (on Play click)
RUNNING/PAUSED → EXITED/ERROR  (on process finish)
```

**New invariant (Invariant 7):** The GUI runner must not send any OSC message
while in `LAUNCHING` state. `send_osc()` and `schedule_osc()` enforce this
via the existing state guard. See `agent_threading_and_safety.md §OSC Runtime
Parameter Delivery` for the full threading analysis.

---

### Phase 1 Completion Log (Backend Adapter)

**Files created:**

| File                                                    | Purpose                                                                                                                                                                             |
| ------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/CMakeLists.txt`          | Build system — links AlloLib + Gamma, shares `JSONLoader`/`LayoutLoader`/`WavUtils` from `../src/`                                                                                  |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | Shared data types: `RealtimeConfig` (device settings, paths, atomic gain/playback flags) and `EngineState` (frame counter, playback time, CPU load, xrun count)                     |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Agent 8 implementation — wraps AlloLib `AudioIO` with `init()`/`start()`/`stop()`/`shutdown()` lifecycle, static C-style callback dispatches to `processBlock()`, CPU load clamping |
| `spatial_engine/realtimeEngine/src/main.cpp`            | CLI entry point — parses `--layout`/`--scene`/`--sources` + optional args, runs monitoring loop with status display, handles SIGINT for clean shutdown                              |
| `runRealtime.py`                                        | (Archived; removed in Phase 6) Python launcher used during the prototype era to orchestrate preprocessing + launch the C++ binary.                                                  |

**Build & test results:**

- CMake configures successfully (AlloLib + Gamma link)
- `make -j4` compiles with zero errors
- Binary runs, opens audio device (2-channel test), streams silence for 3 seconds
- SIGINT handler triggers clean shutdown (stop → close → exit 0)
- Frame counter advances correctly (~144k frames in 3s at 48kHz)
- CPU load reports 0.0% (silence — trivial callback)

**What the next phase gets:**

- A working audio callback that currently outputs silence
- `processBlock(AudioIOData&)` is the insertion point for all future agents
- `RealtimeConfig` and `EngineState` are the shared state structs
- (Archived; removed in Phase 6) `runRealtime.py` could be called from the PySide6 GUI in the prototype era

### Phase 2 Completion Log (Streaming Agent)

**Files created/modified:**

| File                                                    | Action       | Purpose                                                                                                                                                                                                           |
| ------------------------------------------------------- | ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`       | **Created**  | Agent 1 — double-buffered per-source WAV streaming with background loader thread. Each source gets two 5-second buffers (240k frames at 48kHz). Lock-free audio-thread reads via atomic buffer states.            |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | **Modified** | Added `Streaming*` pointer and `setStreaming()`/`cacheSourceNames()` methods. `processBlock()` now reads mono blocks from each source, sums with 1/N normalization × master gain, mirrors to all output channels. |
| `spatial_engine/realtimeEngine/src/main.cpp`            | **Modified** | Now loads LUSID scene via `JSONLoader::loadLusidScene()`, creates `Streaming`, opens all source WAVs, wires into backend, starts loader thread before audio, shuts down in correct order (backend → streaming).   |
| `internalDocsMD/AGENTS.md`                              | **Modified** | Added "Real-Time Spatial Audio Engine" section with architecture, file descriptions, build instructions, streaming design, run example. Updated file structure tree and Future Work.                              |

**Design decisions:**

- **libsndfile access**: Uses `<sndfile.h>` directly (same as `WavUtils.cpp`). Available transitively through Gamma → `find_package(LibSndFile QUIET)` → exported via PUBLIC link. No new dependencies.
- **Per-source double buffers**: Each source gets independent buffers (not a shared multichannel buffer). Simpler, avoids cross-source contention.
- **5-second chunk size** (240k frames): Balances memory (~1.8 MB per source, ~63 MB for 35 sources) against seek frequency. Only needs ~20 loads per source over a 98-second piece.
- **50% preload threshold**: Background thread starts loading the next chunk when playback passes the halfway point of the active buffer. Gives 2.5 seconds of runway before the buffer switch.
- **Single loader thread**: One thread services all sources sequentially. At 2ms poll interval and ~35 sources, worst-case full scan takes <1ms. Sufficient for current source count.
- **Mutable atomics for buffer state**: `stateA`, `stateB`, `activeBuffer` are `mutable` in `SourceStream` because the audio thread may switch buffers during a logically-const `getSample()` call.

**Build & test results:**

- `cmake .. && make -j4` compiles with zero errors
- 35 sources loaded successfully (34 audio objects + LFE), each ~98 seconds (4,703,695 frames at 48kHz)
- Ran for 69.7 seconds with 2 output channels, --gain 0.1
- CPU load: 0.0% (mono sum of 35 sources + memcpy is trivially fast)
- No xruns, no underruns, no file handle leaks
- Clean SIGINT shutdown: backend stops → streaming agent closes all SNDFILE handles → exit 0
- Background loader thread joins cleanly

**What the next phase gets:**

- `StreamingAgent::getBlock(sourceName, startFrame, numFrames, outBuffer)` — lock-free mono block read from any source
- `StreamingAgent::sourceNames()` — list of all loaded source keys
- `StreamingAgent::isLFE(sourceName)` — LFE detection for routing in Phase 5
- The audio callback in `processBlock()` is the insertion point for Pose Agent (Phase 3) and Spatializer Agent (Phase 4)
- Current mono mix (equal-sum to all channels) will be replaced by per-source DBAP panning in Phase 4

### Phase 3 Completion Log (Pose — Source Position Interpolation)

**Files created/modified:**

| File                                                    | Action       | Purpose                                                                                                                                                                                                                                                            |
| ------------------------------------------------------- | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `spatial_engine/realtimeEngine/src/Pose.hpp`            | **Created**  | Agent 2 — source position interpolation and layout-aware transforms. SLERP between LUSID keyframes, elevation sanitization (3 modes), DBAP coordinate transform. Outputs `SourcePose` vector per audio block.                                                      |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | **Modified** | Added `ElevationMode` enum (Clamp, RescaleAtmosUp, RescaleFullSphere) and `elevationMode` field to `RealtimeConfig`.                                                                                                                                               |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | **Modified** | Added `Pose*` pointer and `setPose()` method. `processBlock()` now calls `mPose->computePositions(blockCenterSec)` at step 1.5, computing per-source positions before the audio mixing loop. Positions are computed but not yet used for spatialization (Phase 4). |
| `spatial_engine/realtimeEngine/src/main.cpp`            | **Modified** | Now loads speaker layout via `LayoutLoader::loadLayout()`, creates `Pose`, calls `pose.loadScene(scene, layout)` to analyze layout and store keyframes, wires Pose into backend via `backend.setPose(&pose)`. Updated to Phase 3 banner and help text.             |
| `internalDocsMD/AGENTS.md`                              | **Modified** | Updated Phase 3 row to ✅ Complete, added `Pose.hpp` description to Key Files section.                                                                                                                                                                             |

**Design decisions:**

- **SLERP interpolation** (not linear Cartesian): Prevents direction vectors from passing through near-zero magnitude when keyframes are far apart on the sphere. Adapted from `SpatialRenderer::slerpDir()`.
- **Three elevation modes**: `Clamp` (hard clip to layout bounds), `RescaleAtmosUp` (default, maps [0,π/2] → layout), `RescaleFullSphere` (maps [-π/2,π/2] → layout). Identical to offline renderer.
- **DBAP coordinate transform**: Our system (y-forward, x-right, z-up) → AlloLib DBAP internal does `Vec3d(pos.x, -pos.z, pos.y)`, so we pre-compensate with `(x, z, -y)`. Adapted from `SpatialRenderer::directionToDBAPPosition()`.
- **Layout radius = median speaker distance**: Same calculation as offline renderer constructor. Used to scale unit direction vectors to DBAP positions at the speaker ring distance.
- **2D detection**: If speaker elevation span < 3°, all directions are flattened to the horizontal plane (z=0). Same threshold as offline renderer.
- **Fallback chain for degenerate directions**: (1) normalize raw interpolation, (2) last-good cached direction, (3) nearest keyframe direction, (4) front (0,1,0). Same logic as `SpatialRenderer::safeDirForSource()`.
- **Block-center sampling**: Position is computed at the center of each audio block (`frameCounter + bufferSize/2`) per design doc specification.
- **Pre-allocated output vector**: `mPoses` and `mSourceOrder` are allocated once at `loadScene()` time. `computePositions()` updates entries in-place — no allocation on the audio thread.
- **`std::map` for keyframe lookup**: Acceptable because `computePositions()` iterates sequentially through the pre-built source order, not doing random lookups. The map is read-only during playback.

**Build & test results:**

- `cmake --build .` compiles with zero errors
- 35 sources loaded, 54 speakers + 1 subwoofer in AlloSphere layout
- Layout analysis: median radius 5.856m, elevation [-27.7°, 32.7°], 3D mode
- Ran for 8.6 seconds with 2 output channels, --gain 0.3
- CPU load: 0.0% (SLERP + transforms for 35 sources add negligible overhead)
- No xruns, no crashes
- Clean shutdown via SIGINT

**What the next phase gets:**

- `Pose::computePositions(blockCenterTimeSec)` — called once per audio block, updates internal pose vector
- `Pose::getPoses()` → `const vector<SourcePose>&` — per-source `{name, position, isLFE, isValid}` ready for DBAP
- Positions are in DBAP-ready coordinates (pre-transformed), scaled to layout radius
- LFE sources have `isLFE=true` → route to subwoofer channels, skip DBAP
- The spatializer (Phase 4) will iterate `getPoses()` and compute per-speaker gain coefficients

### Phase 4 Completion Log (Spatializer — DBAP Spatial Panning)

**Files created/modified:**

| File                                                    | Action       | Purpose                                                                                                                                                                                                                                                                          |
| ------------------------------------------------------- | ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`     | **Created**  | Agent 3 — DBAP spatial audio panning. Builds `al::Speakers` from layout, computes output channels from layout, creates `al::Dbap`, renders all sources via internal render buffer, routes LFE to subwoofers. Internal render buffer is the future Channel Remap insertion point. |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | **Modified** | `outputChannels` default changed from 60 → 0. Now computed from speaker layout by `Spatializer::init()`. Added documentation comment explaining the formula.                                                                                                                     |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | **Modified** | Added `Spatializer*` pointer and `setSpatializer()` method. `processBlock()` now calls Spatializer `renderBlock()` instead of the Phase 2 mono-mix fallback. Pipeline: zero outputs → Pose positions → Spatializer render → update state → CPU monitor.                          |
| `spatial_engine/realtimeEngine/src/main.cpp`            | **Modified** | Removed `--channels` CLI argument. Creates `Spatializer`, calls `init(layout)` which computes `outputChannels` into config. Backend reads the layout-derived channel count. Updated help text, banner, and config printout.                                                      |
| `internalDocsMD/AGENTS.md`                              | **Modified** | Updated Phase 4 row to ✅ Complete, added `Spatializer.hpp` description, updated run example (no `--channels`), updated file tree.                                                                                                                                               |

**Design decisions:**

- **Layout-derived output channels** (not user-specified): `outputChannels = max(numSpeakers-1, max(subwooferDeviceChannels)) + 1`. Same formula as offline `SpatialRenderer.cpp` (lines 837-842). For the Allosphere layout: `max(53, 55) + 1 = 56`. Removed the `--channels` CLI flag entirely.
- **Internal render buffer (`mRenderIO`)**: All rendering (DBAP + LFE) goes into an `al::AudioIOData` buffer sized to `outputChannels`. The copy step from render buffer → real AudioIO is the future **Channel Remap insertion point**, where logical render channels will be mapped to physical device outputs (like `channelMapping.hpp`'s `defaultChannelMap` does for the Allosphere ADM player). Currently identity mapping.
- **Nothing hardcoded to any layout**: No Allosphere-specific values anywhere. Channel count, speaker positions, subwoofer channels — all derived from the layout JSON at runtime. Works with any speaker layout.
- **Consecutive 0-based speaker channels**: Same as offline renderer. `al::Speaker` gets indices 0..N-1 for N speakers. The hardware `deviceChannel` numbers from the layout JSON (1-based, non-consecutive with gaps) are only used for subwoofer routing. Future Channel Remap will handle mapping render channels to hardware channels.
- **LFE into render buffer** (not directly to AudioIO): LFE sources write into `mRenderIO` subwoofer channels, so all audio flows through the same remap point. Consistent with the design where the copy step is the single point of channel routing.
- **Sub compensation**: `masterGain * 0.95 / numSubwoofers` — same formula as offline `SpatialRenderer::renderPerBlock()`.
- **DBAP panning**: Uses `al::Dbap::renderBuffer()` directly. Source audio is pre-multiplied by `masterGain` before DBAP accumulates into speaker channels. Focus parameter is configurable via `RealtimeConfig::dbapFocus`.

**Build & test results:**

- `cmake .. && make -j4` compiles with zero errors
- 35 sources loaded, 54 speakers + 1 subwoofer in AlloSphere layout
- Output channels derived from layout: 56 (speakers 0-53, sub at deviceChannel 55)
- AudioIO opened with 56 output channels
- Internal render buffer: 56 channels × 512 frames
- Ran for 6 seconds with `--gain 0.1`
- CPU load: 0.0% (DBAP for 35 sources × 54 speakers + LFE is trivially fast)
- No xruns, no assertion failures, no crashes
- Clean shutdown via SIGINT/kill

**What the next phase gets:**

- `Spatializer::renderBlock(io, streaming, poses, frame, numFrames)` — renders all sources into the real AudioIO output
- Layout-derived `config.outputChannels` — backend opens AudioIO with the right channel count automatically
- Internal render buffer (`mRenderIO`) — future Channel Remap agent replaces the identity copy loop with a mapping table
- LFE routing already handled (subwoofer channels from layout, no DBAP on LFE sources)
- Phases 1-4 together form the **minimum audible spatial prototype** — sound comes out of the correct speakers based on LUSID scene positions

### ADM Direct Streaming Completion Log (Optimization — Skip Stem Splitting)

**Files created/modified:**

| File                                                       | Action       | Purpose                                                                                                                                                                                                                                                                                                   |
| ---------------------------------------------------------- | ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/MultichannelReader.hpp` | **Created**  | Shared multichannel WAV reader. Opens one SNDFILE\*, pre-allocates interleaved buffer (chunkFrames × numChannels), de-interleaves channels into per-source SourceStream double buffers. Method implementations in Streaming.hpp (after SourceStream definition).                                          |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`          | **Modified** | Added `loadSceneFromADM()` (creates buffer-only SourceStreams, maps to MultichannelReader), `loaderWorkerMultichannel()` (one bulk read fills all mapped streams), `SourceStream::initBuffersOnly()` (allocates buffers without file handle), `parseChannelIndex()` (source key → 0-based channel index). |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`      | **Modified** | Added `std::string admFile` to `RealtimeConfig` for ADM direct streaming path.                                                                                                                                                                                                                            |
| `spatial_engine/realtimeEngine/src/main.cpp`               | **Modified** | Added `--adm <path>` CLI flag (mutually exclusive with `--sources`). Dispatches to `loadSceneFromADM()` or `loadScene()`. Updated help text.                                                                                                                                                              |
| `src/packageADM/packageForRender.py`                       | **Modified** | Added `writeSceneOnly()` function — writes scene.lusid.json without splitting stems.                                                                                                                                                                                                                      |
| `runRealtime.py`                                           | **Modified** | `_launch_realtime_engine()` now accepts `adm_file` parameter (uses `--adm` flag). `run_realtime_from_ADM()` calls `writeSceneOnly()` instead of `packageForRender()`, skipping stem splitting entirely.                                                                                                   |
| `internalDocsMD/AGENTS.md`                                 | **Modified** | Updated all realtime engine descriptions for dual-mode streaming, added MultichannelReader.hpp docs, updated run examples.                                                                                                                                                                                |

**Design decisions:**

- **Shared MultichannelReader (Option A)**: One SNDFILE\*, one interleaved buffer (~44MB for 48ch), shared across all sources. Much more memory-efficient than per-source multichannel handles (Option B, rejected).
- **Channel mapping derived in C++**: Source key `"11.1"` → extract number before dot → subtract 1 → channel index 10. `"LFE"` → hardcoded index 3 (standard ADM LFE position). No LUSID schema changes needed.
- **Audio thread completely untouched**: SourceStream's double-buffer + getSample/getBlock are identical in both modes. The audio callback doesn't know or care whether data came from mono files or de-interleaved multichannel.
- **Mono path preserved**: `--sources` still works exactly as before (zero regression risk).
- **Separate loader workers**: `loaderWorkerMono()` (original per-source iteration) and `loaderWorkerMultichannel()` (one bulk read per cycle) avoid any conditional branching in the hot loop.
- **`writeSceneOnly()`**: Factored out of `packageForRender()` to write just the scene.lusid.json without stem splitting. Used by the real-time ADM path; offline pipeline still uses full `packageForRender()`.

**Build & test results:**

- `cmake .. && make -j4` compiles with zero errors
- `--help` shows new `--adm` flag and updated usage
- Error handling: `--adm` + `--sources` together rejected; neither provided rejected
- LUSID package path (mono): ✅ No regression — 78 sources, allosphere layout, 56 output channels
- ADM WAV path (direct streaming): ✅ Full pipeline works — SWALE-ATMOS-LFE.wav, 24 sources mapped from 48ch ADM, translab layout, 18 output channels
- ADM pipeline skips Step 4 (stem splitting) — saves ~30-60 seconds and ~2.9GB disk I/O

### Phase 5 Skip Log (LFE Router — Skipped)

**Decision:** Phase 5 (LFE Router) is **skipped** for the v1 prototype.

**Rationale:** LFE routing is already fully implemented inside `Spatializer.hpp`
(Phase 4). LFE sources are identified by `pose.isLFE`, bypass DBAP entirely,
and route directly to subwoofer channels from the speaker layout with
`subGain = masterGain * 0.95 / numSubwoofers` — the same formula as the offline
renderer. This pass-through approach matches the offline pipeline's behavior
and is sufficient for the current prototype.

The `agent_lfe_router.md` design document describes bass management features
(crossover filtering, extracting LF content from main speakers, phase alignment)
that are **not needed** for replicating the offline pipeline in real-time. These
are deferred to the "Possible Future Development" section below as lowest
priority / experimental.

**What the next phase gets (unchanged from Phase 4):**

- LFE sources routed to subwoofer channels (already implemented in Spatializer)
- No new files created or modified
- No behavioral changes

---

### Phase 6 Completion Log (Compensation and Gain)

**Files modified:**

| File                                                | Action       | Purpose                                                                                                                                                                                                                                                                                                                                                                                                     |
| --------------------------------------------------- | ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp` | **Modified** | Added `isSubwooferChannel()` private helper. Added Phase 6 mix-trim pass in `renderBlock()` — applies `loudspeakerMix` to all non-sub channels and `subMix` to sub channels in `mRenderIO` after DBAP+LFE, before copy-to-output. Added `computeFocusCompensation()` public method. Added `mLayoutRadius` member, populated at `init()` time using the median speaker radius. Added `#include <algorithm>`. |
| `spatial_engine/realtimeEngine/src/main.cpp`        | **Modified** | Updated banner to Phase 6. After Spatializer init, calls `computeFocusCompensation()` if `--auto_compensation` is active. Prints Phase 6 gain summary (loudspeakerMix, subMix in dB). Updated help text and comment block.                                                                                                                                                                                  |

**Design decisions:**

- **Mix trims applied to `mRenderIO`** — after all DBAP and LFE rendering, before the copy-to-output step. This is the precise location specified in the design doc and means `masterGain` (already baked into DBAP gains per source) and the post-DBAP trims are independent, non-interfering scalars.
- **Unity guard (`!= 1.0f`)** — the trim loops are entirely skipped at the default 0 dB setting. Zero additional cost for the common case.
- **`isSubwooferChannel()` helper** — a linear scan over `mSubwooferChannels` (typically 1-2 entries). Called O(renderChannels) times per block only when `spkMix != 1.0f`, which is negligible.
- **`computeFocusCompensation()` runs on the main/control thread only** — it allocates temporary `al::AudioIOData` and `al::Dbap` objects for an offline impulse test, computes the RMS power ratio between focus=current and focus=0, then writes the square-root amplitude scalar to `mConfig.loudspeakerMix`. It must not be called from the audio callback.
- **Reference position = (0, mLayoutRadius, 0)** — front-center at the layout radius, in DBAP-ready coordinates. Consistent with how Pose transforms directions before handing to DBAP.
- **Compensation clamped to ±10 dB** (0.316–3.162 linear) — matches the design doc range and guards against division-by-near-zero at extreme focus values.
- **`RealtimeConfig` already had the three atomics** (`loudspeakerMix`, `subMix`, `focusAutoCompensation`) and `main.cpp` already parsed the CLI flags from a previous forward-looking commit. Phase 6 completes the implementation by wiring them into the audio path.

**Build & test results:**

- `cmake --build . -j4` compiles with zero errors
- `--speaker_mix 6` → `loudspeakerMix=1.995` (≈+6 dB), sub unchanged
- `--sub_mix -6` → `subMix=0.501` (≈-6 dB), mains unchanged
- Both sliders at 0 dB → unity guard fires, no extra computation
- `--auto_compensation` → `computeFocusCompensation()` called at startup, logs loudspeakerMix in dB

**What the next phase gets:**

- `mConfig.loudspeakerMix` and `mConfig.subMix` are live atomics — any agent (GUI, control thread) can update them at runtime and the audio callback picks them up within one block
- `spatializer.computeFocusCompensation()` is callable from any non-audio-thread context when focus changes
- The copy-from-render-to-output section in `renderBlock()` is now cleanly separated from the gain application section, making it the clear insertion point for the Channel Remap agent (Phase 7)

---

### Phase 7 Completion Log (Output Remap)

**Files created/modified:**

| File                                                | Action       | Purpose                                                                                                                                                                                                                                                                                          |
| --------------------------------------------------- | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `spatial_engine/realtimeEngine/src/OutputRemap.hpp` | **Created**  | CSV-based channel remap table. Parses a `layout,device` CSV once at startup. Stores a compact `std::vector<RemapEntry>` for the audio thread to iterate. Detects identity maps and sets the `identity=true` fast-path flag. All out-of-range rows are dropped and logged once (never per-frame). |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp` | **Modified** | Added `#include "OutputRemap.hpp"`. Added `mRemap` (const pointer, default nullptr). Added `setRemap()` method. Replaced the old identity-copy loop in `renderBlock()` with a two-branch remap: identity fast-path (bit-identical to Phase 6) or accumulate-via-entries-list remap path.         |
| `spatial_engine/realtimeEngine/src/main.cpp`        | **Modified** | Added `#include "OutputRemap.hpp"`. Added `--remap <path>` CLI flag + help text. After Spatializer init, creates an `OutputRemap`, calls `load()` with render and device channel counts, passes `&outputRemap` to `spatializer.setRemap()`. Updated banner to Phase 7 and comment block.         |

**Design decisions:**

- **CSV schema from the spec**: `layout,device` headers required (case-insensitive), 0-based indices, extra columns ignored, `#` comment lines and empty lines skipped. Matches `agent_output_remap.md` exactly.
- **Accumulate (not replace)**: multiple `layout` entries mapping to the same `device` channel are summed, matching the AlloApp reference behavior in `channelMapping.hpp`.
- **Identity detection**: after parsing, `checkIdentity()` verifies that every entry has `layout == device`, there are no duplicates, and coverage is complete (exactly `renderChannels` entries). Only then is the fast-path flag set.
- **No ownership transfer**: `OutputRemap` is stack-allocated in `main()` and passed as a `const*`. The Spatializer never deletes it. The object lives for the entire playback duration.
- **No `--remap` → zero overhead**: when the flag is omitted, `setRemap()` is never called (`mRemap` stays `nullptr`), the `identity` branch fires, and the loop is identical to pre-Phase-7 code.
- **Device channel count for range-checking**: `load()` is called with `config.outputChannels` for both `renderChannels` and `deviceChannels`. Since the engine currently uses identity hardware mapping, both counts are the same layout-derived value. Phase 8 or the Allosphere-specific CSV will use the real device channel count.
- **Allosphere usage**: to apply the Allosphere-specific remap, generate a CSV from `channelMapping.hpp`'s `defaultChannelMap` (54 entries + sub) and pass it with `--remap allosphere.csv`. No code change needed.

**Build & test results:**

- `cmake --build . -j4` compiles with zero errors
- No `--remap` → identity fast-path, output bit-identical to Phase 6
- `--remap identity.csv` (all `layout==device`) → identity detected, fast-path active
- `--remap swap.csv` (two channels swapped) → remap path active, entries logged

**What the next phase gets:**

- `Spatializer::setRemap(const OutputRemap*)` — can be called at any time before audio starts
- `OutputRemap::load(path, renderCh, deviceCh)` — returns true on success, logs warnings on bad rows
- The audio output is now fully configurable for any hardware channel layout via a two-column CSV
- Phase 8 (Threading and Safety) can harden the existing atomic usage; no new remap-related thread concerns (the table is immutable during playback)

---

## Possible Future Development

> Items below are not planned for the v1 prototype. They are listed in
> priority order for future consideration.

### X. Audio driver and OS compatibility

Add a tiny “device sanity toolkit” (non-RT):

Print: device name, host API, output channel count

“Channel ID” test mode (pulse each layout channel)

explore potential issues with ASIO (and WASAPI/DirectSound)

### 1. Single-Keyframe Pose Optimization (Medium Priority)

**Analysis:** `Pose::computePositions()` unconditionally recomputes the full
SLERP → sanitize → coordinate-transform pipeline for every non-LFE source on
every audio block, even when the source's position hasn't changed. This
includes sources with:

- **A single keyframe** (e.g., DirectSpeaker bed channels, which are inherently
  static). These go through `interpolateDirRaw()` → `safeDirForSource()` →
  `sanitizeDirForLayout()` → `directionToDBAPPosition()` every block despite
  always returning the same position.
- **Multiple keyframes but currently in a hold segment** (before the first
  keyframe or after the last, where the result is clamped and constant).

**Current impact:** Negligible — Phase 3 testing showed 0.0% CPU for 35 sources.
The SLERP + trig is a handful of floating-point ops per source per block.

**Optimization (when needed):**

1. **Static source fast path:** At `loadScene()` time, detect sources with
   exactly 1 keyframe (or all keyframes at the same position). Pre-compute
   their DBAP-ready position once and store it directly in `mPoses[i]`.
   In `computePositions()`, skip these sources entirely.
2. **Time-segment caching:** Cache which keyframe segment (k1, k2) was used
   on the previous block. If the new block-center time is still in the same
   segment and the interpolation parameter `u` hasn't changed significantly
   (within epsilon), skip recomputation.

**Estimated benefit:** For typical ADM content (many static bed channels +
fewer moving objects), this could skip 50-80% of sources. Worth implementing
when source counts reach hundreds or when CPU headroom becomes tight.

### 2. Bass Management / LFE Crossover Filtering (Lowest Priority — Experimental)

**Description:** The `agent_lfe_router.md` document describes a full bass
management system with:

- Low-pass filtering (e.g., 80 Hz Butterworth) to extract LF content from main
  speaker channels and redirect to subwoofers
- Optional complementary high-pass on main channels to avoid LF doubling
- Phase-aligned crossover (Linkwitz-Riley option)
- Per-channel biquad filter states, pre-allocated for RT safety
- Configurable crossover frequency, additive vs. redirected bass modes

**Why deferred:** The current pass-through LFE routing (dedicated LFE sources →
subwoofer channels, no crossover) matches the offline pipeline exactly. Bass
management is a separate concern from spatial rendering — it's a playback
system feature that depends on the physical speaker setup (whether mains are
full-range or satellite + sub). Adding it would introduce complexity and
configuration surface area that isn't needed for the v1 goal of "replicate
the offline pipeline in real-time."

**If implemented later:** Create a new `LFERouter.hpp` agent that runs after
the Spatializer's render pass but before the copy-to-output step. It would
operate on the internal render buffer (`mRenderIO`) channels. The Spatializer's
existing LFE pass-through routing should remain as-is (it handles dedicated LFE
source content); bass management would be additive, extracting LF from the
main speaker channels.

---

## Critical Implementation Details for Future Context Windows

> **Archived:** These details were captured at the end of the ADM Direct Streaming implementation session. Much of the Python logic referenced here (e.g. `xml_etree_parser.py`, `runPipeline.py`) was entirely removed in Phase 6, moving entirely to the C++ `cult-transcoder` tool.

### Full LFE Channel Mapping Chain (Archived python reference)

The LFE channel mapping historically crossed two codebases and involved a hardcoded flag:

1. **Python LUSID parser** (`LUSID/src/xml_etree_parser.py` line 44 - REMOVED):
   `_DEV_LFE_HARDCODED = True` — when `True`, any DirectSpeaker at 1-based
   ADM channel 4 is tagged as LFE (function `_is_lfe_channel()`). When `False`,
   it falls back to checking `speakerLabel` for the substring "lfe".

2. **LUSID scene JSON output**: The LFE source gets key `"LFE"` (from the
   `LFENode(id=f"{group_id}.1")` construction at line 371). In the SWALE test
   file, the DirectSpeaker group containing channel 4 has `group_id = "4"`,
   so the LFE source key becomes `"4.1"` in the scene — but its **type** is
   `"lfe"` in the JSON, so the C++ engine identifies it as LFE.

3. **C++ `parseChannelIndex()`** (`Streaming.hpp`): For source key `"LFE"` or
   any source flagged as LFE, returns hardcoded index 3 (0-based ADM channel 4).
   For normal sources like `"11.1"`, extracts the number before the dot and
   subtracts 1 → channel index 10.

4. **C++ `Spatializer.hpp`**: LFE sources (identified by `pose.isLFE`) bypass
   DBAP entirely and route directly to subwoofer channels from the speaker layout.

**Implication**: Current Phase 6 logic using `cult-transcoder` now handles this C++-side. Legacy implications regarding `_DEV_LFE_HARDCODED` in python are archived.

### Shared Loaders — Offline ↔ Real-Time Code Sharing

The real-time engine compiles three `.cpp` files from the offline renderer's
`spatial_engine/src/` directory (see `realtimeEngine/CMakeLists.txt` lines 29-33):

```
add_executable(spatialroot_realtime
    src/main.cpp
    ../src/JSONLoader.cpp      # Parses scene.lusid.json → SpatialData struct
    ../src/LayoutLoader.cpp    # Parses speaker_layout.json → SpeakerLayout struct
    ../src/WavUtils.cpp        # WAV I/O (used for metadata only in realtime)
)
```

These are the **exact same source files** the offline renderer
(`spatial_engine/spatialRender/`) uses. Any changes to these shared files affect
both the offline and real-time pipelines. The offline renderer is at
`spatial_engine/spatialRender/build/spatialroot_spatial_render`.

### Circular Header Pattern (MultichannelReader ↔ Streaming)

`MultichannelReader.hpp` forward-declares `struct SourceStream` and declares
methods `deinterleaveInto(SourceStream&, ...)` and `zeroFillBuffer(SourceStream&, ...)`.
The **implementations** of these methods are placed at the very bottom of
`Streaming.hpp` (after `SourceStream` is fully defined) as inline free-standing
functions within the `MultichannelReader` class scope. This is standard C++ for
resolving circular dependencies between header-only types. If you move
`SourceStream` to its own header, the method impls should move with it.

### Known Bug — `runPipeline.py` Line 177 (Archived)

> **Archived:** `runPipeline.py` was removed in Phase 6.

In the `if __name__ == "__main__"` CLI block, the LUSID branch historically called an invalid parameter.
**Not an active issue** — `runPipeline.py` no longer exists.

### Audio Scan Toggle — `scan_audio` (✅ Implemented, Archived Phase 6)

The ADM real-time path (`run_realtime_from_ADM()`) previously ran a full
per-channel audio activity scan unconditionally at Step 2, adding ~14 seconds
of startup latency before the engine could launch.

**What changed (`runRealtime.py`):**

| Step | What it does                               | Default     |
| ---- | ------------------------------------------ | ----------- |
| 2a   | `exportAudioActivity()` — write JSON       | **skipped** |
| 2b   | `channelHasAudio()` — scan full WAV        | **skipped** |
| 2c   | Synthetic all-active dict passed to parser | **used**    |

- `run_realtime_from_ADM()` gains a `scan_audio=False` keyword argument.
- When `False` (the default): both scan calls are skipped. A synthetic
  `contains_audio_data` dict is built from `sf.info()` alone (no I/O beyond
  the file header), marking all channels active. Startup time drops by ~14s.
- When `True`: the original scan runs and writes `processedData/containsAudio.json`
  as before. Use this if the LUSID parser needs accurate silence data.
- CLI flag: append `--scan_audio` to the positional arguments to enable it.
  Absence of the flag means `False`.
- The LUSID path (`run_realtime_from_LUSID`) has no scan — unaffected.

**Synthetic fallback dict format** (matches `channelHasAudio()` return schema):

```python
{
    "sample_rate": <int>,
    "threshold_db": -100,
    "elapsed_seconds": 0.0,
    "channels": [
        {"channel_index": i, "rms_db": 0.0, "contains_audio": True}
        for i in range(num_channels)
    ]
}
```

---

### Phase 8 Completion Log — Threading and Safety (✅ Complete)

**Approach:** Full threading audit of all five agents. The engine was already
largely correct. Phase 8 replaced ambiguous comments with precise threading
contracts, added a hardened null-pointer guard, and produced the canonical
threading model documentation that future phases (GUI) must respect.

**No new runtime mechanisms were needed.** All synchronization was already in
place (atomics with correct memory orders, happens-before from `start()`,
double-buffer acquire/release pairs). Phase 8's contribution is the explicit,
auditable specification of what each thread owns and what the rules are.

#### Threading Model Reference Table

| Thread     | Owns (exclusive write)                                                                            | Read-only access                                                         | Sync mechanism                                               |
| ---------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------ | ------------------------------------------------------------ |
| **MAIN**   | Agent lifetimes; `mLoaderRunning` (→ false); startup setup                                        | `EngineState` atomics (for display)                                      | `memory_order_relaxed` on reads                              |
| **AUDIO**  | `mPoses`, `mLastGoodDir` (Pose); `mRenderIO`, `mSourceBuffer` (Spatializer); `EngineState` writes | `mConfig` atomics; `mStreams` (read-only); agent pointers                | `memory_order_relaxed` (sole writer for EngineState)         |
| **LOADER** | Inactive buffer slot of `SourceStream::bufferA/B`                                                 | `EngineState::frameCounter` (relaxed); `stateA/B` (acquire) to pick slot | acquire/release on `stateA/B`, `chunkStart*`, `validFrames*` |

#### Invariants Documented (now in RealtimeTypes.hpp)

1. Agent pointers in `RealtimeBackend` set **once** before `start()`, **never** changed during playback — no sync needed (happens-before from `start()`).
2. All agent data structures fully populated before `start()`, read-only during playback (audio thread reads only).
3. `Streaming::shutdown()` **must** be called only **after** `Backend::stop()` returns — see shutdown ordering contract in `Streaming.hpp`.
4. `Pose::computePositions()` is **audio-thread-only** — `mPoses` and `mLastGoodDir` are exclusively owned by the audio thread.
5. `Spatializer::computeFocusCompensation()` is **main-thread-only, not RT-safe** — must not be called while audio is streaming.
6. Loader thread only writes to EMPTY buffers; audio thread only reads PLAYING buffers — the `LOADING → READY` state transition with `memory_order_release` ensures data visibility before the state flip.

#### Memory Order Audit Results

| Atomic                                                      | Order used                       | Verdict                                                                    |
| ----------------------------------------------------------- | -------------------------------- | -------------------------------------------------------------------------- |
| `RealtimeConfig::masterGain` / `loudspeakerMix` / `subMix`  | relaxed (both r/w)               | ✅ Correct — gain staleness of one buffer is inaudible and not a data race |
| `RealtimeConfig::playing` / `shouldExit`                    | relaxed                          | ✅ Correct — polling only, no dependent memory                             |
| `EngineState::frameCounter` / `playbackTimeSec` / `cpuLoad` | relaxed                          | ✅ Correct — single writer (audio); display lag of one buffer is fine      |
| `SourceStream::stateA/B`                                    | release (write) / acquire (read) | ✅ Correct — synchronizes buffer data visibility                           |
| `SourceStream::chunkStartA/B` / `validFramesA/B`            | release / acquire                | ✅ Correct — same acquire/release pair                                     |
| `SourceStream::activeBuffer`                                | release / acquire                | ✅ Correct — buffer switch is visible atomically                           |
| `Streaming::mLoaderRunning`                                 | release (false) / acquire (poll) | ✅ Correct — loader sees the stop signal                                   |

#### Files Modified

| File                                                    | Change summary                                                                                                                                                                                                                                                                                                             |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | Added 60-line threading model doc block (thread table, memory order table, 6 invariants). Updated gain-atomics comment to remove stale "Phase 6 adds" note.                                                                                                                                                                |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Updated `processBlock()` comment to reflect all phases done (removed "Future phases" stubs). Added threading annotations to agent pointer and cached-data member sections. Added `memory_order_relaxed` justification comment on EngineState writes.                                                                       |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`       | Added 50-line threading model block to file header (three-thread table, memory ordering protocol, **shutdown ordering contract**). Rewrote `shutdown()` comment to explicitly state the caller precondition and explain why `mStreams.clear()` is safe.                                                                    |
| `spatial_engine/realtimeEngine/src/Pose.hpp`            | Added threading model block (main/audio/loader roles, read-only vs. audio-thread-owned data, note on `mLastGoodDir` first-block allocation). Updated `computePositions()` doc to add `THREADING: AUDIO THREAD ONLY` warning.                                                                                               |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`     | Added threading model block (main/audio/loader roles, read-only vs. audio-thread-owned data). Updated `computeFocusCompensation()` header to replace vague "control/main thread" with explicit `THREADING: MAIN THREAD ONLY` + reason + correct usage guide. Updated member data comments with per-field thread ownership. |

**Build result:** `cmake --build . -j4` — zero errors, zero warnings (all changes are comments and doc strings only; no compiled code changed).

---

### Exact File Paths for Key Artifacts (Phase 6)

> Phase 6 removed the Python launcher (`runRealtime.py`) and the PySide6/QProcess GUI. Paths below reflect the current C++ entrypoints.

| Artifact                          | Path                                                                     |
| --------------------------------- | ------------------------------------------------------------------------ |
| Real-time C++ executable          | `build/spatial_engine/realtimeEngine/spatialroot_realtime`               |
| Offline C++ executable            | `build/spatial_engine/spatialRender/spatialroot_spatial_render`          |
| CULT transcoder (ADM → LUSID)     | `build/cult_transcoder/cult-transcoder`                                  |
| CMakeLists (real-time)            | `spatial_engine/realtimeEngine/CMakeLists.txt`                           |
| Shared JSONLoader                 | `spatial_engine/src/JSONLoader.cpp` / `.hpp`                             |
| Shared LayoutLoader               | `spatial_engine/src/LayoutLoader.cpp` / `.hpp`                           |
| Shared WavUtils                   | `spatial_engine/src/WavUtils.cpp` / `.hpp`                               |
| Speaker layouts dir               | `spatial_engine/speaker_layouts/`                                        |
| Processed data (scene JSON, etc.) | `processedData/stageForRender/scene.lusid.json`                          |
| ADM extracted metadata            | `processedData/currentMetaData.xml`                                      |
| LUSID schema                      | `LUSID/schema/lusid_scene_v0.5.schema.json`                              |
| Design doc (streaming/DBAP)       | `internalDocsMD/Realtime_Engine/realtimeEngine_designDoc.md`             |
| ADM streaming design doc          | `internalDocsMD/Realtime_Engine/agentDocs/agent_adm_direct_streaming.md` |

### Verified Test Commands (Phase 6)

```bash
# Build (engine only, fast path):
./engine.sh

# Or full rebuild:
./build.sh

# Generate the LUSID scene JSON from an ADM file:
./build/cult_transcoder/cult-transcoder transcode sourceData/SWALE-ATMOS-LFE.wav

# Realtime engine (ADM mode):
./build/spatial_engine/realtimeEngine/spatialroot_realtime \
  --scene  processedData/stageForRender/scene.lusid.json \
  --adm    sourceData/SWALE-ATMOS-LFE.wav \
  --layout spatial_engine/speaker_layouts/translab-sono-layout.json \
  --gain 0.5 --buffersize 512

# Realtime engine (mono stems mode):
./build/spatial_engine/realtimeEngine/spatialroot_realtime \
  --scene   processedData/stageForRender/scene.lusid.json \
  --sources processedData/stageForRender/ \
  --layout  spatial_engine/speaker_layouts/allosphere_layout.json \
  --gain 0.1 --buffersize 512

# ImGui GUI (builds first if needed):
./run.sh
```

### Python Environment (Phase 6)

- No Python toolchain is required for building or running the realtime engine or GUI as of Phase 6.

---

## Architecture Overview

This real-time spatial audio engine is designed as a collection of specialized **agents**, each handling a distinct aspect of audio processing. Splitting functionality into separate agents enables **sequential, testable development** where each piece is verified before the next begins. The engine's goal is to render spatial audio with minimal latency and no glitches, even under heavy load, by carefully coordinating these components.

Key design goals include:

- **Hard Real-Time Performance:** The audio processing must complete within each audio callback frame (e.g. on a 512-sample buffer at 48 kHz, ~10.7ms per callback) to avoid underruns or glitches. Each agent is designed to do its work within strict time budgets.
- **Modularity:** Each agent has a clear responsibility and interface, making the system easier to maintain and allowing multiple team members to work in parallel.
- **Thread Safety:** Agents communicate via thread-safe or lock-free structures to avoid blocking the high-priority audio thread. No dynamic memory allocation or unbounded waits occur in the audio callback.
- **Scalability:** The architecture should handle multiple audio sources and outputs, scaling with available CPU cores by distributing work across threads where possible.

## Agents and Responsibilities

Below is a summary of each agent in the system and its primary responsibilities:

- **Streaming Agent:** Handles input audio streams (file, network, or live sources). It reads, decodes, and buffers audio data for each source, providing timely audio buffers to the engine.
- **Pose and Control Agent:** Manages dynamic source and listener states (positions, orientations, and control commands). It processes external controls (e.g., from GUI or network) and updates the shared scene data (source positions, activation, etc.).
- **Spatializer (DBAP) Agent:** Core audio processing module that spatializes audio using Distance-Based Amplitude Panning (DBAP). It computes gain coefficients for each source-to-speaker path and mixes source audio into the appropriate output channels based on spatial positions.
- **LFE Router Agent:** Extracts and routes low-frequency content to the Low-Frequency Effects (LFE) channel. It ensures subwoofer output is properly generated (e.g., by low-pass filtering content or routing dedicated LFE sources) without affecting main channel clarity.
- **Output Remap Agent:** Maps and adapts the engine’s output channel layout to the actual audio output hardware or desired format. This includes reordering or downmixing channels to match the device configuration (e.g., mapping internal channels to sound card output channels).
- **Compensation and Gain Agent:** Solves the focus/sub balance problem. As DBAP `focus` increases, main speaker power drops but sub level stays constant (computed independently from `masterGain`). This agent adds: (1) a **loudspeaker mix slider** (±10 dB) applied to main speaker channels after DBAP, (2) a **sub mix slider** (±10 dB) applied to subwoofer channels after LFE routing, and (3) a **focus auto-compensation toggle** that auto-updates the loudspeaker slider when focus changes. Both sliders are post-DBAP, pre-output, atomic, and GUI/CLI controllable at runtime.
- **Threading and Safety Agent:** Oversees the multi-threading model and ensures real-time safety. It defines how threads (audio callback thread, streaming thread, control thread, GUI thread, etc.) interact and shares data, using lock-free queues or double-buffering to maintain synchronization without blocking.
- **Backend Adapter Agent:** Abstracts the audio hardware or API (e.g., CoreAudio, ASIO, ALSA, PortAudio). It provides a unified interface for the engine to output audio, handling device initialization, buffer callbacks or threads, and bridging between the engine’s audio buffers and the OS/hardware.
- **GUI Agent:** Handles the graphical user interface and user interaction. It displays system state (levels, positions, statuses) and allows the user to adjust parameters (e.g., moving sound sources, changing volumes, selecting output device) in a way that’s safe for the real-time engine.

Each agent has its own detailed document (located in `internalDocsMD/Realtime_Engine/agentDocs/`) describing its role, constraints, and interfaces in depth. Developers responsible for each component should refer to those documents for implementation guidance.

## Data Flow and Interactions

The spatial audio engine’s processing pipeline flows through these agents as follows:

1. **Input Stage (Streaming):** Audio sources are ingested by the **Streaming Agent** (from files or streams). Decoded audio frames for each source are buffered in memory.
2. **Control Updates:** Concurrently, the **Pose and Control Agent** receives updates (e.g., new source positions, user commands) and updates a shared scene state. This state includes each source’s position and orientation, as well as global controls like mute or gain changes.
3. **Audio Callback Processing:** On each audio callback (or frame tick):
   - The **Spatializer (DBAP) Agent** reads the latest audio frame from each source (provided by the Streaming agent via a lock-free buffer) and the latest positional data (from Pose and Control agent’s shared state). It calculates the gain for each source on each speaker using the DBAP algorithm and mixes the sources into the spatial audio output buffer (one channel per speaker output).
   - The **Compensation and Gain Agent** applies post-DBAP mix trims to the internal render buffer before copying to output: a loudspeaker mix multiplier to all non-subwoofer channels, and a sub mix multiplier to subwoofer channels. If focus auto-compensation is enabled, the loudspeaker mix is automatically adjusted when `focus` changes to compensate for DBAP's reduced total power at higher focus values.
   - As part of the mixing process, the **LFE Router Agent** extracts low-frequency content. For example, it might low-pass filter the summed signal or individual source channels and send those frequencies to a dedicated LFE output channel. If a source is flagged as LFE-only, this agent routes that source’s content directly to the subwoofer channel.
   - After sources are mixed into a set of output channels (including the LFE channel if present), the **Output Remap Agent** takes this intermediate multichannel output and reorders or downmixes it according to the actual output configuration. For instance, if the engine’s internal spatial layout is different from the sound card’s channel order, it swaps channels as needed. If the output device has fewer channels than produced (e.g., rendering a multichannel scene on stereo output), it downmixes appropriately (with pre-defined gains to preserve balance).
4. **Output Stage:** The **Backend Adapter Agent** interfaces with the audio hardware or API. It either provides the engine’s output buffer directly to the hardware driver or calls the system’s audio callback with the mixed/remapped audio. This agent handles specifics like buffer format (interleaved vs planar audio), sample rate conversion (if needed), and ensuring the audio thread meets the API’s timing requirements.
5. **User Interface Loop:** In parallel with the audio processing, the **GUI Agent** runs on the main/UI thread. It fetches state (e.g., current source positions, levels, streaming status) in a thread-safe manner (often via copies or atomics provided by other agents) and presents it to the user. When the user interacts (for example, moving a sound source in the UI or changing master volume), the GUI Agent passes those commands to the relevant agents (Pose and Control for position changes, or Compensation and Gain for volume adjustments, etc.) without directly interfering with the audio thread.

This data flow ensures that heavy I/O (disk or network reads, GUI operations) and control logic are offloaded to separate threads, while the time-critical audio mixing runs on the dedicated real-time thread. Communication between threads is handled by shared data structures that are updated in a controlled way.

## Real-Time Considerations

Real-time audio processing imposes strict constraints that all agents must respect for the system to function without audio dropouts:

- **No Blocking Calls in Audio Thread:** The audio callback (Spatializer and subsequent processing) must never wait on locks, file I/O, network, or any operation that could block. Agents like Streaming or GUI must use double-buffering or lock-free queues to deliver data to the audio thread, so the audio processing can run without pausing:contentReference[oaicite:1]{index=1}.
- **No Dynamic Memory Allocation in Callback:** All memory required for audio processing should be pre-allocated. For example, audio buffers for mixing and any filter coefficients should be initialized ahead of time. This avoids unpredictable delays from memory allocation or garbage collection during the audio loop.
- **Time-Bound Processing:** Each audio callback must complete within the allotted frame time (e.g., a few milliseconds). Algorithms used (such as the DBAP calculations, filtering for LFE, etc.) should be optimized (using efficient math and avoiding overly complex operations per sample). Worst-case execution time must be considered, especially when the number of sources or speakers scales up.
- **Thread Priorities:** The audio processing thread (or callback) should run at a high priority or real-time scheduling class as allowed by the OS. Background threads (streaming, control, GUI) run at lower priorities to ensure the audio thread isn’t starved of CPU time. The Threading and Safety agent will outline how to set this up and avoid priority inversions.
- **Synchronization Strategy:** Shared data (like source positions, audio buffers) is synchronized in a lock-free manner. For example, the Pose and Control agent might maintain two copies of the positions and atomically swap pointers to publish new data to the audio thread, or use atomics for small updates. The goal is to eliminate heavy locks in the audio path while still keeping data consistent.
- **Buffering and Latency:** The Streaming agent should keep a small buffer of audio data ready in advance (e.g., a few blocks) so that momentary disk or network delays don’t cause underruns. However, excessive buffering adds latency, so a balance is required. Similarly, any control or GUI commands that need to take effect (e.g., mute or move source) should be applied at frame boundaries to maintain sync.

All agents must cooperate under these constraints. If any agent fails to meet real-time requirements (for instance, if decoding audio takes too long, or a lock is held too long), the whole system can suffer an audible glitch. During development, agents should test under stress conditions to ensure timing stays within limits. Logging in the audio thread should be minimal or absent (since I/O can block); use lightweight telemetry (e.g., atomic counters or ring buffers for logs) if needed to diagnose issues without hurting performance.

## Development and Documentation Notes

This master document and the individual agent documents are living plans for the implementation. As development progresses:

- **Maintain Consistency:** Developers should keep the design aligned across documents. If an interface between agents changes, update both this overview and the respective agent docs to reflect it.
- **Progress Updates:** Each agent lead should update `internalDocsMD/agents.md` (the central index of agents) with status and any noteworthy changes. For example, mark when an agent is implemented or note decisions that affect other agents.
- **Cross-Referencing:** Ensure that references to code (e.g., `SpatialRenderer.cpp`, `mainplayer.cpp`) remain accurate. If code structure changes (like files are renamed or new helper classes are created), update the documentation accordingly.
- **Rendering Pipeline Documentation:** If the processing pipeline is modified (for instance, adding a new processing stage or altering the order of operations), update the global `RENDERING.md` document. This ensures our real-time audio rendering approach is clearly recorded for future maintainers.
- **Parallel Development Coordination:** Given that agents are being implemented in parallel, schedule regular sync-ups to discuss integration points. Use this document as a guide to verify that all assumptions (data formats, call sequences, thread responsibilities) match across agents.

By following this plan and keeping documentation up-to-date, the team can build a robust real-time spatial audio engine. This overview will serve as a roadmap, and each agent’s detailed document will provide the specific guidance needed for individual implementation and eventual integration into a cohesive system.

**OSC port policy:** use a **fixed localhost port (9009)** for the engine `ParameterServer`.
This is simplest for the prototype, but may conflict if multiple instances run or the port is occupied.
GUI must surface clear errors; future refactor can add configurable/auto-pick ports.

---

## Phase 11 — Bug-Fix Pass (March 7, 2026) ✅ Complete

> **Context:** Full code inspection of all five realtime engine headers/source
> files identified four distinct failure classes. All four are fixed in this
> pass. Docs updated in the same commit.
>
> **Files modified:** `Spatializer.hpp`, `Streaming.hpp`, `RealtimeTypes.hpp`,
> `RealtimeBackend.hpp` (minor comment update), `internalDocsMD/AGENTS.md`.
>
> **Build result:** `cmake --build . -j4` — zero errors after all changes.

### Bugs Fixed

| #   | Symptom                                 | Root Cause                                                                                          | Fix                                                                                                                              |
| --- | --------------------------------------- | --------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| 1   | Focus gain compensation broken/inverted | `autoComp` never forwarded into `ControlsSnapshot`; comp result and manual slider wrote same atomic | Add `autoComp` to `ControlsSnapshot`; separate `mAutoCompValue`; override mode in `renderBlock()`                                |
| 2   | Random loudspeaker dropout              | Hard `0.0f` on buffer miss; 50% preload threshold too late; 5 s chunk too small                     | 10 s chunks; 75% threshold; exponential fade-to-zero on miss; underrun counter                                                   |
| 3   | Noise bursts                            | No `isfinite`/clamp guard on DBAP output before hardware write                                      | Post-render clamp loop; NaN→0, clamp ±4.0f; `nanGuardCount` counter in `EngineState`                                             |
| 4   | Pops at speaker-set transitions         | Focus updated once per block as a hard step; per-channel gain interpolation scaffolded but stubbed  | Block-level exponential smooth is sufficient; `renderBuffer()` retained; `mPrevFocus` stub added for future threshold-gated skip |

---

### Fix 1 — Focus Compensation Parameter Plumbing

**Problem (two parts):**

**Part A — `autoComp` never reached the spatializer.**
`RealtimeBackend::processBlock()` Step A snapshots `focusAutoCompensation` into
`mSmooth.target.autoComp`. Step B smooths it (takes target immediately, no
lerp — correct for a bool). Step 3 builds `ControlsSnapshot ctrl` and passes
it to `renderBlock()`. But `ControlsSnapshot` (defined in `Spatializer.hpp`)
had **no `autoComp` field**, so the value was silently dropped. The spatializer
had no way to know compensation was active.

**Part B — compensation and manual slider shared one atomic.**
`computeFocusCompensation()` wrote its result into `mConfig.loudspeakerMix`.
The `spkMixDbParam` OSC callback also wrote `mConfig.loudspeakerMix`. Last
writer wins with no priority. A GUI slider move silently zeroed compensation;
a focus change silently zeroed the slider. Neither path knew about the other.

**Fix — two changes in `Spatializer.hpp`:**

1. Added `bool autoComp = false` field to `ControlsSnapshot`.
2. Added `float mAutoCompValue = 1.0f` private member to `Spatializer`.
3. `computeFocusCompensation()` now writes `mAutoCompValue` instead of
   `mConfig.loudspeakerMix`. `mConfig.loudspeakerMix` is left for the
   manual slider exclusively.
4. In `renderBlock()`, the effective speaker mix is computed as:
   ```cpp
   float spkMix = ctrl.autoComp
       ? mAutoCompValue          // compensation overrides slider when auto is on
       : ctrl.loudspeakerMix;   // manual slider when auto is off
   ```

**Fix — one change in `RealtimeBackend.hpp`:**

5. In Step 3 of `processBlock()`, added `ctrl.autoComp = mSmooth.smoothed.autoComp;`

**Invariant added (Invariant 8):**

> When `autoComp` is enabled, `mAutoCompValue` (written by
> `computeFocusCompensation()` on the main thread, read by `renderBlock()` on
> the audio thread) is the exclusive source of the speaker-channel gain scalar.
> `mConfig.loudspeakerMix` is reserved for the manual slider and is never
> read by `renderBlock()` when `autoComp` is true.
>
> `mAutoCompValue` is written only from the main thread (same constraint as
> `computeFocusCompensation()` — see Invariant 5). It is a plain `float` read
> from the audio thread. This is safe because: the audio thread reads it only
> via the `ctrl` snapshot (which takes it from `mSmooth.smoothed.autoComp`
> first), and by the time `renderBlock()` runs, any preceding write from the
> main thread is visible (happens-before through the audio callback scheduling).
> No atomic needed — same reasoning as agent pointer immutability.

**Code locations:**

- `Spatializer.hpp`: `ControlsSnapshot` struct, `renderBlock()` Phase 6 section,
  `computeFocusCompensation()`, `mAutoCompValue` member declaration.
- `RealtimeBackend.hpp`: `processBlock()` Step 3 `ctrl` construction block.

---

### Fix 2 — Streaming Hardening (Eliminate Buffer Misses)

**Problem — three compounding issues:**

1. **5 s chunks + 50% threshold = 2.5 s runway.** On a loaded system, if the
   loader thread's `sleep_for(2ms)` scan loop is de-scheduled or if disk I/O
   is slow, the inactive buffer may still be `LOADING` or `EMPTY` when the
   audio thread needs to switch. The switch fails; `getSample()` returns hard
   `0.0f` — heard as a complete dropout of that source.

2. **Hard silence on miss.** `SourceStream::getSample()` returned `0.0f`
   unconditionally on underrun. A single missed buffer switch zeros an entire
   source for one audio block (~10 ms at 512/48k). With multiple sources,
   multiple dropouts can coincide (they all share the same chunk boundary timing
   when loaded from a multichannel file). The result is an audible full-band
   dropout rather than a gentle glitch.

3. **No observability.** No counter was incremented on miss, so dropouts were
   invisible in the monitoring loop.

**Design decision — eliminate, not paper over:**
The goal is to make misses structurally impossible under normal operating
conditions. The approach is to increase the runway to a point where no
realistic I/O delay can cause a miss:

- **10 s chunks** (`kDefaultChunkFrames = 480000` at 48 kHz): doubles the
  buffer window. Memory cost: ~1.8 MB → ~3.7 MB per source. For 80 sources:
  ~150 MB total (2 buffers × 80 sources × ~960 KB). Acceptable for a DAW-class
  workstation.
- **75% preload threshold** (`kPreloadThreshold = 0.75f`): triggers the next
  chunk load when 7.5 s of the 10 s active buffer has been consumed, leaving
  2.5 s of active buffer remaining + the full 10 s inactive buffer being filled.
  The loader now has **7.5 s** to read the next 10 s of audio — a 0.75× real-time
  I/O rate requirement, easily met even on slow spinning disks.

**Fallback — exponential fade-to-zero on miss (safety net only):**
Even with the structural fix, a fade-to-zero fallback is added in case of an
exceptional miss (system suspend, I/O error, etc.). This is not the primary
defence — it is a last-resort that makes any residual miss audible as a short
fade rather than a hard click/dropout.

Implementation: `SourceStream` gains `mutable float mFadeGain{1.0f}`. On a
successful sample read, `mFadeGain` is snapped back to `1.0f`. On a miss,
`mFadeGain` is multiplied by `kMissFadeRate = 0.9958f` per sample (time
constant ≈ 5 ms at 48 kHz — the comment in `Streaming.hpp` gives the
derivation: `exp(-1/(48000×0.005)) ≈ 0.9958`). The multiplied `mFadeGain`
is tracked as state for the next call but the return value is always `0.0f`
once the underrun path is taken — the fade envelope prevents `mFadeGain`
from snapping back abruptly if a late buffer arrives mid-fade, avoiding a
click on recovery. Once `mFadeGain` falls below `1e-4f` it is zeroed and
held.

**Observability — underrun counter:**
`SourceStream` gains `mutable std::atomic<uint64_t> underrunCount{0}`.
`Streaming` exposes `totalUnderruns()` which sums across all streams.
`main.cpp` monitoring loop prints underrun count. Any non-zero value is a
diagnostic signal that the runway needs widening or disk I/O is pathological.

**Code locations:**

- `Streaming.hpp`: `kDefaultChunkFrames`, `kPreloadThreshold` constants;
  `SourceStream::getSample()` miss path; `SourceStream::mFadeGain`,
  `SourceStream::underrunCount` members; `Streaming::totalUnderruns()`.
- `main.cpp`: monitoring loop status line (prints underrun count).

---

### Fix 3 — NaN / Inf / Extreme-Gain Guards

**Problem:**
`al::Dbap::renderBuffer()` computes amplitude weights from inverse-distance
terms. If a source position places the source very close to or coincident with
a speaker, the distance term approaches zero and the weight approaches infinity.
This produces `Inf` or extremely large float values in `mRenderIO`, which then
propagate unchanged through the Phase 6 mix-trim pass and the Phase 7 copy
into `io.outBuffer()` → hardware. Result: full-amplitude noise burst, often
lasting only one block but loud enough to damage speakers or hearing.

Additionally, a degenerate SLERP in `Pose` (caught by `safeNormalize`) could
theoretically produce a zero vector, which after the `directionToDBAPPosition`
coordinate transform places the source at the origin. DBAP at origin is
implementation-dependent — could be `NaN` or extreme gain.

**Fix — post-render clamp pass in `Spatializer::renderBlock()`:**

After all DBAP and LFE rendering into `mRenderIO`, before the Phase 6 mix-trim
pass, a single loop over every frame of every render channel:

- Replaces any `!isfinite(s)` value with `0.0f`
- Clamps any value outside `[-kMaxSample, +kMaxSample]` (4.0f ≈ +12 dBFS)
- Increments `mNanGuardFired` (a block-level bool) if any bad value was found
- If `mNanGuardFired`, increments `EngineState::nanGuardCount` (new atomic)

**New `EngineState` field:**

```cpp
std::atomic<uint64_t> nanGuardCount{0};  // incremented per block where clamp fired
```

Printed in the monitoring loop as `| NaN: N` — zero means clean; any non-zero
value identifies a source position or DBAP distance bug.

**Minimum-distance guard:**
A secondary guard in `renderBlock()` checks each source position magnitude
before passing to DBAP: if `pose.position.mag() < kMinSourceDist` (0.05 m),
the position is nudged to `kMinSourceDist` along the same direction. This
prevents the division-by-near-zero in DBAP's distance term entirely, making
the clamp pass a true last-resort rather than a regular occurrence.

**Code locations:**

- `RealtimeTypes.hpp`: `EngineState::nanGuardCount` field.
- `Spatializer.hpp`: clamp loop before Phase 6 section; minimum-distance guard
  in the DBAP spatialization loop; `kMaxSample` and `kMinSourceDist` constants.
- `main.cpp`: monitoring loop status line (prints `nanGuardCount`).

---

### Fix 4 — Focus Interpolation Across Block Boundaries

**Problem:**
`mDBap->setFocus()` was called once per block with `ctrl.focus` (the
block-start smoothed value). When focus changes (user moves the GUI slider,
or `computeFocusCompensation()` updates `mAutoCompValue`), all source gains
jump simultaneously at the block boundary. With a 512-frame block at 48 kHz
(~10.7 ms), this is a step discontinuity in every speaker gain — potentially
audible as a pop, especially at high focus values where gain differences
between near and far speakers are large.

**Attempted approach — abandoned:**
The initial implementation replaced `renderBuffer()` with a per-frame loop
calling `al::Dbap::renderSample()` + `setFocus()` per frame. This was reverted
because `renderSample()` recomputes per-speaker inverse-distance gains on every
call (the gain calculation is inside the per-sample path, not cached). With
N sources × S speakers × F frames of `powf()` calls per block this caused
severe CPU overload and audio dropouts — the exact symptom it was meant to fix.

**Current state — `renderBuffer()` retained, problem not fully resolved:**
`ctrl.focus` is the output of a 50 ms exponential smoother in
`RealtimeBackend::processBlock()`, so each block gets a continuously ramped
value rather than a hard step. This reduces the severity of any boundary
discontinuity, but does not eliminate it — the smoother output is constant
within a block, not interpolated across it.

The broken `autoComp` plumbing (Fix 1) was a confirmed real bug that would
have caused audible gain jumps when auto-compensation toggled or recomputed.
That is now fixed. However, **it would be overconfident to say this was the
sole cause of all pops**. Other remaining candidates include:

- Stream underruns coinciding with buffer switches (see Fix 2 / Invariant 9)
- Gain spikes from near-zero source positions (see Fix 3 / Invariant 10)
- Speaker-set gain handoffs when a moving source crosses between speaker
  influence zones — inherent to DBAP block rendering, unrelated to focus

`mPrevFocus` is kept as a private member (assigned `= ctrl.focus` each block)
as a hook for a future threshold-gated fast-path: if the focus delta between
blocks is below an epsilon, skip re-running DBAP entirely and reuse cached
speaker gains. This is a valid future optimization but should only be
implemented once the above remaining candidates are confirmed resolved.

**What needs testing (before declaring this symptom closed):**
Run the engine for several minutes under typical scene content and observe:

- `Underruns:` counter in the monitoring loop (any non-zero → Fix 2 not
  sufficient for that I/O path, or chunk size still too small)
- `NaN:` counter (any non-zero → source position issue, investigate Pose output)
- Subjective pops when moving the focus slider slowly vs. quickly

**Code locations:**

- `Spatializer.hpp`: `mPrevFocus` member (assigned but not yet used for
  threshold gating); `renderBuffer()` call retained.

---

### Phase 11 — Outstanding Diagnostics

Phase 11 fixed the four identified structural bugs and restored build stability.
The original symptoms (occasional pops, dropout after a few minutes, noise bursts,
broken compensation) had multiple contributing causes. The fixes address all
**confirmed** root causes, but the following questions remain open and should be
answered by observation before declaring the audio quality issues fully resolved:

| Question                                       | How to observe                                        | Expected if fixed                                                                |
| ---------------------------------------------- | ----------------------------------------------------- | -------------------------------------------------------------------------------- |
| Are stream misses still occurring?             | `Underruns: N` in monitoring loop                     | `0` throughout a 10 min run                                                      |
| Are NaN/gain spikes still occurring?           | `NaN: N` in monitoring loop                           | `0` throughout a 10 min run                                                      |
| Do pops correlate with focus changes?          | Move focus slider slowly vs. quickly; listen for pops | No pop with slow movement (smoother handles it); rare or none with fast movement |
| Does autoComp toggle cleanly?                  | Toggle auto-comp on/off via OSC while audio plays     | Smooth gain transition, no click                                                 |
| Do dropouts still occur after several minutes? | Leave running, observe monitoring loop                | No dropouts; all sources continuous                                              |

**Conservative next steps (diagnostic-first, no architectural changes):**

1. Run end-to-end with the monitoring loop visible and capture `Underruns` and
   `NaN` counts over a 10-minute session. This confirms whether Fixes 2 and 3
   are operating as intended.
2. Verify the three gain paths (`mAutoCompValue`, `ctrl.loudspeakerMix`,
   `ctrl.masterGain`) compose predictably: with `autoComp` on, the manual
   slider should have no effect on speaker output; with `autoComp` off,
   `mAutoCompValue` should be ignored.
3. If pops still occur on focus changes after observation confirms stream/NaN
   counters are clean, then revisit within-block focus interpolation — but via
   a different approach than `renderSample()`. The correct approach would be
   to compute per-speaker gain vectors for both `focusStart` and `focusEnd`
   once each (not per-frame), then lerp those gain vectors across the block
   samples directly, bypassing `setFocus()`/`renderBuffer()` entirely.

---

### Phase 11 Agent Implementation Order Table Update

| Phase | Agent(s)                   | Status                                                |
| ----- | -------------------------- | ----------------------------------------------------- |
| 1–10  | _(as above)_               | ✅ Complete                                           |
| 11    | **Bug-Fix Pass**           | ✅ Complete                                           |
|       | Fix 1: Focus comp plumbing | ✅ `Spatializer.hpp` + `RealtimeBackend.hpp`          |
|       | Fix 2: Streaming hardening | ✅ `Streaming.hpp`                                    |
|       | Fix 3: NaN/clamp guards    | ✅ `Spatializer.hpp` + `RealtimeTypes.hpp`            |
|       | Fix 4: Focus interpolation | ⚠️ `Spatializer.hpp` — partial; see diagnostics above |

### New Invariants (added Phase 11)

**Invariant 8 — Auto-compensation ownership:**

> When `ctrl.autoComp` is `true`, `renderBlock()` uses `mAutoCompValue`
> (written by `computeFocusCompensation()`, main thread only) as the speaker
> gain scalar. `mConfig.loudspeakerMix` is reserved for the manual slider and
> is never consulted when `autoComp` is true. The two paths cannot interfere.

**Invariant 9 — Streaming runway guarantee:**

> The preload threshold (75%) and chunk size (10 s) are sized so that the
> loader thread has 7.5 s to read the next 10 s chunk. Any I/O subsystem
> capable of sustained 0.75× real-time throughput will never produce a miss.
> The fade-to-zero fallback exists only as a diagnostic safety net, not as
> a normal operating path. A non-zero `underrunCount` in the monitoring loop
> indicates a pathological I/O condition that must be investigated.

**Invariant 10 — Output sample range:**

> All values written to `io.outBuffer()` are guaranteed finite and within
> `[-4.0f, +4.0f]` (~+12 dBFS). The clamp pass in `renderBlock()` is the
> enforcement point. `EngineState::nanGuardCount` is the observable indicator.
> A count of zero means the clamp never fired — the system is operating cleanly.

---

## Phase 12 — DBAP Proximity Bug-Fix (March 8, 2026)

### Background and Diagnosis

After Phase 11 shipped, two residual audio artifact classes were identified
during playback of `fileWithClicks.lusid.json` (TransLab, 16-speaker layout):

1. **Brief clicks correlated with object movement** — scattered throughout
   playback, particularly near trajectory extremes.
2. **Extended high-amplitude noise during "Eden"** — initially suspected to be
   a buffer or sample-rate issue; subsequently re-attributed to DBAP proximity
   pathology (see below).

Python analysis of the scene's 13,685 keyframes against the TransLab speaker
layout revealed the following minimum source-to-speaker distances at the closest
approach points in DBAP position space:

| Source | Min dist (m) | Max DBAP gain | Speaker | Time (s) |
| ------ | ------------ | ------------- | ------- | -------- |
| 21.1   | **0.049**    | 0.988         | ch15    | 47.79    |
| 27.1   | 0.133        | 0.948         | ch15    | 68.90    |
| 28.1   | 0.220        | 0.883         | ch10    | 31.03    |
| 33.1   | 0.321        | 0.839         | ch1     | 19.15    |

Source 21.1 passes **through** the Phase 11 exclusion zone (4.9 cm < 5 cm
`kMinSourceDist`) — but the Phase 11 guard never fired because it checked
`pose.position.mag()` (distance from the origin), which always equals
`mLayoutRadius` (~5.21 m) since all source positions are normalized direction
vectors scaled to the speaker ring radius. The guard was geometrically inert.

The Eden extended noise was re-attributed to the same root cause: source 21.1's
0.049 m approach is sufficient to produce near-unity DBAP gain on a single
speaker for an extended trajectory segment, resulting in a loud tonal artifact
that persists for as long as the source remains near that speaker. This is
**finite but pathological gain behavior**, not a streaming underrun or sample-rate
mismatch (underrun and NaN counters were clean throughout playback).

### Fix 1 — Replace Broken Origin Guard with Per-Speaker Proximity Guard

**File:** `Spatializer.hpp`

**What changed:**

- Removed: `kMinSourceDist = 0.05f` and the origin-distance guard block in
  `renderBlock()`.
- Added: `kMinSpeakerDist = 0.15f` — minimum allowed source-to-speaker distance
  in DBAP position space.
- Added: `std::vector<al::Vec3f> mSpeakerPositions` — speaker positions
  pre-cached at `init()` in DBAP coordinate space using the same
  `(x,y,z) → (x,z,−y) × r` transform as `Pose::directionToDBAPPosition()`.
- Added: per-speaker loop in `renderBlock()` before `mDBap->renderBuffer()`:
  for each speaker, if `dist(safePos, spkPos) < kMinSpeakerDist`, push the
  source outward along the source→speaker axis to exactly `kMinSpeakerDist`.
  The push is a continuous linear displacement — no step discontinuity.

**Threshold rationale:** 0.15 m is a conservative first-pass value. The worst
observed case (0.049 m) is well inside this radius. Can be reduced toward
0.05–0.10 m if near-speaker localization sounds artificially constrained. Do
not increase toward 0.35 m or higher without testing — that range starts to
flatten legitimate close-speaker trajectory segments.

**Real-time safety:** The per-speaker loop is O(numSpeakers) comparisons per
source per block — 16 iterations for the TransLab layout. No allocation, no
branching beyond the distance compare. Fully safe in the audio callback.

### Fix 2 — Add `speakerProximityCount` Diagnostic Counter

**Files:** `RealtimeTypes.hpp`, `Spatializer.hpp`, `main.cpp`

- `EngineState::speakerProximityCount` (`std::atomic<uint64_t>`) — incremented
  by the audio thread each time the proximity guard fires (one increment per
  source-speaker pair that triggers, per block).
- `Spatializer::renderBlock()` increments via
  `mState.speakerProximityCount.fetch_add(1, std::memory_order_relaxed)`.
- Monitoring loop in `main.cpp` displays `SpeakerGuard: N` alongside the
  existing `Underruns` and `NaN` counters.

A non-zero `SpeakerGuard` count during playback confirms that proximity
paths are being hit and the guard is active. The count accumulates across
the session; its rate of change (how fast it climbs during specific scene
segments) helps correlate guard activity with known problematic trajectories.

### Deferred Items

The following were analyzed but deliberately deferred from this pass:

| Item                                                            | Reason deferred                                                                                                               |
| --------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| Block-to-block position smoothing (`Pose.hpp`, alpha ≈ 0.5–0.7) | Second priority; adds lag and may obscure remaining spatial math issues. Revisit only if clicks persist after proximity fix.  |
| Scene loop / `resetForLoop()` (`Streaming.hpp` + `main.cpp`)    | Separate lifecycle feature; introduces moving parts while audio artifact diagnosis is ongoing. Implement in a dedicated pass. |

### Phase 12 Invariants

**Invariant 11 — Speaker exclusion zone:**

> No source position passed to `mDBap->renderBuffer()` shall be within
> `kMinSpeakerDist` (0.15 m) of any speaker's DBAP position. The per-speaker
> loop in `renderBlock()` is the enforcement point. `EngineState::speakerProximityCount`
> counts enforcement events; zero means no source entered any speaker's exclusion
> zone during that session.

### Phase 12 Agent Implementation Order Table Update

| Phase | Agent(s)                                  | Status                                                  |
| ----- | ----------------------------------------- | ------------------------------------------------------- |
| 1–11  | _(as above)_                              | ✅ Complete                                             |
| 12    | **DBAP Proximity Bug-Fix Pass**           | ✅ Complete                                             |
|       | Fix 1: Per-speaker proximity guard        | ✅ `Spatializer.hpp`                                    |
|       | Fix 2: `speakerProximityCount` diagnostic | ✅ `RealtimeTypes.hpp` + `Spatializer.hpp` + `main.cpp` |
|       | Position smoothing                        | ⏸ Deferred                                              |
|       | Scene loop / `resetForLoop()`             | ⏸ Deferred                                              |

---

## Phase 13 — Coordinate Space Correctness + AutoComp Disable

### Root Cause: Phase 12 Speaker Cache was in the Wrong Coordinate Space

During Phase 12, `Spatializer::init()` manually reconstructed speaker positions
from the JSON layout data and stored them as `(sx, sz, -sy)` — the same transform
applied by `directionToDBAPPosition()` to produce _Pose-output space_ coordinates.
However, the proximity guard in `renderBlock()` needed to compare source positions
against speaker positions **in DBAP-internal space**, which is a different
coordinate system.

**Coordinate space chain (fully traced from `al_Dbap.cpp` and `al_Speaker.cpp`):**

| Space                             | Formula                                         | Used by                                                        |
| --------------------------------- | ----------------------------------------------- | -------------------------------------------------------------- |
| Pose-output space                 | `(dir.x·r, dir.z·r, −dir.y·r)`                  | `pose.position`, input to `renderBuffer()`                     |
| DBAP-internal space               | `(pos.x, −pos.z, pos.y)`                        | flip applied inside `renderBuffer()` before distance math      |
| `speaker.vec()`                   | `(sin(az)·cosEl·r, cos(az)·cosEl·r, sin(el)·r)` | speaker positions stored in DBAP — same as DBAP-internal space |
| Forward flip (pose→DBAP-internal) | `(x,y,z) → (x,−z,y)`                            | guard pre-step                                                 |
| Inverse flip (DBAP-internal→pose) | `(x,y,z) → (x,z,−y)`                            | guard post-step (same operation, self-inverse)                 |

The Phase 12 cache stored positions in Pose-output space. The proximity
comparisons were therefore mixing two different spaces — the distances computed
were geometrically meaningless and the guard would fire (or not fire) at the
wrong positions.

### Fix 1 — Speaker Position Cache: Use `speaker.vec()`

**File:** `Spatializer.hpp`, `init()`

**Before (Phase 12):** manually computed `(sx, sz, -sy)` from JSON data.

**After (Phase 13):** uses `mSpeakers[i].vec()` directly:

```cpp
mSpeakerPositions.clear();
mSpeakerPositions.reserve(mNumSpeakers);
for (int i = 0; i < mNumSpeakers; ++i) {
    al::Vec3d v = mSpeakers[i].vec();
    mSpeakerPositions.emplace_back(
        static_cast<float>(v.x),
        static_cast<float>(v.y),
        static_cast<float>(v.z));
}
```

`al::Speaker::vec()` returns the same Cartesian vectors that `al_Dbap.cpp`
stores internally in `mSpeakerVecs[k]`. The cache is now in exactly the same
space that DBAP uses for its distance computations.

### Fix 2 — Proximity Guard: Operate Entirely in DBAP-Internal Space

**File:** `Spatializer.hpp`, `renderBlock()`

The guard now performs an explicit flip before comparing, pushes in the correct
space, then un-flips back to Pose-output space before calling `renderBuffer()`.
`renderBuffer()` re-applies the flip internally, recovering the pushed position.

```cpp
// Step 1: flip pose.position → DBAP-internal space
const al::Vec3f& p = pose.position;
al::Vec3f relpos(p.x, -p.z, p.y);

// Step 2: guard in DBAP-internal space (same space as mSpeakerPositions)
bool guardFiredForSource = false;
for (const auto& spkVec : mSpeakerPositions) {
    al::Vec3f delta = relpos - spkVec;
    float dist = delta.mag();
    if (dist < kMinSpeakerDist) {
        relpos = spkVec + ((dist > 1e-7f)
            ? (delta / dist) * kMinSpeakerDist
            : al::Vec3f(0.0f, kMinSpeakerDist, 0.0f));
        guardFiredForSource = true;
    }
}
if (guardFiredForSource) {
    mState.speakerProximityCount.fetch_add(1, std::memory_order_relaxed);
}

// Step 3: un-flip back to pose space for renderBuffer()
al::Vec3f safePos(relpos.x, relpos.z, -relpos.y);
mDBap->renderBuffer(mRenderIO, safePos, mSourceBuffer.data(), numFrames);
```

**Counter behavior change:** The counter now increments once per source per
block (not once per speaker-pair), which is more meaningful for the monitoring
display — it reflects how many source-blocks were remapped, not how many
individual speaker collisions were found within a single block.

### Fix 3 — Disable Auto-Compensation (Two Structural Bugs Found)

**File:** `Spatializer.hpp`, `computeFocusCompensation()`

Full audit of `computeFocusCompensation()` revealed two independent bugs that
together caused ~+10 dB unconditional boost when `autoComp` was enabled:

**Bug A — Wrong reference position:**
`refPos = (0, radius, 0)` was passed directly to `renderBuffer()`. But
`renderBuffer()` applies the internal flip `(x,y,z)→(x,−z,y)` before
computing distances. The position that was actually tested was `(0, 0, radius)` —
the top of the sphere, not the intended front-center reference. All speaker
distances from the zenith point are equal and far, producing an anomalously
low reference power.

**Bug B — Wrong focus=0 baseline:**
At `mFocus=0`, DBAP computes `gain = pow(1/(1+dist), 0) = 1.0` for every
speaker regardless of distance. The reference power accumulator therefore sums
N×1.0 across all speakers rather than a physically meaningful power. This
inflated `refPower` relative to the actual focused `power` at any `focus > 0`,
pushing `compensation = sqrt(refPower/power)` toward the +10 dB clamp on every
call.

**Fix:** `computeFocusCompensation()` body replaced with a stub that immediately
returns `1.0f` (identity — no gain change). The function signature, `mAutoCompValue`
member, `ctrl.autoComp` branch in `renderBlock()`, and all OSC plumbing are
preserved intact for future reimplementation.

```cpp
float computeFocusCompensation() {
    if (!mInitialized) return 1.0f;
    // [Phase 13] Disabled — see realtime_master.md for root cause.
    mAutoCompValue = 1.0f;
    return 1.0f;
}
```

**Confirmed:** Buzzing was present with `autoComp OFF`, so autoComp was not the
root cause of the movement-click artifacts. The proximity guard coordinate fix
(Fixes 1 + 2) is the primary structural change targeting those artifacts.

### Phase 13 Invariants

**Invariant 12 — Speaker position cache uses DBAP-internal coordinate space:**

> `mSpeakerPositions[i]` shall contain `mSpeakers[i].vec()` cast to `float` —
> the same Cartesian vector that `al_Dbap.cpp` stores in `mSpeakerVecs[i]`.
> No manual spherical-to-Cartesian conversion or coordinate flip shall be
> applied to these values. Any future changes to the speaker layout must
> regenerate the cache via the `init()` path, which calls `vec()` directly.

**Invariant 13 — Proximity guard operates entirely in DBAP-internal space:**

> The proximity guard in `renderBlock()` shall convert `pose.position` to
> DBAP-internal space `(x,−z,y)` before all distance comparisons and push
> operations. The guarded position shall be converted back to Pose-output space
> `(x,z,−y)` before being passed to `renderBuffer()`. No guard comparison or
> push shall be performed in Pose-output space.

**Invariant 14 — Auto-compensation is a no-op until redesigned:**

> `computeFocusCompensation()` shall return `1.0f` unconditionally. The
> `mAutoCompValue` member and `ctrl.autoComp` branch are preserved for future
> use but the compensation scalar applied to audio output is always 1.0.
> This invariant is revoked when a correct reimplementation is merged.

### Phase 13 Agent Implementation Order Table Update

| Phase | Agent(s)                                               | Status                                             |
| ----- | ------------------------------------------------------ | -------------------------------------------------- |
| 1–12  | _(as above)_                                           | ✅ Complete                                        |
| 13    | **Coordinate Space + AutoComp Correctness**            | ✅ Complete                                        |
|       | Fix 1: Speaker cache → `speaker.vec()` (DBAP-internal) | ✅ `Spatializer.hpp` `init()`                      |
|       | Fix 2: Guard flip/push/unflip in DBAP-internal space   | ✅ `Spatializer.hpp` `renderBlock()`               |
|       | Fix 3: AutoComp disabled (stub returns 1.0f)           | ✅ `Spatializer.hpp` `computeFocusCompensation()`  |
|       | Position smoothing                                     | ⏸ Deferred                                         |
|       | Scene loop / `resetForLoop()`                          | ⏸ Deferred                                         |
|       | AutoComp redesign (correct ref position + power model) | ⏸ Deferred                                         |
| 14    | **Channel Relocation Diagnostic**                      | ✅ Complete                                        |
|       | Pre-copy render-bus active-channel mask                | ✅ `Spatializer.hpp` `renderBlock()` (before Ph 7) |
|       | Post-copy device-output active-channel mask            | ✅ `Spatializer.hpp` `renderBlock()` (after Ph 7)  |
|       | Relocation event latches (render + device)             | ✅ `EngineState` + monitoring loop in `main.cpp`   |
|       | Main/sub RMS totals                                    | ✅ `EngineState` `mainRmsTotal` / `subRmsTotal`    |
|       | Corrected wall-clock CPU meter                         | ✅ `RealtimeBackend.hpp` `processBlock()`          |
|       | Phase 14 channel-relocation root cause                 | ⏳ Pending listener test                           |

---

## Phase 14 — Channel Relocation Diagnostic

**Date:** 2026-03-25

**Goal:** Determine whether runtime channel relocation occurs before or after the Phase 7 `OutputRemap` copy, without adding per-sample or per-source logging to the audio callback.

### Design

Two lightweight measurement points are added in `Spatializer::renderBlock()`:

**Point 1 — render-bus (pre-copy)** — inserted immediately after the Phase 11 NaN clamp pass, before the Phase 7 `OutputRemap` copy loop:

- Scans `mRenderIO.outBuffer(ch)` for all render-bus channels
- Computes block mean-square per channel; sets bit `ch` in `renderActiveMask` if mean-square > 1e-8
- Accumulates `mainRmsTotal` / `subRmsTotal` across main / sub channels
- If `renderActiveMask` differs from last block, stores old+new masks to `renderRelocPrev` / `renderRelocNext` and sets `renderRelocEvent` latch

**Point 2 — device output (post-copy)** — inserted immediately after the Phase 7 copy:

- Same scan on `io.outBuffer(ch)` (the real device buffer)
- Sets `deviceActiveMask`, fires `deviceRelocEvent` latch on change

**CPU meter fix** — `RealtimeBackend::processBlock()`:

- `std::chrono::steady_clock::now()` captured at the very top of the callback (before all rendering)
- `callbackCpuLoad = elapsed_µs / block_budget_µs` stored to `EngineState` at Step 6 and at the paused early-return
- Capped at 2.0 (not 1.0) so overloads are visible
- Legacy `mAudioIO.cpu()` still stored to `cpuLoad` for comparison

**Monitoring loop** (`main.cpp`):

- One-time pre-loop print: `requested / actual-device / render-bus` channel counts
- Each 500 ms iteration: consumes `renderRelocEvent` / `deviceRelocEvent` latches and prints them immediately with timestamp and hex masks, then prints rolling status line with both masks, RMS totals, and `callbackCpuLoad`

### Diagnostic key

| Observation                                           | Conclusion                                                                          |
| ----------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `[RELOC-RENDER]` fires                                | Active channel set changing inside `mRenderIO` — render/DBAP layer origin           |
| `[RELOC-DEVICE]` fires, `[RELOC-RENDER]` does **not** | Device/output-layer origin only (OutputRemap, channel-count mismatch, AlloLib copy) |
| Both fire                                             | Render-bus change propagated through copy — consistent with render-layer origin     |
| Neither fires                                         | Threshold 1e-8 too high, or event masked by 500 ms poll window                      |

### New `EngineState` fields

| Field                        | Type                    | Writer                     | Meaning                                                     |
| ---------------------------- | ----------------------- | -------------------------- | ----------------------------------------------------------- |
| `renderActiveMask`           | `atomic<uint64_t>`      | audio                      | Bitmask of render-bus channels with signal, latest block    |
| `deviceActiveMask`           | `atomic<uint64_t>`      | audio                      | Bitmask of device-output channels with signal, latest block |
| `renderRelocPrev/Next/Event` | `atomic<uint64_t/bool>` | audio (set) / main (clear) | One-shot latch: render mask before/after a change           |
| `deviceRelocPrev/Next/Event` | `atomic<uint64_t/bool>` | audio (set) / main (clear) | One-shot latch: device mask before/after a change           |
| `mainRmsTotal`               | `atomic<float>`         | audio                      | sqrt(mean-square sum across main channels), latest block    |
| `subRmsTotal`                | `atomic<float>`         | audio                      | sqrt(mean-square sum across sub channels), latest block     |
| `callbackCpuLoad`            | `atomic<float>`         | audio                      | Wall-clock callback duration / block budget                 |

### Files changed

| File                  | Change                                                                                          |
| --------------------- | ----------------------------------------------------------------------------------------------- |
| `RealtimeTypes.hpp`   | New `EngineState` fields above                                                                  |
| `Spatializer.hpp`     | Pre-copy and post-copy measurement blocks; `numRenderChannels()` getter                         |
| `RealtimeBackend.hpp` | `#include <chrono>`; `mCallbackStart` member; `callbackCpuLoad` store at Step 6 and paused path |
| `main.cpp`            | Pre-loop channel count print; relocation event consumption and print; updated status line       |

### Invariant 15 — Phase 14 diagnostic measurements are additive

> The pre-copy and post-copy measurement blocks in `renderBlock()` shall not modify any audio data, any config atomic, or any render state. They shall only read from `mRenderIO.outBuffer()` / `io.outBuffer()` and write to `EngineState` diagnostic fields. Removing both blocks and the `EngineState` additions shall restore identical audio output (bit-for-bit) to the pre-Phase-14 build.
