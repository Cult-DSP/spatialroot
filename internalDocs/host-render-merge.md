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
  - Updated `shutdown()` to clear host-bus state first.
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
    - copies from the spatializer’s internal render bus into an interleaved host buffer
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

- `source/spatial_engine/realtimeEngine/src/Spatializer.hpp`
  - Added `internalChannelBuffer(unsigned int)` so host-pull output can read the compact internal render bus after a block render.

- `source/spatial_engine/realtimeEngine/src/Streaming.hpp`
  - Added `isLoaderRunning()` so host-bus preparation can safely start the loader thread only when needed.

### Public and Maintainer Docs

- `PUBLIC_DOCS/API.md`
  - Split lifecycle docs into:
    - Hardware Device Mode
    - Internal Host Bus Mode
  - Added host-pull example flow.
  - Documented `AudioOutputMode` and `HostBusConfig`.
  - Added new `EngineSession` host-render methods to the public methods table.
  - Documented channel mismatch behavior and zero-fill expectations.

- `internalDocs/API_internal.md`
  - Added `AudioOutputMode` and `HostBusConfig` to the internal contract table.
  - Added host-render lifecycle methods to the maintainer API contract.
  - Documented host-bus shutdown behavior and hardware/host-bus mutual exclusion.

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
- Added warning reporting for handled channel-count mismatches.
- Added zero-fill behavior for host-buffer error cases.

## Validation Performed

- Ran `./build.sh --engine-only`
  - Result: passed
- Ran `./build/source/spatial_engine/realtimeEngine/spatialroot_realtime --help`
  - Result: passed

## Validation Still Recommended

- Add a dedicated host-render smoke test that:
  - loads known content
  - prepares Internal Host Bus mode
  - calls `renderHostBlock()` for several blocks
  - verifies nonzero output when expected
  - verifies no NaN/Inf output
  - optionally writes a WAV for inspection

## Diff Scope Summary

- Runtime/API files modified: 6
- Public/internal docs modified: 6
- New maintainer doc added: 1
- Planning artifact intentionally excluded: 1

## Diff Stat Snapshot

```text
PUBLIC_DOCS/API.md                                 |  60 ++++++++
README.md                                          |   1 +
internalDocs/AGENTS.md                             |   3 +-
internalDocs/API_internal.md                       |  11 ++
internalDocs/FUTURE_WORK.md                        |   4 +
internalDocs/REALTIME_ENGINE.md                    |   7 +-
source/spatial_engine/realtimeEngine/src/EngineSession.cpp   | 161 +++++++++++++++++++++
source/spatial_engine/realtimeEngine/src/EngineSession.hpp   |  14 ++
source/spatial_engine/realtimeEngine/src/RealtimeBackend.hpp |  66 +++++++++
source/spatial_engine/realtimeEngine/src/RealtimeTypes.hpp   |  22 +++
source/spatial_engine/realtimeEngine/src/Spatializer.hpp     |   6 +
source/spatial_engine/realtimeEngine/src/Streaming.hpp       |   4 +
12 files changed, 357 insertions(+), 2 deletions(-)
```

## Commit Message Draft

```text
Add host-render Internal Host Bus API

Integrates the experimental Spatial Root embedding host-render path onto
current devel/main state without merging the stale embedding branch.
Adds InternalHostBus output mode, host bus configuration, host-pull
rendering via renderHostBlock(), channel mismatch warnings, and
maintainer/public documentation while preserving existing hardware-device
playback.
```

## Notes

- The integration was applied as a selective port, not a merge.
- No broad stale file replacement from the older embedding checkout was performed.
- The host-render code was adapted onto the current repo state and current documentation structure.
