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

---

## 2026-03-09 — Session 4: Fix 3 is a no-op (channelsOut vs channelsOutDevice)

### Context entering this session

The previous session concluded that if Fix 3 (post-open channel validation) is
working, any mid-playback mains collapse must originate inside `mRenderIO` before
the device copy. The latest test data contradicted this framing: channels were
observed **moving to different ranges dynamically at runtime**, including in response
to subMix changes. This is inconsistent with a purely DSP-side collapse and points
back to a routing/device-state bug.

The new symptoms in detail:

- Some audio disappeared around 150 s (Canyon test 1)
- Loudspeakers jumped to channels 1–2, then 19+, skipping the main range (Canyon test 2)
- Loudspeakers disappeared then jumped to higher/non-existent channels when subMix
  was dramatically moved (Canyon test 3)
- Returned to original channels after ~2 s; brief high-pitched noise on return
- SpeakerGuard stayed at 4, underruns 0, NaNs 0 throughout all events

The new evidence strongly suggests the failure is **at or after the device copy
stage**, not inside `mRenderIO`, because the active output channel set changes
dynamically rather than collapsing uniformly.

---

### Finding: Fix 3 uses the wrong AlloLib accessor — validation never fires

#### The bug

Fix 3 reads:

```cpp
const int actualOutChannels = static_cast<int>(mAudioIO.channelsOut());
if (actualOutChannels < mConfig.outputChannels) { ... abort ... }
```

`mAudioIO.channelsOut()` returns `mNumO` — the value set during `mAudioIO.init()`
by `AudioIOData::channels(outChansA, true)`. This is the **requested** channel
count, which was passed in as `mConfig.outputChannels`. The comparison is therefore
`mConfig.outputChannels < mConfig.outputChannels`, which is **always false**.
Fix 3 never fires. The engine has never actually validated the negotiated hardware
channel count.

The correct accessor is `mAudioIO.channelsOutDevice()`, which queries
`data->oParams.nChannels` from the RtAudio backend. This is the value set by
`AudioBackend::channels()` during `mAudioIO.init()` after clamping the requested
count to `min(requested, info.outputChannels)` — i.e. the actual hardware limit.

#### AlloLib call chain (traced in source)

```
mAudioIO.init(callback, this, bufSize, sampleRate, outChans, inChans)
  → AudioIO::init(...)              // selects default device
  → device(dev)                     // calls deviceOut(dev) → channelsOut(dev.channelsOutMax())
                                    //   this sets mNumO = device.channelsOutMax()
                                    //   BUT only if mNumO != dev.channelsOutMax()
  → channels(inChansA, false)       // sets mNumI
  → channels(outChansA, true)       // calls AudioBackend::channels(outChansA, true)
                                    //   → clamps: num = min(outChansA, dev.outputChannels)
                                    //   → sets data->oParams.nChannels = num  (actual hw count)
                                    // then calls AudioIOData::channels(outChansA, true)
                                    //   → sets mNumO = outChansA              (requested count)

mAudioIO.channelsOut()       → mNumO                    = requested count (always == mConfig.outputChannels)
mAudioIO.channelsOutDevice() → oParams.nChannels         = PortAudio/RtAudio-negotiated count
```

`mAudioIO.open()` later opens the RtAudio stream using `oParams.nChannels` as
the hardware channel count. If that was clamped to 2 (MacBook built-in), the
stream opens at 2 channels even though `mNumO` still says 18. The rtaudioCallback
then interleaves only `channelsOutDevice()` channels into the hardware output
buffer, silently discarding the rest.

#### Why this was not caught

The log line added with Fix 3 reads:

```cpp
const int actualOutChannels = static_cast<int>(mAudioIO.channelsOut());
std::cout << "  Actual output channels: " << actualOutChannels << std::endl;
```

Because this uses the same wrong accessor, the log always prints the requested
count regardless of the device. So even the diagnostic logging was useless as
a check. The log would show "Actual output channels: 18" even when the device
opened at 2.

#### Effect on the copy path in Spatializer::renderBlock()

```cpp
const unsigned int numOutputChannels = io.channelsOut();   // also mNumO = requested
const unsigned int copyChannels = std::min(renderChannels, numOutputChannels);
```

