Root Cause Assessment
Most Plausible Categories (ranked)
Category A — Proximity guard is systematically altering all near-speaker sources (HIGH confidence)

The guard (kMinSpeakerDist = 0.15) was calibrated on one Eden source (21.1 at 0.049 m). It is set 3× the worst observed case. For Swale 360RA content, DirectSpeaker channels naturally land at small but non-zero distances from their target speakers (~0.02–0.10 m, depending on how ADM azimuth/elevation maps through directionToDBAPPosition and the Translab layout radius). Every one of those sources that falls inside 0.15 m gets pushed outward every single block. The localization gain toward the intended speaker is reduced; energy redistributes to neighbors. This is steady-state, always-on, and has no analog in the offline renderer (which has no guard at all). It explains:

flatter image / reduced presence (persistent, not burst)
gains out of proportion for 360RA
right-side bias if the fallback push direction in DBAP-internal space ((0, kMinSpeakerDist, 0) when dist < 1e-7) happens to land at a specific azimuthal sector after the un-flip
Category B — Guard fallback direction is not neutral (MEDIUM confidence, secondary to A)

When dist < 1e-7 (source exactly on a speaker), the guard doesn't push "away from the speaker" — it unconditionally uses (0, kMinSpeakerDist, 0) in DBAP-internal space. After the un-flip to pose-space that is (0, 0, -kMinSpeakerDist). That is a fixed angular direction, and if multiple sources trigger this path, they're all pushed to the same spot. This could produce a systematic bias toward a specific speaker cluster — consistent with the Swale "skewed right" observation. Whether this fires in practice depends on whether Swale sources ever reach exactly-zero distance, which requires the DirectSpeaker ADM positions to hit the speaker vector exactly. The more likely case is small-but-nonzero distances (Category A applies instead).

Category C — Block-boundary amplitude modulation from guard threshold crossing (MEDIUM confidence for the 130s buzz)

If a Swale source at ~130 s is in slow motion (within its trajectory window, not yet clamped to final keyframe), and its distance to the nearest speaker is oscillating near the 0.15 m boundary on alternate blocks, the DBAP gain for that speaker alternates between two distinct values every ~10 ms. That AM rate is ~86 Hz for 512-frame buffers at 48 kHz. It would be audible as a buzz. Once the source hits its final keyframe and holds, the distance is fixed and the AM stops. The observation "high-pitched buzzing around 130 s" with previously confirmed proximity-guard-related artifacts is consistent with this.

Category D — Steady-state smoother seeding offset (LOW confidence for steady-state symptoms)

ControlSmooth::smoothed initialises at hardcoded focus=1.0 regardless of the --focus CLI flag (1.5 default). It ramps to 1.5 in ~200 ms. At 130 s this has been irrelevant for ~129 s. Does not explain steady-state artifacts. **FIXED — see Fix 4 below.**

Test First
Category A — it is the only candidate that:

Is realtime-only (no counterpart in the offline renderer)
Is active every block for many sources simultaneously in steady state
Is directly observable without code changes (mState.speakerProximityCount is already being incremented)
Has a minimal, reversible diagnostic step (see below)
Smallest Safe Next Change
Add one log line in main.cpp (or wherever playback status is printed) that periodically prints speakerProximityCount, e.g. every 5 seconds of playback time. No audio behavior changes. If the count is climbing continuously during Swale playback (hundreds or thousands per second in steady state), Category A is confirmed as the dominant path.

That output directly tells you whether the guard is a steady-state phenomenon or only fires transiently on the Eden source-21.1 case it was designed for.

Questions
Is speakerProximityCount currently printed anywhere during playback, and if so, what are typical values during the Swale late section? Or has it not been checked yet?

For the 130s Swale buzz: is it present from measure one of that section every run (i.e., deterministic at that timestamp), or does it vary slightly between runs? Deterministic → guard firing at a fixed held-source position. Variable → amplitude modulation from a source still in motion near the threshold.

What is the Translab layout's speaker radius — roughly? This determines how large 0.15 m is relative to the layout, which changes whether Category A's gain effect is audible.

For the "skewed right" in Swale late section: does the offline render (same scene, same focus) sound balanced, or does offline also lean right? If offline is balanced and realtime leans right, that's a realtime-only distortion and the fallback-direction bias (Category B) becomes more interesting.

