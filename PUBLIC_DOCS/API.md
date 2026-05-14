# Spatial Root EngineSession API

`EngineSession` is the public C++ embedding surface for the Spatial Root realtime engine. It lets a host application configure audio I/O, load a LUSID scene, apply a speaker layout, control runtime DSP parameters, and poll status and diagnostics from the main thread.

This document is the public integration guide. Maintainer-only lifecycle, threading, shutdown-order, and implementation notes live in [internalDocs/API_internal.md](../internalDocs/API_internal.md).

## Quick Start

```cpp
#include "EngineSession.hpp"
#include <iostream>

int main() {
    EngineSession session;

    EngineOptions engine;
    engine.sampleRate = 48000;
    engine.bufferSize = 512;
    engine.oscPort    = 0; // disable OSC for an in-process host

    if (!session.configureEngine(engine)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    SceneInput scene;
    scene.scenePath     = "scene.lusid.json";
    scene.sourcesFolder = "/path/to/package";

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

    RuntimeParams runtime = RuntimeParams::defaults();
    runtime.masterGainDb = 0.0f;
    runtime.dbapFocus    = 1.5f;

    if (!session.configureRuntime(runtime)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    if (!session.start()) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    while (true) {
        EngineStatus status = session.queryStatus();
        if (status.isExitRequested) break;

        session.update();
        DiagnosticEvents events = session.consumeDiagnostics();
        (void)events;
    }

    session.shutdown();
    return 0;
}
```

## Lifecycle

Call methods in this order:

1. `configureEngine()`
2. `loadScene()`
3. `applyLayout()`
4. `configureRuntime()`
5. `start()`

While running, call `update()`, `queryStatus()`, and `consumeDiagnostics()` regularly from the main thread. `shutdown()` is always safe and is terminal for that session instance.

`configureRuntime()` is safe both before and after `start()`. Direct setters such as `setMasterGainDb()` and `setDbapFocus()` may also be used to update live runtime state.

## Core Types

### `EngineOptions`

| Field | Type | Default | Meaning |
| --- | --- | --- | --- |
| `sampleRate` | `int` | `48000` | Audio sample rate in Hz |
| `bufferSize` | `int` | `512` | Frames per audio callback |
| `outputDeviceId` | `int` | `-1` | Stable backend output-device id; `-1` selects the system default |
| `outputDeviceName` | `std::string` | `""` | Display name / CLI fallback; used only when no device id is provided |
| `oscPort` | `int` | `9009` | OSC control port; set to `0` to disable OSC |
| `elevationMode` | `ElevationMode` | `RescaleAtmosUp` | Vertical remapping mode |

### `SceneInput`

Exactly one source mode must be provided.

| Field | Type | Meaning |
| --- | --- | --- |
| `scenePath` | `std::string` | Path to `scene.lusid.json` |
| `sourcesFolder` | `std::string` | Folder containing mono WAV stems |
| `admFile` | `std::string` | Path to a multichannel ADM WAV file |

### `LayoutInput`

| Field | Type | Meaning |
| --- | --- | --- |
| `layoutPath` | `std::string` | Path to the speaker layout JSON file |
| `remapCsvPath` | `std::string` | Deprecated internal scaffolding; public callers should leave this empty |

### `RuntimeParams`

`RuntimeParams::defaults()` is the canonical default source used by API, CLI, and GUI callers of the realtime engine.

| Field | Type | Default |
| --- | --- | --- |
| `masterGainDb` | `float` | `0.0f` |
| `dbapFocus` | `float` | `1.5f` |
| `speakerMixDb` | `float` | `0.0f` |
| `subMixDb` | `float` | `0.0f` |

## Input Contracts

### LUSID Scene

The engine reads LUSID `scene.lusid.json` directly.

- `timeUnit` should always be specified explicitly.
- `duration` is authoritative for scene length.
- Every `audio_object` is expected to have an initial keyframe at `t=0.0`.
- `cart` coordinates are normalized Cartesian directions in `[-1, 1]`.

### Speaker Layout JSON

The public JSON field is `channel`. Internally, Spatial Root stores that resolved output slot as `deviceChannel`.

```json
{
  "speakers": [
    { "azimuth": 0.0, "elevation": 0.0, "radius": 5.0, "channel": 1 }
  ],
  "subwoofers": [
    { "channel": 16 },
    { "channel": 17 }
  ]
}
```

- Layout routing is layout-derived.
- Final output width is `max(channel) + 1`.
- Sparse channel layouts are valid; unmapped output channels remain silent.
- CSV remapping is deprecated and is no longer the supported public routing workflow.

### LUSID Package Layout

The common mono-stem package format is a flat folder containing:

- `scene.lusid.json`
- `containsAudio.json`
- mono WAV stems such as `1.1.wav`, `11.1.wav`, and `LFE.wav`

If `containsAudio.json` is present, it is the preferred source-to-file mapping contract.

## Methods

| Method | Summary |
| --- | --- |
| `configureEngine(const EngineOptions&)` | Stores audio and backend configuration |
| `loadScene(const SceneInput&)` | Loads the LUSID scene and source-input mode |
| `applyLayout(const LayoutInput&)` | Loads the speaker layout and output-channel plan |
| `configureRuntime(const RuntimeParams&)` | Applies gain, focus, and mix parameters; safe before or after `start()` |
| `start()` | Opens audio, starts streaming, and begins playback |
| `setPaused(bool)` | Pauses or resumes transport |
| `setMasterGainDb(float)` | Sets master gain in dB |
| `setDbapFocus(float)` | Sets DBAP focus |
| `setSpeakerMixDb(float)` | Sets loudspeaker trim in dB |
| `setSubMixDb(float)` | Sets subwoofer trim in dB |
| `setElevationMode(ElevationMode)` | Updates vertical remapping mode |
| `getRuntimeParams()` | Returns current runtime values in user-facing units |
| `resetRuntimeParams()` | Restores `RuntimeParams::defaults()` |
| `update()` | Main-thread tick |
| `queryStatus()` | Returns a snapshot of transport, levels, masks, and backend state |
| `consumeDiagnostics()` | Returns and clears pending relocation or cluster events |
| `getLastError()` | Returns the last synchronous failure string |
| `getFailureDiagnostics()` | Returns the last startup-stage diagnostic block |
| `shutdown()` | Stops audio and releases session resources |

## Status And Diagnostics

`queryStatus()` is the host-facing status surface. In addition to transport and render metrics, it exposes backend-facing fields such as:

- `audioBackendLabel`
- `requestedSampleRate`
- `effectiveStreamSampleRate`
- `outputDeviceId`
- `outputDeviceName`
- `outputDevicePreferredSampleRate`

`consumeDiagnostics()` is the event surface for render-bus and device-bus relocation or dominant-cluster changes.

`getFailureDiagnostics()` returns a structured block for the most recent failed `loadScene()`, `applyLayout()`, or `start()` call. It is intended for logs and embedding-host diagnostics panels.

## Embedding With CMake

```cmake
find_package(spatialroot REQUIRED)
target_link_libraries(myapp PRIVATE spatialroot::EngineSessionCore)
```

Use:

```cpp
#include "EngineSession.hpp"
#include "RealtimeTypes.hpp"
```

All required AlloLib include paths are propagated by the `EngineSessionCore` target.