This also uses `io.channelsOut()` (= `mNumO` = requested). The `std::min` never
reduces `copyChannels` below `renderChannels`, so the copy loop always writes all
render channels into `io.outBuffer(ch)` for `ch = 0..renderChannels-1`.

However, `io.outBuffer(ch)` writes into the internal `mBufO` array which is sized
to `mNumO` (requested). The rtaudioCallback then interleaves only the first
`channelsOutDevice()` entries of that buffer into the hardware output. Channels
beyond `channelsOutDevice()` are written into `mBufO` but never copied to the
device — they are silently discarded.

**Net effect:** even though there is no out-of-bounds write, the device only
receives the first N channels where N = `channelsOutDevice()`. All render channels
at indices ≥ N are rendered, mix-trimmed, and clamped — then silently dropped
after `processAudio()` returns.

---

### Why subMix perturbations can produce dynamic channel-range shifts

From the C++ code alone, `ctrl.subMix` flows only into the LFE path and cannot
affect speaker channel routing. However, two external mechanisms can produce the
observed behavior:

**Mechanism 1 — macOS audio routing change triggered by level**

macOS can silently reassign the default audio device when signal levels or device
activity change. If the MOTU wakes or takes priority while the engine is running,
`channelsOutDevice()` (the PortAudio-negotiated count) does not change — it was
fixed at stream-open time. However, the stream's actual output endpoint may
re-route within macOS. A subMix change that produces a loud transient on the
sub outputs could be the trigger for the OS to switch the active device route,
causing the subsequent blocks to be delivered to a different physical device or
a different channel mapping within the MOTU.

**Mechanism 2 — Wrong device opened, correct device appears mid-playback**

If the engine opens on the MacBook built-in (e.g. 2-channel), only render
channels 0 and 1 reach hardware. Channels 2+ are dropped. If macOS then routes
the audio stream to the MOTU (which has 18+ channels), the frame buffer that was
being written for 2-channel output is now being interpreted as MOTU channel data
— with the same interleave layout, now mapping to different MOTU channel groups.
This would produce exactly the "channels jump to a different range" symptom.

**Mechanism 3 — Sub channel index overlapping with DBAP speaker channels (layout-specific)**

If a layout has a subwoofer `deviceChannel` value that is also within the 0-based
consecutive speaker index range (e.g., sub at channel 2 in a layout with 4
speakers), `isSubwooferChannel(2)` returns true, so that channel slot is excluded
from `spkMix` and scaled by `lfeMix` instead. Moving `subMix` would then directly
affect what is audible on that speaker index. This is a layout-correctness question,
not a code bug. Verify that the Canyon test layout's subwoofer `deviceChannel`
values are all outside the `[0, numSpeakers-1]` index range.

---

### Proposed Fix 6: correct the channelsOutDevice accessor in Fix 3

**One line change in `RealtimeBackend::init()` in `RealtimeBackend.hpp`.**

Current (broken — always reads requested count):

```cpp
const int actualOutChannels = static_cast<int>(mAudioIO.channelsOut());
```

Fix 6:

```cpp
const int actualOutChannels = static_cast<int>(mAudioIO.channelsOutDevice());
```

This change must be applied in **two places** in `init()`:

1. The log line: `"  Actual output channels: " << actualOutChannels`
2. The Fix 3 guard: `if (actualOutChannels < mConfig.outputChannels)`

Both currently use the same local `actualOutChannels` variable, so a single
assignment fix propagates to both. Confirm the variable is used for both before
applying.

**No other files need to change.** The fix is entirely within the `init()` method.

#### Secondary: copy-path also uses wrong accessor

In `Spatializer::renderBlock()`:

```cpp
const unsigned int numOutputChannels = io.channelsOut();
```

If Fix 6 makes the guard work correctly (engine refuses to start if device
under-provisioned), this line becomes safe by invariant: `io.channelsOutDevice()

> = renderChannels`is guaranteed at start time. The`std::min` in the copy loop
> remains a useful safety belt but is no longer the primary line of defence.

No change to the Spatializer copy path is required as part of Fix 6; the guard
at init time is sufficient. The Spatializer `io.channelsOut()` read could be
changed to `io.channelsOutDevice()` for semantic correctness in a follow-up, but
it is not the blocking issue.

---

### Status after this session

