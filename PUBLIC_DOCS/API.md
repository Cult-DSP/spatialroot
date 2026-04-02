# Spatial Root EngineSession API — V1

`EngineSession` is the embeddable C++ runtime for the Spatial Root realtime spatial audio engine. It handles audio device setup, scene and layout loading, DSP parameter control, and clean shutdown — exposing a single typed surface usable from a CLI, a GUI host, or any embedding context.

## Document in progress:

- To prepare the API documentation for the next iteration, the document needs significant clarification around runtime control and error conditions. Most importantly, it must explicitly state how a host C++ application can dynamically update parameters (like gain or focus) while the engine is running, rather than implying this is an OSC-only feature. Additionally, the lifecycle state machine needs to address edge cases, such as whether initialization methods can be re-called to correct errors, how setPaused() behaves before start(), and if background threads terminate automatically when isExitRequested fires. The failure states for configureEngine(), applyLayout(), and configureRuntime() must also be explicitly defined.

- Furthermore, the data types and main loop contract require stricter constraints. The document needs to define acceptable ranges and limits for fields like sampleRate, bufferSize, and dB trims, convert elevationMode into a clearly defined enum, and explicitly confirm or deny the 64-channel limit implied by the uint64_t bitmasks. Finally, the main loop documentation must provide guidance on the required polling frequency for update() and consumeDiagnostics(), clarifying the relationship between these calls to prevent buffer overruns or event queue bloat.

## Quick Start

```cpp
#include "EngineSession.hpp"
#include <iostream>

int main() {
    EngineSession session;

    EngineOptions engine;
    engine.sampleRate = 48000;
    engine.bufferSize = 512;
    engine.oscPort    = 0; // disable OSC

    if (!session.configureEngine(engine)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    SceneInput scene;
    scene.scenePath     = "scene.lusid.json";
    scene.sourcesFolder = "/path/to/media";

    if (!session.loadScene(scene)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    LayoutInput layout;
    layout.layoutPath = "layout.json";

    if (!session.applyLayout(layout)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    RuntimeParams runtime;
    runtime.masterGain = 0.5f;
    runtime.dbapFocus  = 1.5f;

    if (!session.configureRuntime(runtime)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    if (!session.start()) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    // Main loop — call update() and consumeDiagnostics() regularly
    while (true) {
        EngineStatus s = session.queryStatus();
        if (s.isExitRequested) break;

        session.update();
        DiagnosticEvents ev = session.consumeDiagnostics();
        // ... inspect s and ev
    }

    session.shutdown();
    return 0;
}
```

---

## Session Lifecycle

Methods must be called in the following order. Each stage requires the previous to have succeeded.

```
[new EngineSession]
        │
        ▼
  configureEngine()  →  sets audio device, sample rate, buffer size, OSC port
        │
        ▼
    loadScene()      →  parses LUSID scene file, resolves source media
        │
        ▼
   applyLayout()     →  loads speaker layout, sets output channel count
        │
        ▼
 configureRuntime()  →  sets DSP parameters (gain, focus, mix trims)
        │
        ▼
      start()        →  opens audio device, starts streaming
        │
        ▼
  [Running: update() / queryStatus() / consumeDiagnostics() / setPaused()]
        │
        ▼
     shutdown()      →  always safe to call, including after partial failure
```

`shutdown()` is terminal. To run again, construct a new `EngineSession`.

---

## Methods

### `bool configureEngine(const EngineOptions& opts)`

Applies audio device and engine configuration. Must be called before any other method.

Returns `false` and sets `getLastError()` if the options are invalid.

---

### `bool loadScene(const SceneInput& input)`

Parses the LUSID scene file and prepares source routing. Requires `configureEngine()` to have succeeded.

Exactly one of `sourcesFolder` or `admFile` must be set. Returns `false` if the scene file is missing, malformed, or neither/both source fields are provided.

---

### `bool applyLayout(const LayoutInput& input)`

Loads the speaker layout and establishes the required output channel count. Requires `loadScene()` to have succeeded.

Device compatibility against the physical channel count is not validated here — it is checked when `start()` opens the audio device.

---

### `bool configureRuntime(const RuntimeParams& params)`

Sets DSP parameters. Must be called before `start()`. Can be called after `applyLayout()` succeeds.

