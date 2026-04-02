# Development History

**Last Updated:** March 31, 2026

## Transition from `spatialroot_adm_extract` to `cult-transcoder`

### Overview

In Phase 3 of the project (March 2026), the ADM extraction functionality was transitioned from the deprecated `spatialroot_adm_extract` binary to the `cult-transcoder` tool. This change was made to streamline the pipeline and leverage the enhanced capabilities of `cult-transcoder`.

### Old System: `spatialroot_adm_extract`

- **Purpose**: Extracted ADM XML metadata from BW64/RF64/WAV files.
- **Implementation**: Built using EBU libraries (`libbw64` and `libadm`).
- **Output**: `processedData/currentMetaData.xml`.
- **Limitations**:
  - Required a separate build step.
  - Limited to ADM extraction without additional processing capabilities.

### New System: `cult-transcoder`

- **Purpose**: Handles ADM extraction and additional transcoding tasks.
- **Implementation**: Integrated into the `cult-transcoder` binary.
- **Usage**: `cult-transcoder transcode --in-format adm_wav`.
- **Advantages**:
  - Simplified pipeline with fewer dependencies.
  - Enhanced functionality beyond ADM extraction.
  - Direct integration with the realtime workflow (historical; the Python launcher was removed in Phase 6).

### Documentation Updates

- `AGENTS.md` updated to reflect the transition.
- Deprecated references to `spatialroot_adm_extract` removed from the codebase.
- Pipeline steps updated to use `cult-transcoder`.

### Future Considerations

- Ensure all team members are familiar with the `cult-transcoder` usage.
- Monitor for any issues arising from the transition and address them promptly.

---

## Key Milestones

### Phase 1: Initial Implementation

- **Date**: January 2026
- **Description**: Initial implementation of the ADM extraction pipeline using `spatialroot_adm_extract`.

### Phase 2: Audit and Cleanup

- **Date**: February 2026
- **Description**: Audited the codebase and identified areas for improvement, including the deprecation of `spatialroot_adm_extract`.

### Phase 3: Transition to `cult-transcoder`

- **Date**: March 2026
- **Description**: Fully transitioned to `cult-transcoder` for ADM extraction and removed all references to `spatialroot_adm_extract`.

---

### Phase 6: C++ Refactor and ImGui GUI

- **Date**: March 31, 2026
- **Description**: Python build system replaced by `init.sh` and `build.sh`. `EngineSessionCore` static library hardened with V1.1 API, adding runtime setters, a typed `ElevationMode`, and an `oscPort=0` guard. Python PySide6 GUI replaced by a Dear ImGui + GLFW GUI linking `EngineSessionCore` directly. Python GUI, entry points, LUSID library, and Python virtual environment all removed.
- **Note**: `spatialroot_realtime` is now the primary CLI. `EngineSession::shutdown()` is terminal; a new instance must be constructed to restart.

---

## Contributors

- **Lead Developer**: Lucian Parisi
- **Team Members**: Cult-DSP Development Team