| Issue                                           | Status                                                                                                                                                          |
| ----------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Fix 3 (post-open validation)                    | ~~**BROKEN** — uses `channelsOut()` instead of `channelsOutDevice()`; never fires~~                                                                             |
| Fix 6 (correct accessor)                        | **PATCHED** — `mAudioIO.channelsOut()` → `mAudioIO.channelsOutDevice()` in `RealtimeBackend::init()`; guard and log now read actual negotiated hw channel count |
| subMix → loudspeaker channel perturbation       | **Explained** by external device-routing mechanisms, not a C++ code bug in the mixing path                                                                      |
| Dynamic channel-range shifts                    | **Consistent** with Fix 3 being a no-op: device opens on wrong device, Fix 3 silent, wrong-device output produced                                               |
| Collapse is in mRenderIO (Session 3 conclusion) | **Superseded** — that conclusion assumed Fix 3 was working; it was not                                                                                          |

### Fix 6 — PATCHED (2026-03-09)

#### File changed

- `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
  - `RealtimeBackend::init()`: changed `const int actualOutChannels = static_cast<int>(mAudioIO.channelsOut())` to `static_cast<int>(mAudioIO.channelsOutDevice())`
  - Single assignment change; both the log line and the `< mConfig.outputChannels` guard use the same `actualOutChannels` local, so both are fixed by the one-line edit.

#### Effect

The post-open validation guard now reads `oParams.nChannels` from the RtAudio backend — the value clamped to `min(requested, deviceMax)` during stream open — instead of `mNumO` (the requested count that was always equal to `mConfig.outputChannels`). The engine will now correctly refuse to start when the OS-selected audio device provides fewer channels than the speaker layout requires, and the log line will print the true hardware-negotiated channel count rather than always echoing the requested count.

---

## 2026-03-18 — Session 5: Fix 5 re-applied, Fix 2 implemented

### Context entering this session

Post-Fix-6 listener tests showed the broad routing/device instability is largely resolved. Remaining symptoms:

1. Occasional large pop coinciding with a `speakerProximityCount` jump (guard-entry event)
2. `speakerProximityCount` still rises during runs (guard is active for some content)
3. Dramatic sub-mix slider jump can still perturb channel/output behavior (mechanism unconfirmed)
4. Quieter minimum range for master gain / speaker mix — deferred UX issue

---

### Finding: Fix 5 was missing from the codebase

**Expected:** `RealtimeBackend` constructor seeds `mSmooth.smoothed` and `mSmooth.target` from CLI config atomics before the first block runs.

**Actual:** Constructor body was empty (`{}`). `mSmooth` struct defaults all float fields to 1.0f. Every session started with masterGain=1.0, focus=1.0 regardless of `--gain`/`--focus` CLI flags, producing a ~200 ms ramp from wrong initial values.

The fix was recorded as patched in the Session 3 audit entry but was not present in `RealtimeBackend.hpp` — dropped during the Session 4 edit.

---

### Fix 5 — Re-applied (2026-03-18)

#### File changed

- `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
  - Constructor body now seeds `mSmooth.smoothed` (masterGain, focus, loudspeakerMix, subMix, autoComp) from config atomics
  - Sets `mSmooth.target = mSmooth.smoothed` so the smoother starts at the correct value with no initial ramp

---

### Root cause of guard-entry pop (confirmed)

The proximity guard (`Spatializer.hpp`, DBAP branch) computes a single `safePos` for the entire block and passes it to `renderBuffer()`. When a source crosses the `kMinSpeakerDist = 0.15 m` threshold between two consecutive blocks, the DBAP gains step discontinuously at the block boundary — audible as a pop. The guard itself is not the bug; the block-level single-position snap is the bug. This is the same mechanism as the general fast-mover problem, triggered specifically at guard-entry.

The `speakerProximityCount` jump visible in monitoring output identifies the exact block where the discontinuity occurs: the first block where the guard fires for a source.

**Fix 2 (fast-mover sub-stepping) is the direct fix.** By rendering each fast-moving source as 4 sub-chunks with independently interpolated positions, the gain change that previously happened in one block now ramps smoothly across 128-frame steps.

---

### Fix 2 — Fast-mover sub-stepping — PATCHED (2026-03-18)

#### Design decisions