The proximity guard comment says the 0.049 m worst case is "source 21.1 at t=47.79 s" — that is from Eden. Has the guard ever been verified to fire at all during a Swale-only playback run (i.e., is Swale content actually close to any speakers, or was the guard only needed for Eden geometry)?

---

## 2026-03-09 — Discontinuity Fixes: onset fade (Fix 1, PATCHED) and fast-mover sub-stepping (Fix 2, PLAN ONLY)

### Context coming into this session

- Per-sample DBAP rendering was tried and reverted (caused worse artifacts).
- `renderBuffer()` block-based path is restored and canonical.
- Auto-compensation is disabled as a no-op (constant 1.0 return).
- A stronger always-on proximity-guard change was tried and reverted: made
  the image blurrier / more diffuse overall.
- After reverting, the image sounds more normal but clicks remain.
- A confirmed **low-end pop at source-appearance (onset)** is present.
- The two leading remaining candidates are:
  1. Source onset / activation discontinuity — step-from-zero at first active block
  2. Block-boundary motion discontinuity — large angular jump in one callback

---

### Fix 1: Onset fade — PATCHED (2026-03-09)

#### Problem

When a source first becomes active (previous block was silence / EOF zero-fill,
current block has real signal), the transition is an instantaneous amplitude step
from zero to the full-amplitude first sample. DBAP multiplies that step by speaker
gains, producing a wideband click heard as a low-end thump/pop.

#### Design decisions

| Decision                                                             | Reason                                                                                                                                       |
| -------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| Fixed per-source index (`si`) into `mSourceWasSilent[]`              | No string-keyed map. `mPoses` / `mSourceOrder` are stable from `loadScene()` — same slot every block.                                        |
| `prepareForSources(size_t n)` called on main thread before `start()` | Single allocation point. Audio thread never allocates. Guard `si < mSourceWasSilent.size()` keeps path safe if hook is accidentally skipped. |
| Fade only on first active block after silence                        | Subsequent blocks need no ramp; the discontinuity only exists at the 0→signal transition.                                                    |
| Energy gate (`kOnsetEnergyThreshold = 1e-10f`)                       | `getBlock()` writes exact `0.0f` for silence/EOF. Any real signal exceeds 1e-10f.                                                            |
| Fade length `kOnsetFadeSamples = 128` (~2.7 ms at 48 kHz)            | Short enough not to audibly smear attacks. Long enough to suppress a step-from-zero pop. Adjustable later.                                   |
| Ramp placed **before** master-gain multiply (DBAP path)              | Gain is applied once, not twice. Proximity guard operates on post-ramp samples.                                                              |
| Same ramp applied in both LFE and DBAP paths                         | LFE onset can also pop. Logic is identical in both branches.                                                                                 |

#### Files changed

- `spatial_engine/realtimeEngine/src/Spatializer.hpp`
  - Added `kOnsetEnergyThreshold` and `kOnsetFadeSamples` constants
  - Added `mSourceWasSilent` member (preallocated `std::vector<uint8_t>`)
  - Added `prepareForSources(size_t n)` public method
  - Changed `for (const auto& pose : poses)` → `for (size_t si = 0; si < poses.size(); ++si)`
  - Inserted onset-fade block in LFE branch (after `getBlock`, before `subGain`)
  - Inserted onset-fade block in DBAP branch (after `getBlock`, before masterGain loop)
- `spatial_engine/realtimeEngine/src/main.cpp`
  - Added `spatializer.prepareForSources(pose.numSources())` after `spatializer.init()`,
    before `backend.start()`

#### RT-safety

- Zero allocation in audio callback.
- `mSourceWasSilent[si]` is a plain `uint8_t` array read/write — no atomics needed
  (audio thread is sole owner after `start()`).
- Energy accumulation: O(numFrames) per source per block. At 80 sources × 512 frames
  = 40,960 FMA ops per block. Negligible vs. DBAP render cost.
- Ramp inner loop: at most 128 multiply-assigns per source per transition block only
  (not steady state).

---

### Fix 2: Fast-mover sub-stepping — PLAN (not yet patched)

#### Problem

When a source moves a large angular distance between consecutive audio blocks (~10 ms
at 48 kHz / 512 frames), DBAP speaker gains jump from the old-block values to the
new-block values without any within-block interpolation. At fast-moving segments this
produces a perceivable click at the block boundary.

