# 4 - 1 - 26 realtime testing

## round 1 - bug fix 9.1 part 1

### results [translab testing]:

- ascent test 1:

* first half no bugs - perfect
* no issues the whole time -

- swale test 1:

* perfect

-

- 360ra test file test 1

* note: since 360ra uses full cartesian coords (y -1 to +1), unlike atmos (y 0 to +1), i select the rescale full sphere, not rescale atmos up

* 1 pop around 96 seconds - that was all

- 360ra test file test 2
- no issues the second time

# 3 - 28 - 26 Realtime Testing

## Round 4

## Assesment / findings:

Log Interpretation First
Before the plan, one critical clarification from the Canyon test log data:

The [RELOC-RENDER] / [RELOC-DEVICE] events are not main-channel relocation. They are the sub channels appearing and disappearing:

0x3ffff = bits 0–17 set (16 main + 2 sub channels active)
0xffff = bits 0–15 set (16 main, sub channels silent)
The mask oscillation tracks subRms exactly: when rBus=0x3ffff, subRms > 0; when rBus=0xffff, subRms=0.0000. This is the LFE content turning on and off as the scene progresses. It is expected behavior. Importantly, rBus and dev always agree — there is no discrepancy between the render bus and device output at the software level.

The MOTU hardware evidence is real but cannot be explained by the render path internals. The render path is correct. The "outside-layout" signal observed on physical outputs is entirely a device-selection problem: the engine opens whatever the system default output device is, not the intended device. If macOS switches defaults, or if the default was never the right device, audio goes elsewhere.

Track A: Explicit Output Device Selection
What is available in AlloLib
al_AudioIO.hpp exposes exactly what is needed:

// Constructor by name keyword (partial match, case-insensitive in RtAudio):
al::AudioDevice dev("MOTU", al::AudioDevice::OUTPUT);
dev.valid() // false if not found
dev.channelsOutMax() // max output channels on that device

// Overload of AudioIO::init() that accepts a device directly:
mAudioIO.init(callback, userData, dev, framesPerBuf, framesPerSec, outChans, inChans);

// Enumerate all devices to stdout (for --list-devices CLI mode):
al::AudioDevice::printAll();
al::AudioDevice::numDevices();
This is the complete API needed. No external library required on the C++ side.

GUI changes — RealtimeInputPanel.py
Add an Output Device row to the input panel, above Buffer Size:

# In \_build_ui(), after the Remap CSV row:

layout.addWidget(self.\_make_row_label("Output Device"))
dev_row = QHBoxLayout()
self.\_device_combo = QComboBox()
self.\_device_combo.addItem("(System Default)", None)

# Populate from sounddevice at construction time:

try:
import sounddevice as sd
for info in sd.query_devices():
if info["max_output_channels"] > 0:
self.\_device_combo.addItem(info["name"], info["name"])
except Exception:
pass # sounddevice not installed — System Default only
self.\_device_combo.setFont(ui_font(8))
dev_row.addWidget(self.\_device_combo)
layout.addLayout(dev_row)
Add to public read API:

def get_output_device(self) -> Optional[str]:
return self.\_device_combo.currentData() # None = system default
Add to set_enabled_for_state(): include self.\_device_combo.

sounddevice is already a common dependency in Python audio projects. If not present, the dropdown falls back to "(System Default)" only — no hard failure.

Python/runner changes — realtime_runner.py
RealtimeConfig dataclass — add one field:

output_device: Optional[str] = None # None = use system default
\_build_args() — add device flag:

if cfg.output_device is not None:
args += ["--device", cfg.output_device]
\_on_start() in realtimeGUI.py — pass the selection:

cfg = RealtimeConfig(
source_path = self.\_input_panel.get_source_path(),
speaker_layout = self.\_input_panel.get_layout_path(),
remap_csv = self.\_input_panel.get_remap_csv(),
output_device = self.\_input_panel.get_output_device(), # new
buffer_size = self.\_input_panel.get_buffer_size(),
)
C++ config changes — RealtimeTypes.hpp
Add to RealtimeConfig:

// ── Output device selection ───────────────────────────────────────────
// Keyword to match against available output device names (partial, case-
// insensitive per RtAudio). Empty string = use system default. Set from
// the --device CLI flag. Must be set before RealtimeBackend::init().
std::string outputDeviceName; // e.g. "MOTU", "Dante", "" = default
C++ main changes — main.cpp
Add --device and --list-devices flags to argument parsing and usage text:

config.outputDeviceName = getArgString(argc, argv, "--device");
Handle --list-devices early in main():

if (hasArg(argc, argv, "--list-devices")) {
std::cout << "[Devices] Available audio output devices:" << std::endl;
al::AudioDevice::printAll();
return 0;
}
Add to usage string: --device <keyword>, --list-devices.

C++ backend changes — RealtimeBackend.hpp
In RealtimeBackend::init(), immediately before the mAudioIO.init(...) call (currently line 101), insert:

// ── Explicit output device selection ─────────────────────────────────
// If the user specified --device, resolve it to an al::AudioDevice and
// validate it before calling init(). Falls through to system default if
// no name was given.
if (!mConfig.outputDeviceName.empty()) {
al::AudioDevice dev(mConfig.outputDeviceName, al::AudioDevice::OUTPUT);

    if (!dev.valid()) {
        std::cerr << "[Backend] FATAL: Output device not found: '"
                  << mConfig.outputDeviceName << "'\n"
                  << "  Run with --list-devices to see available devices."
                  << std::endl;
        return false;
    }

    const int devMaxOut = dev.channelsOutMax();
    if (devMaxOut < mConfig.outputChannels) {
        std::cerr << "[Backend] FATAL: Device '" << mConfig.outputDeviceName
                  << "' has only " << devMaxOut
                  << " output channel(s), but layout requires "
                  << mConfig.outputChannels << ".\n"
                  << "  Check that the correct device and layout are selected."
                  << std::endl;
        return false;
    }

    std::cout << "[Backend] Explicit output device: '" << mConfig.outputDeviceName
              << "' (" << devMaxOut << " ch available)." << std::endl;
    mAudioIO.deviceOut(dev);

}
The existing actualOutChannels < mConfig.outputChannels guard (line 136) remains as the post-open validation.

Failure modes now covered:

--device not specified → system default, same behavior as before (no regression)
Device name not found → dev.valid() fails, clear error, exit before opening hardware
Device found but insufficient channels → channelsOutMax() check, clear error, exit
Device opened with wrong channel count (OS clamped) → existing post-open guard fires
Minimal startup output self-test
Add --test-outputs flag. When present, after backend.init() and before backend.start():

if (hasArg(argc, argv, "--test-outputs")) {
// Play a 100ms 1kHz sine on each output channel sequentially.
// Confirms physical channel mapping before a full render run.
// Uses mAudioIO directly; does not start the full spatializer pipeline.
// ... (≈30 lines, single-threaded, mAudioIO.start / fill / stop loop)
return 0;
}
Scope: one channel at a time, 100ms each, 1kHz sine, then silence. The full implementation is ≈30 lines using mAudioIO.processAudio() in a loop. Propose separately if wanted.

Track A — Exact files and interfaces
File Change
RealtimeTypes.hpp Add std::string outputDeviceName to RealtimeConfig
RealtimeBackend.hpp Device resolution + validation block in init() before mAudioIO.init()
main.cpp Parse --device, --list-devices; assign config.outputDeviceName
realtime_runner.py RealtimeConfig.output_device field; \_build_args() passes --device
RealtimeInputPanel.py Output Device dropdown row; get_output_device() method
realtimeGUI.py Pass output_device when constructing RealtimeConfig in \_on_start()
Track B: Cross-Block Gain Continuity
Where the problem lives now
RealtimeBackend.hpp:354–362 acknowledges the gap explicitly: mPrevChannelGains / mNextChannelGains is a placeholder that assigns identity gains every block. This means no block-boundary gain ramp exists.

Spatializer.hpp normal path (lines ~519–524):

mDBap->renderBuffer(mRenderIO, safePos, mSourceBuffer.data(), numFrames);
The center position safePos can jump significantly between consecutive blocks, producing a step in DBAP speaker gains at the block boundary.

The fast-mover path (kFastMoverAngleRad = 0.25 rad ≈ 14.3°) already addresses this for large angular excursions. The remaining problem is:

Sources crossing the guard zone in under 1 block (guard fires at step N+1 but not N, or vice versa)
Any source where the DBAP cluster shifts enough between safePos[N] and safePos[N+1] to produce an audible click, even if the angular distance is below the fast-mover threshold
Design
Do not directly interpolate DBAP gains. renderBuffer() is a black box; gains aren't exposed. Instead, use position continuity at the block boundary, which is sufficient because DBAP gains are a pure function of position.

Key insight: at the block boundary between block N and block N+1:

End of block N: gains computed from safePos[N]
Start of block N+1: gains computed from safePos[N+1]
If safePos[N] ≈ safePos[N+1] (continuous trajectory), gains are continuous
If guard fires in N+1 but not N, safePos[N+1] is guard-pushed and may differ significantly from safePos[N]
Fix: for sources that had guard activity (guardFiredForSource == true in the current or previous block), apply the same sub-step infrastructure already in place for fast movers, but with only two distinct positions instead of four:

Sub-steps 0, 1 → mPrevSafePos[si] (last block's guard-resolved position)
Sub-steps 2, 3 → safePos (current block's guard-resolved position)
The block boundary is then:

End of block N: rendered at safePos[N] (same as sub-steps 2,3 → safePos[N])
Start of block N+1: rendered at mPrevSafePos[N+1] = safePos[N]
Continuity: same position → same gains → no step
State
All new state goes in Spatializer.hpp private section:

// ── Track B: cross-block gain continuity for guard-transition sources ─
// For sources whose guard fired this block or last block, the normal
// single-position renderBuffer() is replaced with a 4-sub-step render
// where the first two sub-steps use the previous block's safe position
// and the last two use the current block's safe position. This closes
// the block-boundary gain step caused by guard entry/exit.
// Allocated by prepareForSources(); audio-thread-owned after start().
std::vector<al::Vec3f> mPrevSafePos; // safe position at end of last block
std::vector<uint8_t> mPrevSafeValid; // 1 = mPrevSafePos initialized, 0 = first block
std::vector<uint8_t> mPrevGuardFired;// 1 = guard fired in the previous block
Initialization
In prepareForSources(size_t numSources) — append three lines:

mPrevSafePos.assign(numSources, al::Vec3f(0.0f, 0.0f, 0.0f));
mPrevSafeValid.assign(numSources, 0u);
mPrevGuardFired.assign(numSources, 0u);
Modified normal path in renderBlock()
Replace the current if (!isFastMover) block (lines ~519–524) with:

if (!isFastMover) {
const bool prevGuard = (si < mPrevGuardFired.size()) && mPrevGuardFired[si];
const bool prevValid = (si < mPrevSafeValid.size()) && mPrevSafeValid[si];
const bool doBlend = prevValid && (guardFiredForSource || prevGuard);

    if (doBlend) {
        // Guard was active this block or last block → blend old→new position
        // using the existing 4-sub-step scratch infrastructure.
        for (int j = 0; j < kNumSubSteps; ++j) {
            const al::Vec3f& subPos = (j < kNumSubSteps / 2)
                                      ? mPrevSafePos[si]
                                      : safePos;
            mFastMoverScratch.zeroOut();
            mDBap->renderBuffer(mFastMoverScratch, subPos,
                                mSourceBuffer.data() + j * subFrames,
                                subFrames);
            for (unsigned int ch = 0; ch < renderChannels; ++ch) {
                const float* src = mFastMoverScratch.outBuffer(ch);
                float*       dst = mRenderIO.outBuffer(ch);
                for (unsigned int f = 0; f < subFrames; ++f)
                    dst[j * subFrames + f] += src[f];
            }
        }
    } else {
        // Normal path: single position, no extra cost.
        mDBap->renderBuffer(mRenderIO, safePos,
                            mSourceBuffer.data(), numFrames);
    }

}

// Save position state for next block (guard-transition blending).
if (si < mPrevSafePos.size()) {
mPrevSafePos[si] = safePos;
mPrevSafeValid[si] = 1u;
mPrevGuardFired[si] = guardFiredForSource ? 1u : 0u;
}
The subFrames local is already defined in the fast-mover block. Move the definition above the isFastMover test so both paths can use it:

const unsigned int subFrames = numFrames / static_cast<unsigned int>(kNumSubSteps);
Interaction with existing systems
System Interaction
Normal DBAP path Replaced only when doBlend == true (guard active). Zero overhead otherwise.
Fast-mover path Unchanged. Fast movers take if (isFastMover) first; this code is in the else.
mFastMoverScratch Reused as-is. Scratch is already sized to subFrames = numFrames / kNumSubSteps. No new allocation.
Onset fade Applied to mSourceBuffer before this block runs. Unaffected.
Soft-guard (Pass 1 + Pass 2) Still runs before this block. safePos and mPrevSafePos are both guard-resolved.
RealtimeBackend placeholder mPrevChannelGains / mNextChannelGains remains identity — this Track B fix supersedes it for the relevant sources and is more fundamental.
Track B — Exact files and interfaces
File Change
Spatializer.hpp 3 new member vectors (mPrevSafePos, mPrevSafeValid, mPrevGuardFired); extend prepareForSources(); replace normal-path renderBuffer() with doBlend conditional; move subFrames definition above the isFastMover test; save state after the branch.
No other files change for Track B.

Confirmed vs Speculative
Routing relocation (Track A)
Status Finding
Confirmed rBus and dev masks always agree in logs → no relocation in the software render path
Confirmed [RELOC-RENDER/DEVICE] events are sub-channel toggling (0x3ffff ↔ 0xffff = bits 16–17), not main-channel relocation
Confirmed The engine uses mAudioIO.init() without specifying a device → system default only
Confirmed AlloLib AudioIO has deviceOut(AudioDevice) and AudioDevice(nameKeyword, OUTPUT) → explicit selection is directly implementable
Confirmed Post-open channel-count guard exists and catches wrong-device opens
Speculative → resolved by architecture "Outside layout" hardware signal is at the OS/device level, caused by audio going to a device that was never explicitly selected. Track A fix directly eliminates this.
Pops / high-pitched noise (Track B)
Status Finding
Confirmed Fast-mover sub-stepping (Fix 2) is in code and addresses large-angular-motion gain steps
Confirmed Soft-zone guard (kGuardSoftZone = 0.45m) is in code and reduced but did not eliminate Canyon relocation/buzzing
Confirmed Block-boundary gain state for the normal path is an explicit placeholder (identity, not implemented)
Confirmed mFastMoverScratch and subFrames infrastructure is already present and sized correctly for the blending approach
Speculative (strong) Guard-transition pops: sources crossing the soft zone in under 1 block still produce a gain step at the block boundary → addressed by Track B
Speculative Whether the remaining Ascent pops after soft-guard are entirely explained by guard-transition block boundaries, or also have contribution from non-guard DBAP cluster shifts in difficult geometries → confirm by testing Ascent post-Track-B implementation
Still open SpkG=0 test showed pops without any guard activity → there is at least one remaining pop mechanism not guard-related. Do not pursue until guard-transition blending is confirmed clean.

## pre test agent notes

## Round 3

## Assesment / findings:

## v1

1. Backend io zeroing contract
   Verified safe.

RealtimeBackend.hpp:364-366 — Step 1 unconditionally memsets all numChannels output channels to zero on every callback before renderBlock() is called at Step 3. There is no early-return path that bypasses Step 1 and reaches Step 3. The only early return is at line 426 (fully paused), which fires after Step 3 has already completed with zeroed buffers. The += in Phase 7 copy is safe on all paths.

2. Soft-repulsion guard patch candidate
   Diagnosis
   The current guard (Spatializer.hpp:457-471, same structure at 527-548):

if (dist < kMinSpeakerDist) { // kMinSpeakerDist = 0.15f
relpos = spkVec + (delta/dist) \* kMinSpeakerDist; // hard snap
The hard snap is the issue. A source at dist = 0.005m (1 cm from speaker) snaps to 0.15m — a 30× positional displacement in one block. DBAP gains are distance-dependent; that jump changes the panning cluster abruptly. The convergence patch only makes the snap geometrically stable, not perceptually smooth.

Patch design
Two-pass structure in place of the current single-loop guard. Pass 1 (soft zone, single scan, no convergence loop) and Pass 2 (hard floor, convergence loop, identical to current — preserves the safety guarantee):

New constant (add at Spatializer.hpp:986 next to kMinSpeakerDist):

static constexpr float kGuardSoftZone = 0.45f; // outer soft-repulsion boundary (3× hard floor)
Replacement guard block — applies in both line 453 (normal path) and line 533 (fast-mover path):

// Pass 1: Soft outer zone — single scan, no convergence loop needed.
// Smoothly blends position toward kMinSpeakerDist as source approaches.
// Sources near only one speaker need no iteration; the blend is applied once.
for (const auto& spkVec : mSpeakerPositions) {
al::Vec3f delta = relpos - spkVec;
float dist = delta.mag();
if (dist >= kMinSpeakerDist && dist < kGuardSoftZone && dist > 1e-7f) {
// t=0 at outer boundary (no push), t=1 at kMinSpeakerDist (full push)
float t = (kGuardSoftZone - dist) / (kGuardSoftZone - kMinSpeakerDist);
float safeRadius = dist + t _ t _ (kMinSpeakerDist - dist); // quadratic ease-in
relpos = spkVec + (delta / dist) \* safeRadius;
}
}

// Pass 2: Hard inner floor — convergence loop unchanged.
// Catches any source that Pass 1 pushed into another speaker's hard zone.
bool guardFiredForSource = false;
for (int iter = 0; iter < kGuardMaxIter; ++iter) {
bool pushed = false;
for (const auto& spkVec : mSpeakerPositions) {
al::Vec3f delta = relpos - spkVec;
float dist = delta.mag();
if (dist < kMinSpeakerDist) {
relpos = spkVec + ((dist > 1e-7f)
? (delta / dist) \* kMinSpeakerDist
: al::Vec3f(0.0f, kMinSpeakerDist, 0.0f));
pushed = true;
}
}
if (!pushed) break;
guardFiredForSource = true;
}
Why this is safe: in the Allosphere layout (radius ~4.8m, speakers distributed over the sphere surface), inter-speaker distances are much larger than 0.45m. Pass 1 pushing a source toward one speaker's boundary will not enter another speaker's soft zone — so the single-pass is geometrically sufficient without iteration. Pass 2's convergence loop is unchanged and remains the safety net.

The quadratic ease-in (t \* t) ensures the blend is C1-continuous at the outer boundary and steepens gradually toward the hard floor, avoiding a sudden-onset ramp feel.

3. Streaming-side pop/noise mechanism
   Streaming is unlikely to be the leading source of pops or high-pitched noise. Specific findings:

Fix 1 (onset fade) scope: Correct and sufficient for the silence→active transition. The 128-sample ramp fires on the first active block. After that mSourceWasSilent[si] = 0 and no ramp is applied. The second active block has no inter-block gain ramp — but this is a DBAP/spatializer concern, not a streaming concern, because streaming just provides the sample data.

Buffer switch mid-block (getBlock slow path): Streaming.hpp:716-720 — the per-sample fallback correctly handles buffer switches within a block via acquire/release atomics. The switch happens at the exact sample where the buffer boundary falls. There is no sample gap or duplication.

Underrun path: Streaming.hpp:434-444 — returns 0.0f (not the last held sample) after fade. With 10s chunks and 75% preload threshold, structural underruns should not occur. Any underrun would be observable via underrunCount.

The one genuine streaming risk: the hard-snap return of 0.0f (not mFadeGain \* lastSample) at the end of the underrun path means that if an underrun fires mid-playback in a signal with significant energy, it's a hard zero output rather than a fade. This is a theoretical pop path that would only appear in the log as a non-zero underrunCount. Not implicated in the current test results since no underrun data was reported.

Conclusion: streaming does not currently look like the leading source for either Canyon or Ascent.

4. Confirmed vs speculative
   Canyon relocation / strange movement
   Status Finding
   Confirmed Output zeroing is safe — not the cause
   Confirmed Convergence guard patch didn't eliminate relocation — order-dependence was not the root cause
   Confirmed Hard positional snap is structural: sources near speakers jump to the 0.15m exclusion shell in one block, causing a discrete DBAP cluster change regardless of convergence iterations
   Speculative Soft-repulsion patch will reduce or eliminate this for gradual approaches; whether Canyon's specific relocation events are at the guard boundary is unverified without position data at the relocation timestamps
   Still open Whether the relocation is a guard-entry event (source crossing into 0.15m), a guard-exit event (source leaving 0.15m and resuming natural trajectory), or both
   Ascent pops / high-pitched noise
   Status Finding
   Confirmed Streaming is not the leading cause
   Confirmed Fix 1 onset fade protects first active block; subsequent blocks are unprotected by any gain ramp
   Speculative (strong) Hard guard on/off transitions within the fast-mover path: a guard-entry event changes the sub-step position discontinuously between adjacent sub-chunks within the same block, which is not smoothed by the sub-stepping interpolation
   Speculative Inter-block gain step between the last sub-step of block N and the first sub-step of block N+1 — there is no per-channel gain ramp across block boundaries (the mPrevChannelGains/mNextChannelGains mechanism in RealtimeBackend.hpp:354-362 is explicitly a placeholder/identity, not yet implemented)
   Still open Whether pops are at guard-entry (source enters hard zone), guard-exit (leaves it), or at both transitions; confirmed fix requires soft-repulsion patch + post-fix Ascent test

## v2

1. Corrected soft-repulsion formula
   The fix is a symmetric bump: normalize dist into u ∈ [0,1] across the soft zone (0 at the hard floor, 1 at the outer boundary), then apply a parabolic bump u\*(1−u) scaled by the zone width. The bump is strictly positive inside the zone and exactly zero at both endpoints. Since delta = relpos − spkVec points outward, scaling by (dist + push) moves the source away from the speaker.

// Pass 1 — soft outer zone (single scan, no convergence loop)
for (const auto& spkVec : mSpeakerPositions) {
al::Vec3f delta = relpos - spkVec;
float dist = delta.mag();
if (dist > kMinSpeakerDist && dist < kGuardSoftZone && dist > 1e-7f) {
// u = 0 at kMinSpeakerDist, u = 1 at kGuardSoftZone
float u = (dist - kMinSpeakerDist) / (kGuardSoftZone - kMinSpeakerDist);
float push = (kGuardSoftZone - kMinSpeakerDist) _ u _ (1.0f - u);
relpos = spkVec + (delta / dist) \* (dist + push);
}
}
Properties:

dist = kGuardSoftZone: u=1, push=0 — no effect
dist = kMinSpeakerDist: u=0, push=0 — no effect; Pass 2 hard floor takes over seamlessly
dist = midpoint (0.30m with defaults): u=0.5, push = 0.30 \* 0.25 = 0.075m outward — peak bias
newDist never exceeds kGuardSoftZone (maximum of 0.30 + 0.075 = 0.375m < 0.45m)
Pass 2 (convergence loop over kMinSpeakerDist) is identical to the current code and unchanged.

2. Exact patch locations
   New constant — add adjacent to kMinSpeakerDist at Spatializer.hpp:986:

static constexpr float kGuardSoftZone = 0.45f; // outer soft-repulsion boundary
Normal path guard — replace the current convergence loop block at Spatializer.hpp:453–474 with Pass 1 (above) followed by the unchanged Pass 2.

Fast-mover path guard — replace the identical structure at Spatializer.hpp:533–548 with the same two-pass replacement.

The un-flip at line 479 (safePos) and the fast-mover equivalent at line 549 (subSafePos) are unchanged.

3. Track B and test ordering
   Yes, retest Ascent after Canyon confirms clean with the soft-guard patch — same reason as before, and now stronger: the soft-repulsion patch directly addresses the guard-entry position discontinuity that was the stated Track B trigger condition. If the patch smooths out the guard transition in both paths, the sub-step boundary gain step may no longer be large enough to pop. Splitting the work before seeing Canyon results would waste a test cycle and risk attributing a remaining Ascent pop to a cause the guard patch may have already resolved.

Test order: Canyon first. If clean, Ascent. Only if Ascent pops persist at the same timestamps post-patch does Track B (intra-block gain ramp at sub-step guard boundaries) become the next active candidate.

## Pre test agent notes:

All three locations are correct. Summary of what changed:

Spatializer.hpp:1021 — kGuardSoftZone = 0.45f added next to kMinSpeakerDist
Spatializer.hpp:453–489 — normal-path guard replaced with Pass 1 (soft zone scan) + Pass 2 (unchanged hard-floor convergence loop)
Spatializer.hpp:549–577 — fast-mover per-sub-step guard replaced with the same two-pass structure
Nothing else was touched. Test with Canyon first.

- Canyon Test 1:

* speaker gaurds seem to be reduced
* first half of track played with no issue
* first channel reloc happened around 150 sec - only some channels not all
* high pitched buzzing and more reloc around 175 sec - give or take a few sec.
* log:
  [CLUSTER-DEVICE] t=148.49s top4: 0x3028 → 0xf0

t=148.5s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0228 subRms=0.0041 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=148.99s top4: 0x10d0 → 0x78

[CLUSTER-DEVICE] t=148.99s top4: 0x10d0 → 0x78

t=149.0s CPU=34.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0024 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=149.49s top4: 0x78 → 0x5050

[CLUSTER-DEVICE] t=149.49s top4: 0x78 → 0x5050

t=149.5s CPU=34.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0220 subRms=0.0013 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=150.01s top4: 0x2e → 0xca

[CLUSTER-DEVICE] t=150.01s top4: 0x2e → 0xca

t=150.0s CPU=43.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0012 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=150.51s top4: 0x80d0 → 0x804a

[CLUSTER-DEVICE] t=150.51s top4: 0x80d0 → 0x804a

t=150.5s CPU=34.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0002 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=151.01s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=151.01s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=151.01s top4: 0x78 → 0xc050

[CLUSTER-DEVICE] t=151.01s top4: 0x78 → 0xc050

t=151.0s CPU=34.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0167 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=151.52s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=151.52s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=151.52s top4: 0x6a → 0x8e

[CLUSTER-DEVICE] t=151.52s top4: 0x6a → 0x8e

t=151.5s CPU=34.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0131 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=152.02s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=152.02s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=152.02s top4: 0x800e → 0x6a

[CLUSTER-DEVICE] t=152.02s top4: 0x800e → 0x6a

t=152.0s CPU=34.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0120 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=152.52s top4: 0x802a → 0x9048

[CLUSTER-DEVICE] t=152.52s top4: 0x802a → 0x9048

t=152.5s CPU=34.6% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0121 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=153.02s top4: 0x8068 → 0x80d0

[CLUSTER-DEVICE] t=153.02s top4: 0x8068 → 0x80d0

t=153.0s CPU=34.3% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0130 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=153.53s top4: 0x6a → 0x800e

[CLUSTER-DEVICE] t=153.53s top4: 0x6a → 0x800e

t=153.5s CPU=34.2% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0143 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=154.03s top4: 0x802a → 0x80c2

[CLUSTER-DEVICE] t=154.03s top4: 0x802a → 0x80c2

t=154.0s CPU=34.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0136 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=154.53s top4: 0x8058 → 0x5050

[CLUSTER-DEVICE] t=154.53s top4: 0x8058 → 0x5050

t=154.5s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0153 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=155.04s top4: 0x8052 → 0xca

[CLUSTER-DEVICE] t=155.04s top4: 0x8052 → 0xca

t=155.0s CPU=34.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0141 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=155.54s top4: 0x804a → 0x78

[CLUSTER-DEVICE] t=155.54s top4: 0x804a → 0x78

t=155.5s CPU=34.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0113 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=156.04s top4: 0xd040 → 0xc028

[CLUSTER-DEVICE] t=156.04s top4: 0xd040 → 0xc028

t=156.0s CPU=34.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0182 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=156.54s top4: 0x4068 → 0xd040

[CLUSTER-DEVICE] t=156.54s top4: 0x4068 → 0xd040

t=156.5s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0156 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=157.05s top4: 0xe8 → 0x808a

[CLUSTER-DEVICE] t=157.05s top4: 0xe8 → 0x808a

t=157.0s CPU=34.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0117 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=157.55s top4: 0x8b → 0x78

[CLUSTER-DEVICE] t=157.55s top4: 0x8b → 0x78

t=157.5s CPU=34.8% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0107 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=158.05s top4: 0xd2 → 0x78

[CLUSTER-DEVICE] t=158.05s top4: 0xd2 → 0x78

t=158.0s CPU=35.0% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0107 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=158.56s top4: 0x6a → 0x800e

[CLUSTER-DEVICE] t=158.56s top4: 0x6a → 0x800e

t=158.6s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0095 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=159.06s top4: 0x78 → 0x804a

[CLUSTER-DEVICE] t=159.06s top4: 0x78 → 0x804a

t=159.1s CPU=34.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0107 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=159.56s top4: 0x808a → 0x2e

[CLUSTER-DEVICE] t=159.56s top4: 0x808a → 0x2e

t=159.6s CPU=36.3% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0164 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=160.06s top4: 0x78 → 0x40d0

[CLUSTER-DEVICE] t=160.06s top4: 0x78 → 0x40d0

t=160.1s CPU=34.6% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0128 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=160.58s top4: 0x5a → 0xc3

[CLUSTER-DEVICE] t=160.58s top4: 0x5a → 0xc3

t=160.6s CPU=35.1% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0141 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=161.08s top4: 0x1058 → 0x6a

[CLUSTER-DEVICE] t=161.08s top4: 0x1058 → 0x6a

t=161.1s CPU=34.1% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0164 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=161.58s top4: 0x104a → 0x78

[CLUSTER-DEVICE] t=161.58s top4: 0x104a → 0x78

t=161.6s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0202 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=162.08s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=162.08s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=162.08s top4: 0xb8 → 0x10d0

[CLUSTER-DEVICE] t=162.08s top4: 0xb8 → 0x10d0

t=162.1s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0075 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=162.58s top4: 0xaa → 0x10d0

[CLUSTER-DEVICE] t=162.58s top4: 0xaa → 0x10d0

t=162.6s CPU=35.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0320 subRms=0.0119 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=163.08s top4: 0x80d0 → 0x3090

[CLUSTER-DEVICE] t=163.08s top4: 0x80d0 → 0x3090

t=163.1s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0201 subRms=0.0027 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=163.58s top4: 0x9180 → 0x3090

[CLUSTER-DEVICE] t=163.58s top4: 0x9180 → 0x3090

t=163.6s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0333 subRms=0.0042 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=164.10s top4: 0x5050 → 0xc0c0

[CLUSTER-DEVICE] t=164.10s top4: 0x5050 → 0xc0c0

t=164.1s CPU=34.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0040 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=164.60s top4: 0x4103 → 0x5140

[CLUSTER-DEVICE] t=164.60s top4: 0x4103 → 0x5140

t=164.6s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0022 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=165.11s top4: 0x40c8 → 0x78

[CLUSTER-DEVICE] t=165.11s top4: 0x40c8 → 0x78

t=165.1s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0021 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=165.61s top4: 0xc082 → 0x4068

[CLUSTER-DEVICE] t=165.61s top4: 0xc082 → 0x4068

t=165.6s CPU=35.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0005 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=166.11s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=166.11s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=166.11s top4: 0x4068 → 0x82a

[CLUSTER-DEVICE] t=166.11s top4: 0x4068 → 0x82a

t=166.1s CPU=35.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0239 subRms=0.0002 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=166.61s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=166.61s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=166.61s top4: 0xd040 → 0x4058

[CLUSTER-DEVICE] t=166.61s top4: 0xd040 → 0x4058

t=166.6s CPU=35.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0002 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=167.11s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=167.11s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=167.11s top4: 0x4058 → 0xc140

[CLUSTER-DEVICE] t=167.11s top4: 0x4058 → 0xc140

t=167.1s CPU=35.1% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0241 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=167.62s top4: 0x2828 → 0x4068

[CLUSTER-DEVICE] t=167.62s top4: 0x2828 → 0x4068

t=167.6s CPU=35.3% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0221 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=168.12s top4: 0xd040 → 0x4058

[CLUSTER-DEVICE] t=168.12s top4: 0xd040 → 0x4058

t=168.1s CPU=35.3% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0170 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=168.63s top4: 0x4058 → 0xc140

[CLUSTER-DEVICE] t=168.63s top4: 0x4058 → 0xc140

t=168.6s CPU=35.2% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0166 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=169.13s top4: 0x408a → 0xc028

[CLUSTER-DEVICE] t=169.13s top4: 0x408a → 0xc028

t=169.1s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0188 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=169.63s top4: 0xd040 → 0x4058

[CLUSTER-DEVICE] t=169.63s top4: 0xd040 → 0x4058

t=169.6s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0133 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=170.13s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=170.13s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=170.13s top4: 0x3090 → 0x5050

[CLUSTER-DEVICE] t=170.13s top4: 0x3090 → 0x5050

t=170.1s CPU=45.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0313 subRms=0.0100 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=170.65s top4: 0x3810 → 0x10d0

[CLUSTER-DEVICE] t=170.65s top4: 0x3810 → 0x10d0

t=170.6s CPU=35.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0388 subRms=0.0090 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=171.15s top4: 0x40d0 → 0x1070

[CLUSTER-DEVICE] t=171.15s top4: 0x40d0 → 0x1070

t=171.1s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0288 subRms=0.0073 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=171.65s top4: 0x40d0 → 0x408a

[CLUSTER-DEVICE] t=171.65s top4: 0x40d0 → 0x408a

t=171.6s CPU=35.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0156 subRms=0.0029 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=172.15s top4: 0xd2 → 0xc0c0

[CLUSTER-DEVICE] t=172.15s top4: 0xd2 → 0xc0c0

t=172.1s CPU=35.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0028 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=172.65s top4: 0x40a8 → 0xc140

[CLUSTER-DEVICE] t=172.65s top4: 0x40a8 → 0xc140

t=172.7s CPU=35.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0010 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=173.15s top4: 0x40d0 → 0x10b0

[CLUSTER-DEVICE] t=173.15s top4: 0x40d0 → 0x10b0

t=173.2s CPU=34.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0277 subRms=0.0052 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=173.66s top4: 0x183 → 0x8e

[CLUSTER-DEVICE] t=173.66s top4: 0x183 → 0x8e

t=173.7s CPU=35.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0296 subRms=0.0026 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=174.17s top4: 0x40c2 → 0x78

[CLUSTER-DEVICE] t=174.17s top4: 0x40c2 → 0x78

t=174.2s CPU=34.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0010 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=174.67s top4: 0x40d0 → 0xc140

[CLUSTER-DEVICE] t=174.67s top4: 0x40d0 → 0xc140

t=174.7s CPU=34.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0012 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=175.18s top4: 0xd8 → 0x6a

[CLUSTER-DEVICE] t=175.18s top4: 0xd8 → 0x6a

t=175.2s CPU=35.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0238 subRms=0.0007 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=175.68s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=175.68s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=175.68s top4: 0x4052 → 0x40c8

[CLUSTER-DEVICE] t=175.68s top4: 0x4052 → 0x40c8

t=175.7s CPU=35.2% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0153 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=176.18s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=176.18s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=176.18s top4: 0x4052 → 0x183

[CLUSTER-DEVICE] t=176.18s top4: 0x4052 → 0x183

t=176.2s CPU=35.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0177 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[RELOC-RENDER] t=176.68s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=176.68s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=176.68s top4: 0x9a → 0x78

[CLUSTER-DEVICE] t=176.68s top4: 0x9a → 0x78

t=176.7s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0152 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=177.18s top4: 0x408a → 0x4052

[CLUSTER-DEVICE] t=177.18s top4: 0x408a → 0x4052

t=177.2s CPU=35.0% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0169 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=177.70s top4: 0xc090 → 0x4038

[CLUSTER-DEVICE] t=177.70s top4: 0xc090 → 0x4038

t=177.7s CPU=35.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0117 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=178.20s top4: 0x401a → 0x838

[CLUSTER-DEVICE] t=178.20s top4: 0x401a → 0x838

t=178.2s CPU=35.2% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0147 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=178.70s top4: 0xc090 → 0x4083

[CLUSTER-DEVICE] t=178.70s top4: 0xc090 → 0x4083

t=178.7s CPU=35.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0169 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=179.21s top4: 0x8b → 0x4098

[CLUSTER-DEVICE] t=179.21s top4: 0x8b → 0x4098

t=179.2s CPU=34.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0155 subRms=0.0000 Xrun=0 NaN=0 SpkG=30 PLAYING

[CLUSTER-RENDER] t=179.71s top4: 0xaa → 0x87

- notes on this test ^ : wondering if the buffer wasnt totally cleared and thats things collapsed toward the end

- Ascent test 1:

* early pop around 30 secs
* channel reloc around 94 sec
* pop around 112 sec
* more pops around 130
* log:
  [CLUSTER-DEVICE] t=22.15s top4: 0x87 → 0x55

t=22.2s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=23.16s top4: 0x95 → 0x47

[CLUSTER-DEVICE] t=23.16s top4: 0x95 → 0x47

t=23.2s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=23.66s top4: 0x87 → 0x56

[CLUSTER-DEVICE] t=23.66s top4: 0x87 → 0x56

t=23.7s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=25.66s top4: 0x56 → 0xc5

[CLUSTER-DEVICE] t=25.66s top4: 0x56 → 0xc5

t=25.7s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0063 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=27.68s top4: 0xd4 → 0x87

[CLUSTER-DEVICE] t=27.68s top4: 0xd4 → 0x87

t=27.7s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.7s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.7s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.2s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.2s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.7s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=32.2s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=32.71s top4: 0xd4 → 0x8085

[CLUSTER-DEVICE] t=32.71s top4: 0xd4 → 0x8085

t=32.7s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0299 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=33.22s top4: 0xd4 → 0x87

[CLUSTER-DEVICE] t=33.22s top4: 0xd4 → 0x87

t=33.2s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=33.73s top4: 0x87 → 0xd4

[CLUSTER-DEVICE] t=33.73s top4: 0x87 → 0xd4

t=33.7s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.2s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.7s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0073 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=35.23s top4: 0x87 → 0x56

[CLUSTER-DEVICE] t=35.23s top4: 0x87 → 0x56

t=35.2s CPU=20.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=35.73s top4: 0x87 → 0xd4

[CLUSTER-DEVICE] t=35.73s top4: 0x87 → 0xd4

t=35.7s CPU=27.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.25s dom: 0xffff → 0xefef

[DOM-DEVICE] t=36.25s dom: 0xffff → 0xefef

t=36.2s CPU=30.1% rDom=0xefef dDom=0xefef rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.75s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=36.75s dom: 0xdfff → 0xffff

t=36.7s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=37.2s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.76s dom: 0xffff → 0xafaf

[DOM-DEVICE] t=37.76s dom: 0xffff → 0xafaf

[CLUSTER-RENDER] t=37.76s top4: 0x486 → 0x207

[CLUSTER-DEVICE] t=37.76s top4: 0x486 → 0x207

t=37.8s CPU=20.1% rDom=0xafaf dDom=0xafaf rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.26s dom: 0xefef → 0xffff

[DOM-DEVICE] t=38.26s dom: 0xefef → 0xffff

t=38.3s CPU=23.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.76s dom: 0x57df → 0xffff

[DOM-DEVICE] t=38.76s dom: 0x57df → 0xffff

[CLUSTER-RENDER] t=38.76s top4: 0x87 → 0x56

[CLUSTER-DEVICE] t=38.76s top4: 0x87 → 0x56

t=38.8s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.27s dom: 0xd7ff → 0xffff

[DOM-DEVICE] t=39.27s dom: 0xd7ff → 0xffff

t=39.3s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.78s dom: 0xafff → 0xffff

[DOM-DEVICE] t=39.78s dom: 0xafff → 0xffff

t=39.8s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.28s dom: 0x7eff → 0xffff

[DOM-DEVICE] t=40.28s dom: 0x7eff → 0xffff

[CLUSTER-RENDER] t=40.28s top4: 0x87 → 0x56

[CLUSTER-DEVICE] t=40.28s top4: 0x87 → 0x56

t=40.3s CPU=23.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=40.79s top4: 0x56 → 0xc5

[CLUSTER-DEVICE] t=40.79s top4: 0x56 → 0xc5

t=40.8s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=41.29s top4: 0xc3 → 0x56

[CLUSTER-DEVICE] t=41.29s top4: 0xc3 → 0x56

t=41.3s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0111 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=41.79s top4: 0x93 → 0xc6

[CLUSTER-DEVICE] t=41.79s top4: 0x93 → 0xc6

t=41.8s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=42.29s top4: 0x87 → 0xd4

[CLUSTER-DEVICE] t=42.29s top4: 0x87 → 0xd4

t=42.3s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=26.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=43.31s top4: 0x56 → 0xd1

[CLUSTER-DEVICE] t=43.31s top4: 0x56 → 0xd1

t=43.3s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=43.81s top4: 0xd1 → 0xc6

[CLUSTER-DEVICE] t=43.81s top4: 0xd1 → 0xc6

t=43.8s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.8s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=45.82s top4: 0x96 → 0xc3

[CLUSTER-DEVICE] t=45.82s top4: 0x96 → 0xc3

t=45.8s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=46.3s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=46.8s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=47.3s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=47.8s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=48.3s CPU=26.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=48.8s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=49.34s top4: 0xc5 → 0x96

[CLUSTER-DEVICE] t=49.34s top4: 0xc5 → 0x96

t=49.3s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=49.86s top4: 0x87 → 0x56

[CLUSTER-DEVICE] t=49.86s top4: 0x87 → 0x56

t=49.9s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=50.4s CPU=31.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0131 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=50.9s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=51.4s CPU=23.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0111 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=51.9s CPU=20.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.4s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=52.87s top4: 0xd1 → 0x56

[CLUSTER-DEVICE] t=52.87s top4: 0xd1 → 0x56

t=52.9s CPU=23.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=53.39s top4: 0xc5 → 0xd2

[CLUSTER-DEVICE] t=53.39s top4: 0xc5 → 0xd2

t=53.4s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=53.9s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=54.4s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=54.9s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.4s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.9s CPU=31.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=57.4s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=57.9s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0111 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0086 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=58.91s top4: 0xd1 → 0x56

[CLUSTER-DEVICE] t=58.91s top4: 0xd1 → 0x56

t=58.9s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=59.41s top4: 0x56 → 0xd1

[CLUSTER-DEVICE] t=59.41s top4: 0x56 → 0xd1

t=59.4s CPU=31.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0279 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=59.91s top4: 0x56 → 0xc5

[CLUSTER-DEVICE] t=59.91s top4: 0x56 → 0xc5

t=59.9s CPU=20.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=60.4s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=60.9s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=61.43s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=61.43s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=61.43s top4: 0xd1 → 0x56

[CLUSTER-DEVICE] t=61.43s top4: 0xd1 → 0x56

t=61.4s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=61.9s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.4s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.9s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.9s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.4s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=64.96s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=64.96s top4: 0x5050 → 0xd1

t=65.0s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0203 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=65.46s top4: 0xd1 → 0x8181

[CLUSTER-DEVICE] t=65.46s top4: 0xd1 → 0x8181

t=65.5s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=65.96s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=65.96s top4: 0x5050 → 0xd1

t=66.0s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=66.46s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=66.46s top4: 0x5050 → 0xd1

t=66.5s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0089 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.97s dom: 0xf9fd → 0xffff

[DOM-DEVICE] t=66.97s dom: 0xf9fd → 0xffff

t=67.0s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=67.5s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.98s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=67.98s dom: 0xfdff → 0xffff

t=68.0s CPU=20.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.48s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=68.48s dom: 0xfdff → 0xffff

t=68.5s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.99s dom: 0xffff → 0xfdff

[DOM-DEVICE] t=68.99s dom: 0xffff → 0xfdff

t=69.0s CPU=28.2% rDom=0xfdff dDom=0xfdff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.49s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=69.49s dom: 0xfdff → 0xffff

t=69.5s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.98s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=69.98s dom: 0xfdff → 0xffff

t=70.0s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.50s mask: 0xffff → 0x3ffff

t=70.5s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=71.00s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=71.00s mask: 0xffff → 0x3ffff

t=71.0s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.50s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=71.50s dom: 0xfdff → 0xffff

t=71.5s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.00s dom: 0xffff → 0xfdff

[DOM-DEVICE] t=72.00s dom: 0xffff → 0xfdff

t=72.0s CPU=29.9% rDom=0xfdff dDom=0xfdff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.50s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=72.50s dom: 0xfdfd → 0xffff

[CLUSTER-RENDER] t=72.50s top4: 0x5050 → 0x8d0

[CLUSTER-DEVICE] t=72.50s top4: 0x5050 → 0x8d0

t=72.5s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=73.00s top4: 0x8d0 → 0x5050

[CLUSTER-DEVICE] t=73.00s top4: 0x8d0 → 0x5050

t=73.0s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.50s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=73.50s dom: 0xfdff → 0xffff

t=73.5s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.02s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=74.02s top4: 0x8181 → 0xd1

[CLUSTER-DEVICE] t=74.02s top4: 0x8181 → 0xd1

t=74.0s CPU=26.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.52s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=74.52s mask: 0x3ffff → 0xffff

[CLUSTER-RENDER] t=74.52s top4: 0x8d0 → 0x5050

[CLUSTER-DEVICE] t=74.52s top4: 0x8d0 → 0x5050

t=74.5s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0096 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.02s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=75.02s mask: 0x3ffff → 0xffff

t=75.0s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0079 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.52s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.52s mask: 0xffff → 0x3ffff

t=75.5s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=76.02s top4: 0x8d0 → 0x5050

[CLUSTER-DEVICE] t=76.02s top4: 0x8d0 → 0x5050

t=76.0s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=76.52s top4: 0x1850 → 0x40d0

[CLUSTER-DEVICE] t=76.52s top4: 0x1850 → 0x40d0

t=76.5s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=77.0s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=77.5s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0076 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=78.0s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=78.5s CPU=23.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.0s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.5s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.1s CPU=26.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.6s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=81.1s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=81.6s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

t=82.1s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=82.6s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=83.07s top4: 0x1850 → 0xd1

[CLUSTER-DEVICE] t=83.07s top4: 0x1850 → 0xd1

t=83.1s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=83.57s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=83.57s top4: 0x5050 → 0xd1

t=83.6s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=84.07s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=84.07s top4: 0x5050 → 0xd1

t=84.1s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=84.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.1s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=85.59s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=85.59s top4: 0x5050 → 0xd1

t=85.6s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=86.1s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=86.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0255 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=87.09s top4: 0x8181 → 0xd1

[CLUSTER-DEVICE] t=87.09s top4: 0x8181 → 0xd1

t=87.1s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=87.61s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=87.61s top4: 0x5050 → 0xd1

t=87.6s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=88.11s top4: 0x8d0 → 0x5050

[CLUSTER-DEVICE] t=88.11s top4: 0x8d0 → 0x5050

t=88.1s CPU=24.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

t=88.6s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=89.11s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=89.11s mask: 0xffff → 0x3ffff

t=89.1s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=89.61s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=89.61s top4: 0x5050 → 0xd1

t=89.6s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=90.12s top4: 0xd1 → 0x5050

[CLUSTER-DEVICE] t=90.12s top4: 0xd1 → 0x5050

t=90.1s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.62s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=90.62s dom: 0xfdff → 0xffff

t=90.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.13s dom: 0xffff → 0xfbfb

[DOM-DEVICE] t=91.13s dom: 0xffff → 0xfbfb

[CLUSTER-RENDER] t=91.13s top4: 0xd1 → 0x5050

[CLUSTER-DEVICE] t=91.13s top4: 0xd1 → 0x5050

t=91.1s CPU=23.1% rDom=0xfbfb dDom=0xfbfb rBus=0x3ffff dev=0x3ffff mainRms=0.0076 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.63s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=91.63s dom: 0xfbff → 0xffff

t=91.6s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.14s dom: 0xf9fd → 0xffff

[DOM-DEVICE] t=92.14s dom: 0xf9fd → 0xffff

[CLUSTER-RENDER] t=92.14s top4: 0xd1 → 0x5050

[CLUSTER-DEVICE] t=92.14s top4: 0xd1 → 0x5050

t=92.1s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.64s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=92.64s dom: 0xfbff → 0xffff

t=92.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=93.14s top4: 0x8181 → 0xd1

[CLUSTER-DEVICE] t=93.14s top4: 0x8181 → 0xd1

t=93.1s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0053 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=93.64s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=93.64s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=93.64s dom: 0xf9fd → 0xfdfd

[DOM-DEVICE] t=93.64s dom: 0xf9fd → 0xfdfd

[CLUSTER-RENDER] t=93.64s top4: 0xd1 → 0x5050

[CLUSTER-DEVICE] t=93.64s top4: 0xd1 → 0x5050

t=93.6s CPU=28.0% rDom=0xfdfd dDom=0xfdfd rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.15s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.15s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.15s dom: 0xf9f9 → 0xf9fd

[DOM-DEVICE] t=94.15s dom: 0xf9f9 → 0xf9fd

t=94.2s CPU=30.4% rDom=0xf9fd dDom=0xf9fd rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.66s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.66s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.66s dom: 0xd0f3 → 0xd0f1

[DOM-DEVICE] t=94.66s dom: 0xd0f3 → 0xd0f1

t=94.7s CPU=29.8% rDom=0xd0f1 dDom=0xd0f1 rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=95.16s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=95.16s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=95.16s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=95.16s dom: 0xfbff → 0xffff

[CLUSTER-RENDER] t=95.16s top4: 0x5050 → 0x53

[CLUSTER-DEVICE] t=95.16s top4: 0x5050 → 0x53

t=95.2s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=95.66s top4: 0x53 → 0x303

[CLUSTER-DEVICE] t=95.66s top4: 0x53 → 0x303

t=95.7s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0226 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.17s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=96.17s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=96.17s top4: 0x53 → 0x303

[CLUSTER-DEVICE] t=96.17s top4: 0x53 → 0x303

t=96.2s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.67s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=96.67s mask: 0xffff → 0x3ffff

t=96.7s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=97.17s top4: 0x303 → 0x53

[CLUSTER-DEVICE] t=97.17s top4: 0x303 → 0x53

t=97.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.67s dom: 0xdff7 → 0xffff

[DOM-DEVICE] t=97.67s dom: 0xdff7 → 0xffff

t=97.7s CPU=21.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0178 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=98.18s top4: 0x183 → 0x53

[CLUSTER-DEVICE] t=98.18s top4: 0x183 → 0x53

t=98.2s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.68s dom: 0xffdf → 0xffff

[DOM-DEVICE] t=98.68s dom: 0xffdf → 0xffff

t=98.7s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=99.19s top4: 0x183 → 0x53

[CLUSTER-DEVICE] t=99.19s top4: 0x183 → 0x53

t=99.2s CPU=30.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=99.69s top4: 0x183 → 0x53

[CLUSTER-DEVICE] t=99.69s top4: 0x183 → 0x53

t=99.7s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=100.19s top4: 0x53 → 0x183

[CLUSTER-DEVICE] t=100.19s top4: 0x53 → 0x183

t=100.2s CPU=23.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0201 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=100.69s top4: 0x93 → 0x303

[CLUSTER-DEVICE] t=100.69s top4: 0x93 → 0x303

t=100.7s CPU=23.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0222 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.21s dom: 0xdfdf → 0xffff

[DOM-DEVICE] t=101.21s dom: 0xdfdf → 0xffff

[CLUSTER-RENDER] t=101.21s top4: 0x303 → 0x53

[CLUSTER-DEVICE] t=101.21s top4: 0x303 → 0x53

t=101.2s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0209 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=101.71s top4: 0x183 → 0x53

[CLUSTER-DEVICE] t=101.71s top4: 0x183 → 0x53

t=101.7s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0320 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=102.21s top4: 0x53 → 0x183

[CLUSTER-DEVICE] t=102.21s top4: 0x53 → 0x183

t=102.2s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0224 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.71s dom: 0xdfd7 → 0xffff

[DOM-DEVICE] t=102.71s dom: 0xdfd7 → 0xffff

[CLUSTER-RENDER] t=102.71s top4: 0xd1 → 0x8181

[CLUSTER-DEVICE] t=102.71s top4: 0xd1 → 0x8181

t=102.7s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=103.2s CPU=31.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.72s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=103.72s dom: 0xfdfd → 0xffff

[CLUSTER-RENDER] t=103.72s top4: 0xd1 → 0x8181

[CLUSTER-DEVICE] t=103.72s top4: 0xd1 → 0x8181

t=103.7s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=104.22s top4: 0xd1 → 0x5050

[CLUSTER-DEVICE] t=104.22s top4: 0xd1 → 0x5050

t=104.2s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.74s dom: 0xfdf9 → 0xffff

[DOM-DEVICE] t=104.74s dom: 0xfdf9 → 0xffff

[CLUSTER-RENDER] t=104.74s top4: 0x8d0 → 0x5050

[CLUSTER-DEVICE] t=104.74s top4: 0x8d0 → 0x5050

t=104.7s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=105.24s top4: 0x5050 → 0xd1

[CLUSTER-DEVICE] t=105.24s top4: 0x5050 → 0xd1

t=105.2s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0066 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=105.7s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.24s dom: 0xf9f1 → 0xffff

[DOM-DEVICE] t=106.24s dom: 0xf9f1 → 0xffff

t=106.2s CPU=31.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=106.74s top4: 0x2881 → 0xd1

[CLUSTER-DEVICE] t=106.74s top4: 0x2881 → 0xd1

t=106.7s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=107.2s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=107.75s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=107.75s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=107.75s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=107.75s dom: 0xfffd → 0xffff

[CLUSTER-RENDER] t=107.75s top4: 0x8d0 → 0x3050

[CLUSTER-DEVICE] t=107.75s top4: 0x8d0 → 0x3050

t=107.8s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0087 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.26s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.26s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=108.26s top4: 0xd1 → 0x1850

[CLUSTER-DEVICE] t=108.26s top4: 0xd1 → 0x1850

t=108.3s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.76s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.76s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=108.76s top4: 0x8d0 → 0x5050

[CLUSTER-DEVICE] t=108.76s top4: 0x8d0 → 0x5050

t=108.8s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.26s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.26s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=109.26s top4: 0x5050 → 0x8d0

[CLUSTER-DEVICE] t=109.26s top4: 0x5050 → 0x8d0

t=109.3s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0093 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.77s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=109.77s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=109.77s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=109.77s dom: 0xfffd → 0xffff

[CLUSTER-RENDER] t=109.77s top4: 0x5050 → 0x8d0

[CLUSTER-DEVICE] t=109.77s top4: 0x5050 → 0x8d0

t=109.8s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0171 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.27s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.27s mask: 0xffff → 0x3ffff

t=110.3s CPU=31.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=110.8s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.27s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.27s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=111.27s top4: 0x1850 → 0xd1

[CLUSTER-DEVICE] t=111.27s top4: 0x1850 → 0xd1

t=111.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0204 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.79s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.79s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.79s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=111.79s dom: 0xfdfd → 0xffff

[CLUSTER-RENDER] t=111.79s top4: 0xd1 → 0x1850

[CLUSTER-DEVICE] t=111.79s top4: 0xd1 → 0x1850

t=111.8s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=112.29s top4: 0x5050 → 0x8d0

[CLUSTER-DEVICE] t=112.29s top4: 0x5050 → 0x8d0

t=112.3s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=112.79s top4: 0x858 → 0xd1

[CLUSTER-DEVICE] t=112.79s top4: 0x858 → 0xd1

t=112.8s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=113.29s top4: 0x10d0 → 0x858

[CLUSTER-DEVICE] t=113.29s top4: 0x10d0 → 0x858

t=113.3s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0075 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=113.80s top4: 0xd1 → 0x858

[CLUSTER-DEVICE] t=113.80s top4: 0xd1 → 0x858

t=113.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=114.3s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=114.8s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=115.32s top4: 0x80b → 0x143

[CLUSTER-DEVICE] t=115.32s top4: 0x80b → 0x143

t=115.3s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0192 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=115.82s top4: 0x53 → 0x10b

[CLUSTER-DEVICE] t=115.82s top4: 0x53 → 0x10b

t=115.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0205 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=116.32s top4: 0x80b → 0x53

[CLUSTER-DEVICE] t=116.32s top4: 0x80b → 0x53

t=116.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=116.82s top4: 0x8b → 0xd1

[CLUSTER-DEVICE] t=116.82s top4: 0x8b → 0xd1

t=116.8s CPU=20.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=117.33s top4: 0x8b → 0xd1

[CLUSTER-DEVICE] t=117.33s top4: 0x8b → 0xd1

t=117.3s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=117.83s top4: 0x59 → 0x93

[CLUSTER-DEVICE] t=117.83s top4: 0x59 → 0x93

t=117.8s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=118.34s top4: 0x93 → 0x4b

[CLUSTER-DEVICE] t=118.34s top4: 0x93 → 0x4b

t=118.3s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0182 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=118.84s top4: 0x59 → 0xc3

[CLUSTER-DEVICE] t=118.84s top4: 0x59 → 0xc3

t=118.8s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=119.35s top4: 0xc9 → 0x1b

[CLUSTER-DEVICE] t=119.35s top4: 0xc9 → 0x1b

t=119.3s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=119.85s top4: 0x8b → 0x59

[CLUSTER-DEVICE] t=119.85s top4: 0x8b → 0x59

t=119.9s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=120.35s top4: 0xd1 → 0x183

[CLUSTER-DEVICE] t=120.35s top4: 0xd1 → 0x183

t=120.4s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0192 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=120.85s top4: 0x8b → 0xd1

[CLUSTER-DEVICE] t=120.85s top4: 0x8b → 0xd1

t=120.9s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=121.35s top4: 0x183 → 0xd1

[CLUSTER-DEVICE] t=121.35s top4: 0x183 → 0xd1

t=121.4s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=121.86s top4: 0xc9 → 0x2809

[CLUSTER-DEVICE] t=121.86s top4: 0xc9 → 0x2809

t=121.9s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.4s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.9s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=123.37s top4: 0xc9 → 0x858

[CLUSTER-DEVICE] t=123.37s top4: 0xc9 → 0x858

t=123.4s CPU=30.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=123.9s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0071 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=124.37s top4: 0x858 → 0xc9

[CLUSTER-DEVICE] t=124.37s top4: 0x858 → 0xc9

t=124.4s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=124.87s top4: 0x858 → 0x53

[CLUSTER-DEVICE] t=124.87s top4: 0x858 → 0x53

t=124.9s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=125.38s top4: 0x889 → 0xd1

[CLUSTER-DEVICE] t=125.38s top4: 0x889 → 0xd1

t=125.4s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=125.9s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=126.4s CPU=20.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=126.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0178 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=127.4s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=127.9s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=128.39s top4: 0x87 → 0x55

[CLUSTER-DEVICE] t=128.39s top4: 0x87 → 0x55

t=128.4s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=128.9s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=129.4s CPU=38.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=129.90s top4: 0x87 → 0x55

[CLUSTER-DEVICE] t=129.90s top4: 0x87 → 0x55

t=129.9s CPU=24.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=130.4s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=130.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=131.4s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=131.9s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0066 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=132.43s top4: 0x84c → 0x56

[CLUSTER-DEVICE] t=132.43s top4: 0x84c → 0x56

t=132.4s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=132.93s top4: 0x8e → 0x56

[CLUSTER-DEVICE] t=132.93s top4: 0x8e → 0x56

t=132.9s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=133.43s top4: 0x56 → 0xf

[CLUSTER-DEVICE] t=133.43s top4: 0x56 → 0xf

t=133.4s CPU=23.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=133.93s top4: 0x5c → 0x1b

[CLUSTER-DEVICE] t=133.93s top4: 0x5c → 0x1b

t=133.9s CPU=20.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=134.43s top4: 0x1d → 0x56

[CLUSTER-DEVICE] t=134.43s top4: 0x1d → 0x56

t=134.4s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=134.94s top4: 0x80e → 0x8d

[CLUSTER-DEVICE] t=134.94s top4: 0x80e → 0x8d

t=134.9s CPU=20.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=135.45s top4: 0x1e → 0x80d

[CLUSTER-DEVICE] t=135.45s top4: 0x1e → 0x80d

t=135.4s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0288 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=135.9s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0290 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

t=136.4s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0066 Xrun=0 NaN=0 SpkG=0 PLAYING

t=137.0s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0097 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=137.46s top4: 0xf → 0x56

[CLUSTER-DEVICE] t=137.46s top4: 0xf → 0x56

t=137.5s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=137.96s top4: 0x4e → 0x17

[CLUSTER-DEVICE] t=137.96s top4: 0x4e → 0x17

t=138.0s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=138.47s top4: 0xf → 0x59

[CLUSTER-DEVICE] t=138.47s top4: 0xf → 0x59

t=138.5s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=138.98s top4: 0xf → 0x55

[CLUSTER-DEVICE] t=138.98s top4: 0xf → 0x55

t=139.0s CPU=20.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=139.48s top4: 0x1b → 0x47

[CLUSTER-DEVICE] t=139.48s top4: 0x1b → 0x47

t=139.5s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=139.98s top4: 0x53 → 0xf

[CLUSTER-DEVICE] t=139.98s top4: 0x53 → 0xf

t=140.0s CPU=26.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0239 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=140.48s top4: 0x1d → 0x53

[CLUSTER-DEVICE] t=140.48s top4: 0x1d → 0x53

t=140.5s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=140.99s top4: 0x53 → 0xf

[CLUSTER-DEVICE] t=140.99s top4: 0x53 → 0xf

t=141.0s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=141.49s top4: 0x87 → 0x55

[CLUSTER-DEVICE] t=141.49s top4: 0x87 → 0x55

t=141.5s CPU=31.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=141.99s top4: 0x55 → 0xf

[CLUSTER-DEVICE] t=141.99s top4: 0x55 → 0xf

t=142.0s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0196 subRms=0.0063 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=142.51s top4: 0x1e → 0x55

[CLUSTER-DEVICE] t=142.51s top4: 0x1e → 0x55

t=142.5s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=143.01s top4: 0x47 → 0x5c

[CLUSTER-DEVICE] t=143.01s top4: 0x47 → 0x5c

t=143.0s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=143.51s top4: 0x87 → 0x55

[CLUSTER-DEVICE] t=143.51s top4: 0x87 → 0x55

t=143.5s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=144.01s top4: 0xf → 0x56

[CLUSTER-DEVICE] t=144.01s top4: 0xf → 0x56

t=144.0s CPU=22.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=144.51s top4: 0x1d → 0x56

[CLUSTER-DEVICE] t=144.51s top4: 0x1d → 0x56

t=144.5s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

## Round 2

## Assesment /findings:

Pass 1 — Spatializer::renderBlock()
A. Block lifecycle correctness
mRenderIO.zeroOut() called once at line 346, before the source loop. No double-zero, no missed-zero. Normal DBAP path accumulates via renderBuffer() (+=). Fast-mover path accumulates via the manual += loop into the correct frame offset. Both are consistent. No lifecycle bug here.

B. Normal DBAP path
Position is pose.position (block-center, from Pose). Guard applied before renderBuffer() via flip → push → un-flip. Main/sub treated separately in the Phase 6 trim pass via isSubwooferChannel(). No cross-path contamination. Clean.

C. Fast-mover path
Scratch buffer is zeroed (mFastMoverScratch.zeroOut()) at each sub-step before renderBuffer(). Sub-step frame arithmetic is correct (subFrames = numFrames / kNumSubSteps, offsets are j \* subFrames). Coordinate flip/guard/un-flip sequence mirrors the normal path. Accumulation is += src[f] into the correct frame window of mRenderIO. No double-add, no frame-offset error.

D. Guard behavior — Track A finding
[Spatializer.hpp:455-467] — the normal-path proximity guard:

for (const auto& spkVec : mSpeakerPositions) {
al::Vec3f delta = relpos - spkVec;
float dist = delta.mag();
if (dist < kMinSpeakerDist) {
relpos = spkVec + ...; // push away from this speaker
guardFiredForSource = true;
}
}
This is a single sequential pass. After being pushed away from speaker K, the new relpos may now be within kMinSpeakerDist of speaker K+1 (or another adjacent speaker). The loop does not restart — it continues to speaker K+1 and pushes again. The final relpos after iterating all speakers depends on array order and can be geometrically inconsistent.

In Canyon, when speakerProximityCount is rising (~30 events, then ~150s), this means sources are repeatedly triggering multi-speaker pushes. For a source passing through a cluster of nearby speakers, small input-position differences between consecutive blocks produce different multi-push sequences, resulting in materially different final relpos values. DBAP computed from those different positions produces a different speaker-cluster gain distribution → audible cluster relocation and buzzing.

The fast-mover sub-step guard at [Spatializer.hpp:527-536] has the same single-pass structure, but applied to each sub-step independently. This creates an additional issue (see Track B below).

E. Post-render trim / pre-copy
Phase 6 trim loop iterates renderChannels and applies spkMix to all non-sub channels. isSubwooferChannel() is a linear scan — correct but O(N×subs). subMix applied to mSubwooferChannels only. No channel misclassification visible.

The Phase 7 copy is += into io.outBuffer(). If io was not zeroed by the backend before renderBlock(), this would double-add. The comment says "io output buffers must be zeroed BEFORE calling this method" — need to verify backend compliance, but this is the stated contract.

Pass 2 — Pose::computePositions()
A. Center / start / end consistency
The ordering guarantee [Pose.hpp:205-211] is: center (mutating) first → mLastGoodDir updated → start/end (read-only) use that updated direction as fallback. This is correct. When start or end time has a degenerate direction, both fall back to the center direction (mLastGoodDir), making positionStart ≈ positionEnd ≈ position. Angular span ≈ 0 → isFastMover = false. Normal path handles it. No bug.

B. Last-good / safe-direction fallback
safeDirForSource [Pose.hpp:361-408] — when rawDir is degenerate, it reads mLastGoodDir without updating it. This is correct and deterministic. The source stays at its last valid direction rather than snapping to zero. No NaN risk since safeNormalize returns a valid front direction as last resort.

C. Sparse keyframe behavior — secondary Pose finding
interpolateDirRaw [Pose.hpp:320-357] uses binary-search segment lookup by iterating the keyframe array. For a keyframe exactly at t = k[i+1].time, the loop may return either the K/K+1 or K+1/K+2 segment depending on >= boundary handling. For the start-time evaluation, if blockStartTimeSec exactly hits a keyframe boundary, both SLERP segments are valid. This is not a bug but could create sub-sample position jitter if a source has a very dense keyframe at the exact block start time. Unlikely to explain Canyon or Ascent specifically.

No Pose-sourced abrupt-step mechanism discovered beyond what the normal SLERP pipeline produces. The center/start/end positions are geometrically self-consistent when keyframes are valid. Pose hands clean positions to Spatializer.

Pass 3 — Fast-mover sub-step guard discontinuity — Track B finding
This is the most important finding for Ascent. Within the fast-mover loop at [Spatializer.hpp:509-551]:

Each of 4 sub-steps independently applies the proximity guard. Sub-step j has center position at alpha = (j + 0.5) / 4. For a source sweeping through a speaker's guard zone, the sub-step positions might span the 0.15m boundary:

Sub-step 1 center: 0.16m → not pushed → renders with high but finite gain on nearest speaker
Sub-step 2 center: 0.14m → pushed to 0.15m → renders with different gain distribution
Sub-step 3 center: 0.16m → not pushed → back to original gain
Sub-step 4 center: 0.18m → not pushed
The gain vector is discontinuous at sub-step boundaries 1→2 and 2→3. Each boundary is 128 samples (~2.7ms) into the block. This is an intra-block gain step, audible as a click or high-frequency transient. DBAP gain at 0.14m vs 0.15m from a speaker at focus=1.5: d^(-2×1.5) = 0.14^-3 ≈ 364 vs 0.15^-3 ≈ 296 — a 23% gain difference on the dominant speaker channel. That is definitely audible.

This mechanism is scene-dependent (only fires when fast-moving sources pass near speakers), which explains why Ascent shows pops at specific timestamps rather than continuously.

Confirmed vs Speculative
Claim Status
Canyon: sequential guard pushes from adjacent speakers create inconsistent block-to-block positions Confirmed — code structure makes this mechanically certain when speakerProximityCount is high
Canyon: buzzing = repeated guard zone crossings causing cluster shift every N blocks Confirmed — deterministic consequence of single-pass guard + rising guard counter
Ascent: fast-mover intra-block guard binary on/off at sub-step boundaries causes pops Confirmed — guard threshold crossing mid-sweep is geometrically guaranteed for fast sources near speakers
Ascent: pops at ~115s, ~198s, ~345s correlate with fast-mover + guard zone entry Speculative — timing matches, but not yet verified against trajectory data
Pose contributing abrupt positions independently Not supported — Pose is clean; the step is entirely in Spatializer guard
Deliverable

1. Canyon relocation / disappearance / buzzing
   Location: [Spatializer.hpp:455-467] — normal-path proximity guard, single-pass sequential loop.

Mechanism: When a source passes through a cluster of adjacent speakers (Canyon has enough spatial density to make this likely), the single-pass sequential guard pushes relpos away from speaker K, but the new position may land inside speaker K+1's zone, which then pushes it again. The final position is order-dependent and geometrically inconsistent. Between consecutive blocks with slightly different input positions, different speakers dominate the push sequence, producing materially different output relpos values and therefore materially different DBAP gain vectors → cluster relocation. Repeated crossings at 2+ nearby speakers produce the buzzing pattern. The rising SpeakerGuard counter is exactly what confirms this path is hot.

2. Ascent pops / high-pitched transients
   Location: [Spatializer.hpp:527-536] — fast-mover path, per-sub-step independent guard application.

Mechanism: For fast-moving sources, the 4 sub-steps span different distances from nearby speakers. When the sub-step center position crosses the kMinSpeakerDist = 0.15m boundary, the guard fires for some sub-steps but not adjacent ones. The DBAP gain on the dominant nearest-speaker channel changes by ~20% at this boundary (focus=1.5), creating an intra-block step at the sub-step frame boundary (~128 samples into the block). This is a click/pop. The high-pitched component comes from the abrupt step exciting high-frequency content in the DBAP gain function.

3. Smallest safe patches
   Track A — Canyon ([Spatializer.hpp:455-467]):

Run the guard loop in a convergence pass (max 2-3 iterations) rather than a single pass. After any push fires, restart the speaker scan to check whether the new position violates another speaker's zone:

// Replace single-pass loop with convergence loop (max 3 iterations)
for (int iter = 0; iter < 3; ++iter) {
bool pushed = false;
for (const auto& spkVec : mSpeakerPositions) {
al::Vec3f delta = relpos - spkVec;
float dist = delta.mag();
if (dist < kMinSpeakerDist) {
relpos = spkVec + ((dist > 1e-7f)
? (delta / dist) \* kMinSpeakerDist
: al::Vec3f(0.0f, kMinSpeakerDist, 0.0f));
guardFiredForSource = true;
pushed = true;
}
}
if (!pushed) break;
}
No new state required. No allocation. Worst case is 3× the speaker-scan cost, which is still O(N_speakers) and RT-safe. This converges to a position that is outside all speakers' guard zones simultaneously, making the result geometrically consistent and order-independent.

Track B — Ascent ([Spatializer.hpp:509-551]):

Pre-guard positionStart and positionEnd once before the sub-step loop. Then lerp between those pre-guarded positions inside the loop, rather than applying an independent guard per sub-step:

// Pre-guard start and end positions once (same flip/push/un-flip as normal path)
auto applyGuard = [&](al::Vec3f posePos) -> al::Vec3f {
al::Vec3f rp(posePos.x, -posePos.z, posePos.y);
for (const auto& spkVec : mSpeakerPositions) {
al::Vec3f delta = rp - spkVec;
float dist = delta.mag();
if (dist < kMinSpeakerDist)
rp = spkVec + ((dist > 1e-7f)
? (delta / dist) \* kMinSpeakerDist
: al::Vec3f(0.0f, kMinSpeakerDist, 0.0f));
}
return al::Vec3f(rp.x, rp.z, -rp.y); // un-flip
};

al::Vec3f guardedStart = applyGuard(pose.positionStart);
al::Vec3f guardedEnd = applyGuard(pose.positionEnd);

for (int j = 0; j < kNumSubSteps; ++j) {
float alpha = (static*cast<float>(j) + 0.5f) / static_cast<float>(kNumSubSteps);
al::Vec3f subSafePos = guardedStart + alpha * (guardedEnd - guardedStart);
// renormalize:
float mag = subSafePos.mag();
if (mag > 1e-7f) subSafePos = (subSafePos / mag) \_ mLayoutRadius;
// (no per-step guard — already guarded endpoints)
mFastMoverScratch.zeroOut();
mDBap->renderBuffer(mFastMoverScratch, subSafePos,
mSourceBuffer.data() + j \* subFrames, subFrames);
// accumulate...
}
The lerp between two pre-guarded positions is guaranteed smooth. The guard fires at most twice per block per source (once for start, once for end) rather than 4× independently. The transition between sub-steps is now continuous in position space, so the DBAP gain function is continuous within the block. No new persistent state required.

4. Confirmed vs speculative
   Confirmed:

Single-pass guard is the Canyon relocation mechanism (structurally certain given rising guard counter)
Per-sub-step independent guard is the Track B intra-block step mechanism (geometrically provable)
Both are in Spatializer, not in Pose — Pose is producing clean positions
Still speculative:

Whether Canyon's "channel disappearance" involves more than guard-induced relocation (e.g., a source position being pushed to a geometrically degenerate location that silences one speaker cluster entirely — possible but not proven without trajectory data)
Whether Ascent's high-pitched noise is purely the sub-step gain step or also involves a DBAP distance-singularity at the guard boundary (if kMinSpeakerDist is being exited at very small distances before the guard fires, the gain could spike momentarily)
Whether Track A's convergence loop resolves the buzzing fully, or whether some scenes have speaker geometries where no convergent push-out position exists (three speakers forming a triangle smaller than 2×kMinSpeakerDist)

## Pre Test Agent Notes round 2:

1. Patch locations

[Spatializer.hpp:453-467] — normal-path guard (Step 2, between the DBAP-internal flip and the un-flip)
[Spatializer.hpp:527-536] — fast-mover per-sub-step guard (same structure, same fix)
Constants section — added kGuardMaxIter = 4 adjacent to kMinSpeakerDist 2. How the convergence guard works in both paths

Identical structure in both places: an outer for (iter < kGuardMaxIter) loop wraps the speaker scan. A local pushed flag tracks whether any speaker fired this iteration. If the entire speaker scan completes with no push, the loop breaks early — in practice this is the common case (0 or 1 iterations) for sources well clear of all speakers. The outer loop only runs multiple times when a push lands inside another speaker's zone.

For the normal path: guardFiredForSource is set on the first iteration that fires, regardless of subsequent iterations. The speakerProximityCount increment is unchanged.

For the fast-mover path: the structure mirrors the normal path exactly. No diagnostic counter there currently, so no change needed.

3. Track B after Track A

The Track B mechanism (intra-block gain step at sub-step guard boundaries) is logically independent of the convergence fix. However, the convergence guard changes the geometry of what counts as "inside the zone": with multiple nearby speakers, the effective exclusion region is now the union of all guard spheres rather than a sequential-push artifact. This makes the guard result stable and consistent across iterations, which could reduce (but not eliminate) the frequency of sub-step guard on/off flickering for Ascent.

Specifically: if Ascent's fast-moving source was previously getting pushed into the guard zone of a second speaker only because of the sequential first push, the convergence fix now resolves it fully. The sub-step positions after the fix are more geometrically coherent, so guard zone crossings at sub-step boundaries become less likely — but not impossible if the source genuinely passes near a speaker.

Hold Track B as-is. Test Canyon first. If Ascent pops persist at the same timestamps after Canyon testing, the per-sub-step discontinuity is the remaining cause and the patch needs addressing.

## Testing

- Eden test 1:

* 1 pop
* semi audible jumps / movement but no pops, just perceptually strange

- Canyon test 1

* channel reloc a few seconds in

- Canyon test 2

* channel reloc 70 seconds in

## Round 1

## Pre Test Dev Notes:

Switched the buffer size from 512 to 128. Change was maid in spatial_engine/realtimeEngine/src/main.cpp && gui/realtimeGUI/realtime_runner.py. overwritten in the gui with tests as follows:

Offering test 1 [buffer 256 ]

- multiple pops
- cpu use around 30-40%

# 3 - 27 - 26 Realtime Testing

## Testing round 5

## Pre agent test notes 5:

RMS gate — main.cpp:542-617

mainRmsGate is read once before all event blocks. All four DOM/CLUSTER blocks now: clear the latch unconditionally, then gate the print on mainRmsGate > 0.005f. RELOC blocks are untouched. No audio thread changes.

Fix 2 re-enabled — Spatializer.hpp:494-498

The test gate (isFastMover = false + (void)angleDelta) is replaced with the real comparison: isFastMover = (angleDelta > kFastMoverAngleRad). Fast-mover sub-stepping is now active for sources whose block-boundary angular step exceeds the threshold (~14.3°).

What to watch for in the next two runs:

Swale/Canyon (Track A — relocation):

DOM and CLUSTER events will now only print when mainRms > 0.005 — the near-silence churn is suppressed
Check whether CLUSTER prev/next bitmasks represent geometrically opposite speaker clusters (e.g. 0x00ff → 0xff00) or just adjacent shuffling
Check whether render and device still track each other exactly in meaningful-signal windows
Ascent re-run (Track B — pops, Fix 2 now active):

If pops at the previously heard timestamps are reduced or gone → Fix 2 is addressing the mechanism (block-boundary DBAP weight step)
If pops are unchanged → onset/offset or something else is the mechanism; Fix 2 is not the primary cause
Watch SpkG — if it rises with Fix 2 active (more sub-steps hitting the guard), note whether that correlates with any remaining artifacts

- canyon test 1

* around when speaker guard hit 30 - some channels dissapeared, occasional pops, high pitched buzzinfg
* around 150sec - many channels dissapeared, buzzing got worse
* channels shifted again later on

- ascent test 1

* high pithced + channel reloc around 198 sec - give or take 5 seconds

## TESTING ROUND 4

## Pre agent test notes 4:

Patch locations and changes:

RealtimeTypes.hpp:305-322 — 8 new atomics added after subRmsTotal: renderClusterMask, renderClusterPrev, renderClusterNext, renderClusterEvent, and the four device-side equivalents.

Spatializer.hpp:707-737 — render-side block: top-4 computation inserted between renderDomMask store and mainRmsTotal store.

Spatializer.hpp:829-856 — device-side block: identical computation after deviceDomMask store, using numOutputChannels.

main.cpp:580-601 — two new event blocks after the existing DOM block.

How top-4 comparison works:

Four passes over main channels (subs skipped, already-picked channels skipped via bitmask). Each pass takes the current max. After all 4 passes, \_\_builtin_popcountll(clusterMask & prevCluster) counts the shared channels. If < 3 (2+ changed), the latch fires.

Event text:

[CLUSTER-RENDER] t=42.13s top4: 0x0123 → 0x0456
[CLUSTER-DEVICE] t=42.13s top4: 0x0123 → 0x0456
If CLUSTER events still appear after the DOM refinement: they are real. Both prev and next masks will have exactly 4 bits set among the 16 mains, with fewer than 3 in common — meaning the source driving the dominant speaker cluster genuinely relocated 2+ speakers. If those timestamps align with heard pops, the pop is the block-boundary DBAP weight discontinuity and re-enabling Fix 2 is the correct next step.

- Ascent test 1

* first pop - 115~, general spatial presense still seems fine
* second pop - 345 seconds -
* third was from 360 seconds to 370 at some point
* spatial presense was good overall

* result:
  [RELOC-DEVICE] t=303.11s mask: 0x2828 → 0x828

[CLUSTER-RENDER] t=303.11s top4: 0x8 → 0x2828

[CLUSTER-DEVICE] t=303.11s top4: 0x8 → 0x2828

t=303.1s CPU=27.0% rDom=0x0 dDom=0x0 rBus=0x828 dev=0x828 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=303.63s mask: 0x828 → 0x808

[RELOC-DEVICE] t=303.63s mask: 0x828 → 0x808

[CLUSTER-RENDER] t=303.63s top4: 0x828 → 0x808

[CLUSTER-DEVICE] t=303.63s top4: 0x828 → 0x808

t=303.6s CPU=29.9% rDom=0x0 dDom=0x0 rBus=0x808 dev=0x808 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=304.13s mask: 0x8 → 0x808

[RELOC-DEVICE] t=304.13s mask: 0x8 → 0x808

[CLUSTER-RENDER] t=304.13s top4: 0x8 → 0x808

[CLUSTER-DEVICE] t=304.13s top4: 0x8 → 0x808

t=304.1s CPU=29.7% rDom=0x0 dDom=0x0 rBus=0x808 dev=0x808 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=304.63s mask: 0x4000 → 0x8

[RELOC-DEVICE] t=304.63s mask: 0x4000 → 0x8

[CLUSTER-RENDER] t=304.63s top4: 0x8 → 0x8

[CLUSTER-DEVICE] t=304.63s top4: 0x8 → 0x8

t=304.6s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x8 dev=0x8 mainRms=0.0001 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=305.13s mask: 0x8 → 0x2828

[RELOC-DEVICE] t=305.13s mask: 0x8 → 0x2828

[CLUSTER-RENDER] t=305.13s top4: 0x8 → 0x2828

[CLUSTER-DEVICE] t=305.13s top4: 0x8 → 0x2828

t=305.1s CPU=29.9% rDom=0x0 dDom=0x0 rBus=0x2828 dev=0x2828 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=305.63s mask: 0x8 → 0x808

[RELOC-DEVICE] t=305.63s mask: 0x8 → 0x808

[CLUSTER-RENDER] t=305.63s top4: 0x8 → 0x808

[CLUSTER-DEVICE] t=305.63s top4: 0x8 → 0x808

t=305.6s CPU=30.1% rDom=0x0 dDom=0x0 rBus=0x808 dev=0x808 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=306.13s mask: 0xa9a9 → 0x2828

[RELOC-DEVICE] t=306.13s mask: 0xa9a9 → 0x2828

[CLUSTER-RENDER] t=306.13s top4: 0x8 → 0x2828

[CLUSTER-DEVICE] t=306.13s top4: 0x8 → 0x2828

t=306.1s CPU=28.2% rDom=0x0 dDom=0x0 rBus=0x2828 dev=0x2828 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=306.63s mask: 0x2828 → 0x28a8

[RELOC-DEVICE] t=306.63s mask: 0x2828 → 0x28a8

[CLUSTER-RENDER] t=306.63s top4: 0x808 → 0x2828

[CLUSTER-DEVICE] t=306.63s top4: 0x808 → 0x2828

t=306.6s CPU=30.3% rDom=0x0 dDom=0x0 rBus=0x28a8 dev=0x28a8 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=307.14s mask: 0x28a8 → 0x808

[RELOC-DEVICE] t=307.14s mask: 0x28a8 → 0x808

[CLUSTER-RENDER] t=307.14s top4: 0x2828 → 0x808

[CLUSTER-DEVICE] t=307.14s top4: 0x2828 → 0x808

t=307.1s CPU=29.7% rDom=0x0 dDom=0x0 rBus=0x808 dev=0x808 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=307.64s mask: 0x7868 → 0x2828

[RELOC-DEVICE] t=307.64s mask: 0x7868 → 0x2828

[CLUSTER-RENDER] t=307.64s top4: 0x808 → 0x2828

[CLUSTER-DEVICE] t=307.64s top4: 0x808 → 0x2828

t=307.6s CPU=21.7% rDom=0x0 dDom=0x0 rBus=0x2828 dev=0x2828 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=308.15s mask: 0x808 → 0x2828

[RELOC-DEVICE] t=308.15s mask: 0x808 → 0x2828

[CLUSTER-RENDER] t=308.15s top4: 0x808 → 0x2828

[CLUSTER-DEVICE] t=308.15s top4: 0x808 → 0x2828

t=308.1s CPU=27.6% rDom=0x0 dDom=0x0 rBus=0x2828 dev=0x2828 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=308.65s mask: 0xa8a9 → 0x68a8

[RELOC-DEVICE] t=308.65s mask: 0xa8a9 → 0x68a8

[CLUSTER-RENDER] t=308.65s top4: 0x8 → 0x2828

[CLUSTER-DEVICE] t=308.65s top4: 0x8 → 0x2828

t=308.7s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0x68a8 dev=0x68a8 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=309.15s mask: 0xa9a9 → 0x2828

[RELOC-DEVICE] t=309.15s mask: 0xa9a9 → 0x2828

t=309.2s CPU=29.6% rDom=0x0 dDom=0x0 rBus=0x2828 dev=0x2828 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=309.65s mask: 0x2828 → 0x68a8

[RELOC-DEVICE] t=309.65s mask: 0x2828 → 0x68a8

[CLUSTER-RENDER] t=309.65s top4: 0x4888 → 0x2828

[CLUSTER-DEVICE] t=309.65s top4: 0x4888 → 0x2828

t=309.7s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0x68a8 dev=0x68a8 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=310.15s mask: 0x2828 → 0xe9ad

[RELOC-DEVICE] t=310.15s mask: 0x2828 → 0xe9ad

[CLUSTER-RENDER] t=310.15s top4: 0x20e → 0x2888

[CLUSTER-DEVICE] t=310.15s top4: 0x20e → 0x2888

t=310.2s CPU=30.5% rDom=0x0 dDom=0x0 rBus=0xe9ad dev=0xe9ad mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=310.66s mask: 0xffef → 0xa9a9

[RELOC-DEVICE] t=310.66s mask: 0xffef → 0xa9a9

[CLUSTER-RENDER] t=310.66s top4: 0x808 → 0x2828

[CLUSTER-DEVICE] t=310.66s top4: 0x808 → 0x2828

t=310.7s CPU=27.4% rDom=0x0 dDom=0x0 rBus=0xa9a9 dev=0xa9a9 mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=311.16s mask: 0xa9a9 → 0xafaf

[RELOC-DEVICE] t=311.16s mask: 0xa9a9 → 0xafaf

t=311.2s CPU=27.7% rDom=0x0 dDom=0x0 rBus=0xafaf dev=0xafaf mainRms=0.0005 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=311.66s mask: 0xa9a9 → 0xe9a9

[RELOC-DEVICE] t=311.66s mask: 0xa9a9 → 0xe9a9

[CLUSTER-RENDER] t=311.66s top4: 0x808 → 0x2828

[CLUSTER-DEVICE] t=311.66s top4: 0x808 → 0x2828

t=311.7s CPU=30.0% rDom=0x0 dDom=0x0 rBus=0xe9a9 dev=0xe9a9 mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=312.16s mask: 0xf9e9 → 0xf9f9

[RELOC-DEVICE] t=312.16s mask: 0xf9e9 → 0xf9f9

t=312.2s CPU=29.6% rDom=0x0 dDom=0x0 rBus=0xf9f9 dev=0xf9f9 mainRms=0.0005 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=312.66s mask: 0xf9f9 → 0xffff

[RELOC-DEVICE] t=312.66s mask: 0xf9f9 → 0xffff

[CLUSTER-RENDER] t=312.66s top4: 0x4848 → 0x2828

[CLUSTER-DEVICE] t=312.66s top4: 0x4848 → 0x2828

t=312.7s CPU=28.6% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0007 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=313.16s mask: 0xf8c8 → 0xa8a9

[RELOC-DEVICE] t=313.16s mask: 0xf8c8 → 0xa8a9

[CLUSTER-RENDER] t=313.16s top4: 0x48c0 → 0x2828

[CLUSTER-DEVICE] t=313.16s top4: 0x48c0 → 0x2828

t=313.2s CPU=28.5% rDom=0x0 dDom=0x0 rBus=0xa8a9 dev=0xa8a9 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=313.66s mask: 0x6828 → 0x0

[RELOC-DEVICE] t=313.66s mask: 0x6828 → 0x0

[CLUSTER-RENDER] t=313.66s top4: 0x2828 → 0x0

[CLUSTER-DEVICE] t=313.66s top4: 0x2828 → 0x0

t=313.7s CPU=30.1% rDom=0x0 dDom=0x0 rBus=0x6e2e dev=0x6e2e mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=314.18s mask: 0x5040 → 0x5848

[RELOC-DEVICE] t=314.18s mask: 0x5040 → 0x5848

[CLUSTER-RENDER] t=314.18s top4: 0x4000 → 0x5040

[CLUSTER-DEVICE] t=314.18s top4: 0x4000 → 0x5040

t=314.2s CPU=30.5% rDom=0x0 dDom=0x0 rBus=0x5848 dev=0x5848 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=314.68s mask: 0x5056 → 0x5050

[RELOC-DEVICE] t=314.68s mask: 0x5056 → 0x5050

[CLUSTER-RENDER] t=314.68s top4: 0x4046 → 0x5050

[CLUSTER-DEVICE] t=314.68s top4: 0x4046 → 0x5050

t=314.7s CPU=30.1% rDom=0x0 dDom=0x0 rBus=0x5050 dev=0x5050 mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=315.18s mask: 0x4646 → 0x4206

[RELOC-DEVICE] t=315.18s mask: 0x4646 → 0x4206

[CLUSTER-RENDER] t=315.18s top4: 0x5050 → 0x4206

[CLUSTER-DEVICE] t=315.18s top4: 0x5050 → 0x4206

t=315.2s CPU=30.5% rDom=0x0 dDom=0x0 rBus=0x4206 dev=0x4206 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=315.68s mask: 0x5256 → 0x5056

[RELOC-DEVICE] t=315.68s mask: 0x5256 → 0x5056

[CLUSTER-RENDER] t=315.68s top4: 0x4206 → 0x5042

[CLUSTER-DEVICE] t=315.68s top4: 0x4206 → 0x5042

t=315.7s CPU=29.4% rDom=0x0 dDom=0x0 rBus=0x5056 dev=0x5056 mainRms=0.0005 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=316.18s mask: 0x5256 → 0x5050

[RELOC-DEVICE] t=316.18s mask: 0x5256 → 0x5050

[CLUSTER-RENDER] t=316.18s top4: 0x4046 → 0x5050

[CLUSTER-DEVICE] t=316.18s top4: 0x4046 → 0x5050

t=316.2s CPU=28.4% rDom=0x0 dDom=0x0 rBus=0x5050 dev=0x5050 mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=316.68s mask: 0x5050 → 0x5040

[RELOC-DEVICE] t=316.68s mask: 0x5050 → 0x5040

[CLUSTER-RENDER] t=316.68s top4: 0x4046 → 0x5040

[CLUSTER-DEVICE] t=316.68s top4: 0x4046 → 0x5040

t=316.7s CPU=29.2% rDom=0x0 dDom=0x0 rBus=0x5040 dev=0x5040 mainRms=0.0002 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=317.19s mask: 0x4004 → 0x5f4f

[RELOC-DEVICE] t=317.19s mask: 0x4004 → 0x5f4f

[CLUSTER-RENDER] t=317.19s top4: 0x4004 → 0x606

[CLUSTER-DEVICE] t=317.19s top4: 0x4004 → 0x606

t=317.2s CPU=27.6% rDom=0x0 dDom=0x0 rBus=0x5f4f dev=0x5f4f mainRms=0.0007 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=317.70s mask: 0xfefe → 0xffff

[RELOC-DEVICE] t=317.70s mask: 0xfefe → 0xffff

[CLUSTER-RENDER] t=317.70s top4: 0x4006 → 0x2804

[CLUSTER-DEVICE] t=317.70s top4: 0x4006 → 0x2804

t=317.7s CPU=20.2% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0015 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=318.20s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=318.20s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=318.20s dom: 0xffff → 0x0

[DOM-DEVICE] t=318.20s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=318.20s top4: 0xc0c0 → 0x28c0

[CLUSTER-DEVICE] t=318.20s top4: 0xc0c0 → 0x28c0

t=318.2s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0036 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=318.70s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=318.70s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=318.70s top4: 0x28c0 → 0x80d0

[CLUSTER-DEVICE] t=318.70s top4: 0x28c0 → 0x80d0

t=318.7s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0017 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=319.2s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0040 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=319.71s top4: 0x20b0 → 0xa0c0

[CLUSTER-DEVICE] t=319.71s top4: 0x20b0 → 0xa0c0

t=319.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=320.20s top4: 0x80b0 → 0xa0c0

[CLUSTER-DEVICE] t=320.20s top4: 0x80b0 → 0xa0c0

t=320.2s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=320.70s top4: 0x8181 → 0xa880

[CLUSTER-DEVICE] t=320.70s top4: 0x8181 → 0xa880

t=320.7s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=321.22s top4: 0xa880 → 0x8181

[CLUSTER-DEVICE] t=321.22s top4: 0xa880 → 0x8181

t=321.2s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0098 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=321.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=322.2s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=322.73s top4: 0x8d → 0x8181

[CLUSTER-DEVICE] t=322.73s top4: 0x8d → 0x8181

t=322.7s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=323.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=323.73s top4: 0x4181 → 0x8281

[CLUSTER-DEVICE] t=323.73s top4: 0x4181 → 0x8281

t=323.7s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=324.25s top4: 0x381 → 0xc081

[CLUSTER-DEVICE] t=324.25s top4: 0x381 → 0xc081

t=324.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=324.75s top4: 0x381 → 0xc081

[CLUSTER-DEVICE] t=324.75s top4: 0x381 → 0xc081

t=324.7s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=325.25s top4: 0xc081 → 0x2c1

[CLUSTER-DEVICE] t=325.25s top4: 0xc081 → 0x2c1

t=325.2s CPU=22.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=325.75s top4: 0x4380 → 0x8181

[CLUSTER-DEVICE] t=325.75s top4: 0x4380 → 0x8181

t=325.7s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=326.25s top4: 0x4680 → 0x381

[CLUSTER-DEVICE] t=326.25s top4: 0x4680 → 0x381

t=326.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=326.75s top4: 0x4380 → 0x701

[CLUSTER-DEVICE] t=326.75s top4: 0x4380 → 0x701

t=326.8s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0070 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=327.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=327.25s mask: 0xffff → 0x3ffff

t=327.3s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

t=327.8s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=328.27s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=328.27s mask: 0xffff → 0x3ffff

t=328.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=328.77s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=328.77s mask: 0xffff → 0x3ffff

t=328.8s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=329.27s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=329.27s mask: 0xffff → 0x3ffff

t=329.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=329.78s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=329.78s mask: 0xffff → 0x3ffff

t=329.8s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=330.28s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=330.28s mask: 0xffff → 0x3ffff

t=330.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=330.78s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=330.78s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=330.78s top4: 0x4700 → 0x381

[CLUSTER-DEVICE] t=330.78s top4: 0x4700 → 0x381

t=330.8s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=331.29s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=331.29s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=331.29s dom: 0xffff → 0x0

[DOM-DEVICE] t=331.29s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=331.29s top4: 0x8380 → 0x4301

[CLUSTER-DEVICE] t=331.29s top4: 0x8380 → 0x4301

t=331.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0037 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=331.79s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=331.79s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=331.79s dom: 0xffff → 0x0

[DOM-DEVICE] t=331.79s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=331.79s top4: 0x8181 → 0x701

[CLUSTER-DEVICE] t=331.79s top4: 0x8181 → 0x701

t=331.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0025 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=332.30s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=332.30s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=332.30s dom: 0xffff → 0x0

[DOM-DEVICE] t=332.30s dom: 0xffff → 0x0

t=332.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0033 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=332.80s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=332.80s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=332.80s dom: 0xffff → 0x0

[DOM-DEVICE] t=332.80s dom: 0xffff → 0x0

t=332.8s CPU=29.6% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=333.30s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=333.30s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=333.30s dom: 0xffff → 0x0

[DOM-DEVICE] t=333.30s dom: 0xffff → 0x0

t=333.3s CPU=28.1% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0014 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=333.80s dom: 0xffff → 0x0

[DOM-DEVICE] t=333.80s dom: 0xffff → 0x0

t=333.8s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=334.31s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=334.31s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=334.31s dom: 0xffff → 0x0

[DOM-DEVICE] t=334.31s dom: 0xffff → 0x0

t=334.3s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0015 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=334.82s dom: 0xffff → 0x0

[DOM-DEVICE] t=334.82s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=334.82s top4: 0x93 → 0x80c1

[CLUSTER-DEVICE] t=334.82s top4: 0x93 → 0x80c1

t=334.8s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0035 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=335.32s top4: 0x80c1 → 0x93

[CLUSTER-DEVICE] t=335.32s top4: 0x80c1 → 0x93

t=335.3s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=335.8s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0054 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=336.3s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=336.8s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0093 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=337.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=337.8s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=338.3s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=338.8s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=339.35s top4: 0x4281 → 0x8181

[CLUSTER-DEVICE] t=339.35s top4: 0x4281 → 0x8181

t=339.3s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=339.85s top4: 0xc180 → 0xc201

[CLUSTER-DEVICE] t=339.85s top4: 0xc180 → 0xc201

t=339.9s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=340.35s top4: 0x4281 → 0x8301

[CLUSTER-DEVICE] t=340.35s top4: 0x4281 → 0x8301

t=340.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=340.86s top4: 0x8304 → 0x8181

[CLUSTER-DEVICE] t=340.86s top4: 0x8304 → 0x8181

t=340.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=341.37s top4: 0x8380 → 0x704

[CLUSTER-DEVICE] t=341.37s top4: 0x8380 → 0x704

t=341.4s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0064 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=341.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=342.38s top4: 0x8380 → 0x4700

[CLUSTER-DEVICE] t=342.38s top4: 0x8380 → 0x4700

t=342.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=342.9s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=343.4s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=343.9s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=344.4s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=344.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=345.40s dom: 0xffdf → 0xffff

[DOM-DEVICE] t=345.40s dom: 0xffdf → 0xffff

t=345.4s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=345.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=346.41s top4: 0x701 → 0x5300

[CLUSTER-DEVICE] t=346.41s top4: 0x701 → 0x5300

t=346.4s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=346.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

t=347.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=347.9s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0071 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=348.43s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=348.43s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=348.43s top4: 0x701 → 0x4380

[CLUSTER-DEVICE] t=348.43s top4: 0x701 → 0x4380

t=348.4s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=348.94s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=348.94s dom: 0xfff7 → 0xffff

t=348.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=349.44s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=349.44s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=349.44s top4: 0x4380 → 0x701

[CLUSTER-DEVICE] t=349.44s top4: 0x4380 → 0x701

t=349.4s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0043 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=349.9s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0057 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=350.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=350.94s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=350.94s mask: 0xffff → 0x3ffff

t=350.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0039 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=351.4s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0033 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=351.95s dom: 0xffff → 0x0

[DOM-DEVICE] t=351.95s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=351.95s top4: 0x8301 → 0x4700

[CLUSTER-DEVICE] t=351.95s top4: 0x8301 → 0x4700

t=351.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0033 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=352.45s dom: 0xffff → 0x0

[DOM-DEVICE] t=352.45s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=352.45s top4: 0x8301 → 0x780

[CLUSTER-DEVICE] t=352.45s top4: 0x8301 → 0x780

t=352.4s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0027 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=352.96s dom: 0xffff → 0x0

[DOM-DEVICE] t=352.96s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=352.96s top4: 0xc280 → 0x8700

[CLUSTER-DEVICE] t=352.96s top4: 0xc280 → 0x8700

t=353.0s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0024 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=353.46s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=353.46s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=353.46s dom: 0xffff → 0x0

[DOM-DEVICE] t=353.46s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=353.46s top4: 0x8680 → 0x381

[CLUSTER-DEVICE] t=353.46s top4: 0x8680 → 0x381

t=353.5s CPU=30.1% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0016 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=353.96s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=353.96s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=353.96s dom: 0xffff → 0x0

[DOM-DEVICE] t=353.96s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=353.96s top4: 0x780 → 0x8281

[CLUSTER-DEVICE] t=353.96s top4: 0x780 → 0x8281

t=354.0s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=354.46s dom: 0xffff → 0x0

[DOM-DEVICE] t=354.46s dom: 0xffff → 0x0

t=354.5s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0018 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=354.97s dom: 0xffff → 0x0

[DOM-DEVICE] t=354.97s dom: 0xffff → 0x0

t=355.0s CPU=29.9% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0014 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=355.47s dom: 0xffff → 0x0

[DOM-DEVICE] t=355.47s dom: 0xffff → 0x0

t=355.5s CPU=20.6% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0016 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=355.98s dom: 0xffff → 0x0

[DOM-DEVICE] t=355.98s dom: 0xffff → 0x0

t=356.0s CPU=19.8% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0015 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=356.48s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=356.48s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=356.48s dom: 0xffff → 0x0

[DOM-DEVICE] t=356.48s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=356.48s top4: 0x8281 → 0x81c0

[CLUSTER-DEVICE] t=356.48s top4: 0x8281 → 0x81c0

t=356.5s CPU=29.3% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0013 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=356.98s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=356.98s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=356.98s dom: 0xffff → 0x0

[DOM-DEVICE] t=356.98s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=356.98s top4: 0x8181 → 0x8288

[CLUSTER-DEVICE] t=356.98s top4: 0x8181 → 0x8288

t=357.0s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0015 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=357.48s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=357.48s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=357.48s dom: 0xffff → 0x0

[DOM-DEVICE] t=357.48s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=357.48s top4: 0x81c0 → 0x8680

[CLUSTER-DEVICE] t=357.48s top4: 0x81c0 → 0x8680

t=357.5s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0012 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=357.98s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=357.98s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=357.98s dom: 0xffff → 0x0

[DOM-DEVICE] t=357.98s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=357.98s top4: 0x8700 → 0x682

[CLUSTER-DEVICE] t=357.98s top4: 0x8700 → 0x682

t=358.0s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0028 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=358.49s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=358.49s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=358.49s dom: 0xffef → 0xef8f

[DOM-DEVICE] t=358.49s dom: 0xffef → 0xef8f

t=358.5s CPU=29.2% rDom=0xef8f dDom=0xef8f rBus=0x3ffff dev=0x3ffff mainRms=0.0071 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=359.00s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=359.00s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=359.00s dom: 0xefcf → 0xef8f

[DOM-DEVICE] t=359.00s dom: 0xefcf → 0xef8f

t=359.0s CPU=29.4% rDom=0xef8f dDom=0xef8f rBus=0x3ffff dev=0x3ffff mainRms=0.0052 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=359.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=359.50s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=359.50s dom: 0xffff → 0xffaf

[DOM-DEVICE] t=359.50s dom: 0xffff → 0xffaf

t=359.5s CPU=30.1% rDom=0xffaf dDom=0xffaf rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=360.00s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=360.00s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=360.00s dom: 0xffef → 0xffff

[DOM-DEVICE] t=360.00s dom: 0xffef → 0xffff

[CLUSTER-RENDER] t=360.00s top4: 0x606 → 0x8680

[CLUSTER-DEVICE] t=360.00s top4: 0x606 → 0x8680

t=360.0s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=360.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=360.50s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=360.50s dom: 0xffff → 0x0

[DOM-DEVICE] t=360.50s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=360.50s top4: 0x682 → 0x8700

[CLUSTER-DEVICE] t=360.50s top4: 0x682 → 0x8700

t=360.5s CPU=19.9% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0012 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=361.00s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=361.00s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=361.00s dom: 0xffff → 0x0

[DOM-DEVICE] t=361.00s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=361.00s top4: 0x606 → 0x8680

[CLUSTER-DEVICE] t=361.00s top4: 0x606 → 0x8680

t=361.0s CPU=27.0% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0011 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=361.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=361.50s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=361.50s dom: 0xffff → 0x0

[DOM-DEVICE] t=361.50s dom: 0xffff → 0x0

t=361.5s CPU=27.2% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0012 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=362.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=362.02s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=362.02s dom: 0xffff → 0x0

[DOM-DEVICE] t=362.02s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=362.02s top4: 0xa081 → 0x8601

[CLUSTER-DEVICE] t=362.02s top4: 0xa081 → 0x8601

t=362.0s CPU=20.2% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0012 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=362.52s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=362.52s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=362.52s dom: 0xffff → 0x0

[DOM-DEVICE] t=362.52s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=362.52s top4: 0x2e00 → 0x3280

[CLUSTER-DEVICE] t=362.52s top4: 0x2e00 → 0x3280

t=362.5s CPU=28.3% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0013 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=363.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=363.02s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=363.02s dom: 0xffff → 0x0

[DOM-DEVICE] t=363.02s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=363.02s top4: 0x30c0 → 0x3820

[CLUSTER-DEVICE] t=363.02s top4: 0x30c0 → 0x3820

t=363.0s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0029 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=363.52s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=363.52s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=363.52s dom: 0xffff → 0x0

[DOM-DEVICE] t=363.52s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=363.52s top4: 0x2a80 → 0x3820

[CLUSTER-DEVICE] t=363.52s top4: 0x2a80 → 0x3820

t=363.5s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0017 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=364.03s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=364.03s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=364.03s dom: 0xffff → 0x0

[DOM-DEVICE] t=364.03s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=364.03s top4: 0x3820 → 0xaa00

[CLUSTER-DEVICE] t=364.03s top4: 0x3820 → 0xaa00

t=364.0s CPU=25.5% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0012 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=364.53s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=364.53s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=364.53s dom: 0xffff → 0x0

[DOM-DEVICE] t=364.53s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=364.53s top4: 0x2680 → 0x3a00

[CLUSTER-DEVICE] t=364.53s top4: 0x2680 → 0x3a00

t=364.5s CPU=20.0% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0011 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=365.03s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=365.03s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=365.03s dom: 0xffff → 0x0

[DOM-DEVICE] t=365.03s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=365.03s top4: 0xa180 → 0xb800

[CLUSTER-DEVICE] t=365.03s top4: 0xa180 → 0xb800

t=365.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0017 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=365.54s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=365.54s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=365.54s dom: 0xffff → 0x0

[DOM-DEVICE] t=365.54s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=365.54s top4: 0xa180 → 0xb800

[CLUSTER-DEVICE] t=365.54s top4: 0xa180 → 0xb800

t=365.5s CPU=30.3% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0014 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=366.05s dom: 0xffff → 0x0

[DOM-DEVICE] t=366.05s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=366.05s top4: 0x3880 → 0xa0c0

[CLUSTER-DEVICE] t=366.05s top4: 0x3880 → 0xa0c0

t=366.0s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0024 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=366.55s top4: 0x3880 → 0xa0c0

[CLUSTER-DEVICE] t=366.55s top4: 0x3880 → 0xa0c0

t=366.5s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0021 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=367.05s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=367.05s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=367.05s top4: 0x3880 → 0xa0c0

[CLUSTER-DEVICE] t=367.05s top4: 0x3880 → 0xa0c0

t=367.1s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=367.6s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0041 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=368.1s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=368.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=369.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=369.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0058 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=370.08s top4: 0x808c → 0x81c0

[CLUSTER-DEVICE] t=370.08s top4: 0x808c → 0x81c0

t=370.1s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=370.58s top4: 0x808c → 0x8181

[CLUSTER-DEVICE] t=370.58s top4: 0x808c → 0x8181

t=370.6s CPU=22.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=371.08s top4: 0x80c1 → 0xc180

[CLUSTER-DEVICE] t=371.08s top4: 0x80c1 → 0xc180

t=371.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=371.6s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=372.1s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=372.6s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=373.10s top4: 0xc081 → 0x8301

[CLUSTER-DEVICE] t=373.10s top4: 0xc081 → 0x8301

t=373.1s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=373.60s top4: 0x4281 → 0x80c1

[CLUSTER-DEVICE] t=373.60s top4: 0x4281 → 0x80c1

t=373.6s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=374.10s top4: 0xc0c0 → 0x5081

[CLUSTER-DEVICE] t=374.10s top4: 0xc0c0 → 0x5081

t=374.1s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0089 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=374.61s top4: 0xd080 → 0x6201

[CLUSTER-DEVICE] t=374.61s top4: 0xd080 → 0x6201

t=374.6s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0070 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=375.11s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=375.11s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=375.11s top4: 0x6a00 → 0x5300

[CLUSTER-DEVICE] t=375.11s top4: 0x6a00 → 0x5300

t=375.1s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=375.62s top4: 0x4301 → 0x5600

[CLUSTER-DEVICE] t=375.62s top4: 0x4301 → 0x5600

t=375.6s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0057 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=376.12s top4: 0xc300 → 0x6600

[CLUSTER-DEVICE] t=376.12s top4: 0xc300 → 0x6600

t=376.1s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0050 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=376.62s top4: 0xc300 → 0x4680

[CLUSTER-DEVICE] t=376.62s top4: 0xc300 → 0x4680

t=376.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0041 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=377.13s top4: 0x4281 → 0x5401

[CLUSTER-DEVICE] t=377.13s top4: 0x4281 → 0x5401

t=377.1s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0034 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=377.63s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=377.63s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=377.63s top4: 0x4181 → 0xc300

[CLUSTER-DEVICE] t=377.63s top4: 0x4181 → 0xc300

t=377.6s CPU=20.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=378.13s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=378.13s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=378.13s top4: 0xd080 → 0x4181

[CLUSTER-DEVICE] t=378.13s top4: 0xd080 → 0x4181

t=378.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0051 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=378.63s top4: 0x4301 → 0xc180

[CLUSTER-DEVICE] t=378.63s top4: 0x4301 → 0xc180

t=378.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=379.15s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=379.15s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=379.15s dom: 0xffff → 0x0

[DOM-DEVICE] t=379.15s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=379.15s top4: 0xd400 → 0x4580

[CLUSTER-DEVICE] t=379.15s top4: 0xd400 → 0x4580

t=379.1s CPU=20.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0031 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=379.65s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=379.65s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=379.65s top4: 0x8181 → 0x4301

[CLUSTER-DEVICE] t=379.65s top4: 0x8181 → 0x4301

t=379.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=380.15s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=380.15s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=380.15s top4: 0xc300 → 0xd080

[CLUSTER-DEVICE] t=380.15s top4: 0xc300 → 0xd080

t=380.1s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=380.66s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=380.66s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=380.66s top4: 0x5180 → 0x5600

[CLUSTER-DEVICE] t=380.66s top4: 0x5180 → 0x5600

t=380.7s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=381.16s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=381.16s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=381.16s dom: 0xfffb → 0xfff3

[DOM-DEVICE] t=381.16s dom: 0xfffb → 0xfff3

t=381.2s CPU=25.9% rDom=0xfff3 dDom=0xfff3 rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=381.66s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=381.66s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=381.66s top4: 0xc180 → 0x5300

[CLUSTER-DEVICE] t=381.66s top4: 0xc180 → 0x5300

t=381.7s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0035 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=382.18s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=382.18s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=382.18s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=382.18s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=382.18s top4: 0x5050 → 0x5300

[CLUSTER-DEVICE] t=382.18s top4: 0x5050 → 0x5300

t=382.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0046 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=382.68s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=382.68s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=382.68s dom: 0xfffb → 0xffff

[DOM-DEVICE] t=382.68s dom: 0xfffb → 0xffff

[CLUSTER-RENDER] t=382.68s top4: 0xc180 → 0x5090

[CLUSTER-DEVICE] t=382.68s top4: 0xc180 → 0x5090

t=382.7s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=383.18s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=383.18s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=383.18s dom: 0xfffb → 0xffff

[DOM-DEVICE] t=383.18s dom: 0xfffb → 0xffff

t=383.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0041 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=383.69s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=383.69s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=383.69s dom: 0xfff9 → 0xffff

[DOM-DEVICE] t=383.69s dom: 0xfff9 → 0xffff

[CLUSTER-RENDER] t=383.69s top4: 0x5030 → 0xd080

[CLUSTER-DEVICE] t=383.69s top4: 0x5030 → 0xd080

t=383.7s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=384.18s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=384.18s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=384.18s top4: 0x10b0 → 0xc090

[CLUSTER-DEVICE] t=384.18s top4: 0x10b0 → 0xc090

t=384.2s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=384.69s top4: 0x9030 → 0x8190

[CLUSTER-DEVICE] t=384.69s top4: 0x9030 → 0x8190

t=384.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0040 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=385.2s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=385.7s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=386.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0071 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=386.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=387.2s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=387.7s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=388.2s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=388.7s CPU=21.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=389.2s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=389.7s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=390.2s CPU=22.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=390.74s top4: 0x8181 → 0xa00c

[CLUSTER-DEVICE] t=390.74s top4: 0x8181 → 0xa00c

t=390.7s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=391.24s top4: 0x810c → 0xc180

[CLUSTER-DEVICE] t=391.24s top4: 0x810c → 0xc180

t=391.2s CPU=20.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=391.75s top4: 0xe080 → 0xc300

[CLUSTER-DEVICE] t=391.75s top4: 0xe080 → 0xc300

t=391.8s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=392.3s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=392.8s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=393.3s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=393.77s top4: 0xc300 → 0x701

[CLUSTER-DEVICE] t=393.77s top4: 0xc300 → 0x701

t=393.8s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=394.27s dom: 0xffdf → 0xffff

[DOM-DEVICE] t=394.27s dom: 0xffdf → 0xffff

t=394.3s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=394.77s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=394.77s mask: 0xffff → 0x3ffff

t=394.8s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=395.27s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=395.27s mask: 0xffff → 0x3ffff

t=395.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=395.78s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=395.78s dom: 0xfff7 → 0xffff

t=395.8s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0076 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=396.29s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=396.29s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=396.29s dom: 0xf7d3 → 0xffff

[DOM-DEVICE] t=396.29s dom: 0xf7d3 → 0xffff

[CLUSTER-RENDER] t=396.29s top4: 0x5300 → 0xd040

[CLUSTER-DEVICE] t=396.29s top4: 0x5300 → 0xd040

t=396.3s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=396.79s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=396.79s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=396.79s dom: 0xfffb → 0xffff

[DOM-DEVICE] t=396.79s dom: 0xfffb → 0xffff

t=396.8s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=397.29s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=397.29s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=397.29s top4: 0x5140 → 0xd200

[CLUSTER-DEVICE] t=397.29s top4: 0x5140 → 0xd200

t=397.3s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0048 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=397.79s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=397.79s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=397.79s top4: 0x7100 → 0x6280

[CLUSTER-DEVICE] t=397.79s top4: 0x7100 → 0x6280

t=397.8s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=398.29s top4: 0x5300 → 0x7040

[CLUSTER-DEVICE] t=398.29s top4: 0x5300 → 0x7040

t=398.3s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=398.81s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=398.81s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=398.81s top4: 0x4380 → 0x7100

[CLUSTER-DEVICE] t=398.81s top4: 0x4380 → 0x7100

t=398.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0057 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=399.30s top4: 0x2700 → 0x7200

[CLUSTER-DEVICE] t=399.30s top4: 0x2700 → 0x7200

t=399.3s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=399.81s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=399.81s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=399.81s top4: 0x5180 → 0x6300

[CLUSTER-DEVICE] t=399.81s top4: 0x5180 → 0x6300

t=399.8s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0051 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=400.31s top4: 0x4181 → 0xe080

[CLUSTER-DEVICE] t=400.31s top4: 0x4181 → 0xe080

t=400.3s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0034 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=400.81s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=400.81s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=400.81s top4: 0x7800 → 0x6180

[CLUSTER-DEVICE] t=400.81s top4: 0x7800 → 0x6180

t=400.8s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0035 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=401.32s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=401.32s dom: 0xfffd → 0xffff

t=401.3s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0030 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=401.82s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=401.82s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=401.82s dom: 0xffff → 0x0

[DOM-DEVICE] t=401.82s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=401.82s top4: 0xa880 → 0x7800

[CLUSTER-DEVICE] t=401.82s top4: 0xa880 → 0x7800

t=401.8s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0022 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=402.33s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=402.33s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=402.33s dom: 0xffff → 0x0

[DOM-DEVICE] t=402.33s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=402.33s top4: 0xb080 → 0x2a80

[CLUSTER-DEVICE] t=402.33s top4: 0xb080 → 0x2a80

t=402.3s CPU=30.2% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=402.83s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=402.83s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=402.83s dom: 0xffff → 0x0

[DOM-DEVICE] t=402.83s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=402.83s top4: 0xa180 → 0xf000

[CLUSTER-DEVICE] t=402.83s top4: 0xa180 → 0xf000

t=402.8s CPU=27.4% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=403.33s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=403.33s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=403.33s dom: 0xffff → 0x0

[DOM-DEVICE] t=403.33s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=403.33s top4: 0xa180 → 0xc280

[CLUSTER-DEVICE] t=403.33s top4: 0xa180 → 0xc280

t=403.3s CPU=22.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0025 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=403.84s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=403.84s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=403.84s dom: 0xffff → 0x0

[DOM-DEVICE] t=403.84s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=403.84s top4: 0x8380 → 0x6280

[CLUSTER-DEVICE] t=403.84s top4: 0x8380 → 0x6280

t=403.8s CPU=20.0% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0017 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=404.34s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=404.34s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=404.34s dom: 0xffff → 0x0

[DOM-DEVICE] t=404.34s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=404.34s top4: 0xa280 → 0x81c0

[CLUSTER-DEVICE] t=404.34s top4: 0xa280 → 0x81c0

t=404.3s CPU=19.8% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0018 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=404.84s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=404.84s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=404.84s dom: 0xffff → 0x0

[DOM-DEVICE] t=404.84s dom: 0xffff → 0x0

t=404.8s CPU=29.9% rDom=0x0 dDom=0x0 rBus=0xffff dev=0xffff mainRms=0.0014 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=405.34s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=405.34s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=405.34s dom: 0xffff → 0x0

[DOM-DEVICE] t=405.34s dom: 0xffff → 0x0

t=405.3s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0019 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=405.85s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=405.85s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=405.85s dom: 0xffff → 0x0

[DOM-DEVICE] t=405.85s dom: 0xffff → 0x0

t=405.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0023 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=406.35s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=406.35s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=406.35s dom: 0xffff → 0x0

[DOM-DEVICE] t=406.35s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=406.35s top4: 0x8700 → 0x682

[CLUSTER-DEVICE] t=406.35s top4: 0x8700 → 0x682

t=406.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0032 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=406.86s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=406.86s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=406.86s dom: 0xffef → 0xffff

[DOM-DEVICE] t=406.86s dom: 0xffef → 0xffff

[CLUSTER-RENDER] t=406.86s top4: 0x702 → 0x8680

[CLUSTER-DEVICE] t=406.86s top4: 0x702 → 0x8680

t=406.9s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0050 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=407.36s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=407.36s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=407.36s dom: 0xef8f → 0xaf8f

[DOM-DEVICE] t=407.36s dom: 0xef8f → 0xaf8f

t=407.4s CPU=30.1% rDom=0xaf8f dDom=0xaf8f rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=407.86s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=407.86s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=407.86s dom: 0xffff → 0xffcf

[DOM-DEVICE] t=407.86s dom: 0xffff → 0xffcf

[CLUSTER-RENDER] t=407.86s top4: 0x8680 → 0x606

[CLUSTER-DEVICE] t=407.86s top4: 0x8680 → 0x606

t=407.9s CPU=19.7% rDom=0xffcf dDom=0xffcf rBus=0x3ffff dev=0x3ffff mainRms=0.0037 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=408.36s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=408.36s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=408.36s dom: 0xffff → 0x0

[DOM-DEVICE] t=408.36s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=408.36s top4: 0x8680 → 0x606

[CLUSTER-DEVICE] t=408.36s top4: 0x8680 → 0x606

t=408.4s CPU=24.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0030 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=408.87s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=408.87s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=408.87s dom: 0xffff → 0x0

[DOM-DEVICE] t=408.87s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=408.87s top4: 0x606 → 0x8680

[CLUSTER-DEVICE] t=408.87s top4: 0x606 → 0x8680

t=408.9s CPU=25.9% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0012 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=409.38s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=409.38s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=409.38s dom: 0xffff → 0x0

[DOM-DEVICE] t=409.38s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=409.38s top4: 0x80c1 → 0x681

[CLUSTER-DEVICE] t=409.38s top4: 0x80c1 → 0x681

t=409.4s CPU=29.4% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0015 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=409.88s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=409.88s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=409.88s dom: 0xffff → 0x0

[DOM-DEVICE] t=409.88s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=409.88s top4: 0x8181 → 0x8680

[CLUSTER-DEVICE] t=409.88s top4: 0x8181 → 0x8680

t=409.9s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0009 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=410.38s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=410.38s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=410.38s top4: 0x80c1 → 0x8284

[CLUSTER-DEVICE] t=410.38s top4: 0x80c1 → 0x8284

t=410.4s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0008 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=410.88s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=410.88s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=410.88s top4: 0xc180 → 0x82c0

[CLUSTER-DEVICE] t=410.88s top4: 0xc180 → 0x82c0

t=410.9s CPU=29.0% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0010 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=411.39s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=411.39s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=411.39s top4: 0x8181 → 0xc0c0

[CLUSTER-DEVICE] t=411.39s top4: 0x8181 → 0xc0c0

t=411.4s CPU=19.6% rDom=0x0 dDom=0x0 rBus=0x3ffff dev=0x3ffff mainRms=0.0009 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=411.89s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=411.89s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=411.89s dom: 0xffff → 0x0

[DOM-DEVICE] t=411.89s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=411.89s top4: 0x8680 → 0xc180

[CLUSTER-DEVICE] t=411.89s top4: 0x8680 → 0xc180

t=411.9s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0021 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=412.39s dom: 0xffff → 0x0

[DOM-DEVICE] t=412.39s dom: 0xffff → 0x0

[CLUSTER-RENDER] t=412.39s top4: 0xb8 → 0x80d0

[CLUSTER-DEVICE] t=412.39s top4: 0xb8 → 0x80d0

t=412.4s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0028 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=412.9s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0040 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=413.40s top4: 0x80c4 → 0xf0

[CLUSTER-DEVICE] t=413.40s top4: 0x80c4 → 0xf0

t=413.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=413.90s top4: 0x95 → 0x8181

[CLUSTER-DEVICE] t=413.90s top4: 0x95 → 0x8181

t=413.9s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0044 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=414.41s top4: 0x8181 → 0x80b0

[CLUSTER-DEVICE] t=414.41s top4: 0x8181 → 0x80b0

t=414.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=414.91s top4: 0x80c1 → 0x80b0

[CLUSTER-DEVICE] t=414.91s top4: 0x80c1 → 0x80b0

t=414.9s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0098 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=415.41s top4: 0x8181 → 0x80b0

[CLUSTER-DEVICE] t=415.41s top4: 0x8181 → 0x80b0

t=415.4s CPU=22.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=415.93s top4: 0xd1 → 0x8181

[CLUSTER-DEVICE] t=415.93s top4: 0xd1 → 0x8181

t=415.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=416.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=416.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=417.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=417.94s top4: 0x4281 → 0x8181

[CLUSTER-DEVICE] t=417.94s top4: 0x4281 → 0x8181

t=417.9s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=418.44s top4: 0x8380 → 0xc101

[CLUSTER-DEVICE] t=418.44s top4: 0x8380 → 0xc101

t=418.4s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=418.9s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=419.45s top4: 0xc280 → 0x381

[CLUSTER-DEVICE] t=419.45s top4: 0xc280 → 0x381

t=419.4s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=419.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=420.45s top4: 0x8181 → 0xc300

[CLUSTER-DEVICE] t=420.45s top4: 0x8181 → 0xc300

t=420.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.2500

[OSC] gain → 0.3100

[CLUSTER-RENDER] t=420.96s top4: 0xc101 → 0xd200

[CLUSTER-DEVICE] t=420.96s top4: 0xc101 → 0xd200

t=421.0s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.3300

[OSC] gain → 0.3600

[OSC] gain → 0.3800

t=421.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=421.96s top4: 0xd100 → 0x4301

[CLUSTER-DEVICE] t=421.96s top4: 0xd100 → 0x4301

t=422.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=422.47s top4: 0xd100 → 0x4301

[CLUSTER-DEVICE] t=422.47s top4: 0xd100 → 0x4301

t=422.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=422.98s top4: 0x5180 → 0x4301

[CLUSTER-DEVICE] t=422.98s top4: 0x5180 → 0x4301

t=423.0s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=423.5s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=424.0s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=424.49s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=424.49s mask: 0xffff → 0x3ffff

t=424.5s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0215 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=424.99s top4: 0xd100 → 0x4301

[CLUSTER-DEVICE] t=424.99s top4: 0xd100 → 0x4301

t=425.0s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=425.49s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=425.49s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=425.49s top4: 0xc180 → 0x4301

[CLUSTER-DEVICE] t=425.49s top4: 0xc180 → 0x4301

t=425.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=425.99s dom: 0xfff3 → 0xffff

[DOM-DEVICE] t=425.99s dom: 0xfff3 → 0xffff

[CLUSTER-RENDER] t=425.99s top4: 0x5101 → 0xc180

[CLUSTER-DEVICE] t=425.99s top4: 0x5101 → 0xc180

t=426.0s CPU=25.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=426.50s dom: 0xfff7 → 0xffff

[DOM-DEVICE] t=426.50s dom: 0xfff7 → 0xffff

[CLUSTER-RENDER] t=426.50s top4: 0xd080 → 0x5300

[CLUSTER-DEVICE] t=426.50s top4: 0xd080 → 0x5300

t=426.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=427.00s dom: 0xfbfb → 0xffff

[DOM-DEVICE] t=427.00s dom: 0xfbfb → 0xffff

[CLUSTER-RENDER] t=427.00s top4: 0x381 → 0xc300

[CLUSTER-DEVICE] t=427.00s top4: 0x381 → 0xc300

t=427.0s CPU=22.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=427.50s dom: 0xfffb → 0xffff

[DOM-DEVICE] t=427.50s dom: 0xfffb → 0xffff

[CLUSTER-RENDER] t=427.50s top4: 0xd040 → 0x5101

[CLUSTER-DEVICE] t=427.50s top4: 0xd040 → 0x5101

t=427.5s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=428.01s dom: 0xffd7 → 0xffff

[DOM-DEVICE] t=428.01s dom: 0xffd7 → 0xffff

[CLUSTER-RENDER] t=428.01s top4: 0x4301 → 0xd100

[CLUSTER-DEVICE] t=428.01s top4: 0x4301 → 0xd100

t=428.0s CPU=22.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=428.51s top4: 0x4181 → 0xd100

[CLUSTER-DEVICE] t=428.51s top4: 0x4181 → 0xd100

t=428.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0066 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=429.01s top4: 0x5180 → 0x4301

[CLUSTER-DEVICE] t=429.01s top4: 0x5180 → 0x4301

t=429.0s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=429.5s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=430.03s top4: 0x5101 → 0x8301

[CLUSTER-DEVICE] t=430.03s top4: 0x5101 → 0x8301

t=430.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0066 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=430.5s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=431.0s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0054 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=431.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

t=432.0s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0037 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=432.53s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=432.53s mask: 0xffff → 0x3ffff

t=432.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=433.05s top4: 0x303 → 0x8181

[CLUSTER-DEVICE] t=433.05s top4: 0x303 → 0x8181

t=433.0s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0076 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

t=433.5s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0075 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=434.05s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=434.05s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=434.05s top4: 0x8181 → 0x303

[CLUSTER-DEVICE] t=434.05s top4: 0x8181 → 0x303

t=434.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0051 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=434.55s top4: 0x303 → 0x8181

[CLUSTER-DEVICE] t=434.55s top4: 0x303 → 0x8181

t=434.5s CPU=25.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0046 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=435.05s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=435.05s mask: 0xffff → 0x3ffff

[CLUSTER-RENDER] t=435.05s top4: 0x8190 → 0x381

[CLUSTER-DEVICE] t=435.05s top4: 0x8190 → 0x381

t=435.1s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=435.56s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=435.56s mask: 0xffff → 0x3ffff

t=435.6s CPU=24.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0056 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=436.06s top4: 0x80b0 → 0x8181

[CLUSTER-DEVICE] t=436.06s top4: 0x80b0 → 0x8181

t=436.1s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=436.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=437.08s top4: 0x8190 → 0x8031

[CLUSTER-DEVICE] t=437.08s top4: 0x8190 → 0x8031

t=437.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=437.58s top4: 0xb1 → 0x8190

[CLUSTER-DEVICE] t=437.58s top4: 0xb1 → 0x8190

t=437.6s CPU=21.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0301 subRms=0.0124 Xrun=0 NaN=0 SpkG=0 PLAYING

t=438.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0230 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=438.58s top4: 0x8091 → 0x81c0

[CLUSTER-DEVICE] t=438.58s top4: 0x8091 → 0x81c0

t=438.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

[CLUSTER-RENDER] t=439.09s top4: 0x8d → 0x80c4

[CLUSTER-DEVICE] t=439.09s top4: 0x8d → 0x80c4

t=439.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0181 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

## TESTING ROUND 3

## Pre agent test notes 3:

Patch locations:

Render-side: Spatializer.hpp:654–683
Device-side: Spatializer.hpp:754–775
What changed (both blocks identically):

Added maxMainMs — tracked in the existing per-channel loop with a one-line !isSubwooferChannel guard.
domThresh now references maxMainMs instead of maxMs — so a loud sub no longer inflates the threshold and causes main channels to drop out.
The domMask bit-setting loop now skips sub channels — so subs can never appear in domMask, and a sub crossing threshold cannot produce a DOM event.
No new atomics. subRmsTotal already carries sub state. maxMs is kept for the absolute-mask path (unchanged).

If DOM events remain after this patch:

Yes, they would be meaningful. domMask at that point only reflects bits among the 16 mains, referenced against the loudest main channel. A change in that mask means the dominant main-speaker cluster genuinely shifted — not a sub threshold crossing, not a bleed edge artifact. Any remaining event with the same bit-count (16 bits in both prev and next) would be real spatial energy moving between main speakers and is the right thing to correlate against pop timestamps.

- ascent test 1:

* channels dissapeared around 168
* returned 201 but high pitched and only some channels, some poping
* result:
  Parameter gain(gain)[N2al9ParameterE] : /realtime/gain
  Parameter focus(focus)[N2al9ParameterE] : /realtime/focus
  Parameter speaker_mix_db(speaker_mix_db)[N2al9ParameterE] : /realtime/speaker_mix_db
  Parameter sub_mix_db(sub_mix_db)[N2al9ParameterE] : /realtime/sub_mix_db
  Parameter auto_comp(auto_comp)[N2al13ParameterBoolE] : /realtime/auto_comp
  Parameter paused(paused)[N2al13ParameterBoolE] : /realtime/paused
  Parameter elevation_mode(elevation_mode)[N2al9ParameterE] : /realtime/elevation_mode

---

[OSC] gain → 0.5
[OSC] focus → 1.5
[OSC] speaker_mix_db → 0 dB
[OSC] sub_mix_db → 0 dB
[OSC] auto_comp → off
[OSC] elevation_mode → RescaleAtmosUp (0)
[Backend] Initializing audio device...
Sample rate: 48000 Hz
Buffer size: 512 frames
Output channels: 18
Input channels: 0
[Backend] Audio device opened successfully.
Actual output channels: 18
Actual buffer size: 512
[Backend] Cached 64 source names for audio callback.
[Streaming] Background loader thread started.
[Backend] Starting audio stream...
[Backend] Audio stream started.
[Main] DBAP spatialization active: 64 sources → 16 speakers. Press Ctrl+C to stop.

[Diag] Channels — requested: 18 actual-device: 18 render-bus: 18

t=0.0s CPU=0.0% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

t=0.5s CPU=28.9% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.01s mask: 0x4 → 0x6

[RELOC-DEVICE] t=1.01s mask: 0x4 → 0x6

t=1.0s CPU=27.6% rDom=0x0 dDom=0x0 rBus=0x6 dev=0x6 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.51s mask: 0x30e0f → 0xe0f

[RELOC-DEVICE] t=1.51s mask: 0x30e0f → 0xe0f

t=1.5s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0xe0f dev=0xe0f mainRms=0.0010 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.02s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.02s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.02s dom: 0x2f2f → 0xf2f

[DOM-DEVICE] t=2.02s dom: 0x2f2f → 0xf2f

t=2.0s CPU=30.1% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.52s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.52s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.52s dom: 0x2f2f → 0xf2f

[DOM-DEVICE] t=2.52s dom: 0x2f2f → 0xf2f

t=2.5s CPU=29.4% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0036 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.02s dom: 0xf0f → 0xf2f

[DOM-DEVICE] t=3.02s dom: 0xf0f → 0xf2f

t=3.0s CPU=27.6% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.52s dom: 0xf0f → 0xf2f

[DOM-DEVICE] t=3.52s dom: 0xf0f → 0xf2f

t=3.5s CPU=29.6% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.02s dom: 0x2faf → 0xf2f

[DOM-DEVICE] t=4.02s dom: 0x2faf → 0xf2f

t=4.0s CPU=29.5% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.53s dom: 0xe0f → 0xafaf

[DOM-DEVICE] t=4.53s dom: 0xe0f → 0xafaf

t=4.5s CPU=28.5% rDom=0xafaf dDom=0xafaf rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.03s dom: 0x2faf → 0x2f2f

[DOM-DEVICE] t=5.03s dom: 0x2faf → 0x2f2f

t=5.0s CPU=27.5% rDom=0x2f2f dDom=0x2f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.54s dom: 0x2f2f → 0x2faf

[DOM-DEVICE] t=5.54s dom: 0x2f2f → 0x2faf

t=5.5s CPU=29.3% rDom=0x2faf dDom=0x2faf rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.05s dom: 0xe0f → 0xf2f

[DOM-DEVICE] t=6.05s dom: 0xe0f → 0xf2f

t=6.0s CPU=28.6% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.55s dom: 0xf2f → 0xf0f

[DOM-DEVICE] t=6.55s dom: 0xf2f → 0xf0f

t=6.5s CPU=28.8% rDom=0xf0f dDom=0xf0f rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.06s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=7.06s dom: 0x2f7f → 0xf7f

t=7.1s CPU=26.9% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.56s dom: 0xf7f → 0x2f7f

[DOM-DEVICE] t=7.56s dom: 0xf7f → 0x2f7f

t=7.6s CPU=22.9% rDom=0x2f7f dDom=0x2f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0130 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.06s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=8.06s dom: 0x2f7f → 0xf7f

t=8.1s CPU=20.1% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.57s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=8.57s dom: 0x2f7f → 0xf7f

t=8.6s CPU=28.1% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.07s dom: 0xffff → 0x7fff

[DOM-DEVICE] t=9.07s dom: 0xffff → 0x7fff

t=9.1s CPU=29.9% rDom=0x7fff dDom=0x7fff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.58s dom: 0xafff → 0x7fff

[DOM-DEVICE] t=9.58s dom: 0xafff → 0x7fff

t=9.6s CPU=30.3% rDom=0x7fff dDom=0x7fff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.08s dom: 0x2f7f → 0xf7f

[DOM-DEVICE] t=10.08s dom: 0x2f7f → 0xf7f

t=10.1s CPU=22.3% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.58s dom: 0x1f7f → 0xffff

[DOM-DEVICE] t=10.58s dom: 0x1f7f → 0xffff

t=10.6s CPU=25.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.08s dom: 0x7fff → 0x7f7f

[DOM-DEVICE] t=11.08s dom: 0x7fff → 0x7f7f

t=11.1s CPU=29.9% rDom=0x7f7f dDom=0x7f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.58s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=11.58s dom: 0x7fff → 0xffff

t=11.6s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.09s dom: 0x7f7f → 0xffff

[DOM-DEVICE] t=12.09s dom: 0x7f7f → 0xffff

t=12.1s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.59s dom: 0x2f7f → 0xffff

[DOM-DEVICE] t=12.59s dom: 0x2f7f → 0xffff

t=12.6s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.09s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=13.09s dom: 0x7fff → 0xffff

t=13.1s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0130 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.60s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=13.60s dom: 0x7fff → 0xffff

t=13.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.10s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=14.10s dom: 0x7fff → 0xffff

t=14.1s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.60s dom: 0x7fff → 0xffff

[DOM-DEVICE] t=14.60s dom: 0x7fff → 0xffff

t=14.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.1s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.1s CPU=25.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.6s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=17.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=17.6s CPU=21.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.1s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=19.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0253 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=19.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.1s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.7s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.2s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0072 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.7s CPU=20.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0198 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=20.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.7s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0081 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.2s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=32.2s CPU=31.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=32.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0347 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=33.2s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=33.7s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.2s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0220 subRms=0.0096 Xrun=0 NaN=0 SpkG=0 PLAYING

t=35.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=35.7s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.25s dom: 0xffff → 0xefef

[DOM-DEVICE] t=36.25s dom: 0xffff → 0xefef

t=36.2s CPU=29.6% rDom=0xefef dDom=0xefef rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.75s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=36.75s dom: 0xdfff → 0xffff

t=36.7s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=37.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.75s dom: 0x8f8f → 0xffff

[DOM-DEVICE] t=37.75s dom: 0x8f8f → 0xffff

t=37.7s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.26s dom: 0xefef → 0xffff

[DOM-DEVICE] t=38.26s dom: 0xefef → 0xffff

t=38.3s CPU=31.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.76s dom: 0x57df → 0xffff

[DOM-DEVICE] t=38.76s dom: 0x57df → 0xffff

t=38.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.26s dom: 0xd7ff → 0xffff

[DOM-DEVICE] t=39.26s dom: 0xd7ff → 0xffff

t=39.3s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.78s dom: 0xafff → 0xffff

[DOM-DEVICE] t=39.78s dom: 0xafff → 0xffff

t=39.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0211 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.28s dom: 0x7eff → 0xffff

[DOM-DEVICE] t=40.28s dom: 0x7eff → 0xffff

t=40.3s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=40.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=41.3s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0115 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=41.8s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=43.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=43.8s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.8s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=46.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=46.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=47.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=47.8s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=48.3s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=48.8s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=49.3s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=49.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=50.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=50.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=51.3s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=51.8s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.9s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=53.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=53.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=54.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0183 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=54.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0072 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0131 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=57.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=57.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0071 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.9s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=59.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0360 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=59.9s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=60.4s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=60.9s CPU=29.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=61.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=61.9s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0182 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.4s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.4s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=65.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0122 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=65.9s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=66.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.93s dom: 0xffff → 0xf9fd

[DOM-DEVICE] t=66.93s dom: 0xf9f9 → 0xffff

t=66.9s CPU=29.0% rDom=0xf9fd dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.45s dom: 0xf9fd → 0xffff

[DOM-DEVICE] t=67.45s dom: 0xf9fd → 0xffff

t=67.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.95s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=67.95s dom: 0xfdff → 0xffff

t=67.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.45s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=68.45s dom: 0xfdff → 0xffff

t=68.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0183 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.95s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=68.95s dom: 0xfdff → 0xffff

t=68.9s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.45s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=69.45s dom: 0xfdff → 0xffff

t=69.5s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.96s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=69.96s dom: 0xfdff → 0xffff

t=70.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.46s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.46s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=70.46s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=70.46s dom: 0xfdff → 0xffff

t=70.5s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.97s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.97s mask: 0xffff → 0x3ffff

t=71.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0073 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.47s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=71.47s dom: 0xfdff → 0xffff

t=71.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.97s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=71.97s dom: 0xfdff → 0xffff

t=72.0s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.48s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=72.48s dom: 0xfdfd → 0xffff

t=72.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=73.0s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.48s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=73.48s dom: 0xfdff → 0xffff

t=73.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=73.98s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=73.98s mask: 0xffff → 0x3ffff

t=74.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.50s mask: 0xffff → 0x3ffff

t=74.5s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.00s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=75.00s mask: 0x3ffff → 0xffff

t=75.0s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0134 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.50s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.50s mask: 0xffff → 0x3ffff

t=75.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.5s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=77.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=77.5s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=78.0s CPU=19.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=78.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0136 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0156 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.5s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] sub_mix_db → 0.1000 dB

t=81.0s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] sub_mix_db → -2.2000 dB

t=81.5s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=82.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

t=82.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=83.1s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=83.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=84.1s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=84.6s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.1s CPU=27.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.6s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=86.1s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=86.6s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0348 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0125 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.6s CPU=20.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=88.1s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=88.6s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=89.10s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=89.10s mask: 0xffff → 0x3ffff

t=89.1s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=89.6s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=90.1s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.60s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=90.60s dom: 0xfdff → 0xffff

t=90.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.10s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=91.10s dom: 0xfbff → 0xffff

t=91.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.61s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=91.61s dom: 0xfbff → 0xffff

t=91.6s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0207 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.12s dom: 0xf9fd → 0xffff

[DOM-DEVICE] t=92.12s dom: 0xf9fd → 0xffff

t=92.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.62s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=92.62s dom: 0xfbff → 0xffff

t=92.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=93.1s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=93.62s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=93.62s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=93.62s dom: 0xfdfd → 0xf9f9

[DOM-DEVICE] t=93.62s dom: 0xfdfd → 0xf9f9

t=93.6s CPU=28.9% rDom=0xf9f9 dDom=0xf9f9 rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.13s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.13s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.13s dom: 0xf9f9 → 0xf9fd

[DOM-DEVICE] t=94.13s dom: 0xf9f9 → 0xf9fd

t=94.1s CPU=28.4% rDom=0xf9fd dDom=0xf9fd rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.63s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.63s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.63s dom: 0xf1fb → 0xf9fb

[DOM-DEVICE] t=94.63s dom: 0xf1fb → 0xf9fb

t=94.6s CPU=29.9% rDom=0xf9fb dDom=0xf9fb rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=95.14s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=95.14s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=95.14s dom: 0xfbff → 0xffff

[DOM-DEVICE] t=95.14s dom: 0xfbff → 0xffff

t=95.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

t=95.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0326 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=96.1s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0267 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.65s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=96.65s mask: 0xffff → 0x3ffff

t=96.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=97.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.66s dom: 0xdff7 → 0xffff

[DOM-DEVICE] t=97.66s dom: 0xdff7 → 0xffff

t=97.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0236 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=98.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.67s dom: 0xffdf → 0xffff

[DOM-DEVICE] t=98.67s dom: 0xffdf → 0xffff

t=98.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=99.2s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0285 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=99.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0288 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=100.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0264 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=100.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0407 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.18s dom: 0xdfdf → 0xffff

[DOM-DEVICE] t=101.18s dom: 0xdfdf → 0xffff

t=101.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0479 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=101.7s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0355 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=102.2s CPU=27.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0362 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.70s dom: 0xdfd7 → 0xffff

[DOM-DEVICE] t=102.70s dom: 0xdfd7 → 0xffff

t=102.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=103.2s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.70s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=103.70s dom: 0xfdfd → 0xffff

t=103.7s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=104.2s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0081 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.71s dom: 0xfdf9 → 0xffff

[DOM-DEVICE] t=104.71s dom: 0xfdf9 → 0xffff

t=104.7s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=105.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=105.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.22s dom: 0xf9f1 → 0xffff

[DOM-DEVICE] t=106.22s dom: 0xf9f1 → 0xffff

t=106.2s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0127 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=106.7s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=107.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=107.73s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=107.73s dom: 0xfffd → 0xffff

t=107.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.23s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.23s mask: 0xffff → 0x3ffff

t=108.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.74s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.74s mask: 0xffff → 0x3ffff

t=108.7s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.25s mask: 0xffff → 0x3ffff

t=109.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=109.75s dom: 0xfffd → 0xffff

[DOM-DEVICE] t=109.75s dom: 0xfffd → 0xffff

t=109.7s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.25s mask: 0xffff → 0x3ffff

t=110.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.75s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.75s mask: 0xffff → 0x3ffff

t=110.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0221 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.25s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.25s mask: 0xffff → 0x3ffff

t=111.3s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.75s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.75s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.75s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=111.75s dom: 0xfdfd → 0xffff

t=111.8s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

t=112.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=112.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=113.3s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=113.8s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0111 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=114.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=114.8s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=115.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=115.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0250 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=116.3s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0206 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=116.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.3s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.8s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0206 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=118.3s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0247 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=118.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0221 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=119.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=119.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=120.3s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=120.8s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=121.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=121.8s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.3s CPU=27.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.8s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

t=123.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0201 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=123.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0283 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=124.3s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=124.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=125.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=125.9s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=126.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=126.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=127.4s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=127.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=128.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=128.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

t=129.4s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=129.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=130.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=130.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=131.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0169 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=131.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

t=132.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0197 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=132.9s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=133.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0192 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=133.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=134.4s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0219 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=134.9s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=135.4s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0416 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=135.9s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0356 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=136.4s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0246 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=136.9s CPU=28.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0246 subRms=0.0076 Xrun=0 NaN=0 SpkG=0 PLAYING

t=137.4s CPU=20.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0224 subRms=0.0079 Xrun=0 NaN=0 SpkG=0 PLAYING

t=137.9s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0206 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=138.4s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0217 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=138.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0238 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

t=139.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0257 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=140.0s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0254 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=140.5s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=141.0s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0326 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=141.5s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=142.0s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0238 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=142.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0215 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=143.0s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=143.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=144.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=144.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0221 subRms=0.0103 Xrun=0 NaN=0 SpkG=0 PLAYING

t=145.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=145.5s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0200 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=146.0s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0204 subRms=0.0044 Xrun=0 NaN=0 SpkG=0 PLAYING

t=146.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0209 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=147.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=147.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0281 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=148.0s CPU=21.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0064 Xrun=0 NaN=0 SpkG=0 PLAYING

t=148.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0392 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=149.0s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0266 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=149.5s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0211 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=150.0s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0083 Xrun=0 NaN=0 SpkG=0 PLAYING

t=150.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0211 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=151.0s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=151.5s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0228 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=152.0s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=152.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=153.0s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=153.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=154.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0342 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=154.5s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0332 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=155.1s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0344 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=155.6s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0359 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=156.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=156.6s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=157.1s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0181 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=157.6s CPU=22.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0089 Xrun=0 NaN=0 SpkG=0 PLAYING

t=158.1s CPU=22.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=158.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=159.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=159.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=160.1s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=160.6s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=161.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=161.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=162.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=162.6s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0309 subRms=0.0072 Xrun=0 NaN=0 SpkG=0 PLAYING

t=163.1s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=163.6s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0218 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=164.1s CPU=26.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=164.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0229 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=165.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0307 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=165.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0278 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=166.1s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0194 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=166.62s dom: 0x8fff → 0xffff

[DOM-DEVICE] t=166.62s dom: 0x8fff → 0xffff

t=166.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0313 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=167.14s dom: 0x8fff → 0xffff

[DOM-DEVICE] t=167.14s dom: 0x8fff → 0xffff

t=167.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0379 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=167.6s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0427 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=168.1s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0275 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=168.64s dom: 0xbfff → 0xffff

[DOM-DEVICE] t=168.64s dom: 0xbfff → 0xffff

t=168.6s CPU=28.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0258 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=169.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0268 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=169.7s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=170.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0222 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=170.7s CPU=26.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=171.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=171.7s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=172.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=172.7s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

t=173.2s CPU=28.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=173.7s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0215 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=174.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=174.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0063 Xrun=0 NaN=0 SpkG=0 PLAYING

t=175.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0198 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

t=175.7s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0188 subRms=0.0049 Xrun=0 NaN=0 SpkG=0 PLAYING

t=176.2s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=176.7s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0217 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=177.2s CPU=26.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

t=177.7s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0223 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=178.2s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0212 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=178.7s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0229 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=179.2s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0228 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=179.7s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=180.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=180.7s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0212 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=181.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0347 subRms=0.0058 Xrun=0 NaN=0 SpkG=0 PLAYING

t=181.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0251 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=182.2s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=182.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0258 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=183.2s CPU=29.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=183.7s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0298 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=184.2s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0232 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=184.8s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=185.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=185.8s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=186.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0196 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

t=186.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=187.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=187.8s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0246 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=188.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0559 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=188.8s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0399 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=189.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0280 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=189.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0469 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=190.3s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0299 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=190.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0437 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=191.30s dom: 0xffff → 0xdfff

[DOM-DEVICE] t=191.30s dom: 0xffff → 0xdfff

t=191.3s CPU=28.0% rDom=0xdfff dDom=0xdfff rBus=0x3ffff dev=0x3ffff mainRms=0.0527 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=191.81s dom: 0xefff → 0xffff

[DOM-DEVICE] t=191.81s dom: 0xefff → 0xffff

t=191.8s CPU=22.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0444 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=192.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0295 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=192.8s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0291 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=193.3s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=193.8s CPU=22.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=194.3s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=194.83s dom: 0xf8f9 → 0xffff

[DOM-DEVICE] t=194.83s dom: 0xf8f9 → 0xffff

t=194.8s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=195.33s dom: 0xfdff → 0xffff

[DOM-DEVICE] t=195.33s dom: 0xfdff → 0xffff

t=195.3s CPU=27.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=195.84s dom: 0xffff → 0xf9f9

[DOM-DEVICE] t=195.84s dom: 0xffff → 0xf9f9

t=195.8s CPU=29.9% rDom=0xf9f9 dDom=0xf9f9 rBus=0x3ffff dev=0x3ffff mainRms=0.0225 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=196.34s dom: 0xfdfd → 0xffff

[DOM-DEVICE] t=196.34s dom: 0xfdfd → 0xffff

t=196.3s CPU=22.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=196.8s CPU=22.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0241 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=197.3s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0289 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=197.8s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0279 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=198.4s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

t=198.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0357 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=199.4s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0315 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=199.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0334 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=200.4s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0369 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=200.9s CPU=28.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0642 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=201.38s dom: 0xcfdf → 0xffff

[DOM-DEVICE] t=201.38s dom: 0xcfdf → 0xffff

t=201.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0498 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=201.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0264 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=202.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0328 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=202.9s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0282 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=203.4s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0353 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

t=203.9s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0416 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

t=204.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0349 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=204.9s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0514 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

t=205.4s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0347 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

t=205.9s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0255 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=206.4s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0260 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=206.9s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0476 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=207.42s dom: 0xefff → 0xffff

[DOM-DEVICE] t=207.42s dom: 0xefff → 0xffff

t=207.4s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0363 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=207.93s dom: 0xefef → 0xffff

[DOM-DEVICE] t=207.93s dom: 0xefef → 0xffff

t=207.9s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0363 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=208.43s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=208.43s dom: 0xdfff → 0xffff

t=208.4s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0336 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=208.93s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=208.93s dom: 0xdfff → 0xffff

t=208.9s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0250 subRms=0.0066 Xrun=0 NaN=0 SpkG=0 PLAYING

t=209.4s CPU=25.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0359 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=209.94s dom: 0xefff → 0xffff

[DOM-DEVICE] t=209.94s dom: 0xefff → 0xffff

t=209.9s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0214 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

t=210.4s CPU=26.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0326 subRms=0.0078 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=210.94s dom: 0xefef → 0xffff

[DOM-DEVICE] t=210.94s dom: 0xefef → 0xffff

t=210.9s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0541 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=211.4s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0295 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=211.9s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0310 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=212.5s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0315 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=213.0s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=213.5s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0390 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=214.0s CPU=29.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0366 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=214.47s dom: 0xefff → 0xffff

[DOM-DEVICE] t=214.47s dom: 0xefff → 0xffff

t=214.5s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0423 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=215.0s CPU=19.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0338 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=215.5s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0261 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=216.0s CPU=22.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0242 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

t=216.5s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0349 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=217.0s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0290 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=217.50s dom: 0xefff → 0xffff

[DOM-DEVICE] t=217.50s dom: 0xefff → 0xffff

t=217.5s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0390 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=218.0s CPU=20.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0343 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=218.51s dom: 0xefef → 0xffff

[DOM-DEVICE] t=218.51s dom: 0xefef → 0xffff

t=218.5s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0337 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=219.0s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0400 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=219.5s CPU=28.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0372 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=220.0s CPU=28.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0532 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=220.52s dom: 0xcfff → 0xffff

[DOM-DEVICE] t=220.52s dom: 0xcfff → 0xffff

t=220.5s CPU=22.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0507 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=221.03s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=221.03s dom: 0xdfff → 0xffff

t=221.0s CPU=26.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0468 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=221.5s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0361 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=222.04s dom: 0xefff → 0xffff

[DOM-DEVICE] t=222.04s dom: 0xefff → 0xffff

t=222.0s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0271 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=222.5s CPU=22.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=223.05s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=223.05s mask: 0xffff → 0x3ffff

t=223.1s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0223 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=223.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0299 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=224.1s CPU=27.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0266 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=224.6s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0355 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=225.1s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0283 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=225.57s dom: 0xefff → 0xffff

[DOM-DEVICE] t=225.57s dom: 0xefff → 0xffff

t=225.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0320 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

t=226.1s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0226 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

t=226.6s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0178 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=227.07s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=227.07s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=227.07s dom: 0xdfff → 0xffff

[DOM-DEVICE] t=227.07s dom: 0xdfff → 0xffff

t=227.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0222 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

t=227.6s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0195 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=228.09s dom: 0xefff → 0xffff

[DOM-DEVICE] t=228.09s dom: 0xefff → 0xffff

t=228.1s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=228.59s dom: 0xefef → 0xffff

[DOM-DEVICE] t=228.59s dom: 0xefef → 0xffff

t=228.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0266 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=229.1s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0270 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=229.59s dom: 0xcfcf → 0xffff

[DOM-DEVICE] t=229.59s dom: 0xcfcf → 0xffff

t=229.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0376 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=230.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0374 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=230.6s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0372 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=231.1s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0283 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=231.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0232 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

t=232.1s CPU=22.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0234 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=232.62s dom: 0xefff → 0xffff

[DOM-DEVICE] t=232.62s dom: 0xefff → 0xffff

t=232.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0278 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=233.1s CPU=22.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=233.6s CPU=25.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=234.1s CPU=25.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0296 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=234.65s dom: 0xefff → 0xffff

[DOM-DEVICE] t=234.65s dom: 0xefff → 0xffff

t=234.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0269 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=235.1s CPU=29.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0204 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=235.65s dom: 0xefef → 0xffff

[DOM-DEVICE] t=235.65s dom: 0xefef → 0xffff

t=235.6s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0240 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=236.16s dom: 0xefff → 0xffff

[DOM-DEVICE] t=236.16s dom: 0xefff → 0xffff

t=236.2s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0350 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=236.66s dom: 0xefef → 0xffff

[DOM-DEVICE] t=236.66s dom: 0xefef → 0xffff

t=236.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0339 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=237.16s dom: 0xefff → 0xffff

[DOM-DEVICE] t=237.16s dom: 0xefff → 0xffff

t=237.2s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0321 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=237.7s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0329 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=238.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0263 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=238.7s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=239.2s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0251 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=239.7s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

t=240.2s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0180 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

t=240.7s CPU=29.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=241.19s dom: 0xefef → 0xffff

[DOM-DEVICE] t=241.19s dom: 0xefef → 0xffff

t=241.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

t=241.7s CPU=29.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0205 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=242.2s CPU=30.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0167 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=242.70s dom: 0xefff → 0xffff

[DOM-DEVICE] t=242.70s dom: 0xefff → 0xffff

t=242.7s CPU=28.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0333 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

-

## TESTING ROUND 2

## Pre agent test notes 2:

Summary of all changes:

RealtimeTypes.hpp — added 8 new atomics to EngineState: renderDomMask, deviceDomMask, and their three-field relocation latches each.

Spatializer.hpp — both Phase 14 blocks rewritten:

Single pass computes per-channel mean-square into a stack-local chMs[64], tracks maxMs
Absolute mask built as before (kRmsThresh = 1e-8)
Dominant mask built in a second pass: channels with ms ≥ 0.01 × maxMs (−20 dBFS relative)
Both relocation latches add the prevMask != 0 first-block suppression guard
Fix 2 test gate left in place (isFastMover = false) — unchanged
main.cpp — monitoring loop updated:

Two new event printers: [DOM-RENDER] and [DOM-DEVICE], printed after the existing [RELOC-*] lines
Status line shows rDom and dDom before rBus/dev so the dominant cluster is the first thing visible
What to watch in the next test run:

If [RELOC-*] fires but [DOM-*] stays silent → it's far-field DBAP bleed crossing the absolute threshold, not an audible relocation
If [DOM-*] fires → the dominant speaker cluster genuinely changed, likely audible
The rDom value in the status line gives a continuous read of which cluster is carrying the energy; watch it for drift or oscillation without a [DOM-*] event

- Ascent Test 1

* not super audible channel reloc, 1 pop
* resulting output from some of it:
  t=0.0s CPU=0.0% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

t=0.5s CPU=29.5% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.01s mask: 0x4 → 0x6

[RELOC-DEVICE] t=1.01s mask: 0x4 → 0x6

t=1.0s CPU=29.9% rDom=0x0 dDom=0x0 rBus=0x6 dev=0x6 mainRms=0.0003 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.51s mask: 0x30e0f → 0xe0f

[RELOC-DEVICE] t=1.51s mask: 0x30e0f → 0xe0f

t=1.5s CPU=29.7% rDom=0x0 dDom=0x0 rBus=0xe0f dev=0xe0f mainRms=0.0010 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.02s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.02s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=2.02s dom: 0xf2f → 0x30f2f

t=2.0s CPU=30.1% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.52s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.52s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.52s dom: 0x32f2f → 0x30f2f

[DOM-DEVICE] t=2.52s dom: 0x32f2f → 0x30f2f

t=2.5s CPU=30.0% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0036 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=3.02s dom: 0xf2f → 0x30f2f

t=3.0s CPU=30.1% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.52s dom: 0x30f0f → 0x30f2f

[DOM-DEVICE] t=3.52s dom: 0x30f0f → 0x30f2f

t=3.5s CPU=30.1% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=4.02s dom: 0xf2f → 0x30f2f

t=4.0s CPU=29.7% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.53s dom: 0x30e0f → 0x3afaf

[DOM-DEVICE] t=4.53s dom: 0x30e0f → 0x3afaf

t=4.5s CPU=30.0% rDom=0x3afaf dDom=0x3afaf rBus=0x3ffff dev=0x3ffff mainRms=0.0120 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.03s dom: 0x32faf → 0x32f2f

[DOM-DEVICE] t=5.03s dom: 0x32faf → 0x32f2f

t=5.0s CPU=30.1% rDom=0x32f2f dDom=0x32f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.54s dom: 0x32f2f → 0x32faf

[DOM-DEVICE] t=5.54s dom: 0x32f2f → 0x32faf

t=5.5s CPU=30.1% rDom=0x32faf dDom=0x32faf rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.04s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=6.04s dom: 0x30f2f → 0xf2f

t=6.0s CPU=30.0% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.54s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=6.54s dom: 0x30f2f → 0xf2f

t=6.5s CPU=30.1% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.04s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.04s dom: 0x32f7f → 0x30f7f

t=7.0s CPU=30.1% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0126 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.55s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.55s dom: 0x32f7f → 0x30f7f

t=7.6s CPU=26.0% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.05s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=8.05s dom: 0x32f7f → 0x30f7f

t=8.1s CPU=20.1% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.55s dom: 0x30f7f → 0x32f7f

[DOM-DEVICE] t=8.55s dom: 0x30f7f → 0x32f7f

t=8.6s CPU=28.0% rDom=0x32f7f dDom=0x32f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.06s dom: 0x35f7f → 0x3ffff

[DOM-DEVICE] t=9.06s dom: 0x35f7f → 0x3ffff

t=9.1s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.56s dom: 0x3ffff → 0x30f7f

[DOM-DEVICE] t=9.56s dom: 0x3ffff → 0x30f7f

t=9.6s CPU=27.3% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.06s dom: 0xf5f → 0xf7f

[DOM-DEVICE] t=10.06s dom: 0xf5f → 0xf7f

t=10.1s CPU=28.4% rDom=0xf7f dDom=0xf7f rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.57s dom: 0x31f7f → 0x3ffff

[DOM-DEVICE] t=10.57s dom: 0x31f7f → 0x3ffff

t=10.6s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.07s dom: 0x30f7f → 0x37fff

[DOM-DEVICE] t=11.07s dom: 0x30f7f → 0x37fff

t=11.1s CPU=28.2% rDom=0x37fff dDom=0x37fff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.57s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=11.57s dom: 0x37fff → 0x3ffff

t=11.6s CPU=28.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.07s dom: 0x37f7f → 0x3ffff

[DOM-DEVICE] t=12.07s dom: 0x37f7f → 0x3ffff

t=12.1s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.58s dom: 0x3ffff → 0x32f7f

[DOM-DEVICE] t=12.58s dom: 0x3ffff → 0x32f7f

t=12.6s CPU=28.2% rDom=0x32f7f dDom=0x32f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.08s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=13.08s dom: 0x37fff → 0x3ffff

t=13.1s CPU=29.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0124 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.59s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=13.59s dom: 0xffff → 0x3ffff

t=13.6s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.09s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=14.09s dom: 0x3ffff → 0xffff

t=14.1s CPU=30.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.59s dom: 0x7fff → 0x3ffff

[DOM-DEVICE] t=14.59s dom: 0x7fff → 0x3ffff

t=14.6s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.1s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=15.61s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=15.61s dom: 0xffff → 0x3ffff

t=15.6s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.1s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0173 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=16.61s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=16.61s dom: 0xffff → 0x3ffff

t=16.6s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.11s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.11s dom: 0xffff → 0x3ffff

t=17.1s CPU=30.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.62s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.62s dom: 0xffff → 0x3ffff

t=17.6s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=18.12s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=18.12s dom: 0xffff → 0x3ffff

t=18.1s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0181 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.6s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0233 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.13s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=19.13s dom: 0x3ffff → 0xffff

t=19.1s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0257 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.64s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=19.64s dom: 0xffff → 0x3ffff

t=19.6s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.1s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0171 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=20.64s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=20.64s dom: 0xffff → 0x3ffff

t=20.6s CPU=22.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.1s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0074 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=21.65s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=21.65s dom: 0xffff → 0x3ffff

t=21.7s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=22.15s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=22.15s dom: 0xffff → 0x3ffff

t=22.2s CPU=30.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0047 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=23.17s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=23.17s dom: 0xffff → 0x3ffff

t=23.2s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.7s CPU=28.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0163 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.7s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0068 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0177 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.7s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0164 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=28.70s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=28.70s dom: 0xffff → 0x3ffff

t=28.7s CPU=26.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0089 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=29.71s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=29.71s dom: 0xffff → 0x3ffff

t=29.7s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.1400

t=30.2s CPU=28.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0040 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=28.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0054 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=31.21s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=31.21s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=31.21s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=31.21s dom: 0xffff → 0x3ffff

t=31.2s CPU=28.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.1600

t=31.7s CPU=27.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.1900

[OSC] gain → 0.2100

[OSC] gain → 0.2400

[OSC] gain → 0.2500

[OSC] gain → 0.2600

[DOM-RENDER] t=32.21s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=32.21s dom: 0xffff → 0x3ffff

t=32.2s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.2800

[OSC] gain → 0.2900

[OSC] gain → 0.3000

[DOM-RENDER] t=32.73s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=32.73s dom: 0x3ffff → 0xffff

t=32.7s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0208 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=33.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.23s dom: 0xffff → 0x3ffff

t=33.2s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[OSC] gain → 0.2900

[OSC] gain → 0.2800

[DOM-RENDER] t=33.73s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.73s dom: 0xffff → 0x3ffff

t=33.7s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=34.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=34.23s dom: 0xffff → 0x3ffff

t=34.2s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0089 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.7s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.24s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.24s dom: 0xffff → 0x3ffff

t=35.2s CPU=29.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.74s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.74s dom: 0xffff → 0x3ffff

t=35.7s CPU=27.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.25s dom: 0x3ffff → 0x3efef

[DOM-DEVICE] t=36.25s dom: 0x3ffff → 0x3efef

t=36.2s CPU=27.8% rDom=0x3efef dDom=0x3efef rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.75s dom: 0x3dfff → 0x3ffff

[DOM-DEVICE] t=36.75s dom: 0x3dfff → 0x3ffff

t=36.7s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.26s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=37.26s dom: 0xffff → 0x3ffff

t=37.3s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.76s dom: 0x3ffff → 0x3afaf

[DOM-DEVICE] t=37.76s dom: 0x3ffff → 0x3afaf

t=37.8s CPU=20.1% rDom=0x3afaf dDom=0x3afaf rBus=0x3ffff dev=0x3ffff mainRms=0.0064 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.26s dom: 0x3efef → 0x3ffff

[DOM-DEVICE] t=38.26s dom: 0x3efef → 0x3ffff

t=38.3s CPU=24.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.77s dom: 0x357df → 0x3ffff

[DOM-DEVICE] t=38.77s dom: 0x357df → 0x3ffff

t=38.8s CPU=29.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.27s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=39.27s dom: 0xffff → 0x3ffff

t=39.3s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.78s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=39.78s dom: 0x3ffff → 0xffff

t=39.8s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.28s dom: 0xffff → 0x3ffff

t=40.3s CPU=28.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.79s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.79s dom: 0xffff → 0x3ffff

t=40.8s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.29s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.29s dom: 0xffff → 0x3ffff

t=41.3s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.79s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.79s dom: 0xffff → 0x3ffff

t=41.8s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=42.29s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=42.29s dom: 0xffff → 0x3ffff

t=42.3s CPU=27.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=43.30s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=43.30s dom: 0x3ffff → 0xffff

t=43.3s CPU=27.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0098 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=43.81s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=43.81s dom: 0xffff → 0x3ffff

t=43.8s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=44.81s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=44.81s dom: 0xffff → 0x3ffff

t=44.8s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=45.82s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=45.82s dom: 0xffff → 0x3ffff

t=45.8s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.33s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.33s dom: 0xffff → 0x3ffff

t=46.3s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.83s dom: 0xffff → 0x3ffff

t=46.8s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.33s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.33s dom: 0xffff → 0x3ffff

t=47.3s CPU=28.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0082 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.83s dom: 0xffff → 0x3ffff

t=47.8s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.33s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=48.33s dom: 0xffff → 0x3ffff

t=48.3s CPU=29.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.84s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=48.84s dom: 0xffff → 0x3ffff

t=48.8s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.34s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.34s dom: 0xffff → 0x3ffff

t=49.3s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.85s dom: 0xffff → 0x3ffff

t=49.8s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.35s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=50.35s dom: 0x3ffff → 0xffff

t=50.3s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=50.85s dom: 0xffff → 0x3ffff

t=50.8s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=51.35s dom: 0xffff → 0x3ffff

t=51.3s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0088 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=51.85s dom: 0xffff → 0x3ffff

t=51.9s CPU=27.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.4s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.9s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.37s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=53.37s dom: 0x3ffff → 0xffff

t=53.4s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=53.87s dom: 0xffff → 0x3ffff

t=53.9s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.37s dom: 0xffff → 0x3ffff

t=54.4s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.87s dom: 0xffff → 0x3ffff

t=54.9s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=55.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=55.37s dom: 0xffff → 0x3ffff

t=55.4s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=56.89s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=56.89s dom: 0xffff → 0x3ffff

t=56.9s CPU=27.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.39s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.39s dom: 0xffff → 0x3ffff

t=57.4s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.89s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.89s dom: 0xffff → 0x3ffff

t=57.9s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.40s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.40s dom: 0x3ffff → 0xffff

t=59.4s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.90s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.90s dom: 0x3ffff → 0xffff

t=59.9s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.41s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=60.41s dom: 0x3ffff → 0xffff

t=60.4s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.91s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=60.91s dom: 0xffff → 0x3ffff

t=60.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=61.41s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=61.41s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=61.41s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.41s dom: 0xffff → 0x3ffff

t=61.4s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0091 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=61.92s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.92s dom: 0xffff → 0x3ffff

t=61.9s CPU=27.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=62.42s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=62.42s dom: 0xffff → 0x3ffff

t=62.4s CPU=23.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.9s CPU=25.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.43s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=63.43s dom: 0xffff → 0x3ffff

t=63.4s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.94s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=63.94s dom: 0x3ffff → 0xffff

t=63.9s CPU=30.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=64.44s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=64.44s dom: 0xffff → 0x3ffff

t=64.4s CPU=27.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

t=64.9s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=65.45s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=65.45s dom: 0xffff → 0x3ffff

t=65.5s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=66.0s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.45s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=66.45s dom: 0xffff → 0x3ffff

t=66.5s CPU=28.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0075 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.95s dom: 0x3f9fd → 0x3ffff

[DOM-DEVICE] t=66.95s dom: 0x3f9fd → 0x3ffff

t=67.0s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=67.5s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0048 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.97s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=67.97s dom: 0x3ffff → 0xffff

t=68.0s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0044 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=68.47s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=68.47s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=68.47s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=68.47s dom: 0x3ffff → 0xffff

t=68.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.97s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=68.97s dom: 0x3fdff → 0x3ffff

t=69.0s CPU=22.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.48s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=69.48s dom: 0xffff → 0x3ffff

t=69.5s CPU=30.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0045 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.98s dom: 0xfdff → 0x3ffff

[DOM-DEVICE] t=69.98s dom: 0xfdff → 0x3ffff

t=70.0s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0071 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.49s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=70.49s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=70.49s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=70.49s dom: 0x3ffff → 0xffff

t=70.5s CPU=28.7% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0088 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.99s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.99s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=70.99s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=70.99s dom: 0xffff → 0x3ffff

t=71.0s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.50s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=71.50s dom: 0x3fdff → 0x3ffff

t=71.5s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.00s dom: 0x3ffff → 0x3fdff

[DOM-DEVICE] t=72.00s dom: 0x3ffff → 0x3fdff

t=72.0s CPU=30.1% rDom=0x3fdff dDom=0x3fdff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.50s dom: 0xfdfd → 0x3ffff

[DOM-DEVICE] t=72.50s dom: 0xfdfd → 0x3ffff

t=72.5s CPU=29.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0056 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=73.0s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0054 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.51s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=73.51s dom: 0x3ffff → 0xffff

t=73.5s CPU=30.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0130 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.02s mask: 0xffff → 0x3ffff

t=74.0s CPU=27.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.52s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=74.52s mask: 0x3ffff → 0xffff

t=74.5s CPU=29.5% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0071 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.02s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=75.02s mask: 0x3ffff → 0xffff

t=75.0s CPU=27.9% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0058 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.53s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.53s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=75.53s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=75.53s dom: 0xffff → 0x3ffff

t=75.5s CPU=28.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0057 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=76.03s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=76.03s dom: 0xffff → 0x3ffff

t=76.0s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.5s CPU=28.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.03s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=77.03s dom: 0xffff → 0x3ffff

t=77.0s CPU=30.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.54s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=77.54s dom: 0xffff → 0x3ffff

t=77.5s CPU=31.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0056 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=78.04s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=78.04s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=78.04s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=78.04s dom: 0x3ffff → 0xffff

t=78.0s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=78.54s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=78.54s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=78.54s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=78.54s dom: 0xffff → 0x3ffff

t=78.5s CPU=29.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.0s CPU=25.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.6s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=80.05s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=80.05s dom: 0xffff → 0x3ffff

t=80.1s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.6s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0097 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.06s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=81.06s dom: 0xffff → 0x3ffff

t=81.1s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.56s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=81.56s dom: 0x3ffff → 0xffff

t=81.6s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.07s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=82.07s dom: 0x3ffff → 0xffff

t=82.1s CPU=20.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.57s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=82.57s dom: 0xffff → 0x3ffff

t=82.6s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=83.07s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=83.07s dom: 0xffff → 0x3ffff

t=83.1s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=83.58s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=83.58s dom: 0xffff → 0x3ffff

t=83.6s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=84.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=84.09s dom: 0xffff → 0x3ffff

t=84.1s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=84.59s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=84.59s dom: 0xffff → 0x3ffff

t=84.6s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=85.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=85.09s dom: 0xffff → 0x3ffff

t=85.1s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0070 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

t=85.6s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0053 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=86.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=86.09s dom: 0xffff → 0x3ffff

t=86.1s CPU=28.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=86.60s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=86.60s dom: 0xffff → 0x3ffff

t=86.6s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.1s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=87.6s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=88.12s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=88.12s dom: 0x3ffff → 0xffff

t=88.1s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0072 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=88.62s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=88.62s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=88.62s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=88.62s dom: 0x3ffff → 0xffff

t=88.6s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0076 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=89.12s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=89.12s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=89.12s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=89.12s dom: 0xffff → 0x3ffff

t=89.1s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0084 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=89.6s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.13s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=90.13s dom: 0xffff → 0x3ffff

t=90.1s CPU=28.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0069 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=90.63s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=90.63s dom: 0x3fdff → 0x3ffff

t=90.6s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.14s dom: 0x3fbfb → 0x3f9fb

[DOM-DEVICE] t=91.14s dom: 0x3fbfb → 0x3f9fb

t=91.1s CPU=30.1% rDom=0x3f9fb dDom=0x3f9fb rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=91.64s dom: 0x3fbff → 0x3ffff

[DOM-DEVICE] t=91.64s dom: 0x3fbff → 0x3ffff

t=91.6s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0077 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.15s dom: 0x3f9fd → 0x3ffff

[DOM-DEVICE] t=92.15s dom: 0x3f9fd → 0x3ffff

t=92.1s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0052 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=92.65s dom: 0x3fbff → 0x3ffff

[DOM-DEVICE] t=92.65s dom: 0x3fbff → 0x3ffff

t=92.7s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=93.15s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=93.15s dom: 0xffff → 0x3ffff

t=93.2s CPU=28.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=93.65s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=93.65s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=93.65s dom: 0xfdfd → 0xf9f9

[DOM-DEVICE] t=93.65s dom: 0xfdfd → 0xf9f9

t=93.7s CPU=28.6% rDom=0xf9f9 dDom=0xf9f9 rBus=0x3ffff dev=0x3ffff mainRms=0.0047 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.17s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.17s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.17s dom: 0xf9f9 → 0xf9fd

[DOM-DEVICE] t=94.17s dom: 0xf9f9 → 0xf9fd

t=94.2s CPU=29.3% rDom=0xf9fd dDom=0xf9fd rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=94.67s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=94.67s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=94.67s dom: 0xd0f3 → 0xd0f1

[DOM-DEVICE] t=94.67s dom: 0xd0f3 → 0xd0f1

t=94.7s CPU=27.6% rDom=0xd0f1 dDom=0xd0f1 rBus=0x3ffff dev=0x3ffff mainRms=0.0138 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=95.18s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=95.18s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=95.18s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=95.18s dom: 0x3ffff → 0xffff

t=95.2s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=95.68s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=95.68s dom: 0x3ffff → 0xffff

t=95.7s CPU=29.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0188 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.18s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=96.18s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=96.18s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=96.18s dom: 0x3ffff → 0xffff

t=96.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0143 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=96.69s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=96.69s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=96.69s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=96.69s dom: 0x3ffff → 0xffff

t=96.7s CPU=27.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.19s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=97.19s dom: 0x3ffff → 0xffff

t=97.2s CPU=30.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0095 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=97.70s dom: 0x3dff7 → 0x3ffff

[DOM-DEVICE] t=97.70s dom: 0x3dff7 → 0x3ffff

t=97.7s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0150 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.20s dom: 0x3ffdf → 0x3ffff

[DOM-DEVICE] t=98.20s dom: 0x3ffdf → 0x3ffff

t=98.2s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0093 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=98.70s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=98.70s dom: 0xffff → 0x3ffff

t=98.7s CPU=27.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=99.20s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=99.20s dom: 0x3ffff → 0xffff

t=99.2s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=99.70s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=99.70s dom: 0xffff → 0x3ffff

t=99.7s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=100.21s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=100.21s dom: 0xffff → 0x3ffff

t=100.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=100.71s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=100.71s dom: 0xffff → 0x3ffff

t=100.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0259 subRms=0.0025 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=101.23s dom: 0xffff → 0x3ffff

t=101.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0264 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=101.72s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=101.72s dom: 0xffff → 0x3ffff

t=101.7s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=102.23s dom: 0xffff → 0x3ffff

t=102.2s CPU=28.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=102.73s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=102.73s dom: 0xffff → 0x3ffff

t=102.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0073 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.24s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=103.24s dom: 0xffff → 0x3ffff

t=103.2s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0062 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=103.74s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=103.74s dom: 0x3ffff → 0xffff

t=103.7s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=104.25s dom: 0xffff → 0x3ffff

t=104.2s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=104.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=104.75s dom: 0xffff → 0x3ffff

t=104.7s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=105.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=105.25s dom: 0xffff → 0x3ffff

t=105.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0049 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=105.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=105.75s dom: 0xffff → 0x3ffff

t=105.7s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0068 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=106.25s dom: 0xffff → 0x3ffff

t=106.3s CPU=27.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0060 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=106.76s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=106.76s dom: 0xffff → 0x3ffff

t=106.8s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=107.26s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=107.26s dom: 0x3ffff → 0xffff

t=107.3s CPU=28.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0063 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=107.77s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=107.77s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=107.77s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=107.77s dom: 0x3ffff → 0xffff

t=107.8s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.27s mask: 0x3ffff → 0xffff

[RELOC-DEVICE] t=108.27s mask: 0x3ffff → 0xffff

[DOM-RENDER] t=108.27s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=108.27s dom: 0x3ffff → 0xffff

t=108.3s CPU=29.8% rDom=0xffff dDom=0xffff rBus=0xffff dev=0xffff mainRms=0.0075 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=108.77s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=108.77s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=108.77s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=108.77s dom: 0xffff → 0x3ffff

t=108.8s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0064 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.28s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.28s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=109.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=109.28s dom: 0xffff → 0x3ffff

t=109.3s CPU=22.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0059 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=109.78s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=109.78s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=109.78s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=109.78s dom: 0x3ffff → 0xffff

t=109.8s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=110.28s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=110.28s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=110.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=110.28s dom: 0xffff → 0x3ffff

t=110.3s CPU=26.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0074 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=110.78s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=110.78s dom: 0xffff → 0x3ffff

t=110.8s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0067 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.30s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.30s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.30s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=111.30s dom: 0x3ffff → 0xffff

t=111.3s CPU=27.1% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0061 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=111.80s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=111.80s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=111.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=111.80s dom: 0xffff → 0x3ffff

t=111.8s CPU=27.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0065 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=112.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=112.30s dom: 0xffff → 0x3ffff

t=112.3s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0081 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=112.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=112.80s dom: 0xffff → 0x3ffff

t=112.8s CPU=28.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=113.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=113.30s dom: 0xffff → 0x3ffff

t=113.3s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0078 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=113.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=113.80s dom: 0xffff → 0x3ffff

t=113.8s CPU=30.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=114.31s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=114.31s dom: 0xffff → 0x3ffff

t=114.3s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0070 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=114.82s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=114.82s dom: 0xffff → 0x3ffff

t=114.8s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0086 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=115.32s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=115.32s dom: 0xffff → 0x3ffff

t=115.3s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=115.82s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=115.82s dom: 0x3ffff → 0xffff

t=115.8s CPU=30.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=116.32s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=116.32s dom: 0xffff → 0x3ffff

t=116.3s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0121 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=116.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=116.83s dom: 0xffff → 0x3ffff

t=116.8s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0107 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.3s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

t=117.8s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=118.3s CPU=29.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=118.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=118.85s dom: 0xffff → 0x3ffff

t=118.8s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0133 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=119.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=119.35s dom: 0xffff → 0x3ffff

t=119.3s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=119.9s CPU=27.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0085 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=120.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=120.35s dom: 0xffff → 0x3ffff

t=120.4s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=120.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=120.85s dom: 0xffff → 0x3ffff

t=120.9s CPU=27.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=121.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=121.35s dom: 0xffff → 0x3ffff

t=121.4s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0123 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=121.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=121.87s dom: 0xffff → 0x3ffff

t=121.9s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0035 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.4s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

t=122.9s CPU=29.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0092 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=123.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=123.37s dom: 0xffff → 0x3ffff

t=123.4s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

- ascent test 2

* some pops
* reloc not super audible
* some results:
  t=0.0s CPU=0.0% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

t=0.5s CPU=29.8% rDom=0x0 dDom=0x0 rBus=0x0 dev=0x0 mainRms=0.0000 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.00s mask: 0x4 → 0x6

[RELOC-DEVICE] t=1.00s mask: 0x4 → 0x6

t=1.0s CPU=30.0% rDom=0x0 dDom=0x0 rBus=0x6 dev=0x6 mainRms=0.0004 subRms=0.0000 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=1.50s mask: 0x3060f → 0x30e0f

[RELOC-DEVICE] t=1.50s mask: 0x3060f → 0x30e0f

t=1.5s CPU=30.3% rDom=0x0 dDom=0x0 rBus=0x30e0f dev=0x30e0f mainRms=0.0010 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.01s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.01s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.01s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=2.01s dom: 0x30f2f → 0xf2f

t=2.0s CPU=27.9% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0020 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=2.51s mask: 0x3afbf → 0x3ffff

[RELOC-DEVICE] t=2.51s mask: 0x3afbf → 0x3ffff

[DOM-RENDER] t=2.51s dom: 0x32f2f → 0x30f2f

[DOM-DEVICE] t=2.51s dom: 0x32f2f → 0x30f2f

t=2.5s CPU=28.3% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0035 subRms=0.0007 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=3.02s dom: 0xf2f → 0x30f2f

t=3.0s CPU=27.3% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0055 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=3.52s dom: 0x30f0f → 0x30f2f

[DOM-DEVICE] t=3.52s dom: 0x30f0f → 0x30f2f

t=3.5s CPU=29.6% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.02s dom: 0xf2f → 0x30f2f

[DOM-DEVICE] t=4.02s dom: 0xf2f → 0x30f2f

t=4.0s CPU=27.7% rDom=0x30f2f dDom=0x30f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0101 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=4.52s dom: 0x30f2f → 0x30e0f

[DOM-DEVICE] t=4.52s dom: 0x30f2f → 0x30e0f

t=4.5s CPU=30.1% rDom=0x30e0f dDom=0x30e0f rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.03s dom: 0x32faf → 0x32f2f

[DOM-DEVICE] t=5.03s dom: 0x32faf → 0x32f2f

t=5.0s CPU=30.1% rDom=0x32f2f dDom=0x32f2f rBus=0x3ffff dev=0x3ffff mainRms=0.0100 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=5.54s dom: 0x32f2f → 0x32faf

[DOM-DEVICE] t=5.54s dom: 0x32f2f → 0x32faf

t=5.5s CPU=28.8% rDom=0x32faf dDom=0x32faf rBus=0x3ffff dev=0x3ffff mainRms=0.0080 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.04s dom: 0x30f2f → 0xf2f

[DOM-DEVICE] t=6.04s dom: 0x30f2f → 0xf2f

t=6.0s CPU=30.4% rDom=0xf2f dDom=0xf2f rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=6.55s dom: 0xf2f → 0x30f0f

[DOM-DEVICE] t=6.55s dom: 0xf2f → 0x30f0f

t=6.5s CPU=22.6% rDom=0x30f0f dDom=0x30f0f rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.05s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.05s dom: 0x32f7f → 0x30f7f

t=7.1s CPU=30.0% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=7.55s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=7.55s dom: 0x32f7f → 0x30f7f

t=7.6s CPU=19.9% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.05s dom: 0x32f7f → 0x30f7f

[DOM-DEVICE] t=8.05s dom: 0x32f7f → 0x30f7f

t=8.1s CPU=30.3% rDom=0x30f7f dDom=0x30f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0104 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=8.55s dom: 0x30f7f → 0x32f7f

[DOM-DEVICE] t=8.55s dom: 0x30f7f → 0x32f7f

t=8.6s CPU=30.0% rDom=0x32f7f dDom=0x32f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.07s dom: 0x3ffff → 0x37fff

[DOM-DEVICE] t=9.07s dom: 0x3ffff → 0x37fff

t=9.1s CPU=29.8% rDom=0x37fff dDom=0x37fff rBus=0x3ffff dev=0x3ffff mainRms=0.0083 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=9.57s dom: 0x30f7f → 0x3afff

[DOM-DEVICE] t=9.57s dom: 0x30f7f → 0x3afff

t=9.6s CPU=30.4% rDom=0x3afff dDom=0x3afff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.07s dom: 0xf7f → 0x2f7f

[DOM-DEVICE] t=10.07s dom: 0xf7f → 0x2f7f

t=10.1s CPU=29.8% rDom=0x2f7f dDom=0x2f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=10.57s dom: 0x31f7f → 0x3ffff

[DOM-DEVICE] t=10.57s dom: 0x31f7f → 0x3ffff

t=10.6s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0105 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.08s dom: 0x37fff → 0x37f7f

[DOM-DEVICE] t=11.08s dom: 0x37fff → 0x37f7f

t=11.1s CPU=19.8% rDom=0x37f7f dDom=0x37f7f rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=11.58s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=11.58s dom: 0x37fff → 0x3ffff

t=11.6s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0103 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.09s dom: 0x37f7f → 0x3ffff

[DOM-DEVICE] t=12.09s dom: 0x37f7f → 0x3ffff

t=12.1s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=12.60s dom: 0x32f7f → 0x3ffff

[DOM-DEVICE] t=12.60s dom: 0x32f7f → 0x3ffff

t=12.6s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0099 subRms=0.0016 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.10s dom: 0x37fff → 0x3ffff

[DOM-DEVICE] t=13.10s dom: 0x37fff → 0x3ffff

t=13.1s CPU=21.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=13.60s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=13.60s dom: 0xffff → 0x3ffff

t=13.6s CPU=28.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0041 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.11s dom: 0xffff → 0x37fff

[DOM-DEVICE] t=14.11s dom: 0xffff → 0x37fff

t=14.1s CPU=20.0% rDom=0x37fff dDom=0x37fff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0019 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=14.61s dom: 0x7fff → 0x3ffff

[DOM-DEVICE] t=14.61s dom: 0x7fff → 0x3ffff

t=14.6s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=15.1s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=15.62s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=15.62s dom: 0xffff → 0x3ffff

t=15.6s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0060 Xrun=0 NaN=0 SpkG=0 PLAYING

t=16.1s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0192 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=16.62s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=16.62s dom: 0xffff → 0x3ffff

t=16.6s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.12s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.12s dom: 0xffff → 0x3ffff

t=17.1s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=17.63s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=17.63s dom: 0xffff → 0x3ffff

t=17.6s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=18.13s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=18.13s dom: 0xffff → 0x3ffff

t=18.1s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0213 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=18.6s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0243 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.15s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=19.15s dom: 0xffff → 0x3ffff

t=19.1s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0233 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=19.65s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=19.65s dom: 0xffff → 0x3ffff

t=19.6s CPU=22.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

t=20.1s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0062 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=20.65s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=20.65s dom: 0xffff → 0x3ffff

t=20.7s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=21.2s CPU=28.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=21.66s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=21.66s dom: 0xffff → 0x3ffff

t=21.7s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0032 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=22.17s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=22.17s dom: 0xffff → 0x3ffff

t=22.2s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0140 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=22.7s CPU=28.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0176 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=23.18s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=23.18s dom: 0xffff → 0x3ffff

t=23.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=23.7s CPU=29.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0156 subRms=0.0069 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.2s CPU=29.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

t=24.7s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0073 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.2s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

t=25.7s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.2s CPU=30.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0031 Xrun=0 NaN=0 SpkG=0 PLAYING

t=26.7s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0040 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.2s CPU=29.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0149 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=27.7s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0184 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

t=28.2s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=28.71s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=28.71s dom: 0xffff → 0x3ffff

t=28.7s CPU=20.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0175 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

t=29.2s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0054 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=29.72s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=29.72s dom: 0xffff → 0x3ffff

t=29.7s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0046 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

t=30.7s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=31.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=31.23s dom: 0xffff → 0x3ffff

t=31.2s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0178 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

t=31.7s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=32.23s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=32.23s dom: 0xffff → 0x3ffff

t=32.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=32.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=32.75s dom: 0xffff → 0x3ffff

t=32.7s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0248 subRms=0.0067 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=33.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.25s dom: 0xffff → 0x3ffff

t=33.2s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=33.75s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=33.75s dom: 0xffff → 0x3ffff

t=33.7s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0061 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=34.25s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=34.25s dom: 0xffff → 0x3ffff

t=34.3s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

t=34.8s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0199 subRms=0.0101 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.26s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.26s dom: 0xffff → 0x3ffff

t=35.3s CPU=27.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0141 subRms=0.0070 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=35.77s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=35.77s dom: 0xffff → 0x3ffff

t=35.8s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0100 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.27s dom: 0x3efef → 0x3efff

[DOM-DEVICE] t=36.27s dom: 0x3efef → 0x3efff

t=36.3s CPU=19.9% rDom=0x3efff dDom=0x3efff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0065 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=36.78s dom: 0x3dfff → 0x3ffff

[DOM-DEVICE] t=36.78s dom: 0x3dfff → 0x3ffff

t=36.8s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.28s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=37.28s dom: 0xffff → 0x3ffff

t=37.3s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=37.78s dom: 0x3afaf → 0xafaf

[DOM-DEVICE] t=37.78s dom: 0x3afaf → 0xafaf

t=37.8s CPU=19.7% rDom=0xafaf dDom=0xafaf rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.28s dom: 0x3ffff → 0x3afef

[DOM-DEVICE] t=38.28s dom: 0x3ffff → 0x3afef

t=38.3s CPU=30.2% rDom=0x3afef dDom=0x3afef rBus=0x3ffff dev=0x3ffff mainRms=0.0146 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=38.78s dom: 0x357df → 0x3ffff

[DOM-DEVICE] t=38.78s dom: 0x357df → 0x3ffff

t=38.8s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.29s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=39.29s dom: 0x3ffff → 0xffff

t=39.3s CPU=23.2% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0189 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=39.79s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=39.79s dom: 0x3ffff → 0xffff

t=39.8s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0216 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.30s dom: 0xffff → 0x3ffff

t=40.3s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0020 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=40.79s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=40.79s dom: 0xffff → 0x3ffff

t=40.8s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.30s dom: 0xffff → 0x3ffff

t=41.3s CPU=20.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0145 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=41.80s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=41.80s dom: 0xffff → 0x3ffff

t=41.8s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=42.30s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=42.30s dom: 0xffff → 0x3ffff

t=42.3s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0131 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

t=42.8s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0170 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=43.32s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=43.32s dom: 0xffff → 0x3ffff

t=43.3s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

t=43.8s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=44.3s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=44.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=44.83s dom: 0xffff → 0x3ffff

t=44.8s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0043 Xrun=0 NaN=0 SpkG=0 PLAYING

t=45.3s CPU=26.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0185 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=45.83s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=45.83s dom: 0xffff → 0x3ffff

t=45.8s CPU=27.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0174 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.34s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.34s dom: 0xffff → 0x3ffff

t=46.3s CPU=22.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0015 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=46.85s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=46.85s dom: 0xffff → 0x3ffff

t=46.8s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.35s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.35s dom: 0xffff → 0x3ffff

t=47.3s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0154 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=47.86s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=47.86s dom: 0xffff → 0x3ffff

t=47.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0148 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.36s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=48.36s dom: 0x3ffff → 0xffff

t=48.4s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0190 subRms=0.0013 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=48.86s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=48.86s dom: 0xffff → 0x3ffff

t=48.9s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0021 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.37s dom: 0xffff → 0x3ffff

t=49.4s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0158 subRms=0.0057 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=49.87s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=49.87s dom: 0xffff → 0x3ffff

t=49.9s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0144 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.37s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=50.37s dom: 0x3ffff → 0xffff

t=50.4s CPU=19.9% rDom=0x3ffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0179 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=50.88s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=50.88s dom: 0xffff → 0x3ffff

t=50.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.38s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=51.38s dom: 0x3ffff → 0xffff

t=51.4s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0010 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=51.88s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=51.88s dom: 0xffff → 0x3ffff

t=51.9s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0151 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0055 Xrun=0 NaN=0 SpkG=0 PLAYING

t=52.9s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0166 subRms=0.0039 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.40s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=53.40s dom: 0xffff → 0x3ffff

t=53.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0165 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=53.90s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=53.90s dom: 0xffff → 0x3ffff

t=53.9s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0143 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.40s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.40s dom: 0xffff → 0x3ffff

t=54.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0159 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=54.90s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=54.90s dom: 0xffff → 0x3ffff

t=54.9s CPU=22.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0155 subRms=0.0037 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=55.40s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=55.40s dom: 0xffff → 0x3ffff

t=55.4s CPU=20.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0187 subRms=0.0034 Xrun=0 NaN=0 SpkG=0 PLAYING

t=55.9s CPU=22.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

t=56.4s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=56.92s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=56.92s dom: 0xffff → 0x3ffff

t=56.9s CPU=20.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0161 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.42s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.42s dom: 0xffff → 0x3ffff

t=57.4s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0157 subRms=0.0018 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=57.92s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=57.92s dom: 0xffff → 0x3ffff

t=57.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0162 subRms=0.0107 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.4s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0172 subRms=0.0076 Xrun=0 NaN=0 SpkG=0 PLAYING

t=58.9s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0168 subRms=0.0053 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.43s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.43s dom: 0x3ffff → 0xffff

t=59.4s CPU=25.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0356 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=59.94s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=59.94s dom: 0x3ffff → 0xffff

t=59.9s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0191 subRms=0.0006 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.44s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=60.44s dom: 0x3ffff → 0xffff

t=60.4s CPU=22.4% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0153 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=60.95s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=60.95s dom: 0x3ffff → 0xffff

t=60.9s CPU=22.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0129 subRms=0.0001 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=61.45s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.45s dom: 0xffff → 0x3ffff

t=61.5s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0030 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=61.95s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=61.95s dom: 0xffff → 0x3ffff

t=62.0s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0137 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

t=62.5s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0202 subRms=0.0051 Xrun=0 NaN=0 SpkG=0 PLAYING

t=63.0s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0186 subRms=0.0050 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.47s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=63.47s dom: 0xffff → 0x3ffff

t=63.5s CPU=29.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0152 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=63.97s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=63.97s dom: 0x3ffff → 0xffff

t=64.0s CPU=30.5% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0147 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=64.47s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=64.47s dom: 0xffff → 0x3ffff

t=64.5s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0048 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=64.97s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=64.97s dom: 0x3ffff → 0xffff

t=65.0s CPU=28.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0306 subRms=0.0027 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=65.48s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=65.48s dom: 0xffff → 0x3ffff

t=65.5s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0112 subRms=0.0026 Xrun=0 NaN=0 SpkG=0 PLAYING

t=66.0s CPU=20.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0106 subRms=0.0033 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.49s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=66.49s dom: 0xffff → 0x3ffff

t=66.5s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0128 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=66.99s dom: 0x3f9fd → 0x3ffff

[DOM-DEVICE] t=66.99s dom: 0x3f9fd → 0x3ffff

t=67.0s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0096 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=67.49s dom: 0x3ffff → 0x3fdff

[DOM-DEVICE] t=67.49s dom: 0x3ffff → 0x3fdff

t=67.5s CPU=19.6% rDom=0x3fdff dDom=0x3fdff rBus=0x3ffff dev=0x3ffff mainRms=0.0087 subRms=0.0012 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.00s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=68.00s dom: 0x3ffff → 0xffff

t=68.0s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0079 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=68.50s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=68.50s dom: 0x3ffff → 0xffff

t=68.5s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0210 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.00s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=69.00s dom: 0x3fdff → 0x3ffff

t=69.0s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=69.51s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=69.51s dom: 0x3ffff → 0xffff

t=69.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0102 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=70.02s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=70.02s dom: 0xffff → 0x3ffff

t=70.0s CPU=30.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0116 subRms=0.0011 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=70.52s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=70.52s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=70.52s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=70.52s dom: 0x3ffff → 0xffff

t=70.5s CPU=30.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0142 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=71.02s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=71.02s mask: 0xffff → 0x3ffff

[DOM-RENDER] t=71.02s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=71.02s dom: 0xffff → 0x3ffff

t=71.0s CPU=27.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0135 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=71.52s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=71.52s dom: 0xffff → 0x3ffff

t=71.5s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0119 subRms=0.0028 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.02s dom: 0x3fdff → 0x3ffff

[DOM-DEVICE] t=72.02s dom: 0x3fdff → 0x3ffff

t=72.0s CPU=20.0% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0038 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=72.52s dom: 0xfdfd → 0x3ffff

[DOM-DEVICE] t=72.52s dom: 0xfdfd → 0x3ffff

t=72.5s CPU=20.1% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0094 subRms=0.0022 Xrun=0 NaN=0 SpkG=0 PLAYING

t=73.0s CPU=19.6% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0023 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=73.54s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=73.54s dom: 0x3ffff → 0xffff

t=73.5s CPU=19.7% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0232 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.04s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.04s mask: 0xffff → 0x3ffff

t=74.0s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0003 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=74.54s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=74.54s mask: 0xffff → 0x3ffff

t=74.5s CPU=19.9% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0114 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[RELOC-RENDER] t=75.04s mask: 0xffff → 0x3ffff

[RELOC-DEVICE] t=75.04s mask: 0xffff → 0x3ffff

t=75.0s CPU=20.0% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0090 subRms=0.0002 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=75.54s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=75.54s dom: 0xffff → 0x3ffff

t=75.5s CPU=19.7% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0110 subRms=0.0017 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=76.05s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=76.05s dom: 0xffff → 0x3ffff

t=76.1s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0109 subRms=0.0045 Xrun=0 NaN=0 SpkG=0 PLAYING

t=76.6s CPU=19.5% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0117 subRms=0.0036 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.06s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=77.06s dom: 0x3ffff → 0xffff

t=77.1s CPU=19.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0134 subRms=0.0004 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=77.57s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=77.57s dom: 0xffff → 0x3ffff

t=77.6s CPU=19.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0108 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=78.07s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=78.07s dom: 0x3ffff → 0xffff

t=78.1s CPU=19.8% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0169 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=78.57s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=78.57s dom: 0xffff → 0x3ffff

t=78.6s CPU=19.8% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0056 Xrun=0 NaN=0 SpkG=0 PLAYING

t=79.1s CPU=30.2% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0118 subRms=0.0029 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=79.58s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=79.58s dom: 0x3ffff → 0xffff

t=79.6s CPU=27.3% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0160 subRms=0.0008 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=80.09s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=80.09s dom: 0xffff → 0x3ffff

t=80.1s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0139 subRms=0.0042 Xrun=0 NaN=0 SpkG=0 PLAYING

t=80.6s CPU=29.9% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0193 subRms=0.0059 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.10s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=81.10s dom: 0xffff → 0x3ffff

t=81.1s CPU=30.4% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0132 subRms=0.0014 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=81.60s dom: 0x3ffff → 0xffff

[DOM-DEVICE] t=81.60s dom: 0x3ffff → 0xffff

t=81.6s CPU=29.6% rDom=0xffff dDom=0xffff rBus=0x3ffff dev=0x3ffff mainRms=0.0113 subRms=0.0005 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.10s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=82.10s dom: 0xffff → 0x3ffff

t=82.1s CPU=28.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0098 subRms=0.0009 Xrun=0 NaN=0 SpkG=0 PLAYING

[DOM-RENDER] t=82.60s dom: 0xffff → 0x3ffff

[DOM-DEVICE] t=82.60s dom: 0xffff → 0x3ffff

t=82.6s CPU=27.3% rDom=0x3ffff dDom=0x3ffff rBus=0x3ffff dev=0x3ffff mainRms=0.0075 subRms=0.0024 Xrun=0 NaN=0 SpkG=0 PLAYING

```

## Testing Round 1

## Pre test agent notes 1 :

Done. The (void)angleDelta suppresses any unused-variable warning since the comparison is now dead.

What to watch for in the test run:

Render-relocation and device-relocation messages should decrease or disappear if Fix 2 is the cause
Pops may also reduce, or may remain if they have a secondary source
Spatial image may be slightly less smooth for fast-moving sources (expected — sub-stepping is disabled)
Interpret the result:

Relocations gone → Fix 2 confirmed, proceed with hysteresis
Relocations remain → look elsewhere (DBAP focus changes, OutputRemap edge case, or something upstream in how poses are fed)
Relocations reduced but not eliminated → Fix 2 is a partial contributor, still need to look for another cause

- Ascent Test 1

* still resulted in reloc console messages almost immediately
* very quickly channels seemed to fully relocate

- Ascent Test 2

* still resulted in reloc console messages almost immediately
* channels didnt relocate as drastically but when they came back into focus - agressive pops and high pitched noise
```
