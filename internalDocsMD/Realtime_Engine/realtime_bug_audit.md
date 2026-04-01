# Realtime Engine Bug Audit — 2026-04-01

## How to use this document

This document is the **primary onboarding reference** for any agent or developer working on the realtime engine. Read this before touching code. Do not re-derive things already documented here.

### Bug numbering convention

Each distinct root-cause problem is assigned a **bug number** (integer). Each patch attempt for that bug is a **sub-number**:

- `8` — Bug 8: the problem description
- `8.1` — First patch attempt for Bug 8 (may be a plan, partial fix, or full fix)
- `8.2` — Second attempt (if 8.1 was insufficient or revised)

Bugs are listed **newest first**. Closed bugs follow open ones.

---

## Agent onboarding — read before anything else

### What this engine does

The realtime engine reads a `.lusid.json` scene file, streams audio from a multitrack WAV, and renders spatial audio to a multichannel audio device in real time using DBAP panning. It runs as a C++ process controlled via OSC from a Python/Qt GUI.

### Key source files and their roles

| File | Role |
|---|---|
| `spatial_engine/realtimeEngine/src/Spatializer.hpp` | Core DBAP render loop. All panning logic lives here: proximity guard (Pass 1 soft zone + Pass 2 hard floor), fast-mover sub-stepping, Phase 6 mix trims (spkMix/lfeMix), Phase 7 device copy, Phase 14 diagnostic measurement points. **Most bugs touch this file.** |
| `spatial_engine/realtimeEngine/src/Pose.hpp` | Keyframe interpolation pipeline: SLERP between ADM keyframes → `safeDirForSource` → `sanitizeDirForLayout` → `directionToDBAPPosition`. Computes `SourcePose::position` (block center), `positionStart`, `positionEnd`. Pose is known clean — do not suspect it without evidence. |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Audio callback controller. Owns `ControlSmooth` (50 ms exponential smoother for gain/focus), `processBlock()` orchestration (Steps 1–6), per-block timing, CPU meter. All config values reach the audio thread exclusively through `mSmooth`. |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` | `RealtimeConfig` atomics (written by OSC/CLI, read by audio thread via smoother). `EngineState` diagnostic counters: `speakerProximityCount`, `nanGuardCount`, `renderActiveMask`, `deviceActiveMask`, DOM/CLUSTER relocation latches, `callbackCpuLoad`. Read-only reference for audio thread. |
| `spatial_engine/realtimeEngine/src/main.cpp` | CLI arg parsing, OSC parameter setup, monitoring loop (prints diagnostic counters every 500 ms). |
| `spatial_engine/realtimeEngine/src/Streaming.hpp` | Per-source audio streaming from the multichannel ADM WAV. `parseChannelIndex()` maps source name to 0-based ADM channel: `"N.1" → N-1`, `"LFE" → 3`. |
| `gui/realtimeGUI/realtimeGUI.py` | Qt GUI window. `_on_start()` builds `RealtimeConfig`, resets controls, launches engine. `_on_restart()` calls `reset_to_defaults()` before relaunch (Bug 4.1). |
| `gui/realtimeGUI/realtime_runner.py` | `RealtimeConfig` dataclass. `_build_args()` constructs the CLI argv list passed to the C++ process. |
| `gui/realtimeGUI/RealtimeInputPanel.py` | Input panel UI: layout path, source path, remap CSV, buffer size. |

### Speaker layout quick reference (translab-sono-layout.json)

- 16 main speakers (channels 0–15), 2 subs (channels 16, 17)
- Ring 0 (ch 0–7): elevation ~1.75° (near-horizontal)
- Ring 1 (ch 8–15): elevation ~38–44° (elevated)
- Layout is 3D (`mLayoutIs2D = false`) → `RescaleAtmosUp` mode fires for all sources with z > 0
- Median radius ~5.065 m (used as DBAP position scale / `mLayoutRadius`)
- Sub channels 16–17 are always excluded from `domMask` and DBAP gain output

### DBAP render path summary (one source, one block)

```
getBlock() → onset-fade ramp (Bug 1.1) → masterGain multiply
→ [fast-mover?]
    YES → 4 sub-chunks, each:
            lerp positionStart→positionEnd → renorm to mLayoutRadius
            → Pass 1 soft guard → Pass 2 hard guard
            → renderBuffer into mFastMoverScratch → accumulate into mRenderIO
    NO  → [guard-blend? (Bug 9.1 — planned)]
            YES → 4 sub-chunks: first 2 use mPrevSafePos[si], last 2 use safePos
            NO  → single renderBuffer(mRenderIO, safePos, ...)