---

### `bool start()`

Opens the audio device, starts the streaming loader thread, and begins audio output. Requires all four configure/load steps to have succeeded.

Returns `false` if the audio device cannot be opened or the backend fails to initialize.

If `oscPort > 0` in `EngineOptions`, the OSC server is started here.

---

### `void setPaused(bool isPaused)`

Pauses or resumes audio output without stopping the session. Safe to call while running.

---

### `void setMasterGain(float gain)`

Sets the master output gain (linear, 0.0–1.0 recommended; OSC range 0.1–3.0). Safe to call after `start()`. Uses `std::memory_order_relaxed` — a one-buffer lag is inaudible and not a data race.

---

### `void setDbapFocus(float focus)`

Sets the DBAP rolloff exponent (range 0.2–5.0). If auto-compensation is enabled, schedules a recomputation on the next `update()` call. Safe to call after `start()`.

---

### `void setSpeakerMixDb(float dB)`

Sets the loudspeaker mix trim in dB (range ±10). The value is converted to linear scale before storage. Safe to call after `start()`.

---

### `void setSubMixDb(float dB)`

Sets the subwoofer mix trim in dB (range ±10). The value is converted to linear scale before storage. Safe to call after `start()`.

---

### `void setAutoCompensation(bool enable)`

Enables or disables automatic gain compensation as DBAP focus changes. When enabled, schedules a recomputation on the next `update()` call. Safe to call after `start()`.

---

### `void setElevationMode(ElevationMode mode)`

Sets the elevation rescaling mode at runtime. Takes effect on the next audio block. Safe to call after `start()`.

---

### `void update()`

Processes deferred main-thread work, including auto-compensation state changes. Must be called regularly from the main thread while the session is running.

---

### `EngineStatus queryStatus() const`

