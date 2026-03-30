# Spatial Root — C++ Refactor Audit (Revised)
**Date:** 2026-03-29
**Scope:** Pre-refactor architectural assessment grounded in current repo state
**Purpose:** Source-of-truth document for the C++-first refactor prompt
**Supersedes:** `secondary_audit.md`

---

## What Changed from the Prior Audit

The prior audit (secondary_audit.md) left the GUI/runtime-control replacement path open as an unresolved question. That question is now resolved. This revision updates the audit to reflect the chosen direction:

- The replacement GUI is a **polished C++ Qt desktop application**, not an AlloLib/ImGui wrapper or a Qt app communicating over OSC.
- The Qt app will **embed `EngineSessionCore` directly** — same process, direct C++ API calls.
- **Direct in-process runtime setters** on `EngineSession` are now a required API-hardening task, not future polish. The atomics in `RealtimeConfig` already support this threading model; what is missing is the public setter surface on `EngineSession`.
- **OSC is demoted to an optional secondary control surface** (remote/debug/network), not the primary local GUI control path.
- The migration sequence is reordered to reflect this: `EngineSessionCore` must be hardened for Qt embedding before the Qt GUI is built, and OSC removal/demotion is a consequence of that, not a prerequisite.
- The old open question about python-osc replacement is removed. It is answered: direct C++ API calls via the Qt host.

Everything else in the prior audit that was accurate is preserved.

---

## 1. Executive Verdict

The repo is meaningfully closer to the paper's architecture than it first appears. The realtime engine (`EngineSession` and its subsystems) is a real, well-implemented C++ library with a clean typed API. CULT is a working C++ CLI (standalone submodule). The JSON-based LUSID contract is honored end-to-end in C++. What remains Python is launch infrastructure and GUI glue, not core audio logic.

The main structural gap is that **the three surfaces (library API, CLI, GUI) are not cleanly separated at the process boundary**: the GUI launches Python which launches the binary, the build system lives in Python, and there is no top-level CMake or shell script that ties the stack together. The C++ library API (`EngineSession`) is real but not yet usable by an embedding host without building from the full source tree.

The chosen target direction — a C++ Qt GUI embedding `EngineSessionCore` directly — is well-supported by the existing C++ architecture. `RealtimeConfig` already uses `std::atomic` fields for all runtime-controllable parameters. What is missing is a thin public setter surface on `EngineSession` that exposes those atomics to a Qt host. OSC currently provides the only external write path to those atomics; once direct setters exist, OSC becomes optional.

The stack is not far from the paper's target. The build and launch infrastructure needs to move out of Python, `EngineSessionCore` needs a small API hardening pass for embedding, and the Qt GUI replaces the Python GUI.

---

## 2. Current Stack Map

### 2.1 Realtime Engine — `spatial_engine/realtimeEngine/`

- **Role:** Real-time spatial audio playback
- **Language:** C++17
- **State:** Production-ready, well-documented
- **Runtime-critical:** Yes
- **Key files:** `EngineSession.hpp/.cpp`, `RealtimeBackend.hpp`, `RealtimeTypes.hpp`, `Streaming.hpp`, `Pose.hpp`, `Spatializer.hpp`, `OutputRemap.hpp`, `MultichannelReader.hpp`
- **Dependencies:** AlloLib (DBAP, audio I/O, OSC parameter server via `al::ParameterServer`), shared loaders from `spatial_engine/src/`

`EngineSession` implements the exact lifecycle described in `PUBLIC_DOCS/API.md`:
`configureEngine()` → `loadScene()` → `applyLayout()` → `configureRuntime()` → `start()` → polling loop → `shutdown()`.

CMake builds two targets:
- `EngineSessionCore` — static library (linkable)
- `spatialroot_realtime` — CLI binary that links `EngineSessionCore`