→ Phase 6: spkMix trim (mains), lfeMix trim (subs)
→ Phase 14 pre-copy measurement (render-bus active mask, DOM/CLUSTER latches)
→ Phase 7: identity copy mRenderIO → io.outBuffer()
→ Phase 14 post-copy measurement (device active mask)
```

### Proximity guard structure (as of 2026-03-28)

Two passes applied in **both** the normal path and each fast-mover sub-step:

**Pass 1 — soft zone** (`kGuardSoftZone = 0.45 m`): single scan, symmetric parabolic bump pushing sources away from each speaker. Zero effect at the boundary, peak at midpoint, zero at the hard floor — smooth and C1-continuous.

**Pass 2 — hard floor** (`kMinSpeakerDist = 0.15 m`): convergence loop (`kGuardMaxIter = 4`). Scans all speakers; if any source is within 0.15 m, pushes it out and restarts the scan. Continues until no speaker fires or iteration limit reached.

`guardFiredForSource` is set when Pass 2 fires. `speakerProximityCount` is incremented per source per block where the hard floor fires.

### Diagnostic output key

```
t=42.5s  CPU=23%  rDom=0xffff  dDom=0xffff  rBus=0x3ffff  dev=0x3ffff  mainRms=0.031  subRms=0.004  Xrun=0  NaN=0  SpkG=14  PLAYING
[RELOC-RENDER] t=42.5s  mask: 0xffff → 0x3ffff       ← sub channels appearing (expected)
[DOM-RENDER]   t=42.5s  dom:  0xffff → 0x3ffff        ← dominant-set change (mains only after Bug 6.2)
[CLUSTER-RENDER] t=42.5s  top4: 0x0123 → 0x0456      ← 2+ of top-4 mains changed
```

| Field | Meaning |
|---|---|
| `rBus` / `dev` | Active channel bitmask: render bus / device output. `0x3ffff` = 18 ch active (16 main + 2 sub). If these differ → output-layer problem. If they always agree → render path is correct. |
| `rDom` / `dDom` | Dominant-set mask (mains only, post-Bug-6.2). Steady `0xffff` = all 16 mains above threshold. |
| `[RELOC]` toggling `0x3ffff ↔ 0xffff` | Sub channels turning on/off with scene content. **Expected, not a bug.** |
| `[CLUSTER]` event | 2+ of the top-4 mains changed. Correlate timestamps with audible pops. |
| `SpkG` | `speakerProximityCount`: hard-floor guard events this 500 ms window. Nonzero = guard is active. |
| `NaN` | `nanGuardCount`: samples that hit the ±4.0 clamp. Should be 0 during normal playback. |

### What not to do

- Do not redesign auto-compensation (returns 1.0f — plumbing exists, math was wrong; deferred).
- Do not reopen broad device/output mismatch analysis — the C++ post-open guard (Bug 3.2) is correct. The remaining device problem is OS device selection (Bug 8).
- Do not add per-sample or per-source logging in the audio callback (RT-unsafe).
- Do not modify `runPipeline.py` — deprecated. Use `runRealtime.py`.
- Do not change DBAP render granularity (block-based is canonical; per-sample was tried and reverted).
- Do not re-litigate the structural parity audit — confirmed complete (see `3-8-bug-diagnoses.md`). Numerical comparison is deferred as Bug 10.

---

## Change log template

Use this format for every new patch attempt. Copy and fill in.

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

---

## Open bugs

---

### Bug 9 — Fast-mover intra-block guard transition pop

**Root cause:** In the fast-mover sub-step path (`Spatializer.hpp`), the proximity guard is applied independently per sub-step. When a source sweeps through the `kMinSpeakerDist = 0.15 m` boundary mid-block, adjacent sub-steps have different guard states. DBAP gain on the dominant speaker changes ~23% at the boundary (`focus=1.5`: `0.14^-3 ≈ 364` vs `0.15^-3 ≈ 296`). This step falls at a 128-sample boundary — audible as a click or high-pitched transient.

**Evidence:** Ascent pops at specific timestamps (~115 s, ~198 s, ~345 s). `SpkG` and `NaN` stay near 0 during these events, ruling out the hard-floor guard and clamp. The intra-block step is geometrically guaranteed whenever a fast-moving source crosses the guard boundary within a block.

**Relevant code:**
- `Spatializer.hpp` — fast-mover `for (int j = 0; j < kNumSubSteps; ++j)` block in the DBAP branch. The guard currently runs inside this loop on each `subSafePos` independently.
- `Spatializer.hpp` — normal path `if (!isFastMover)` block: needs `mPrevSafePos` blending for guard-transition blocks.
- `Spatializer.hpp` — `prepareForSources(size_t n)`: extend to allocate 3 new per-source state vectors.

---

### Bug 9.1 — Pre-guard endpoint blending — PLAN

**Approach (fast-mover path):** Pre-guard `positionStart` and `positionEnd` once before the sub-step loop, then lerp between those pre-guarded endpoints inside the loop. Lerp between two guard-resolved positions is smooth by construction.

```cpp
auto applyGuard = [&](al::Vec3f posePos) -> al::Vec3f {
    al::Vec3f rp(posePos.x, -posePos.z, posePos.y);  // flip
    // Pass 1: soft zone
    for (const auto& spkVec : mSpeakerPositions) { /* parabolic bump */ }
    // Pass 2: hard floor convergence
    for (int iter = 0; iter < kGuardMaxIter; ++iter) { /* push loop */ }
    return al::Vec3f(rp.x, rp.z, -rp.y);  // un-flip
};

