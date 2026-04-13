# Repository Auditing — Internal Reference

**Last Updated:** February 22–23, 2026

---

## Repository Cleanup Audit

> `REPO_CLEANUP_AUDIT.md` — Phase 1 complete, February 23, 2026.

### Phase 1: Completed Removals ✅

Files removed February 23, 2026:
1. `LUSID/src/old_XML_parse/` — archived XML parser
2. `LUSID/tests/old_XML_parse/` — archived XML parser tests
3. `src/analyzeADM/old_XML_parse/` — archived XML parser
4. `src/packageADM/old_schema/` — old schema files
5. `src/packageADM/createRenderInfo.py` — deprecated wrapper
6. `spatial_engine/src/renderer/rendererUtils-TODO/` — empty directory
7. `src/adm_extract/build/` — build artifacts

All 79 LUSID tests pass post-removal.

### Items Marked to Leave

Per human review annotations in the audit:
- `processedData/` — all contents (render outputs, scene data, debug artifacts)
- `sourceData/` — 19 GB ADM WAV test files
- `quickCommands.txt` — useful dev command examples
- `LUSID/tests/fixtures/sample_scene_v0.5.json` — test fixtures
- `internalDocsMD/` — all internal docs (superseded by current consolidation)

### Repository Size Context

| Item | Size |
|---|---|
| `sourceData/` | 19 GB (ADM WAV test files) |
| `processedData/` | 11 GB (rendered multichannel audio) |
| Python venv (removed Phase 6) | was 1.6 GB |
| Source code + submodules | ~122 MB |

Note: Python virtual environment was removed in Phase 6 (2026-03-31) as part of the C++ refactor. All Python build tooling and GUI code has been removed.

### Phase 2: Remaining Candidates (review needed)

- `.DS_Store` files throughout repo — `find . -name ".DS_Store" -delete`
- Stale submodule entries: `thirdparty/libbw64` and `thirdparty/libadm` are in `.gitmodules` at the spatialroot level but not used by any active `CMakeLists.txt` (`spatialroot_adm_extract` archived in Phase 3). Not harmful but confusing.

---

## AlloLib Dependency Audit

> `allolib-audit.md` — February 22, 2026. Pre-Track B cleanup. See also AGENTS.md §AlloLib Audit & Lightweighting.

### Weight Analysis

| Item | Size | Notes |
|---|---|---|
| `thirdparty/allolib/` working tree | 38 MB | All source files |
| `.git/modules/thirdparty/allolib` | **511 MB** | Full history — 1,897 commits |
| `external/Gamma` | 2.3 MB | DSP library — **linked by spatialroot** |
| `external/rtaudio` | 1.3 MB | Audio I/O — needed for realtime |
| `external/imgui` | 5.1 MB | Dear ImGui — not used by spatialroot directly |
| `external/glfw` | 4.5 MB | GLFW window — now used by GUI |
| `external/stb` | 2.0 MB | Image loading — not used |
| `external/dr_libs` | 744 KB | Audio decoding — not used (spatialroot uses libsndfile) |

**Primary problem:** `.git/modules` history (511 MB) dwarfs working tree (38 MB). `--depth 1` shallow clone reduces to ~a few MB.

**Status: Implemented.** `.gitmodules` has `shallow = true` for `thirdparty/allolib`. `init.sh` uses `--depth 1`. `scripts/shallow-submodules.sh` handles existing deep clones.

### AlloLib Modules — Usage Classification

**Required today (KEEP):**

| Module | What's used |
|---|---|
| `sound/` | `al_Vbap.hpp`, `al_Dbap.hpp`, `al_Lbap.hpp`, `al_Spatializer.hpp`, `al_Speaker.hpp` + `.cpp` files |
| `math/` | `al_Vec.hpp` + transitive math headers (Mat, Quat, Constants, Functions, Spherical) |
| `spatial/` | `al_Pose.hpp` (pulled by `al_Spatializer.hpp`) |
| `io/AudioIOData` | `al_AudioIOData.hpp` (buffer/channel descriptor) |
| `external/Gamma` | DSP primitives, linked by `spatialroot_spatial_render` |
| `external/json` | nlohmann/json — used by AlloLib CMake and spatialroot JSONLoader |

**Needed for realtime path (already in use):**
- `io/al_AudioIO.hpp` + `al_AudioIO.cpp` — live audio device
- `external/rtaudio/` — cross-platform audio device backend
- `protocol/al_OSC.hpp` — OSC parameter server
- `external/oscpack/` — OSC protocol impl
- `app/` + `system/al_PeriodicThread` — real-time scheduling
- `ui/al_Parameter.hpp` etc. — AlloLib parameter system for OSC

**Safe to exclude from working tree (unused, no near-term path):**
- `graphics/`, `sphere/`, `ui/` source — no OpenGL/sphere rendering
- `external/glfw/` — 4.5 MB (window creation; now needed for ImGui GUI)
- `external/imgui/` — 5.1 MB (AlloLib's copy; spatialroot uses its own `thirdparty/imgui`)
- `external/stb/` — 2.0 MB (image loading)
- `external/dr_libs/` — 744 KB (audio decoding; spatialroot uses libsndfile)
- `external/glad/` — OpenGL loader
- `external/serial/` — serial port

### Lightweighting Plan

| Tactic | Saving | Risk | Status |
|---|---|---|---|
| `--depth 1` shallow clone | ~510 MB (git history) | None | **✅ Done** — `init.sh` + `.gitmodules` |
| Sparse checkout working tree | ~14 MB (source files) | Medium (CMake fragility) | **Opt-in only** — `scripts/sparse-allolib.sh` |
| Minimal fork `Cult-DSP/allolib-sono` | ~20 MB + avoids graphics deps | Low (one-time effort) | **Future option** if above insufficient |

**To apply to an existing deep clone:** `./scripts/shallow-submodules.sh`  
**Opt-in sparse tree:** `./scripts/sparse-allolib.sh` — read warnings in script before using.
