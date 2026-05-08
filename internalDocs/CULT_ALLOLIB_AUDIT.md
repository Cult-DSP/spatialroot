# CULT_ALLOLIB Audit

## Summary

This audit was performed against `internal/cult-allolib` as used by Spatial Root on macOS.

Spatial Root currently uses `cult-allolib` as a runtime support library for:

- audio device I/O via `al::AudioIO` and `al::AudioIOData`
- parameter control via `al::Parameter`, `al::ParameterBool`, and `al::ParameterServer`
- OSC transport used by `ParameterServer`
- math/types including `al::Vec3f`
- speaker layout and spatialization primitives including `al::Speaker`, `al::Spatializer`, `al::Dbap`, `al::Vbap`, and `al::Lbap`
- low-level support required by the above: thread/time utilities, sockets, file helpers, pose/type helpers

Spatial Root does not currently include or link against AlloLib graphics, windowing, OpenGL app infrastructure, VR, examples, or tests in its engine/offline build path.

## Direct Spatial Root Evidence

Direct `al/` includes found in Spatial Root:

- `source/spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`: `al/io/al_AudioIO.hpp`
- `source/spatial_engine/realtimeEngine/src/EngineSession.cpp`: `al/ui/al_Parameter.hpp`, `al/ui/al_ParameterServer.hpp`
- `source/spatial_engine/realtimeEngine/src/main.cpp`: `al/io/al_AudioIO.hpp`
- `source/spatial_engine/realtimeEngine/src/Pose.hpp`: `al/math/al_Vec.hpp`
- `source/spatial_engine/realtimeEngine/src/Spatializer.hpp`: `al/io/al_AudioIO.hpp`, `al/sound/al_Dbap.hpp`, `al/sound/al_Speaker.hpp`
- `source/spatial_engine/src/LayoutLoader.hpp`: `al/sound/al_Speaker.hpp`
- `source/spatial_engine/src/renderer/SpatialRenderer.hpp`: `al/math/al_Vec.hpp`, `al/sound/al_Vbap.hpp`, `al/sound/al_Dbap.hpp`, `al/sound/al_Lbap.hpp`, `al/sound/al_Spatializer.hpp`, `al/io/al_AudioIOData.hpp`
- `source/spatial_engine/src/vbap_src/VBAPRenderer.hpp`: `al/math/al_Vec.hpp`, `al/sound/al_Vbap.hpp`, `al/io/al_AudioIOData.hpp`

Direct target links found in Spatial Root:

- `source/spatial_engine/realtimeEngine/CMakeLists.txt`: `al`, `Gamma`, `SndFile::sndfile`
- `source/spatial_engine/spatialRender/CMakeLists.txt`: `al`, `Gamma`, `SndFile::sndfile`

## Dependency Notes

- `al/ui/al_Parameter.hpp` pulls in `al/protocol/al_OSC.hpp`, `al/spatial/al_Pose.hpp`, `al/types/al_Color.hpp`, and `al/types/al_VariantValue.hpp`.
- `al/ui/al_ParameterServer.cpp` additionally uses `al/io/al_Socket.hpp`.
- `al/ui/al_Parameter.cpp` additionally uses `al/io/al_File.hpp`.
- `al/protocol/al_OSC.hpp` depends on `al/system/al_Thread.hpp` and `al/system/al_Time.hpp`.
- `al/sound/al_Dbap.hpp`, `al_Vbap.hpp`, `al_Lbap.hpp`, and `al_Spatializer.hpp` depend on `al/math/al_Vec.hpp`, `al/sound/al_Speaker.hpp`, `al/spatial/al_Pose.hpp`, and `al/io/al_AudioIOData.hpp`.
- GUI code in `source/gui/imgui/src/App.cpp` uses `al/io/al_AudioIO.hpp`, but the GUI's actual graphics stack is native `GLFW`/OpenGL/ImGui rather than AlloLib graphics.

## Audit Table

