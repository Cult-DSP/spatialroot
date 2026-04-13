# Realtime Engine — Internal Reference

**Last Updated:** April 1, 2026  
**Source:** `spatial_engine/realtimeEngine/`  
**Primary entry points:** `spatialroot_realtime` CLI binary, `gui/imgui/` (embeds `EngineSessionCore` in-process)

All phases complete: Phases 1–10 + OSC timing fix + Phase 11 bug-fix pass.  
See [API_internal.md](API_internal.md) for the `EngineSession` public API contract.

---

## Agent Architecture Overview

The engine follows a sequential agent model. Each agent owns one stage of the processing chain. All agents share `RealtimeConfig` and `EngineState` defined in `RealtimeTypes.hpp`.

| Phase | Agent                 | Status      | File                               |
| ----- | --------------------- | ----------- | ---------------------------------- |
| 1     | Backend Adapter       | ✅ Complete | `RealtimeBackend.hpp`              |
| 2     | Streaming             | ✅ Complete | `Streaming.hpp`                    |
| 3     | Pose                  | ✅ Complete | `Pose.hpp`                         |
| 4     | Spatializer (DBAP)    | ✅ Complete | `Spatializer.hpp`                  |
| —     | ADM Direct Streaming  | ✅ Complete | `MultichannelReader.hpp`           |
| 5     | LFE Router            | ⏭️ Skipped  | handled inside Spatializer         |
| 6     | Compensation and Gain | ✅ Complete | `main.cpp` + `RealtimeTypes.hpp`   |
| 7     | Output Remap          | ✅ Complete | `OutputRemap.hpp`                  |
| 8     | Threading and Safety  | ✅ Complete | `RealtimeTypes.hpp` (audit + docs) |
| 9     | Init / Config         | ✅ Complete | `init.sh`, build scripts           |
| 10    | GUI                   | ✅ Complete | `gui/imgui/` (ImGui + GLFW)        |
| 10.1  | OSC Timing Fix        | ✅ Complete | `EngineSession.cpp`                |
| 11    | Bug-Fix Pass          | ✅ Complete | multiple                           |

---

## Bug Audit (April 1, 2026)

> Consolidated from prior bug-audit docs; all bugs closed as of this date.

### Bug numbering convention

Each distinct root-cause problem is assigned a bug number (integer). Each patch attempt for that bug is a sub-number:

- `8` — Bug 8: the problem description
- `8.1` — First patch attempt for Bug 8 (may be plan, partial fix, or full fix)
- `8.2` — Second attempt (if 8.1 was insufficient or revised)

### Change Log Template

Use this format for every new patch attempt:

```
### Bug N.M — [Short description] — STATUS (date)

**Problem:** One-paragraph description of the failure mode and observable symptom.

**Root cause:** Where in the code the problem originates. File path and approximate location.

**Approach:** What this patch does and why.

**Files changed:**
- `path/to/file.hpp`: description of change

**RT-safety:** Any concerns and why they are acceptable (or not).

**Test result:** What happened when tested. Pass / partial / fail. Relevant log output.

**Status:** PATCHED | REVERTED | PLAN | IN PROGRESS
```

### Build / Run Quick Reference

```bash
./init.sh              # first-time setup (run once)
./build.sh --gui       # build everything including GUI
./build.sh --engine-only  # faster rebuild for engine-only changes
./run.sh               # launch GUI (normal path)

# Headless CLI testing:
./build/spatialroot_realtime \
    --layout spatial_engine/speaker_layouts/translab-sono-layout.json \
    --scene  processedData/stageForRender/SWALE-ATMOS-LFE.lusid.json \
    --adm    sourceData/SWALE-ATMOS-LFE.wav \
    --device "MOTU Pro Audio"   # omit for system default
    --list-devices              # enumerate output devices then exit
```

Note: `./engine.sh` is a legacy standalone build script outputting to `spatial_engine/realtimeEngine/build/`. Prefer `./build.sh` — it uses the unified CMake build at `build/` and is the canonical path.

### Test Content

