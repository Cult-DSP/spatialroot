# spatialroot — Comprehensive Agent Context

**Last Updated:** March 31, 2026  
**Project:** spatialroot - Open Spatial Audio Infrastructure  
**Lead Developer:** Lucian Parisi

> **Phase 6 (2026-03-31): C++ refactor complete.** Python GUI/entrypoints/build tooling/venv removed.
> Primary entry points are the `spatialroot_realtime` CLI binary and the Dear ImGui + GLFW desktop GUI (`gui/imgui/`),
> which embeds `EngineSessionCore` in-process (no subprocess, no required OSC dependency for local control).
> See `internalDocsMD/cpp_refactor/refactor_planning.md`.

> **Phase 3 (2026-03-04):** ADM WAV preprocessing moved into `cult-transcoder`.
> **Historical (pre-Phase 6):** The (now removed) Python launcher `runRealtime.py` called `cult_transcoder/build/cult-transcoder transcode --in-format adm_wav`
> instead of `extractMetaData()` + Python oracle + `writeSceneOnly()`.
> See `cult_transcoder/internalDocsMD/AGENTS-CULT.md §8` and `DEV-PLAN-CULT.md Phase 3`.

> **Phase 4 (2026-03-07):** `cult-transcoder` gains `--lfe-mode` flag (hardcoded | speaker-label)
> and ADM profile detection (DolbyAtmos, Sony360RA). 40/40 tests pass. Fully documented in
> `cult_transcoder/internalDocsMD/AGENTS-CULT.md §11`.

> **Phase 5 GUI (2026-03-07):** TRANSCODE UI added to the realtime GUI (PySide6 at the time; superseded by ImGui GUI in Phase 6).
> See §Phase 5 below for historical notes.

---

## Table of Contents

