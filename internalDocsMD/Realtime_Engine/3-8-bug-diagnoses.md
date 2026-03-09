# Behavioral Parity Audit — March 8, 2026

## Realtime Engine vs Offline Renderer (DBAP, Swale scene)

---

## Status

Structural parity audit is **complete**. The session ended before a numerical comparison was run. The next agent should **run a numerical signal-path comparison** — see Section 5 for the exact procedure.

---

## 1. What Was Audited

Both engines were read in full:

| File                                                    | Role                                                               |
| ------------------------------------------------------- | ------------------------------------------------------------------ |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | RT audio callback, per-block controller                            |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`     | RT DBAP render, gain chain, LFE routing                            |
| `spatial_engine/realtimeEngine/src/Pose.hpp`            | RT keyframe interpolation, elevation sanitization, coord transform |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`       | RT audio source delivery (ADM direct streaming, channel mapping)   |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | RT config/state types, defaults                                    |
| `spatial_engine/src/renderer/SpatialRenderer.cpp`       | Offline DBAP render, gain chain, sub-stepping                      |
| `spatial_engine/src/renderer/SpatialRenderer.hpp`       | Offline config defaults, constants                                 |
| `spatial_engine/src/JSONLoader.cpp`                     | Scene loader — shared by both                                      |
| `spatial_engine/src/WavUtils.cpp`                       | Offline mono WAV loading — `{name}.wav` naming                     |
| `src/packageADM/splitStems.py`                          | Offline pre-processing: ADM → mono files                           |
| `runPipeline.py` / `runRealtime.py`                     | Pipeline entry points                                              |

---

## 2. Confirmed IDENTICAL (Steady-State Signal Path)

- `directionToDBAPPosition`: `Vec3f(pos.x, pos.z, -pos.y)` — byte-identical in both
- Speaker construction: `al::Speaker(i, az*180/π, el*180/π, 0, r)` — identical
- Layout radius: median of speaker distances — identical calculation
- SLERP interpolation + clamp-to-last-keyframe: identical
- Elevation sanitization (`sanitizeDirForLayout`): identical switch logic, identical `RescaleAtmosUp` default
- LFE gain formula: `masterGain * 0.95 / numSubwoofers` — identical
- `gainChain` masterGain default: **both default to 0.5** (RT: `RealtimeConfig::masterGain{0.5f}`; offline: `RenderConfig::masterGain = 0.5`)
- Block-center timing: `(blockStart + blockLen/2) / sr` — equivalent
- `direct_speaker` and `audio_object` nodes: treated identically in both (source `"4.1"` with `type=LFE` becomes key `"LFE"` in JSONLoader — done at load time)

---

## 3. Confirmed Differences (Behavioral Gaps)

### 3A. Smoother Cold-Start (SECONDARY — does not affect steady state)

`ControlSmooth::smoothed` always initialises to `{masterGain=1.0, focus=1.0}` regardless of CLI values stored in `mConfig`. CLI passes `masterGain=0.5`, `focus=1.5` (standard defaults) but the smoother doesn't read them until processBlock block 0.

Result: first ~200 ms of every cold-start plays at 2× intended gain and lower-than-intended DBAP focus, then ramps to correct values.

**Fix (ready, not yet applied):** Seed `mSmooth.smoothed` from `mConfig` atomics at end of `Backend::init()`, before audio starts.

```cpp
mSmooth.smoothed.masterGain     = mConfig.masterGain.load(std::memory_order_relaxed);
mSmooth.smoothed.focus          = mConfig.dbapFocus.load(std::memory_order_relaxed);
mSmooth.smoothed.loudspeakerMix = mConfig.loudspeakerMix.load(std::memory_order_relaxed);
mSmooth.smoothed.subMix         = mConfig.subMix.load(std::memory_order_relaxed);
```

Location: `RealtimeBackend.hpp`, end of `init()`, after the `mAudioIO.open()` block.

---

### 3B. Audio Source Material — DIFFERENT DECODE PATH (HIGHEST PRIORITY, NOT YET NUMERICALLY VERIFIED)

**Offline pipeline:**

1. `src/packageADM/splitStems.py::splitChannelsToMono()` — reads the multichannel ADM WAV with `soundfile.read()` (Python `float64` precision), extracts each channel as mono.
2. Files written as `{chanNumber}.1.wav` (e.g. `1.1.wav`, `2.1.wav`), except channel 4 → `LFE.wav` (hardcoded `_DEV_LFE_HARDCODED = True`).
3. `JSONLoader` maps `type=LFE` node `id="4.1"` to source key `"LFE"`.
4. `WavUtils::loadSources()` reads `{name}.wav` files — so it opens `1.1.wav`, `2.1.wav`, ..., `LFE.wav`.
5. Offline passes through `libsndfile` as 32-bit float.

