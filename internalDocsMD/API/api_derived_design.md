# EngineSession API: Derived Design
**Status:** Transitional / Architectural Reference 
*(Note: Refer to `api_internal_contract.md` for strict usage rules. This doc explains the "Why" behind the structural design).*

## Architecture Overview
The realtime engine leverages an `EngineSession` boundary to decouple the application lifecycle from the internal rendering DSP. 

### Pimpl-style OSC and Parameter Lifetime
Because AlloLib parameters inherently bind to internal memory topologies, exposing them directly to the host risks severe lifetime violations. `EngineSession` uses a Pimpl-style `OscParams` wrapper. This ensures that the `mParamServer` is entirely owned and destroyed by the session, mapping external generic `RuntimeConfig` values into AlloLib specifics securely within the `configureRuntime()` phase.

### Separation of Status and Diagnostics
The design deliberately splits introspection into two paths:
1. `queryStatus()`: Provides a lock-free, immediate snapshot of state (playhead, CPU).
2. `consumeDiagnostics()`: Consumes queued warnings (e.g., object dropouts, matrix clipping). This prevents the host from missing transient errors between polling intervals.

### Main-Thread Tick (`update()`)
Audio thread strictness dictates that heavy configuration matrices cannot be recalculated during a callback. `computeFocusCompensation()` and similar structural mutators are deferred to the main thread via the `update()` tick, preserving DSP execution boundaries.
