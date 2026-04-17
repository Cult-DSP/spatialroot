# cult-allolib Fork and DBAP Normalization Plan

## Goal

Create an internal AlloLib fork named `cult-allolib` so Spatial Root can rely on a DBAP implementation whose focus behavior and normalization match the intended academic model and the needs of the Cult DSP toolchain.

The immediate technical motivation is clear from the DBAP investigation:

- current AlloLib DBAP applies focus as a raw exponent on inverse-distance gain
- there is no normalization step
- increasing focus attenuates all speakers, including the loudest one
- the current auto-compensation path is therefore correcting a flaw in the panner behavior rather than adding a desirable higher-level feature

This makes DBAP ownership a justified fork point.  
Source of truth for this motivation: the DBAP Focus Investigation Report. :contentReference[oaicite:1]{index=1}

---

## High-level sequence

1. Create `cult-allolib` (done and added submodule)
   1.5 - update cmake, init, builds, and anything else to use this submodule (**DONE 2026-04-17**)
2. Augment the existing DBAP files in-place rather than rewriting from scratch (**NEXT**)
3. Update Spatial Root to build against `cult-allolib` (**DONE 2026-04-17** — see Phase 1.5)
4. Test realtime runtime behavior thoroughly
5. Disable and then remove auto-compensation if validation confirms normalized DBAP makes it unnecessary
6. Prune unused parts of AlloLib only after the fork is stable

---

## Phase 1 — Create the `cult-allolib` fork

### Objective

Establish a controlled internal fork of AlloLib that preserves history, licensing, authorship context, and a stable baseline before any algorithm changes.

### Actions

- Fork AlloLib into a new repository named `cult-allolib` (done)
- Keep directory structure unchanged initially
- update cmake, init / build shell scripts, anything else to use this internal fork
- Do not remove unrelated graphics / VR / app files yet
- Tag or branch the initial import so there is a clean reference point
- Add a top-level note describing the fork purpose:
  - audio-focused internal fork for Cult DSP tooling
  - DBAP behavior ownership
  - future dependency reduction and pruning, but not in the initial pass

### Deliverables

- `cult-allolib` repository created
- baseline branch or tag recorded
- top-level fork note added

### Exit criteria

- Spatial Root can still point to the forked repo without functional changes yet
- there is a clean baseline for later comparison

---

## Phase 2 — Augment the existing DBAP implementation in-place

### Objective

Modify the existing DBAP implementation to add normalization and corrected focus behavior while preserving original file continuity, original authorship context, and license clarity.

### Constraints

- Do not rewrite DBAP from scratch
- Keep the original DBAP files and structure
- Preserve original author and license information
- Add clear inline comments differentiating:
  - original AlloLib behavior
  - Cult DSP modifications
- Add Lucian’s name only where new code or comments are introduced

### Files

- `cult-allolib/include/al/sound/al_Dbap.hpp`
- `cult-allolib/src/sound/al_Dbap.cpp`

### Required work

- Document the current behavior as legacy behavior inside comments
- Add the new normalization path directly in the existing DBAP implementation
- Ensure the new code makes the intended invariant explicit
- Define clearly what focus means in the modified implementation
- Add comments explaining:
  - original raw exponent path
  - why normalization is being added
  - what invariant is now being preserved
  - what behavior should remain stable as focus changes

  - document revised dbap with citations internalDocsMD/dbap_paper.pdf in internalDocsMD/dbapMath.md

### Design target

The new implementation should eliminate the current global attenuation behavior caused by raw exponentiation without normalization. The investigation showed that the existing implementation preserves no useful invariant across focus changes. :contentReference[oaicite:2]{index=2}

### Recommended safety measure

Keep a temporary comparison path during implementation:

- legacy behavior path
- normalized Cult behavior path

This can be:

- a compile-time switch, or
- a temporary alternate code path used only during validation

Do not keep this indefinitely unless it proves useful.

### Deliverables

- updated `al_Dbap.hpp/.cpp`
- comments clearly marking original behavior versus Cult modifications
- temporary comparison mechanism if needed

### Exit criteria

- DBAP code compiles in `cult-allolib`
- focus behavior is no longer based on unnormalized global attenuation
- normalization policy is explicit and documented

---

## Phase 3 — Update Spatial Root to use `cult-allolib` ✓ DONE 2026-04-17

### Objective

Move Spatial Root fully onto the new fork with clean build integration.

### Actions

- update submodule or dependency reference to `cult-allolib`
- update init scripts
- update build scripts
- update CMake references
- update any README or setup docs that mention upstream AlloLib directly
- confirm no accidental references remain to the old AlloLib source
- verify CI / bootstrap logic still works with the fork

### Areas to check

- repo submodule declarations
- `init` or setup scripts
- `build.sh`
- CMakeLists files
- CI workflow files
- any documentation describing dependency setup

### Deliverables

- Spatial Root builds against `cult-allolib`
- setup and bootstrap scripts work with the new fork
- docs updated to reflect the dependency change

### Exit criteria

- clean build on the main supported development machine
- clean fresh-clone bootstrap using the new fork
- no hidden dependency on the old AlloLib path remains

---

## Phase 4 — Realtime runtime validation

### Objective

Prove that the modified DBAP behavior is correct in practice before removing higher-level compensation logic.

### Required tests

- focus sweep tests at fixed source positions
- sources near speakers
- sources between speakers
- moving source tests
- long-duration runtime playback
- regression scenes already used in Spatial Root debugging
- verify no new clicks, pops, or gain discontinuities are introduced

### Metrics to inspect

