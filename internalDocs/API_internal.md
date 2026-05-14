# EngineSession API — Internal Reference

**Last Updated:** May 12, 2026  
**Canonical code source:** `source/spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp`  
**Derived public guide:** `PUBLIC_DOCS/API.md`

---

## Documentation Boundary

- `PUBLIC_DOCS/API.md` is the public embedding guide. Keep it focused on stable host-facing types, lifecycle usage, and input contracts.
- This file is the maintainer reference. Keep lifecycle invariants, threading rules, shutdown sequencing, failure-model details, OSC behavior, and internal gotchas here.

## Contract

> `api_internal_contract.md` — Canonical post-refactor source of truth.

### Public Structs

| Struct | Purpose |
|---|---|
| `EngineOptions` | Core system settings (sample rate, buffer size, device id/name, OSC port, elevation mode) |
| `SceneInput` | Audio scene definition (LUSID scene path, sources folder, ADM file) |
| `LayoutInput` | Speaker layout path and optional remap CSV path |
| `RuntimeParams` | Initial gain/focus/mix values passed at configure time |
| `EngineStatus` | Side-effect-free snapshot of current state (playhead, CPU load, masks, RMS, xruns, backend/sample-rate status) |
| `DiagnosticEvents` | Relocation and cluster-change event flags + bitmask pairs; consumed once per call |

> **Note:** All structs are global (outside any namespace) to avoid polluting public interfaces with internal legacy types.

### Struct Fields

**`EngineOptions`** — passed to `configureEngine()`:
```cpp
struct EngineOptions {
    int sampleRate = 48000;
    int bufferSize = 512;
    int outputDeviceId = -1;        // stable backend id; -1 = system default
    std::string outputDeviceName;   // empty = system default
    int oscPort = 9009;             // 0 = disable OSC entirely
    ElevationMode elevationMode = ElevationMode::RescaleAtmosUp;
};
```

**`SceneInput`** — passed to `loadScene()`:
```cpp
struct SceneInput {
    std::string scenePath;          // path to scene.lusid.json
    std::string sourcesFolder;      // path to mono WAV stems (mono file mode)
    std::string admFile;            // path to multichannel ADM WAV (ADM mode)
};
```
`admFile` and `sourcesFolder` are mutually exclusive. If `admFile` is non-empty, ADM direct streaming is used.

**`LayoutInput`** — passed to `applyLayout()`:
```cpp
struct LayoutInput {
    std::string layoutPath;         // path to speaker layout JSON
    std::string remapCsvPath;       // optional; empty = identity remap
};
```

**`RuntimeParams`** — passed to `configureRuntime()`:
```cpp
struct RuntimeParams {
    float masterGainDb = 0.0f;      // Master gain in dB. Range: -60–+12 dB. 0 dB = unity.
    float dbapFocus    = 1.5f;      // DBAP rolloff exponent. Range: 0.1–5.0.
    float speakerMixDb = 0.0f;      // Post-DBAP main trim in dB. Range: -60–+12 dB.
    float subMixDb     = 0.0f;      // Post-DBAP sub trim in dB. Range: -60–+12 dB.

    static RuntimeParams defaults(); // Canonical defaults — single source of truth.
};
```
All dB fields are converted to linear at store time via `clampDb` → `dbToLinear`.
`getRuntimeParams()` performs the inverse conversion (`linearToDb`) and re-clamps; a zero or
negative linear value returns `-60.0f` (never `-inf` or `NaN`).

**`EngineStatus`** — returned by `queryStatus()`:

In addition to the existing playhead / CPU / mask / RMS / underrun fields, `EngineStatus` now carries a minimal read-only backend/audio snapshot for host-facing diagnostics:

```cpp
std::string audioBackendLabel;              // best-effort runtime backend/API label
int requestedSampleRate = 48000;           // requested engine rate
double effectiveStreamSampleRate = 0.0;    // actual running stream rate, if known
bool effectiveStreamSampleRateKnown = false;
int outputDeviceId = -1;                   // resolved backend output device id, if known
std::string outputDeviceName;              // selected output device, if known
double outputDevicePreferredSampleRate = 0.0; // device preferred/default rate, if known
bool outputDevicePreferredSampleRateKnown = false;
```

Semantics:

- `audioBackendLabel` is sourced from the backend, not from GUI platform guesses.
- RtAudio builds report the best available API-specific label from RtAudio itself, e.g. `RtAudio / CoreAudio`, `RtAudio / JACK`, `RtAudio / ALSA`, `RtAudio / PulseAudio`, `RtAudio / WASAPI`, or `RtAudio API unknown`.
- `outputDevicePreferredSampleRate` is scan/open metadata only. It is **not** the same thing as the actual running stream rate.
- `effectiveStreamSampleRate` is the authoritative runtime rate when available after backend open/start.
- Hosts must check the `*Known` flags before presenting either rate as confirmed.

---

### Lifecycle & Public Methods

The engine enforces a strict, linear initialization sequence:

1. `configureEngine(const EngineOptions&)` — stores sampleRate, bufferSize, outputDeviceId, outputDeviceName, oscPort, elevationMode into `mConfig`. Always returns `true`.
2. `loadScene(const SceneInput&)` — parses LUSID scene via `JSONLoader`, initializes `Streaming`. Returns `false` if scene file missing or no sources loaded.
3. `applyLayout(const LayoutInput&)` — requires `loadScene` to have succeeded (`mSceneData` guard). Loads speaker layout, initializes `Pose` and `Spatializer`. Calls `configureOutputRouting()` internally.
4. `configureRuntime(const RuntimeParams&)` — clamps and writes gain/focus/mix atomics to `mConfig`. **Does not perform output routing setup** (moved to `applyLayout()`). Safe before and after `start()`. Syncs OSC param values if the OSC server is already running. **OSC ParameterServer is NOT started here — it starts in `start()`.**
5. `start()` — creates and starts `al::ParameterServer` (if `oscPort > 0`), initializes OSC params from current runtime state via `getRuntimeParams()`, registers OSC callbacks, starts `RealtimeBackend` + loader thread.

**Runtime control (before or after `start()`):**

`configureRuntime()` is safe to call at any point after `configureEngine()`:
- **Before `start()`:** stages the initial playback values; the engine will start from these.
- **After `start()`:** applies values live through the same atomic path as the individual setters.

| Method | Writes | Notes |
|---|---|---|
| `configureRuntime(const RuntimeParams&)` | all 4 gain/focus params | Clamps + converts dB→linear. Syncs OSC if running. |
| `getRuntimeParams() -> RuntimeParams` | — (read only) | Returns current state in dB; reflects setters, OSC changes, and `configureRuntime`. |
| `resetRuntimeParams()` | all 4 gain/focus params | Equivalent to `configureRuntime(RuntimeParams::defaults())`. Does not restart playback, reload scene/layout, or clear files. |
| `setPaused(bool)` | `mConfig.paused` | Only supported transport control. Stop/seek unsupported. |
| `update()` | — | Should be called from the main thread / host loop. Currently retained for API stability. |
| `queryStatus() -> EngineStatus` | — | Lock-free snapshot. No state mutation. |
| `consumeDiagnostics() -> DiagnosticEvents` | — | Atomically exchanges event flags. Clears them on read. |
| `shutdown()` | — | Terminal. Destroy and recreate `EngineSession` to restart. |

**Runtime setters (direct C++ control — no OSC required; no OSC sync on individual setters):**

| Method | Writes | Range |
|---|---|---|
| `setMasterGainDb(float)` | `mConfig.masterGain` | -60–+12 dB |
| `setDbapFocus(float)` | `mConfig.dbapFocus` | 0.1–5.0 |
| `setSpeakerMixDb(float)` | `mConfig.loudspeakerMix` | -60–+12 dB |
| `setSubMixDb(float)` | `mConfig.subMix` | -60–+12 dB |
| `setElevationMode(ElevationMode)` | `mConfig.elevationMode` | cast to int |

All writes use `std::memory_order_relaxed`. Safe to call before and after `start()`.
Individual setters do **not** sync OSC visible parameter values. Use `configureRuntime()` or
`resetRuntimeParams()` when OSC sync is required.

### Error Model

- **Synchronous:** Lifecycle methods return `bool`. On failure: `getLastError() -> std::string`.
- **Events/Diagnostics:** `consumeDiagnostics()` returns a `DiagnosticEvents` struct — one struct per call, event flags cleared atomically on read. Fields:
  - `renderRelocEvent` / `renderRelocPrev` / `renderRelocNext` — render-bus channel mask change
  - `deviceRelocEvent` / `deviceRelocPrev` / `deviceRelocNext` — device output channel mask change
  - `renderDomRelocEvent` / `deviceDomRelocEvent` — dominant-speaker mask change (render / device)
  - `renderClusterEvent` / `deviceClusterEvent` — top-4 cluster changed (render / device)
  - Each `*Prev`/`*Next` pair is a `uint64_t` bitmask.

**Backend/sample-rate failure reporting (May 2026):**

- Existing `EngineSession::getLastError()` and `getFailureDiagnostics()` remain the canonical failure surface.
- `EngineSession::start()` now prefers backend-provided init/start errors when `RealtimeBackend` can provide a more specific reason.
- Backend failure strings may include:
  - backend/API label
  - selected device
  - requested sample rate
  - selected device preferred/default sample rate, if known
  - effective running stream sample rate, if known
- If the effective running stream rate is known and not `48000`, startup fails immediately with:
  - `Sample rate mismatch: Spatial Root requires 48000 Hz, but the audio stream is running at <rate> Hz.`

### Threading Constraints

- **Main thread:** All lifecycle methods (`configureEngine` through `start`, and `shutdown`).
- **`update()`:** Must be called from main thread / UI event loop.
- **Audio thread:** Managed internally by `RealtimeBackend`. Host must not block backend threads. Inter-thread communication is wait-free internally (relaxed atomics + lock-free buffer swaps).

### Shutdown Order

Violating this sequence **will** cause deadlocks on macOS CoreAudio and ASIO:

1. `mParamServer->stopServer()` + `mParamServer.reset()` — kill network ingestion first
2. `mOscParams.reset()` — release parameter objects
3. `mBackend->shutdown()` + `mBackend.reset()` — halt audio callback gracefully
4. `mStreaming->shutdown()` + `mStreaming.reset()` — release disk I/O and memory buffers

`mPose`, `mSpatializer`, `mOutputRemap`, and `mSceneData` are destroyed implicitly via `unique_ptr` when `EngineSession` is destructed. They hold no OS-level resources.

### Explicit Exclusions

- **Restartable Stop/Seek:** Deferred — destroy and recreate `EngineSession` to reset or seek. Ring buffers and ADM block-streamers hold state that cannot be flushed atomically.
- **Granular CLI Features:** Debug toggles remain in `main.cpp` CLI parsing, not exposed in `EngineSession`.

---

## Design Rationale

> `api_derived_design.md` — Explains the "why" behind structural choices.

### Pimpl-style OSC and Parameter Lifetime

AlloLib parameters bind to internal memory topologies — exposing them directly risks lifetime violations. `EngineSession` uses a Pimpl-style `OscParams` struct (defined in `EngineSession.cpp`). `mParamServer` is entirely owned and destroyed by the session. OSC parameters are initialized in `start()` with values already in `mConfig` atomics (set by `configureRuntime()`), ensuring OSC and direct-setter state are always in sync at startup.

**OSC parameter ranges** (from `OscParams` in `EngineSession.cpp`):

| Parameter | AlloLib type | Range | Default |
|---|---|---|---|
| `gain_db` | `al::Parameter` | -60–+12 dB | 0.0 |
| `focus` | `al::Parameter` | 0.1–5.0 | 1.5 |
| `speaker_mix_db` | `al::Parameter` | -60–+12 dB | 0.0 |
| `sub_mix_db` | `al::Parameter` | -60–+12 dB | 0.0 |
| `paused` | `al::ParameterBool` | 0/1 | 0 |
| `elevation_mode` | `al::Parameter` | 0–2 | 0 |

### Separation of Status and Diagnostics

- `queryStatus()` — lock-free immediate snapshot (playhead, CPU load, channel masks, RMS)
- `consumeDiagnostics()` — event flags for relocation/cluster events; prevents the host from missing transient events between polling intervals

### Main-Thread Tick (`update()`)

`update()` remains available so hosts can preserve an existing main-thread tick structure, but the current implementation no longer dispatches deferred focus-compensation work. That path was removed after normalized DBAP made it unnecessary.

---

## Hard Constraints

> `api_mismatch_ledger.md` — Do not attempt to refactor around these without a fundamental engine rewrite.

1. **Staged Setup is Non-Negotiable:** `applyLayout()` checks `if (!mSceneData)` and fails immediately if called before `loadScene()`. Object counts from `loadScene` dictate memory allocations required before `applyLayout` can construct the spatial matrix.
2. **Restartable Stop/Seek is Unsafe:** Ring buffers and ADM block-streamers hold state that cannot be flushed atomically. Transport is strictly `setPaused(bool)`.
3. **OSC Ownership:** `mParamServer` cannot be shared with the host. Must be spun up and torn down inside `EngineSession` to guarantee valid AlloLib parameter scoping.
4. **Shutdown Sequence:** `mParamServer->stopServer()` → `mOscParams.reset()` → `mBackend->shutdown()` → `mStreaming->shutdown()` — any other order **will** deadlock on CoreAudio/ASIO.

---

## Validation & Known Gotchas

> `new_context.md` — Phase complete notes and structural gotchas.

**What was validated:** `EngineSessionCore` extracted into a distinct linkable CMake library (`EngineSessionCore` static target). Type definitions restructured to prevent `AlloLib`/threading leakage. `internal_validation_runner.cpp` smoke test proved robust execution and clean teardown.

**Layout vs. device channel count mismatch:** Mismatches between layout channel counts and hardware channel counts trigger a fatal fast-fail. Public docs must describe `EngineOptions` device fallback behavior and layout configuration dependency.

**`uint64_t` bitmask channel cap:** `EngineStatus` uses `uint64_t` bitmasks for channel masks, implicitly capping the engine at 64 output channels.

**`dbapFocus` default divergence:** `RealtimeConfig` atomic default is `1.0f`; `RuntimeParams::dbapFocus` default is `1.5f`. The `1.5f` value from `RuntimeParams` is written to the atomic by `configureRuntime()`. The `1.0f` atomic default is only ever seen if the engine is started without calling `configureRuntime()` first — do not do this.

**Key file pointers:**
- Public API entry point: `source/spatial_engine/realtimeEngine/src/EngineSession.hpp`
- Implementation: `source/spatial_engine/realtimeEngine/src/EngineSession.cpp`
- Core library CMake target: `source/spatial_engine/realtimeEngine/CMakeLists.txt` (`EngineSessionCore`)
- Working reference: `source/spatial_engine/realtimeEngine/src/internal_validation_runner.cpp`
- Test assets: `data/sourceData/lusid_package/scene.lusid.json`, `source/speaker_layouts/stereo.json`