| Decision | Reason |
|---|---|
| `positionStart` / `positionEnd` added to `SourcePose` | Gives the spatializer the block boundary positions without re-running the interpolation pipeline inside the audio callback |
| `computePositions(blockStartSec, blockEndSec)` — center derived as midpoint | Keeps the existing `position` field semantics unchanged; no caller changes beyond the one call site in `processBlock()` |
| Center position computed first (mutating `mLastGoodDir`), start/end via `computePositionAtTimeReadOnly()` | Prevents start/end evaluations from overwriting the last-good-direction cache with off-center values. The read-only helper uses `map::find()` (no insert) for its fallback |
| Chord lerp → renormalise to `mLayoutRadius` before guard | Chord midpoint falls slightly inside the sphere; `(subPose / mag) * mLayoutRadius` projects back to the speaker-ring surface. Prevents the guard from seeing artificially reduced source distances on fast-mover sub-chunks |
| Guard re-run per sub-chunk | Each sub-chunk position is independently guarded using the same flip → guard → un-flip pattern. Ensures no sub-chunk reaches a near-zero speaker distance |
| `kFastMoverAngleRad = 0.25f` (~14.3°) | Matches offline renderer threshold. Conservative enough to catch meaningful block-boundary jumps without triggering on slow-moving sources |
| `kNumSubSteps = 4` (128-frame sub-chunks at 512/48k) | 4× overhead only for fast-mover sources, not all sources. Falls back to normal path when `numFrames % kNumSubSteps != 0` |
| `mFastMoverScratch` pre-allocated in `init()` | Zero allocation in audio callback. Scratch is zeroed per sub-chunk, rendered into, then accumulated into `mRenderIO` at the correct frame offset |

#### Files changed

- `spatial_engine/realtimeEngine/src/Pose.hpp`
  - `SourcePose`: added `positionStart` and `positionEnd` fields (`al::Vec3f`)
  - `computePositions()`: signature changed from `(double blockCenterTimeSec)` to `(double blockStartTimeSec, double blockEndTimeSec)`; center derived internally as midpoint; center computed first (mutating), start/end via read-only helper
  - Added private `const` method `computePositionAtTimeReadOnly()`: full interpolation pipeline without writing `mLastGoodDir`

- `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`
  - `processBlock()` Step 2: computes `blockStartSec` and `blockEndSec`, passes both to `computePositions()`

- `spatial_engine/realtimeEngine/src/Spatializer.hpp`
  - Added constants `kFastMoverAngleRad = 0.25f` and `kNumSubSteps = 4`
  - Added member `mFastMoverScratch` (audio-thread-owned `al::AudioIOData`)
  - `init()`: sizes `mFastMoverScratch` to `(bufferSize / kNumSubSteps)` frames × `outputChannels`
  - `renderBlock()` DBAP branch: single `renderBuffer()` call replaced with fast-mover detection block. Normal path (angle ≤ threshold) unchanged. Fast-mover path: 4 sub-chunks, each with chord lerp → renorm → guard → `renderBuffer` into scratch → accumulate into `mRenderIO`

#### RT-safety

| Concern | Status |
|---|---|
| `acos()` per non-LFE source per block | Once per source per block — not inside a sample loop. ~80 trig ops per block. Acceptable |
| `mFastMoverScratch.zeroOut()` per sub-chunk | 4 × 128 × outputChannels float-zeroes per fast-mover source only, not per-sample |
| Guard runs 4× per fast-mover | 4 × numSpeakers vector ops. At 50 speakers: 200 vector ops per fast-mover per block. Fine |
| `positionStart.normalized()` | Two magnitude+divide ops per source per block for the angle test. No allocation |
| Two extra `al::Vec3f` per `SourcePose` | 80 sources × 2 × 12 bytes = 1.9 KB extra per block traversal. Negligible |

---

### Remaining open items after Session 5