**Runtime parameter atomics** are defined in `RealtimeConfig` (`RealtimeTypes.hpp`): `masterGain`, `dbapFocus`, `loudspeakerMix`, `subMix`, `focusAutoCompensation`, `elevationMode`, `paused`. All are `std::atomic` with relaxed ordering, written by the OSC listener thread and read by the audio thread. The threading model is already correct for direct C++ setter calls — a Qt host writing these atomics via setter methods on `EngineSession` would be identical to how the OSC listener writes them.

**Current limitation:** No public setter methods on `EngineSession` expose these atomics. The only external runtime write path is OSC via `al::ParameterServer`. `mParamServer` and `mOscParams` are private. `configureRuntime()` is only callable before `start()`.

OSC parameter control via `al::ParameterServer` is wired in `EngineSession.cpp`. Parameters: gain, focus, speaker_mix_db, sub_mix_db, auto_comp, paused, elevation_mode. The OSC port defaults to 9009 (note: README incorrectly says 12345 — the code is authoritative).

### 2.2 Offline Renderer — `spatial_engine/src/` and `spatial_engine/spatialRender/`

- **Role:** Batch multichannel WAV rendering
- **Language:** C++17
- **State:** Functional; will continue to be used post-refactor (exact integration TBD)
- **Runtime-critical:** No (offline only)
- **Key files:** `SpatialRenderer.cpp/.hpp`, `VBAPRenderer.cpp/.hpp`, `JSONLoader.cpp/.hpp`, `LayoutLoader.cpp/.hpp`, `WavUtils.cpp/.hpp`

`JSONLoader` and `LayoutLoader` are shared with the realtime engine via CMake `target_include_directories`. `SpatialRenderer` is not shared — the realtime engine re-implements rendering in `Spatializer.hpp`.

The Python offline pipeline (`runPipeline.py`) that currently orchestrates `SpatialRenderer` will be removed. How the offline renderer is invoked post-refactor is TBD, but the C++ code itself (`SpatialRenderer.cpp` and the `spatialroot_spatial_render` binary) is retained.

### 2.3 CULT Transcoder — `cult_transcoder/`

- **Role:** ADM → LUSID scene conversion
- **Language:** C++17
- **State:** Functional; ADM + Sony 360RA working; MPEG-H incomplete
- **Scope:** Standalone submodule used in other projects — **not to be modified in the scope of this refactor**
- **Key files:** `include/cult_transcoder.hpp`, `src/transcoder.cpp`, `src/adm_to_lusid.cpp`, `transcoding/adm/adm_reader.cpp`
- **Dependencies:** pugixml (FetchContent), libbw64 (git submodule at `cult_transcoder/thirdparty/`)

CULT has a public API header (`cult_transcoder.hpp` with `cult::transcode()`) but **the current CMake builds only the CLI binary, not a separately installable library target**. Spatial Root currently reaches CULT exclusively via subprocess.

**Post-refactor target:** CULT should be invoked as a subprocess from shell scripts or directly from a C++ host, not from Python. Changes to CULT's CMake (add library target, install target) are future work documented in Section 8.

### 2.4 Python Launch Infrastructure (to be removed)

| File | Role | Fate |
|---|---|---|
| `runRealtime.py` | CLI launcher (no GUI) — calls setupCppTools(), invokes cult-transcoder + spatialroot_realtime as subprocesses | **Remove** |
| `realtimeMain.py` | GUI launcher — invokes the PySide6 GUI | **Remove** |
| `runPipeline.py` | Offline pipeline orchestrator | **Remove** (already deprecated) |
| `src/config/configCPP_posix.py` | CMake/make orchestration via subprocess | **Remove** (replace with shell script) |
| `src/config/configCPP_windows.py` | Windows equivalent | **Remove** |
| `src/config/configCPP.py` | Dispatcher | **Remove** |

### 2.5 Python GUI — `gui/realtimeGUI/` (to be removed)

- **Language:** Python 3 + PySide6
- **State:** Active, Phase 10 complete
- **Current runtime control path:** GUI → python-osc UDP → C++ `al::ParameterServer` on port 9009
- **Launch path:** QProcess → `runRealtime.py` subprocess → `spatialroot_realtime` binary

