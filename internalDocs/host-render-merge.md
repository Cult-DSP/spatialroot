# Host Render Merge Report

**Date:** 2026-06-29  
**Target branch:** `host-render`  
**Goal:** selectively integrate the experimental host-render / Internal Host Bus work from the embedding checkout without merging the stale branch wholesale

## Source and Approach

- Reviewed the embedding work from the separate checkout and isolated the relevant host-render changes instead of merging the old branch directly.
- Ported only the host-render runtime and documentation changes onto current `devel`/`main` state in this repo.
- Preserved newer local realtime-engine and documentation structure.
- Did **not** carry over the embedding branch planning note commit (`internalDocs/API/hostUpdate.md`), because it was a planning artifact rather than maintained product or maintainer documentation.

## Files Changed

### Runtime / API

- `source/spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`
  - Added `AudioOutputMode` with:
    - `HardwareDevice`
    - `InternalHostBus`
  - Added `HostBusConfig` with:
    - `sampleRate`
    - `blockSize`
    - `outputChannels`
    - `interleaved`

- `source/spatial_engine/realtimeEngine/src/EngineSession.hpp`
  - Added new public host-render API:
    - `setAudioOutputMode(AudioOutputMode)`
    - `prepareInternalHostBus(const HostBusConfig&)`
    - `renderHostBlock(float* interleavedOutput, int numFrames, int numChannels)`
    - `shutdownInternalHostBus()`
    - `getRequiredOutputChannelCount() const`
    - `getLastWarning() const`
  - Clarified `EngineStatus::isExitRequested` semantics:
    - host/app-owned exit-request flag only
    - not a proxy for backend-running state
  - Added private warning helper:
    - `setLastWarning(const std::string&)`
  - Added host-bus state members:
    - `mOutputMode`
    - `mHostBusConfig`
    - `mHostBusPrepared`
    - `mLastWarning`

- `source/spatial_engine/realtimeEngine/src/EngineSession.cpp`
  - Added `setLastWarning()` and `getLastWarning()`.
  - Guarded `start()` so hardware-device startup is rejected when `InternalHostBus` mode is selected or prepared.
  - Updated `shutdown()` so:
    - host-bus cleanup is pulled forward only when host-bus mode is actually active
    - hardware-mode shutdown preserves the older backend-stop-before-streaming-stop order
  - Implemented `setAudioOutputMode()` with mutual-exclusion rules:
    - cannot switch to host-bus mode while hardware output is running
    - cannot switch back to hardware mode while host bus is prepared
  - Implemented `prepareInternalHostBus()`:
    - requires successful `loadScene()` and `applyLayout()`
    - rejects invalid channel counts, block size, or sample-rate mismatches
    - rejects non-interleaved mode
    - wires backend to existing `Streaming`, `Pose`, and `Spatializer`
    - starts loader thread if it is not already running
  - Implemented `renderHostBlock()`:
    - validates buffer and dimensions
    - zero-fills output on error
    - calls backend render path without opening a device
    - copies from the routed layout/device output bus into an interleaved host buffer
    - enforces exact per-call channel-count equality with `HostBusConfig::outputChannels`
    - handles channel mismatch behavior:
      - host channels < required channels: render first host channels only and store warning
      - host channels > required channels: render required channels and zero-fill extras, then store warning
  - Implemented `shutdownInternalHostBus()`.
  - Implemented `getRequiredOutputChannelCount()`.
  - Added `<algorithm>` and `<cstring>` includes for host-buffer copy/zero-fill logic.