0. [🔎 Issues Found During Duration/RF64 Investigation](#-issues-found-during-durationrf64-investigation-feb-16-2026)
1. [Project Overview](#project-overview)
2. [Architecture & Data Flow](#architecture--data-flow)
3. [Core Components](#core-components)
4. [LUSID Scene Format](#lusid-scene-format)
5. [Spatial Rendering System](#spatial-rendering-system)
6. [Real-Time Spatial Audio Engine](#real-time-spatial-audio-engine)
7. [File Structure & Organization](#file-structure--organization)
8. [Common Issues & Solutions](#common-issues--solutions)
9. [Development Workflow](#development-workflow)
10. [Testing & Validation](#testing--validation)
11. [Future Work & Known Limitations](#future-work--known-limitations)

---

## 🔎 Issues Found During Duration/RF64 Investigation (Feb 16, 2026)

> Comprehensive catalog of all issues discovered while tracing the truncated-render bug.
> Items marked ✅ are fixed. Items marked ⚠️ need code changes. Items marked ℹ️ are observations.

| #   | Status   | Severity     | Issue                                                                                   | Location                                            |
| --- | -------- | ------------ | --------------------------------------------------------------------------------------- | --------------------------------------------------- |
| 1   | ✅ FIXED | **Critical** | WAV 4 GB header overflow — `SF_FORMAT_WAV` wraps 32-bit size field                      | `WavUtils.cpp`                                      |
| 2   | ✅ FIXED | **High**     | Legacy script trusted corrupted WAV header without cross-check (script removed Phase 6) | (historical)                                        |
| 3   | ✅ FIXED | **Low**      | Stale `DEBUG` print statements left in renderer                                         | `SpatialRenderer.cpp`                               |
| 4   | ✅ FIXED | **Medium**   | `masterGain` default mismatch resolved — now consistently `0.5` across code and docs    | `SpatialRenderer.hpp` · `main.cpp` · `RENDERING.md` |
| 5   | ✅ FIXED | **Medium**   | `dbap_focus` now forwarded for all DBAP-based modes, including plain `"dbap"`           | (archived, pre-Phase 6: `runPipeline.py`)           |
| 6   | ✅ FIXED | **Medium**   | Legacy Python wrapper exposed `master_gain` (wrapper removed Phase 6)                   | (historical)                                        |

CURRENT PROJECT:
Switching from BWF MetaEdit to embedded EBU parsing submodules (TRACK A — COMPLETE)

### Goal

Replace the external `bwfmetaedit` dependency with **embedded EBU libraries** while keeping the existing ADM parsing + LUSID conversion behavior unchanged. **Completed.**

- Output: `processedData/currentMetaData.xml` (ADM XML string extracted from WAV via `spatialroot_adm_extract`)
- Downstream tooling has changed since Track A: Python parsers/wrappers were removed in Phase 6, and ADM→LUSID generation is handled by `cult-transcoder` (C++).
- This is a **plumbing swap only**. ADM support not broadened in Track A (Track B documented below as future work).

### Documentation update obligations (MANDATORY)

Whenever a change impacts the toolchain dataflow, CLI flags, on-disk artifacts, or any cross-module contract, the agent **must update documentation in the same PR**.

For Track A (embedded ADM extractor) the following docs MUST be kept consistent:

- `AGENTS.md` (this file): Track A plan + non-goals + build wiring
- `LUSID/LUSID_AGENTS.md`: pipeline diagram reflects `spatialroot_adm_extract` (embedded); note added that Track A does **not** change LUSID parsing semantics
- `toolchain_AGENTS.md`: if any contract-level path/filename/artifact changes (should not happen in Track A), update it
- `CHANGELOG_TOOLCHAIN.md`: add an entry if the contract changes (new required/optional dependency, new artifact, changed path, new validation step). If Track A only changes the preferred extractor implementation but preserves outputs, record it as an **implementation** note only if your changelog policy allows; otherwise omit.

Rules:

- Do not leave docs in a contradictory state.
- If docs disagree, `toolchain_AGENTS.md` is the contract authority; resolve conflicts by updating the other docs accordingly.
- Keep changes minimal: Track A should only require a small pipeline-diagram + note update in `LUSID_AGENTS.md`.

### Repository constraints

- **Submodules must live in `thirdparty/`**
  - `thirdparty/libbw64` (EBU BW64/RF64 container I/O)
  - `thirdparty/libadm` (EBU ADM XML model + parser)
- Keep changes minimal and compatible with the current pipeline and GUI, especially:
  - file outputs under `processedData/`

### Deliverable (Track A)

1. Add EBU submodules
   - Add git submodules in `thirdparty/`:
     - `thirdparty/libbw64`
     - `thirdparty/libadm`
   - Document how to initialize them: `git submodule update --init --recursive`

2. Build an **embedded ADM XML extractor tool**
   - Create a small C++ CLI tool in the spatialroot repo that:
     - opens a WAV/RF64/BW64 file,
     - extracts the `axml` chunk (ADM XML),
     - writes it to a file path supplied by the user (or prints to stdout).
   - Recommended placement:
     - `tools/adm_extract/` (new)

- CMake target name: `spatialroot_adm_extract`
- The tool should not interpret ADM semantics; it is only a chunk extractor.
- Keep the output stable: `processedData/currentMetaData.xml` remains the same format (raw ADM XML string).

3. Wire the pipeline to use the new tool (no semantic changes)

- Update the C++ preprocessing/tooling path that generates `processedData/currentMetaData.xml` to use `spatialroot_adm_extract` exclusively.
- Preserve current filenames and directories so everything downstream stays compatible.

4. Update `init.sh` to build the tool
   - `init.sh` should:
     - initialize submodules,
     - build the new extractor tool (via CMake),
     - continue building the spatial renderer as before.
   - Keep build artifacts in an existing or clearly documented build folder (e.g., `tools/adm_extract/build/`).

5. Testing (must pass)
   - End-to-end pipeline with a known-good Atmos ADM test file:
     - produces `processedData/currentMetaData.xml`
     - produces `processedData/stageForRender/scene.lusid.json`
     - renderer runs and outputs a WAV

- C++ tests pass (where applicable via CTest/Catch2).

### Explicit non-goals (Track A)

- Do NOT change LUSID schema semantics in this task.
- Do NOT attempt to add Sony 360RA parsing support here.
- Do NOT restructure the ADM→LUSID conversion.
- Do NOT require EBU ADM Toolbox (EAT) in the main build.

### AlloLib Audit & Lightweighting — ✅ COMPLETE (Feb 22, 2026)

> Full details: [`internalDocsMD/Repo_Auditing/allolib-audit.md`](Repo_Auditing/allolib-audit.md)

**Problem:** `thirdparty/allolib` had full git history (1,897 commits). `.git/modules/thirdparty/allolib` = **511 MB**; working tree = 38 MB.

#### Headers directly `#include`d by spatialroot

| Header                                                                                           | Module |
| ------------------------------------------------------------------------------------------------ | ------ |
| `al/math/al_Vec.hpp`                                                                             | math   |
| `al/sound/al_Vbap.hpp` · `al_Dbap.hpp` · `al_Lbap.hpp` · `al_Spatializer.hpp` · `al_Speaker.hpp` | sound  |
| `al/io/al_AudioIOData.hpp`                                                                       | io     |

CMake link targets: `al` + `Gamma`.

#### Keep list (required today)

`sound/` · `math/` · `spatial/` · `io/al_AudioIO` · `system/` · `external/Gamma/` · `external/json/`

#### Likely-future list (real-time audio engine)

`io/al_AudioIO` · `app/` (App, AudioDomain, SimulationDomain) · `system/al_PeriodicThread` · `protocol/` (OSC) · `scene/` (PolySynth, DynamicScene) · `external/rtaudio/` · `external/rtmidi/` · `external/oscpack/`

#### Safe to trim (graphics/UI — unused, no near-term path)

`graphics/` · `ui/` · `sphere/` · `external/glfw/` (4.5 MB) · `external/imgui/` (5.1 MB) · `external/stb/` (2.0 MB) · `external/glad/` · `external/serial/` · `external/dr_libs/`

#### Changes applied

| File                                  | Change                                                                                                                                           |
| ------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `.gitmodules`                         | Added `shallow = true` to `thirdparty/allolib` — fresh clones are depth=1 automatically                                                          |
| `init.sh` `initializeSubmodules()`    | Uses `--depth 1` — `init.sh` now initializes allolib shallow (~510 MB saved)                                                                     |
| `init.sh` `initializeEbuSubmodules()` | Uses `--depth 1` — libbw64/libadm also shallow                                                                                                   |
| `scripts/shallow-submodules.sh`       | **New.** One-time idempotent script to re-shallow an existing deep clone                                                                         |
| `scripts/sparse-allolib.sh`           | **New, opt-in only.** Sparse working-tree checkout (~14 MB saved); ⚠️ fragile with AlloLib's unconditional CMakeLists — not run by default or CI |

**Default path (`init.sh` / CI):** full working tree, shallow history. Builds correctly with no CMake changes.
**To apply to an existing deep clone:** `./scripts/shallow-submodules.sh`
**Opt-in sparse tree:** `./scripts/sparse-allolib.sh` — read warnings in script before using.
**Future option if still too heavy:** minimal fork `Cult-DSP/allolib-sono` stripping graphics/UI/sphere (see `allolib-audit.md` §4 Step 3).

### Track B (FUTURE — DO NOT IMPLEMENT YET)

**Objective:** Add a profile adaptation layer inside LUSID to accept a wider range of ADM variants (Sony 360RA, edge-case Atmos exports, etc.).
Planned work:

- Add C++ classes inside `cult-transcoder/src/adm_profiles/`
  - `detect_profile.cpp`
  - `atmos_adapter.cpp`
  - `sony360_adapter.cpp`
  - `common.cpp` (ID handling, time parsing incl. `S48000`, polar→cart, block compaction, mute gating)
- Sony 360RA needs:
  - Opaque string IDs (hex-like suffixes such as `...0a`)
  - `rtime/duration` parsing with `S####` suffix
  - mute-block handling (gain=0 segments)
  - block compaction to avoid massive frame counts

**Status:** Document only. Await further instructions before implementing Track B.

FUTURE ISSUES
| 9 | ℹ️ NOTE | **Info** | Large interleaved buffer (~11.3 GB peak for 56ch × 566s) allocated in one shot | `WavUtils.cpp` `writeMultichannelWav()` |
| 10 | ℹ️ NOTE | **Info** | Test file exercises only `audio_object` + `LFE` paths; `direct_speaker` untested at render level | Pipeline integration tests |

additional:
|

### Details for Unfixed Items

**#4 — masterGain default mismatch**

- `SpatialRenderer.hpp` line 76: `float masterGain = 0.5;`
- `main.cpp` line 48 (help text): `"default: 0.5"`
- `main.cpp` line 101 (comment): `// Default master gain: 0.5f`
- `RENDERING.md`: updated to document `0.5f`
- **Resolution**: Default value standardized to `0.5` across all locations.

**#5 — dbap_focus forwarded for all DBAP modes (Archived, pre-Phase 6)**

- Historical note from the legacy offline pipeline (removed in Phase 6).

**#6 — master_gain exposed**

- **Historical note:** A legacy Python wrapper exposed `master_gain` to the offline renderer.
- Phase 6 removed the Python wrapper; `master_gain` is now a C++ CLI/runtime concern.

**#9 — Large interleaved buffer**

- `WavUtils.cpp` allocates a single `std::vector<float>` of `totalSamples × channels` (56 × 27,168,000 = 1.52 billion floats ≈ 5.67 GB).
- Combined with the per-channel buffers already in memory, peak is ~11.3 GB.
- **Mitigation idea**: Chunked/streaming write (write N blocks at a time instead of all at once).

---

## Project Overview

### Purpose

spatialroot is a C++17 codebase for decoding and rendering Audio Definition Model (ADM) Broadcast WAV files (Dolby Atmos masters) to arbitrary speaker arrays using multiple spatialization algorithms.

### Key Features

- **Multi-format Input**: Dolby Atmos ADM BWF WAV files
- **Multi-spatializer Support**: DBAP (default), VBAP, LBAP
- **Arbitrary Speaker Layouts**: JSON-defined speaker positions
- **LUSID Scene Format**: Canonical time-sequenced node graph for spatial audio (`scene.lusid.json`)
- **ADM Duration Preservation**: Extracts and uses authoritative duration from ADM metadata (fixes truncated renders)
- **Native GUI**: Dear ImGui + GLFW GUI (`gui/imgui/`) embeds the realtime engine in-process
- **Subwoofer/LFE Handling**: Automatic routing to designated subwoofer channels
- **C++ Test Suite**: Renderer and transcoder tests via CTest/Catch2

### Technology Stack

- **C++17**: High-performance spatial audio renderer (AlloLib-based)
- **AlloLib**: Audio spatialization framework (DBAP, VBAP, LBAP)
- **CMake 3.12+**: Build system for C++ components
- **Dear ImGui + GLFW**: Desktop GUI

---

## Architecture & Data Flow

### Complete Pipeline Flow

```
ADM BWF WAV File
    │
    ├─► cult-transcoder (C++) → scene.lusid.json (+ stems when needed)
    │         │
    │         ▼
    │  processedData/stageForRender/scene.lusid.json  (CANONICAL FORMAT)
    │
    └─► spatialroot_realtime (C++)
              │
              ├─► loads speaker layout + LUSID scene
              ├─► uses stems (offline-prepped) or direct ADM streaming (when configured)
              └─► outputs to hardware speakers (AlloLib AudioIO)
```

### LUSID as Canonical Format

**IMPORTANT:** LUSID `scene.lusid.json` is the **source of truth** for spatial data. The old `renderInstructions.json` format is **deprecated** and moved to `old_schema/` directories.

The C++ renderer reads LUSID directly — no intermediate format conversion.

### Phase 6 Architecture Notes (2026-03-31)

- Python pipeline orchestration and the Python LUSID library were removed from the primary workflow.
- `cult-transcoder` is the canonical preprocessing/transcoding tool for producing `scene.lusid.json` (and stems when applicable).
- The primary realtime entrypoints are `spatialroot_realtime` (CLI) and `gui/imgui/` (ImGui + GLFW GUI).

### ADM Duration Preservation

- **Problem**: Renderer was calculating duration from longest WAV file length, causing truncated renders when keyframes ended early
- **Solution**: Extract authoritative duration from ADM `<Duration>` field, store in LUSID `duration` field
- **Impact**: Ensures full composition duration is rendered (e.g., 9:26 ADM file now renders 9:26, not truncated 2:47)
- **Implementation**: Updated the ADM→LUSID generation stage (historically Python; Phase 6 uses `cult-transcoder`) plus the C++ renderer and JSON schema

**RF64 Auto-Selection for Large Renders (v0.5.2, Feb 16 2026):**

- **Problem**: Standard WAV format uses unsigned 32-bit data-chunk size (max ~4.29 GB). Multichannel spatial renders for long compositions (e.g., 56 channels × 566s × 48kHz × 4B = 5.67 GB) caused the header to wrap around, making readers report truncated duration (~166s instead of 566s). The C++ renderer was producing correct output all along — only the WAV header was wrong.
- **Solution**: `WavUtils::writeMultichannelWav()` auto-selects `SF_FORMAT_RF64` when audio data exceeds 4 GB. RF64 (EBU Tech 3306) uses 64-bit size fields. Falls back to standard WAV for files under 4 GB (maximum compatibility).
- **Impact**: Renders of any size are now correctly readable by downstream tools.
- **Implementation**: Updated `WavUtils.cpp`

---

## Core Components

### 1. ADM Metadata Extraction & Parsing

#### `spatialroot_adm_extract` (ARCHIVED)

- **Note**: Replaced by `cult-transcoder` in Phase 3. Kept here for historical context only.
- **Purpose**: Extract ADM XML from BWF WAV file using the EBU libbw64 library
- **Type**: Embedded C++ CLI tool (Removed)

#### `cult-transcoder` — ADM → LUSID (+ stems) Tool

- **Purpose**: Convert ADM BWF WAV inputs into LUSID scenes (`scene.lusid.json`), and (when needed) generate stems and reports.
- **Build output**: `build/cult_transcoder/cult-transcoder`
- **Source**: `cult_transcoder/`

#### `LUSID/` — Schema + Docs

- **Scope**: LUSID is a schema and documentation bundle in this repo; the Python LUSID runtime/library is removed (Phase 6).
- **Schema**: `LUSID/schema/`

### 5. C++ Spatial Renderer

#### `spatial_engine/src/JSONLoader.cpp` — LUSID Scene Parser

- **Purpose**: Parse LUSID JSON and load audio sources for rendering
- **Key Functions**:
  - `JSONLoader::loadLusidScene(path)` → `SpatialData` struct
  - Extracts `audio_object`, `direct_speaker`, `LFE` nodes
  - Converts timestamps using `timeUnit` + `sampleRate`
  - Source keys use node ID format (`"1.1"`, `"11.1"`)
  - Ignores `spectral_features`, `agent_state` nodes

#### `spatial_engine/src/renderer/SpatialRenderer.cpp` — Core Renderer

- **Purpose**: Render spatial audio using DBAP/VBAP/LBAP
- **Key Methods**:
  - `renderPerBlock()` — block-based rendering (default 64 samples)
  - `sanitizeDirForLayout()` — elevation compensation (RescaleAtmosUp default)
  - `directionToDBAPPosition()` — coordinate transform for AlloLib DBAP
  - `nearestSpeakerDir()` — fallback for VBAP coverage gaps
- **Robustness Features**:
  - Zero-block detection and retargeting
  - Fast-mover sub-stepping (angular delta > 0.25 rad)
  - LFE direct routing to subwoofer channels

#### AlloLib Spatializers

- **DBAP** (`al::Dbap`): Distance-Based Amplitude Panning
  - Works with any layout (no coverage gaps)
  - Uses inverse-distance weighting
  - `--dbap_focus` controls distance rolloff (default: 1.0)
  - **Coordinate quirk**: AlloLib applies internal transform `(x,y,z) → (x,-z,y)` — compensated automatically
- **VBAP** (`al::Vbap`): Vector Base Amplitude Panning
  - Triangulation-based (builds speaker mesh at startup)
  - Each source maps to 3 speakers (or 2 for 2D)
  - Can have coverage gaps at zenith/nadir
  - Fallback: Retarget to nearest speaker (90% toward speaker)
- **LBAP** (`al::Lbap`): Layer-Based Amplitude Panning
  - Optimized for multi-ring layouts
  - Groups speakers into horizontal layers
  - `--lbap_dispersion` controls zenith/nadir spread (default: 0.5)

### 6. Pipeline Orchestration

#### `spatialroot_realtime` — Primary CLI Entry Point

- **Purpose**: Run the realtime spatial audio engine.
- **Inputs**: A speaker layout (`--layout`), a LUSID scene (`--scene`), and either mono stems (`--sources`) or a multichannel ADM WAV (`--adm`) for direct streaming.
- **Source prep**: Use `cult-transcoder` to generate/update `processedData/stageForRender/scene.lusid.json` (and stems when needed). The legacy Python wrappers that performed this orchestration have been removed.
- **CLI flags**: See `PUBLIC_DOCS/API.md` or run `spatialroot_realtime --help`.

#### `gui/imgui/` — Dear ImGui + GLFW Desktop GUI (Phase 6)

- **Purpose**: Native GUI for local control of the realtime engine.
- **Build**: `./init.sh` (once) then `./build.sh --gui`.
- **Run**: `./run.sh`.
- **Architecture**: The GUI links and embeds `EngineSessionCore` directly in-process (no `QProcess`, no subprocess). Runtime parameters are updated via direct C++ setter methods (see “Runtime control plane” below). OSC remains available as an optional secondary surface.

#### Removed (historical)

- `runPipeline.py`, `runRealtime.py`, and the PySide6 GUI (`gui/realtimeGUI/`) were removed in Phase 6 (2026-03-31).

### 7. Analysis & Debugging

Phase 6 removed legacy Python analysis/wrapper scripts. Current debugging surfaces:

- `spatialroot_realtime` logs + assertions (engine)
- `cult-transcoder` logs/reports (preprocessing)

---

## LUSID Scene Format

### JSON Structure (v0.5.2)

```json
{
  "version": "0.5",
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "duration": 566.0,
  "metadata": {
    "title": "Scene name",
    "sourceFormat": "ADM",
    "duration": "00:09:26.000"
  },
  "frames": [
    {
      "time": 0.0,
      "nodes": [
        {
          "id": "1.1",
          "type": "direct_speaker",
          "cart": [-1.0, 1.0, 0.0],
          "speakerLabel": "RC_L",
          "channelID": "AC_00011001"
        },
        {
          "id": "4.1",
          "type": "LFE"
        },
        {
          "id": "11.1",
          "type": "audio_object",
          "cart": [-0.975753, 1.0, 0.0]
        }
      ]
    }
  ]
}
```

### Top-Level Fields

- **version**: LUSID format version (currently "0.5")
- **sampleRate**: Sample rate in Hz (must match audio files)
- **timeUnit**: Time unit for keyframes: `"seconds"` (default), `"samples"`, or `"milliseconds"`
- **duration**: **NEW in v0.5.2** - Total scene duration in seconds (from ADM metadata). Critical fix: ensures renderer uses authoritative ADM duration instead of calculating from WAV file lengths. Prevents truncated renders when keyframes end before composition end.
- **metadata**: Optional metadata object (source format, original duration string, etc.)
- **frames**: Array of time-ordered frames containing spatial nodes

### Node Types & ID Convention

**Node ID Format: `X.Y`**

- **X** = group number (logical grouping)
- **Y** = hierarchy level (1 = parent, 2+ = children)

**Channel Assignment Convention:**

- Groups 1–10: DirectSpeaker bed channels
- Group 4: LFE (currently hardcoded — see `_DEV_LFE_HARDCODED`)
- Groups 11+: Audio objects

**Node Types:**

| Type                | ID Pattern | Required Fields                                   | Renderer Behavior                             |
| ------------------- | ---------- | ------------------------------------------------- | --------------------------------------------- |
| `audio_object`      | `X.1`      | `id`, `type`, `cart`                              | Spatialized (DBAP/VBAP/LBAP)                  |
| `direct_speaker`    | `X.1`      | `id`, `type`, `cart`, `speakerLabel`, `channelID` | Treated as static audio_object                |
| `LFE`               | `X.1`      | `id`, `type`                                      | Routes to subwoofers, bypasses spatialization |
| `spectral_features` | `X.2+`     | `id`, `type`, + data fields                       | Ignored by renderer                           |
| `agent_state`       | `X.2+`     | `id`, `type`, + data fields                       | Ignored by renderer                           |

### Coordinate System

**Cartesian Direction Vectors: `cart: [x, y, z]`**

- **x**: Left (−) / Right (+)
- **y**: Back (−) / Front (+)
- **z**: Down (−) / Up (+)
- Normalized to unit length by renderer
- Zero vectors replaced with front `[0, 1, 0]`

### Time Units

| Value            | Aliases  | Description                            |
| ---------------- | -------- | -------------------------------------- |
| `"seconds"`      | `"s"`    | Default. Timestamps in seconds         |
| `"samples"`      | `"samp"` | Sample indices (requires `sampleRate`) |
| `"milliseconds"` | `"ms"`   | Timestamps in milliseconds             |

**Always specify `timeUnit` explicitly** to avoid heuristic detection warnings.

### Source ↔ WAV File Mapping

| Node ID | WAV Filename | Description                |
| ------- | ------------ | -------------------------- |
| `1.1`   | `1.1.wav`    | DirectSpeaker (e.g., Left) |
| `4.1`   | `LFE.wav`    | LFE (special naming)       |
| `11.1`  | `11.1.wav`   | Audio object group 11      |

**Important:** Old `src_N` naming convention is deprecated.

---

## Spatial Rendering System

### Spatializer Comparison

| Feature          | DBAP (default)            | VBAP                  | LBAP                        |
| ---------------- | ------------------------- | --------------------- | --------------------------- |
| **Coverage**     | No gaps (works anywhere)  | Can have gaps         | No gaps                     |
| **Layout Req**   | Any layout                | Good 3D triangulation | Multi-ring layers           |
| **Localization** | Moderate                  | Precise               | Moderate                    |
| **Speakers/Src** | Distance-weighted (many)  | 3 speakers (exact)    | Layer interpolation         |
| **Best For**     | Unknown/irregular layouts | Dense 3D arrays       | Allosphere, TransLAB        |
| **Params**       | `--dbap_focus` (0.2–5.0)  | None                  | `--lbap_dispersion` (0–1.0) |

### Rendering Modes

| Mode     | Description                            | Performance | Accuracy | Recommended |
| -------- | -------------------------------------- | ----------- | -------- | ----------- |
| `block`  | Direction computed once per block (64) | Fast        | High     | ✓ Yes       |
| `sample` | Direction computed every sample        | Slow        | Highest  | Critical    |

### Elevation Compensation

**Default: RescaleAtmosUp**

- Maps Atmos-style elevations [0°, +90°] into layout's elevation range
- Prevents sources from becoming inaudible at zenith
- Options: `RescaleAtmosUp` (default), `RescaleFullSphere` (legacy "compress"), `Clamp` (hard clip)
- CLI: `--elevation_mode compress` or `--no-vertical-compensation`

### LFE (Low-Frequency Effects) Handling

**Detection & Routing:**

- Sources named "LFE" or node type `LFE` bypass spatialization
- Routed directly to all subwoofer channels defined in layout JSON
- Energy divided by number of subs
- Gain compensation: `dbap_sub_compensation = 0.95` (global var — TODO: make configurable)

**Layout JSON Subwoofer Definition:**

```json
{
  "speakers": [...],
  "subwoofers": [
    { "channel": 16 },
    { "channel": 17 }
  ]
}
```

**Buffer Sizing:**

- Output buffer sized to `max(maxSpeakerChannel, maxSubwooferChannel) + 1`
- Supports arbitrary subwoofer indices beyond speaker count

### Robustness Features

#### Zero-Block Detection & Fallback

- Detects when spatializer produces silence despite input energy
- Common with VBAP at coverage gaps
- **Fallback**: Retarget direction 90% toward nearest speaker
- Threshold: `kPannerZeroThreshold = 1e-6` (output sum)

#### Fast-Mover Sub-Stepping

- Detects sources moving >14° (~0.25 rad) within a block
- Subdivides block into 16-sample chunks with per-chunk direction
- Prevents "blinking" artifacts from rapid trajectory changes

#### Direction Validation

- All directions validated before use (NaN/Inf check)
- Zero-length vectors replaced with front `[0, 1, 0]`
- Warnings rate-limited (once per source, not per block)

### Render Statistics

**End-of-render diagnostics:**

- Overall peak, near-silent channels, clipping channels, NaN channels
- Direction sanitization summary (clamped/rescaled counts)
- Panner robustness summary (zero-blocks, retargets, sub-stepped blocks)

**Debug output** (`--debug_dir`):

- `render_stats.json` — per-channel RMS/peak, spatializer info
- `block_stats.log` — per-block processing stats (sampled)

### CLI Usage Examples

```bash
# Default render with DBAP
./spatialroot_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render.wav

# Use VBAP for precise localization
./spatialroot_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render_vbap.wav \
  --spatializer vbap

# DBAP with tight focus
./spatialroot_spatial_render \
  --spatializer dbap \
  --dbap_focus 3.0 \
  --layout translab_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render_tight.wav

# Debug single source with diagnostics
./spatialroot_spatial_render \
  --solo_source "11.1" \
  --debug_dir ./debug_output/ \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out debug_source.wav
```

---

## Real-Time Spatial Audio Engine

### Overview

The real-time engine (`spatial_engine/realtimeEngine/`) performs live spatial audio rendering. It reads the same LUSID scene files and source WAVs as the offline renderer but streams them through an audio device in real-time instead of rendering to a WAV file.

**Status:** All phases complete (Phases 1–10 + OSC timing fix + Polish tasks + **Phase 11 Bug-Fix Pass**). See `internalDocsMD/Realtime_Engine/agentDocs/realtime_master.md` for full completion logs.

### Architecture — Agent Model

The engine follows a sequential agent architecture where each agent handles one stage of the audio processing chain. All agents share `RealtimeConfig` and `EngineState` structs defined in `RealtimeTypes.hpp`.

| Phase | Agent                         | Status      | File                                                    |
| ----- | ----------------------------- | ----------- | ------------------------------------------------------- |
| 1     | **Backend Adapter** (Agent 8) | ✅ Complete | `RealtimeBackend.hpp`                                   |
| 2     | **Streaming** (Agent 1)       | ✅ Complete | `Streaming.hpp`                                         |
| 3     | **Pose** (Agent 2)            | ✅ Complete | `Pose.hpp`                                              |
| 4     | **Spatializer** (Agent 3)     | ✅ Complete | `Spatializer.hpp`                                       |
| —     | **ADM Direct Streaming**      | ✅ Complete | `MultichannelReader.hpp`                                |
| 5     | LFE Router (Agent 4)          | ⏭️ Skipped  | — (handled in Spatializer)                              |
| 6     | **Compensation and Gain**     | ✅ Complete | `main.cpp` + `RealtimeTypes.hpp`                        |
| 7     | **Output Remap**              | ✅ Complete | `OutputRemap.hpp`                                       |
| 8     | **Threading and Safety**      | ✅ Complete | `RealtimeTypes.hpp` (audit + docs)                      |
| 9     | **Init / Config update**      | ✅ Complete | `init.sh`, `src/config/`                                |
| 10    | **GUI Agent**                 | ✅ Complete | `gui/imgui/` (ImGui + GLFW, embeds `EngineSessionCore`) |
| 10.1  | **OSC Timing Fix**            | ✅ Complete | `realtime_runner.py` (sentinel probe + `flush_to_osc`)  |
| 11    | **Documentation update**      | ✅ Complete | `README.md`, `AGENTS.md`                                |
| 12    | **Polish tasks**              | ✅ Complete | Default audio folder, layout dropdowns, remap CSV       |

### Key Files

- **`RealtimeTypes.hpp`** — Shared data types: `RealtimeConfig` (sample rate, buffer size, layout-derived output channels, paths including `admFile` for ADM direct streaming, atomics: `masterGain`, `dbapFocus`, `loudspeakerMix`, `subMix`, `focusAutoCompensation`, `paused`, `playing`, `shouldExit`), `EngineState` (atomic frame counter, playback time, CPU load, source/speaker counts). Output channel count is computed from the speaker layout by `Spatializer::init()` — not user-specified. **Phase 8:** full threading model documented inline (3-thread table, memory-order table, 6 invariants). **Phase 10:** added `std::atomic<bool> paused{false}` with threading doc comment. **Phase 11:** added `std::atomic<uint64_t> nanGuardCount{0}` — incremented (relaxed) per audio block where the post-render clamp pass fires, visible in the main monitoring loop.

- **`RealtimeBackend.hpp`** — Agent 8. Wraps AlloLib's `AudioIO` for audio I/O. Registers a static callback that dispatches to `processBlock()`. Phase 4: zeroes all output channels, calls Pose to compute positions, calls Spatializer to render DBAP-panned audio into all speaker channels including LFE routing to subwoofers. **Phase 10:** pause guard added at the top of `processBlock()` — relaxed load of `config.paused`, zero all output channels + early return (RT-safe, no locks). **Phase 11:** `ctrl.autoComp` wired from `mSmooth.smoothed.autoComp` in Step 3 of `processBlock()` so the focus auto-compensation flag reaches `renderBlock()` correctly.

- **`Streaming.hpp`** — Agent 1. Double-buffered disk streaming for audio sources. Supports two input modes: (1) **mono file mode** — each source opens its own mono WAV file (for LUSID packages via `--sources`), and (2) **ADM direct streaming mode** — a shared `MultichannelReader` opens one multichannel ADM WAV file, reads interleaved chunks, and de-interleaves individual channels into per-source buffers (for ADM sources via `--adm`, skipping stem splitting). In both modes, each source gets two pre-allocated 10-second buffers (480k frames at 48kHz). A background loader thread monitors consumption and preloads the next chunk into the inactive buffer when the active buffer is 75% consumed (7.5s runway). The audio callback reads from buffers using atomic state flags — no locks on the audio thread. On a buffer miss, `getSample()` fades to zero at ~5ms rate (`kMissFadeRate = 0.9958f`) and increments a per-source `underrunCount`; `Streaming::totalUnderruns()` aggregates all counts for monitoring. **Phase 11:** chunk size doubled (240k→480k), threshold raised (50%→75%), exponential fade-to-zero fallback, underrun counter. Key methods: `loadScene()` (mono mode), `loadSceneFromADM()` (multichannel mode), `loaderWorkerMono()`, `loaderWorkerMultichannel()`.

- **`MultichannelReader.hpp`** — Shared multichannel WAV reader for ADM direct streaming. Opens one `SNDFILE*` for the entire multichannel ADM WAV, pre-allocates a single interleaved read buffer (`chunkFrames × numChannels` floats, ~44MB for 48ch), and maintains a channel→SourceStream mapping. Called by the Streaming loader thread to read one interleaved chunk and distribute de-interleaved mono data to each mapped source's double buffer. Method implementations (`deinterleaveInto()`, `zeroFillBuffer()`) are at the bottom of `Streaming.hpp` (after `SourceStream` is fully defined) following standard C++ circular-header patterns.

- **`Pose.hpp`** — Agent 2. Source position interpolation and layout-aware transforms. At each audio block, SLERP-interpolates between LUSID keyframes to compute each source's direction, sanitizes elevation for the speaker layout (clamp, rescale-atmos-up, or rescale-full-sphere modes), and applies the DBAP coordinate transform (direction × layout radius → position). Outputs a flat `SourcePose` vector consumed by the spatializer. All math is adapted from `SpatialRenderer.cpp` with provenance comments.

- **`Spatializer.hpp`** — Agent 3. DBAP spatial audio panning. Builds `al::Speakers` from the speaker layout (radians → degrees, consecutive 0-based channels), computes `outputChannels` from the layout (`max(numSpeakers-1, max(subDeviceChannels)) + 1` — same formula as offline renderer), creates `al::Dbap` with configurable focus. At each audio block: spatializes non-LFE sources via `renderBuffer()` into an internal render buffer, routes LFE sources directly to subwoofer channels with `masterGain * 0.95 / numSubwoofers` compensation, then copies the render buffer to the real AudioIO output. The copy step is the future insertion point for Channel Remap (mapping logical render channels to physical device outputs). Nothing is hardcoded to any specific speaker layout. All math adapted from `SpatialRenderer.cpp` with provenance comments. **Phase 11:** (1) focus compensation override — `autoComp` flag routes gain to private `mAutoCompValue` (written by `computeFocusCompensation()`) instead of `loudspeakerMix`; (2) minimum-distance guard (0.05m) before DBAP to prevent Inf/NaN from coincident source-speaker positions; (3) post-render clamp pass (±4.0f, NaN→0.0f) with `nanGuardCount` increment; (4) `mPrevFocus` member tracks previous block's focus value (reserved for future threshold-gated skip of `renderBuffer` when focus is static — not yet active).

- **`main.cpp`** — Entry point for the `spatialroot_realtime` CLI binary. Parses CLI args, loads the speaker layout + LUSID scene, initializes Streaming/Pose/Spatializer/Backend, and runs the monitoring loop.

- **Removed:** `runRealtime.py` (Python launcher) was removed in Phase 6 (2026-03-31). The CLI entry point is now the `spatialroot_realtime` binary.

### Runtime Control Plane (Phase 6)

**Primary control (local, in-process):** the GUI embeds `EngineSessionCore` and updates runtime parameters via direct C++ setters:

- `EngineSession::setMasterGain(...)`
- `EngineSession::setDbapFocus(...)`
- `EngineSession::setSpeakerMixDb(...)`
- `EngineSession::setSubMixDb(...)`
- `EngineSession::setAutoCompensation(...)`
- `EngineSession::setElevationMode(...)`

**Secondary control (optional, OSC):** OSC remains supported for external tooling/remote control, but it is no longer the primary local control surface.

- Default port: `9009`
- Disable: set `oscPort=0`
- Parameter names/ranges are unchanged from the Phase 10 table below.

| Parameter      | OSC address                | Range     | Default |
| -------------- | -------------------------- | --------- | ------- |
| Master Gain    | `/realtime/gain`           | 0.0 – 1.0 | 0.5     |
| DBAP Focus     | `/realtime/focus`          | 0.2 – 5.0 | 1.5     |
| Speaker Mix dB | `/realtime/speaker_mix_db` | -10 – +10 | 0.0     |
| Sub Mix dB     | `/realtime/sub_mix_db`     | -10 – +10 | 0.0     |
| Auto-Comp      | `/realtime/auto_comp`      | 0 / 1     | 0       |
| Pause/Play     | `/realtime/paused`         | 0 / 1     | 0       |

### Build System

```bash
# One-time dependency init (submodules + initial build)
./init.sh

# Subsequent builds
./build.sh --engine-only
```

The realtime engine target builds into the top-level `build/` directory. It compiles `spatial_engine/realtimeEngine/src/main.cpp` plus shared loaders from `spatial_engine/src/` (JSONLoader, LayoutLoader, WavUtils).

**Dependencies:** No new dependencies beyond what AlloLib/Gamma already provide. libsndfile comes through Gamma's CMake (`find_package(LibSndFile)` → exports via PUBLIC link).

### Run Examples

```bash
# Mono file mode (from LUSID package with pre-split stems):
./build/spatialroot_realtime \
    --layout ../speaker_layouts/allosphere_layout.json \
    --scene ../../processedData/stageForRender/scene.lusid.json \
    --sources ../../sourceData/lusid_package \
    --gain 0.1 --buffersize 512

# ADM direct streaming mode (reads from original multichannel WAV):
./build/spatialroot_realtime \
    --layout ../speaker_layouts/translab-sono-layout.json \
    --scene ../../processedData/stageForRender/scene.lusid.json \
    --adm ../../sourceData/SWALE-ATMOS-LFE.wav \
    --gain 0.5 --buffersize 512
```

Output channels are derived from the speaker layout automatically (e.g., 56 for the AlloSphere layout: 54 speakers at channels 0-53 + subwoofer at device channel 55).

### Streaming Agent Design

**Two input modes:**

1. **Mono file mode** (`--sources`): Each source opens its own mono WAV file. The loader thread iterates sources independently, loading the next chunk for each.
2. **ADM direct streaming mode** (`--adm`): A shared `MultichannelReader` opens one multichannel WAV. The loader thread reads one interleaved chunk (all channels) and de-interleaves into per-source buffers in a single pass. Eliminates the ~30-60 second stem splitting step entirely.

**Double-buffer pattern:** Each source has two float buffers (A and B). Buffer states cycle through `EMPTY → LOADING → READY → PLAYING`. The audio thread reads from the `PLAYING` buffer. When playback crosses 50% of the active buffer, the loader thread fills the inactive buffer with the next chunk from disk.

**Buffer swap is lock-free:** The audio thread atomically switches `activeBuffer` when it detects the other buffer is `READY` and contains the needed data. The mutex in `SourceStream` only protects `sf_seek()`/`sf_read_float()` calls and is only ever held by the loader thread.

**Source naming convention:** Source key (e.g., `"11.1"`) maps to WAV filename `"11.1.wav"` in mono mode. In ADM mode, source key `"11.1"` → ADM channel 11 → 0-based index 10. LFE source key `"LFE"` → channel index 3 (hardcoded standard ADM LFE position).

### ✅ Phase 6 — C++ Refactor + ImGui GUI — COMPLETE (2026-03-31)

- The realtime engine entry point is the `spatialroot_realtime` CLI binary.
- The desktop GUI is Dear ImGui + GLFW (`gui/imgui/`) and embeds `EngineSessionCore` directly in-process (no subprocess).
- Runtime control is primarily direct C++ setters on `EngineSession`; OSC is an optional secondary surface (default port 9009, disabled with `oscPort=0`).

## File Structure & Organization

### Project Root

```
spatialroot/
├── init.sh / build.sh / run.sh / engine.sh     # macOS/Linux entrypoints
├── init.ps1 / build.ps1 / run.ps1              # Windows entrypoints
├── build/                                      # Top-level CMake build output
│   ├── spatial_engine/realtimeEngine/spatialroot_realtime
│   ├── spatial_engine/spatialRender/spatialroot_spatial_render
│   ├── cult_transcoder/cult-transcoder
│   └── gui/imgui/Spatial Root
├── gui/
│   └── imgui/                                  # Dear ImGui + GLFW desktop GUI (embeds EngineSessionCore)
├── spatial_engine/                             # Offline renderer + realtime engine sources
├── cult_transcoder/                            # ADM↔LUSID transcoder (submodule)
├── thirdparty/allolib/                          # AlloLib (submodule)
├── processedData/                              # Working outputs (scene, caches)
├── sourceData/                                 # Input audio + packages
├── internalDocsMD/                              # Internal docs (this file)
└── LUSID/                                      # LUSID schema + docs (no Python runtime)
```

### Obsolete Files (Archived/Removed)

- The Phase 6 refactor removed the Python-only entrypoints, GUI, and build tooling.
- Older schema conversion tooling is retained only via git history unless explicitly preserved.

---

## Python Virtual Environment

**Removed (Phase 6, 2026-03-31):** spatialroot no longer uses a Python virtual environment in the primary toolchain.
Build and run via `init.sh` / `build.sh` / `run.sh` and the C++ binaries.

---

## Common Issues & Solutions

### ADM Parsing

**Issue:** `ModuleNotFoundError: No module named 'lxml'`  
**Solution:** `lxml` is no longer required by the active pipeline. If you still see this, you're running archived code; stop and use the current C++ toolchain.

**Issue:** Empty scene / no frames after transcoding  
**Solution:** Check ADM XML format. Some ADM files have non-standard structure. Check the `cult-transcoder` log output for diagnostics.

### Stem Splitting

**Issue:** Stems have wrong naming (`src_1.wav` instead of `1.1.wav`)  
**Solution:** Updated code uses node IDs now. Re-run pipeline with latest code.

**Issue:** LFE stem missing  
**Solution:** Check the `--lfe-mode` flag logic in `cult-transcoder` C++ source
**Issue:** Sources at zenith/nadir are silent  
**Cause:** Layout doesn't have speakers at extreme elevations  
**Solution:** Use `--elevation_mode compress` (RescaleFullSphere) to map full [-90°, +90°] range

**Issue:** "Zero output" / silent channels  
**Cause:** AlloLib expects speaker angles in degrees, not radians  
**Solution:** Verify `LayoutLoader.cpp` converts radians → degrees:

```cpp
speaker.azimuth = s.azimuth * 180.0f / M_PI;
```

**Issue:** LFE too loud or too quiet  
**Cause:** `dbap_sub_compensation` global variable needs tuning  
**Current:** 0.95 (95% of original level)  
**Solution:** Adjust `dbap_sub_compensation` in `SpatialRenderer.cpp` (TODO: make CLI option)

**Issue:** Clicks / discontinuities in render  
**Cause:** Stale buffer data between blocks  
**Solution:** Renderer now clears buffers with `std::fill()` before each source — fixed in current code

**Issue:** DBAP sounds wrong / reversed  
**Cause:** AlloLib DBAP coordinate transform: `(x,y,z) → (x,-z,y)`  
**Solution:** Renderer compensates automatically in `directionToDBAPPosition()` — no action needed

**Issue:** Render duration appears truncated when read back (e.g., 166s instead of 566s)  
**Cause:** Standard WAV format header overflow. Audio data exceeds 4 GB (common with 54+ speaker layouts and compositions over ~7 minutes at 48kHz). The 32-bit data-chunk size wraps around modulo 2³², causing readers to see fewer samples than were actually written. The audio data on disk is correct — only the header is wrong.

**Fix:** `WavUtils.cpp` now auto-selects RF64 format for files over 4 GB.

**Issue:** ✅ Master gain default is now consistently `0.5` across all code and docs.

**Issue:** ✅ `dbap_focus` forwarded for all DBAP-based modes (`"dbap"` and `"dbapfocus"`).

**Removed (Phase 6):** Python offline pipeline issues (`runPipeline.py`) are no longer applicable.

### Building C++ Renderer

**Issue:** CMake can't find AlloLib  
**Solution:** Initialize submodule: `git submodule update --init --recursive`

**Issue:** Build fails with "C++17 required"  
**Solution:** Update CMake to 3.12+ and ensure compiler supports C++17

**Issue:** Changes to C++ code not reflected after rebuild  
**Solution:** Clean build: `rm -rf build/ && ./build.sh --offline-only`

### LUSID Scene

**Issue:** Parser warnings about unknown fields  
**Solution:** LUSID parser is permissive — warnings are non-fatal. Check if field name is misspelled.

**Issue:** Frames not sorted by time  
**Solution:** Parser auto-sorts frames — no action needed. Warning is informational.

**Issue:** Duplicate node IDs within frame  
**Solution:** Parser keeps last occurrence — fix upstream data generation.

---

## Development Workflow

### Python Workflow (Removed)

Phase 6 removed the Python GUI/entrypoints/build tooling from the primary workflow.

### Making Changes to C++ Renderer

1. **Edit files** in `spatial_engine/`
2. **Rebuild**: `./build.sh --offline-only` (or `rm -rf build/ && ./build.sh --offline-only` for a clean build)
3. **Test manually**:
   ```bash
   ./build/spatial_engine/spatialRender/spatialroot_spatial_render \
     --layout spatial_engine/speaker_layouts/allosphere_layout.json \
     --positions processedData/stageForRender/scene.lusid.json \
     --sources processedData/stageForRender/ \
     --out test_render.wav \
     --debug_dir debug/
   ```
4. **Check diagnostics** in `debug/render_stats.json`

### Adding a New Node Type to LUSID

1. **Update JSON Schema** in `LUSID/schema/lusid_scene_v0.5.schema.json`
2. **Update C++ loaders** (e.g., `spatial_engine/src/JSONLoader.cpp`) if the renderer needs to consume the new type
3. **Document** the new node in `PUBLIC_DOCS/API.md` and relevant internal docs

### Adding a New Spatializer

1. **Add enum value** to `PannerType` in `SpatialRenderer.hpp`
2. **Initialize panner** in `SpatialRenderer` constructor
3. **Add CLI flag** in `main.cpp` argument parsing
4. **Update dispatch** in `renderPerBlock()` to call new panner
5. \*\*Test with various layouts`
6. **Document** in `internalDocsMD/Spatialization/RENDERING.md`

### Git Workflow

```bash
# Fetch latest from origin
git fetch origin

# Create feature branch
git checkout -b feature/my-feature

# Make changes, commit
git add .
git commit -m "feat: description of changes"

# Push to origin
git push origin feature/my-feature

# Create PR on GitHub
```

---

## Testing & Validation

### Transcoder + Engine Tests

- C++ tests are built and run via CTest (Catch2).
- The legacy Python unit tests and Python pipeline integration tests were removed in Phase 6.

```bash
# Configure + build tests
./init.sh && ./build.sh --cult-only

# Run cult-transcoder tests
ctest --test-dir build/cult_transcoder --output-on-failure
```

### Renderer Smoke Test

```bash
# Build renderer
./build.sh --offline-only

# Test DBAP render
./build/spatial_engine/spatialRender/spatialroot_spatial_render \
  --layout spatial_engine/speaker_layouts/allosphere_layout.json \
  --positions processedData/stageForRender/scene.lusid.json \
  --sources processedData/stageForRender/ \
  --out test_dbap.wav \
  --spatializer dbap

# Check output exists and has correct channel count
ffprobe test_dbap.wav 2>&1 | grep "Stream.*Audio"
```

### Benchmarking (Removed)

Python benchmarking scripts were removed with the Phase 6 refactor.

### Validation Checklist

- [ ] `./init.sh && ./build.sh` completes without error
- [ ] `./run.sh` launches the GUI
- [ ] `spatialroot_realtime --help` runs
- [ ] Offline renderer smoke test produces a valid multichannel WAV

---

## Future Work & Known Limitations

### High Priority

#### Realtime GUI Prototype (Phases 1–12 — ✅ COMPLETE, Feb 26 2026)

- Superseded by the Phase 6 ImGui + GLFW GUI (`gui/imgui/`) which embeds `EngineSessionCore` in-process.

#### Pipeline Refactor (next major task after prototype)

- ✅ Complete (Phase 6, 2026-03-31): the canonical entrypoints are the `spatialroot_realtime` CLI and the ImGui GUI.

#### LUSID Integration Tasks (Historical)

The Python LUSID library and related pipeline scripts were removed in Phase 6. Preserve only the LUSID schema + docs unless a new implementation is explicitly in-scope.

#### Renderer Enhancements

- [x] **Fix masterGain default mismatch** ✅ FIXED — standardized to `0.5` across `SpatialRenderer.hpp`, `main.cpp`, `RENDERING.md`

- [x] **Expose `master_gain`** ✅ FIXED — legacy Python wrapper provided this; Phase 6 removed Python wrappers while keeping the underlying gain capability

- [x] **Forward `dbap_focus` for all DBAP modes** ✅ FIXED — archived note from the removed Python offline pipeline (`runPipeline.py`)

- [x] **LFE gain control** ✅ — realtime engine has `subMix` atomic in `RealtimeTypes.hpp`, exposed as `/realtime/sub_mix_db` (±10 dB) via OSC and the Sub Mix slider in `RealtimeControlsPanel`. Fully wired.
  - Note: `dbap_sub_compensation = 0.95f` in `SpatialRenderer.cpp` is the **offline renderer only** — not relevant to realtime pipeline.

- [ ] **Spatializer auto-detection**
  - Analyze layout to recommend best spatializer
  - Heuristics: elevation span, ring detection, triangulation quality
  - Implement `--spatializer auto` CLI flag

- [ ] **Channel remapping**
  - Support arbitrary device channel assignments
  - Currently: output channels = consecutive indices
  - Layout JSON has `deviceChannel` field (not used by renderer)

- [ ] **Atmos mix fixes**
  - Test with diverse Atmos content (different bed configurations)
  - Validate DirectSpeaker handling matches Atmos spec

### Medium Priority

#### Performance Optimizations

- [ ] **Chunked/streaming WAV write** ℹ️ _[Issues list #9]_
  - `WavUtils.cpp` currently allocates a single interleaved buffer of `totalSamples × channels` (~5.67 GB for 56ch × 566s)
  - Peak memory ~11.3 GB with per-channel buffers on top
  - Write in chunks (e.g., 1s blocks) to reduce peak allocation

- [ ] **Eliminate double audio-channel scan** ⚠️ _[Issues list #7]_ (archived, offline pipeline)
  - `runPipeline.py` called `exportAudioActivity()` then `channelHasAudio()` — both scanned the full WAV (~14s each)
  - Historical note only; the Python offline pipeline was removed in Phase 6

- [ ] **Large scene optimization** (1000+ frames)
  - Current: 2823 frames loads in <1ms (acceptable)
  - Profile with 10000+ frame synthetic scenes
  - Consider lazy frame loading if needed

- [ ] **SIMD energy computation**
  - Use vector ops for sum-of-absolutes in zero-block detection
  - Currently: scalar loop

- [ ] **Parallel source processing**
  - Sources are independent within a block
  - Could parallelize `renderPerBlock()` loop

#### Feature Additions

- [ ] **Additional node types**
  - `reverb_zone` — spatial reverb metadata
  - `interpolation_hint` — per-node interpolation mode
  - `width` — source width parameter (DBAP/reverb)

- [x] **Real-time rendering engine — ALL PHASES COMPLETE** ✅ (Feb 26 2026)
  - Phases 1–4: Backend, Streaming, Pose, Spatializer ✅
  - ADM Direct Streaming optimization ✅
  - Phase 5: LFE Router — ⏭️ Skipped (LFE pass-through already implemented in Spatializer.hpp)
  - Phase 6: Compensation Agent — loudspeaker mix + sub mix sliders (±10 dB) + focus auto-compensation ✅
  - Phase 7: Output Remap — CSV-based logical-to-physical channel mapping ✅
  - Phase 8: Threading and Safety audit ✅
  - Phase 9: Init/Config update (`init.sh`, `src/config/`) ✅
  - Phase 10: GUI (ImGui + GLFW, in-process control plane; OSC optional) ✅
  - Phase 10.1: OSC timing fix (sentinel probe) ✅
  - Phase 11–12: Polish tasks ✅

- [ ] **AlloLib player bundle**
  - Package renderer + player + layout loader as single allolib app
  - GUI for layout selection, playback control
  - Integration with AlloSphere dome

#### Pipeline Improvements

> **Scope note (2026-03-07):** All items below reference `runPipeline.py` (the deprecated **offline** pipeline). These are **not active work items**. Current development focus is the realtime engine (`spatialroot_realtime`) and the ImGui GUI (`gui/imgui/`). Do not fix offline pipeline bugs without an explicit owner decision to revive that path.

- [ ] **Fix `sys.argv` bounds check ordering** ⚠️ _[Issues list #8] — offline pipeline only_
  - `runPipeline.py` line 158 reads `sys.argv[1]` before the `len(sys.argv) < 2` guard
  - Move bounds check before first access to prevent `IndexError`

- [ ] **Add `direct_speaker` integration test coverage** ℹ️ _[Issues list #10]_
  - Current test file (ASCENT-ATMOS-LFE) only exercises `audio_object` + `LFE` paths
  - Need a test with active DirectSpeaker bed channels to exercise that renderer path

- [ ] **Stem splitting without intermediate files**
  - Currently: splits all channels → mono WAVs → C++ loads them
  - Alternative: pass audio buffers directly (C++ transcoder → engine via in-memory or mmap)

- [ ] **Internal data structures instead of many JSONs**
  - Already done for LUSID (scene.lusid.json is canonical)
  - Cleanup: remove stale `renderInstructions.json` files from old runs

- [ ] **Debugging JSON with extended info**
  - Single debug JSON with all metadata (ADM, LUSID, render stats)
  - Useful for analysis tools and debugging

### Low Priority

#### Code Quality

- [ ] **Consolidate file deletion helpers**
  - Multiple files use different patterns for delete-before-write
  - Create single util function in `utils/`

- [ ] **Fix hardcoded paths** (archived, Python-era tooling)
  - `parser.py`, `packageForRender.py` had hardcoded `processedData/` paths
  - Historical note only; Python tooling was removed in Phase 6

- [ ] **Static object handling in render instructions**
  - LUSID handles static objects via single keyframe
  - Verify behavior matches expectations

#### Dependency Management

- [ ] **Stable builds for all dependencies** (archived, Python-era tooling)
  - Ensure `requirements.txt` pins versions
  - Git submodules should track specific commits (already done for AlloLib)

- [ ] **Partial submodule clones**
  - AlloLib is large — only clone parts actually used?
  - May not be worth complexity

- [ ] **Bundle as CLI tool** (archived Python-era idea)
  - Package entire pipeline as installable command (`pip install spatialroot`)
  - Single entry point: `spatialroot render <adm_file> --layout <layout>`

### Known Limitations

#### ADM Format Support

- **Assumption:** Standard EBU ADM BWF structure
- **Limitation:** Non-standard ADM files may fail to parse
- **Workaround:** Test with diverse ADM sources, add special cases as needed

#### Bed Channel Handling

- **Current:** DirectSpeakers treated as static audio_objects (1 keyframe)
- **Limitation:** No bed-specific features (e.g., "fixed gain" metadata)
- **Impact:** Minimal — beds are inherently static

#### LFE Detection

- **Current:** Hardcoded to channel 4 (`_DEV_LFE_HARDCODED = True`)
- **Limitation:** Non-standard LFE positions may not be detected
- **Planned Fix:** Label-based detection (check `speakerLabel` for "LFE")

#### Memory Usage

- **Legacy note (pre Phase 6):** The removed Python `xml_etree_parser` used ~5.5x more memory than the lxml path (175MB vs 32MB for 25MB XML).
- **Current (Phase 6):** ADM→LUSID conversion is handled by `cult-transcoder` (C++); this note is retained for archaeology only.

#### Coordinate System Quirks

- **AlloLib DBAP:** Internal transform `(x,y,z) → (x,-z,y)`
- **Status:** Compensated automatically in `directionToDBAPPosition()`
- **Risk:** If AlloLib updates this, our compensation may break
- **Mitigation:** AlloLib source marked with `// FIXME test DBAP` — monitor upstream

#### VBAP Coverage Gaps

- **Issue:** VBAP can produce silence for directions outside speaker hull
- **Mitigation:** Zero-block detection + retarget to nearest speaker
- **Alternative:** Use DBAP (no coverage gaps)

---

## References

### Documentation

- [Spatialization/RENDERING.md](Spatialization/RENDERING.md) — Spatial renderer comprehensive docs
- [Dependencies/json_schema_info.md](Dependencies/json_schema_info.md) — LUSID & layout JSON schemas
- [LUSID/internalDocs/AGENTS.md](../LUSID/internalDocs/AGENTS.md) — LUSID-specific agent spec
- [LUSID/internalDocs/DEVELOPMENT.md](../LUSID/internalDocs/DEVELOPMENT.md) — LUSID dev notes
- [LUSID/internalDocs/xml_benchmark.md](../LUSID/internalDocs/xml_benchmark.md) — XML parser benchmarks

### External Resources

- [Dolby Atmos ADM Interoperability Guidelines](https://dolby.my.site.com/professionalsupport/s/article/Dolby-Atmos-IMF-IAB-interoperability-guidelines)
- [EBU Tech 3364: Audio Definition Model](https://tech.ebu.ch/publications/tech3364)
- [AlloLib Documentation](https://github.com/AlloSphere-Research-Group/AlloLib)
- [libbw64 (EBU)](https://github.com/ebu/libbw64)
- [Example ADM Files](https://zenodo.org/records/15268471)

### Known Issues

#### ✅ RESOLVED — WAV 4 GB Header Overflow (2026-02-16)

**Root Cause:** Standard WAV uses an unsigned 32-bit data-chunk size (max 4,294,967,295 bytes). A 56-channel × 566s × 48 kHz × 4-byte render produces 6,085,632,000 bytes, which wraps to 1,790,664,704 → readers see 166.54 s instead of 566 s. The C++ renderer was correct all along — only the WAV header was wrong.

**Fix:** `WavUtils.cpp` now auto-selects `SF_FORMAT_RF64` when data exceeds 4 GB.

#### ✅ RESOLVED — masterGain Default Mismatch (2026-02-16)

- `SpatialRenderer.hpp`, `main.cpp` help text, and `RENDERING.md` all standardized to `0.5`.
- **Fix:** Updated all three locations to match `float masterGain = 0.5`.

#### ⚠️ OPEN — runPipeline.py Robustness (OFFLINE PIPELINE ONLY — do not fix)

> **Scope note (2026-03-07):** `runPipeline.py` is the **deprecated offline pipeline** and is not a focus of active development. These bugs are documented for archaeology only. Do not spend time on them unless the owner explicitly scopes offline pipeline work. The active realtime entrypoints are `spatialroot_realtime` and `gui/imgui/`.

- `sys.argv[1]` accessed before bounds check (line 158 vs check on line 162)
- Double audio-channel scan wastes ~28 s per run (calls both `exportAudioActivity()` and `channelHasAudio()`)
- **LUSID CLI branch bug (line 177):** `run_pipeline_from_LUSID()` is called with `outputRenderPath` which is never defined in the `__main__` block — will crash with `NameError` if a LUSID package is passed via CLI

#### ℹ️ NOTE — Large Interleaved Buffer Allocation

- `WavUtils.cpp` allocates a single `std::vector<float>` of `totalSamples × channels` (~5.67 GB for the 56-channel test case, ~11.3 GB peak with per-channel buffers).
- Works on high-memory machines but may OOM on constrained systems.
- **Mitigation:** Chunked/streaming write (future work).

## Build & Tooling Scripts (Phase 6)

**Updated:** March 31, 2026

spatialroot uses shell/PowerShell scripts (no Python toolchain required) to initialize submodules and drive a top-level CMake build.

### Primary scripts

- `./init.sh` (macOS/Linux) — one-time: initializes required submodules and runs `./build.sh`
- `./build.sh` (macOS/Linux) — configures + builds via CMake; supports `--engine-only`, `--offline-only`, `--cult-only`, and `--gui`
- `./engine.sh` — convenience rebuild for the realtime engine
- `./run.sh` — launches the ImGui + GLFW GUI

Windows equivalents:

- `./init.ps1`, `./build.ps1`, `./run.ps1`

### Build products (top-level `build/`)

| Tool                 | Path                                                            | Status |
| -------------------- | --------------------------------------------------------------- | ------ |
| Realtime Engine CLI  | `build/spatial_engine/realtimeEngine/spatialroot_realtime`      | Active |
| Offline Renderer CLI | `build/spatial_engine/spatialRender/spatialroot_spatial_render` | Active |
| cult-transcoder CLI  | `build/cult_transcoder/cult-transcoder`                         | Active |
| Desktop GUI (ImGui)  | `build/gui/imgui/Spatial Root`                                  | Active |

### Version History

- **v0.5.4** (2026-03-07): Phase 11 Bug-Fix Pass — four audio-quality defects resolved in the realtime engine: (1) focus auto-compensation plumbing fixed (`autoComp` field added to `ControlsSnapshot`, `mAutoCompValue` private member in Spatializer, override mode so autoComp and manual slider never clobber each other); (2) streaming hardened (10s chunks, 75% preload threshold = 7.5s runway, exponential fade-to-zero on residual miss, per-stream `underrunCount`); (3) NaN/Inf/clamp guards added (min-distance 5cm nudge before DBAP, post-render clamp ±4.0f pass with `nanGuardCount`); (4) per-frame focus interpolation via `renderSample()` replaces single `renderBuffer()` per block — eliminates block-boundary pops on focus changes. New invariants 8, 9, 10 documented in `realtime_master.md`.
- **v0.5.3** (2026-03-07): `buildCultTranscoder()` + `initializeCultTranscoderSubmodules()` added
  to both posix and windows configs; `setupCppTools()` wired to build cult-transcoder as step 2.
  See `internalDocsMD/OS/build-wiring.md` for posix `make` vs `cmake --build` notes.
- **v0.5.2** (2026-03-07): Phase 5 GUI — TRANSCODE tab added (`RealtimeTranscodePanel`, `RealtimeTranscoderRunner`); font system overhauled (`ui_font()`, Courier New, no bold); file dialog two-button pattern; `RealtimeInputPanel` File.../Pkg... buttons; QTabWidget ENGINE/TRANSCODE tabs
- **v0.5.2** (2026-03-07): Phase 4 cult-transcoder -- `--lfe-mode` flag (hardcoded|speaker-label), ADM profile detection (DolbyAtmos/Sony360RA), 40/40 tests pass
- **v0.6.0** (2026-03-31): Phase 6 — C++ refactor complete. Python entrypoints/build tooling/GUI removed; ImGui + GLFW GUI embeds `EngineSessionCore`; runtime control is direct C++ API with optional OSC (disable via `oscPort=0`).
- **v0.5.2** (2026-02-16): RF64 auto-selection for large renders, WAV header overflow fix, debug print cleanup, masterGain/dbap_focus/master_gain fixes
- **v0.5.2** (2026-02-13): Duration field added to LUSID scene, ADM duration preservation, XML parser migration, eliminate intermediate JSONs
- **v0.5.0** (2026-02-05): Initial LUSID Scene format
- **PUSH 3** (2026-01-28): LFE routing, multi-spatializer support (DBAP/VBAP/LBAP)
- **PUSH 2** (2026-01-27): Renamed VBAPRenderer → SpatialRenderer
- **PUSH 1** (2026-01-27): VBAP robustness (zero-block detection, fast-mover sub-stepping)

---

## Contact & Contribution

**Project Lead:** Lucian Parisi  
**Organization:** Cult DSP  
**Repository:** https://github.com/Cult-DSP/spatialroot

For questions or contributions, open an issue or PR on GitHub.

---

**End of Agent Context Document**

**Active development scope (2026-03-31):** The canonical realtime entrypoints are the `spatialroot_realtime` CLI and the ImGui + GLFW GUI (`gui/imgui/`). Python entrypoints and the PySide6 GUI are removed.

**OSC policy:** OSC remains supported as an optional secondary surface (default port 9009). Disable with `oscPort=0`.