**Realtime pipeline:**

1. Reads the original multichannel ADM WAV directly via `MultichannelReader` (libsndfile, `float32`).
2. `parseChannelIndex()` maps source names to 0-based ADM channels: `"N.1" → channel (N-1)`, `"LFE" → channel 3` (channel 4 in 1-based).
3. De-interleaves at audio callback time — no pre-split step.

**The concern:** The intermediate `soundfile.write()` / `libsndfile.read()` round-trip in the offline path introduces a WAV encode-decode step per channel. Even at `SF_FORMAT_FLOAT` (32-bit IEEE), this should be lossless, but:

- Were the original mono files written at float32 or a different bit depth?
- Does `soundfile` (Python) default to a different sample format than what `libsndfile` reads back as?
- Does the Python `float64 → float32` downcast during stem splitting lose precision?

**This is the most plausible source of a steady-state numerical difference.** It is not a code logic difference — it is a data pipeline difference that would cause every source's audio samples to differ between engines, producing systematic differences across all speaker outputs.

---

### 3C. Fast-Mover Sub-Stepping (Offline Only)

Offline `renderPerBlock` detects sources moving >14° (`kFastMoverAngleRad = 0.25 rad`) within a block and re-renders with sub-block positions (`kSubStepHop = 16` samples). Realtime uses one position per block for every source.

For **Swale**: sources 19.1 and 20.1 are the only dynamic ones. Their motion rate should be verified, but is unlikely to exceed 14°/block at 512 frames (10.7 ms). For other scenes with fast trajectories this will produce a spatial difference.

---

### 3D. `loudspeakerMix` / `subMix` Post-DBAP Trim (Realtime Only)

Offline has no such sliders. At GUI defaults (0 dB → 1.0), no signal difference. If GUI sends non-unity values on startup via `flush_to_osc()`, the mix will differ from offline. Defaults confirmed: `DEFAULTS["speaker_mix_db"] = 0.0`, `DEFAULTS["sub_mix_db"] = 0.0` — so at default state no gap.

---

### 3E. NaN/Clamp Guard (Threshold Difference)

- Offline: `isfinite` check only, replaces NaN with 0.0 at output copy
- Realtime: full ±4.0 clamp + `isfinite` guard, post-DBAP pre-copy

For normal audio levels this is never triggered. Not a source of steady-state difference.

---

### 3F. Silent-Source Skip (Offline Only)

Offline skips DBAP for blocks where source abs-sum < threshold. Realtime always processes all sources. Mathematically equivalent: `DBAP(0) = 0`. No signal difference.

---

## 4. What Was NOT Yet Done

The audit was **structural / code-reading only**. No actual audio samples were compared. The claim "identical at steady state" is **NOT yet verified numerically**.

Specifically unverified:

1. Whether the mono WAV files written by `splitStems.py` are **bit-identical** to the ADM channels read directly by the realtime engine
2. Per-speaker RMS/peak comparison between offline and realtime output for the same segment
3. Whether elevation sanitization produces the same output for the specific Swale source positions (nominal positions are all at `z=0` for direct_speakers and minimal z for audio_objects — the translab layout has elevation span ~0.73 rad ≈ 42°, so it is 3D and RescaleAtmosUp fires; the rescaling will affect all sources with z > 0)

---

## 5. What the Next Agent Should Do

### 5A. FIRST: Verify the mono split round-trip (highest priority)

Check the bit-depth that `splitStems.py` writes its mono WAVs at:

```bash
python3 -c "
import soundfile as sf
import numpy as np
info = sf.info('processedData/stageForRender/1.1.wav')
print(info)
# Read both and compare
adm, sr_adm = sf.read('sourceData/LUSID_package/swale.wav')  # or wherever the source ADM is
mono, sr_mono = sf.read('processedData/stageForRender/1.1.wav')
ch0_adm = adm[:, 0].astype(np.float32)
delta = np.abs(ch0_adm - mono.astype(np.float32))
print('max delta:', delta.max(), '  mean delta:', delta.mean())
"
```

If `delta.max() > 0`, the source material feeding the offline renderer differs from what the realtime engine reads — this is the divergence point.