#### Why the previous "prev-center interpolation" approach was rejected

Interpolating from `mPrevPositions[si]` (last block's center position) to the current
block's center position introduces **temporal lag**: the sub-step positions span
_last-block-center → current-block-center_, which is the time window of the _previous_
block, not the current one. This blurs localization — the current block's audio is
rendered with a position trajectory that lags one block behind reality.

#### Revised approach: current-block samples only

**Extend `SourcePose` with two new fields:**

```cpp
struct SourcePose {
    std::string name;
    al::Vec3f   position;       // DBAP position at block center (50%) — unchanged
    al::Vec3f   positionStart;  // DBAP position at block start (t = blockStart)
    al::Vec3f   positionEnd;    // DBAP position at block end   (t = blockEnd)
    bool        isLFE  = false;
    bool        isValid = true;
};
```

**Extend `Pose::computePositions()` signature:**

Change from:

```cpp
void computePositions(double blockCenterTimeSec)
```

To:

```cpp
void computePositions(double blockStartTimeSec, double blockEndTimeSec)
```

`blockCenterTimeSec = (blockStartTimeSec + blockEndTimeSec) / 2.0` is derived
internally. The existing `position` field continues to be computed at the midpoint.
`positionStart` and `positionEnd` are computed via the _identical_ pipeline
(interpolateDirRaw → safeDirForSource → sanitizeDirForLayout → directionToDBAPPosition)
at `blockStartTimeSec` and `blockEndTimeSec` respectively.

**Caller change in `RealtimeBackend::processBlock()` (Step 2):**

```cpp
// Currently:
const double blockCtrSec = static_cast<double>(curFrame + numFrames / 2) / sampleRate;
mPose->computePositions(blockCtrSec);

// Becomes:
const double blockStartSec = static_cast<double>(curFrame)             / sampleRate;
const double blockEndSec   = static_cast<double>(curFrame + numFrames) / sampleRate;
mPose->computePositions(blockStartSec, blockEndSec);
```

**Fast-mover detection in `Spatializer::renderBlock()`:**

Use `positionStart` and `positionEnd` for the angular-change test, matching the
offline renderer's Q1/Q3 approach conceptually (start→end captures the total
angular span of the block; conservative, may trigger slightly more than the offline
test — acceptable):

```cpp
// Normalize positions to unit sphere for angular comparison
al::Vec3f d0 = pose.positionStart.normalized();
al::Vec3f d1 = pose.positionEnd.normalized();
float dotVal = std::clamp(d0.dot(d1), -1.0f, 1.0f);
float angleDelta = std::acos(dotVal);
bool isFastMover = (angleDelta > kFastMoverAngleRad);
```

`acos` is called once per source per block, only for non-LFE, non-skipped sources.
Not in an inner loop — cost is acceptable.

**Sub-step rendering (4 sub-chunks, kNumSubSteps = 4):**

For fast-mover sources only, split `mSourceBuffer` into 4 equal sub-chunks and render
each sub-chunk into `mFastMoverScratch` with the interpolated position at that
sub-chunk's center time, then accumulate into `mRenderIO`:

```
Sub-chunk 0: samples [0..127]    center at 12.5% of block  → lerp(positionStart, positionEnd, 0.125)
Sub-chunk 1: samples [128..255]  center at 37.5%           → lerp(positionStart, positionEnd, 0.375)
Sub-chunk 2: samples [256..383]  center at 62.5%           → lerp(positionStart, positionEnd, 0.625)
Sub-chunk 3: samples [384..511]  center at 87.5%           → lerp(positionStart, positionEnd, 0.875)
```

Linear interpolation in DBAP-space Cartesian coordinates is acceptable: positions
lie on or near a sphere of radius `mLayoutRadius`, so linear interpolation stays
close to the sphere surface and `mLayoutRadius` is order-of-magnitude larger than
the proximity threshold `kMinSpeakerDist`. The proximity guard is re-run on each
sub-chunk position (same flip → guard → un-flip pattern as the main path).

**New members needed in `Spatializer`:**

