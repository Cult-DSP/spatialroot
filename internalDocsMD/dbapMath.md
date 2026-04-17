# DBAP Math Reference

This document is the math source of truth for Spatial Root's `cult-allolib` DBAP implementation.

Implementation files:
- `internal/cult-allolib/src/sound/al_Dbap.cpp`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp`

Reference paper:
- `internalDocsMD/dbap_paper.pdf` — Ville Pulkki / Trond Lossius / et al., ICMC 2009

Any change to DBAP math, invariants, or focus semantics must be documented here before the work is considered complete.

---

## 1. Problem Statement

The original AlloLib DBAP implementation computed one raw weight per speaker:

```cpp
w_k = powf(1 / (1 + dist_k), focus)
```

and then applied that weight directly to the output.

That preserved no useful normalization invariant. Because

```text
0 < 1 / (1 + dist_k) <= 1
```

for all valid speaker distances, raising the term to a larger positive exponent makes every nonzero weight smaller. Increasing `focus` therefore reduced not only off-axis speakers, but also the dominant speaker. The audible result was:

- tighter spatial distribution
- lower overall output level
- focus behaving as both a localization control and an unintended gain trim

The DBAP paper specifies a normalization condition that the original AlloLib path did not enforce.

---

## 2. Implemented Invariant

The current `cult-allolib` implementation enforces:

```text
sum(v_k^2) = 1
```

where `v_k` is the final gain applied to speaker `k`.

This matches equation (2) in the DBAP paper, which defines the intended constant-power-style invariant for the gain vector.

Practical meaning:
- changing `focus` changes the distribution of energy across speakers
- changing `focus` does not change total squared gain
- the panner no longer needs a compensating output boost just because focus increased

---

## 3. Coordinate And Distance Model

For each source position `pos`, the DBAP renderer maps it into the internal speaker-space coordinate system as:

```cpp
relpos = Vec3d(pos.x, -pos.z, pos.y)
```

For each speaker `k`:

```text
vec_k  = relpos - speakerVec_k
dist_k = |vec_k|
```

The raw distance law remains the original AlloLib law:

```text
raw_k = (1 / (1 + dist_k)) ^ focus
```

The `1 + dist_k` floor is intentionally preserved. It is part of the inherited AlloLib behavior and acts as a built-in spatial blur / softening term near speakers. The implementation does not switch to `1 / dist_k`, and it does not introduce a singularity at `dist_k = 0`.

---

## 4. Focus Semantics

`focus` is the DBAP rolloff exponent.

Current supported behavior:
- `focus = 1.0` gives the base inverse-distance rolloff used by this implementation
- larger `focus` sharpens localization by increasing contrast between near and far speakers
- smaller positive `focus` flattens the distribution toward a more even speaker spread

The engine enforces a minimum of `0.1`.

Clamp points:
- `Dbap` constructor in `internal/cult-allolib/src/sound/al_Dbap.cpp`
- `Dbap::setFocus()` in `internal/cult-allolib/include/al/sound/al_Dbap.hpp`
- `EngineSession::configureRuntime()` in `spatial_engine/realtimeEngine/src/EngineSession.cpp`
- `EngineSession::setDbapFocus()` in `spatial_engine/realtimeEngine/src/EngineSession.cpp`
- OSC focus callback in `EngineSession::start()` in `spatial_engine/realtimeEngine/src/EngineSession.cpp`

Values below `0.1` are not supported because they move toward inverted distance weighting, where far speakers can become louder than near speakers.

With normalization in place:
- low focus means broader spread, not quieter output
- high focus means tighter spread, not quieter output

---

## 5. Normalization Algorithm

The implementation uses max-scaled L2 normalization.

### Step 1: compute raw weights

For each speaker:

```text
w_k = (1 / (1 + dist_k)) ^ focus
```

### Step 2: compute a safe scaling reference

Let:

```text
maxW = max_k(w_k)
```

If:

```text
maxW < 1e-6
```

the renderer returns silence for that source contribution.

This is a practical underflow guard: if even the loudest raw speaker weight is negligible, the entire gain vector is treated as numerically insignificant.

### Step 3: accumulate normalized squared weight sum

Instead of computing `sum(w_k^2)` directly, the implementation computes:

```text
sumSq = sum_k((w_k / maxW)^2)
```

Because `0 <= w_k / maxW <= 1`, this guarantees:

```text
1 <= sumSq <= N
```

for `N` active speakers.

This avoids unstable intermediate scaling factors when all raw weights are very small.

### Step 4: recover the true L2 normalizer

The implementation then computes:

```text
kNorm = 1 / (maxW * sqrt(sumSq))
```

Since:

```text
sumSq = sum_k(w_k^2) / maxW^2
```

we get:

```text
maxW * sqrt(sumSq) = sqrt(sum_k(w_k^2))
```

Therefore:

```text
kNorm = 1 / sqrt(sum_k(w_k^2))
```

which is exactly the ordinary L2 normalization constant, just computed in a more numerically stable way.

### Step 5: final gains

Final per-speaker gains are:

```text
v_k = kNorm * w_k
```

So:

```text
sum_k(v_k^2)
= sum_k((kNorm * w_k)^2)
= kNorm^2 * sum_k(w_k^2)
= 1
```

This proves the implemented invariant.

---

## 6. `renderSample()` vs `renderBuffer()`

Both render paths use the same math.

`renderSample()`:
- computes `w_k`
- computes `maxW`, `sumSq`, and `kNorm`
- applies `kNorm * w_k * sample` directly

`renderBuffer()`:
- computes the same normalized gain vector once per block
- stores `gain_k = kNorm * w_k`
- applies each `gain_k` across the frame loop

The block path is therefore mathematically identical to the single-sample path, but avoids recomputing the normalization inside the per-sample loop.

---

## 7. Why Max-Scaled L2 Was Chosen

Simple L2 normalization would be:

```text
kNorm = 1 / sqrt(sum_k(w_k^2))
```

That formula is mathematically correct, but the implemented max-scaled form is preferred because it keeps the accumulation bounded:

- `w_k / maxW` is always in `[0, 1]`
- `sumSq` stays in `[1, N]`
- the computation avoids large reciprocal factors when all weights are tiny

The resulting gain vector is identical to plain L2 normalization.

This is a numerical-stability choice, not a behavioral change.

---

## 8. Speaker Count Constraint

`DBAP_MAX_NUM_SPEAKERS` is a compile-time cap:

```text
DBAP_MAX_NUM_SPEAKERS = 192
```

It sizes:
- `mSpeakerVecs`
- `mDeviceChannels`
- the per-render `std::array<float, DBAP_MAX_NUM_SPEAKERS>` weight buffer
- the per-render `std::array<float, DBAP_MAX_NUM_SPEAKERS>` gain buffer in `renderBuffer()`

The constructor now throws `std::runtime_error` if the provided layout exceeds the cap. This turns the bound into an explicit invariant instead of allowing silent overflow.

---

## 9. Behavior Summary

Expected behavior of the current implementation:

- Source moved closer to a speaker:
  dominant gain increases relative to other speakers, but total squared gain remains `1`
- Focus increased:
  energy concentrates onto the nearest speaker set, but total squared gain remains `1`
- Focus decreased toward `0.1`:
  gain distribution becomes flatter across speakers, but total squared gain remains `1`
- Extremely weak raw weights:
  the `maxW < 1e-6` guard outputs silence instead of amplifying numerical noise

What this implementation does not do:
- it does not preserve `sum(v_k) = 1`
- it does not preserve equal perceived loudness in all rooms or layouts
- it does not change LFE/sub behavior
- it does not compensate for downstream mix decisions

The invariant is specifically:

```text
sum(v_k^2) = 1
```

---

## 10. Consequences For Spatial Root

Because DBAP normalization is now internal to the panner:

- focus no longer needs output gain compensation
- the previous focus auto-compensation layer was structurally obsolete and was removed
- runtime validation should focus on perceptual behavior, clipping, transitions, and sub/main balance, not on restoring lost level from focus changes

This does not guarantee that every rendered scene will have identical perceived loudness. It guarantees only that the DBAP gain vector itself obeys the intended squared-gain invariant.

---

## 11. Locked Decisions

These are intentionally fixed unless a new design decision is documented here first:

- Do not rewrite DBAP from scratch; modify the inherited AlloLib implementation in place
- Do not remove the `1 + dist` floor
- Do not add a legacy/normalized runtime or compile-time switch
- Do not allow focus values below `0.1`
- Do not replace the normalization invariant with `sum(v_k) = 1`

---

## 12. Implementation References

- `internal/cult-allolib/src/sound/al_Dbap.cpp:28`
- `internal/cult-allolib/src/sound/al_Dbap.cpp:78`
- `internal/cult-allolib/include/al/sound/al_Dbap.hpp:81`
- `spatial_engine/realtimeEngine/src/EngineSession.cpp:278`