| Content | ADM WAV                             | LUSID Scene                                                                 |
| ------- | ----------------------------------- | --------------------------------------------------------------------------- |
| Swale   | `sourceData/SWALE-ATMOS-LFE.wav`    | `processedData/stageForRender/SWALE-ATMOS-LFE.lusid.json`                   |
| Ascent  | `sourceData/ASCENT-ATMOS-LFE.wav`   | `processedData/stageForRender/ASCENT-ATMOS-LFE.lusid.json`                  |
| Eden    | `sourceData/EDEN-ATMOS-MIX-LFE.wav` | `processedData/stageForRender/EDEN-ATMOS-MIX-LFE.lusid.json`                |
| Canyon  | `sourceData/CANYON-ATMOS-LFE.wav`   | _(no pre-built scene — transcode via GUI TRANSCODE tab or cult-transcoder)_ |
| 360RA   | `sourceData/360RA_test.wav`         | `processedData/stageForRender/360RA_test.lusid.json`                        |

Speaker layouts: `spatial_engine/speaker_layouts/translab-sono-layout.json` (primary test), `allosphere_layout.json` (56-ch).

### Key Source Files

**C++ engine:**

| File                                                       | Role                                                                                                                                                                                                                                                                                         |
| ---------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`        | Core DBAP render loop. Proximity guard (Pass 1 soft zone + Pass 2 hard floor), fast-mover sub-stepping, cross-block guard-transition blending (`mPrevSafePos`/`mPrevSafeValid`/`mPrevGuardFired`), Phase 6 mix trims, Phase 14 diagnostic measurement points. **Most bugs touch this file.** |
| `spatial_engine/realtimeEngine/src/Pose.hpp`               | Keyframe interpolation pipeline: SLERP → `safeDirForSource` → `sanitizeDirForLayout` → `directionToDBAPPosition`. Computes `SourcePose::position` (block center), `positionStart`, `positionEnd`.                                                                                            |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`    | Audio callback controller. Owns `ControlSmooth` (50 ms exponential smoother for gain/focus), `processBlock()` Steps 1–6, per-block timing, CPU meter. All config values reach the audio thread exclusively via `mSmooth`.                                                                    |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`      | Shared types: `RealtimeConfig` atomics, `EngineState` diagnostic counters, all public structs. Threading model documented in header comments — read them.                                                                                                                                    |
| `spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp` | Public session API. Wraps all subsystems. Contains OSC `ParameterServer` and `OscParams` inner struct.                                                                                                                                                                                       |
| `spatial_engine/realtimeEngine/src/main.cpp`               | Headless CLI entry point. Parses args, builds config structs, calls `EngineSession` API, runs monitoring loop.                                                                                                                                                                               |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`          | Per-source audio streaming from multichannel ADM WAV. `parseChannelIndex()` maps source name → 0-based ADM channel: `"N.1" → N-1`, `"LFE" → 3`.                                                                                                                                              |

**C++ GUI:**

| File                                      | Role                                                                                                                                                                                                    |
| ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `gui/imgui/src/App.hpp` / `App.cpp`       | ImGui + GLFW desktop app. Owns `EngineSession`. `onStart()` always calls `resetRuntimeToDefaults()` before launching. Controls engine via direct C++ setters — not OSC. Two tabs: ENGINE and TRANSCODE. |
| `gui/imgui/src/SubprocessRunner.hpp/.cpp` | Runs `cult-transcoder` subprocess for ADM WAV → LUSID scene conversion.                                                                                                                                 |
| `gui/imgui/src/main.cpp`                  | GLFW window setup, render loop, calls `App::tick()` each frame.                                                                                                                                         |

