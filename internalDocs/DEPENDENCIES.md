# Dependencies & Data Formats — Internal Reference

**Last Updated:** February 2026

---

## LUSID Scene JSON Format

> `json_schema_info.md` — Primary format spec. See also `LUSID/schema/lusid_scene_v0.5.schema.json` and `LUSID/internalDocs/DEVELOPMENT.md`.

**LUSID `scene.lusid.json` is the canonical spatial data format** read directly by the C++ renderer. The old `renderInstructions.json` format is deprecated and removed.

### Schema (abbreviated)

```json
{
  "version": "0.5",
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "duration": 566.0,
  "metadata": { "sourceFormat": "ADM", "duration": "00:09:26.000" },
  "frames": [
    {
      "time": 0.0,
      "nodes": [
        { "id": "1.1", "type": "direct_speaker", "cart": [-1.0, 1.0, 0.0], "speakerLabel": "RC_L", "channelID": "AC_00011001" },
        { "id": "4.1", "type": "LFE" },
        { "id": "11.1", "type": "audio_object", "cart": [0.0, 1.0, 0.0] }
      ]
    },
    { "time": 0.5, "nodes": [{ "id": "11.1", "type": "audio_object", "cart": [0.3, 0.9, 0.1] }] }
  ]
}
```

### Top-Level Fields

| Field | Description |
|---|---|
| `version` | LUSID format version (`"0.5"`) |
| `sampleRate` | Sample rate in Hz (must match audio files) |
| `timeUnit` | `"seconds"` (default), `"samples"`, or `"milliseconds"` — **always specify explicitly** |
| `duration` | **v0.5.2+** Total scene duration in seconds from ADM metadata. Renderer uses this instead of inferring from WAV lengths. Prevents truncated renders. |
| `metadata` | Optional: source format, original duration string |
| `frames` | Array of time-ordered frames containing spatial nodes |

### Node Types

| Type | ID Pattern | Required Fields | Renderer Behavior |
|---|---|---|---|
| `audio_object` | groups 11+ | `id`, `type`, `cart` | Spatialized (DBAP/VBAP/LBAP) |
| `direct_speaker` | groups 1–10 | `id`, `type`, `cart`, `speakerLabel`, `channelID` | Treated as static audio_object |
| `LFE` | group 4 | `id`, `type` | Routes to subwoofers, bypasses spatialization |
| `spectral_features` | X.2+ | — | Ignored by renderer |
| `agent_state` | X.2+ | — | Ignored by renderer |

**Node ID Format:** `X.Y` — X = group number, Y = hierarchy level (1 = parent, 2+ = children)

**Channel assignment convention:**
- Groups 1–10: DirectSpeaker bed channels
- Group 4: LFE (currently hardcoded — `_DEV_LFE_HARDCODED`)
- Groups 11+: Audio objects

### Coordinate System

`cart: [x, y, z]` — Cartesian direction vectors:
- **x**: Left (−) / Right (+)
- **y**: Back (−) / Front (+)
- **z**: Down (−) / Up (+)
- Normalized to unit length by renderer; zero vectors → front `[0, 1, 0]`

### Source ↔ WAV File Mapping

| Node ID | WAV Filename | Notes |
|---|---|---|
| `1.1` | `1.1.wav` | DirectSpeaker |
| `4.1` | `LFE.wav` | Special naming |
| `11.1` | `11.1.wav` | Audio object |

Old `src_N` naming convention is deprecated.

### Renderer Validation

1. Keyframe validation — drops keyframes with NaN/Inf values
2. Zero vector handling — replaces zero-length direction vectors with `[0, 1, 0]`
3. Time sorting — sorts keyframes by time ascending
4. Duplicate removal — collapses identical timestamps
5. Time unit detection — falls back to heuristic if `timeUnit` absent (with warning)

---

## Speaker Layout JSON Format

```json
{
  "speakers": [
    { "azimuth": 0.0, "elevation": 0.0, "radius": 5.0, "deviceChannel": 1 }
  ],
  "subwoofers": [
    { "channel": 16 },
    { "channel": 17 }
  ]
}
```

