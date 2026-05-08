5/7/26

## fix step 1

Here's a summary of what changed and why:

What was broken: mPrevSafePos[si] was always written as the block-center guard-resolved position (safePos), even for fast-mover blocks whose last rendered audio corresponded to the last sub-step (near positionEnd). Under normalized DBAP, a 4× gain difference can exist between block-center and positionEnd positions when they straddle a normalization basin boundary. The Bug 9.1 doBlend on the following block then started from the wrong gain state, injecting a discontinuity instead of smoothing one.

What changed (Spatializer.hpp):

Location Change
Lines 696–698 (fast-mover loop) Capture lastSubSafePos = subSafePos at j == kNumSubSteps - 1
Lines 714–723 (new in-branch state write) Fast-mover branch writes mPrevSafePos = lastSubSafePos, clears mPrevGuardFired, sets mPrevWasFastMover = 1
Line 730 (normal-path state update) Guarded by !isFastMover; clears mPrevWasFastMover = 0
Private section + prepareForSources() New mPrevWasFastMover vector, same pattern as existing flags
Next step to verify: Run 360RA at translab with --device "MOTU Pro Audio" and listen for pops at fast-mover → slow segment transitions. If pops persist, the secondary mechanism (per-sub-step guard variability under normalized DBAP) is likely dominant — that's Bug 10 deferred, now documented in the audit.

## diagnoses (prior to fix)

Regression Analysis: Normalized DBAP — Fast-Mover Logic Breakdown
Context
The normalized DBAP upgrade added L2 normalization to al_Dbap.cpp so that sum(gain_k²) = 1 at all source positions and focus values. This was correct by design (Lossius et al., ICMC 2009, eq. 2). However, it changed the shape of the per-speaker gain function in a way that invalidates assumptions baked into the fast-mover and guard-continuity logic from before the upgrade.

The older fast-mover fix (Bug 2.1) and the guard-transition blending fix (Bug 9.1) were built against a gain function where gain_k ∝ (1/(1+dist_k))^f — a smooth, bounded, monotonic function of distance. No such assumption holds under normalization.

Root Cause: What Changed
Old (unnormalized) DBAP gain function
gain_k(pos) = (1 / (1 + dist_k))^focus
Gains are bounded: gain_k ≤ 1.0 always
Gain is a smooth monotonic function of distance; small position change → small gain change
Near a speaker (dist ≈ 0): gain → 1.0; at the guard boundary (dist = 0.15m, focus=1.5): gain ≈ 0.81
Guard prevents a gain step from ~0.81 → 1.0 — a moderate, bounded discontinuity
New (normalized) DBAP gain function (al_Dbap.cpp lines 104–124)
kNorm = 1 / (maxW × sqrt(sumSq)) where sumSq = Σ(w_k/maxW)²
gain_k = kNorm × w_k
sum(gain_k²) = 1 always — power is position-invariant
At equidistant position (source equidistant from all N=16 speakers): gain_k = 1/sqrt(16) ≈ 0.25
Near one speaker (dist ≈ 0): maxW → 1, sumSq → 1, kNorm → 1, gain_near → 1.0
Result: dominant speaker gain transitions from 0.25 → 1.0 (4× jump) as source approaches
Guard boundary (dist=0.15m) gives gain ≈ 1.0 — nearly the same as dist=0.001m
The guard no longer bounds the gain spike because normalization pushes kNorm up to compensate for small raw weights. The dangerous discontinuity has moved: it no longer lives at the speaker surface — it lives at the normalization basin boundary, the distance at which one speaker starts becoming clearly dominant over the rest.

Most Likely Reason Fast-Mover Logic Broke
The state written at the end of every source render (mPrevSafePos[si], mPrevGuardFired[si]) is always derived from the block-center guard-resolved position (safePos). For fast-mover blocks, the last audio actually rendered corresponds to the last sub-step position (alpha ≈ 0.875, near positionEnd), not block center.

Under unnormalized DBAP: block-center and near-positionEnd positions gave similar gains because the gain function was smooth and low-gradient. The mismatch was sub-threshold.

