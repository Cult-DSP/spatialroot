# Spatial Root - Mismatch Ledger

This ledger reconciles conflicts between the ideal `api_concept.md` design parameters relative to the hard constraints observed directly within the `spatial_engine/realtimeEngine` C++ architecture.

## Mismatch 1: Staged Setup Strictness (Inter-dependency Rules)

**Concept Implication:** It is implied that setting up options, scenes, layouts, and engine tuning behave somewhat like detached options cleanly submitted until an eventual `.start()`.
**Code Reality:** The engine strictly couples the timeline of class instances forming chained prerequisites. `Pose::loadScene` mandates knowledge that stems from the completion of the `Streaming::loadScene()`. `Spatializer::prepareForSources` must execute post-both completions.
**Resolution:** The API functions (`loadScene` and `applyLayout`) must be utilized iteratively. They are not merely struct setters, but initialization anchors invoking deep component orchestration inside `EngineSession`.

## Mismatch 2: Transport Semantics (Restartable Stops/Seeks)

**Concept Implication:** "avoid promising transport behavior that current code does not cleanly support, such as restartable stop/seek semantics".
**Code Reality:** Confirmed. The `RealtimeTypes.hpp` outlines variables and threads explicitly built on real-time forwards progression. Audio streaming blocks use background `.wav` file workers (`Streaming::loaderWorker`) loading chunked double buffeting slots asynchronously. `stop` semantics fundamentally deallocate buffers and close backend endpoints. There is roughly no existing concept of sample-accurate seeking out-of-the-box or restarting.
**Resolution:** Defer non-linear transport implementations. `setPaused(bool)` is the canonical naming convention going forward. It serves as the sole supported transport state, preventing timeline progression implicitly without making guarantees about internal buffer clearing semantics, safely deferring any seek/restart mechanics.

## Mismatch 3: Ownership Boundaries Over OSC (GUI Server)

**Concept Implication:** OSC ports are described as "optional engine options" configured on startup.
**Code Reality:** The `al::ParameterServer` actually spins up its own standalone UDP background thread listening for requests over OSC and firing callbacks modifying `RealtimeConfig` parameter atomics. It must be explicitly shut down prior to engine backend disposal.
**Resolution:** The OSC listener sits neatly under the `EngineSession` umbrella as an instantiation of the public API structure representing a remote control module acting exclusively to inform `EngineState`. Clean shutdown sequence dependencies have been extended to account for OSC teardowns.

## Mismatch 4: `computeFocusCompensation` constraints

**Concept Implication:** Realtime updates to auto-compensation happen asynchronously.
**Code Reality:** Memory architectures strictly inhibit execution of the DBAP auto gain computation natively inside realtime UI hooks. Because it performs allocations, `Spatializer::computeFocusCompensation()` MUST trigger explicitly bounded to the MAIN thread logic context (currently handled within `main.cpp` inside a while loop pulling a `pendingAutoComp` flag).
**Resolution:** The `EngineStatus` / API tick mechanism mandates that `EngineSession` must probably expose an internal `update()` or `processMessages()` to run inside the outer loop acting as the MAIN thread execution proxy to trigger these compensations.

## Mismatch 5: Error Handlers

**Concept Implication:** "Informative failure surface... simple approach such as bool plus lastError()".
**Code Reality:** Presently failures are printed via `std::cerr` or invoked under strict fatal crash scenarios.
**Resolution:** Refactoring `EngineSession` initializers (`start`, `loadScene`, etc) will catch these failure patterns and translate them into a `std::string getLastError()` property, allowing the user implementations (like a new CLI app) to safely parse setup failures prior to fatal aborts.

## Mismatch 6: AlloLib Event Callbacks & Object Lifting

**Concept Implication:** OSC ports and runtime parameters are managed internally and hidden from the API caller to keep the header clean of dependencies.
**Code Reality:** The `al::ParameterServer` expects parameters (`al::Parameter`, `al::ParameterBool`) passed to it via `operator<<` to remain alive in memory for the life of the server. In the original `main.cpp`, they were instantiated as blocking stack variables. When abstracting into `EngineSession`, making them static violates multiple instantiation constraints (as callbacks would retain invalid `this` pointers across restarts). Since `EngineSession.hpp` aims to be free of `allolib` header pollution, they cannot be declared explicitly as struct members in the header.
**Resolution:** Implemented an opaque pointer (Pimpl pattern) `struct OscParams;` mapped to a `std::unique_ptr<OscParams> mOscParams;` inside `EngineSession.hpp`. The full struct is defined privately inside `EngineSession.cpp`. This encapsulates the lifetime of AlloLib types strictly to the execution context of the active session without polluting the public API or causing callback dangling references.
