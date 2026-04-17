# findings:

DBAP Focus Investigation Report

1. Relevant Files
   File Role
   thirdparty/allolib/src/sound/al_Dbap.cpp The entire DBAP implementation: gain formula, focus, no normalization
   thirdparty/allolib/include/al/sound/al_Dbap.hpp Public API; focus setter; comment describing focus as an exponent
   thirdparty/allolib/src/sound/al_Speaker.cpp vec() returns speaker Cartesian in audio space
   spatial_engine/realtimeEngine/src/Spatializer.hpp Calls setFocus() and renderBuffer(); disabled compensation at line 1092
   spatial_engine/realtimeEngine/src/Pose.hpp directionToDBAPPosition() applies pre-compensating coord flip at line 480
2. Call Path

RealtimeBackend::processBlock()
→ ControlsSnapshot{focus = smoothed ctrl value}
→ Spatializer::renderBlock(ctrl)
→ mDBap->setFocus(ctrl.focus) // Spatializer.hpp:423
→ mDBap->renderBuffer(mRenderIO, safePos, ...) // Spatializer.hpp:639
— inside al_Dbap.cpp renderBuffer():
relpos = (pos.x, -pos.z, pos.y) // coord flip
dist = |relpos - speakerVec[k]|
gain = pow(1/(1+dist), focus) // THE ONLY MATH
out[k] += gain _ sample // accumulate, no normalize
→ Phase 6: out[k] _= loudspeakerMix (or autoComp=1.0 currently)
→ Phase 7: route mRenderIO → io (copy only, no gain) 3. DBAP Focus Math
The complete per-speaker gain formula (al_Dbap.cpp:54–55):

base_k = 1.0 / (1.0 + dist_k) // ∈ (0, 1] for dist_k ≥ 0
gain_k = base_k ^ focus // = exp(focus × ln(base_k))
out_k += gain_k × sample // direct accumulate — no normalize
Focus enters solely as an exponent applied to the raw inverse-distance weight. There is no other focus term — no blur parameter, no effective-distance rescaling, no spread term.

Critical property: because base_k = 1/(1+dist_k) < 1 for every speaker with dist_k > 0, the function base_k ^ focus is strictly decreasing in focus. Every gain shrinks as focus rises.

4. Normalization Behavior
   There is none. Not L2, not L1, not anything. The AlloLib DBAP in this codebase is a minimal implementation that omits the normalization step that the academic DBAP literature (Lossius et al. 2009) specifies. The academic version normalizes weights so sum(w_k²) = constant, which would preserve total power independent of focus. This implementation does not do that. The gains are raw exponentiated inverse-distances, accumulated directly.

5. Numerical Behavior — 8-Speaker Ring, Front Source (r = 1.0 m)
   Reference: octagon_8.json, source directed at az=0°. The proximity guard pushes the source 0.15 m outward from the front speaker (kMinSpeakerDist = 0.15). Coordinate round-trip (Pose flip + DBAP re-flip) cancels to identity, so distances are computed in the natural audio-space geometry.

Distances from guarded source position to each speaker:

Speaker az dist (m) base gain (f=1)
sp0 (front) 0° 0.15 0.870
sp1, sp7 ±45° 0.834 0.545
sp2, sp6 ±90° 1.524 0.396
sp3, sp5 ±135° 1.987 0.335
sp4 (back) 180° 2.15 0.317
Metrics across focus values:

focus maxG sumG sumG²
0 1.000 8.000 8.000
1 0.870 3.739 1.989
2 0.757 1.989 0.833
4 0.572 0.833 0.290
All three metrics decrease monotonically with focus. The loudest speaker (sp0, nearest) drops from 0.870 at f=1 to 0.572 at f=4, a loss of ~3.7 dB. sumG drops by a factor of ~4.5 from f=1 to f=4. No invariant is conserved.

6. Root Cause of the Global Attenuation
   The attenuation originates entirely inside AlloLib renderBuffer(), before any engine-side gain stage, and before normalization (which does not exist anyway).

The mechanism is simple: every speaker's base gain is a number less than 1. Raising a number less than 1 to a higher power makes it smaller. Focus is that exponent. There is no compensating operation anywhere in the call path — not in DBAP, not in the Phase 6 mix trim (which is currently 1.0 when autoComp is disabled), not in the Phase 7 copy. The attenuation is 100% from the focus exponentiation, with no normalization to counteract it.

The nearest speaker attenuates more slowly than far speakers (which produces the apparent "focusing" effect), but it still attenuates. At focus=1, the front speaker is at 0.870 (-1.2 dBFS from unity). At focus=4, it is at 0.572 (-4.8 dBFS). The more distant speakers crater far faster, which is the useful spatial sharpening behavior — but the loudest one never stays level.

The post-DBAP path in the engine (Spatializer.hpp Phase 6 and Phase 7) adds no additional focus-dependent attenuation. They are neutral for this analysis.

7. Implications for Auto-Compensation
   The previous computeFocusCompensation() was built on two invalid premises, both identified in the Phase 13 disable note at Spatializer.hpp:1097–1116:

Premise 1: "DBAP keeps constant power at focus=0, total power = 1.0"
False. At focus=0, every speaker gain = 1.0 unconditionally. sumG² = N (number of speakers), not 1.0. There is no normalization to divide by √N. The code comment was describing the academic DBAP model, not this implementation.

Premise 2: The reference position (0, radius, 0) tests "front-center" behavior
False. That position is in pose space. renderBuffer() applies the flip (pos.x, -pos.z, pos.y) internally, so it actually tested a source at the top of the sphere in DBAP-internal space. For an azimuth-only ring, every speaker is equidistant from the top, so the test was geometrically blind to azimuth distribution — it saw uniform weights regardless of focus, producing a nonsensical compensation value.

Result: refPower / power was computing N² / something ≈ a large number, which always saturated the ±10 dB clamp, unconditionally applying +10 dB to the output. This made the artifact significantly louder whenever autoComp was on.

The conceptually correct compensation would need to:

Simulate a render at a real directional position (one that produces non-uniform speaker weights), using the coordinate round-trip correctly so the guarded relpos is what DBAP actually sees.
Choose a reference metric to preserve — maxG (loudest speaker) is simplest and most perceptually direct; sumG² (total power) matches the academic theory.
Compute compensation = ref_metric / current_metric (or sqrt of ratio for power).
Accept that compensation is always a boost (because all focus values > 0 produce absolute attenuation vs. focus = 0) — the compensation can not be derived from the assumption of constant power. 8. Recommended Next Step
Before designing a new compensation formula, decide on two things:

A. What is the reference focus level?

Focus=1 is the default and is the only sensible reference: it is the value at which the system is calibrated (all scenes load with this default). Compensating relative to focus=1 means: at focus=1, compensation = 1.0 (no change); at focus=2, apply a boost; at focus=0.5, apply a cut.

B. What metric to preserve?

maxG (amplitude of loudest speaker): simplest; best for ensuring perceived loudness when source is near a speaker stays constant. Compensation = g_ref_near(f=1) / g_near(f).
sumG² (total power): matches standard DBAP theory; better if sources spread evenly. Compensation = sqrt(sumG²(f=1) / sumG²(f)).
Recommendation: use maxG for a first implementation. It is the physically intuitive invariant ("the nearest speaker should always be at the same level"), it is a single float division, and it is robust to layout geometry changes without needing a re-simulation. The simulation-based approach used by the disabled code is the right structure — just fix the reference position (use the correctly flipped+guarded position) and replace the power formula.

Do not implement yet. The next step is to agree on the metric choice first.