| Component/module | Purpose | Used by Spatial Root? | Evidence | Required headers or source files | Required CMake target | Keep, disable, or remove | Risk |
|---|---|---:|---|---|---|---|---|
| Core math/types/system | `Vec`, `Pose`, time/thread, color, variant helpers | yes | `al/math/al_Vec.hpp`, `al/spatial/al_Pose.hpp`, `al/protocol/al_OSC.hpp` include chain | `src/spatial/al_Pose.cpp`, `src/system/al_*`, `src/types/al_*` | `cult_allolib_core` | keep | low |
| File utilities | file/path helpers used by parameter code | yes | `src/ui/al_Parameter.cpp` includes `al/io/al_File.hpp` | `src/io/al_File.cpp` | `cult_allolib_file` | keep | low |
| Audio I/O | runtime device/backend access | yes | `RealtimeBackend.hpp`, `main.cpp`, `Spatializer.hpp` include `al/io/al_AudioIO.hpp` | `src/io/al_AudioIO.cpp`, `src/io/al_AudioIOData.cpp` | `cult_allolib_audio` | keep | low |
| OSC protocol | packet send/receive used by parameter server | yes | `al/ui/al_Parameter.hpp` and `al/ui/al_ParameterServer.hpp` include `al/protocol/al_OSC.hpp` | `src/protocol/al_OSC.cpp` | `cult_allolib_osc` | keep | low |
| Parameters | realtime controls and OSC server | yes | `EngineSession.cpp` uses `al::Parameter`, `al::ParameterBool`, `al::ParameterServer` | `src/ui/al_Parameter.cpp`, `src/ui/al_ParameterBundle.cpp`, `src/ui/al_ParameterServer.cpp`, `src/io/al_Socket.cpp` | `cult_allolib_params` | keep | low |
| Spatialization | speaker layouts and DBAP/VBAP/LBAP support | yes | `SpatialRenderer.hpp`, `Spatializer.hpp`, `VBAPRenderer.hpp` include DBAP/VBAP/LBAP headers | `src/sound/al_Dbap.cpp`, `al_Vbap.cpp`, `al_Lbap.cpp`, `al_Spatializer.cpp`, `al_Speaker.cpp` | `cult_allolib_spatial` | keep | low |
| Gamma | libsndfile exposure plus audio utility dependency already linked by Spatial Root | yes | Spatial Root links `Gamma` directly; comments in `Streaming.hpp`/`MultichannelReader.hpp` rely on it for `sndfile.h` visibility | external `Gamma` | `Gamma` | keep | medium |
| RtAudio | actual backend for `al::AudioIO` on current build path | yes | `al_AudioIO.cpp` compiles with `AL_AUDIO_RTAUDIO` by default | external `rtaudio` | transitively via `cult_allolib_audio` | keep | low |
| oscpack | underlying OSC transport library | yes | `al_OSC.cpp` uses oscpack | external `oscpack` | transitively via `cult_allolib_osc` | keep | low |
| Serial | serial device support for disabled AlloLib serial APIs | no for current Spatial Root build | only referenced by `al_SerialIO.*`; not used by `ParameterServer` or Spatial Root sources | external `serial` only | none in minimal build path | disable | low |
| cpptoml | TOML parser for disabled config helpers | no for current Spatial Root build | only referenced by `al_Toml.*` and `PersistentConfig`; not used by Spatial Root sources | external `cpptoml` only | none in minimal build path | disable | low |
| RtMidi / AlloLib MIDI | MIDI input helpers | no | no Spatial Root include or symbol evidence found | `include/al/io/al_MIDI.hpp`, `src/io/al_MIDI.cpp` | none in minimal build path | disable | low |
| Graphics / OpenGL | AlloLib rendering stack | no | no Spatial Root engine/offline includes of `al/graphics/*`, `al_Window`, `al_App`, `al_Graphics`, `al_Shader`, `al_Mesh` | `src/graphics/*`, `src/io/al_Window*` | `cult_allolib_graphics` | disable | low |
| App/domain infrastructure | windowed app/runtime domains | no | no engine/offline includes of `al/app/*`; GUI uses native GLFW/OpenGL | `src/app/*` | `cult_allolib_app` | disable | low |
| VR / omni renderer | AlloSphere/VR-specific support | no | no Spatial Root includes or links | `al_OmniRendererDomain`, sphere helpers | none in minimal build path | disable | low |
| Examples | sample programs only | no | only referenced from `internal/cult-allolib/examples/CMakeLists.txt` | `examples/` | `ALLOLIB_BUILD_EXAMPLES` | disable, quarantine | low |
| Tests | upstream verification only | no | only referenced from `internal/cult-allolib/test/CMakeLists.txt` | `test/` | `ALLOLIB_BUILD_TESTS` | disable, quarantine | low |
| Docs / tutorial assets | docs/examples support | no | no Spatial Root references | docs/assets | none in minimal build path | disable | low |

## Build-System Changes

`internal/cult-allolib/CMakeLists.txt` now exposes a Spatial Root-oriented module split:

- `cult_allolib_core`
- `cult_allolib_file`
- `cult_allolib_audio`
- `cult_allolib_osc`
- `cult_allolib_spatial`
- `cult_allolib_params`
- `cult_allolib_graphics`
- `cult_allolib_app`
- compatibility target: `al`

