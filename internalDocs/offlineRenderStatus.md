# Spatial Root Offline Render Status and V2 Return Notes

_Last updated: May 2026_

## GUI frontend status (May 10, 2026)

The Offline Render tab is visible in the Dear ImGui GUI, but its controls are intentionally
hidden behind a frontend-only UI guard in `source/gui/imgui/src/App.cpp`:

```cpp
const bool kShowOfflineRenderControls = false;
```

**What this means:**

- The Offline Render tab appears in the GUI tab bar.
- The tab body shows "UNDER CONSTRUCTION" and an explanatory message.
- No usable render buttons, file pickers, or CULT-invocation controls are visible.
- The offline render backend, state variables, subprocess callbacks, CLI wiring, and
  internal render handlers remain intact in the code — nothing was deleted or rolled back.
- This is a frontend-only visibility decision, not a deletion or architectural rollback.

**To re-enable the controls** after the offline render path is validated, change the guard:

```cpp
const bool kShowOfflineRenderControls = true;
```

The hidden controls cover two modes: ADM WAV Offline Render (Experimental) and LUSID Package
Render. Both modes construct and fire `spatialroot_spatial_render` via the existing
`SubprocessRunner`. The backend state (`mOrAdmInput`, `mOrLayout`, `mOrOutput`,
`mOrLusidPackage`, `mOrLusidLayout`, `mOrLusidOutput`, `mOrRunner`, `mOrLog`, etc.) and
callbacks (`appendOrLog`, `findSpatialRenderer`, `tickEngine` polling, `requestShutdown`
cleanup) all remain active.

The controls are withheld because the offline render path has not yet been validated against
Spatial Root's realtime ADM/LUSID behavior and device-indexed output routing. See the
sections below for the full rationale and v2 return plan.

---

## Current decision

Offline rendering should be deferred from the main v1 scope of Spatial Root. It remains an important future capability, but it is currently causing enough engineering drag that it should not block the core project.

The v1 focus should remain:

```text
ADM / LUSID-derived scene data + speaker layout JSON -> realtime Spatial Root playback
```

The v2 target can be:

```text
ADM / LUSID-derived scene data + speaker layout JSON + render parameters -> deterministic rendered WAV
```

This keeps the current system focused on the main research and toolchain claim: Spatial Root can play spatial scene data through arbitrary speaker layouts in realtime, using a clear separation between transcoding, scene representation, layout description, and runtime rendering.

## Why offline render is deferred

Offline render is useful, but it is secondary to the main v1 proof. It adds value for reproducibility, testing, regression analysis, and export workflows, but it can also turn into a second architecture if handled too early.

The current risk is that offline render may require resolving several hard issues at once:

- parity with realtime playback behavior;
- deterministic timing and block processing;
- final-frame hold semantics;
- fast-mover handling;
- channel mapping and layout remapping;
- output WAV/BW64 writing;
- render parameter capture;
- transport, seek, and reset behavior;
- duplicated engine code paths;
- stale legacy batch-render code.

If these are not carefully scoped, offline render could pull effort away from the more important v1 goals: stable realtime playback, speaker-layout handling, GUI integration, CULT/LUSID interoperability, and reliable documentation.

## How to describe this in the thesis or paper

A safe framing is:

> Spatial Root is currently centered on realtime layout-adaptive playback. Offline rendering is treated as a planned extension of the same engine pathway rather than a separate rendering system. This keeps the present implementation focused on validating scene translation, speaker-layout adaptation, and runtime spatialization, while leaving deterministic batch export as future work.

Another concise version:

> Offline rendering is not presented as a primary v1 feature. It is retained as a future headless execution mode that should reuse the same scene loading, speaker-layout mapping, and spatialization path as realtime playback.

## Current v1 boundary

### In scope for v1

- Realtime playback from LUSID or ADM-derived scene data.
- Speaker layout JSON as an explicit runtime input.
- Correct handling of nonlinear / non-contiguous device channels.
- Separation between internal render bus and hardware output bus.
- Stable DBAP behavior for irregular arrays.
- Runtime controls such as master gain, loudspeaker gain, sub gain, DBAP focus, and elevation mode.
- GUI session as the owner of user-facing orchestration.
- CULT handling as currently implemented in the GUI session.
- BEAR, if integrated, should remain a separate subprocess invoked from the GUI session, not a new engine-session bridge.

