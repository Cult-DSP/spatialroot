# Spatial Root - Code-Derived Public API Design Doc

## 1. Purpose and Toolchain Role

The Spatial Root public API is the embeddable C++ runtime surface for the real-time engine, situated in `spatial_engine/realtimeEngine/src`. Its role is to expose engine setup, scene loading, layout application, runtime control, and status querying via a stable library interface. It acts as a clear façade over internal implementation details (such as `Streaming`, `Pose`, `Spatializer`, and the AlloLib-based `RealtimeBackend`).

## 2. API Identity and Ownership Model

**Top-Level Public Identity:** `EngineSession`

The `EngineSession` class acts as the sole owner of a configured playback instance runtime lifecycle.

**Ownership:**

- Uses the struct `RealtimeConfig` and `EngineState` internally.
- Owns instantiations of the internal C++ engine components: `Streaming`, `Pose`, `Spatializer`, `RealtimeBackend`, `OutputRemap`, and optionally `al::ParameterServer` (for OSC tracking).
- Handles sequence orchestration internally, ensuring threads and internal constraints are rigidly isolated from the public caller.

## 3. Public Types / Nouns

The public API structures configuration into small, descriptive structs, rather than relying on raw parameter lists.

```cpp
struct EngineOptions {
    int sampleRate = 48000;
    int bufferSize = 512;
    std::string outputDeviceName; // Empty for system default
    int oscPort = 9009;           // OSC parameter server port
    bool enableOsc = false;
    int elevationMode = 0;        // 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp
};

struct SceneInput {
    std::string scenePath;        // Path to Scene LUSID JSON
    std::string sourcesFolder;    // Either a folder path (for mono)...
    std::string admFile;          // ...or an ADM file (mutually exclusive)
};

struct LayoutInput {
    std::string layoutPath;       // Speaker layout JSON
    std::string remapCsvPath;     // Mapping of internal layout channels to device channels (optional)
};

struct RuntimeParams {
    float masterGain;
    float dbapFocus;
    float speakerMixDb;
    float subMixDb;
    bool autoCompensation;
};

struct EngineStatus {
    bool isPlaying;
    bool isPaused;
    double playbackTimeSec;
    float cpuLoad;
    uint64_t xrunCount;
    int numSources;
    int numSpeakers;
    double sceneDuration;
    // ... Diagnostic payload (RMS, relocations) exposed for CLI polling loops
};
```

## 4. Lifecycle and Canonical Public Operations

The startup order in the internal engine is strictly constrained by dependencies between internal agents. The conceptual flow mapped to an `EngineSession` instance is:

1. **`configureEngine(const EngineOptions& opts)`**
   - Prepares internal state/config with non-changing execution variables.
2. **`loadScene(const SceneInput& sceneIn)`**
   - Loads the LUSID JSON and opens audio WAV files (sets up `Streaming`). Must occur prior to applying the layout.
3. **`applyLayout(const LayoutInput& layoutIn)`**
   - Loads layout JSON, initializes `Pose` (calculates positions from scene + layout), configures `Spatializer` structure, determines layout dimensions, prepares the Output Mapping CSV.
4. **`configureRuntime(const RuntimeParams& params)`**
   - Applies live gain parameters, calculates auto compensation off-thread before audio starts.
5. **`start()`**
   - Activates OSC server, starts `RealtimeBackend`, and begins background `LoaderThread`.
6. **`pause(bool)`**
   - Flips `config.paused` atomic flag gracefully pausing traversal and outputting silence.
7. **`queryStatus() -> EngineStatus`**
   - Provides snapshot copies of relaxed-atomic UI markers for the CLI.
8. **`shutdown()`**
   - Cleanly shuts down the OSC server, halts the Audio backend, finally shutting down `Streaming` threads.

## 5. Threading and Safety Constraints

- **MAIN thread:** Method invocations of the `EngineSession` should remain on the primary execution thread. Things like `Spatializer::computeFocusCompensation()` allocate memory and must be invoked exclusively from the MAIN thread.
- **AUDIO thread:** `EngineSession` functions internally abstract `Backend` behavior—none of the `start()`, `pause()`, etc., directly run in the callback but write to thread-safe atomics mapped to `RealtimeConfig` and `EngineState`.
- **Atomic mutations:** Live updates via `setRuntimeParams(...)` are strictly performed using standardized `std::memory_order_relaxed` routines and flags, guaranteeing lock-free reads in the audio loop.

## 6. Separating Stable Public API vs CLI Surface

**What stays in the new API bounds (via `EngineSession`):**

- System configuration, object ownership, state management, setup invariants threading setup, and session teardown.

**What stays purely CLI (`main.cpp`):**

- Console splash/banners.
- Signal interception `(SIGINT/SIGTERM)`.
- CLI parameter parsing (converting argc/argv into typed configurations).
- Device listing `--list-devices`.
- Textual monitoring loop (which polls `EngineSession::queryStatus()` at intervallic sleeps and formats diagnostic masks like `[RELOC-RENDER]`).

## 7. Deferred Items

- **Stop, restart, and seeking semantics:** Right now, time progression handles pausing (early empty callback returns). True transport seeks or full restarts require changes to `Streaming::loaderWorker` which enforces sequential buffer streaming right now.
- **Offline Render capabilities:** Are still abstracted under `SpatialRenderer` header logic.
