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

Math reference: `internalDocsMD/dbapMath.md` — all DBAP algorithm changes, invariants, and derivations live here. **Any change to DBAP math or algorithm must be documented in `dbapMath.md` before the phase is considered complete.**

Phases:
1. Create `cult-allolib` fork — **DONE**
2. Add L2 normalization to DBAP in-place — **DONE 2026-04-17**
3. Switch Spatial Root build to use `cult-allolib` — **DONE 2026-04-17**
4. Runtime validation — **NEXT**
5. Remove auto-compensation — **DONE 2026-04-17**
6. Prune unused AlloLib parts

---

## Current state (as of 2026-04-17)

Phases 1, 2, 3, and 5 are complete. The build is fully on `internal/cult-allolib`. `thirdparty/allolib` has been removed from the repo entirely. Focus auto-compensation has been removed from engine, GUI, OSC, and all docs. **Phase 4 (runtime validation) is next.**

**What was done in Phase 2:**
- `internal/cult-allolib/src/sound/al_Dbap.cpp` — L2 normalization added to `renderSample()` and `renderBuffer()` using max-scaled weights (see Locked decisions below)
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp` — `setFocus()` doc updated; minimum focus 0.1 enforced

**What was done in Phase 3:**
- `CMakeLists.txt`, `spatial_engine/realtimeEngine/CMakeLists.txt`, `spatial_engine/spatialRender/CMakeLists.txt` — all `add_subdirectory` and `target_include_directories` paths switched to `internal/cult-allolib`
- `init.sh` Step 2 — now initializes `internal/cult-allolib` (sentinel: `internal/cult-allolib/include`)
- `gui/imgui/CMakeLists.txt` — stb include path updated to `internal/cult-allolib/external/stb/stb`
- `scripts/sparse-allolib.sh`, `scripts/shallow-submodules.sh` — updated to reference cult-allolib
- `thirdparty/allolib` submodule — deinited, `git rm`'d, removed from `.gitmodules` and `.git/modules`

**What was done in Phase 5:**
- `computeFocusCompensation()`, `mAutoCompValue`, `focusAutoCompensation` atomic, `mPendingAutoComp`, `setAutoCompensation()`, and the `autoComp` OSC parameter were all removed from engine, GUI, CLI, and docs

---

## Locked decisions

### Normalization invariant: `sumG² = 1`

Paper equation (2): `I = Σ v_i² = 1`

The implemented gain computation uses **max-scaled L2 normalization** to avoid large intermediate values when all raw weights are very small (extreme focus + distant speakers):

```cpp
// Step 1: compute unnormalized weights (original AlloLib distance loop, unchanged)
float w[DBAP_MAX_NUM_SPEAKERS];
for each speaker k:
    float dist = |relpos - speakerVec_k|
    w[k] = powf(1.0f / (1.0f + dist), mFocus)

// Step 2: max-scaled L2 normalization
// Dividing by maxW before squaring keeps sumSq in [1, N] — no large intermediate factors.
// Guard is on maxW: if the loudest raw weight is negligible, output silence.
float maxW = max over k of w[k]
if maxW < 1e-6f: return  // silence

float sumSq = 0.0f;
for each k: sumSq += (w[k] / maxW) * (w[k] / maxW)
float kNorm = 1.0f / (maxW * sqrt(sumSq))
// kNorm * w[k] = w[k] / sqrt(Σ w[j]²) — identical to simple L2, computed stably

// Step 3 (renderBuffer only): precompute per-speaker gains outside the sample loop
float gain[DBAP_MAX_NUM_SPEAKERS];
for each k: gain[k] = kNorm * w[k]

// Step 4: apply to output
for each k:
    out[k] += gain[k] * sample
```

This guarantees `Σ v_k² = 1` for any source position and any focus value ≥ 0.1.

### Focus semantics

Focus is a rolloff exponent (= `a` in the paper). It controls spatial sharpening only. With normalization:
- focus=0.1 → nearly equal weight across all speakers, sumG² = 1
- focus=high → energy concentrates on nearest speaker(s), sumG² still = 1
- No global attenuation at any focus value
- **Minimum enforced: 0.1** — clamped at `setFocus()` in `al_Dbap.hpp` and at `setDbapFocus()` / OSC callback in `EngineSession.cpp`. Values below 0.1 invert distance weighting (far speakers louder than near) and are not a supported mode.

### No legacy comparison path needed

The normalization adds a max-scan pass, a sumSq pass, and a gain-precompute pass after the existing per-speaker weight loop. The original loop is preserved. No compile-time switch is needed.

---

## Speaker count cap — `DBAP_MAX_NUM_SPEAKERS` (resolved)

`DBAP_MAX_NUM_SPEAKERS = 192` is a compile-time cap. All fixed-capacity arrays (`mSpeakerVecs[]`, `mDeviceChannels[]`, and the per-render `w` / `gain` buffers) are sized to this constant.

**Fixed before Phase 4:** the constructor now throws `std::runtime_error` immediately if `mNumSpeakers > DBAP_MAX_NUM_SPEAKERS`. The per-render arrays are `std::array<float, DBAP_MAX_NUM_SPEAKERS>`. An invariant comment in `al_Dbap.hpp` documents the constraint. Silent overflow is no longer possible.

---

## Files modified in Phase 2

- `internal/cult-allolib/src/sound/al_Dbap.cpp` — max-scaled L2 normalization in `renderBuffer()` and `renderSample()`; per-speaker gain precompute in `renderBuffer()`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp` — `setFocus()` doc updated; 0.1 minimum enforced inline

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
