# Output Routing Architecture — Layout-Derived Routing

**Status:** Implemented (April 2026)  
**Scope:** Realtime engine only. Offline renderer (`spatialroot_spatial_render`) is explicitly out of scope — it uses consecutive 0-based speaker channels and has no routing table.

---

## Summary

> The engine renders to a compact internal bus and routes to a layout-defined output bus; the layout JSON is the only supported public routing source.

---

## Two-Space Model

| Space | Width | Owner | Contents |
|---|---|---|---|
| Internal bus | `numSpeakers + numSubwoofers` | `mRenderIO` in `Spatializer` | Channels 0..N-1 = DBAP speaker outputs; N..N+M-1 = LFE subwoofer outputs |
| Output bus | `max(all .deviceChannel) + 1` | `io` from `RealtimeBackend` | Physical AudioIO channels; may be sparse |

`mConfig.outputChannels` stores the output bus width. It is written by `Spatializer::init()` and read by `RealtimeBackend` to open AudioIO. It is **never** the internal bus width.

---

## Routing Table

`mOutputRouting` (type `OutputRemap`, owned by `Spatializer`) maps each internal channel to exactly one output channel. Built once in `init()` from the layout JSON's `deviceChannel` fields. One-to-one — no fan-in.

```
Speaker i   → internal channel i             → output channel layout.speakers[i].deviceChannel
Sub j       → internal channel numSpeakers+j → output channel layout.subwoofers[j].deviceChannel
```

`mRemap` is a non-owning `const OutputRemap*` set to `&mOutputRouting` after `init()`. Legacy CSV can override it via `EngineSession::configureRuntime()` (deprecated path only).

---

## Phase 7 Routing Stage

### Identity copy (fast path)

**Condition:** `mRemap->identity() == true`  
Valid only when `outputChannelCount == internalChannelCount` AND all entries are diagonal AND full coverage holds. `checkIdentity()` enforces the width-equality condition.

**Behavior:** Direct contiguous copy, internal ch N → output ch N.  
**Zeroing:** Relies on `RealtimeBackend::processBlock()` Step 1 pre-zero. Safe because identity guarantees no unmapped output channels.

### Scatter routing (non-identity path)

**Condition:** `mRemap->identity() == false`

**Behavior — self-contained:**
1. `renderBlock()` zero-fills all `outputChannelCount` output channels before scattering.
2. For each routing entry `{internalCh, outputCh}`: `memcpy` internal → output.

`renderBlock()` owns the output-bus clear in the scatter path. It does not rely on the caller to have zeroed `io.outBuffer()`. Unmapped output channels remain silent by this self-clear, not by caller contract.

### Summary

| Layout | Path | Output cleared by | Unmapped channels |
|---|---|---|---|
| Identity (e.g. translab) | Fast copy | Backend pre-zero | None — all output channels mapped |
| Non-identity (e.g. allosphere) | Scatter | `renderBlock()` self-clear | Zeroed by self-clear |

---

## Validation Gate

Located at the top of `Spatializer::init()`, before routing construction. Hard-fails (returns `false`) on:

- No speakers in layout
- Any `deviceChannel < 0`
- Duplicate `deviceChannel` across speakers
- Duplicate `deviceChannel` across subwoofers
- Speaker and subwoofer sharing a `deviceChannel`

Non-fatal warning: any `deviceChannel > 127` (suspiciously large — possible layout typo).

Post-construction guard: if `mOutputRouting.entries().empty()` after a passing validation, `init()` returns `false` with an internal error log.

---

## Naming Policy

| Engine concept | Canonical name | Notes |
|---|---|---|
| Compact render bus | internal bus / internal channels | |
| Physical output bus | output bus / output channels | |
| Internal bus width (local) | `internalChannelCount` | |
| Output bus width (config) | `mConfig.outputChannels` | |
| Internal subwoofer indices | `mSubwooferInternalChannels` | compact: numSpeakers+j |
| Output subwoofer channels | `mSubwooferOutputChannels` | from layout deviceChannel |
| Internal-space classifier | `isInternalSubwooferChannel(ch)` | |
| Output-space classifier | `isOutputSubwooferChannel(ch)` | |
| Accessor | `numInternalChannels()` | `numRenderChannels()` kept as `[[deprecated]]` alias |
| Routing table object | `mOutputRouting` | owned; type `OutputRemap` |
| Schema field | `deviceChannel` | kept verbatim — do not rename |

"auto-remap", "render channels", "device channels" (as engine terms), and "remap feature" are prohibited in engine-facing code and comments.

---

## CSV Deprecation

CSV routing is deprecated immediately as a user-facing feature.

