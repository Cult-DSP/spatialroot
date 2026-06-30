# Future Work

This is the canonical future-work tracker for Spatial Root maintainers. Keep this file for active backlog themes and short forward-looking notes; leave historical decision logs in the dated audit documents.

## Internal Host Bus

- add a host-render smoke test: load known content, call `renderHostBlock()` for several blocks, verify output is nonzero and NaN-free, optionally dump a WAV. Blocked on selecting a stable test input/layout pair. See [HOST_RENDER_BACKEND.md](HOST_RENDER_BACKEND.md).

## Spatialization & Rendering

- continue offline rendering parity work without widening the realtime-engine ownership boundary
- validate final-frame hold, sparse-channel routing, ADM-driven source mapping, and parameter-default parity in the offline renderer
- keep the first future offline return small: scene/package input, layout input, deterministic multichannel WAV output, explicit render parameters
- move spatial transformation math into a separate file if that still reduces engine coupling
- tune DBAP presence for large spaces
- if DBAP focus compensation is revisited, pick an explicit reference focus first and document the preserved metric before reintroducing any automatic trim
- allow DBAP focus metadata in layout or LUSID inputs only after the contract is stable
- add binaural mixdown support

### Offline Return Checklist

- keep the GUI Offline Render controls hidden until parity work is intentionally resumed; the existing guard is `kShowOfflineRenderControls = false`
- confirm ownership boundaries before editing: realtime in `source/spatial_engine/realtimeEngine/`, offline in `source/spatial_engine/spatialRender/`, shared loaders in `source/spatial_engine/src/`
- verify offline routing still matches the two-space model: compact internal bus, sparse final output width, silent unmapped channels
- test `--adm` and package-driven flows separately
- check final-frame hold semantics, LFE routing, `direct_speaker` behavior, and default parameter parity against realtime
- avoid expanding scope into DAW-style export features or a second standalone rendering architecture

## GUI & Workflow

- improve the Dear ImGui GUI presentation and add visualization where it helps debugging
- queue multiple files in the GUI workflow
- add test coverage for `DefaultLayoutManager`, especially save/load/clear behavior under `SPATIALROOT_SETTINGS_ROOT`
- validate the default-layout settings path on a real Windows build

## Release & Packaging

- add release signing/notarization on top of the existing bundle/install work
- evaluate Sigstore / `cosign` for artifact provenance
- plan Apple-signing/notarization requirements for macOS distribution
- evaluate OSS-friendly Windows signing options if distribution expands there

## Transcoding & Research

- investigate ambisonic encoding workflows
- evaluate the JACK-based reference at <https://www.matthiaskronlachner.com/?p=2015>
- keep these external references handy for future research:
  - <https://www.matthiaskronlachner.com/wp-content/uploads/2013/01/ICSA2014_KronlachnerZotter_AmbisonicTransformations.pdf>
  - <https://developer.apple.com/accelerate/>
  - <https://fftw.org/>
  - <https://github.com/orgs/Signalsmith-Audio/repositories>
