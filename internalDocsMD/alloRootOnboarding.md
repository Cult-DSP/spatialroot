# cult-allolib / DBAP Normalization Onboarding

Use this brief to get oriented before changing Spatial Root's DBAP implementation or any docs that describe it.

---

## Project Snapshot

Spatial Root is a real-time spatial audio engine. Its DBAP panner lives in the internal AlloLib fork at:

- `internal/cult-allolib/src/sound/al_Dbap.cpp`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp`

The engine now builds entirely against `internal/cult-allolib`. The old `thirdparty/allolib` submodule has been removed from the repo.

---

## The Core Problem

Original AlloLib DBAP applied focus as a raw exponent on inverse-distance weights:

```cpp
gain_k = powf(1.0f / (1.0f + dist_k), focus)
```

with no normalization.

That meant increasing `focus` reduced every speaker gain, including the loudest one. Focus sharpened localization, but it also caused unintended global attenuation.

The current fork fixes that by normalizing the gain vector so focus changes speaker distribution only, not total squared gain.

---

## Current State

As of `2026-04-17`:

- Phase 1: create `cult-allolib` fork — done
- Phase 2: add L2 normalization in place — done
- Phase 3: switch Spatial Root build to `cult-allolib` — done
- Phase 4: runtime validation — next
- Phase 5: remove focus auto-compensation — done
- Phase 6: prune unused AlloLib parts — pending

Supporting docs:
- Plan: `internalDocsMD/alloRootPlan.md`
- Math source of truth: `internalDocsMD/dbapMath.md`

Rule: any DBAP math or algorithm change must be documented in `dbapMath.md` before the work is considered complete.

---

## Locked Technical Decisions

### Normalization invariant

The implementation enforces:

```text
sum(v_k^2) = 1
```

using max-scaled L2 normalization. This is mathematically equivalent to ordinary L2 normalization, but is computed in a numerically safer way when all raw weights are small.

### Focus semantics

`focus` is a rolloff exponent, not a loudness control.

With normalization:
- low focus spreads energy more evenly across speakers
- high focus concentrates energy onto the nearest speakers
- total squared gain remains constant

Minimum supported focus is `0.1`. Values below that are clamped before they reach the live DBAP path, including:
- `Dbap` construction
- `Dbap::setFocus()`
- `EngineSession::configureRuntime()`
- `EngineSession::setDbapFocus()`

### Original distance law stays

Do not remove or replace the `1 + dist` term. The inherited AlloLib law:

```text
(1 / (1 + dist)) ^ focus
```

is intentionally retained before normalization.

### No legacy path

There is no compile-time or runtime switch for old unnormalized DBAP. The normalized path is the implementation.

---

## Code Facts That Matter

- `DBAP_MAX_NUM_SPEAKERS` is `192`
- constructor throws `std::runtime_error` if a layout exceeds that cap
- per-render scratch buffers use `std::array<float, DBAP_MAX_NUM_SPEAKERS>`
- `renderSample()` and `renderBuffer()` implement the same normalized math
- `renderBuffer()` precomputes final per-speaker gains outside the sample loop

---

## What Was Removed

Focus auto-compensation has already been removed from engine, GUI, CLI, OSC, and current DBAP-facing docs.

That removal depends on the new invariant in `dbapMath.md`: because the DBAP gain vector now preserves `sum(v_k^2) = 1`, there is no longer a normalization defect for higher-level code to correct.

---

## Attribution Rules

Do not disturb existing AlloLib attribution.

- Original AlloLib code remains attributed to Ryan McGee
- New DBAP modification code and comments are attributed to Lucian Parisi

When adding DBAP comments, preserve the original license/header and clearly distinguish original behavior from Cult DSP modifications.

---

## What Not To Do

- Do not rewrite DBAP from scratch
- Do not remove the `1 + dist` floor
- Do not add a legacy normalized/un-normalized switch
- Do not reintroduce focus auto-compensation
- Do not touch `thirdparty/allolib/`; it no longer exists here

---

## Next Work

Phase 4 runtime validation should verify:
- no global attenuation as focus increases
- stable behavior during focus sweeps and source motion
- no clicks or discontinuities from the normalized path
- no clipping regressions
- acceptable sub/main perceptual balance after auto-comp removal

If DBAP behavior changes, update `dbapMath.md` first, then update this onboarding brief and the plan doc to match.
