# Spatialization & Rendering — Internal Reference

**Last Updated:** February 2026  
**Source:** `spatial_engine/src/renderer/SpatialRenderer.cpp`, `spatial_engine/src/JSONLoader.cpp`

See [DEPENDENCIES.md](DEPENDENCIES.md) for LUSID scene and speaker layout JSON format specs.

---

## Rendering System

> `RENDERING.md` — Comprehensive reference for the offline spatial renderer (`spatialroot_spatial_render`).

### Overview

Three spatializers supported:

| Feature | DBAP (default) | VBAP | LBAP |
|---|---|---|---|
| **Coverage** | No gaps (works anywhere) | Can have gaps | No gaps |
| **Layout Req** | Any layout | Good 3D triangulation | Multi-ring layers |
| **Localization** | Moderate | Precise | Moderate |
| **Speakers/Src** | Distance-weighted (many) | 3 speakers (exact) | Layer interpolation |
| **Best For** | Unknown/irregular layouts | Dense 3D arrays | AlloSphere, TransLAB |
| **Params** | `--dbap_focus` (0.2–5.0) | — | `--lbap_dispersion` (0–1.0) |

Pipeline: Source WAVs + LUSID scene + Layout JSON → N-channel WAV

### CLI Usage

```bash
# Default render with DBAP
./build/spatialroot_spatial_render \
  --layout spatial_engine/speaker_layouts/allosphere_layout.json \
  --positions processedData/stageForRender/scene.lusid.json \
  --sources processedData/stageForRender/ \
  --out render.wav

# VBAP
./build/spatialroot_spatial_render \
  --spatializer vbap \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render_vbap.wav

# DBAP with tight focus
./build/spatialroot_spatial_render \
  --spatializer dbap --dbap_focus 3.0 \
  --layout translab_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render_tight.wav

# Debug single source
./build/spatialroot_spatial_render \
  --solo_source "11.1" \
  --debug_dir ./debug_output/ \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out debug_source.wav
```

**Required flags:** `--layout`, `--positions`, `--sources`, `--out`  
**Spatializer:** `--spatializer dbap|vbap|lbap`, `--dbap_focus`, `--lbap_dispersion`  
**General:** `--master_gain`, `--solo_source`, `--t0`, `--t1`, `--elevation_mode`, `--debug_dir`

### Duration Handling (Feb 16, 2026)

**Fixed:** C++ renderer now uses `mSpatial.duration` from LUSID scene instead of inferring from WAV file length. This prevents truncated renders when keyframes end before composition end (e.g., 9:26 ADM → correct 566s render, not truncated 167s).

### RF64 Auto-Selection for Large Renders (v0.5.2)

**Problem:** Standard WAV 32-bit data-chunk size wraps at 4 GB. 56-channel × 566s × 48kHz × 4B = 5.67 GB caused header overflow, making readers report ~166s instead of 566s. Audio data on disk was correct — only the header was wrong.

**Fix:** `WavUtils::writeMultichannelWav()` auto-selects `SF_FORMAT_RF64` when audio data exceeds 4 GB. RF64 (EBU Tech 3306) uses 64-bit size fields. Falls back to standard WAV for files under 4 GB.

**Duration limits at 48 kHz 32-bit float:** 56-channel layout → ~6.6 min before RF64 kicks in.

### Elevation Compensation

**Default: `RescaleAtmosUp`** — maps Atmos-style elevations [0°, +90°] into the layout's actual elevation range. Prevents sources from becoming inaudible at zenith.

| Mode | CLI | Description |
|---|---|---|
| `RescaleAtmosUp` | `--elevation_mode rescale_atmos_up` | Default. Maps [0°, +90°] → layout range |
| `RescaleFullSphere` | `--elevation_mode compress` | Maps full [-90°, +90°] range |
| `Clamp` | `--no-vertical-compensation` | Hard clip |

### LFE Handling

