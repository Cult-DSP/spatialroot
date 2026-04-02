# Realtime EngineSession API: Internal Contract
**Status:** Canonical Internal Source of Truth (Post-Refactor Stabilization)

## 1. Purpose and Role
The `EngineSession` class provides a stable, embeddable C++ runtime surface for the Spatial Root realtime engine. This contract defines the strict lifecycle, threading constraints, and error-handling models required to host the engine safely.

## 2. Public Structs
The API relies on typed configuration and status structs to eliminate ambiguous primitive parameters:
* `EngineConfig`: Core system settings (sample rate, buffer size, base paths).
* `SceneConfig`: Definition of the audio scene (ADM/LUSID payload paths).
* `LayoutConfig`: Speaker layout and routing parameters.
* `RuntimeConfig`: OSC ports, UI bindings, and telemetry settings.
* `EngineStatus`: Side-effect-free snapshot of current engine state (playhead, CPU load, active voices).
* `DiagnosticMessage`: Structured warning/error payload for asynchronous events.

## 3. Lifecycle & Public Methods
The engine enforces a strict, linear initialization sequence. State transitions must occur in this exact order:

1. `configureEngine(const EngineConfig&)`: Allocates base resources.
2. `loadScene(const SceneConfig&)`: Parses scene data and prepares object tracks.
3. `applyLayout(const LayoutConfig&)`: Configures structural rendering and spatial mapping.
4. `configureRuntime(const RuntimeConfig&)`: Binds OSC (Pimpl-style `OscParams`) and telemetry.
5. `start()`: Ignites the audio backend and begins processing.

**Runtime Control:**
* `setPaused(bool paused)`: The explicitly supported transport control. (Note: Stop/seek are deferred and currently unsupported).
* `update()`: Main-thread tick. *Must be called regularly* to process deferred work (e.g., `computeFocusCompensation()`).
* `queryStatus() -> EngineStatus`: Polls current metrics without mutating state.
* `consumeDiagnostics() -> std::vector<DiagnosticMessage>`: Empties the internal diagnostic queue.
* `shutdown()`: Triggers the rigid teardown sequence.

## 4. Error Model
* **Synchronous Errors:** Lifecycle methods return `bool` (or `Result` wrappers). On failure, the host must call `getLastError() -> std::string` to retrieve the human-readable failure reason.
* **Asynchronous/Runtime Errors:** Non-fatal spatialization or clipping errors are routed to the queue retrieved via `consumeDiagnostics()`.

## 5. Threading Constraints
* **Main Thread:** All lifecycle methods (`configureEngine` through `start`, and `shutdown`) must be called from the main thread.
* **Deferred Processing:** `update()` MUST be called from a main-thread UI/Event loop. It handles asynchronous dispatch tasks that are unsafe for the audio thread, like `computeFocusCompensation()`.
* **Audio Thread:** Managed entirely internally by the backend. The host must not block the backend threads. Inter-thread communication is strictly wait-free internally.

## 6. Exact Shutdown Order
The `shutdown()` method strictly adheres to the following teardown sequence to prevent dangling OSC pointers and audio thread segfaults:
1. `mParamServer->stopServer()`: Kill network ingestion first to detach AlloLib parameters.
2. `mBackend->shutdown()`: Halt the audio callback gracefully.
3. `mStreaming->shutdown()`: Release disk IO and memory buffers.

## 7. Explicit Exclusions
* **Restartable Stop/Seek:** Deferred. The engine must currently be destroyed and recreated to perform a hard reset or seek operation.
* **Granular CLI Features:** Highly specific debug toggles remain relegated to CLI parsing and are not exposed in the `EngineSession` API contract.
