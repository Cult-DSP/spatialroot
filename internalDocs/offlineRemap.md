Investigation Report

I did not modify code, build files, or runtime behavior. I also did not run builds/tests because this pass was investigation-only.

1. Current Architecture Summary

Realtime path: GUI or realtime CLI both end up in EngineSession: configureEngine → loadScene → applyLayout → configureRuntime → start in EngineSession.cpp (line 163). start() wires Streaming, Pose, Spatializer, and RealtimeBackend, then starts the loader thread and AudioIO.

Scene loading path: LUSID is parsed by JSONLoader::loadLusidScene() in JSONLoader.cpp (line 51). audio_object and direct_speaker both become spatialized sources keyed by node id; LFE becomes the special "LFE" source.

Layout loading path: JSON speaker layouts are parsed by LayoutLoader::loadLayout() in LayoutLoader.cpp (line 7). It reads speaker/subwoofer channel values into deviceChannel.

Audio streaming/render path: realtime uses Streaming with double-buffered mono-file streaming or ADM direct streaming via MultichannelReader in Streaming.hpp (line 530) and Streaming.hpp (line 587). Offline uses full-file loads via WavUtils and renders in-memory through SpatialRenderer in SpatialRenderer.cpp (line 676).

Output/remapping path: realtime remapping lives in Spatializer::init() and Spatializer::renderBlock() in Spatializer.hpp (line 165) and Spatializer.hpp (line 397), backed by OutputRemap.hpp (line 58). Offline renderer does not use this two-space routing model.

CLI structure: there is no shared command registry. There are separate binaries with hand-rolled main.cpp parsing in realtimeEngine/main.cpp (line 99) and spatialRender/main.cpp (line 89).

GUI structure: the top-level tab bar is in App.cpp (line 244) with only ENGINE and TRANSCODE. A future offline tab would be another BeginTabItem(...) plus new state/render methods in App.hpp (line 220).

2. File Map

EngineSession.hpp (line 90): embeddable engine API and lifecycle boundary.

EngineSession.cpp (line 163): active orchestration for realtime playback; also startup failure-diagnostics capture.

Streaming.hpp (line 530): realtime source loading, ADM direct mode, loader thread, lock-free callback reads.

Pose.hpp (line 120): keyframe interpolation, elevation sanitization, DBAP position prep.

Spatializer.hpp (line 165): realtime DBAP/LFE render path, layout validation, routing, diagnostics.

OutputRemap.hpp (line 180): layout-derived or legacy-CSV internal→output routing table.

RealtimeBackend.hpp (line 93): AudioIO init/start/stop and callback pipeline.

JSONLoader.cpp (line 51): shared LUSID parser for realtime and offline.

LayoutLoader.cpp (line 7): shared layout parser.

WavUtils.cpp (line 136): shared WAV/RF64 writer; also ADM channel extraction helpers.

spatialRender/main.cpp (line 89): current offline CLI entry.

SpatialRenderer.cpp (line 923): current offline render implementation.

App.cpp (line 999): GUI start path, transcoder subprocess use, engine launch, tabs.

SpatialRootPaths.cpp (line 84): temp-session root, manifest, safe deletion rules.

devHistory.md (line 70): confirms old offline code moved out of source/spatial_engine/src/.

spatialRender_split_report_2026-05-08.md (line 23): best map of current vs old offline locations.

3. Offline Rendering Feasibility

Smallest viable path already exists: spatialroot_spatial_render can load LUSID + mono stems or --adm, render offline, and write WAV/RF64.

The gap is not “can we render offline”; it is “can we make offline output follow current realtime routing/layout semantics without touching realtime code.”

That is feasible, but current offline code diverges from realtime in the most important place: output channel/routing behavior. Realtime uses a compact internal bus plus a layout-derived sparse output bus. Offline mostly renders speakers to consecutive indices and only honors layout subwoofer channel indices.

Reusing realtime behavior exactly without touching realtime code is possible if offline adds its own routing/validation stage or consumes OutputRemap as a readonly helper. Reusing Spatializer directly would be a bigger coupling step and not the smallest path.

4. Reusable Components

Directly reusable: JSONLoader, LayoutLoader, WavUtils::loadSources, WavUtils::loadSourcesFromADM, WavUtils::writeMultichannelWav.

Likely reusable with care: OutputRemap for offline internal→output routing.

Reusable concepts, but not clean shared utilities yet: realtime layout validation in Spatializer::init(), pose/elevation logic in Pose.hpp, and DBAP/LFE handling patterns in Spatializer.hpp.

CULT transcoder integration already exists at the app/workflow level via subprocess in App.cpp (line 1003); the engine itself does not depend on it.