al::Vec3f guardedStart = applyGuard(pose.positionStart);
al::Vec3f guardedEnd   = applyGuard(pose.positionEnd);

for (int j = 0; j < kNumSubSteps; ++j) {
    float alpha = (static_cast<float>(j) + 0.5f) / static_cast<float>(kNumSubSteps);
    al::Vec3f subSafePos = guardedStart + alpha * (guardedEnd - guardedStart);
    float mag = subSafePos.mag();
    if (mag > 1e-7f) subSafePos = (subSafePos / mag) * mLayoutRadius;
    // no per-step guard — endpoints already guarded
    mFastMoverScratch.zeroOut();
    mDBap->renderBuffer(mFastMoverScratch, subSafePos,
                        mSourceBuffer.data() + j * subFrames, subFrames);
    // accumulate...
}
```

**Approach (normal path — cross-block guard-transition):** When guard fired this block or last block, use the 4-sub-step scratch infrastructure to blend `mPrevSafePos[si]` (sub-steps 0–1) → `safePos` (sub-steps 2–3). New state vectors in `Spatializer.hpp` private section:

```cpp
std::vector<al::Vec3f>  mPrevSafePos;    // guard-resolved position from last block
std::vector<uint8_t>    mPrevSafeValid;  // 1 = initialized
std::vector<uint8_t>    mPrevGuardFired; // 1 = guard fired last block
```

Normal-path logic (in `if (!isFastMover)` block):
```cpp
const bool doBlend = mPrevSafeValid[si] && (guardFiredForSource || mPrevGuardFired[si]);