- loudest speaker stability versus focus
- perceived loudness consistency
- absence of global attenuation when increasing focus
- stable transitions when focus changes dynamically
- no unintended clipping from normalization changes
- no obvious mismatch between expected and rendered spatial sharpening

### Recommended temporary instrumentation

During this phase only, log or inspect:

- max speaker gain
- sum of gains
- sum of squared gains
- any normalization scalar
- output peak behavior across focus values

### Deliverables

- runtime test notes
- conclusion on whether focus now behaves correctly
- conclusion on whether compensation is still needed

### Exit criteria

- focus behavior is stable and matches design expectations
- no compensation is required to counteract the old global attenuation problem
- no new runtime regressions appear

---

## Phase 5 — Disable then remove auto-compensation

### Objective

Retire the current auto-compensation system once runtime validation confirms it is no longer serving a necessary function.

### Important note

This should happen **after** validation, not before.

The current findings indicate that the previous compensation logic was built around invalid assumptions about DBAP behavior. If normalized DBAP now preserves the desired invariant, auto-compensation should become unnecessary. But this must be confirmed in runtime testing first. :contentReference[oaicite:3]{index=3}

### Recommended sequence

1. disable auto-compensation by default
2. keep code temporarily available during verification
3. confirm normalized DBAP produces stable focus behavior without it
4. remove:
   - UI checkbox
   - engine config plumbing
   - status reporting hooks
   - dead helper functions
   - stale comments based on the old assumptions

### Deliverables

- auto-comp disabled
- validation confirming it is unnecessary
- auto-comp code removed cleanly

### Exit criteria

- no focus loudness correction layer is needed anymore
- Spatial Root behavior is simpler and clearer
- no dead compensation code remains

---

## Phase 6 — Prune unused parts of AlloLib

### Objective

Reduce framework surface area only after the new fork is stable and Spatial Root is confirmed to work against it.

### Scope

Potential pruning targets may include:

- VR-specific systems
- graphics systems not needed by Spatial Root
- unrelated app scaffolding
- unused examples or targets
- optional modules not relevant to Cult DSP audio tooling

### Constraints

- prune only after DBAP and Spatial Root integration are stable
- prefer targeted removal over broad deletion
- ensure each removal is justified by actual non-use
- do not break build assumptions silently

### Recommended method

- identify what Spatial Root actually uses
- map transitive build dependencies
- remove in small passes
- test after each pass

### Deliverables

- leaner `cult-allolib`
- updated build documentation
- list of intentionally retained versus removed components

### Exit criteria

- fork is smaller and easier to maintain
- Spatial Root still builds and runs cleanly
- pruning does not obscure ownership of the DBAP modifications

---

## Notes on authorship and documentation

For DBAP file edits:

- preserve original license and author comments
- do not erase or overwrite original attribution
- add clearly separated comments for Cult DSP modifications
- identify Lucian’s additions explicitly where new behavior is introduced
- make it obvious which parts are inherited and which parts are new

Suggested comment style:

- `Original AlloLib behavior: ...`
- `Cult DSP modification: ...`
- `Added by Lucian Parisi: ...`

This keeps provenance clean and avoids confusion later.

---

## Locked decisions (2026-04-17)

These were resolved by reading the Lossius et al. 2009 ICMC paper and the investigation report.

### 1. Invariant: `sumG² = 1` (L2 / constant-power normalization)

The paper is explicit: equation (2) states `I = Σ v_i² = 1`. This is the DBAP invariant.
The normalization coefficient `k = 1 / sqrt(Σ w_i²)` follows algebraically from it.
Total acoustic power is constant regardless of source position and regardless of focus value.

### 2. Exact semantics of focus in the normalized model

Focus (`a` in the paper, `mFocus` in AlloLib) is a rolloff exponent on the per-speaker inverse-distance weight:

```
w_k = pow(1.0f / (1.0f + dist_k), focus)   // unnormalized; keeps AlloLib 1+d blur floor
k   = 1.0f / sqrt(sum_k(w_k * w_k))         // L2 normalizer
v_k = k * w_k                               // final gain; sum(v_k^2) = 1
```

Focus controls spatial sharpening only. It has no effect on total power. As focus increases:

- the nearest speaker's gain rises toward 1.0 (single-speaker limit)
- all other speakers fall correspondingly
- no global attenuation occurs

### 3. Legacy comparison path

Not required. The normalization is a single `sqrt` + division pass added after the existing per-speaker weight calculation. The original per-speaker distance loop is preserved unchanged. No conditional compile switch needed.

### 4. Acceptance baseline for runtime validation

- 8-speaker ring at focus = 1, 2, 4: loudest speaker must not drop below its focus=1 level
- Source at front: sp0 gain must be stable across focus sweep
- Moving source: no pops or gain steps at block boundaries
- Regression scenes already used in realtime engine debugging (Fix 1–6, Phase 14 logs)

---

## Final decision points to lock before Phase 2 completes

~~1. What invariant the new DBAP will preserve~~ — **LOCKED: sumG² = 1**
~~2. Exact semantics of focus in the normalized model~~ — **LOCKED: see above**
~~3. Whether a temporary legacy comparison path will exist during validation~~ — **LOCKED: not needed** 4. What test scenes will be the acceptance baseline for runtime validation — **LOCKED: see above**

---

## Success condition

This plan is successful if:

- `cult-allolib` becomes the owned audio-focused fork used by Spatial Root
- DBAP focus behavior matches intended design rather than causing global attenuation
- Spatial Root no longer needs to compensate for a flawed panner implementation
- the codebase becomes simpler before any broad pruning pass begins