| Surface | Treatment |
|---|---|
| GUI | Collapsed under `Legacy / Internal` header, hidden by default |
| CLI `--remap` | Help text marked `[DEPRECATED]`; `stderr` warning on use |
| `EngineSession` | `configureRuntime()` emits `[WARNING]` to stderr |
| `LayoutInput::remapCsvPath` | Deprecation comment in header |
| `OutputRemap::load()` | Deprecation comment in header |

**Removal (next step after validation):** Remove `--remap` flag, `remapCsvPath`, the CSV branch in `configureRuntime()`, and the GUI section in a single follow-up commit. `OutputRemap::load()` may be retained for tooling use.

---

## `mConfig.outputChannels` Read-Site Audit

All reads classified:

| File | Site | Classification |
|---|---|---|
| `EngineSession.cpp` | log "Output channels (from layout)" | output-space — correct |
| `EngineSession.cpp` | `mOutputRemap->load(..., mConfig.outputChannels)` | output-space — correct (was stale bug: previously passed as both args; fixed) |
| `RealtimeBackend.hpp` | log, validation, `AudioIO` open | backend/device — correct |
| `Spatializer.hpp` | `mConfig.outputChannels = outputChannelCount` | write site — correct |
| `RealtimeTypes.hpp` | comment | documentation — updated |

No internal DSP code reads `mConfig.outputChannels` as internal-bus width.

---

## Files Changed

| File | Change |
|---|---|
| `spatial_engine/realtimeEngine/src/Spatializer.hpp` | Validation gate; two-space init; `mSubwooferInternalChannels` + `mSubwooferOutputChannels`; `internalChannelCount` / `outputChannelCount`; `mOutputRouting.buildAuto()`; `mRemap = &mOutputRouting`; Phase 7 scatter; `isInternalSubwooferChannel` / `isOutputSubwooferChannel`; `numInternalChannels()` + deprecated `numRenderChannels()` |
| `spatial_engine/realtimeEngine/src/OutputRemap.hpp` | `buildAuto()` method; `checkIdentity()` enforces width equality; header updated; `load()` deprecation notice |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` | `outputChannels` comment updated to reflect output-bus-only role |
| `spatial_engine/realtimeEngine/src/EngineSession.cpp` | Fixed stale bug (`outputChannels` passed as both args to `load()`); deprecation warning on CSV path; updated log strings |
| `spatial_engine/realtimeEngine/src/EngineSession.hpp` | Deprecation comment on `remapCsvPath` |
| `spatial_engine/realtimeEngine/src/main.cpp` | `--remap` help text marked deprecated; comment at parse site |
| `gui/imgui/src/App.cpp` | CSV field moved into collapsed `Legacy / Internal` section |
| `gui/imgui/src/App.hpp` | Deprecation comment on `mRemapPath` |

---

## Invariants (post-patch)

1. `mRenderIO.channelsOut() == numSpeakers + numSubwoofers` — always compact.
2. `mConfig.outputChannels == max(all .deviceChannel) + 1` — output bus only.
3. `mRemap != nullptr` after `init()` returns `true`.
4. `mSubwooferInternalChannels[j] == numSpeakers + j` always.
5. `isInternalSubwooferChannel(ch)` called only with `ch < internalChannelCount`.
6. `isOutputSubwooferChannel(ch)` called only with `ch < outputChannelCount`.
7. No internal DSP code reads `mConfig.outputChannels` as internal-bus width.
8. `checkIdentity()` returns `true` only when `outputChannelCount == internalChannelCount` AND all entries diagonal AND full coverage.
9. DBAP math, proximity guard, onset fade, fast-mover sub-stepping untouched.
10. `init()` returns `false` on any validation failure.
11. `buildAuto()` after passing validation produces exactly `internalChannelCount` entries; out-of-range entries are a hard internal error.
12. Scatter path owns its output-bus clear; identity path relies on backend pre-zero.
13. Offline renderer (`SpatialRenderer`) out of scope — separate channel model.

---

## Verification Checklist

- [ ] `./build.sh --engine-only` — clean compile, no errors
- [ ] Translab layout: log shows `identity copy active`; audio correct; nanGuardCount = 0
- [ ] Allosphere layout (no `--remap`): log shows `scatter routing active`; routing entries match JSON; audio at correct physical outputs
- [ ] Sparse-gap silence: non-contiguous layout → unmapped output channels contain only zeros while non-gap channels carry audio (leading, middle, and trailing gap positions)
- [ ] Duplicate speaker `deviceChannel` → hard fail, descriptive error, engine refuses start
- [ ] Speaker/subwoofer shared `deviceChannel` → same
- [ ] Negative `deviceChannel` → same
- [ ] No speakers → same
- [ ] `--remap` legacy CSV: `[WARNING]` in stderr; CSV applied with correct internal/output widths
- [ ] Phase 14 `renderDomMask` / `deviceDomMask` stable — no spurious relocation events