- Sources named `"LFE"` or node type `LFE` bypass spatialization.
- Routed directly to all subwoofer channels defined in `subwoofers` array of layout JSON.
- Energy divided by number of subs.
- Gain compensation: `dbap_sub_compensation = 0.95` (global — TODO: make configurable).
- Output buffer auto-sized to `max(maxSpeakerChannel, maxSubwooferChannel) + 1`.

```json
{
  "speakers": [...],
  "subwoofers": [{ "channel": 16 }, { "channel": 17 }]
}
```

### Robustness Features

**Zero-Block Detection & Fallback (VBAP):**
- Detects when spatializer produces silence despite input energy (VBAP coverage gaps)
- Fallback: retarget direction 90% toward nearest speaker
- Threshold: `kPannerZeroThreshold = 1e-6`

**Fast-Mover Sub-Stepping:**
- Detects sources moving >14° (~0.25 rad) within a 64-sample block
- Subdivides block into 16-sample chunks with per-chunk direction
- Prevents "blinking" artifacts from rapid trajectory changes

**Direction Validation:**
- NaN/Inf check on all directions before use
- Zero-length vectors → front `[0, 1, 0]`
- Warnings rate-limited (once per source, not per block)

### DBAP Coordinate Quirk

AlloLib DBAP applies internal transform `(x,y,z) → (x,-z,y)`. `directionToDBAPPosition()` in `SpatialRenderer.cpp` compensates automatically — no action needed from callers.

### Render Statistics

End-of-render diagnostics (`--debug_dir` writes `render_stats.json`, `block_stats.log`):
- Overall peak, near-silent channels, clipping channels, NaN channels
- Direction sanitization summary (clamped/rescaled/invalid counts)
- Panner robustness summary (zero-blocks, retargets, sub-stepped blocks)

### Key Source Files

- `spatial_engine/src/renderer/SpatialRenderer.cpp/.hpp` — core renderer
- `spatial_engine/src/JSONLoader.cpp/.hpp` — LUSID scene parser
- `spatial_engine/src/LayoutLoader.cpp/.hpp` — speaker layout parser
- `spatial_engine/src/WavUtils.cpp/.hpp` — WAV/RF64 I/O
- `spatial_engine/src/vbap_src/VBAPRenderer.cpp/.hpp` — VBAP implementation

### Algorithm Details

**Interpolation:** Block-center SLERP for direction between LUSID keyframes.

**Safe Fallback:** Last-good direction → nearest keyframe → front `(0,1,0)`.

**LUSID Scene Parser (`JSONLoader.cpp`):**
- `JSONLoader::loadLusidScene(path)` → `SpatialData` struct
- Extracts `audio_object`, `direct_speaker`, `LFE` nodes
- Converts timestamps using `timeUnit` + `sampleRate`
- Source keys use node ID format (`"1.1"`, `"11.1"`)
- Ignores `spectral_features`, `agent_state` nodes

---

## DBAP Field Testing Notes

> `DBAP-Testing.md` — Field testing at Translab (Feb 3–10, 2026) with "Swale", 8.8.2 speaker config (2 subs).

### Focus Level Results

| Focus | Observation |
|---|---|
| 1.0 | Best level balance, not very localized |
| **1.5** | Sweet spot — localized but slightly dispersed. **Chosen as default.** |
| 2.0 | Strong localization but level adjustment needed |
| 2.5 | Mix becomes muddy |

### Takeaways

- Default focus: **1.5**
- Conduct further testing for range 1.1–1.5
- Subs need energy distribution based on number of subs — currently routed equally to both
- Sub level at focus 2.0 is 30–40% too loud (DBAP focus increases energy concentration → mains drop, but LFE bypass means subs don't compensate)
- **Possible future:** scale sub level based on DBAP focus parameter (see `agent_compensation_and_gain.md` — `focusAutoCompensation` toggle partially addresses this)

### Master Gain Default

`masterGain` default is consistently `0.5` across all code and docs. (`SpatialRenderer.hpp` line 76, `main.cpp` default, all documentation.)