| Field | Type | Unit | Notes |
|---|---|---|---|
| `azimuth` | number | **radians** | 0 = front, positive = right |
| `elevation` | number | **radians** | 0 = horizon, positive = up |
| `radius` | number | meters | Typically 5.0 for AlloSphere |
| `deviceChannel` | integer | — | Informational; renderer uses consecutive 0-based indices |

Renderer uses consecutive channel indices (0, 1, 2, …) regardless of `deviceChannel`. Channel remapping to hardware channels is done via the Output Remap table (`--remap` CSV). Output buffer sized to `max(maxSpeakerChannel, maxSubwooferChannel) + 1`.

---

## LUSID Package Import Contract (SpatialSeed)

> `importingLUSIDpackage.md` — Spec for ingesting SpatialSeed-produced LUSID packages.

### Package Layout

Flat folder (no nested `audio/` directory) containing:
- `scene.lusid.json` — canonical LUSID scene (v0.5.x)
- `containsAudio.json` — channel metadata and ADM ordering (beds first, then objects)
- `mir_summary.json` — per-node MIR feature summaries (optional)
- Mono WAV files: `1.1.wav`, `2.1.wav`, …, `LFE.wav`, `11.1.wav`, `12.1.wav`, …

Audio format: 48 kHz, float32 WAV (v1 contract). `LFE.wav` is the special case — node id `4.1` maps to `LFE.wav` not `4.1.wav`.

### Audio Resolution for a Node

1. If `containsAudio.json` is present: look up `group_id` → `filename` field (preferred).
2. If node id is `4.1`: use `LFE.wav`.
3. Fallback: node id `X.1` → `X.1.wav` in package root.

### `containsAudio.json` Contract

```json
{
  "sample_rate": 48000,
  "threshold_db": -60.0,
  "channels": [
    { "channel_index": 11, "group_id": "11.1", "filename": "11.1.wav", "contains_audio": true, "rms_db": -12.3 }
  ]
}
```

Channel ordering: beds first (1.1, 2.1, 3.1, LFE/4.1, 5.1 … 10.1), then objects (11.1, 12.1 …).

### LUSID Scene Expectations

- Every `audio_object` node **must** have a keyframe at `t=0.0`.
- Delta frames: each frame contains only nodes that changed since the previous frame. Apply changes to listed nodes and hold previous state for unlisted nodes. Construct a full initial snapshot at `t=0.0` before playing frames.
- `cart` coordinates are normalized to `[-1, 1]`; clamp out-of-range values and log.

### Validation Rules on Import

1. Required files present: `scene.lusid.json`, `containsAudio.json`, at least one object WAV.
2. All WAVs referenced in `containsAudio.json` exist and report `sampleRate: 48000`.
3. Every `audio_object` has initial keyframe at `t=0.0`.
4. All `cart` coordinates within `[-1, 1]` — clamp and log if outside.
5. Channel ordering in `containsAudio.json` matches ADM bed-then-objects convention.

### Error Handling

- Missing WAVs: substitute silent buffer, log error (don't crash import).
- Missing `containsAudio.json`: fall back to filename-based resolution, warn user.
- Missing `mir_summary.json`: continue (optional for rendering).
- Frame sequence lacking `t=0.0` for a node: treat as static at origin, log import error.

---

## Dolby ADM Channel Label Abbreviations

> `dolbyMetadata.md` — Fixed channel abbreviations from Dolby Atmos specification.
> Source: Dolby Atmos IMF IAB interoperability guidelines.

| Abbrev | Channel |
|---|---|
| L / R | Left / Right |
| C | Center |
| Lc / Rc | Left Center / Right Center |
| LFE | Low-Frequency Effects |
| Lfh / Rfh | Left Front Height / Right Front Height |
| Ls / Rs | Left Surround / Right Surround |
| Lss / Rss | Left Side Surround / Right Side Surround |
| Lrs / Rrs | Left Rear Surround / Right Rear Surround |
| Lw / Rw | Left Wide / Right Wide |
| Tsl / Tsr | Left Top Middle / Right Top Middle |
| Ltf / Rtf | Left Top Front / Right Top Front |
| Ltr / Rtr | Left Top Rear / Right Top Rear |
| Lrh / Rrh | Left Rear Height / Right Rear Height |
| Lts / Rts | Left Top Surround / Right Top Surround |
