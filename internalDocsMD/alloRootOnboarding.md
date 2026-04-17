# cult-allolib / DBAP Normalization — Agent Onboarding Prompt

Use this document to bring a new agent up to speed before it works on any phase of the cult-allolib plan.

---

## What this project is

Spatial Root is a real-time spatial audio engine. It uses AlloLib's DBAP panner to distribute mono source audio across a multichannel speaker layout. The engine is in `spatial_engine/realtimeEngine/`.

---

## The problem being solved

The AlloLib DBAP implementation (`thirdparty/allolib/src/sound/al_Dbap.cpp`) applies focus as a raw exponent on per-speaker inverse-distance gain with no normalization:

```
gain_k = pow(1.0f / (1.0f + dist_k), focus)
out_k += gain_k * sample
```

Because `1/(1+dist) < 1` for all speakers with dist > 0, raising it to any positive power shrinks the gain. As focus increases, all speaker gains decrease — including the loudest one. Total power is not conserved. Focus was supposed to sharpen spatial distribution; instead it also attenuates the entire output.

This was confirmed by the DBAP Focus Investigation Report (April 2026). The academic DBAP paper (Lossius et al., ICMC 2009) specifies a normalization step that the AlloLib implementation omitted.

---

## The plan

Full plan: `internalDocsMD/alloRootPlan.md`

Phases:
1. Create `cult-allolib` fork — **DONE** (`internal/cult-allolib` submodule, `heads/main`)
2. Add L2 normalization to DBAP in-place — **NEXT**
3. Switch Spatial Root build to use `cult-allolib` instead of `thirdparty/allolib` — **NEXT (Phase 1.5 in plan)**
4. Runtime validation
5. Remove auto-compensation
6. Prune unused AlloLib parts

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

## Current wiring state

The `cult-allolib` submodule exists but the build still points to `thirdparty/allolib`. Three CMake files need updating as part of Phase 1.5 / Phase 3:

| File | Line(s) | Change |
|---|---|---|
| `CMakeLists.txt` | 56 | `thirdparty/allolib` → `internal/cult-allolib` |
| `spatial_engine/realtimeEngine/CMakeLists.txt` | 22–23, 36 | same path change |
| `spatial_engine/spatialRender/CMakeLists.txt` | 20, include path | same path change |
| `init.sh` | Step 2 (line 131–139) | initialize `internal/cult-allolib` instead of `thirdparty/allolib` |

The `init.sh` should check for `internal/cult-allolib/include` as its sentinel, not `thirdparty/allolib/include`.

---

## Files you will touch

For Phase 2 (DBAP modification):
- `internal/cult-allolib/src/sound/al_Dbap.cpp` — add L2 normalization to `renderBuffer()` and `renderSample()`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp` — update doc comment for `setFocus()` to reflect normalized semantics

For Phase 1.5 / 3 (build wiring):
- `CMakeLists.txt`
- `spatial_engine/realtimeEngine/CMakeLists.txt`
- `spatial_engine/spatialRender/CMakeLists.txt`
- `init.sh`

Do not touch `thirdparty/allolib/` — it is the upstream submodule and must not be modified.

---

## Authorship / comment style for DBAP edits

Preserve the original AlloLib license header and Ryan McGee's authorship. Mark all new code clearly:

```cpp
// Original AlloLib behavior: raw exponent, no normalization.
// Cult DSP modification (Added by Lucian Parisi): L2 normalization added
//   so that sum(v_k^2) = 1 for all source positions and focus values.
//   This matches equation (2) of Lossius et al., ICMC 2009.
```

---

## What NOT to do

- Do not rewrite DBAP from scratch
- Do not modify `thirdparty/allolib/` (upstream submodule)
- Do not remove the `1+dist` distance floor — it is AlloLib's spatial blur mechanism
- Do not touch `computeFocusCompensation()` in `Spatializer.hpp` yet — that is Phase 5
- Do not remove auto-compensation UI or plumbing yet — Phase 5 only

---

## Key source locations

| Symbol | File | Line |
|---|---|---|
| `Dbap::renderBuffer()` | `internal/cult-allolib/src/sound/al_Dbap.cpp` | 38 |
| `Dbap::renderSample()` | `internal/cult-allolib/src/sound/al_Dbap.cpp` | 17 |
| `Dbap::setFocus()` | `internal/cult-allolib/include/al/sound/al_Dbap.hpp` | 81 |
| `Spatializer::renderBlock()` | `spatial_engine/realtimeEngine/src/Spatializer.hpp` | 404 |
| `computeFocusCompensation()` | `spatial_engine/realtimeEngine/src/Spatializer.hpp` | 1092 |
| Root CMake AlloLib block | `CMakeLists.txt` | 49–59 |