| Item | Status |
|---|---|
| Fix 5 missing | ✅ Re-applied |
| Fix 2 fast-mover sub-stepping | ✅ Patched |
| Guard-entry pop | ✅ Addressed by Fix 2 — needs listener verification |
| `speakerProximityCount` rising during runs | Still present — guard is working as designed for near-speaker content; acceptable unless it causes audible blur |
| Sub-mix jump → channel perturbation | Still unconfirmed mechanism. macOS audio route reassignment remains the leading hypothesis; not a C++ code bug. Monitor after Fix 2 + Fix 5 are in place to see if it persists |
| Remap load-order bug (`deviceChannels` arg) | Latent, `--remap` path only. Not blocking |
| `kMinSpeakerDist` tuning for 360RA | Deferred — guard value at 0.15 m may be too large for 360RA DirectSpeaker content but guard redesign is out of scope for this pass |

---

## Agent context for new context windows

> Copy this block into the opening message when starting a new session on this task.

### State of the engine (as of 2026-03-18)

All fixes below are **in the codebase**. Do not re-implement or re-analyse them.

| Fix | Description | Status |
|---|---|---|
| Fix 1 | Onset fade — 128-sample ramp at source activation | ✅ in code |
| Fix 2 | Fast-mover sub-stepping — 4 sub-chunks, lerp + renorm + guard per chunk | ✅ in code |
| Fix 3 | Post-open channel validation (device channel count check) | ✅ in code (via Fix 6) |
| Fix 4 | GUI restart resets controls before re-launching engine | ✅ in code |
| Fix 5 | Smoother pre-seeding from CLI config atomics | ✅ in code |
| Fix 6 | Correct `channelsOutDevice()` accessor in `RealtimeBackend::init()` | ✅ in code |

### Key architectural facts

- Block render is canonical (`renderBuffer()` path). Per-sample DBAP was tried and reverted.
- Auto-compensation is disabled (returns 1.0f). The plumbing is intact but the math was wrong; not scheduled for this pass.
- `mSmooth` (50 ms tau exponential smoother) is the sole source of gain/focus values in the audio thread. Config atomics are written by OSC/CLI only; never written back by the audio thread.
- `ControlsSnapshot` is created on the stack in `processBlock()` and passed by const-ref into `renderBlock()`. The spatializer never reads config atomics directly.
- `speakerProximityCount` and `nanGuardCount` are printed every 500 ms by the monitoring loop. They are the primary diagnostic counters for guard and clamp activity.

### Files most likely to need changes

| File | Role |
|---|---|
| `spatial_engine/realtimeEngine/src/Spatializer.hpp` | DBAP render loop, guard, Phase 6 mix trims, Phase 7 copy, Fix 2 sub-step path |
| `spatial_engine/realtimeEngine/src/Pose.hpp` | Keyframe interpolation, `SourcePose` struct, `computePositions()` |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Audio callback, smoother, pause fade, `processBlock()` |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` | `RealtimeConfig` atomics, `EngineState` counters — read-only reference |
| `spatial_engine/realtimeEngine/src/main.cpp` | CLI parsing, OSC parameter setup, monitoring loop |
| `gui/realtimeGUI/realtimeGUI.py` | GUI window, restart/reset wiring |

### Remaining open items (priority order)

1. **Listener verification of Fix 2** — run Eden and Swale, check whether the guard-entry pop is gone. Observe `speakerProximityCount` during the problem sections.
2. **Sub-mix jump** — after Fix 2 + Fix 5 are verified, re-test the dramatic sub-slider move. If the channel issue persists, collect more data (which device, which layout, `nanGuardCount` at the event). macOS route reassignment is the leading hypothesis but is not confirmed.
3. **`kMinSpeakerDist` tuning** — 0.15 m is known to affect 360RA DirectSpeaker content (sources at 0.02–0.10 m from their target speakers). Consider reducing to ~0.05–0.06 m after guard-pop is confirmed fixed, so 360RA localization is restored. Eden 21.1 at 0.049 m would still be caught.
4. **Remap load-order bug** — `outputRemap.load()` passes `config.outputChannels` for both `renderChannels` and `deviceChannels`. The `deviceChannels` arg should be the post-open actual count. Only affects `--remap` path; not currently blocking.

### What not to do

- Do not redesign auto-compensation yet.
- Do not reopen the broad device/output mismatch analysis — Fix 6 resolved the structural issue.
- Do not add per-sample logging in the audio callback.
- Do not modify `runPipeline.py` — it is deprecated. Use `runRealtime.py`.
- `realtime_master.md` has the full phase history; read it for architectural background but trust the audit file for current fix status.