```cpp
static constexpr float kFastMoverAngleRad = 0.25f;  // ~14.3°, matches offline renderer
static constexpr int   kNumSubSteps       = 4;       // 512 / 4 = 128-frame sub-chunks
// Pre-allocated scratch AudioIOData: subFrames × outputChannels.
// Sized at init(). Audio-thread-owned.
al::AudioIOData mFastMoverScratch;
```

**`mFastMoverScratch` sizing (in `init()`, after `mRenderIO` sizing):**

```cpp
{
    int subFrames = std::max(1, mConfig.bufferSize / kNumSubSteps);
    mFastMoverScratch.framesPerBuffer(subFrames);
    mFastMoverScratch.framesPerSecond(mConfig.sampleRate);
    mFastMoverScratch.channelsIn(0);
    mFastMoverScratch.channelsOut(computedOutputChannels);
}
```

**No `mPrevPositions` member needed.** All positions come from the current block.

#### RT-safety concerns for Fix 2

| Concern                                              | Status                                                                                                                                       |
| ---------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `acos()` per non-LFE source per block                | Called once per source per block — not inside a sample loop. ~80 trig ops per block. Acceptable.                                             |
| `mFastMoverScratch.zeroOut()` per sub-chunk          | 4 × 128 × outputChannels float-zeroes per fast-mover source. One per source that triggers, not per sample.                                   |
| Proximity guard runs 4× per fast-mover               | 4 × numSpeakers vector ops. At 50 speakers: 200 vector ops per fast-mover source per block. Fine.                                            |
| `numFrames % kNumSubSteps == 0` guard                | Required. Prevents under-read from `mSourceBuffer`. With `bufferSize=512, kNumSubSteps=4` always true. Guard makes it robust to other sizes. |
| `positionStart.normalized()`                         | `al::Vec3f::normalized()` is a magnitude + divide. Called twice per source per block for the angular test. No allocation.                    |
| `positionStart`/`positionEnd` fields in `SourcePose` | Two extra `al::Vec3f` per source. For 80 sources: 80 × 2 × 12 bytes = 1.9 KB extra per block traversal. Negligible.                          |
| Caller signature change in `computePositions()`      | One additional `double` parameter to compute per call site. Only one call site: `RealtimeBackend::processBlock()`.                           |

#### Files that will be touched (Fix 2, when implemented)

1. `RealtimeTypes.hpp` or `Pose.hpp` — extend `SourcePose` with `positionStart`, `positionEnd`
2. `Pose.hpp` — change `computePositions()` signature; add 2 extra position computations per source
3. `RealtimeBackend.hpp` — update `computePositions()` call in Step 2 of `processBlock()`
4. `Spatializer.hpp` — add constants, `mFastMoverScratch` member, sizing in `init()`, sub-step path in DBAP branch

LFE sources are excluded from fast-mover detection (they have no spatial position).

---

### Recommended patch order

1. ✅ Fix 1 (onset fade) — patched in this session. Verify pop is gone first.
2. Fix 2 (fast-mover sub-stepping) — implement after Fix 1 is confirmed audibly clean.
   The remaining clicks after Fix 1 will be more precisely attributable to block-boundary
   motion, making Fix 2 easier to evaluate in isolation.

---

## 2026-03-09 — Device/Output Channel Coupling: Audit and Fix 3

### Context

The engine now sounds good in some runs but produces non-deterministic output failures:

- Eden run 1: audio shifted to channels 15+ upward, skipping 1–14
- Canyon/Swale mid-playback: loudspeakers disappeared while subs remained
- MOTU channels 1 and 2 extremely quiet or skipped
- First-run / second-run behavior differs for the same content

Focus shifted from DSP/click polishing to routing/output-state correctness.

---

### Investigation: render-to-device copy path audit

#### Internal bus (render buffer)

`Spatializer::init()` computes `computedOutputChannels` from the layout:

```
maxChannel = max(numSpeakers - 1, max(subwooferDeviceChannels))
outputChannels = maxChannel + 1
```

This value is written into `mConfig.outputChannels` and `mRenderIO` is sized to it.
The render buffer is layout-sized and never changes after init.

#### Device open path (`RealtimeBackend::init()`)

`mAudioIO.init(...)` is called with `mConfig.outputChannels` as the requested channel
count. AlloLib passes this to PortAudio, which **negotiates with the OS default audio
device** — not the MOTU or any specific device. The returned `mAudioIO.channelsOut()`
is the **actual negotiated count**, which PortAudio may reduce silently if the
selected device does not support the requested count.