Under normalized DBAP: these two positions can straddle a normalization basin boundary. If the block-center position has the source equidistant from several speakers (gain ≈ 0.25 per speaker) but positionEnd has it clearly closest to one speaker (gain ≈ 1.0 on that speaker), mPrevSafePos[si] encodes a gain state that is 4× different from what the ear actually heard at the end of the block. The next block's doBlend (Bug 9.1) then blends from this wrong anchor — injecting the discontinuity rather than removing it.

Exact Code Locations Where Old Assumptions No Longer Hold
Location 1 — Spatializer.hpp lines 701–710 (state update, unconditional)
// Bug 9.1 — update guard-transition blending state for next block.
// safePos is the block-center guard-resolved position (normal path).
// Written unconditionally so a following normal-path block always
// has a fresh anchor, regardless of whether this block was a
// fast-mover or not.
if (si < mPrevSafePos.size()) {
mPrevSafePos[si] = safePos;
mPrevSafeValid[si] = 1u;
mPrevGuardFired[si] = guardFiredForSource ? 1u : 0u;
}
The comment acknowledges the mismatch. safePos is the block-center position. For fast-mover blocks the last rendered frame is near positionEnd, not block-center. Under normalized DBAP, this gap can span a normalization basin boundary. The anchor written here is wrong for any subsequent doBlend.

Location 2 — Spatializer.hpp lines 611–613 (doBlend gate — normal path)
const bool doBlend = (si < mPrevSafePos.size())
&& mPrevSafeValid[si]
&& (guardFiredForSource || mPrevGuardFired[si]);
doBlend does not check whether the previous block was a fast-mover. When mPrevGuardFired[si] is set by a fast-mover block (guard fired at block-center), doBlend triggers but blends from the wrong anchor position. Under normalized DBAP, this produces a gain step instead of a smooth transition.

Location 3 — Spatializer.hpp lines 636–698 (fast-mover loop — per-sub-step guard independence)
Each sub-step applies Pass 1 + Pass 2 independently. When a source passes through the guard zone during a fast-mover sweep, sub-steps that trigger the guard are pushed to kMinSpeakerDist = 0.15m — but from slightly different angles (the approach angle changes between sub-steps). Under normalized DBAP, these slight position differences map to different kNorm values. What should be a smooth gain sweep becomes non-monotonic, with each sub-step gain computed against a different normalization factor.

Location 4 — al_Dbap.cpp lines 104–120 (normalization — the source of increased sensitivity)
float kNorm = 1.0f / (maxW \* sqrtf(sumSq));
The normalization factor is a nonlinear function of all speaker distances simultaneously. A position change that moves one speaker from "competing" to "clearly dominant" causes a discontinuous shift in kNorm and therefore a step in every speaker's gain — even speakers that are far away and nominally uninvolved. This amplifies the effect of any position mismatch in the layers above.

Failure Mode Classification
Mechanism Path Severity
mPrevSafePos written as block-center; last audio was near positionEnd; doBlend uses wrong anchor fast-mover → normal transition Primary
Guard fires at different sub-step angles → different kNorm per sub-step → non-monotonic gain within block fast-mover path, guard zone crossing Secondary
mPrevGuardFired set from block-center guard, not sub-step guards → wrong doBlend gating fast-mover → normal transition Contributing
Normal-path doBlend itself: blend between two positions whose kNorm values differ more than expected normal path, guard transition Tertiary (may be fine)
This is primarily a fast-mover path + guard-state interaction problem, exposed and amplified by normalized DBAP's higher gain sensitivity near normalization basin boundaries. The normal-path doBlend (Bug 9.1 fix) is probably still sound for pure normal-path sources — re-opening it is not warranted without evidence.

Smallest Safe Fix Path
Fix A (smallest, do first): Track fast-mover blocks in state; suppress or correct doBlend
Add mPrevWasFastMover (std::vector<uint8_t>, same pattern as mPrevGuardFired) to Spatializer.hpp private section and prepareForSources(). Set it at the end of the source render when the fast-mover path was taken.

Option A1 (suppress): Gate doBlend to false when mPrevWasFastMover[si] is true:

const bool doBlend = (si < mPrevSafePos.size())
&& mPrevSafeValid[si]
&& !mPrevWasFastMover[si] // ← add this
&& (guardFiredForSource || mPrevGuardFired[si]);
This prevents using a known-wrong anchor. The cost: the first normal-path block after a fast-mover block does a single-position render (no blend). Any residual step is bounded by the block-to-block gain change under normalized DBAP — which for a slow-moving source after a fast-mover block should be small.

Option A2 (correct anchor — better): Inside the fast-mover loop, track the last sub-step's guarded position, and write that as mPrevSafePos[si]:

al::Vec3f lastSubSafePos;
for (int j = 0; j < kNumSubSteps; ++j) {
// ... guard and render ...
if (j == kNumSubSteps - 1) lastSubSafePos = subSafePos;
}
// State update
mPrevSafePos[si] = lastSubSafePos; // ← last rendered, not block center
mPrevSafeValid[si] = 1u;
mPrevWasFastMover[si] = 1u;
mPrevGuardFired[si] = 0u; // irrelevant — fast-mover path doesn't use the normal guard gate
This makes the anchor geometrically correct. The doBlend on the following normal-path block would then blend from the actual last-rendered position, not block-center. Guard state written for fast-mover blocks is always ambiguous (block-center vs sub-step), so setting mPrevGuardFired[si] = 0 avoids false-triggering doBlend for non-guard-triggered fast-mover blocks.

Recommendation: implement A2. It is the correct fix rather than a suppression. Change count: ~10 lines in Spatializer.hpp private section, prepareForSources(), the fast-mover loop end, and the state update block.

Fix B (follow-up, previously deferred Bug 9 plan): Pre-guard endpoints
Pre-guard positionStart and positionEnd before the sub-step loop. Lerp between pre-guarded endpoints. Inside the loop, apply Pass 2 only (no Pass 1, single scan, no convergence) as a safety net. This eliminates per-sub-step guard variability entirely — all sub-steps interpolate between two geometrically consistent endpoints. Under normalized DBAP, this also eliminates the kNorm non-monotonicity within a fast-mover block. This was deferred pending evidence; that evidence is now present.

Confirmed vs Speculative
Item Status
Normalized DBAP sum(gain_k²) = 1, gain at guard boundary ≈ gain at speaker surface ≈ 1.0 Confirmed (math from al_Dbap.cpp lines 104–124)
mPrevSafePos[si] always written as block-center safePos, not last sub-step position Confirmed (Spatializer.hpp lines 706–710, comment confirms it)
doBlend fires on fast-mover → normal transition with wrong anchor Confirmed (logic gap at lines 611–613)
Normalized DBAP kNorm is more sensitive to position changes near basin boundaries Confirmed (derived from normalization formula; equidistant gain 0.25, near-speaker gain 1.0)
Per-sub-step guard creates non-monotonic gains under normalized DBAP Confirmed as geometrically real; audibility speculative
Normal-path doBlend (Bug 9.1) is also broken Speculative — may still be sound for pure normal-path sources; no evidence to reopen without testing
360RA isolated pop at ~96 s was an early symptom of this mechanism Plausible but speculative
Files to Modify
spatial_engine/realtimeEngine/src/Spatializer.hpp
Private section: add mPrevWasFastMover vector
prepareForSources(): assign mPrevWasFastMover (default 0)
Fast-mover loop end: capture lastSubSafePos
State update block (lines 701–710): write lastSubSafePos for fast-mover blocks; set mPrevWasFastMover[si]; clear mPrevGuardFired[si]
doBlend gate (lines 611–613): optionally add !mPrevWasFastMover[si] guard (only needed if A1; A2 makes this irrelevant if anchor is correct)
Verification
Build engine-only (./build.sh --engine-only)
Run 360RA test with the translab layout — this is the most aggressive fast-mover content and the one with the prior isolated pop
Watch [CLUSTER] events and SpkG counts; both should remain sparse and non-periodic
Run Ascent and Swale — these were clean under Bug 9.1 and must remain clean
Listen for pops specifically at fast-mover → slow segment transitions (source comes to rest after rapid sweep)
If pops persist, implement Fix B (endpoint pre-guarding) and retest 360RA