5. Risky Components

Do not touch for Phase 1: Spatializer.hpp (line 165), RealtimeBackend.hpp (line 371), Streaming.hpp (line 530), Pose.hpp (line 120), EngineSession.cpp (line 163).

Also avoid changing GUI lifecycle code in App.cpp (line 1040) for the first offline pass.

WavUtils::writeMultichannelWav() is usable but risky for very large renders because it allocates one full interleaved buffer before writing.

6. Legacy Batch Renderer Assessment

source/spatial_engine/spatialRender/main.cpp and SpatialRenderer.\*: partially reusable. They are the current offline renderer, but not realtime-parity-correct on routing/remap semantics.

Historical old locations source/spatial_engine/src/main.cpp and source/spatial_engine/src/renderer/SpatialRenderer.\*: obsolete as paths; they were moved, per spatialRender_split_report_2026-05-08.md (line 33).

JSONLoader::loadSpatialInstructions(): obsolete for new work; old schema compatibility only.

Historical Python pipeline (runPipeline.py, stem-splitting flow in devHistory.md): obsolete.

Legacy CSV remap files under source/spatial_engine/remaping/: partially reusable as internal validation artifacts only; not the target routing model.

I did not find any current on-disk OfflineRender* or OfflineRenderRemapPlan* source files in this checkout, so any plan depending on those names is unclear/not present.

7. Remapping Assessment

Internal render-bus indices in realtime: 0..numSpeakers-1 are main speakers; numSpeakers..numSpeakers+numSubs-1 are compact internal subwoofer channels in Spatializer.hpp (line 251).

Device/output indices in realtime: physical output width is max(deviceChannel)+1, stored in mConfig.outputChannels in Spatializer.hpp (line 284).

Non-contiguous device channels in realtime: supported via OutputRemap scatter routing; gaps are allowed.

Silent/unused output channels in realtime: explicitly zeroed in scatter mode inside renderBlock(), so unmapped channels stay silent.

Subwoofer/LFE in realtime: LFE bypasses DBAP and is written directly to compact internal subwoofer channels, then routed to output deviceChannels.

direct_speaker handling in realtime: not special-cased. JSONLoader treats direct_speaker the same as audio_object, so it is still spatialized rather than directly routed to a designated output.

Offline remapping today: speaker deviceChannel values are intentionally ignored for main speakers; offline uses consecutive speaker indices, then places subs at layout subwoofer channel numbers in SpatialRenderer.cpp (line 801). That is the main mismatch.

Offline silent channels: because offline output width can be expanded by subwoofer indices, silent gap channels can exist, but only subwoofer sparse indices are honored; main-speaker sparse deviceChannels are not.

8. Proposed Phased Implementation

Phase 1: goal = add an offline-only layout validation + routing-plan helper that mirrors realtime’s two-space model; likely files = new offline-owned helper under source/spatial_engine/spatialRender/; must not change = all realtime files; validation = load representative layouts and print/inspect internal→output mapping; deliverable = deterministic remap plan and validation errors.

Phase 2: goal = make offline renderer render to a compact internal bus and then route to sparse output channels using the Phase 1 plan; likely files = SpatialRenderer.hpp/.cpp; must not change = realtime engine, public EngineSession API; validation = compare output channel counts and silence gaps against realtime layout expectations; deliverable = offline WAV channel layout parity.

Phase 3: goal = align offline input semantics with current supported workflows, especially --adm, LFE, and documented layout behavior; likely files = spatialRender/main.cpp, SpatialRenderer._, maybe WavUtils._; must not change = realtime lifecycle/API; validation = render same scene from package and ADM paths and inspect channel assignment; deliverable = documented supported offline input matrix.

Phase 4: goal = tighten semantics around direct_speaker and any remaining parity gaps; likely files = offline-owned files plus maybe new shared helper if truly needed; must not change = stable realtime DSP path unless a future dedicated pass approves it; validation = targeted scene fixtures; deliverable = explicit behavior contract for direct-speaker content.

9. Recommended Phase 1

The absolute smallest first implementation step is not “add a new renderer”; it is “add an offline-only routing/validation plan layer.”

Reason: offline rendering already exists, but the biggest architectural mismatch is routing semantics, and that can be isolated without touching realtime playback.

Concretely, the first implementation prompt should target a new offline-owned helper that:
loads SpeakerLayoutData,
validates deviceChannel uniqueness/ranges using the same rules as realtime,
produces internalChannelCount, outputChannelCount, and internal→output entries,
emits a dry-run/loggable plan without changing audio rendering yet.

That gives you a safe wedge: once the plan matches realtime semantics, Phase 2 can wire the existing offline renderer through it with minimal surface area.