The GUI is three process-hops from the audio thread. Its OSC control surface uses `pythonosc.udp_client` (python-osc). Post-refactor this entire path is removed. The replacement is a C++ Qt application that embeds `EngineSessionCore` directly and controls it via C++ API calls.

### 2.6 Python LUSID Library — `LUSID/src/` (to be removed)

| File | Role | Status |
|---|---|---|
| `scene.py` | Python dataclasses for LUSID scene nodes | Not used by any C++ code |
| `xml_etree_parser.py` | Python ADM XML → LUSID (stdlib only) | Superseded by CULT transcoder |
| `parser.py` | Python LUSID JSON loader | Only used by deprecated offline pipeline |

This library is a parallel implementation to the C++ code. Once the offline pipeline and Python GUI are removed, this library has no purpose.

### 2.7 Python Offline Pipeline Scripts — `src/` (to be removed)

| Path | Role | Fate |
|---|---|---|
| `src/analyzeADM/` | ADM XML analysis (superseded by CULT) | **Remove** |
| `src/packageADM/` | Stem splitting, scene packaging | **Remove** |
| `src/analyzeRender.py` | PDF render analysis | **Remove** (offline only) |
| `src/createRender.py` | Subprocess launcher for offline renderer | **Remove** |
| `src/createFromLUSID.py` | LUSID package render launcher | **Remove** |

### 2.8 Virtual Environment — `spatialroot/`

A full Python 3.14 venv committed into the repo root (~1.6 GB). Contains jupyter, numpy, matplotlib, pandas, fonttools, PySide6, python-osc, and more.

Post-refactor, the Python runtime dependency drops to zero for the core engine. The venv should be removed entirely.

---

## 3. Paper Alignment Audit

### 3.1 CULT as embeddable C++ transcoder with library API and CLI

**Partially aligned.**

The CLI (`cult-transcoder`) is real and functional. The public API header (`cult_transcoder.hpp`) defines `cult::transcode()`. However, CMake does not produce a separate static or shared library target — only the CLI binary. Spatial Root reaches CULT via subprocess, not via the C++ API. Adding a library target to CULT's CMake is documented as future work (Section 8) and is outside this refactor's scope.

### 3.2 LUSID as canonical scene and package structure

**Aligned for the realtime path; partially for the overall stack.**

The JSON format is genuinely canonical: CULT writes it, `JSONLoader.cpp` reads it, the engine runs from it. The gap is that there is no standalone C++ LUSID reader library — `JSONLoader.cpp` is the only C++ reader and it lives inside the engine's source tree. The relationship between `JSONLoader.cpp` and a future CULT library reader is documented as future work (Section 8).

### 3.3 Spatial Root as C++ runtime with library API, CLI, and optional GUI

**Mostly aligned; key remaining gap is the API embedding surface.**

`EngineSession` is a real C++ static library (`EngineSessionCore`) with a clean typed API documented in `API.md`. The CLI binary (`spatialroot_realtime`) works. The Python GUI works but goes through Python and will be replaced by a C++ Qt application.

Gaps:
- No CMake `install()` target for `EngineSessionCore` — an embedding host cannot find it without building from source
- No public runtime setter methods on `EngineSession` — a Qt host embedding `EngineSessionCore` cannot control live parameters without OSC (see Section 5)
- `EngineOptions::elevationMode` is `int` in the public API struct, not the `ElevationMode` enum from `RealtimeTypes.hpp`

### 3.4 Runtime control surfaces

**Current reality:** OSC via `al::ParameterServer` is the only external runtime control path. The Python GUI sends OSC. The CLI sets initial state via `configureRuntime()`. There are no C++ setter methods for live parameter changes.

**Chosen target:** The Qt GUI embeds `EngineSessionCore` and calls runtime setters directly. OSC is retained as an optional secondary surface for remote/debug/network control. The three surfaces (CLI, direct C++ API, optional OSC) will have consistent capabilities once the setter surface exists.

