# cult-allolib / DBAP Current Reference

This document is the compact current-state reference for Spatial Root's `cult-allolib` DBAP work.

Supporting documents:
- Math source of truth: `internalDocsMD/dbapMath.md`
- Build / CI notes: `internalDocsMD/BUILD_AND_CI.md`

Rule: if DBAP math, invariants, or focus semantics change, update `dbapMath.md` first, then update this file.

---

## Goal

Spatial Root now owns its DBAP behavior through the internal AlloLib fork at:

- `internal/cult-allolib/src/sound/al_Dbap.cpp`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp`

The purpose of the fork is to keep DBAP behavior aligned with the intended normalized model and with Spatial Root's realtime architecture, while preserving the inherited AlloLib implementation in place rather than rewriting it.

---

## Problem Summary

Original AlloLib DBAP applied focus as a raw exponent on inverse-distance weights:

```cpp
gain_k = powf(1.0f / (1.0f + dist_k), focus)
```

with no normalization.

That caused increasing `focus` to reduce every speaker gain, including the dominant speaker. In practice:

- spatial distribution became tighter
- overall output level dropped
- focus acted as both a localization control and an unintended attenuation control

The `cult-allolib` fork fixes that by normalizing the DBAP gain vector.

---

## Current State

As of `2026-04-17`:

- Phase 1: create `cult-allolib` fork — done
- Phase 2: add normalized DBAP in place — done
- Phase 3: switch Spatial Root build to `cult-allolib` — done
- Phase 4: runtime validation — next
- Phase 5: remove focus auto-compensation — done
- Phase 6: prune unused AlloLib parts — pending

The engine now builds entirely against `internal/cult-allolib`. `thirdparty/allolib` has been removed from the repo.

---

## Locked Decisions

### 1. Keep the original DBAP implementation structure

- Do not rewrite DBAP from scratch
- Keep the existing AlloLib DBAP files and structure
- Preserve original authorship and license headers

### 2. Preserve the distance law

The inherited distance term remains:

```text
(1 / (1 + dist)) ^ focus
```

Do not remove the `1 + dist` floor. It remains part of the intended behavior for this fork.

### 3. Preserve the normalization invariant

The implemented invariant is:

```text
sum(v_k^2) = 1
```

The implementation uses max-scaled L2 normalization, which is mathematically equivalent to ordinary L2 normalization but numerically safer when raw weights are very small.

### 4. Focus is a shaping control only

With normalization in place:

- low focus spreads energy more evenly across speakers
- high focus concentrates energy onto the nearest speakers
- total squared gain remains constant

Focus is no longer a global attenuation control.

### 5. Minimum supported focus is `0.1`

Values below `0.1` are clamped before reaching live DBAP behavior, including:

- `Dbap` construction
- `Dbap::setFocus()`
- `EngineSession::configureRuntime()`
- `EngineSession::setDbapFocus()`
- OSC focus updates

### 6. No legacy path

There is no compile-time or runtime switch for old unnormalized DBAP behavior. The normalized path is the implementation.

---

## Code Facts

- `DBAP_MAX_NUM_SPEAKERS = 192`
- `Dbap` throws immediately if the speaker layout exceeds that cap
- per-render weight and gain scratch buffers are fixed-capacity `std::array<float, DBAP_MAX_NUM_SPEAKERS>`
- `renderSample()` and `renderBuffer()` implement the same normalized math
- `renderBuffer()` precomputes final per-speaker gains outside the sample loop

These implementation details are documented mathematically in `dbapMath.md`.

---

## What Was Changed

### Phase 2 — DBAP normalization

Completed in:
- `internal/cult-allolib/src/sound/al_Dbap.cpp`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp`

Implemented changes:
- raw per-speaker weights still use the inherited AlloLib distance law
- L2 normalization was added in both `renderSample()` and `renderBuffer()`
- `renderBuffer()` precomputes final gains outside the inner sample loop
- focus documentation was updated to match normalized behavior
- minimum focus clamp was enforced
- speaker-count overflow is now rejected explicitly at construction time

### Phase 3 — switch to `cult-allolib`

Completed integration changes include:
- root and component CMake now point at `internal/cult-allolib`
- init/build wiring was updated to use the internal fork
- GUI stb include path now points into `internal/cult-allolib`
- old `thirdparty/allolib` references were removed from active build wiring

