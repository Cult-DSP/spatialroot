# Offline Renderer Split Report — May 8, 2026

## Summary

This pass separated clearly offline renderer code from shared/runtime code without changing the active realtime engine architecture.

- `source/spatial_engine/realtimeEngine/` remains the current active audio engine
- `source/spatial_engine/src/` remains shared infrastructure for:
  - `JSONLoader.*`
  - `LayoutLoader.*`
  - `WavUtils.*`
- `source/spatial_engine/spatialRender/` now owns the offline renderer implementation:
  - `main.cpp`
  - `SpatialRenderer.hpp`
  - `SpatialRenderer.cpp`
  - `OfflineOutputRouteMap.hpp`
  - `OfflineOutputRouteMap.cpp`

Phase note (May 10, 2026): offline render parity Phase 1 added `OfflineOutputRouteMap` as an offline-owned device-indexed routing helper plus layout-only diagnostics. It does not yet change rendered audio output; Phase 2 is responsible for wiring `SpatialRenderer` through this map.

No questionable code was deleted.

## Current Active Realtime Engine Path

- CLI entry point: `source/spatial_engine/realtimeEngine/src/main.cpp`
- Public API: `source/spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp`
- Streaming: `source/spatial_engine/realtimeEngine/src/Streaming.hpp`
- ADM direct streaming: `source/spatial_engine/realtimeEngine/src/MultichannelReader.hpp`
- Pose/layout transform path: `source/spatial_engine/realtimeEngine/src/Pose.hpp`
- Spatialization/DBAP path: `source/spatial_engine/realtimeEngine/src/Spatializer.hpp`
- Audio callback/backend: `source/spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
- Output routing/remap: `source/spatial_engine/realtimeEngine/src/OutputRemap.hpp`
- GUI embed path: `source/gui/imgui/src/App.cpp`

## Old Offline / Batch Renderer Path

- Old offline CLI location: `source/spatial_engine/src/main.cpp`
- Old offline renderer location: `source/spatial_engine/src/renderer/SpatialRenderer.hpp/.cpp`
- Build wiring before this pass: `source/spatial_engine/spatialRender/CMakeLists.txt` built `spatialroot_spatial_render` from those old `../src/...` paths

## Inventory

| File path | Apparent purpose | Used by current realtime engine | Old batch/offline code | Direct references | CMake/build references | Recommended action |
|---|---|---:|---:|---|---|---|
| `source/spatial_engine/realtimeEngine/src/main.cpp` | Realtime CLI entry | yes | no | docs, engine entry searches | `realtimeEngine/CMakeLists.txt` → `spatialroot_realtime` | keep |
| `source/spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp` | Public embeddable runtime API | yes | no | GUI embed, API docs | `realtimeEngine/CMakeLists.txt` → `EngineSessionCore` | keep |
| `source/spatial_engine/realtimeEngine/src/Streaming.hpp` | Disk/ADM streaming | yes | no | `EngineSession.cpp`, `RealtimeBackend.hpp` | header-only via `EngineSessionCore` | keep |
| `source/spatial_engine/realtimeEngine/src/MultichannelReader.hpp` | ADM multichannel reader | yes | no | `Streaming.hpp`, docs | header-only via `EngineSessionCore` | keep |
| `source/spatial_engine/realtimeEngine/src/Pose.hpp` | Realtime pose interpolation/layout sanitization | yes | no | `RealtimeBackend.hpp`, docs | header-only via `EngineSessionCore` | keep |
| `source/spatial_engine/realtimeEngine/src/Spatializer.hpp` | Realtime DBAP render path/output routing | yes | no | `EngineSession.cpp`, `RealtimeBackend.hpp`, docs | header-only via `EngineSessionCore` | keep |
| `source/spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Audio callback/backend | yes | no | `EngineSession.cpp`, docs | header-only via `EngineSessionCore` | keep |
| `source/spatial_engine/realtimeEngine/src/OutputRemap.hpp` | Layout/CSV output remap | yes | no | `EngineSession.cpp`, `Spatializer.hpp`, docs | header-only via `EngineSessionCore` | keep |
| `source/gui/imgui/src/App.cpp` | GUI host embedding `EngineSession` | yes | no | GUI runtime path | `source/gui/imgui/CMakeLists.txt` | keep |
| `source/spatial_engine/src/JSONLoader.hpp/.cpp` | Shared LUSID scene parser | yes | yes | offline main, `EngineSession.cpp`, `Pose.hpp`, `Streaming.hpp` | realtime + offline CMake | keep shared |
| `source/spatial_engine/src/LayoutLoader.hpp/.cpp` | Shared speaker layout parser | yes | yes | offline main, `EngineSession.cpp`, `Pose.hpp`, `Spatializer.hpp` | realtime + offline CMake | keep shared |
| `source/spatial_engine/src/WavUtils.hpp/.cpp` | Shared WAV/RF64 I/O utilities | yes | yes | offline main, realtime CMake source list | realtime + offline CMake | keep shared |
| `source/spatial_engine/spatialRender/main.cpp` | Offline CLI entry point | no | yes | offline docs, CLI path | `spatialRender/CMakeLists.txt` → `spatialroot_spatial_render` | moved |
| `source/spatial_engine/spatialRender/SpatialRenderer.hpp/.cpp` | Offline spatial render orchestration | no | yes | offline docs, comments referenced by realtime historical notes | `spatialRender/CMakeLists.txt` → `spatialroot_spatial_render` | moved |