**Key remaining mismatch:** The C++ API does not yet expose the runtime setter surface that the Qt GUI will require. This is a required hardening step, not optional future work (see Section 6).

### 3.5 Distinct but interoperable layers

**Partially aligned.**

CULT and Spatial Root are distinct in the codebase. LUSID is a format, not a library. The layers are interoperable at the file format level. Interoperability at the API level (C++ library linkage) is future work.

---

## 4. Python Dependency Classification

### Keep for now
- None — all meaningful Python components are on a removal path.

### Migrate in this refactor
- `src/config/configCPP*.py` — replace with `build.sh`
- `runRealtime.py` — replace with shell script or direct binary invocation
- `realtimeMain.py` — remove with Python GUI
- `gui/realtimeGUI/` — remove; replaced by C++ Qt application

### Remove with offline pipeline
- `LUSID/src/` and `LUSID/tests/` — parallel C++ implementation exists
- `src/analyzeADM/`, `src/packageADM/`, `src/analyzeRender.py`
- `runPipeline.py` — already deprecated

### Reduce / remove
- `requirements.txt` — post-refactor runtime Python dependency drops to zero
- `spatialroot/` venv — remove entirely

---

## 5. Public API / CLI / GUI Reality Check

### Current Reality

**C++ Library API (`EngineSession`)**

Exists and is real. `EngineSession.hpp` defines all types. `EngineSessionCore` is a CMake static library target. `API.md` documents it with a working quick-start example.

What is missing for Qt embedding:
- No CMake `install()` target for `EngineSessionCore` or its headers
- No pkg-config or find-package support
- The API header depends on AlloLib headers at include time — an embedding host must also locate AlloLib's include path
- **No per-parameter runtime setters callable while the engine is running.** There is no `setMasterGain(float)`, `setFocus(float)`, `setSpeakerMixDb(float)`, `setSubMixDb(float)`, `setAutoCompensation(bool)`, `setElevationMode(ElevationMode)` on `EngineSession`. Only `setPaused(bool)` exists as a runtime setter. All other parameters are locked in after `configureRuntime()` / `start()` unless the caller sends OSC.
- `EngineOptions::elevationMode` should be the `ElevationMode` enum, not `int`

**Documented hard constraints** (from `api_mismatch_ledger.md`) that will not change in this refactor:
1. Staged setup is non-negotiable: `configureEngine()` → `loadScene()` → `applyLayout()` → `configureRuntime()` → `start()`
2. No restartable transport: only `setPaused(bool)` is supported
3. OSC ownership: `mParamServer` is internal to `EngineSession` and cannot be shared with the host
4. Shutdown sequence: `mParamServer->stopServer()` → `mBackend->shutdown()` → `mStreaming->shutdown()` — violating this causes deadlocks on macOS CoreAudio

**Current limitation:** The engine's active-channel bitmask tracking uses `uint64_t`, which implicitly caps the engine at 64 output channels. The AlloSphere (54.1 channels) is within range. This limit is not documented in `API.md` and should be added.

**CLI Binary (`spatialroot_realtime`)**

Works correctly. Well-documented `--help` output. `--list-devices` is useful for hardware installations.

**The primary documentation problem:** `README.md` documents `realtimeMain.py` (Python wrapper) as the primary CLI entry point, with different flag names than the actual C++ binary. The C++ binary's flags should be primary documentation.

### Chosen Target

**CLI:** Remains a direct binary surface. No change in design.

**GUI:** A polished C++ Qt desktop application that embeds `EngineSessionCore` in-process. The Qt application calls the standard `EngineSession` lifecycle (`configureEngine()` → ... → `start()`), then controls live parameters via direct C++ setter calls. No OSC involved for normal local desktop use.

**OSC:** Retained as an optional secondary control surface for remote/debug/network scenarios. Whether OSC is built by default or becomes a build-time/runtime option is an open question (Section 7).

