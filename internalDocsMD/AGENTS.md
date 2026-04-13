# spatialroot — Agent Context

**Last Updated:** April 12, 2026  
**Project:** spatialroot — Open Spatial Audio Infrastructure  
**Lead Developer:** Lucian Parisi

> **Phase 6 (2026-03-31): C++ refactor complete.** Python GUI, entrypoints, build tooling, and venv removed.
> Primary entry points: `spatialroot_realtime` CLI binary and `gui/imgui/` Dear ImGui + GLFW desktop GUI (embeds `EngineSessionCore` in-process).
> Build: `./init.sh` (once) then `./build.sh`. Run: `./run.sh`.

---

## Quick Navigation

| Topic                                                            | File                                     | Key Sections                                                                                                                                                                                                                             |
| ---------------------------------------------------------------- | ---------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| EngineSession API contract, structs, lifecycle, hard constraints | [API_internal.md](API_internal.md)       | [Contract](API_internal.md#contract) · [Hard Constraints](API_internal.md#hard-constraints) · [Validation & Gotchas](API_internal.md#validation--known-gotchas)                                                                          |
| CI config, vendored deps, Windows fixes, CMake wiring            | [BUILD_AND_CI.md](BUILD_AND_CI.md)       | [CI Overview](BUILD_AND_CI.md#ci-overview) · [Dep Audit](BUILD_AND_CI.md#dependency-audit) · [Build Notes](BUILD_AND_CI.md#build-system-notes)                                                                                           |
| LUSID scene format, speaker layout JSON, LUSID package import    | [DEPENDENCIES.md](DEPENDENCIES.md)       | [LUSID Scene](DEPENDENCIES.md#lusid-scene-json-format) · [Speaker Layout](DEPENDENCIES.md#speaker-layout-json-format) · [Package Import](DEPENDENCIES.md#lusid-package-import-contract-spatialseed)                                      |
| Realtime engine agents, bug audit, OSC params, threading         | [REALTIME_ENGINE.md](REALTIME_ENGINE.md) | [Agent Table](REALTIME_ENGINE.md#agent-architecture-overview) · [Bug Audit](REALTIME_ENGINE.md#bug-audit-april-1-2026) · [OSC Params](REALTIME_ENGINE.md#osc-parameter-reference) · [Threading](REALTIME_ENGINE.md#threading-and-safety) |
| Repo cleanup, AlloLib lightweighting                             | [REPO_AUDITING.md](REPO_AUDITING.md)     | [Cleanup Audit](REPO_AUDITING.md#repository-cleanup-audit) · [AlloLib Audit](REPO_AUDITING.md#allolib-dependency-audit)                                                                                                                  |
| Spatializers, DBAP/VBAP/LBAP, elevation, rendering CLI           | [SPATIALIZATION.md](SPATIALIZATION.md)   | [Rendering System](SPATIALIZATION.md#rendering-system) · [DBAP Testing](SPATIALIZATION.md#dbap-field-testing-notes)                                                                                                                      |
| C++ refactor history, Python pipeline, old GUI, old build system | [devHistory.md](devHistory.md)           | [Phase 6 Refactor](devHistory.md#phase-6--c-refactor-complete-march-29-31-2026) · [Python GUI](devHistory.md#python-gui-pyside6--phase-10-february-2026) · [Old Pipeline](devHistory.md#python-offline-pipeline-v39-march-9-2026)        |

---

## Project Overview

spatialroot is a C++17 codebase for decoding and rendering Audio Definition Model (ADM) Broadcast WAV files (Dolby Atmos masters) to arbitrary speaker arrays using multiple spatialization algorithms.

**Key capabilities:**

- Multi-format input: Dolby Atmos ADM BWF WAV files
- Multi-spatializer: DBAP (default), VBAP, LBAP
- Arbitrary speaker layouts defined in JSON
- LUSID scene format — canonical time-sequenced spatial data
- ADM duration preservation (prevents truncated renders)
- Dear ImGui + GLFW native GUI — embeds `EngineSessionCore` in-process
- Subwoofer/LFE handling with automatic routing
- C++ test suite via CTest/Catch2
- Cross-platform: macOS, Linux, Windows

**Technology stack:** C++17, AlloLib (DBAP/VBAP/LBAP + audio I/O + OSC), libsndfile (WAV/RF64), Dear ImGui + GLFW (GUI), CMake 3.20+

---

## Architecture & Data Flow

```
ADM BWF WAV File
    │
    ├─► cult-transcoder (C++) ──► scene.lusid.json (+ stems when needed)
    │                                    │
    │              processedData/stageForRender/scene.lusid.json  (CANONICAL)
    │
    └─► spatialroot_realtime (C++)
              │
              ├─► loads speaker layout + LUSID scene
              ├─► streams sources (mono stems or direct ADM multichannel)
              └─► outputs to hardware speakers (AlloLib AudioIO)
```

**LUSID `scene.lusid.json` is the source of truth** for spatial data. The C++ renderer reads LUSID directly — no intermediate format conversion. See [DEPENDENCIES.md § LUSID Scene JSON Format](DEPENDENCIES.md#lusid-scene-json-format).

---

## Core Components

### `spatialroot_realtime` — Primary CLI

Runs the realtime spatial audio engine. Inputs: speaker layout (`--layout`), LUSID scene (`--scene`), and either mono stems (`--sources`) or multichannel ADM WAV (`--adm`).

```bash
# ADM direct streaming mode
./build/spatialroot_realtime \
    --layout spatial_engine/speaker_layouts/translab-sono-layout.json \
    --scene processedData/stageForRender/scene.lusid.json \
    --adm sourceData/SWALE-ATMOS-LFE.wav \
    --gain 0.5 --buffersize 512

# Mono file mode (LUSID package with pre-split stems)
./build/spatialroot_realtime \
    --layout spatial_engine/speaker_layouts/allosphere_layout.json \
    --scene processedData/stageForRender/scene.lusid.json \
    --sources sourceData/lusid_package \
    --gain 0.1 --buffersize 512
```

Run `./build/spatialroot_realtime --help` for full flag list. See also `PUBLIC_DOCS/API.md`.

### `gui/imgui/` — Dear ImGui + GLFW Desktop GUI (Phase 6)

Native GUI linking `EngineSessionCore` directly in-process. No subprocess, no OSC dependency for local control.

| File                                | Role                                                          |
| ----------------------------------- | ------------------------------------------------------------- |
| `gui/imgui/src/App.hpp`             | App class declaration — all state members                     |
| `gui/imgui/src/App.cpp`             | All UI rendering and engine lifecycle logic — read this first |
| `gui/imgui/src/main.cpp`            | GLFW setup, ImGui init, render loop                           |
| `gui/imgui/src/FileDialog_macOS.mm` | NSOpenPanel file pickers + macOS Dock icon setter             |
| `gui/imgui/CMakeLists.txt`          | GUI build — includes xxd embed step for miniLogo.png          |

**Build:** `./init.sh` then `./build.sh --gui`. **Run:** `./run.sh`.

### `cult-transcoder` — ADM → LUSID Tool

```bash
build/cult_transcoder/cult-transcoder transcode \
    --in <adm_wav_path> --in-format adm_wav \
    --out processedData/stageForRender/scene.lusid.json \
    --out-format lusid_json \
    [--lfe-mode hardcoded|speaker-label]
```

Source: `cult_transcoder/` (git submodule). See `cult_transcoder/internalDocsMD/AGENTS-CULT.md` for full cult-transcoder docs.

### `spatialroot_spatial_render` — Offline Batch Renderer

Renders a LUSID scene + sources to an N-channel WAV file (offline, not real-time). See [SPATIALIZATION.md § Rendering System](SPATIALIZATION.md#rendering-system) for full CLI docs.

### `EngineSession` — C++ Embeddable Engine API

`EngineSessionCore` static library. Strict 5-stage lifecycle. See [API_internal.md](API_internal.md) for full contract, hard constraints, and threading rules.

### LUSID Schema

`LUSID/` submodule. Schema: `LUSID/schema/lusid_scene_v0.5.schema.json`. The Python LUSID runtime/library was removed in Phase 6 — only the schema and docs remain.

---

## File Structure

```
spatialroot/
├── init.sh / build.sh / run.sh / engine.sh       # macOS/Linux
├── init.ps1 / build.ps1 / run.ps1                # Windows
├── build/                                         # CMake output
│   ├── spatialroot_realtime
│   ├── spatialroot_spatial_render
│   ├── cult_transcoder/cult-transcoder
│   └── gui/imgui/Spatial Root
├── gui/imgui/                                     # Dear ImGui + GLFW GUI
├── spatial_engine/
│   ├── realtimeEngine/                            # Realtime engine source
│   │   └── src/  (EngineSession.hpp, Spatializer.hpp, Streaming.hpp, ...)
│   ├── src/                                       # Shared loaders (JSONLoader, LayoutLoader, WavUtils)
│   └── spatialRender/                             # Offline renderer source
├── cult_transcoder/                               # ADM↔LUSID transcoder (submodule)
├── thirdparty/
│   ├── allolib/                                   # Audio I/O, DBAP, OSC (shallow submodule)
│   ├── libsndfile/                                # WAV/RF64 I/O
│   ├── imgui/                                     # Dear ImGui
│   └── glfw/                                      # GLFW window/GL context
├── processedData/                                 # Working outputs (scene, caches)
│   └── stageForRender/scene.lusid.json            # Canonical scene input for engine
├── sourceData/                                    # Input audio + LUSID packages
├── spatial_engine/speaker_layouts/               # JSON speaker layout files
├── LUSID/                                         # LUSID schema + docs (no Python runtime)
├── internalDocsMD/                                # Internal docs (this file + consolidated files)
└── PUBLIC_DOCS/                                   # Public-facing API docs
```

---

## Runtime Control Plane

**Primary (in-process):** ImGui GUI calls direct C++ setters on `EngineSession`:

- `setMasterGain(float)`, `setDbapFocus(float)`, `setSpeakerMixDb(float)`, `setSubMixDb(float)`, `setAutoCompensation(bool)`, `setElevationMode(ElevationMode)`

**Secondary (optional OSC):** Remains available for external tooling/remote control.

- Default port: `9009`; disable with `oscPort=0` in `EngineOptions`
- See [REALTIME_ENGINE.md § OSC Parameter Reference](REALTIME_ENGINE.md#osc-parameter-reference) for full address/range table

---

## Common Issues & Solutions

### ADM / Transcoder

| Issue                                         | Solution                                                                                |
| --------------------------------------------- | --------------------------------------------------------------------------------------- |
| Empty scene / no frames after transcoding     | Check ADM XML format — run `cult-transcoder` with verbose output                        |
| `cult-transcoder` not found                   | Build with `./build.sh --cult-only`                                                     |
| LFE stem missing / wrong                      | Check `--lfe-mode` flag. `hardcoded` = channel 4; `speaker-label` = ADM label detection |
| `ModuleNotFoundError: No module named 'lxml'` | You're running archived Python code. Use the current C++ toolchain.                     |

### Spatialization

| Issue                                        | Solution                                                                                   |
| -------------------------------------------- | ------------------------------------------------------------------------------------------ |
| Sources at zenith/nadir silent               | Use `--elevation_mode compress` (RescaleFullSphere)                                        |
| Zero output / silent channels                | Verify `LayoutLoader.cpp` converts radians → degrees: `azimuth * 180.0f / M_PI`            |
| LFE too loud/quiet                           | Tune `dbap_sub_compensation` in `SpatialRenderer.cpp` (TODO: make CLI option)              |
| DBAP sounds wrong/reversed                   | AlloLib DBAP coordinate transform `(x,y,z)→(x,-z,y)` is compensated automatically          |
| Render truncated (e.g. 166s instead of 566s) | RF64 auto-selection in `WavUtils.cpp` handles files > 4 GB — ensure you're on current code |

### Build

| Issue                                      | Solution                                                            |
| ------------------------------------------ | ------------------------------------------------------------------- |
| CMake can't find AlloLib                   | `git submodule update --init --recursive`                           |
| Build fails "C++17 required"               | CMake 3.20+ required; ensure compiler supports C++17                |
| Changes not reflected after rebuild        | Clean build: `rm -rf build/ && ./build.sh`                          |
| Layout/device channel mismatch (fast-fail) | Match speaker layout channel count to hardware output channel count |

### LUSID Scene

| Issue                                | Solution                                           |
| ------------------------------------ | -------------------------------------------------- |
| Parser warnings about unknown fields | Parser is permissive — non-fatal. Check for typos. |
| Frames not sorted                    | Parser auto-sorts — informational only             |
| Duplicate node IDs in frame          | Parser keeps last occurrence — fix upstream data   |

---

## Development Workflow

### Making Changes to Realtime Engine

1. Edit files in `spatial_engine/realtimeEngine/src/`
2. Rebuild: `./build.sh --engine-only`
3. Test: `./run.sh` (launches GUI) or run `spatialroot_realtime` directly

### Making Changes to Offline Renderer

1. Edit files in `spatial_engine/src/` or `spatial_engine/spatialRender/`
2. Rebuild: `./build.sh --offline-only`
3. Test manually — see [SPATIALIZATION.md § CLI Usage](SPATIALIZATION.md#cli-usage)

### Adding a New Node Type to LUSID

1. Update JSON schema: `LUSID/schema/lusid_scene_v0.5.schema.json`
2. Update C++ loaders if renderer needs to consume it: `spatial_engine/src/JSONLoader.cpp`
3. Document in `PUBLIC_DOCS/API.md` and relevant internal docs

### Build Targets Reference

| Target                       | Flag             | Description            |
| ---------------------------- | ---------------- | ---------------------- |
| `spatialroot_realtime`       | `--engine-only`  | Realtime engine CLI    |
| `spatialroot_spatial_render` | `--offline-only` | Offline batch renderer |
| `cult-transcoder`            | `--cult-only`    | ADM → LUSID transcoder |
| `spatialroot_gui`            | `--gui`          | Dear ImGui + GLFW GUI  |

---

## AlloLib Audit & Lightweighting

**Status:** Shallow clone implemented. See [REPO_AUDITING.md § AlloLib Dependency Audit](REPO_AUDITING.md#allolib-dependency-audit) for full module keep/trim/future lists.

`.gitmodules` has `shallow = true` for `thirdparty/allolib`. `init.sh` uses `--depth 1` (`~510 MB saved`).  
For existing deep clones: `./scripts/shallow-submodules.sh`  
Opt-in sparse tree: `./scripts/sparse-allolib.sh`

---

## Track B — Sony 360RA ADM Profile (FUTURE — DO NOT IMPLEMENT YET)

**Objective:** Add a profile adaptation layer inside LUSID to accept a wider range of ADM variants (Sony 360RA, edge-case Atmos exports).

**Planned work:**

- C++ classes inside `cult-transcoder/src/adm_profiles/`: `detect_profile.cpp`, `atmos_adapter.cpp`, `sony360_adapter.cpp`, `common.cpp`
- Sony 360RA needs: opaque string IDs (hex-like suffixes `...0a`), `rtime/duration` with `S####` suffix, mute-block handling (gain=0 segments), block compaction to avoid massive frame counts

**Status:** Document only. Await instructions before implementing.
