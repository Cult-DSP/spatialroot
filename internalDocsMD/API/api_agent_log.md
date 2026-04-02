# API Agent History & Rationale Log
**Status:** Historical Log

## Phase 2 -> Phase 3 Transition (March 2026)
**Agent Action:** API Stabilization Pass
**Goal:** Freeze the implemented `EngineSession` contract and align documentation.
**Decisions:**
1. Created `api_internal_contract.md` to stop future agents from hallucinating features based on aspirational Phase 1 texts.
2. Formalized `setPaused(bool)` as the sole transport control. Abandoned `stop()` and `seek()` due to state-corruption risks identified in the mismatch ledger.
3. Enforced `update()` as a required host-side main-thread tick. This was a critical compromise to get `computeFocusCompensation()` safely off the audio thread without requiring a complex internal worker pool.

*(Below this line: Historical Phase 1 & 2 logs...)*

# API Agent Implementation Log

## Session Architecture Extraction

**Goal:** Extract the heavy orchestration logic from `spatial_engine/realtimeEngine/src/main.cpp` into a resilient runtime public API composed of `EngineSession.hpp` and `EngineSession.cpp`.

### Changes Implemented:

1. **Created `EngineSession.hpp` and `EngineSession.cpp`**:
   - Encapsulated core engine state (`RealtimeConfig`, `EngineState`) and all agent instances (`Streaming`, `Pose`, `Spatializer`, `RealtimeBackend`, `OutputRemap`, `al::ParameterServer`).
   - Exposed a clean lifecycle API: `configureEngine()`, `loadScene()`, `applyLayout()`, `configureRuntime()`, `start()`, `shutdown()`.
   - Exposed runtime update and polling mechanisms: `update()`, `queryStatus()`, and `consumeDiagnostics()`.
   - Grouped status and diagnostic latches into decoupled structs (`EngineStatus`, `DiagnosticEvents`) to allow thread-safe, lock-free queries from the main UI polling loop.

2. **Refactored `main.cpp`**:
   - Removed all heavy instantiation and orchestration of audio agents (Streaming, Pose, Spatializer, Backend).
   - Reduced `main.cpp` to strictly handle argument parsing, setting configuration via the `session.config()` reference, and running the `while (!shouldExit)` polling loop.
   - Replaced raw UI block parsing with the use of the new `EngineStatus` and `DiagnosticEvents` APIs.
   - Added `#include <iomanip>` to support `std::setprecision` and `std::fixed` for clean UI diagnostic rendering.

3. **Resolved `RealtimeConfig` Compilation Issue**:
   - `std::atomic` fields inside `RealtimeConfig` (e.g., `dbapFocus`) deleted implicit copy constructors, causing standard passing in `EngineSession` constructors to fail.
   - Solved by making `EngineSession` default-constructible and having `main.cpp` directly populate the config via `session.config()` rather than instantiating the configuration object and attempting to pass or copy it in.

4. **Updated Build Configuration**:
   - Modified `spatial_engine/realtimeEngine/CMakeLists.txt` to include `src/EngineSession.cpp` in the `add_executable` list.
   - Successfully compiled the target `spatialroot_realtime` and verified the build using the `sysctl -n hw.ncpu` concurrent job scaling.

## Phase 2: Struct Refactoring and Immutability (`api_derived_design.md` & `api_mismatch_ledger.md`)

**Date:** March 29, 2026

**Objective:**
Evolve the Phase 1 `EngineSession` API past `void` arguments, resolving "Mismatch 5: Error Handlers" and replacing mutable getters. Instead of leaking internal mutable atomic state (`session.config()`), use strict domain-driven structs: `EngineOptions`, `SceneInput`, `LayoutInput`, and `RuntimeParams`.

**Files Modified:**

- `EngineSession.hpp`
- `EngineSession.cpp`
- `main.cpp`
- `api_mismatch_ledger.md`

**Key Changes:**

1. **Struct Injection:** Defined `EngineOptions`, `SceneInput`, `LayoutInput`, and `RuntimeParams` in `EngineSession.hpp`.
2. **Signature Update:** Modified `EngineSession` initialization methods:
   - `configureEngine(const EngineOptions& opts)`
   - `loadScene(const SceneInput& sceneIn)`
   - `applyLayout(const LayoutInput& layoutIn)`
   - `configureRuntime(const RuntimeParams& params)`
3. **Error Handling Implementation:** Added `std::string getLastError() const` and `void setLastError(...)` for explicit string-based error handling instead of writing to stderr. Ensured all fatal failures, including inside `start()`, route through this mechanism.
4. **`main.cpp` Mapping:** Removed all `session.config()` direct mutations. Documented inline how CLI arguments are grouped into struct blocks and passed collectively through the initialization chain.
5. **State Safety:** Prevented leaking `std::atomic` references or implicit copy-constructors over the module boundary.
6. **Transport Paradigm:** Replaced generic pausing ideas with strict `setPaused(bool)`.
7. **Protected AlloLib Parameters (Mismatch 6):** Used an opaque pointer (Pimpl pattern) `struct OscParams;` in `EngineSession.hpp` with explicit declaration in `.cpp` to securely encapsulate `al::Parameter` objects and satisfy both API boundary hygiene and the background OSC threads requirement for pointer stability. Added this explicit workaround directly to `api_mismatch_ledger.md`.