**Runtime setter surface (required for Qt embedding):** The `RealtimeConfig` struct in `RealtimeTypes.hpp` already uses `std::atomic` for all runtime-controllable parameters, with relaxed ordering. The threading model is correct. What is needed is a set of public methods on `EngineSession` that write these atomics — the same operation the OSC listener thread already performs. At minimum:

```
setMasterGain(float)
setFocus(float)
setSpeakerMixDb(float)
setSubMixDb(float)
setAutoCompensation(bool)
setElevationMode(ElevationMode)
setPaused(bool)   // already exists
```

These are thin wrappers over atomic stores. The threading model does not change. This is an API surface addition, not a redesign.

---

## 6. Resolved Questions and Decisions

**1. Offline rendering after refactor**
`runPipeline.py` and all Python offline pipeline orchestration will be removed. `SpatialRenderer.cpp` and the `spatialroot_spatial_render` binary are retained. How the offline renderer is invoked post-refactor is TBD — it will not involve Python.

**2. `runRealtime.py` vs `realtimeMain.py`**
Both are Python entry points and both will be removed as part of this refactor.

**3. CULT scope**
CULT is a standalone submodule used in other projects outside this repo. **CULT's source is not to be modified in the scope of this refactor.**

**4. GUI and runtime control path** *(previously open; now resolved)*
The replacement GUI is a C++ Qt desktop application that embeds `EngineSessionCore` directly. Local GUI runtime control goes through direct C++ API calls, not OSC. OSC is demoted to an optional secondary control surface for remote/debug use.

**5. Runtime setter surface** *(previously future work; now required)*
Because the Qt GUI will embed `EngineSessionCore`, public runtime setter methods on `EngineSession` are a required part of the refactor path. The `RealtimeConfig` atomics already support this threading model. Adding the setter surface is a required API hardening task in Stage 2.

**6. `EngineSessionCore` API stability**
`EngineSessionCore` is intended to be a stable public API. This means: document constraints explicitly, avoid breaking the typed struct interface between releases, and add a CMake `install()` target so embedding hosts can use it.

**7. JSONLoader vs CULT boundary**
`JSONLoader.cpp` (inside the engine) reads LUSID JSON. CULT writes LUSID JSON. Whether `JSONLoader.cpp` should eventually be replaced by calling a CULT library reader is future work — outside this refactor's scope.

**8. 64-channel limit**
The `uint64_t` bitmask in `EngineStatus` implicitly caps the engine at 64 output channels. This should be documented in `API.md` but will not be addressed in this refactor.

---

## 7. Open Questions Before Implementation

**1. OSC: built by default or optional?**
Currently OSC is always initialized in `EngineSession` via `al::ParameterServer`. Post-refactor, with a Qt host using direct setters, OSC becomes optional. Should it remain always-on (harmless, just unused locally), become a build-time CMake option, or become a runtime option (e.g., `EngineOptions::enableOsc = false`)? The simplest path is to leave it always-on for now and revisit if it causes embedding friction.

**2. What is the post-refactor entry point for a user without the Qt GUI?**
Currently: `python runRealtime.py <args>` or `python realtimeMain.py`.
Post-refactor options:
- `./spatialroot_realtime --scene ... --layout ... --adm ...` (direct binary, requires CULT preprocessing done separately)
- `./run.sh <args>` (shell script that invokes cult-transcoder then spatialroot_realtime)

**3. How does the offline renderer get invoked post-refactor?**
`SpatialRenderer.cpp` and `spatialroot_spatial_render` will be retained. The Python orchestration will be removed. A shell script or a new C++ CLI main for the offline path is needed. This is TBD.

**4. Qt application structure**
The exact structure of the Qt GUI (widget vs QML, CMake integration, AlloLib header exposure to the Qt project) is TBD. The architectural requirement is: Qt app links `EngineSessionCore`, calls the lifecycle API, uses runtime setters for live control, reads `queryStatus()` for display. The internal Qt design is out of scope for this audit.

**5. Offline renderer invocation path**
See item 3. Deferred until Python removal is complete.

