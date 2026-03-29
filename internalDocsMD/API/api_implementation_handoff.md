# Spatial Root - Implementation Handoff Notes

## Target Outcome

The next action is to extract the thick backend orchestration occurring entirely inside `spatial_engine/realtimeEngine/src/main.cpp` into a resilient runtime public API composed of `EngineSession.hpp` and `EngineSession.cpp`. `main.cpp` will be stripped down to a slim adapter, focusing merely on string parsing and periodic console display.

## Files to Create/Modify

1. **Create `spatial_engine/realtimeEngine/src/EngineSession.hpp`**
2. **Create `spatial_engine/realtimeEngine/src/EngineSession.cpp`**
3. **Modify `spatial_engine/realtimeEngine/src/main.cpp`**
4. **Modify `spatial_engine/realtimeEngine/CMakeLists.txt` (to include the new `.cpp` code file in the build).**

## What to Extract from `main.cpp` into `EngineSession`

Move the core sequential logic out of `main()` and cleanly break it into structural initialization steps. The overarching purpose of modifying `main.cpp` is to **strip it of all engine ownership state and internal orchestration**. `main.cpp` will transform from a monolithic script that builds the backend into a clean, simple CLI wrapper that parses arguments, instantiates a single `EngineSession` object, and blindly loops for console outputs.

The `EngineSession` constructor should instantiate the core member properties:

- `RealtimeConfig config;`
- `EngineState state;`
- `std::unique_ptr<Streaming> mStreaming;`
- `std::unique_ptr<Pose> mPose;`
- `std::unique_ptr<Spatializer> mSpatializer;`
- `std::unique_ptr<RealtimeBackend> mBackend;`
- `std::unique_ptr<OutputRemap> mOutputRemap;`
- `std::unique_ptr<al::ParameterServer> mParamServer;`

**Logic Flow Translation:**

1. **`configureEngine`:** populate basics on `RealtimeConfig`.
2. **`loadScene`:** `JSONLoader::loadLusidScene(...)` and instantiate `Streaming`. Call `streaming->loadScene()` or `streaming->loadSceneFromADM()`.
3. **`applyLayout`:** `LayoutLoader::loadLayout(...)`, instantiate `Pose`. `pose->loadScene(...)`, instantiate/initialize `Spatializer`. Set spatializer layout. Instantiate `mOutputRemap` and bind to spatializer. Call `spatializer->prepareForSources(...)`.
4. **`configureRuntime`:** write atomic properties such as `masterGain`, `dbapFocus`. Explicitly calculate `spatializer->computeFocusCompensation()` directly (main thread safe).
5. **`start`:** instantiate `al::ParameterServer`, link callbacks. Instantiate `RealtimeBackend`, assign its streams. `streaming->startLoader()` BEFORE `backend->start()`.
6. **`shutdown`:** Must strictly process in order: `mParamServer->stopServer()` -> wait/halt parameters -> `mBackend->stop()` -> `mStreaming->shutdown()`.
7. **Status polling (`queryStatus`):** Map read-only getters representing the event loops in `main.cpp` covering things like `nanGuardCount`, `renderRelocEvent`, `cpuLoad`, `timeSec`. Provide an easy way for the main event loop to reset latches (e.g. `clearDiagnosticEvents()`).

## What to Retain in `main.cpp` (Do NOT touch internal core mechanics)

- Retain argument parsing logic, `--help`, and argument constraints checking (`getArgString`, `getArgInt`).
- Retain `--list-devices` which outputs `al::AudioDevice` loop.
- Retain `std::signal` listening. When triggered, it flips an external conditional flag invoking `session.shutdown()` or exiting the main CLI polling loop.
- The `while (!shouldExit)` UI polling that logs `[RELOC-OUTPUT]`, spatial info overrides, and outputs tracking frames.

## Crucial Context (Findings from Code Inspection)

During the codebase inspection, specific constraints and behaviors were discovered that the implementations agent must be aware of when setting up the initial refactor:

1. **`atomic<bool> pendingAutoComp` and `spatializer.computeFocusCompensation()`**
   - **Discovery:** In `main.cpp` (around Phase 10 GUI hooks), turning on OSC compensation issues a `pendingAutoComp.store(true, relaxed)`. The calculation `spatializer.computeFocusCompensation()` allocates heap memory internally.
   - **Requirement:** The OSC callback cannot just run this method. It flags it for the MAIN thread's UI polling loop `while (!shouldExit)` to catch and compute it. When making `EngineSession`, you must expose a public tick or `update()` method that the `main.cpp` polling loop can continuously hit so that `EngineSession` can securely check `pendingAutoComp` and safely hit `computeFocusCompensation()` on the main thread securely.

2. **Output Formatting / Diagnostics Latches**
   - **Discovery:** Diagnostic variables like `renderRelocEvent`, `deviceRelocEvent`, and `nanGuardCount` are `atomic<std::bool>` / uints living on `EngineState` that fire once-per trigger inside the fast Audio callback, and are cleared (or reset) continuously by the `main.cpp` polling loop after they are printed to the console.
   - **Requirement:** Because `EngineStatus` provides diagnostic snapshots (e.g. `EngineSession::queryStatus()`), reading the status cannot automatically consume these events (otherwise multiple callers consuming the state would result in missed GUI/CLI frames). The API must either offer a `.consumeDiagnostics()` method that clears the atomic latches or expose the unread values cleanly.

3. **Background Loader Initialization Dependency**
   - **Discovery:** `streaming.startLoader()` MUST be explicitly invoked prior to `backend.start()`. This spinning worker loads chunks for the very first buffer prior to the backend consuming its first callback. This dependency needs to natively happen within `EngineSession::start()`.

4. **Elevation Mode Translation**
   - **Discovery:** `ElevationMode` represents values (`RescaleAtmosUp=0`, `RescaleFullSphere=1`, `Clamp=2`) derived in CLI and saved across a float OSC flag, but structurally stored inside an atomic `int` inside the `RealtimeConfig` struct bound to `ElevationMode`. Handle conversions and bounds-checking properly when configuring runtime features via `RuntimeParams`.

## Ongoing Maintenance / Agent Instructions

Because `EngineSession` abstracts the real-time functionality of the core backend, future changes to `main.cpp` and `RealtimeEngine` processes must be mirrored in the API structure.

When future implementations or coding agents modify the core audio engine logic, they must abide by the following sequence logic:

1. **Engine Updates Must Flow Through the Session:** Do not add new direct instantiations inside `main.cpp`. If a new component or module (i.e., a new tracking agent) is introduced, parameterize its setup inside `EngineSession`.
2. **Sync the EngineStatus Struct:** If new diagnostic latches or metrics (like `nanGuardCount`) are added to `EngineState` or the internal loops, they must be hoisted directly into the `EngineStatus` struct and mapped in `queryStatus()` or `pollEvents()`.
3. **Threading Isolation Enforcement:** Never allow `EngineSession` methods to accidentally violate thread boundaries. For instance, any new engine parameters updated via OSC or live triggers must write using purely lock-free `.store(..., std::memory_order_relaxed)` assignments.
4. **Update the Mismatch Ledger:** If new constraints block behaviors natively promised by the API concept, immediately update the `api_mismatch_ledger.md` rather than attempting a brittle hack.
