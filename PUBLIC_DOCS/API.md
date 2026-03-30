# Spatial Root EngineSession API V1 Internal Implementation Spec

This document defines the intended V1 public C++ session API for the Spatial Root realtime engine. It is an internal implementation spec for extracting the current orchestration logic from main.cpp into an embeddable EngineSession layer. The goal is to make the CLI, GUI, and future embedding paths use the same runtime control surface.

## Overview

EngineSession is the intended embeddable C++ runtime façade for the Spatial Root realtime engine. It should extract the orchestration currently implemented in main.cpp into a reusable session object while preserving existing startup, runtime, and shutdown behavior.

V1 goals:

- expose a typed C++ runtime API
- keep CLI behavior functionally unchanged
- preserve current backend wiring and shutdown ordering
- keep OSC compatibility for the current GUI
- avoid exposing debug-era diagnostics as part of the stable public API

## Core API and lifecycle

The current runtime source of truth is main.cpp. The V1 EngineSession refactor should preserve this sequence:

- configure engine options
- load LUSID scene and stage source input mode
- construct and load Streaming
- load speaker layout
- construct and initialize Pose
- construct and initialize Spatializer
- prepare source state and optional output remap
- optionally start OSC parameter handling
- initialize backend
- wire Streaming, Pose, and Spatializer into backend
- start loader thread
- start backend/audio
- shut down in ordered sequence

## Ownership model

EngineSession should own the runtime objects that are currently stack-allocated in main.cpp, including:

- RealtimeConfig
- EngineState
- loaded SpatialData
- loaded SpeakerLayoutData
- Streaming
- Pose
- Spatializer
- RealtimeBackend
- optional OutputRemap
- optional OSC parameter server state

RealtimeBackend should continue receiving raw pointers to Streaming, Pose, and Spatializer as it does now. The refactor should move ownership, not redesign callback wiring.

## Intended V1 public API surface

Required methods:

- `bool configureEngine(const EngineOptions&)`
- `bool loadScene(const SceneInput&)`
- `bool applyLayout(const LayoutInput&)`
- `bool configureRuntime(const RuntimeParams&)`
- `bool start()`
- `bool setPaused(bool)`
- `bool shutdown()`
- `EngineStatus status() const`
- `std::string lastError() const`

### Method preconditions

- `configureEngine()` must be called before anything else
- `loadScene()` requires configured engine options
- `applyLayout()` requires a loaded scene
- `configureRuntime()` can happen before `start()`
- `start()` requires scene + layout + runtime config to be valid
- `shutdown()` should be safe to call even after partial failure

Runtime setters:

- `setMasterGain(float)`
- `setDbapFocus(float)`
- `setSpeakerMixDb(float)`
- `setSubMixDb(float)`
- `setAutoCompensation(bool)`
- `setElevationMode(ElevationMode)`

Not part of V1 public API:

- `update()`
- `consumeDiagnostics()`
- `stop()`
- `seek()`
- relocation-event reporting
- CLI banner/help/device-list presentation
- restartable transport semantics unless the implementation explicitly supports them

## Public types

The API requires these configurations and return types. The exact field definitions must contain at least:

- **EngineOptions**: `sampleRate` (int), `bufferSize` (int), `enableOsc` (bool)
- **SceneInput**: `scenePath` (string), `inputMode` (enum `InputMode`), `sourcesFolder` (string)
- **LayoutInput**: `layoutPath` (string)
- **RuntimeParams**: `masterGain` (float), `dbapFocus` (float)
- **ElevationMode**: Enum for vertical scaling behaviors (e.g., `RescaleAtmosUp`, `RescaleFullSphere`, `Clamp`)
- **EngineStatus**: `running` (bool), `paused` (bool), `playbackTime` (double), `frameCounter` (uint64_t), `cpuLoad` (float), `mainRms` (float), `subRms` (float), `underrunCount` (size_t), `sourceCount` (size_t), `speakerCount` (size_t), `outputChannelCount` (size_t)

### OSC behavior

If `enableOsc == true` in `EngineOptions`, OSC server startup happens inside `start()`. If `false`, no OSC objects are created.

## Status surface

V1 should expose a simple snapshot-style status() method returning current operational values such as:

- running
- paused
- playback time
- frame counter
- callback CPU load
- main RMS
- sub RMS
- underrun count
- source count
- speaker count
- output channel count

## Quick Start Example

```cpp
#include "EngineSession.hpp"
#include <iostream>

int main() {
    EngineSession session;

    EngineOptions engine;
    engine.sampleRate = 48000;
    engine.bufferSize = 512;
    engine.enableOsc = false;

    if (!session.configureEngine(engine)) {
        std::cerr << session.lastError() << "\n";
        return 1;
    }

    SceneInput scene;
    scene.scenePath = "scene.lusid.json";
    scene.inputMode = InputMode::MonoSources;
    scene.sourcesFolder = "/path/to/media";

    if (!session.loadScene(scene)) {
        std::cerr << session.lastError() << "\n";
        return 1;
    }

    LayoutInput layout;
    layout.layoutPath = "layout.json";

    if (!session.applyLayout(layout)) {
        std::cerr << session.lastError() << "\n";
        return 1;
    }

    RuntimeParams runtime;
    runtime.masterGain = 0.5f;
    runtime.dbapFocus = 1.5f;

    if (!session.configureRuntime(runtime)) {
        std::cerr << session.lastError() << "\n";
        return 1;
    }

    if (!session.start()) {
        std::cerr << session.lastError() << "\n";
        return 1;
    }

    session.setPaused(false);

    EngineStatus s = session.status();
    std::cout << "Running: " << s.running << "\n";

    session.shutdown();
    return 0;
}
```

## Main-thread runtime work

The current engine performs some supervisory work on the main thread in main.cpp, including status polling and certain deferred operations. For V1, the API should not require a host-driven update() contract unless the implementation genuinely needs one after refactor. Avoid introducing update() into the public API unless it is necessary and clearly justified by the backend code.

## Device and channel-count behavior

The layout determines the engine’s required output channel count. Physical device compatibility is ultimately validated when the backend opens the selected audio device. V1 should preserve the current behavior in which layout-derived channel count is established before backend startup, while device-opening failure is treated as a backend/start failure rather than a layout-parse failure.

## Shutdown ordering

shutdown() must preserve the current runtime teardown order:

- stop OSC server first
- stop/shutdown backend second
- shut down streaming last

shutdown() should tolerate partially initialized sessions and should clean up only the subsystems that were successfully created.

The agent must preserve this ordering while moving orchestration out of main.cpp.

## Build integration note

The V1 refactor should produce an embeddable library-facing API layer centered on EngineSession. Do not commit this document to a final CMake target name unless the build system is updated accordingly. The implementation should prefer a minimal extraction that allows the CLI target to call the same EngineSession logic used by host applications.

## Agent implementation constraints

- extract orchestration from main.cpp into EngineSession
- keep core DSP and streaming classes intact unless changes are required for ownership/lifecycle
- preserve current startup order
- preserve current shutdown order
- keep OSC compatibility for the current GUI path
- do not make the public API shell out to the CLI
- do not expose debug monitoring surfaces as stable public API
- do not invent new public methods unless required by actual backend constraints
- keep main.cpp as a thin CLI adapter after refactor
