# EngineSession API — Internal Reference

**Last Updated:** March 2026  
**Source files:** `spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp`, `PUBLIC_DOCS/API.md`

---

## Contract

> `api_internal_contract.md` — Canonical post-refactor source of truth.

### Public Structs

| Struct | Purpose |
|---|---|
| `EngineConfig` | Core system settings (sample rate, buffer size, base paths) |
| `SceneConfig` | Audio scene definition (ADM/LUSID payload paths) |
| `LayoutConfig` | Speaker layout and routing parameters |
| `RuntimeConfig` | OSC ports, UI bindings, telemetry settings |
| `EngineStatus` | Side-effect-free snapshot of current state (playhead, CPU load, active voices) |
| `DiagnosticMessage` | Structured warning/error payload for async events |

> **Note:** Core structs (`EngineOptions`, `SceneInput`, etc.) are deliberately outside the `spatial::` namespace to avoid polluting public interfaces with internal legacy types — they are global structs.

### Lifecycle & Public Methods

The engine enforces a strict, linear initialization sequence:

1. `configureEngine(const EngineConfig&)` — allocates base resources
2. `loadScene(const SceneConfig&)` — parses scene data, prepares object tracks
3. `applyLayout(const LayoutConfig&)` — configures rendering and spatial mapping
4. `configureRuntime(const RuntimeConfig&)` — binds OSC (Pimpl `OscParams`) and telemetry
5. `start()` — ignites audio backend, begins processing

**Runtime Control (after `start()`):**

| Method | Description |
|---|---|
| `setPaused(bool)` | The only supported transport control. Stop/seek are deferred/unsupported. |
| `update()` | Main-thread tick. **Must be called regularly** (e.g. 50ms timer). Handles deferred work: `computeFocusCompensation()`. |
| `queryStatus() -> EngineStatus` | Polls current metrics without mutating state. |
| `consumeDiagnostics() -> vector<DiagnosticMessage>` | Empties internal diagnostic queue. |
| `shutdown()` | Triggers rigid teardown sequence. Terminal — construct new `EngineSession` to restart. |

**Phase 6 runtime setters (direct C++ control, no OSC required):**

| Method | Writes |
|---|---|
| `setMasterGain(float)` | `mConfig.masterGain` (linear 0.0–1.0) |
| `setDbapFocus(float)` | `mConfig.dbapFocus` + sets `mPendingAutoComp` if auto-comp enabled |
| `setSpeakerMixDb(float)` | `mConfig.loudspeakerMix` (dB→linear) |
| `setSubMixDb(float)` | `mConfig.subMix` (dB→linear) |
| `setAutoCompensation(bool)` | `mConfig.focusAutoCompensation` + sets `mPendingAutoComp` if enabling |
| `setElevationMode(ElevationMode)` | `mConfig.elevationMode` |

All writes use `std::memory_order_relaxed`. Safe to call after `start()` and before `shutdown()`.

### Error Model

- **Synchronous:** Lifecycle methods return `bool`. On failure: `getLastError() -> std::string`.
- **Async/Runtime:** Non-fatal errors routed to queue via `consumeDiagnostics()`.

### Threading Constraints

- **Main thread:** All lifecycle methods (`configureEngine` through `start`, and `shutdown`).
- **`update()`:** Must be called from a main-thread UI/event loop.
- **Audio thread:** Managed internally. Host must not block backend threads. Inter-thread communication is strictly wait-free internally.

### Shutdown Order

Violating this sequence **will** cause deadlocks on macOS CoreAudio and ASIO:

1. `mParamServer->stopServer()` — kill network ingestion first
2. `mBackend->shutdown()` — halt audio callback gracefully
3. `mStreaming->shutdown()` — release disk I/O and memory buffers

### Explicit Exclusions

- **Restartable Stop/Seek:** Deferred — destroy and recreate `EngineSession` to reset or seek.
- **Granular CLI Features:** Debug toggles remain in `main.cpp` CLI parsing, not exposed in `EngineSession`.

---

## Design Rationale

> `api_derived_design.md` — Explains the "why" behind structural choices.

### Pimpl-style OSC and Parameter Lifetime

AlloLib parameters bind to internal memory topologies — exposing them directly risks lifetime violations. `EngineSession` uses a Pimpl-style `OscParams` wrapper. `mParamServer` is entirely owned and destroyed by the session. `configureRuntime()` maps external `RuntimeConfig` values into AlloLib specifics securely.

### Separation of Status and Diagnostics

- `queryStatus()` — lock-free immediate snapshot (playhead, CPU)
- `consumeDiagnostics()` — queued warnings (object dropouts, matrix clipping); prevents the host from missing transient errors between polling intervals

### Main-Thread Tick (`update()`)

Audio thread strictness forbids heavy configuration matrix recalculation during a callback. `computeFocusCompensation()` and similar structural mutators are deferred to the main thread via the `update()` tick.

---

## Hard Constraints

> `api_mismatch_ledger.md` — Do not attempt to refactor around these without a fundamental engine rewrite.

1. **Staged Setup is Non-Negotiable:** Object counts from `loadScene` dictate memory allocations required before `applyLayout` can construct the spatial matrix. The 5-stage setup must remain.
2. **Restartable Stop/Seek is Unsafe:** Ring buffers and ADM block-streamers hold state that cannot be flushed atomically. Transport is strictly `setPaused(bool)`.
3. **OSC Ownership:** `mParamServer` cannot be shared with the host. Must be spun up and torn down inside `EngineSession` to guarantee valid AlloLib parameter scoping.
4. **Shutdown Sequence:** `mParamServer->stopServer()` → `mBackend->shutdown()` → `mStreaming->shutdown()` — any other order **will** deadlock on CoreAudio/ASIO.

---

## Validation & Known Gotchas

> `new_context.md` — Phase complete notes and structural gotchas.

**What was validated:** `EngineSessionCore` extracted into a distinct linkable CMake library (`EngineSessionCore` static target). Type definitions restructured to prevent `AlloLib`/threading leakage. `internal_validation_runner.cpp` smoke test proved robust execution and clean teardown.

**Layout vs. device channel count mismatch:** Mismatches between layout channel counts and hardware channel counts trigger a fatal fast-fail (e.g., attempting a 7-channel layout on a 2-channel audio device). Public docs must describe `EngineOptions` device fallback behavior and layout configuration dependency.

**`uint64_t` bitmask channel cap:** `EngineStatus` uses `uint64_t` bitmasks, implicitly capping the engine at 64 output channels.

**Key file pointers:**
- Public API entry point: `spatial_engine/realtimeEngine/src/EngineSession.hpp`
- Core library CMake target: `spatial_engine/realtimeEngine/CMakeLists.txt` (`EngineSessionCore`)
- Working reference: `spatial_engine/realtimeEngine/src/internal_validation_runner.cpp`
- Test assets: `sourceData/lusid_package/scene.lusid.json`, `spatial_engine/speaker_layouts/stereo.json`
