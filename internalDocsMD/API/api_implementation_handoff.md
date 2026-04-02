# API Implementation Handoff
**Status:** Historical / Legacy Maintenance Reference
*(Warning: This document reflects the migration from CLI-based architectures to the `EngineSession` API. Do not use this as a design guide for new features. See `api_internal_contract.md`)*

## Phase 2 Refactor Outcomes
The extraction of `EngineSession` isolated the `main.cpp` CLI wrapper from the core engine execution. 

**Completed Milestones:**
- [x] Extracted `EngineConfig`, `SceneConfig`, `LayoutConfig`.
- [x] Abstracted AlloLib parameter lifetimes into isolated OSC context.
- [x] Implemented `getLastError()` for synchronized failure propagation.
- [x] Re-ordered shutdown phase to prevent ASIO/CoreAudio thread hangs (`mParamServer` -> `mBackend` -> `mStreaming`).

**Demoted / Retired Concepts:**
- Earlier drafts requested dynamic reloading and arbitrary playhead seeking. These were deemed unsafe given the current buffer architecture and have been scrubbed from the active target API.
- CLI-only debugging flags (e.g., specific stdout verbosity traces) were left in `main.cpp` and explicitly not ported to `EngineSession`.
