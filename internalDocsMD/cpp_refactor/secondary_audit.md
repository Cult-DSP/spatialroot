# Spatial Root — C++ Refactor Audit
**Date:** 2026-03-29
**Scope:** Pre-refactor architectural assessment grounded in current repo state
**Purpose:** Source-of-truth document for the C++-first refactor prompt

---

## 1. Executive Verdict

The repo is meaningfully closer to the paper's architecture than it first appears. The realtime engine (`EngineSession` and its subsystems) is a real, well-implemented C++ library with a clean typed API. CULT is a working C++ CLI (standalone submodule). The JSON-based LUSID contract is honored end-to-end in C++. What remains Python is launch infrastructure and GUI glue, not core audio logic.

The main structural gap is that **the three surfaces (library API, CLI, GUI) are not cleanly separated at the process boundary**: the GUI launches Python which launches the binary, the build system lives in Python, and there is no top-level CMake or shell script that ties the stack together. The C++ library API (`EngineSession`) is real but not yet usable by an embedding host without building from the full source tree.

The stack is not far from the paper's target. The build and launch infrastructure needs to move out of Python before the architecture is legible.

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

CULT has a public API header (`cult_transcoder.hpp` with `cult::transcode()`) but **the current CMake builds only the CLI binary, not a separately installable library target**. Spatial Root currently reaches CULT exclusively via subprocess — first from Python (`runRealtime.py`) and ultimately through the Python build system.

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
- **Runtime control path:** GUI → python-osc UDP → C++ `al::ParameterServer` on port 9009
- **Launch path:** QProcess → `runRealtime.py` subprocess → `spatialroot_realtime` binary

The GUI is three process-hops from the audio thread. Its OSC control surface uses `pythonosc.udp_client` (python-osc). Post-refactor this will be replaced, with runtime control moving to AlloLib OSC directly (see Section 6, item 4).

### 2.6 Python LUSID Library — `LUSID/src/` (to be removed)

| File | Role | Status |
|---|---|---|
| `scene.py` | Python dataclasses for LUSID scene nodes | Not used by any C++ code |
| `xml_etree_parser.py` | Python ADM XML → LUSID (stdlib only) | Superseded by CULT transcoder |
| `parser.py` | Python LUSID JSON loader | Only used by deprecated offline pipeline |

This library is a parallel implementation to the C++ code. CULT writes LUSID JSON in C++. `JSONLoader.cpp` reads LUSID JSON in C++. The Python LUSID library exists only to support the deprecated offline pipeline and its 106 Python unit tests. Once that pipeline is removed, this library has no purpose.

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

For the realtime path, only `PySide6` and `python-osc` are actually needed — and post-refactor, python-osc is being replaced by AlloLib OSC. If the GUI is also removed, the Python runtime dependency drops to zero for the core engine.

---

## 3. Paper Alignment Audit

### 3.1 CULT as embeddable C++ transcoder with library API and CLI

**Partially aligned.**

The CLI (`cult-transcoder`) is real and functional. The public API header (`cult_transcoder.hpp`) defines `cult::transcode()`. However, CMake does not produce a separate static or shared library target — only the CLI binary. Spatial Root reaches CULT via subprocess, not via the C++ API. Adding a library target to CULT's CMake is documented as future work (Section 8) and is outside this refactor's scope.

### 3.2 LUSID as canonical scene and package structure

**Aligned for the realtime path; partially for the overall stack.**

The JSON format is genuinely canonical: CULT writes it, `JSONLoader.cpp` reads it, the engine runs from it. The gap is that there is no standalone C++ LUSID reader library — `JSONLoader.cpp` is the only C++ reader and it lives inside the engine's source tree. The relationship between `JSONLoader.cpp` and a future CULT library reader is documented as future work (Section 8).

### 3.3 Spatial Root as C++ runtime with library API, CLI, and optional GUI

**Mostly aligned.**

`EngineSession` is a real C++ static library (`EngineSessionCore`) with a clean typed API documented in `API.md`. The CLI binary (`spatialroot_realtime`) works. The GUI works but goes through Python.

Gaps:
- No CMake `install()` target for `EngineSessionCore` — an embedding host cannot find it without building from source
- `EngineOptions::elevationMode` is `int` in the public API struct, not the `ElevationMode` enum from `RealtimeTypes.hpp` (noted in `API.md` as a known issue)
- No per-parameter runtime setters callable while running from C++ — the only in-process runtime control path for a C++ host is OSC. `configureRuntime()` is only available before `start()`