New CMake options:

- `CULT_ALLOLIB_ENABLE_AUDIO`
- `CULT_ALLOLIB_ENABLE_OSC`
- `CULT_ALLOLIB_ENABLE_PARAMETERS`
- `CULT_ALLOLIB_ENABLE_SPATIAL`
- `CULT_ALLOLIB_ENABLE_GRAPHICS`
- `CULT_ALLOLIB_ENABLE_APP`
- `CULT_ALLOLIB_ENABLE_VR`
- `CULT_ALLOLIB_ENABLE_MIDI`
- `CULT_ALLOLIB_ENABLE_SERIAL`
- `CULT_ALLOLIB_ENABLE_FILE`
- `CULT_ALLOLIB_ENABLE_TIMING`
- `CULT_ALLOLIB_ENABLE_TYPES`
- `CULT_ALLOLIB_ENABLE_EXAMPLES`
- `CULT_ALLOLIB_ENABLE_TESTS`
- `CULT_ALLOLIB_ENABLE_DOCS`

Default minimal profile used for Spatial Root:

- ON: audio, osc, parameters, spatial, file, timing, types
- OFF: graphics, app, VR, MIDI, examples, tests, docs

Result: a minimal Spatial Root build no longer configures `OpenGL`, `glfw`, `glad`, AlloLib graphics, or AlloLib app-domain code.
Result: the verified minimal path also no longer builds or links `serial` or `cpptoml`.

## Quarantine / Removal Status

This pass intentionally used the conservative sequence:

1. audit current usage
2. split the build graph
3. verify Spatial Root builds against the minimal subset
4. quarantine unused subsystems behind OFF-by-default options

This pass physically removed:

- `internal/cult-allolib/.github/workflows/`
- `internal/cult-allolib/doc/`
- `internal/cult-allolib/examples/`
- `internal/cult-allolib/test/`
- `internal/cult-allolib/include/al/sphere/Untitled Document`
- `internal/cult-allolib/src/sphere/Untitled Document`

Those trees were already outside the verified Spatial Root build path. The top-level `cult-allolib` CMake now errors clearly if examples or tests are re-enabled without restoring those directories.

The following remain quarantined behind OFF-by-default options rather than being deleted:

- graphics/windowing code in `src/graphics/` and `src/io/al_Window*`
- app/domain infrastructure in `src/app/`
- nested submodule vendor content such as `internal/cult-allolib/external/glfw/{examples,tests,docs}`
- other third-party vendor example/test content not referenced by the minimal build

Deferred future work:

- coordinated removal of disabled `include/al/app` + `src/app`
- coordinated removal of disabled `include/al/graphics` + `src/graphics`
- coordinated removal of disabled window/imgui support files
- review of non-critical sphere helpers beyond the stray placeholder files
- review of vendored `external/*/examples` trees, especially where the vendor is not a nested submodule

## Verification

Platform tested:

- macOS
- AppleClang 15
- CMake 3.20+ compatible top-level project

Commands run:

```bash
cmake -S . -B /private/tmp/spatialroot-audit-build2 -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_CULT=OFF
cmake --build /private/tmp/spatialroot-audit-build2 --target spatialroot_realtime spatialroot_spatial_render -j4

cmake -S . -B /private/tmp/spatialroot-audit-build7 -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_CULT=OFF
cmake --build /private/tmp/spatialroot-audit-build7 --target spatialroot_realtime spatialroot_spatial_render -j4
```

Observed results:

- clean configure succeeded
- `spatialroot_realtime` built successfully
- `spatialroot_spatial_render` built successfully
- no `OpenGL`/`glfw`/`glad` configure requirement remained in the minimal build

Warnings seen:

- upstream deprecation warnings from `RtAudio`, `oscpack`, and some legacy AlloLib source files
- duplicate-library linker warning for direct `Gamma`/`libsndfile` linkage in `spatialroot_spatial_render`

## Remaining Uncertainties / Follow-up

- `Gamma` is still built because Spatial Root links it directly and expects its exported `libsndfile` visibility/comments; removing or replacing that dependency should be a separate pass.
- `cpptoml` and `serial` were removed from the verified minimal build path, but their source trees remain vendored for now because disabled AlloLib subsystems may still use them when re-enabled.
- If the team wants a harder prune next, the next safest step is to inspect other vendored third-party sample/test trees that are not entered by the minimal build, for example optional content under `external/imgui`, `external/json`, or `external/cpptoml`, and prune only those with the same verify-after-each-step approach.