if (doBlend) {
    for (int j = 0; j < kNumSubSteps; ++j) {
        const al::Vec3f& subPos = (j < kNumSubSteps / 2) ? mPrevSafePos[si] : safePos;
        mFastMoverScratch.zeroOut();
        mDBap->renderBuffer(mFastMoverScratch, subPos,
                            mSourceBuffer.data() + j * subFrames, subFrames);
        // accumulate into mRenderIO at frame offset j * subFrames
    }
} else {
    mDBap->renderBuffer(mRenderIO, safePos, mSourceBuffer.data(), numFrames);
}
// Save state for next block
mPrevSafePos[si]    = safePos;
mPrevSafeValid[si]  = 1u;
mPrevGuardFired[si] = guardFiredForSource ? 1u : 0u;
```

Move `subFrames` definition above the `isFastMover` branch so both paths share it.

**Files to change:** `Spatializer.hpp` only.

**RT-safety:** Zero allocation in audio callback. State vectors are audio-thread-owned after `start()`. 3 vectors × numSources × small types = negligible.

**Status:** PLAN

---

### Bug 8 — Non-deterministic output device routing

**Root cause:** `RealtimeBackend::init()` calls `mAudioIO.init()` without specifying an output device. AlloLib/PortAudio opens the macOS system default. If the system default is not the MOTU, audio goes to the wrong device. Bug 3.2 now correctly refuses to start on channel count mismatch — but if the wrong device happens to have enough channels, the engine starts silently on the wrong device.

**Evidence (2026-03-28 Canyon tests):** `rBus` and `dev` always agree — the render path is correct. "Outside-layout" hardware signal observed on the MOTU is OS-level device routing, not a C++ render bug.

**Relevant code:**
- `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` — `init()`, insert device resolution before the `mAudioIO.init(...)` call
- `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` — `RealtimeConfig` struct (add `outputDeviceName`)
- `spatial_engine/realtimeEngine/src/main.cpp` — CLI arg parsing section and usage string
- `gui/realtimeGUI/realtime_runner.py` — `RealtimeConfig` dataclass and `_build_args()`
- `gui/realtimeGUI/RealtimeInputPanel.py` — `_build_ui()`, add dropdown after Buffer Size row

**AlloLib API available (no new dependencies):**
```cpp
al::AudioDevice dev("MOTU", al::AudioDevice::OUTPUT);
dev.valid()            // false if not found by name
dev.channelsOutMax()   // hardware max output channels
mAudioIO.deviceOut(dev);  // call before mAudioIO.init()
al::AudioDevice::printAll();  // enumerate all devices to stdout
```

---

### Bug 8.1 — Explicit --device flag + GUI dropdown — PLAN

**Files to change:**

| File | Change |
|---|---|
| `RealtimeTypes.hpp` | Add `std::string outputDeviceName;` to `RealtimeConfig` |
| `RealtimeBackend.hpp` | Device resolution + pre-open channel validation in `init()` before `mAudioIO.init()` |
| `main.cpp` | Parse `--device <keyword>`, `--list-devices`; assign `config.outputDeviceName`; early-exit on `--list-devices` |
| `realtime_runner.py` | Add `output_device: Optional[str] = None` to dataclass; emit `--device` in `_build_args()` if set |
| `RealtimeInputPanel.py` | Add Output Device dropdown populated via `sounddevice.query_devices()` (falls back to "System Default" if unavailable); `get_output_device()` accessor |
| `realtimeGUI.py` | Pass `output_device=self._input_panel.get_output_device()` in `_on_start()` |

**Failure modes covered by this patch:**
- No `--device` given → system default, no regression
- Name not found → `dev.valid()` false → clear error, refuse to start
- Device found but insufficient channels → `channelsOutMax()` check → clear error, refuse to start
- OS clamps channel count on open → existing Bug 3.2 post-open guard fires

**Status:** PLAN

---

## Closed bugs

---

### Bug 7 — Guard-induced relocation and buzzing

**Root cause:** The proximity guard was a single sequential pass. After pushing a source away from speaker K, the new position could fall inside speaker K+1's zone, which pushed it again. The result was order-dependent: small input-position differences between consecutive blocks produced different push sequences → different DBAP gain clusters each block → audible cluster relocation and buzzing. A separate issue: no smooth transition existed before the hard floor — a source entering the 0.15 m zone snapped immediately to the exclusion radius.

---

### Bug 7.1 — Guard convergence loop — PATCHED (2026-03-28)

**Files changed:**
- `Spatializer.hpp`: Added `kGuardMaxIter = 4` constant adjacent to `kMinSpeakerDist`. Replaced single-pass guard in both the normal path and the fast-mover per-sub-step path with a convergence loop: outer `for (iter < kGuardMaxIter)`, inner speaker scan with a `pushed` flag; breaks early when no speaker fires. Result is geometrically consistent and order-independent.

**Status:** PATCHED

---

### Bug 7.2 — Soft-repulsion outer zone — PATCHED (2026-03-28)

**Files changed:**
- `Spatializer.hpp` (~line 1021): Added `kGuardSoftZone = 0.45f`.
- `Spatializer.hpp` (normal-path guard block, ~lines 453–489): Added Pass 1 before the existing convergence loop (now Pass 2). Pass 1: single scan, symmetric parabolic bump — `u = (dist - kMinSpeakerDist) / (kGuardSoftZone - kMinSpeakerDist)`, `push = zoneWidth * u * (1 - u)`. Zero at both boundaries, peak at midpoint. No iteration.
- `Spatializer.hpp` (fast-mover per-sub-step guard, ~lines 549–577): Same two-pass structure applied.

**Test result (Canyon Test 1, 2026-03-28):** `SpkG` still at 30 from ~148 s; CLUSTER events every ~0.5 s; audible relocation ~150 s, buzzing ~175 s. Render path confirmed correct (`rBus == dev` throughout). Remaining Canyon symptoms are OS device routing — Bug 8 is the correct next fix.

**Status:** PATCHED (guard improved; remaining Canyon symptoms attributed to Bug 8)

---

### Bug 6 — No visibility into channel-relocation events

**Root cause:** When channels disappeared or relocated at runtime, there were no diagnostics to determine whether the failure was in `mRenderIO` (render layer) or `io.outBuffer()` (device/output layer). AlloLib's `mAudioIO.cpu()` capped at 1.0 and was unreliable.

---

### Bug 6.1 — Phase 14: channel-relocation diagnostic + CPU meter — PATCHED (2026-03-25)

Added to `RealtimeTypes.hpp`: `renderActiveMask`, `deviceActiveMask`, DOM/CLUSTER relocation event latches (`renderRelocEvent`, `deviceRelocEvent`, etc.), `mainRmsTotal`, `subRmsTotal`, `callbackCpuLoad`.

Added to `Spatializer.hpp` in `renderBlock()`: pre-copy measurement block (after NaN clamp, before Phase 7) and post-copy measurement block. Each scans its buffer, sets active mask bits where block mean-square > 1e-8, fires relocation latch on change.

Added to `RealtimeBackend.hpp`: `mCallbackStart` (`std::chrono::steady_clock::time_point`) captured at top of callback; `callbackCpuLoad` stored as `elapsed_µs / block_budget_µs`, capped at 2.0.

Added to `main.cpp`: pre-loop channel count print; monitoring loop prints `[RELOC-RENDER]`, `[RELOC-DEVICE]`, `[DOM-RENDER]`, `[DOM-DEVICE]`, `[CLUSTER-RENDER]`, `[CLUSTER-DEVICE]` events and updated status line.

**Status:** PATCHED

---

### Bug 6.2 — Phase 14A: DOM mask anchored to mains only — PATCHED (2026-03-27)

`domThresh` was computed from `maxMs` (global max including subs). Sub RMS fluctuations caused `maxMs` to collapse, triggering spurious bulk DOM events across all mains.

Fixed in `Spatializer.hpp`: added `maxMainMs` (same loop, `!isSubwooferChannel` guard). Changed `domThresh = maxMs * kDomRelThresh` → `domThresh = maxMainMs * kDomRelThresh`. Added `!isSubwooferChannel` guard to `domMask` bit-setting. Applied identically in both pre-copy and post-copy measurement blocks.

**Status:** PATCHED

---

### Bug 6.3 — Phase 14B: Top-4 cluster tracking — PATCHED (2026-03-27)

DOM events fired on any single-channel threshold crossing — too sensitive to far-field DBAP bleed.

Added top-4 main-channel tracking: O(4 × channels) selection per block, no allocation. `[CLUSTER]` event fires only when new top-4 overlaps previous top-4 by fewer than 3 channels (2+ changed). Added 8 new atomic fields to `RealtimeTypes.hpp` (`renderClusterMask`, `renderClusterPrev/Next/Event`, and device-side equivalents). Monitoring loop in `main.cpp` prints `[CLUSTER-RENDER]` and `[CLUSTER-DEVICE]`.

**Status:** PATCHED

---

### Bug 5 — Smoother cold-start transient

**Root cause:** `ControlSmooth::smoothed` hardcoded to `{masterGain=1.0, focus=1.0}` at construction, regardless of CLI values. First ~200 ms of every run played at wrong gain/focus.

---

### Bug 5.1 — Smoother pre-seeding — PATCHED (2026-03-09), re-applied (2026-03-18)

`RealtimeBackend` constructor seeds `mSmooth.smoothed` and `mSmooth.target` from `mConfig` atomics before any block runs. Was patched in Session 3 but absent from the file by Session 5 (dropped during an edit); re-applied.

**File:** `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` — constructor body.

**Status:** PATCHED

---

### Bug 4 — GUI restart sends stale slider state to new engine

**Root cause:** Restart button wired directly to `RealtimeRunner.restart()`, bypassing `_on_start()`. `reset_to_defaults()` was never called. `flush_to_osc()` fired on `engine_ready` and sent the current slider values (e.g. `gain=1.5` left from a prior run) to the freshly-started engine. `RealtimeConfig.master_gain` was never updated by slider moves — only the Qt widget and the OSC output changed — so CLI always launched with correct defaults, but OSC immediately overrode them with stale values.

---

### Bug 4.1 — Restart control reset — PATCHED (2026-03-09)

Changed `t.restart_requested.connect(self._runner.restart)` → `t.restart_requested.connect(self._on_restart)`. Added `_on_restart()`: calls `self._controls_panel.reset_to_defaults()` (silent, `emit=False`) then `self._runner.restart()`.

**File:** `gui/realtimeGUI/realtimeGUI.py`

**Status:** PATCHED

---

### Bug 3 — Engine starts silently with wrong channel count

**Root cause:** After `mAudioIO.init()`, the OS-negotiated channel count was never validated. If the system default device had fewer channels than the layout required, the engine started and silently discarded the extra speaker channels. Additionally the first validation attempt (3.1) used the wrong AlloLib accessor and never fired.

---

### Bug 3.1 — Post-open channel validation using wrong accessor — REVERTED (2026-03-09)

Used `mAudioIO.channelsOut()` which returns `mNumO` (the requested count, always equal to `mConfig.outputChannels`). Comparison was always false. Guard never fired. Log printed the requested count and labelled it "Actual".

**Status:** REVERTED (superseded by 3.2)

---

### Bug 3.2 — Correct accessor: channelsOutDevice() — PATCHED (2026-03-09)

Changed `mAudioIO.channelsOut()` → `mAudioIO.channelsOutDevice()` in the post-open guard. `channelsOutDevice()` returns `oParams.nChannels` — the value set by `AudioBackend::channels()` after clamping requested count to `min(requested, deviceMax)`. Engine now correctly refuses to start when negotiated hardware channels < layout requirements.

**File:** `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` — `init()`, `actualOutChannels` variable and comparison.

**Status:** PATCHED

---

### Bug 2 — Block-boundary motion discontinuity

**Root cause:** One DBAP position per block: gains are constant within the block, step at the boundary. For sources moving a large angular distance between blocks (~10 ms at 48 kHz / 512 frames), the DBAP gain vector jumped instantaneously — audible as a click.

---

### Bug 2.1 — Fast-mover sub-stepping — PATCHED (2026-03-18)

Sources with angular change > `kFastMoverAngleRad = 0.25 rad` (~14.3°) per block are rendered as 4 sub-chunks of 128 frames each. Each sub-chunk uses a position linearly interpolated between `positionStart` and `positionEnd`, renormalized to `mLayoutRadius`, then guarded. `mFastMoverScratch` pre-allocated in `init()`.

Extended `Pose::computePositions()` signature from `(double blockCenterTimeSec)` to `(double blockStartSec, double blockEndSec)`. Center derived internally as midpoint. `positionStart` / `positionEnd` computed via `computePositionAtTimeReadOnly()` — does not mutate `mLastGoodDir`.

**Files:** `Spatializer.hpp`, `Pose.hpp`, `RealtimeBackend.hpp`

**Status:** PATCHED

---

### Bug 1 — Source onset pop

**Root cause:** On the first active block after silence, `getBlock()` returned real audio samples immediately at full amplitude. DBAP multiplied this step-from-zero by speaker gains, producing a wideband low-end thump.

---

### Bug 1.1 — Onset fade — PATCHED (2026-03-09)

128-sample linear ramp (`kOnsetFadeSamples = 128`, ~2.7 ms at 48 kHz) applied on the first active block. Gate: block energy > `kOnsetEnergyThreshold = 1e-10f`. Ramp placed before masterGain multiply. Applied identically in both LFE and DBAP paths. `mSourceWasSilent[]` (`std::vector<uint8_t>`) preallocated by `prepareForSources(size_t n)`. Zero allocation in audio callback.

**Files:** `Spatializer.hpp`, `main.cpp`

**Status:** PATCHED

---

## Deferred / open questions

- **Bug 10 (deferred): Numerical parity with offline renderer.** Structural parity confirmed (`3-8-bug-diagnoses.md`). Not numerically verified. Primary concern: `splitStems.py` may write mono WAVs at PCM_16 rather than FLOAT (16-bit quantization). Check: `sf.info('processedData/stageForRender/1.1.wav').subtype` should be `'FLOAT'`. Full comparison procedure in `3-8-bug-diagnoses.md` Section 5.
- **`kMinSpeakerDist` tuning:** 0.15 m may be too large for 360RA DirectSpeaker content (sources at 0.02–0.10 m from target speakers). Consider reducing to ~0.05 m after Bug 9 is confirmed fixed.
- **Remap load-order bug:** `outputRemap.load()` passes `config.outputChannels` for both `renderChannels` and `deviceChannels` args. `deviceChannels` should be the post-open actual count. Latent; only affects `--remap` path; not currently blocking.