### Phase 5 — remove focus auto-compensation

Focus auto-compensation was removed from:
- engine
- GUI
- CLI
- OSC
- current API-facing docs

That removal is intentional and depends on the normalized invariant: there is no longer a DBAP power defect for higher-level code to compensate for.

### Historical completion record

The DBAP migration and normalization work completed these major milestones:

- created and adopted the `internal/cult-allolib` fork as Spatial Root's active AlloLib dependency
- normalized DBAP in-place rather than replacing the inherited implementation
- switched realtime, offline, and active GUI build wiring to the internal fork
- removed the old focus auto-compensation layer after normalized DBAP made it structurally obsolete

What remains active is runtime validation of the normalized behavior, followed by any later pruning of unused AlloLib surface area.

---

## Runtime Architecture Note

DBAP speaker count is the logical layout speaker count, not the hardware output width.

Spatial Root's realtime renderer uses a two-space model:

- internal compact render bus:
  `numSpeakers + numSubwoofers`
- output bus:
  `max(deviceChannel) + 1`

DBAP renders to the compact internal bus first. Output routing/remap happens afterward. The DBAP speaker-count cap therefore applies to logical layout speakers, not to device/output channel width.

---

## Ongoing Work

### Phase 4 — runtime validation

This is the current next step.

Validate:
- no global attenuation as focus increases
- stable behavior during focus sweeps
- stable behavior with moving sources
- no clicks, pops, or gain discontinuities
- no clipping regressions
- acceptable main/sub perceptual balance after auto-comp removal

Recommended things to inspect during validation:
- loudest speaker stability versus focus
- perceived loudness consistency
- sum of squared gains
- output peak behavior
- any mismatch between expected and rendered spatial sharpening

Exit condition for this phase:
- focus behavior is stable and matches the normalized design
- no new runtime regressions appear

### Phase 6 — prune unused AlloLib parts

Do this only after DBAP validation is complete and the fork is clearly stable.

Potential pruning targets:
- VR-specific systems
- graphics systems not needed by Spatial Root
- unrelated app scaffolding
- unused examples or optional modules

Constraints:
- remove in small passes
- verify dependencies before removing anything
- test after each pruning pass
- do not obscure ownership of the DBAP modifications

---

## Onboarding Prompt Task

Use the following prompt when onboarding an agent to investigate the post-normalization DBAP symptoms in the realtime engine.

### Task

Investigate the issues that appeared after the normalized DBAP update in `cult-allolib`, and determine whether the normalized algorithm re-exposed old Spatializer continuity / guard problems, introduced a new gain discontinuity, or caused a real routing regression.

### Context

Spatial Root replaced the old unnormalized AlloLib DBAP behavior with a normalized DBAP path in `cult-allolib`. After that change, previously resolved symptoms reappeared: apparent channel relocation and audible pops. The investigation should focus on the interaction between the normalized DBAP math and the existing Spatializer guard / continuity logic.

Do not start from the assumption that remap or output routing is broken again. Prior investigation established that in the realtime path:

- DBAP speaker count is derived from `layout.speakers.size()`
- DBAP renders to a compact internal layout-sized bus
- remap happens downstream from DBAP
- device / hardware width is a separate later layer

So the first suspicion should be render-layer continuity, gain discontinuity, guard interaction, or normalized-weight crossover behavior, not broad remap architecture failure. Use the existing diagnostics to prove where the fault is.

### Primary Goal

Determine exactly what newly arose from the normalized DBAP algorithm, and whether it is:

1. a reopened old Spatializer bug mechanism
2. a new continuity issue introduced by normalization
3. a clipping or restored-level side effect
4. or a genuine routing / output regression

### Critical Source Of Truth

Read the prior bug audit first and treat it as the canonical record of earlier failures and fixes:

- `internalDocsMD/engine_testing/4_1_bug_audit.md`

Pay particular attention to:

- Bug 7 / 7.1 / 7.2: guard-induced relocation and the convergence + soft-zone fixes
- Bug 9 / 9.1: normal-path cross-block guard-transition pop and the previous-block blending fix
- the deferred fast-mover Bug 9 follow-up
- Phase 14 diagnostics and how to interpret `rBus` vs `dev`, `CLUSTER`, `DOM`, and `SpkG`
- the render-path summary
- the current guard structure
- any explicit “what not to do” notes