---

## 8. Future Work (Outside This Refactor)

**CULT CMake library target**
Add `add_library(cult_transcoder_lib STATIC ...)` alongside `add_executable(cult-transcoder ...)` in `cult_transcoder/CMakeLists.txt`. This would allow a C++ host to call `cult::transcode()` directly in-process. Outside this refactor's scope.

**CULT install target**
Add `install(TARGETS cult-transcoder)` and `install(TARGETS cult_transcoder_lib)` with headers. Outside this refactor's scope.

**JSONLoader vs CULT reader**
`JSONLoader.cpp` is a LUSID JSON reader embedded in the engine. A future refactor could replace it with a call to a CULT library reader. Premature until CULT has a library target.

**`EngineOptions::elevationMode` type**
Change from `int` to `ElevationMode` enum (already defined in `RealtimeTypes.hpp`). Small breaking API change — do it before external adoption. Could be done in Stage 2 during API hardening.

**64-channel limit**
The `uint64_t` bitmask in `EngineState` caps the engine at 64 output channels. For arrays larger than 64 channels, the bitmask tracking system would need to be redesigned.

**MPEG-H and IAMF support in CULT**
MPEG-H translation in CULT is incomplete. IAMF support is planned. These are CULT-level concerns tracked in `cult_transcoder/internalDocsMD/`.

**VBAP and LBAP in the realtime engine**
The realtime engine currently uses DBAP only. VBAP and LBAP are implemented in the offline renderer and in AlloLib but not yet wired into `Spatializer.hpp`.

---

## 9. Refactor Scope: What Changes, What Stays

### Changes in this refactor

| Component | Action |
|---|---|
| `src/config/configCPP*.py` | Remove; replace with `build.sh` |
| `runRealtime.py` | Remove |
| `realtimeMain.py` | Remove |
| `runPipeline.py` | Remove (already deprecated) |
| `gui/realtimeGUI/` | Remove (Python GUI) |
| `gui/` (all Python GUI) | Remove |
| `LUSID/src/` | Remove (parallel Python implementation) |
| `LUSID/tests/` | Remove (Python tests) |
| `src/analyzeADM/` | Remove |
| `src/packageADM/` | Remove |
| `src/analyzeRender.py`, `src/createRender.py`, `src/createFromLUSID.py` | Remove |
| `requirements.txt` | Remove (no remaining Python runtime dependency) |
| `spatialroot/` venv | Remove |
| `README.md` | Rewrite to document C++ binary and Qt GUI as primary interfaces |
| `PUBLIC_DOCS/API.md` | Add: documented constraints, 64-ch limit, embedding instructions, setter API |
| Realtime engine CMake | Add `install()` target for `EngineSessionCore` and its public headers |
| `EngineSession.hpp/.cpp` | Add public runtime setter methods (Stage 2) |
| `engine.sh` | Keep or replace with `build.sh` covering all components |

### Stays unchanged

| Component | Reason |
|---|---|
| `spatial_engine/realtimeEngine/src/` (all C++) | Core engine — no changes except setter additions |
| `spatial_engine/src/` (JSONLoader, LayoutLoader, WavUtils, SpatialRenderer) | Shared loaders and offline renderer — retained |
| `spatial_engine/spatialRender/` | Offline renderer build — retained |
| `cult_transcoder/` | Standalone submodule — not modified in this refactor |
| `spatial_engine/speaker_layouts/` | Layout JSON files — retained |
| `thirdparty/allolib` | AlloLib dependency — retained |
| `thirdparty/libbw64`, `thirdparty/libadm` | EBU submodules — retained (used by CULT) |
| `processedData/`, `sourceData/` | Test data — retained |
| `internalDocsMD/` | Internal docs — updated where relevant |

---

## 10. Recommended Migration Sequence

### Stage 1 — Build infrastructure and docs (no code changes)