### Out of scope for v1

- A polished offline batch renderer.
- A separate offline rendering architecture.
- DAW-like export controls.
- Complex timeline automation.
- Multiple export format presets.
- Full regression-render infrastructure.
- Rewriting engine transport semantics just to support offline rendering.
- Adding a new CULT bridge solely for offline render.

## Desired v2 principle

Offline render should eventually become a headless execution mode of the same engine core used by realtime playback.

The goal should not be:

```text
old batch renderer + separate spatialization path
```

The goal should be:

```text
shared engine core
  -> realtime callback mode
  -> offline/headless render mode
```

This matters because offline render is only valuable if it reflects what Spatial Root actually does during playback. A separate renderer that diverges from realtime behavior would weaken testing, documentation, and research claims.

## Current architectural direction

The preferred future architecture is:

```text
Input scene package or ADM-derived scene
        |
        v
Scene loading / validation
        |
        v
Speaker layout loading
        |
        v
Internal render bus mapping
        |
        v
Shared spatialization engine
        |
        +----------------------+
        |                      |
        v                      v
Realtime audio backend     Offline WAV writer
```

The realtime and offline modes should share as much as possible:

- scene parsing;
- LUSID frame evaluation;
- object timeline behavior;
- layout remapping;
- DBAP gain calculation;
- elevation handling;
- channel masks;
- final-frame hold behavior;
- master/loudspeaker/sub gain application;
- diagnostic reporting where possible.

The offline path should differ only at the endpoint: instead of writing to a hardware device callback, it writes sample blocks into an output file.

## What needs investigation before v2 implementation

When returning to this feature, first audit the old render code before writing new code.

Questions to answer:

1. Which code is used by the current realtime audio engine?
2. Which code belongs only to the old batch renderer?
3. Which old batch-render pieces are still useful?
4. Which pieces duplicate current engine behavior and should be deleted or retired?
5. Can the old offline path be reduced to a thin wrapper around the current engine core?
6. Does offline render currently use the same DBAP code as realtime playback?
7. Does offline render respect the current speaker layout JSON schema?
8. Does offline render correctly distinguish internal render-bus channels from device-output channels?
9. Does offline render preserve final-frame hold semantics?
10. Does offline render apply the same parameter defaults as realtime playback?

## Likely useful pieces to preserve

Potentially reusable parts from old batch-render code may include:

- WAV output writing helpers;
- block-based render loops;
- render progress reporting;
- test harnesses;
- command-line argument parsing patterns;
- diagnostic file emission;
- old examples that reveal intended behavior.

Potentially risky parts include:

- duplicated DBAP implementation;
- hardcoded channel assumptions;
- assumptions of contiguous device channels;
- stale layout formats;
- separate scene parsing logic;
- offline-only timeline evaluation;
- code that bypasses current engine state or parameter smoothing;
- direct assumptions about ADM channel order or bed/object layout.

## Proposed v2 minimum viable feature

The first successful v2 offline render should be intentionally small:

```text
spatialroot-render \
  --scene path/to/scene.lusid.json \
  --audio path/to/audio_or_package \
  --layout path/to/layout.json \
  --out path/to/rendered.wav
```

Minimum behavior:

- load the same scene input used by realtime playback;
- load the same speaker layout JSON used by realtime playback;
- render through the same DBAP/spatialization path;
- write a multichannel WAV matching the layout's logical output channel count;
- use explicit render parameters or documented defaults;
- produce deterministic output for the same input;
- fail clearly if unsupported input is provided.

The first version does not need:

- GUI controls;
- batch queues;
- ADM/BW64 export;
- advanced file naming;
- render presets;
- real-time progress UI;
- parameter automation;
- sample-accurate seeking.

## Suggested render parameters for v2

A simple v2 CLI/API should capture these explicitly:

- input scene or package path;
- speaker layout path;
- output WAV path;
- sample rate, probably defaulting to source or 48 kHz;
- block size;
- master gain;
- loudspeaker gain;
- sub gain;
- DBAP focus;
- elevation mode;
- optional duration override;
- optional start time, if seeking becomes safe;
- diagnostic/report output path.

Avoid hidden state. A render should be reproducible from the command line arguments and input files.

## Relationship to BEAR

BEAR should not be used to complicate the Spatial Root engine session in v1.

The current preferred direction is:

- BEAR can remain a separate binary or external tool.
- The GUI session may invoke it when needed.
- The engine session should remain focused on Spatial Root playback.
- CULT handling should stay as currently implemented in the GUI session.
- A cleaner bridge abstraction can be considered later, but should not be introduced just for v1 offline render.

For v2, BEAR may be relevant if the project needs an external ADM-oriented render/reference path. However, Spatial Root's own offline render should still be defined as a headless mode of Spatial Root, not as a BEAR replacement or wrapper unless that becomes an explicit design choice.

## Recommended future implementation sequence

1. Freeze the v1 boundary and mark offline render as future work.
2. Move or quarantine legacy batch-render code so it does not confuse the current engine architecture.
3. Document which current files are realtime engine code and which are old batch-render code.
4. Extract shared engine logic only where it directly reduces duplication.
5. Build a minimal headless render loop around the current engine core.
6. Write a simple multichannel WAV output path.
7. Compare offline output against realtime engine expectations using short test scenes.
8. Add regression tests only after the basic path is stable.
9. Add GUI access only after the CLI/headless path is reliable.

## Suggested repository documentation note

Add a short note to AGENTS.md or development history:

> Offline rendering is deferred from the v1 feature set. The intended future direction is a headless render mode that reuses the same Spatial Root scene loading, layout handling, channel mapping, and spatialization path as realtime playback. Legacy batch-render code should be treated as reference material only until audited. Do not build a separate offline rendering architecture or introduce new GUI-to-engine bridge abstractions solely for offline rendering.

## Agent onboarding prompt for future return

Use this prompt when returning to the feature:

```text
You are working in the Spatial Root repository. Before implementing offline rendering, read AGENTS.md, the development history, and the current engine/session documentation. The current project decision is that offline render was deferred from v1 because it was creating engineering drag and risked becoming a separate architecture. Your task is to audit the existing realtime engine code and any legacy batch-render/offline-render code, then propose the smallest v2 path toward a headless offline render that reuses the current Spatial Root engine core.

Do not create a separate spatialization implementation. Do not add a new CULT bridge. Do not move CULT handling out of the GUI session unless the existing documentation explicitly says this has changed. Do not assume device channels are contiguous. Preserve the distinction between internal render-bus channels and device-output channels. Treat nonlinear speaker layouts as supported.

First, identify which files are used by the current realtime engine and which files belong only to the old batch renderer. Then identify any reusable pieces, such as WAV writing helpers, block render loops, diagnostics, or CLI parsing. Mark duplicated DBAP, stale layout assumptions, or hardcoded channel-order assumptions as risks.

The desired v2 design is a minimal headless command such as:

spatialroot-render --scene scene.lusid.json --audio audio/ --layout layout.json --out rendered.wav

The offline path should share scene loading, speaker layout handling, channel mapping, elevation handling, DBAP gain computation, final-frame hold semantics, and render parameters with realtime playback. It should differ only in the endpoint: writing rendered blocks to a WAV file instead of sending them to a realtime audio backend.

Deliverables:
1. A short audit of current realtime vs legacy offline code.
2. A list of safe reusable pieces from the old batch renderer.
3. A list of risky/stale pieces to avoid or delete.
4. A minimal v2 architecture proposal.
5. A staged implementation plan that does not disturb the v1 realtime engine.
6. Updates to AGENTS.md and development history documenting the decision and future direction.
```

## Final recommendation

Do not remove the idea of offline render from the project. Defer it.

For v1, treat offline render as future work and keep the public claim centered on realtime layout-adaptive playback. For v2, return to offline render only as a headless mode that shares the same engine path as realtime playback.