Also check what format `splitChannelsToMono` actually writes. In `splitStems.py`, `sf.write(path, chanData, sample_rate)` with no `format` argument defaults to format inferred from extension. `.wav` → `SF_FORMAT_WAV | SF_FORMAT_PCM_16` by default in soundfile **unless the dtype is float32/float64**, in which case it writes float. Verify with:

```python
info = sf.info('processedData/stageForRender/1.1.wav')
print(info.subtype)  # want 'FLOAT' not 'PCM_16'
```

If `subtype == 'PCM_16'`, the offline pipeline is quantising every audio sample to 16-bit integers before the render — which is an ~96 dB SNR floor, easily audible as reduced presence and different spectral character.

---

### 5B. SECOND: Run a numerical speaker-output comparison

Once the source material is confirmed (or corrected), do a direct numerical comparison:

**Offline**: run `runPipeline.py` on Swale with:

- `master_gain=0.5`, `dbap_focus=1.5`, `renderMode="dbap"`
- `speaker_layout=spatial_engine/speaker_layouts/translab-sono-layout.json`
- Trim output to a 5-second segment from a "good" section (e.g. t=30–35s) and a "bad" section (t=60–65s or t=105–111s)

**Realtime**: capture the same segment at the same parameters. The cleanest way is to export from the realtime engine by temporarily redirecting its output to a file instead of audio hardware — OR use the offline runner as a stand-in for identical-parameter comparison by temporarily removing the `fast-mover sub-stepping` condition and verifying the constant-position case matches.

**Comparison metric** (per speaker channel, for each 5s segment):

```
RMS (dBFS), Peak (dBFS), cross-correlation peak & lag, energy ratio L/R, F/B, T/B
```

If per-speaker RMS differs by more than ~0.1 dB between engines at steady state, there is a real pipeline divergence beyond what structural analysis found.

---

### 5C. THIRD: If sources are confirmed identical and outputs still differ

Instrument both engines to log per-source DBAP input positions (after `directionToDBAPPosition`) for a few representative frames. Compare:

- Offline: computed in `renderPerBlock` → `sanitizeDirForLayout` → `directionToDBAPPosition` → position passed to `renderBuffer`
- Realtime: computed in `Pose::computePositions` → same chain → stored in `SourcePose::position`

If positions match but speaker outputs differ, the `al::Dbap` instance itself must be in a different state — check that both engines use the same `focus` value at that time and that `setFocus()` is being called (it is, per-block in realtime; once at init in offline).

---

## 6. Source Files to Know

| File                                                    | Key fact                                                                                           |
| ------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | `ControlSmooth::smoothed` defaults (line ~451): `masterGain=1.0, focus=1.0` — smoother seeding bug |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`     | `renderBlock()` gain chain; proximity guard; spkMix/lfeMix                                         |
| `spatial_engine/realtimeEngine/src/Pose.hpp`            | Full interpolation + elevation sanitization — matches offline                                      |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`       | `parseChannelIndex()` line ~920: `"N.1" → (N-1)`, `"LFE" → 3`                                      |
| `spatial_engine/src/renderer/SpatialRenderer.cpp`       | `renderPerBlock()` lines 957–1180: offline gain chain; sub-stepping at 14°                         |
| `src/packageADM/splitStems.py`                          | Line 9: `_DEV_LFE_HARDCODED=True`; `sf.write()` format — **check subtype**                         |
| `spatial_engine/src/WavUtils.cpp`                       | `loadSources()`: reads `{name}.wav` exactly                                                        |
| `spatial_engine/src/JSONLoader.cpp`                     | Line 138: `type=LFE` → source key `"LFE"` (not `"4.1"`)                                            |
| `processedData/stageForRender/swale.lusid.json`         | Source `"4.1"` has `"type": "LFE"` — JSONLoader converts this to key `"LFE"`                       |

---

## 7. Speaker Layout Quick Reference (translab-sono-layout.json)

- 16 speakers (ch 0–15), 2 subs (ch 16, 17)
- Ring 0 (ch 0–7): elevation ~1.75° (near-horizontal)
- Ring 1 (ch 8–15): elevation ~38–44° (elevated)
- Sub channels: 16 and 17 (beyond speaker indices)
- Layout computed as 3D (elevation span ≈ 42°)
- `mLayoutIs2D = false` → `RescaleAtmosUp` mode fires for all sources with z > 0
- Median radius ≈ 5.065m (used as DBAP position scale)