1. Write `build.sh` that runs cmake on all three C++ components in dependency order:
   - `cult_transcoder/` (CULT CLI binary)
   - `spatial_engine/realtimeEngine/` (realtime engine)
   - `spatial_engine/spatialRender/` (offline renderer)

   Verify it produces the same binaries as the Python build path. This decouples the build from Python entirely.

2. Add CMake `install()` target for `EngineSessionCore` and its public headers to `spatial_engine/realtimeEngine/CMakeLists.txt`.

3. Update `README.md`:
   - Document `spatialroot_realtime` directly as the CLI surface with its actual flags
   - Remove references to `realtimeMain.py`, `runRealtime.py`, `runPipeline.py`
   - Fix OSC port: code says 9009, README says 12345 — code is authoritative
   - Document the two-step ADM workflow: `cult-transcoder transcode ...` then `spatialroot_realtime ...`
   - Note that the polished GUI is a C++ Qt application (in progress)

**Why this order:** No code changes, no breakage risk. Makes the actual architecture visible before modifying anything.

### Stage 2 — Harden `EngineSessionCore` for Qt embedding

4. Add public runtime setter methods to `EngineSession`:
   - `setMasterGain(float)`
   - `setFocus(float)`
   - `setSpeakerMixDb(float)`
   - `setSubMixDb(float)`
   - `setAutoCompensation(bool)`
   - `setElevationMode(ElevationMode)`
   - (`setPaused(bool)` already exists)

   These write to the existing `mConfig` atomics. No threading model change. The OSC listener already writes these same atomics — the setter implementations are identical to what the OSC callbacks do.

5. Fix `EngineOptions::elevationMode` type from `int` to `ElevationMode` enum.

6. Update `PUBLIC_DOCS/API.md`:
   - Document the new setter API
   - Add embedding instructions (include paths, CMake link targets)
   - Add the documented hard constraints from `api_mismatch_ledger.md`
   - Document the 64-channel limit

**Why before Stage 3:** The Qt GUI must be built against a stable, embeddable API. Hardening the API before writing the GUI avoids rework.

### Stage 3 — Python removal and Qt GUI

7. Remove all Python entry points and GUI: `runRealtime.py`, `realtimeMain.py`, `gui/`, `src/config/`, `runPipeline.py`
8. Remove all Python LUSID and offline pipeline code: `LUSID/src/`, `LUSID/tests/`, `src/analyzeADM/`, `src/packageADM/`, `src/analyzeRender.py`, `src/createRender.py`, `src/createFromLUSID.py`
9. Remove `requirements.txt` and `spatialroot/` venv
10. Build the C++ Qt GUI as the replacement for the Python GUI:
    - Links `EngineSessionCore`
    - Calls the standard `EngineSession` lifecycle
    - Uses Stage 2 setter methods for live parameter control
    - Reads `queryStatus()` for status display
    - Does not use OSC for local control

**Why this order:** Wait until `build.sh` is confirmed working (Stage 1) and the setter API is stable (Stage 2) before removing the Python build system and building the Qt GUI. Python removal and Qt GUI build can proceed in parallel once Stage 2 is complete.

---

## 11. Key Files for Refactor Reference

| Purpose | File |
|---|---|
| Engine public API header | `spatial_engine/realtimeEngine/src/EngineSession.hpp` |
| Engine runtime config and threading model | `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` |
| Engine public API documentation | `PUBLIC_DOCS/API.md` |
| Engine CMake | `spatial_engine/realtimeEngine/CMakeLists.txt` |
| Engine CLI main | `spatial_engine/realtimeEngine/src/main.cpp` |
| Audio backend | `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` |
| CULT public header | `cult_transcoder/include/cult_transcoder.hpp` |
| CULT CMake | `cult_transcoder/CMakeLists.txt` |
| API constraint ledger | `internalDocsMD/API/api_mismatch_ledger.md` |
| Offline renderer | `spatial_engine/src/renderer/SpatialRenderer.cpp` |
| Shared JSON/layout loaders | `spatial_engine/src/JSONLoader.cpp`, `LayoutLoader.cpp` |