**Critical problem:** after `mAudioIO.open()` the code logs the actual channel count
but does not compare it to the requested count. There is no check, no warning, and no
refusal. `mInitialized = true` proceeds unconditionally.

```
// (RealtimeBackend.hpp, in init(), after mAudioIO.open())
mInitialized = true;
std::cout << "[Backend] Audio device opened successfully." << std::endl;
// Report actual device parameters (may differ from requested)
std::cout << "  Actual output channels: " << mAudioIO.channelsOut() << std::endl;
std::cout << "  Actual buffer size:     " << mAudioIO.framesPerBuffer() << std::endl;
return true;          // ← no mismatch check
```

#### Identity copy path (`Spatializer::renderBlock()`)

```cpp
const unsigned int numOutputChannels = io.channelsOut();    // actual device
const unsigned int copyChannels = std::min(renderChannels, numOutputChannels);
for (unsigned int ch = 0; ch < copyChannels; ++ch) {
    const float* src = mRenderIO.outBuffer(ch);
    float* dst = io.outBuffer(ch);
    for (unsigned int f = 0; f < numFrames; ++f)
        dst[f] += src[f];
}
```

Verdict: **no invalid write**. `std::min` prevents OOB. BUT: if `numOutputChannels
< renderChannels` (e.g., device opened at 2 ch, layout needs 18), channels
`numOutputChannels…renderChannels-1` are silently dropped — never reaching hardware.
The LFE subwoofer channels (e.g., Translab sub at index 16, 17) may still be within
the truncated range if they happen to be ≤ `numOutputChannels-1`, which is why subs
can survive while mains above the cutpoint disappear.

#### Remap load order bug (secondary, only affects `--remap` path)

In `main.cpp`, `outputRemap.load()` is called at line 371, but `backend.init()` is
at line 477. At remap load time, `config.outputChannels` exists (layout-derived) but
the actual device channel count is not yet known. Both `renderChannels` and
`deviceChannels` arguments to `load()` are passed as `config.outputChannels`:

```cpp
bool remapOk = outputRemap.load(remapPath,
                                config.outputChannels,   // renderChannels ← correct
                                config.outputChannels);  // deviceChannels ← WRONG:
                                                         // should be actual hw count
```

Any remap entry targeting a device channel ≥ `config.outputChannels` would be dropped
even if supported by the device. Not triggering current symptoms (no `--remap` in
failing tests) but is a latent correctness bug.

---

### Root cause of non-deterministic channel routing

**AlloLib/PortAudio selects the macOS default audio device when no device ID is
specified.** If that device has fewer output channels than the layout requires
(e.g., MacBook built-in = 2, MOTU = 18+, layout = 18), the device opens with 2
channels. The identity copy truncates to 2 channels silently. On a run where the
MOTU is the default, 18 channels open correctly. This explains:

- Eden run 1 wrong / run 2 right: different runs hit different macOS default states
- "channels 15+ upward, skipping 1–14": consistent with a truncation at a low
  channel count that happens to allow only the upper sub/LFE channels through
  (actually this is inconsistent — see tentative note below)
- MOTU channels 1 and 2 quiet: if only 2 channels negotiated, only render channels
  0 and 1 reach the device, which may be mapped to MOTU outputs 1 and 2

**The "loudspeakers disappeared, subs remained" pattern fits this exactly if the
subwoofer deviceChannel indices (Translab: 16, 17) are ABOVE the negotiated device
channel cutpoint.** In that case subs are in the silently-dropped render channels
and should also disappear — meaning if subs remain while mains go, the exact channel
cutpoint must be above the main-speaker range and below or at the sub channel indices,
which seems unlikely unless the sub indices happen to be 0 or 1.

**Therefore:** "subs remain, mains disappear" is NOT cleanly explained by a simple
channel count truncation alone. The truncation bug explains the non-deterministic
first-run/second-run failure and the channel-skip observations. The mains-disappear
symptom may have additional contributors (guard mass-fire remains tentative).

---

### Fix 3: Post-open device channel validation — PATCHED (2026-03-09)

#### Problem

After `mAudioIO.open()`, the actual negotiated channel count is logged but never
validated. If the OS-selected device has fewer channels than the layout requires,
the engine starts and silently drops speaker channels.

