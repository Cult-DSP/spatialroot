# cult-allolib / DBAP Normalization — Agent Onboarding Prompt

Use this document to bring a new agent up to speed before it works on any phase of the cult-allolib plan.

---

## What this project is

Spatial Root is a real-time spatial audio engine. It uses AlloLib's DBAP panner to distribute mono source audio across a multichannel speaker layout. The engine is in `spatial_engine/realtimeEngine/`.

---

## The problem being solved

The original AlloLib DBAP implementation applied focus as a raw exponent on per-speaker inverse-distance gain with no normalization:

```
gain_k = pow(1.0f / (1.0f + dist_k), focus)
out_k += gain_k * sample
```

Because `1/(1+dist) < 1` for all speakers with dist > 0, raising it to any positive power shrinks the gain. As focus increases, all speaker gains decrease — including the loudest one. Total power is not conserved. Focus was supposed to sharpen spatial distribution; instead it also attenuated the entire output.

This was confirmed by the DBAP Focus Investigation Report (April 2026). The academic DBAP paper (Lossius et al., ICMC 2009) specifies a normalization step that the AlloLib implementation omitted.

---

## The plan

Full plan: `internalDocsMD/alloRootPlan.md`

Phases:
1. Create `cult-allolib` fork — **DONE** (`internal/cult-allolib` submodule, `heads/main`)
2. Add L2 normalization to DBAP in-place — **NEXT**
3. Switch Spatial Root build to use `cult-allolib` — **DONE 2026-04-17**
4. Runtime validation
5. Remove auto-compensation
6. Prune unused AlloLib parts

---

## Current state (as of 2026-04-17)

Phases 1 and 3 are complete. The build is fully on `internal/cult-allolib`. `thirdparty/allolib` has been removed from the repo entirely.

**What was done in Phase 3:**
- `CMakeLists.txt`, `spatial_engine/realtimeEngine/CMakeLists.txt`, `spatial_engine/spatialRender/CMakeLists.txt` — all `add_subdirectory` and `target_include_directories` paths switched to `internal/cult-allolib`
- `init.sh` Step 2 — now initializes `internal/cult-allolib` (sentinel: `internal/cult-allolib/include`)
- `gui/imgui/CMakeLists.txt` — stb include path updated to `internal/cult-allolib/external/stb/stb`
- `scripts/sparse-allolib.sh`, `scripts/shallow-submodules.sh` — updated to reference cult-allolib
- `thirdparty/allolib` submodule — deinited, `git rm`'d, removed from `.gitmodules` and `.git/modules`

**Phase 2 is next.** The DBAP source files in `internal/cult-allolib` still have the original unnormalized implementation. That is the only remaining work before runtime validation.

---

## Locked decisions

### Normalization invariant: `sumG² = 1`

Paper equation (2): `I = Σ v_i² = 1`

The correct modified gain computation is:

```cpp
// Step 1: compute unnormalized weights (keep existing AlloLib distance loop unchanged)
float w[N];
for each speaker k:
    float dist = |relpos - speakerVec_k|
    w[k] = pow(1.0f / (1.0f + dist), mFocus)

// Step 2: L2 normalizer
float sumSq = 0.0f;
for each k: sumSq += w[k] * w[k];
float kNorm = (sumSq > 1e-12f) ? (1.0f / sqrt(sumSq)) : 0.0f;

// Step 3: apply normalized gain to output
for each k:
    out[k] += kNorm * w[k] * sample
```

This makes `Σ v_k² = 1` for any source position and any focus value.

### Focus semantics

Focus is a rolloff exponent (= `a` in the paper). It controls spatial sharpening only. With normalization:
- focus=0 → all speakers equal weight (1/sqrt(N) each), sumG² = 1
- focus=high → energy concentrates on nearest speaker(s), sumG² still = 1
- No global attenuation at any focus value

### No legacy comparison path needed

The normalization is two additional lines (sumSq loop + divide). The original per-speaker distance loop is preserved. No compile-time switch is needed.

---

## Files to touch for Phase 2

- `internal/cult-allolib/src/sound/al_Dbap.cpp` — add L2 normalization to `renderBuffer()` and `renderSample()`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp` — update doc comment for `setFocus()` to reflect normalized semantics

Do not touch any other files for Phase 2.

---

## Authorship / comment style for DBAP edits

**Attribution rule (no exceptions):**
- All original AlloLib code is attributed to **Ryan McGee**
- All new code (including the normalization addition) is attributed to **Lucian Parisi**

Preserve the original AlloLib license header and Ryan McGee's authorship exactly as they appear. Do not remove or modify any existing attribution. Where original code is kept unchanged, it carries Ryan McGee's authorship implicitly via the file header. Where new lines are added, mark them explicitly with Lucian Parisi's name.

Comment style for the normalization block:

```cpp
// Original AlloLib behavior (Ryan McGee): raw exponent on per-speaker inverse-distance gain,
//   no normalization — total power decreases as focus increases.
// Cult DSP modification (Lucian Parisi): L2 normalization added so that sum(v_k^2) = 1
//   for all source positions and focus values.
//   This matches equation (2) of Lossius et al., ICMC 2009.
```

---

## What NOT to do

- Do not rewrite DBAP from scratch
- Do not remove the `1+dist` distance floor — it is AlloLib's spatial blur mechanism
- Do not add a compile-time legacy/normalized switch — not needed (locked decision)
- Do not modify focus auto-compensation — it has been fully removed (Phase 5 complete)
- Do not touch `thirdparty/allolib/` — it no longer exists in this repo

---

## Key source locations

| Symbol | File | Line |
|---|---|---|
| `Dbap::renderSample()` | `internal/cult-allolib/src/sound/al_Dbap.cpp` | 17 |
| `Dbap::renderBuffer()` | `internal/cult-allolib/src/sound/al_Dbap.cpp` | 38 |
| `Dbap::setFocus()` | `internal/cult-allolib/include/al/sound/al_Dbap.hpp` | 81 |
| `Spatializer::renderBlock()` | `spatial_engine/realtimeEngine/src/Spatializer.hpp` | 404 |
| Root CMake cult-allolib block | `CMakeLists.txt` | 49–59 |