Do not re-derive what is already established there.

### What To Investigate

1. Whether normalized DBAP changed the gain field in a way that re-exposes old guard / crossover discontinuities
2. Whether the Bug 9.1 fix still behaves correctly under normalized DBAP
3. Whether the deferred fast-mover Bug 9 issue is now audible and should be reopened
4. Whether the normalized path causes sharper dominant-speaker or top-cluster changes than the old unnormalized path
5. Whether pops / relocation correlate with:
   - guard entry / exit
   - fast-mover sub-steps
   - top-speaker changes
   - normalization behavior
   - downstream routing mismatch
6. Whether the new behavior is really channel relocation, or a render-layer gain redistribution that only sounds like relocation

### Constraints

1. Investigation first. Do not patch broadly.
2. Do not reopen general remap / output analysis unless diagnostics actually show a downstream mismatch.
3. Do not modify device routing or remap code speculatively.
4. Do not redesign DBAP again before isolating the issue.
5. Use the prior bug audit as source of truth for previously closed mechanisms and successful fixes.

### Questions To Answer Explicitly

1. Does normalized DBAP itself create a new gain discontinuity at speaker crossover or guard boundaries?
2. Does the Bug 9.1 previous-block blending path still smooth the normal path adequately under the new gain model?
3. Are the returned symptoms better explained by the deferred fast-mover guard issue than by a new routing bug?
4. When the symptom occurs, do `rBus` and `dev` still match?
   - If yes, treat it as a render-layer issue first.
   - If no, explain exactly where downstream mismatch reappears.
5. Do `CLUSTER`, `DOM`, and active-mask diagnostics line up with the audible events?
6. Is normalization making previously masked gain transitions newly audible because the field is no longer globally attenuated?
7. Is restored level simply making old edge cases louder, or is there a true new defect in the normalized math path?

### Investigation Procedure

1. Read `internalDocsMD/engine_testing/4_1_bug_audit.md` fully before touching code.
2. Identify the exact code changes made for normalized DBAP in `cult-allolib`.
3. Trace the realtime path:
   - source pose
   - guard passes
   - normal-path vs fast-mover branch
   - DBAP render
   - Phase 6 trims
   - Phase 14 diagnostics
   - Phase 7 device copy
4. Compare the normalized DBAP behavior against the assumptions built into the existing Spatializer fixes.
5. Use or add minimal safe instrumentation if needed, but keep it lightweight and RT-safe.
6. During failing scenes, inspect:
   - `rBus` vs `dev`
   - `CLUSTER` and `DOM` events
   - `SpkG`
   - nearest-speaker / dominant-speaker changes
   - top 3 or top 4 gains
   - normalization summary data if needed
   - whether the event happens in the normal path or fast-mover path
7. Determine whether events align with:
   - normal-path guard transition
   - fast-mover sub-step guard behavior
   - speaker crossover under normalized weights
   - or downstream routing mismatch

### Final Report Format

Return a compact technical report with these sections:

1. Relevant files
2. Normalized DBAP change summary
3. Prior fixes likely relevant
4. Diagnostic evidence during failure
5. Most likely root cause
6. Whether this is a reopened old bug or a new one
7. Recommended next patch target

Also answer clearly:

- whether Bug 9.1 still holds under normalized DBAP
- whether the deferred Bug 9 fast-mover issue should now be reopened
- whether the issue is primarily in Spatializer guard continuity, fast-mover handling, normalized gain redistribution, clipping / restored level, or routing / remap

### Important Framing

The point is not just to inspect the normalized algorithm in isolation. The point is to evaluate how it interacts with the exact Spatializer fixes that previously solved relocation and pop issues.

---

## Attribution Rules

Do not disturb existing AlloLib attribution.

- Original AlloLib code remains attributed to Ryan McGee
- New DBAP modification code and comments are attributed to Lucian Parisi

When editing DBAP files:
- preserve the existing AlloLib license header
- preserve original attribution
- clearly distinguish original behavior from Cult DSP modifications

---

## What Not To Do

- Do not rewrite DBAP from scratch
- Do not remove the `1 + dist` floor
- Do not add a normalized / unnormalized legacy switch
- Do not reintroduce focus auto-compensation
- Do not treat DBAP speaker count as hardware output width
- Do not touch `thirdparty/allolib/`; it no longer exists here