### 3.4 Consistent runtime control surfaces via OSC

**Partially aligned.**

`al::ParameterServer` provides a working OSC control surface. CLI args set initial values. The GUI currently sends OSC via Python (python-osc). Post-refactor, the GUI control path will use AlloLib OSC directly, which aligns better with the paper's claim. Currently the three surfaces (CLI, runtime OSC, C++ API) have different capabilities: CLI sets initial state, OSC updates runtime state, C++ API can only set initial state via `configureRuntime()`.

### 3.5 Distinct but interoperable layers

**Partially aligned.**

CULT and Spatial Root are distinct in the codebase. LUSID is a format, not a library. The layers are interoperable at the file format level. Interoperability at the API level (C++ library linkage) is future work.

---

## 4. Python Dependency Classification

### Keep for now
- None — all meaningful Python components are on a removal path.

### Migrate soon (part of this refactor)
- `src/config/configCPP*.py` — replace with `build.sh`
- `runRealtime.py` — replace with shell script or direct binary invocation
- `realtimeMain.py` — remove with GUI
- `gui/realtimeGUI/` — remove; runtime control path moves to AlloLib OSC (see Section 6, item 4)

### Remove with offline pipeline
- `LUSID/src/` and `LUSID/tests/` — parallel C++ implementation exists
- `src/analyzeADM/`, `src/packageADM/`, `src/analyzeRender.py`
- `runPipeline.py` — already deprecated

### Reduce / split
- `requirements.txt` — currently includes jupyter, numpy, matplotlib, pandas, soundfile, gdown, PySide6, python-osc. Post-refactor the runtime dependency drops significantly.

---

## 5. Public API / CLI / GUI Reality Check

### C++ Library API (`EngineSession`)

Exists and is real. `EngineSession.hpp` defines all types. `EngineSessionCore` is a CMake static library target. `API.md` documents it with a working quick-start example.

**What is missing:**
- No CMake `install()` target for `EngineSessionCore` or its headers
- No pkg-config or find-package support
- The API header depends on AlloLib headers at include time — an embedding host must also locate AlloLib's include path
- No per-parameter setters callable while running from C++ (`setGain(float)`, `setFocus(float)` — these are planned for a future API iteration)
- `EngineOptions::elevationMode` should be the `ElevationMode` enum, not `int`

**Documented hard constraints** (from `api_mismatch_ledger.md`) that will not change in this refactor:
1. Staged setup is non-negotiable: `configureEngine()` → `loadScene()` → `applyLayout()` → `configureRuntime()` → `start()`
2. No restartable transport: only `setPaused(bool)` is supported
3. OSC ownership: `mParamServer` is internal to `EngineSession` and cannot be shared with the host
4. Shutdown sequence: `mParamServer->stopServer()` → `mBackend->shutdown()` → `mStreaming->shutdown()` — violating this causes deadlocks on macOS CoreAudio

**Current limitation:** The engine's active-channel bitmask tracking uses `uint64_t`, which implicitly caps the engine at 64 output channels. The AlloSphere (54.1 channels) is within range. This limit is not documented in `API.md` and should be added.

### CLI Binary (`spatialroot_realtime`)

Works correctly. Well-documented `--help` output. `--list-devices` is useful for hardware installations.

**The primary documentation problem:** `README.md` documents `realtimeMain.py` (Python wrapper) as the primary CLI entry point, with different flag names than the actual C++ binary. The C++ binary's flags should be primary documentation.

### GUI

Works (Phase 10 complete) but is Python/PySide6. Subprocess chain: GUI → QProcess → `runRealtime.py` → `spatialroot_realtime`. OSC via python-osc. Both are on the removal path.

---

## 6. Resolved Questions and Decisions

**1. Offline rendering after refactor**
`runPipeline.py` and all Python offline pipeline orchestration will be removed. `SpatialRenderer.cpp` and the `spatialroot_spatial_render` binary are retained. How the offline renderer is invoked post-refactor is TBD — it will not involve Python.

**2. `runRealtime.py` vs `realtimeMain.py`**
`runRealtime.py` = CLI launcher (no GUI). `realtimeMain.py` = GUI launcher. Both are Python entry points and both will be removed as part of this refactor.

**3. CULT scope**
CULT is a standalone submodule used in other projects outside this repo. **CULT's source is not to be modified in the scope of this refactor.** Future improvements to CULT (CMake library target, install target, tighter integration with the engine) are documented in Section 8.