Returns a snapshot of current engine state. See [`EngineStatus`](#enginestatus) below.

Check `status.isExitRequested` in your main loop to detect conditions that require shutdown (e.g. audio device loss).

---

### `DiagnosticEvents consumeDiagnostics()`

Returns and clears any pending spatial event notifications since the last call. Call once per main-loop iteration alongside `update()`. See [`DiagnosticEvents`](#diagnosticevents) below.

---

### `void shutdown()`

Stops audio output, tears down the OSC server, and releases all resources. Safe to call after partial initialization or failure. Shutdown order is: OSC server → audio backend → streaming.

---

### `std::string getLastError() const`

Returns the error message from the most recent failed method call. Returns an empty string if no error has occurred.

---

## Types

### `EngineOptions`

Passed to `configureEngine()`.

| Field              | Type          | Default | Description                                                                         |
| ------------------ | ------------- | ------- | ----------------------------------------------------------------------------------- |
| `sampleRate`       | `int`         | `48000` | Audio sample rate in Hz                                                             |
| `bufferSize`       | `int`         | `512`   | Frames per audio callback                                                           |
| `outputDeviceName` | `std::string` | `""`    | Exact device name to open. Empty selects the system default.                        |
| `oscPort`          | `int`              | `9009`                        | UDP port for OSC parameter control. Set to `0` to disable OSC.          |
| `elevationMode`    | `ElevationMode`    | `RescaleAtmosUp`              | Vertical rescaling mode (see `ElevationMode` enum in `RealtimeTypes.hpp`) |

---

### `SceneInput`

Passed to `loadScene()`. Exactly one of `sourcesFolder` or `admFile` must be non-empty.

| Field           | Type          | Description                                    |
| --------------- | ------------- | ---------------------------------------------- |
| `scenePath`     | `std::string` | Path to the LUSID scene JSON file              |
| `sourcesFolder` | `std::string` | Directory containing per-source mono WAV files |
| `admFile`       | `std::string` | Path to a multichannel ADM WAV file            |

---

### `LayoutInput`

Passed to `applyLayout()`.

| Field          | Type          | Description                                                                 |
| -------------- | ------------- | --------------------------------------------------------------------------- |
| `layoutPath`   | `std::string` | Path to the speaker layout JSON file                                        |
| `remapCsvPath` | `std::string` | Optional CSV remapping internal layout channels to physical device channels |

---

### `RuntimeParams`

Passed to `configureRuntime()`.

| Field              | Type    | Default | Description                                               |
| ------------------ | ------- | ------- | --------------------------------------------------------- |
| `masterGain`       | `float` | `0.5`   | Output gain, linear scale 0.0–1.0                         |
| `dbapFocus`        | `float` | `1.5`   | DBAP rolloff exponent, typical range 0.2–5.0              |
| `speakerMixDb`     | `float` | `0.0`   | Loudspeaker mix trim in dB (±10)                          |
| `subMixDb`         | `float` | `0.0`   | Subwoofer mix trim in dB (±10)                            |
| `autoCompensation` | `bool`  | `false` | Enables automatic gain compensation as DBAP focus changes |

---

### `EngineStatus`

Returned by `queryStatus()`.

| Field                   | Type       | Description                                                         |
| ----------------------- | ---------- | ------------------------------------------------------------------- |
| `timeSec`               | `double`   | Current playback position in seconds                                |
| `cpuLoad`               | `float`    | Audio callback CPU load, 0.0–1.0                                    |
| `renderActiveMask`      | `uint64_t` | Bitmask of active render bus speakers                               |
| `deviceActiveMask`      | `uint64_t` | Bitmask of active device output channels                            |
| `renderDomMask`         | `uint64_t` | Dominant render bus speaker mask                                    |
| `deviceDomMask`         | `uint64_t` | Dominant device channel mask                                        |
| `mainRms`               | `float`    | RMS level of the main speaker mix                                   |
| `subRms`                | `float`    | RMS level of the subwoofer mix                                      |
| `xruns`                 | `size_t`   | Cumulative audio callback overrun count                             |
| `nanGuardCount`         | `uint64_t` | Cumulative NaN/denormal interceptions                               |
| `speakerProximityCount` | `uint64_t` | Cumulative proximity gain correction events                         |
| `paused`                | `bool`     | `true` if output is currently paused                                |
| `isExitRequested`       | `bool`     | `true` if the engine has requested host shutdown (e.g. device loss) |

---

### `DiagnosticEvents`

Returned by `consumeDiagnostics()`. Each event field is `true` at most once per call — the struct is cleared on read. Relocation events fire when the dominant speaker assignment changes. Cluster events fire when the dominant speaker group changes.

| Fields                                      | Type       | Description                               |
| ------------------------------------------- | ---------- | ----------------------------------------- |
| `renderRelocEvent`                          | `bool`     | A render bus speaker relocation occurred  |
| `renderRelocPrev` / `renderRelocNext`       | `uint64_t` | Speaker masks before and after relocation |
| `deviceRelocEvent`                          | `bool`     | A device channel relocation occurred      |
| `deviceRelocPrev` / `deviceRelocNext`       | `uint64_t` | Channel masks before and after relocation |
| `renderDomRelocEvent`                       | `bool`     | Dominant render bus group changed         |
| `renderDomRelocPrev` / `renderDomRelocNext` | `uint64_t` | Dominant render masks before and after    |
| `deviceDomRelocEvent`                       | `bool`     | Dominant device channel group changed     |
| `deviceDomRelocPrev` / `deviceDomRelocNext` | `uint64_t` | Dominant device masks before and after    |
| `renderClusterEvent`                        | `bool`     | Top render speaker cluster changed        |
| `renderClusterPrev` / `renderClusterNext`   | `uint64_t` | Cluster masks before and after            |
| `deviceClusterEvent`                        | `bool`     | Top device channel cluster changed        |
| `deviceClusterPrev` / `deviceClusterNext`   | `uint64_t` | Cluster masks before and after            |

---

## Runtime Parameter Control (V1.1)

The following setter methods allow a host to update parameters while the engine is running. All setters use `std::memory_order_relaxed` — a one-audio-buffer lag is inaudible and these writes are not data races.

**Contract:** Call these only after `start()` returns `true` and before `shutdown()`. Calling before `start()` writes the underlying atomics but has no effect on the engine since the audio thread is not yet running.

| Method | Parameter | Range |
|--------|-----------|-------|
| `setMasterGain(float gain)` | Linear output gain | 0.1–3.0 (OSC range) |
| `setDbapFocus(float focus)` | DBAP rolloff exponent | 0.2–5.0 |
| `setSpeakerMixDb(float dB)` | Loudspeaker mix trim | ±10 dB |
| `setSubMixDb(float dB)` | Subwoofer mix trim | ±10 dB |
| `setAutoCompensation(bool enable)` | Auto gain compensation | — |
| `setElevationMode(ElevationMode mode)` | Elevation rescaling | `ElevationMode` enum |

`setDbapFocus()` and `setAutoCompensation(true)` both schedule a focus compensation recomputation. The recomputation runs on the next `update()` call from the main thread. Do not call `computeFocusCompensation()` directly while the engine is running — it is main-thread-only and not RT-safe.

**`update()` / polling loop contract for GUI hosts:**

A GUI host (e.g., Dear ImGui + GLFW) must drive `update()`, `queryStatus()`, and `consumeDiagnostics()` from the main thread at regular intervals — e.g., once per render loop iteration or via a timer. A 50 ms polling interval is typical. Failing to call `update()` while auto-compensation is pending will defer the recomputation indefinitely.

**`oscPort = 0` behavior:**

Setting `oscPort = 0` in `EngineOptions` disables the OSC server entirely — no `al::ParameterServer` is created. This is the correct setting for a GUI host that uses the direct setter surface. Without the guard, `al::ParameterServer` on port 0 would bind an OS-assigned ephemeral UDP port (not a no-op).

---

## Out of Scope for V1

The following are not part of this API:

- `stop()` / `seek()` — restartable transport
- Audio device enumeration — handled at the CLI layer (`--list-devices` flag)
- Direct access to internal DSP or streaming objects

---

## Constraints

These constraints are structural realities of the engine architecture. Do not attempt to work around them without a fundamental engine rewrite.

### 1. Staged setup is non-negotiable

The five-stage lifecycle (`configureEngine` → `loadScene` → `applyLayout` → `configureRuntime` → `start`) cannot be collapsed into a single call. Object counts from `loadScene` dictate memory allocations required before `applyLayout` can construct the spatial matrix.

### 2. `shutdown()` is terminal

There is no `restart()`. Once `shutdown()` is called, construct a new `EngineSession` to run again. Calling `shutdown()` is always safe, including after partial initialization or a failed `start()`.

### 3. OSC server ownership

`mParamServer` is internal to `EngineSession` and cannot be shared with the host application. It is spun up and torn down inside `EngineSession` to guarantee valid AlloLib parameter scoping. Do not attempt to access or share the OSC server object.

### 4. Shutdown order is mandatory

The internal shutdown sequence is: OSC server → audio backend → streaming. This order is enforced by `shutdown()` internally. Violating it (e.g., by calling streaming teardown before backend shutdown) **will cause deadlocks on macOS CoreAudio and ASIO teardowns**. The host only needs to call `shutdown()` — do not call internal teardown methods directly.

### 5. 64-channel output limit

The `uint64_t` bitmasks in `EngineStatus` (`renderActiveMask`, `deviceActiveMask`, etc.) implicitly cap the engine at 64 output channels. The AlloSphere (54.1 channels) is within range. Arrays larger than 64 channels are not supported without a redesign of the bitmask tracking system.

### 6. `update()` must be called from the main thread

`update()` handles deferred main-thread work including auto-compensation recomputation. It must be called regularly from the main thread while the session is running — not from an audio thread or a background thread. A Qt host should drive it via a `QTimer` callback (e.g. 50 ms interval). `queryStatus()` and `consumeDiagnostics()` should be called in the same timer callback.

---

## Embedding Instructions

To embed `EngineSessionCore` in a host application:

**CMake:**
```cmake
# After cmake --install (or building from source with add_subdirectory):
find_package(spatialroot REQUIRED)
target_link_libraries(myapp spatialroot::EngineSessionCore)
# AlloLib include paths are inherited automatically via PUBLIC target_include_directories.
```

**Include:**
```cpp
#include "EngineSession.hpp"   // Public API header
#include "RealtimeTypes.hpp"   // ElevationMode enum, RealtimeConfig, EngineState
```

**Required headers from AlloLib** are exposed transitively through the `EngineSessionCore` CMake target. A host that links `EngineSessionCore` does not need to add AlloLib include paths manually.