Threading model: audio thread (RT, AlloLib), loader thread (background disk I/O), main thread (lifecycle + update()). See [Threading and Safety](#threading-and-safety) below.

### Closed Bugs

| #   | Bug                                                                       | Fix                                                      |
| --- | ------------------------------------------------------------------------- | -------------------------------------------------------- |
| 9.1 | Cross-block guard-transition blending — clicking at guard-zone entry/exit | Blended transition over several blocks                   |
| 8.1 | Explicit device flag + GUI picker — device selection unreliable           | `--device` flag + GUI device enumeration                 |
| 7.x | Guard-induced relocation and buzzing                                      | Guard transition logic overhaul                          |
| 6.x | Channel relocation diagnostics                                            | Added per-source channel relocation counter              |
| 5.1 | Smoother cold-start transient — initial pop on first block                | Ramped gain from zero on first `processBlock()`          |
| 4.1 | Stale slider state — sliders not reflecting engine state after restart    | State flush on `engine_ready`                            |
| 3.2 | Channel validation — crash on layout/device channel count mismatch        | Fast-fail with error message                             |
| 2.1 | Fast-mover sub-stepping — blinking on rapid trajectory changes            | Sub-step at 16-sample hops when angular delta > 0.25 rad |
| 1.1 | Source onset pop — click at source start                                  | Fade-in over first ~5ms of source playback               |

### Deferred / Engineering Notes

- Large interleaved buffer: `WavUtils.cpp` `writeMultichannelWav()` allocates one `std::vector<float>` for all samples × channels (56 × 27M ≈ 5.67 GB). Mitigation: chunked/streaming write.
- `direct_speaker` nodes untested at render level (test files exercise `audio_object` + `LFE` only).
- Do not use `std::atomic` in `RealtimeConfig` as copy-constructed function arguments — it deletes the implicit copy constructor. Pass by reference or use `session.config()` direct population.

---

## Backend Adapter

> Consolidated from prior agent docs (subfolders removed).

**Responsibilities:** Audio device initialization, callback registration, buffer format conversion, latency tuning, error recovery, backend selection.

**Real-Time Constraints:** `processBlock()` is the RT entry point — no allocations, no locks, deterministic execution, minimal overhead.

**Phase 10 additions to `processBlock()`:**

1. **Atomic snapshot** — all control parameters captured at block start via `std::memory_order_relaxed` loads into `ControlSnapshot ctrl`
2. **Exponential smoothing** — 50ms tau smoothing for gain/focus/mix parameters to prevent clicks
3. **Pause/resume fade** — 8ms linear ramp on pause/resume (RT-safe, no locks)
4. **Per-channel gain anchors** — smooth transitions when speaker count changes
5. **Smoother feedback fix** — smoother was using its own smoothed output as input (eating itself); fixed by storing raw atomics separately
6. **Master gain range** — expanded to 0.1–3.0

**Phase 11 addition:** `ctrl.autoComp` wired from `mSmooth.smoothed.autoComp` in Step 3 so focus auto-compensation flag reaches `renderBlock()` correctly.

---

## Streaming

> Consolidated from prior agent docs (subfolders removed).

**Two input modes:**

**1. Mono file mode (`--sources`):** Each source opens its own mono WAV file independently. `loadScene()` method.

**2. ADM direct streaming (`--adm`):** Shared `MultichannelReader` opens one multichannel ADM WAV, reads interleaved chunks, de-interleaves per-source. `loadSceneFromADM()` method. Eliminates ~30–60 second stem splitting and 2.9 GB disk I/O.

**Double-buffer pattern:** Each source has two pre-allocated 10-second buffers (480k frames at 48 kHz). Buffer states cycle: `EMPTY → LOADING → READY → PLAYING`. Audio thread reads from `PLAYING` buffer. At 75% consumption (7.5s runway), loader thread fills inactive buffer.

**Buffer swap is lock-free:** Audio thread atomically switches `activeBuffer` when the other buffer is `READY`. The mutex in `SourceStream` only protects `sf_seek()`/`sf_read_float()` calls and is only ever held by the loader thread.

**Buffer miss handling:** `getSample()` fades to zero at ~5ms rate (`kMissFadeRate = 0.9958f`) and increments `underrunCount`. `Streaming::totalUnderruns()` aggregates all counts for monitoring.

**Phase 11 changes:** Chunk size doubled (240k → 480k), threshold raised (50% → 75%), exponential fade-to-zero fallback, underrun counter.

**Source naming:** Source key `"11.1"` → `11.1.wav` (mono mode) or ADM channel 11 → 0-based index 10 (ADM mode). LFE `"4.1"` → channel index 3 (hardcoded ADM LFE position).

---

## ADM Direct Streaming

> Consolidated from prior agent docs (subfolders removed).

**Problem solved:** Stem splitting took 30–60 seconds and wrote ~2.9 GB to disk for typical ADM files.

**Design chosen:** Shared multichannel reader (`MultichannelReader.hpp`) with per-source de-interleave into existing double buffers.

**Channel mapping:** LUSID source key `"X.1"` → group number X → 0-based ADM channel index (X-1). LFE source key `"4.1"` → index 3.

**`MultichannelReader.hpp`:** Opens one `SNDFILE*` for the entire multichannel ADM WAV. Pre-allocates one interleaved read buffer (`chunkFrames × numChannels` floats, ~44 MB for 48 channels). Maintains channel → `SourceStream` mapping. `deinterleaveInto()` and `zeroFillBuffer()` implementations are at the **bottom** of `Streaming.hpp` (after `SourceStream` is fully defined — standard C++ circular-header pattern).

**CLI flag:** `--adm <path>` (mutually exclusive with `--sources`).

---

## Pose and Control

> Consolidated from prior agent docs (subfolders removed).

**Responsibilities:** Source position interpolation, elevation sanitization, DBAP coordinate transform.

**Per audio block:** SLERP-interpolates between LUSID keyframes to compute each source's current direction. Sanitizes elevation for the speaker layout. Applies DBAP coordinate transform (direction × layout radius → position). Outputs flat `SourcePose` vector consumed by Spatializer.

**Elevation sanitization modes:**

- `RescaleAtmosUp` (default) — maps Atmos elevations [0°, +90°] into layout's range
- `RescaleFullSphere` — maps full [-90°, +90°] range
- `Clamp` — hard clip

**DBAP coordinate quirk:** AlloLib applies internal transform `(x,y,z) → (x,-z,y)`. `Pose.hpp` compensates automatically in the coordinate transform step.

**Design constraint:** `computePositions()` is audio-thread-only. All scene data is read-only after `loadScene()`.

---

## Spatializer (DBAP)

> Consolidated from prior agent docs (subfolders removed).

**Hard real-time constraints:** Deterministic execution, no locks/allocations in `renderBuffer()`, cache-friendly memory layout, RT-safe math.

**Processing per block:**

1. Spatialize non-LFE sources via DBAP into internal render buffer (`al::Dbap`)
2. Route LFE sources directly to subwoofer channels: `masterGain * 0.95 / numSubwoofers`
3. Copy render buffer to AudioIO output

**Output channel count:** Computed from layout — `max(numSpeakers-1, max(subDeviceChannels)) + 1`. Not user-specified. Nothing hardcoded to any specific layout.

**Phase 11 additions:**

1. Focus compensation override — `autoComp` flag routes gain to `mAutoCompValue` (written by `computeFocusCompensation()`) instead of `loudspeakerMix`
2. Minimum-distance guard (0.05 m) before DBAP — prevents Inf/NaN from coincident source-speaker positions
3. Post-render clamp pass (±4.0f, NaN→0.0f) with `nanGuardCount` increment
4. `mPrevFocus` member — reserved for future threshold-gated skip of `renderBuffer` when focus is static (not yet active)

**DBAP normalization:** Sum of squared gains = 1. `--dbap_focus` controls distance rolloff (default 1.5).

---

## Compensation and Gain

> Consolidated from prior agent docs (subfolders removed).

**Problem:** Increasing DBAP focus concentrates energy on fewer speakers → perceived loudness drops. LFE routing uses `masterGain × 0.95 / numSubs` independently, creating sub-to-mains imbalance.

**Solution:** Two mix sliders + auto-compensation toggle.

| Control                                         | Range  | Default | Effect                                        |
| ----------------------------------------------- | ------ | ------- | --------------------------------------------- |
| Loudspeaker Mix (`--speaker_mix`)               | ±10 dB | 0.0     | Post-DBAP main-channel trim                   |
| Sub Mix (`--sub_mix`)                           | ±10 dB | 0.0     | Post-LFE-routing sub trim                     |
| Focus Auto-Compensation (`--auto_compensation`) | on/off | off     | Auto-updates loudspeaker mix as focus changes |

**Signal chain:** Source → masterGain → DBAP → spkMix trim → output; LFE → lfeMix trim → output.

**Real-time safety:** Relaxed atomic loads, per-channel multiply (O(N)), unity guards, `computeFocusCompensation()` on main thread only (via `update()` tick).

**New `RealtimeConfig` fields:** `loudspeakerMix`, `subMix`, `focusAutoCompensation` (atomics).

---

## LFE Router

> Consolidated from prior agent docs (subfolders removed).

**Current implementation (Phase 4):** LFE sources identified by `pose.isLFE`. Bypass DBAP routing. Route directly to all subwoofer channels: `subGain = masterGain * 0.95 / numSubwoofers`. Matches offline renderer behavior exactly.

**Deferred features (lowest priority / experimental future):** Bass management crossover filtering, phase alignment, LF extraction.

---

## Output Remap

> Consolidated from prior agent docs (subfolders removed).

**Purpose:** Map internal "layout channel order" → physical device channel order at the end of `processBlock()`.

**CSV format:** Headers `layout,device` (0-based indices). Multiple `layout` → same `device` entries accumulate. Out-of-range indices ignored with logging. Comments via `#`. Case-insensitive headers.

**Example:** `spatial_engine/remapping/exampleRemap.csv`

**Runtime:** Identity fast-path when no remap is loaded. Accumulation loop in callback. No allocations, read-only table during playback (RT-safe).

**CLI:** `--remap <path.csv>`.

---

## Threading and Safety

> Consolidated from prior agent docs (subfolders removed).

### Thread Model

| Thread            | Owner                      | Responsibilities                                |
| ----------------- | -------------------------- | ----------------------------------------------- |
| **Audio thread**  | AlloLib `AudioIO`          | `processBlock()` — RT, no locks, no allocations |
| **Loader thread** | `Streaming`                | Disk I/O, buffer filling, chunk loading         |
| **Main thread**   | Host (`gui/imgui/` or CLI) | Lifecycle, `update()`, OSC if enabled           |

### Memory Order Rules

All `RealtimeConfig` atomics use `std::memory_order_relaxed` for reads on the audio thread. The audio thread never needs sequentially-consistent ordering — it just needs the latest value, with no ordering guarantees required relative to other operations.

**6 threading invariants:**

1. Audio thread never blocks (no mutex waits, no allocation)
2. Loader thread only holds `SourceStream` mutex during `sf_seek()`/`sf_read_float()`
3. Main thread only calls `computeFocusCompensation()` (via `update()`)
4. `activeBuffer` swap is fully atomic — no torn reads
5. `shouldExit` is checked at the top of `processBlock()` — no state pollution after shutdown signal
6. `nanGuardCount` uses relaxed increment — monitoring tool only, not a synchronization signal

### OSC Runtime Parameter Delivery — The Timing Problem

> Phase 10 fix for "sliders move but values not updated" bug.

**Four-thread model:** GUI thread → python-osc UDP → kernel network stack → AlloLib ParameterServer thread → write to `RealtimeConfig` atomic. The ParameterServer thread didn't exist until after `start()` was called and the `ParameterServer listening` sentinel was printed to stdout.

**Problem:** GUI sent OSC during `LAUNCHING` state before ParameterServer was bound. Packets were silently dropped.

**Fix:**

1. Engine prints `"ParameterServer listening"` to stdout after `ParameterServer::start()`.
2. GUI monitors stdout, transitions from `LAUNCHING → RUNNING` on finding that sentinel.
3. On `engine_ready`, GUI calls `flush_to_osc()` — pushes all current control values to engine.

**For ImGui GUI (Phase 6):** OSC is the secondary surface. Primary control is direct C++ setters on `EngineSession`. This problem is moot for local GUI control. OSC flush remains relevant for remote/external OSC clients.

**Restart precaution:** On restart, reset all controls to defaults before restarting the engine. Without this, `flush_to_osc()` on `engine_ready` pushes stale values (e.g., `gain=1.5`) into the new engine instance.

---

## OSC Parameter Reference

> `allolib_parameters_reference.md` — OSC addresses and parameter specs.

OSC is the **secondary** control surface (primary is direct `EngineSession` setters). Default port: `9009`. Disable: `oscPort=0` in `EngineOptions` (passed to `configureEngine()`).

| Parameter         | OSC Address                | Type         | Range   | Default | Notes                                          |
| ----------------- | -------------------------- | ------------ | ------- | ------- | ---------------------------------------------- |
| Master Gain       | `/realtime/gain`           | float        | 0.1–3.0 | 0.5     | Master gain                                    |
| DBAP Focus        | `/realtime/focus`          | float        | 0.2–5.0 | 1.5     | DBAP rolloff exponent                          |
| Speaker Mix dB    | `/realtime/speaker_mix_db` | float        | -10–+10 | 0.0     | Post-DBAP main trim                            |
| Sub Mix dB        | `/realtime/sub_mix_db`     | float        | -10–+10 | 0.0     | Post-DBAP sub trim                             |
| Auto-Compensation | `/realtime/auto_comp`      | float (bool) | 0/1     | 0       | Focus auto-compensation                        |
| Pause/Play        | `/realtime/paused`         | float (bool) | 0/1     | 0       | Pause/resume transport                         |
| Elevation Mode    | `/realtime/elevation_mode` | float (int)  | 0/1/2   | 0       | 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp |

**Wiring:** Parameter callbacks write to `RealtimeConfig` atomics via `std::memory_order_relaxed`. `pendingAutoComp` flag: for main-thread-only `computeFocusCompensation()`.

**AlloLib parameter types:**

- `al::Parameter` (float): `get()` returns float, thread-safe
- `al::ParameterBool`: float 0.0 (false) or 1.0 (true)
- `al::ParameterServer`: UDP OSC listener, registers parameters, auto-starts listener thread

**Headers:** `al/ui/al_Parameter.hpp`, `al_ParameterBundle.hpp`, `al_ParameterServer.hpp` (already linked via CMake target).

---

## Design Doc

> `realtimeEngine_designDoc.md` — Architecture overview, design decisions, integration points.

Key architectural decisions (locked for v1):

- 48 kHz sample rate, hard real-time constraints
- Block size: 64 samples (configurable via `--buffersize`)
- DBAP for v1 (block-rate interpolation); VBAP/LBAP exist in offline renderer
- Up to 128 sources simultaneously
- PCM WAV/RF64 input (LUSID packages or ADM direct streaming)
- Runtime toggles: elevation mode, DBAP focus, master gain, speaker compensation, play/pause/restart

---

## DBAP Render Path (One Source, One Block)

> Consolidated from prior bug-audit docs; reference for anyone touching `Spatializer.hpp`.

```
getBlock() → onset-fade ramp (Bug 1.1) → masterGain multiply
→ Pass 1 soft guard → Pass 2 hard guard convergence → safePos
→ [fast-mover? angleDelta > 0.25 rad]
    YES → 4 sub-chunks, each:
            lerp positionStart→positionEnd → renorm to mLayoutRadius
            → Pass 1 soft guard → Pass 2 hard guard (per sub-step, independent)
            → renderBuffer into mFastMoverScratch → accumulate into mRenderIO
    NO  → [doBlend? guardFiredForSource || mPrevGuardFired[si]] (Bug 9.1)
            YES → 4 sub-chunks:
                    first 2 use mPrevSafePos[si] (last block's safe pos)
                    last  2 use safePos (this block's safe pos)
                    → renderBuffer into mFastMoverScratch → accumulate into mRenderIO
            NO  → single renderBuffer(mRenderIO, safePos)
→ update mPrevSafePos[si], mPrevSafeValid[si], mPrevGuardFired[si]
→ Phase 6: spkMix trim (mains), lfeMix trim (subs)
→ Phase 14 pre-copy measurement (render-bus active mask, DOM/CLUSTER latches)
→ Phase 7: identity copy mRenderIO → io.outBuffer()
→ Phase 14 post-copy measurement (device active mask)
```

The coordinate flip `rp = Vec3f(pos.x, -pos.z, pos.y)` is applied before the guard and undone after — all guard math is in DBAP-internal space, not pose space.

---

## Proximity Guard Structure

**Pass 1 — soft zone** (`kGuardSoftZone = 0.45 m`): single scan. Symmetric parabolic bump: `u = (dist - kMin) / (kSoft - kMin)`, `push = zoneWidth * u * (1-u)`. Zero effect at both boundaries, peak at midpoint. No iteration.

**Pass 2 — hard floor** (`kMinSpeakerDist = 0.15 m`): convergence loop (`kGuardMaxIter = 4`). Scans all speakers; if any source is within 0.15 m, pushes out and restarts. Breaks when no speaker fires.

`guardFiredForSource` is set when Pass 2 fires. `speakerProximityCount` incremented per source-block where the hard floor fires.

**Cross-block blending (Bug 9.1, normal path only):** Three per-source state vectors — `mPrevSafePos`, `mPrevSafeValid`, `mPrevGuardFired` — track the previous block's guard-resolved position and whether Pass 2 fired. When a guard transition is detected, the normal path splits into 4 sub-steps: first two use `mPrevSafePos[si]`, last two use `safePos`. Eliminates the ~23% DBAP gain step at block boundaries during guard entry/exit. State updated at the end of every source render.

**Deferred — fast-mover intra-block guard:** Intra-block guard-state discontinuity in the fast-mover sub-step path is geometrically real but produced no consistent audible evidence after Bug 9.1. Do not implement unless pops recur consistently and correlate with fast-mover sources.

---

## Diagnostic Output Key

```
t=42.5s  CPU=23%  rDom=0xffff  dDom=0xffff  rBus=0x3ffff  dev=0x3ffff  mainRms=0.031  subRms=0.004  Xrun=0  NaN=0  SpkG=14  PLAYING
[RELOC-RENDER]   t=42.5s  mask: 0xffff → 0x3ffff
[DOM-RENDER]     t=42.5s  dom:  0xffff → 0x3ffff
[CLUSTER-RENDER] t=42.5s  top4: 0x0123 → 0x0456
```

| Field                                 | Meaning                                                                                                                          |
| ------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| `rBus` / `dev`                        | Active channel bitmask: render bus / device output. `0x3ffff` = 18 ch (16 main + 2 sub). If these differ → output-layer problem. |
| `[RELOC]` toggling `0x3ffff ↔ 0xffff` | Sub channels turning on/off with LFE content. **Expected, not a bug.**                                                           |
| `[CLUSTER]`                           | 2+ of the top-4 mains changed. Correlate with audible pops/relocations.                                                          |
| `SpkG`                                | `speakerProximityCount` this 500 ms window. Nonzero = hard-floor guard active.                                                   |
| `NaN`                                 | `nanGuardCount`. Should be 0.                                                                                                    |
| `CPU`                                 | Wall-clock callback load as `elapsed_µs / block_budget_µs`, capped at 2.0.                                                       |

---

## Speaker Layout Quick Reference (translab-sono-layout.json)

- 16 main speakers (channels 0–15), 2 subs (channels 16, 17)
- Ring 0 (ch 0–7): elevation ~1.75° (near-horizontal)
- Ring 1 (ch 8–15): elevation ~38–44° (elevated)
- Layout is 3D (`mLayoutIs2D = false`) → `RescaleAtmosUp` fires for all sources with z > 0
- Median radius ~5.065 m (`mLayoutRadius`, used as DBAP position scale)
- Sub channels 16–17 always excluded from `domMask` and DBAP gain output

---

## What Not To Do

- Do not redesign auto-compensation (returns 1.0f — plumbing exists, math was wrong; deferred).
- Do not reopen broad device/output mismatch analysis — Bug 3.2 + Bug 8.1 resolved the structural issue.
- Do not reopen guard-pop investigation — Bug 9.1 confirmed closed (translab 2026-04-01). Reopen only if pops recur consistently in later testing.
- Do not implement the fast-mover guard patch — deferred; no consistent audible evidence after Bug 9.1.
- Do not add per-sample or per-source logging in the audio callback (RT-unsafe).
- Do not change DBAP render granularity (block-based is canonical; per-sample was tried and reverted).
- Do not modify `runPipeline.py` — deprecated and removed. Headless runs use the engine binary directly.

---

## Field Testing Log

> `realtime_testing.md` — Translab field testing notes (340 KB log file, April 2026).

Most recent results (April 1, 2026, Translab):

- Ascent test 1: no bugs, perfect
- Swale test 1: perfect
- 360RA test 1 (rescale full sphere mode): 1 pop around 96s, no other issues
- 360RA test 2: no issues

> The full testing log is large. Read `realtime_testing.md` directly for session-by-session bug tracking.