#### Decision: refuse to start on channel count mismatch

Continuing with fewer channels than the layout requires will always produce incorrect
spatial output. Silent truncation is never acceptable in a spatial audio engine.
The correct behavior is a clear error + refusal to start, forcing the user to
verify/set the correct audio device before running.

On over-provisioned device (actual > requested): accepted. Extra hardware channels
are unused and cause no harm.

#### Files changed

- `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
  - After `mAudioIO.open()` succeeds and actual params are logged:
  - Added comparison of `mAudioIO.channelsOut()` against `mConfig.outputChannels`
  - On under-provision (`actual < required`): clear error, `mInitialized = false`,
    return false — engine refuses to start
  - On over-provision (`actual > required`): info log only, proceed normally

#### Relation to remap load order bug

Not fixed in this pass. The `--remap` path's `deviceChannels` argument remains
incorrectly set to `config.outputChannels` instead of the post-open actual count.
This is a latent bug. It will be fixed in a future pass by moving remap load to
after `backend.init()` returns, or by adding a `setDeviceChannels(int n)` method
on `OutputRemap` for post-open validation.

---

### SpeakerGuard spike / mains-disappear — status: TENTATIVE

The Canyon/Swale "loudspeakers disappeared, subs remained" behavior COULD be:
(a) the channel truncation bug on a run where the device opened under-provisioned
(b) proximity guard mass-fire concentrating 101 sources on the DBAP equidistant
shell → per-speaker gain below audibility threshold for mains only
(c) both, with the channel bug as the primary event and guard as amplifier

Current code evidence for (b): guard fires per-source per block (not per-channel),
and the LFE path bypasses both guard and DBAP, writing directly to subwoofer channel
slots in mRenderIO. So even if all DBAP sources are pushed to the equidistant shell,
subs still get full signal. This does support (b) structurally, but no actual
`speakerProximityCount` data from a Canyon run has been collected.

**Do not conclude (b) is root cause until `speakerProximityCount` data from a
Canyon/Swale run is compared against the disappearance events.**
The channel validation fix should be applied first; if the second run (post-fix)
still shows mains disappearing despite correct channel counts, escalate (b).

---

## 2026-03-09 — Session 3: Reset/gain path investigation and GUI restart fix

### Context entering this session

New behavioral data from listener tests:

- Playback is sometimes **worse in the same app instance** on a second run;
  restarting the app fixes it. Strong indicator of state not being reset.
- In Swale, the issue is **deterministic and gain-gated**: behavior was
  described as "totally perfect" below gain=1, then the failure appeared
  when gain was moved above 1. This is a reliable trigger.
- SpeakerProximityCount is present in the monitoring output but observed
  values during failures had not yet been correlated to the audio events.

---

### Investigation 1: GUI / OSC reset path

#### Finding: primary reset bug is in `RealtimeRunner.restart()` bypassing control reset

**Full call chain for normal Start (`RealtimeWindow._on_start()`):**

1. `self._controls_panel.reset_to_defaults()` — sliders snap to safe defaults
   with `emit=False` (no OSC sent, visual reset only)
2. `self._runner.start(cfg)` — launches engine with `--gain 0.5` from `RealtimeConfig`
3. C++ ParameterServer comes up → sentinel line detected in stdout
4. `engine_ready` signal fires → `flush_to_osc()` sends all slider values to engine
5. Engine receives gain=0.5, focus=1.5, mixes=0 dB — matching the defaults

**Full call chain for Restart (`RealtimeTransportPanel` restart button):**

1. `t.restart_requested` → was wired directly to `self._runner.restart()`
2. `RealtimeRunner.restart()` calls `self.stop_graceful()` then
   `QTimer.singleShot(200, lambda: self.start(self._config))`
3. `runner.start()` launches the engine — **`_on_start()` is never called**
4. `reset_to_defaults()` is **never called**
5. C++ ParameterServer comes up → `flush_to_osc()` fires
6. `flush_to_osc()` reads **whatever the sliders currently show** — e.g. gain=1.5
   left from a slider move during the previous run
7. Engine receives gain=1.5 from the very first OSC message after startup

**Key structural point:** slider moves only update the Qt widget and send OSC.
They do NOT write back to `RealtimeConfig.master_gain`. The `RealtimeConfig`
dataclass field stays at its construction-time value (0.5). So every restart
always passes `--gain 0.5` via CLI — but the OSC flush immediately overrides
this with whatever the slider is showing, which can be anything up to 3.0.

This is the confirmed primary reset bug. It is GUI-layer only; it requires no
C++ changes and no audio-path changes.

#### Fix 4 (primary): reset controls on restart — PATCHED (2026-03-09)

**File changed:** `gui/realtimeGUI/realtimeGUI.py`

- Changed `t.restart_requested.connect(self._runner.restart)` to
  `t.restart_requested.connect(self._on_restart)`
- Added `_on_restart()` method:
  ```python
  def _on_restart(self) -> None:
      self._controls_panel.reset_to_defaults()
      self._runner.restart()
  ```

`reset_to_defaults()` uses `emit=False` on all set_value calls, so no OSC is
sent during the reset — it is a silent visual snap. The flush happens later,
after `engine_ready` fires, at which point all sliders are at safe defaults.

#### Fix 5 (secondary): smoother pre-seeding — PATCHED (2026-03-09)

**File changed:** `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`

`RealtimeBackend` constructor now pre-seeds `mSmooth.smoothed` and `mSmooth.target`
from the actual `config` atomics instead of leaving them at struct defaults (1.0f).

Without this, even on a normal first run (no stale GUI state), the very first ~200 ms
of playback runs at `masterGain=1.0` and `focus=1.0` instead of the CLI-specified
values (e.g. 0.5 / 1.5), because the smoother is converging from wrong initial values.
This is a real startup transient bug, but minor compared to Fix 4. Fix 4 addresses the
deterministic same-session failure; Fix 5 cleans up a transient on every run.

Both fixes should be kept.

---

### Investigation 2: Main-channel collapse location (mRenderIO vs output copy)

#### Code-derived conclusion: if channel validation is passing, collapse is in mRenderIO

The device copy path (identity fast-path in `Spatializer::renderBlock()`):

```cpp
const unsigned int copyChannels = std::min(renderChannels, numOutputChannels);
for (unsigned int ch = 0; ch < copyChannels; ++ch) {
    dst[f] += src[f];   // additive copy from mRenderIO → io.outBuffer
}
```

Since Fix 3 (post-open channel validation) was applied, the engine now refuses
to start if `actualOutChannels < mConfig.outputChannels`. If the engine is
running at all, `numOutputChannels >= renderChannels`, so `copyChannels ==
renderChannels` and no render channel is dropped at the copy step.

Therefore: if mains are absent at the hardware output during a running session,
they are already absent in `mRenderIO` at the time of the copy.

#### What can cause mains to collapse in mRenderIO while subs remain

The render paths diverge completely inside `renderBlock()`:

- **DBAP path (mains):** `mSourceBuffer[f] *= masterGain` → proximity guard →
  `mDBap->renderBuffer(mRenderIO, safePos, mSourceBuffer, numFrames)` → Phase 6
  `spkMix` multiply → `kMaxSample` clamp
- **LFE path (subs):** `mSourceBuffer[f] * (masterGain * 0.95 / numSubs)` written
  directly to `mRenderIO.outBuffer(subCh)` — no DBAP, no guard

Any mechanism that collapses DBAP output while leaving LFE untouched is confined
to the DBAP path inside `renderBlock()`. The sub path has no guard, no position
dependency, and a gain divisor that keeps it well below 1.0 for `masterGain ≤ 2.0`.

#### Diagnostic already instrumented: nanGuardCount

The monitoring loop already prints `nanGuardCount` every 500 ms. `nanGuardCount`
is incremented once per block in which any sample in `mRenderIO` exceeds
`kMaxSample = 4.0f` or is non-finite, **before** the device copy.

- **If nanGuardCount climbs during the failure:** mains are being clamped to ±4.0
  inside `mRenderIO`. The clamp is suppressing them but they remain audible as
  loud distortion, not silence — so if mains are actually silent, this is not
  the mechanism.
- **If nanGuardCount stays 0 during the failure:** main channel samples in
  `mRenderIO` are near zero, not near 4.0. DBAP is producing near-zero output.
  This points to a gain structure producing near-zero accumulation across all
  sources, not an overrange/clamp event.

**This monitoring data is the key discriminator. It is collected automatically —
no code changes needed. The next Swale gain-above-1 test should note whether
nanGuardCount is climbing as mains disappear.**

---

### Investigation 3: Gain trigger path — where gain > 1 is applied

`/realtime/gain` OSC → `config.masterGain` atomic → snapshotted once per block
into `ctrl.masterGain` → applied here, before DBAP:

```cpp
// Spatializer::renderBlock(), DBAP path
for (unsigned int f = 0; f < numFrames; ++f) {
    mSourceBuffer[f] *= masterGain;   // ← gain applied per-source, pre-DBAP
}
mDBap->renderBuffer(mRenderIO, safePos, mSourceBuffer.data(), numFrames);
```

For the LFE path, `subGain = masterGain * 0.95 / numSubs`. At `numSubs=2` and
`masterGain=1.5`, `subGain ≈ 0.71`. Subs stay below 1.0 per channel until
`masterGain > 2.1`.

**The gain > 1 failure is mains-only because:**

- Mains accumulate DBAP output from all N non-LFE sources, each pre-multiplied
  by `masterGain`. Speaker channel amplitudes scale as O(N × masterGain).
- Subs accumulate from only the single LFE source, with a gain divisor that keeps
  them in safe range across the full slider range.

**Why gain > 1 is deterministic but geometry/guard should not depend on gain:**
The user correctly notes that guard firing is determined by position, not gain.
The guard fires on sources within `kMinSpeakerDist = 0.15 m` regardless of `masterGain`.
Therefore a gain-gated failure cannot be primarily caused by the proximity guard.
The guard is a correlate at best (sources near speakers also produce high per-speaker
DBAP gains, and guard firing would be noted in `speakerProximityCount`, but the
guard does not reduce output amplitude — it pushes sources slightly away and the
DBAP output actually decreases).

**Current status: gain-path failure mechanism is not yet confirmed.**
The smoothing pre-seed fix removes one compounding factor (startup gain transient).
The restart reset fix removes the primary source of stale gain state between runs.
After those two fixes, a controlled re-test (run → set gain > 1 → observe failure →
check `nanGuardCount` and `speakerProximityCount` at that moment) will determine
whether the remaining failure is:

- (a) amplitude overrange clamped + distorted in mRenderIO (nanGuardCount high)
- (b) near-zero DBAP accumulation from some geometry condition (nanGuardCount 0,
  speakerProximityCount high)
- (c) hardware driver protection firing on over-range that the current clamp
  (`kMaxSample=4.0`) does not prevent (no existing in-engine counter; would require
  a softer clamp ceiling, e.g. 1.0–2.0, to observe)

Do not assume cause until this test data is available.

---

### SpeakerGuard — revised status after this session

**SpeakerGuard is a correlate, not the confirmed primary cause of gain-gated failures.**

Evidence:

- Guard firing is determined by position geometry, not by `masterGain` value.
  A gain-deterministic failure cannot be primarily caused by position-dependent guard.
- Guard fires per-source per block, incrementing `speakerProximityCount`. If the guard
  were causing mains to collapse, `speakerProximityCount` would be high and correlated
  with the event. This data has not yet been collected for a Swale gain>1 failure.
- Guard does not reduce DBAP output amplitude — it displaces sources slightly outward,
  which if anything would reduce the per-speaker gain spike, not create silence.

Guard may still be an **amplifier**: sources displaced to the guard shell contribute
more evenly across all speakers, diffusing the image and reducing perceived loudness
on any individual speaker. This is a plausible secondary effect but does not produce
the "mains disappeared completely" pattern.

**Next step:** collect `speakerProximityCount` and `nanGuardCount` from a Swale run
during a gain>1 failure. These two counters together narrow the failure to one of the
three mechanisms listed above.

---

### Patches applied this session

| Fix                   | File                                                    | Description                                                                                                                                        |
| --------------------- | ------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fix 4 (primary reset) | `gui/realtimeGUI/realtimeGUI.py`                        | Restart now calls `reset_to_defaults()` before re-launching; stale slider state no longer flushed into new engine instance                         |
| Fix 5 (smoother seed) | `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Constructor pre-seeds `mSmooth.smoothed` and `mSmooth.target` from actual config atomics; eliminates ~200 ms startup transient at wrong gain/focus |
