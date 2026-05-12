# EngineSession API — Internal Reference

**Last Updated:** May 2026  
**Source files:** `source/spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp`, `PUBLIC_DOCS/API.md`

---

## Contract

> `api_internal_contract.md` — Canonical post-refactor source of truth.

### Public Structs

| Struct             | Purpose                                                                        |
| ------------------ | ------------------------------------------------------------------------------ |
| `EngineOptions`    | Core system settings (sample rate, buffer size, output device, OSC, elevation) |
| `SceneInput`       | Audio scene definition (ADM/LUSID payload paths)                               |
| `LayoutInput`      | Speaker layout and routing parameters                                          |
| `RuntimeParams`    | Runtime DSP parameters (gain, focus, mix trims)                                |
| `EngineStatus`     | Side-effect-free snapshot of current state (playhead, CPU load, active voices, backend/sample-rate status) |
| `DiagnosticEvents` | Structured per-tick relocation/cluster events                                  |

> **Note:** Core structs are deliberately outside the `spatial::` namespace to avoid polluting public interfaces with internal legacy types — they are global structs.

### Lifecycle & Public Methods

The engine enforces a strict, linear initialization sequence:

1. `configureEngine(const EngineOptions&)` — allocates base resources
2. `loadScene(const SceneInput&)` — parses scene data, prepares object tracks
3. `applyLayout(const LayoutInput&)` — configures rendering and spatial mapping
4. `configureRuntime(const RuntimeParams&)` — applies runtime DSP params (safe before/after start)
5. `start()` — ignites audio backend, begins processing

**Runtime Control (after `start()`):**

| Method                                        | Description                                                                                                                                    |
| --------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- |
| `setPaused(bool)`                             | The only supported transport control. Stop/seek are deferred/unsupported.                                                                      |
| `update()`                                    | Main-thread tick. **Should be called regularly** from the host loop. It currently performs no deferred work and is retained for API stability. |
| `queryStatus() -> EngineStatus`               | Polls current metrics without mutating state.                                                                                                  |
| `consumeDiagnostics() -> DiagnosticEvents`    | Empties internal diagnostic queue.                                                                                                             |
| `shutdown()`                                  | Triggers rigid teardown sequence. Terminal — construct new `EngineSession` to restart.                                                         |
| `getRuntimeParams()` / `resetRuntimeParams()` | Read and reset runtime params (dB-based).                                                                                                      |
| `getFailureDiagnostics()`                     | Structured diagnostic block for the most recent startup-stage failure.                                                                         |

**Phase 6 runtime setters (direct C++ control, no OSC required):**

| Method                            | Writes                                                             |
| --------------------------------- | ------------------------------------------------------------------ |
| `setMasterGainDb(float)`          | `mConfig.masterGain` (dB → linear; range -60–+12 dB, 0 dB = unity) |
| `setDbapFocus(float)`             | `mConfig.dbapFocus` (clamped to minimum `0.1f`)                    |
| `setSpeakerMixDb(float)`          | `mConfig.loudspeakerMix` (dB→linear)                               |
| `setSubMixDb(float)`              | `mConfig.subMix` (dB→linear)                                       |
| `setElevationMode(ElevationMode)` | `mConfig.elevationMode`                                            |

All writes use `std::memory_order_relaxed`. Safe to call after `start()` and before `shutdown()`.

### Error Model

- **Synchronous:** Lifecycle methods return `bool`. On failure: `getLastError() -> std::string`.
- **Async/Runtime:** Non-fatal errors routed to queue via `consumeDiagnostics()`.

### Backend/API and Sample-Rate Status

`queryStatus()` is the canonical read-only path for GUI and host-facing realtime audio status. `EngineStatus` now includes a minimal backend/audio snapshot in addition to transport and render metrics:

- `audioBackendLabel`
  - best-effort backend/API label from the running backend when available
  - examples: `RtAudio / CoreAudio`, `RtAudio / JACK`, `RtAudio / WASAPI`
  - if the active RtAudio API cannot be proven, the status reports `RtAudio API unknown` rather than guessing
- `requestedSampleRate`
  - the engine request, currently expected to remain `48000`
- `outputDeviceName`
  - selected output device name, if known
- `outputDevicePreferredSampleRate`
  - selected device preferred/default rate, if known from scan/open metadata
- `outputDevicePreferredSampleRateKnown`
  - distinguishes a real preferred/default rate from `unknown`
- `effectiveStreamSampleRate`
  - actual running backend stream rate, if available after backend open/start
- `effectiveStreamSampleRateKnown`
  - distinguishes a confirmed running rate from `unknown`

These fields are intentionally read-only. They do not add new backend-selection or sample-rate-selection controls.

**Important policy:** Spatial Root still requires `48000 Hz`. Preferred/default device rate is metadata only and must not be treated as proof of the actual runtime stream rate. If the effective running stream rate is known and differs from `48000`, startup now fails through the existing error path.

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

AlloLib parameters bind to internal memory topologies — exposing them directly risks lifetime violations. `EngineSession` uses a Pimpl-style `OscParams` wrapper. `mParamServer` is entirely owned and destroyed by the session. `configureRuntime()` maps external `RuntimeParams` values into AlloLib specifics securely.

### Separation of Status and Diagnostics

- `queryStatus()` — lock-free immediate snapshot (playhead, CPU)
- `consumeDiagnostics()` — queued warnings (object dropouts, matrix clipping); prevents the host from missing transient errors between polling intervals

### Main-Thread Tick (`update()`)

`update()` remains part of the public API so hosts can keep a stable polling/tick structure, but the current implementation no longer performs deferred focus-compensation work. That path was removed after DBAP normalization made it unnecessary.

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

- Public API entry point: `source/spatial_engine/realtimeEngine/src/EngineSession.hpp`
- Core library CMake target: `source/spatial_engine/realtimeEngine/CMakeLists.txt` (`EngineSessionCore`)
- Working reference: `source/spatial_engine/realtimeEngine/src/internal_validation_runner.cpp`
- Test assets: `data/sourceData/lusid_package/scene.lusid.json`, `source/speaker_layouts/stereo.json`