- `source/spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
  - Added backend-side host-bus lifecycle:
    - `prepareInternalHostBus(const HostBusConfig&)`
    - `renderHostBlock()`
    - `shutdownInternalHostBus()`
    - `isHostBusPrepared() const`
  - Added host-bus state:
    - `mHostBusConfig`
    - `mHostBusPrepared`
    - `mHostIO`
  - Host-bus preparation configures an internal `AudioIOData` instance but does **not** open a hardware audio device.
  - Host-bus render path reuses `processBlock()` so the same render pipeline is used for device-owned and host-pull playback.
  - Host-side scratch output is sized to `RealtimeConfig::outputChannels`, preserving the same routed-bus contract as hardware playback.

- `source/spatial_engine/realtimeEngine/src/Streaming.hpp`
  - Added `isLoaderRunning()` so host-bus preparation can safely start the loader thread only when needed.
  - Added `stopLoader()` and loader-thread restart guards so repeated prepare/shutdown cycles do not double-start or reuse a stale joinable thread.

### Public and Maintainer Docs

- `PUBLIC_DOCS/API.md`
  - Split lifecycle docs into:
    - Hardware Device Mode
    - Internal Host Bus Mode
  - Added host-pull example flow.
  - Documented `AudioOutputMode` and `HostBusConfig`.
  - Added new `EngineSession` host-render methods to the public methods table.
  - Documented channel mismatch behavior and zero-fill expectations.
  - Clarified that `HostBusConfig::outputChannels` is a fixed prepared contract for all `renderHostBlock()` calls.
  - Clarified that `EngineStatus::isExitRequested` is a host/app-owned exit-request flag only.

- `internalDocs/API_internal.md`
  - Added `AudioOutputMode` and `HostBusConfig` to the internal contract table.
  - Added host-render lifecycle methods to the maintainer API contract.
  - Documented host-bus shutdown behavior, hardware/host-bus mutual exclusion, fixed host channel-count contract, and `isExitRequested` semantics.

- `internalDocs/REALTIME_ENGINE.md`
  - Expanded backend responsibilities to include Internal Host Bus mode.
  - Added a short two-mode summary:
    - `HardwareDevice`
    - `InternalHostBus`

- `internalDocs/AGENTS.md`
  - Added `HOST_RENDER_BACKEND.md` to the top-level documentation map.
  - Added it to the maintained internal documentation set.

- `README.md`
  - Added `internalDocs/HOST_RENDER_BACKEND.md` to the documentation map.
  - Added a short public note that host-render returns the routed output bus and that prepared host channel count must remain consistent.

- `internalDocs/FUTURE_WORK.md`
  - Added a short backlog note for a dedicated host-render smoke test.

- `internalDocs/HOST_RENDER_BACKEND.md`
  - New focused maintainer note covering:
    - current realtime audio path
    - new host-render API
    - expected host flow
    - channel mismatch behavior
    - constraints
    - pending validation

## Intentionally Not Carried Over

- `internalDocs/API/hostUpdate.md`
  - This existed on the embedding branch as a large planning/design note.
  - It was intentionally not integrated because the repo now uses the consolidated top-level maintainer docs instead.

## Behavioral Summary

- Added a host-pull rendering mode that does not open a hardware audio device.
- Preserved existing hardware-device playback path.
- Kept both modes mutually exclusive.
- Reused the existing realtime render pipeline instead of introducing a parallel rendering implementation.
- Preserved the pre-existing routing contract by returning the routed layout/device output bus, not the compact internal render bus.
- Made `HostBusConfig::outputChannels` a strict prepared contract for every `renderHostBlock()` call.
- Added warning reporting for handled channel-count mismatches.
- Added zero-fill behavior for host-buffer error cases.
- Fixed host-bus lifecycle teardown so explicit host-bus cleanup prevents loader-thread reuse hazards without changing the normal hardware shutdown order.
- Fixed `queryStatus().isExitRequested` so host-bus mode no longer misreports backend state as an exit request.

## Validation Performed

- Ran `./build.sh --engine-only`
  - Result: passed

## Validation Still Recommended

- Add a dedicated host-render smoke test that:
  - loads known content
  - prepares Internal Host Bus mode
  - calls `renderHostBlock()` for several blocks
  - verifies nonzero output when expected
  - verifies no NaN/Inf output
  - verifies exact rejection when `renderHostBlock(..., numChannels)` differs from prepared `HostBusConfig::outputChannels`
  - verifies safe same-session mode switching:
    - `InternalHostBus -> shutdownInternalHostBus() -> HardwareDevice start()`
    - repeated `prepareInternalHostBus()/shutdownInternalHostBus()` cycles
  - optionally writes a WAV for inspection

## Diff Scope Summary

- Runtime/API files modified: 5
- Public/internal docs modified: 6
- New maintainer doc added: 1
- Planning artifact intentionally excluded: 1

## Diff Stat Snapshot

This snapshot now understates the current landing because follow-up safety and documentation fixes were applied after the initial merge report. Treat the narrative sections above as authoritative.

## Commit Message Draft

```text
Finalize host-render routing and lifecycle contract

Finalize the Internal Host Bus integration by preserving the routed
layout/device output contract in host-render mode, enforcing fixed host
channel width per prepared host bus, restoring safe hardware shutdown
ordering, clarifying isExitRequested semantics, and syncing maintainer and
public docs to the final behavior.
```

## Notes

- The integration was applied as a selective port, not a merge.
- No broad stale file replacement from the older embedding checkout was performed.
- The host-render code was adapted onto the current repo state and current documentation structure.