## What Moved

- `source/spatial_engine/src/main.cpp` → `source/spatial_engine/spatialRender/main.cpp`
- `source/spatial_engine/src/renderer/SpatialRenderer.hpp` → `source/spatial_engine/spatialRender/SpatialRenderer.hpp`
- `source/spatial_engine/src/renderer/SpatialRenderer.cpp` → `source/spatial_engine/spatialRender/SpatialRenderer.cpp`

## What Stayed

- All realtime engine files stayed in place
- Shared infrastructure stayed in `source/spatial_engine/src/`:
  - `JSONLoader.*`
  - `LayoutLoader.*`
  - `WavUtils.*`

## Remaining Ambiguities

- `source/spatial_engine/src/` still contains shared infrastructure used by both offline and realtime code, so it is not a legacy-only directory yet
- Some realtime logic in `Pose.hpp` and `Spatializer.hpp` was historically adapted from `SpatialRenderer`, but the realtime engine is self-contained and should remain so
- Historical docs may still mention older offline/VBAP paths when describing archived phases; those were left alone when clearly historical rather than current-layout guidance

## Build Commands And Results

### Baseline before edits

- `cmake -B build -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_ENGINE=ON -DSPATIALROOT_BUILD_OFFLINE=OFF -DSPATIALROOT_BUILD_CULT=OFF -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_DEVTOOLS=OFF .`
  - configure succeeded
- `cmake --build build --parallel 4`
  - build succeeded
- `cmake -B build-offline -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_ENGINE=OFF -DSPATIALROOT_BUILD_OFFLINE=ON -DSPATIALROOT_BUILD_CULT=OFF -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_DEVTOOLS=OFF .`
  - configure succeeded
- `cmake --build build-offline --parallel 4`
  - build succeeded

### Post-edit validation

- `cmake -B build -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_ENGINE=ON -DSPATIALROOT_BUILD_OFFLINE=OFF -DSPATIALROOT_BUILD_CULT=OFF -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_DEVTOOLS=OFF .`
  - configure succeeded
- `cmake --build build --parallel 4`
  - build succeeded

- `cmake -B build-offline -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_ENGINE=OFF -DSPATIALROOT_BUILD_OFFLINE=ON -DSPATIALROOT_BUILD_CULT=OFF -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_DEVTOOLS=OFF .`
  - configure succeeded
- `cmake --build build-offline --parallel 4`
  - build succeeded
  - linker emitted a pre-existing duplicate-library warning for `Gamma` and `libsndfile`, but the target linked successfully

- `cmake -B build-all -DCMAKE_BUILD_TYPE=Release -DSPATIALROOT_BUILD_ENGINE=ON -DSPATIALROOT_BUILD_OFFLINE=ON -DSPATIALROOT_BUILD_CULT=OFF -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_DEVTOOLS=OFF .`
  - configure succeeded
- `cmake --build build-all --parallel 4`
  - build succeeded
  - mixed build also showed the same duplicate-library warning on the offline target link step

### Stale-path checks

- `rg -n "source/spatial_engine/src/main.cpp" .`
  - matches only in this investigation report plus generated `build-offline/` dependency files from the earlier pre-move build tree
- `rg -n "source/spatial_engine/src/renderer/SpatialRenderer|src/renderer/SpatialRenderer" internalDocs PUBLIC_DOCS source/spatial_engine source/gui CMakeLists.txt`
  - matches only in this investigation report, where the old path is intentionally recorded as historical context
- `rg -n "\.\./src/main\.cpp|\.\./src/renderer/SpatialRenderer|#include \"\.\./JSONLoader\.hpp\"|#include \"\.\./LayoutLoader\.hpp\"|#include \"\.\./WavUtils\.hpp\"" source/spatial_engine internalDocs PUBLIC_DOCS CMakeLists.txt`
  - no matches

Result: no stale active source/CMake/doc references to the old offline locations were left behind outside historical reporting text and generated build artifacts.