**4. python-osc replacement**
python-osc (`pythonosc.udp_client`) is the current GUI runtime control path. This will pivot to using AlloLib OSC directly. Exact implementation needs investigation as part of the refactor — see Section 7, item 3.

**5. `EngineSessionCore` API stability**
`EngineSessionCore` is intended to be a stable public API. This means: document constraints explicitly, avoid breaking the typed struct interface between releases, and add a CMake `install()` target so embedding hosts can use it.

**6. JSONLoader vs CULT boundary**
`JSONLoader.cpp` (inside the engine) reads LUSID JSON. CULT writes LUSID JSON. These are separate C++ implementations of the same format. Whether `JSONLoader.cpp` should eventually be replaced by calling a CULT library reader is future work — outside this refactor's scope.

**7. 64-channel limit**
The `uint64_t` bitmask in `EngineStatus` implicitly caps the engine at 64 output channels. This is a current limitation that should be documented in `API.md` but will not be addressed in this refactor.

---

## 7. Open Questions Before Implementation

**1. AlloLib OSC as GUI control path: what does this look like concretely?**
Currently: Python GUI → `pythonosc.udp_client` → UDP → `al::ParameterServer`.
Target: Replace the Python OSC sender with... what? Options:
- AlloLib provides `al::ParameterGUI` (an ImGui-based parameter inspector) that talks to `al::ParameterServer` directly. This would make the GUI an AlloLib application.
- A minimal C++ OSC client (using AlloLib's oscpack wrapper) embedded in a thin C++ GUI launcher.
- Keep the Qt GUI but use a C++ Qt OSC client instead of python-osc (Qt has OSC support or a C++ oscpack wrapper can be used).

This needs investigation before the GUI removal/replacement is scoped. The OSC protocol surface itself does not change — only the sender implementation.

**2. What is the post-refactor entry point for a user running the engine?**
Currently: `python runRealtime.py <args>` or `python realtimeMain.py` (GUI).
Post-refactor options:
- `./spatialroot_realtime --scene ... --layout ... --adm ...` (direct binary, requires CULT preprocessing to be done separately)
- `./run.sh <args>` (shell script that invokes cult-transcoder then spatialroot_realtime)
- A new C++ launcher that calls cult::transcode() and EngineSession in the same process (requires CULT library target — future work)

**3. How does the offline renderer get invoked post-refactor?**
`SpatialRenderer.cpp` and `spatialroot_spatial_render` will be retained. The Python orchestration (`runPipeline.py`, `src/createRender.py`) will be removed. A shell script or a new C++ CLI main for the offline path is needed. This is TBD.

---

## 8. Future Work (Outside This Refactor)

These items are documented here so they are not lost, but should not be implemented during the current refactor pass.

**CULT CMake library target**
Add `add_library(cult_transcoder_lib STATIC ...)` alongside `add_executable(cult-transcoder ...)` in `cult_transcoder/CMakeLists.txt`. This would allow `EngineSession` to call `cult::transcode()` directly in-process rather than via subprocess, eliminating the need for a shell script intermediary on the ADM path.

**CULT install target**
Add `install(TARGETS cult-transcoder)` and `install(TARGETS cult_transcoder_lib)` with headers. Needed for host applications that want to link CULT as a library.

**JSONLoader vs CULT reader**
`JSONLoader.cpp` is a LUSID JSON reader embedded in the engine. A future refactor could replace it with a call to a CULT library reader, ensuring a single canonical C++ implementation of LUSID parsing. This is premature until CULT has a library target.

**Per-parameter runtime setters on `EngineSession`**
Add `setMasterGain(float)`, `setFocus(float)`, `setSpeakerMixDb(float)`, `setSubMixDb(float)` methods that write config atomics directly. This would make the C++ library API consistent with the paper's claim of runtime render control without requiring OSC. Threading model is unchanged — these write atomics exactly as the OSC listener does.

**`EngineOptions::elevationMode` type**
Change from `int` to `ElevationMode` enum (already defined in `RealtimeTypes.hpp`). Small breaking API change — do it before external adoption.

**64-channel limit**
The `uint64_t` bitmask in `EngineState` caps the engine at 64 output channels. For arrays larger than 64 channels, the bitmask tracking system would need to be redesigned (e.g., `std::bitset<128>` or a different diagnostic mechanism).

**MPEG-H and IAMF support in CULT**
MPEG-H translation in CULT is incomplete. IAMF support is planned. These are CULT-level concerns and tracked in `cult_transcoder/internalDocsMD/`.

**VBAP and LBAP in the realtime engine**
The realtime engine currently uses DBAP only. VBAP and LBAP are implemented in the offline renderer (`SpatialRenderer.cpp`) and in AlloLib but not yet wired into the realtime engine's `Spatializer.hpp`.

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
| `requirements.txt` | Reduce or remove depending on remaining Python needs |
| `spatialroot/` venv | Remove or drastically reduce |
| `README.md` | Rewrite to document C++ binary as primary interface |
| `PUBLIC_DOCS/API.md` | Add: documented constraints, 64-ch limit, embedding instructions |
| Realtime engine CMake | Add CMake `install()` target for `EngineSessionCore` |
| `engine.sh` | Keep or replace with `build.sh` that covers all components |

### Stays unchanged

| Component | Reason |
|---|---|
| `spatial_engine/realtimeEngine/src/` (all C++) | Core engine — no changes needed |
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

### Stage 1 — Build infrastructure (no code changes)

1. Write `build.sh` that runs cmake on all three C++ components in dependency order:
   - `cult_transcoder/` (CULT CLI binary)
   - `spatial_engine/realtimeEngine/` (realtime engine)
   - `spatial_engine/spatialRender/` (offline renderer)

   Verify it produces the same binaries as the Python build path. This decouples the build from Python entirely.

2. Add CMake `install()` target for `EngineSessionCore` and its public headers to `spatial_engine/realtimeEngine/CMakeLists.txt`.

3. Update `README.md`:
   - Document `spatialroot_realtime` directly as the CLI surface with its actual flags
   - Remove references to `realtimeMain.py`, `runRealtime.py`, `runPipeline.py`
   - Remove deprecated `spatialroot_adm_extract` references from setup
   - Fix OSC port: code says 9009, README says 12345 — code is authoritative
   - Document the two-step ADM workflow: `cult-transcoder transcode ...` then `spatialroot_realtime ...`

**Why this order:** No code changes, no breakage risk. Makes the actual architecture visible before modifying anything.

### Stage 2 — Python removal

4. Remove all Python entry points and GUI: `runRealtime.py`, `realtimeMain.py`, `gui/`, `src/config/`, `runPipeline.py`
5. Remove all Python LUSID and offline pipeline code: `LUSID/src/`, `LUSID/tests/`, `src/analyzeADM/`, `src/packageADM/`, `src/analyzeRender.py`, `src/createRender.py`, `src/createFromLUSID.py`
6. Reduce or remove `requirements.txt` and `spatialroot/` venv
7. Investigate and implement AlloLib OSC as the runtime control path for whatever replaces the GUI (see Section 7, item 1)

**Why this order:** Wait until `build.sh` is confirmed working (Stage 1) before removing the Python build system. Removal of LUSID Python library is safe once `runPipeline.py` is confirmed removed.

### Stage 3 — API surface hardening

8. Update `PUBLIC_DOCS/API.md`:
   - Add constraints from `api_mismatch_ledger.md` as explicit documented section
   - Document the 64-channel limit
   - Document how to embed `EngineSessionCore` (include paths, CMake link targets)
   - Document the two-step CLI workflow (cult-transcoder → spatialroot_realtime)
9. Decide and document the post-refactor offline renderer invocation path (shell script or new C++ CLI main)

---

## 11. Key Files for Refactor Reference

| Purpose | File |
|---|---|
| Engine public API header | `spatial_engine/realtimeEngine/src/EngineSession.hpp` |
| Engine public API documentation | `PUBLIC_DOCS/API.md` |
| Engine CMake | `spatial_engine/realtimeEngine/CMakeLists.txt` |
| Engine CLI main | `spatial_engine/realtimeEngine/src/main.cpp` |
| Shared types + threading model | `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` |
| Audio backend | `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` |
| CULT public header | `cult_transcoder/include/cult_transcoder.hpp` |
| CULT CMake | `cult_transcoder/CMakeLists.txt` |
| API constraint ledger | `internalDocsMD/API/api_mismatch_ledger.md` |
| Offline renderer | `spatial_engine/src/renderer/SpatialRenderer.cpp` |
| Shared JSON/layout loaders | `spatial_engine/src/JSONLoader.cpp`, `LayoutLoader.cpp` |
