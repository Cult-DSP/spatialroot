# Compensation and Gain Agent — Phase 6# Compensation and Gain Agent — Phase 6# Compensation and Gain Agent

> **Implementation Status: ✅ COMPLETE (Phase 6, Feb 25 2026)**> **Status: Not started**## Overview

> Implemented inside `spatial_engine/realtimeEngine/src/Spatializer.hpp`.

> No separate file — integrated directly into the render pipeline.> **Last updated:** February 25, 2026

> See `realtime_master.md` Phase 6 Completion Log for details.

> CLI: `--speaker_mix <dB>`, `--sub_mix <dB>`, `--auto_compensation`The **Compensation and Gain Agent** manages the overall gain structure of the spatial audio engine and applies any compensations needed to achieve a natural and consistent listening experience. This includes handling distance-based attenuation of sounds (making sources quieter as they get further away, beyond the geometric panning of the Spatializer), global gain adjustments (master volume, per-source volume trims), and any calibration or equalization that might be necessary for fidelity (such as compensating for speaker sensitivities or applying an overall volume curve).

---## Problem Statement

## ProblemThis agent acts as a refinement layer on top of the raw spatial mix. After the Spatializer (and LFE routing) determine how audio is distributed, the Compensation and Gain Agent ensures the levels are correct for perception and technical limits. It can be thought of as the stage where “physics” and “user preference” are applied: physics in terms of distance attenuation and possibly air absorption, and user preference in terms of volume control or scene loudness normalization.

When DBAP `focus` increases, energy concentrates on fewer speakers. Total powerWhen DBAP `focus` increases, energy concentrates on fewer speakers near each

across the main array **drops**. But LFE routing uses `masterGain * 0.95 / numSubs`

independently — it is **not** affected by focus. This creates a growingsource. The total summed power across the main speaker array **drops**, making## Responsibilities

sub-to-mains imbalance at higher focus values.

the loudspeaker mix quieter. However, the subwoofer level is computed

Different layouts, source counts, and focus settings all produce different

loudness balances that the user needs to trim independently.independently (`masterGain * 0.95 / numSubs`) and is **not affected by focus**.- **Distance Attenuation:** Apply a reduction in gain to sources based on their distance from the listener or center of the speaker array. Commonly, this might follow an inverse-square law (6 dB drop per doubling of distance) or a custom curve (sometimes clamped to avoid extremely low volumes). If the Spatializer’s DBAP doesn’t inherently reduce overall volume with distance (it typically keeps constant power regardless of distance to speakers, which means a far sound would still be loud unless explicitly handled), this agent ensures far sounds are appropriately quieter. This could be implemented by calculating a distance factor for each source and multiplying the source’s gain or audio signal by that factor.

---This creates a disproportionate sub-to-mains balance at higher focus values —- **Per-Source Gain Control:** Allow each source to have an independent gain multiplier (for content mixing purposes or user control). For instance, background music might be set to 0.5 (–6 dB) gain while a foreground effect is 1.0. The agent can store and apply these values so that changes (e.g., user turning one source up/down) take effect smoothly.

## Solution: Two Mix Sliders + Auto-Compensation Togglethe sub sounds too loud relative to the main speakers.- **Master Volume and Mute:** Provide a single master gain that scales the entire output (all channels) for volume control. This could be used for a user volume knob or for fading out the scene. Also handle global mute or pause by setting this gain to 0 or handling ramping to silence if needed.

### Loudspeaker Mix Slider- **Loudness Compensation / Calibration:** If the system has calibration data (like certain speakers are known to be a bit quiet, or a certain frequency range needs boost for perceptual reasons), this agent might incorporate simple compensation. For example, if the room or system requires a +3 dB boost to the LFE for flat response, apply that gain here (or instruct LFE Router to do so). Another example is equal-loudness compensation: at very low volumes, bass frequencies are harder to hear, some systems apply a “loudness” curve boost when volume is low. These are more advanced and might not be implemented initially, but the agent’s scope covers them conceptually.

- **Range:** ±10 dB (linear: ~0.316× – ~3.162×), **default:** 0 dB (1.0×)

- Applied to **main speaker channels** in `mRenderIO` **after** DBAP, **before** copy to `AudioIO`More generally, different speaker layouts, source counts, and focus settings- **Prevent Clipping (Headroom Management):** Monitor or estimate the combined levels to avoid clipping. If many sources play loud sounds together, the sum might exceed 0 dBFS. The agent could include a safety limiter or at least a margin by reducing overall gain slightly. Alternatively, it might just warn in logs if levels are too high. Implementing a full compressor/limiter might be complex, but a simple strategy is to keep a master gain at 0.9 (–1 dB) to provide a bit of headroom. For future improvement, an automatic gain control could be considered.

- Subwoofer channels are **not** affected

- `masterGain` is applied upstream (per-source × masterGain → DBAP); `loudspeakerMix` is an independent post-DBAP trim:produce different loudness balances between mains and sub that the user needs

  `DBAP_output × loudspeakerMix`

to be able to trim independently.## Relevant Internal Files

### Sub Mix Slider

- **Range:** ±10 dB, **default:** 0 dB## Solution: Two Mix Sliders + Auto-Compensation Toggle- **`GainCompensation.cpp` / `.h`:** This module would hold the logic for computing and applying these gain factors. Likely functions to update source gains (when positions change or user adjustment) and to apply those gains to audio buffers in real-time.

- Applied to **subwoofer channels** in `mRenderIO` **after** LFE routing, **before** copy to `AudioIO`

- Main speaker channels are **not** affected- **`SpatialRenderer.cpp` / audio pipeline code:** Depending on implementation, the compensation might be applied inside the spatial mix loop or as a separate loop after mixing. For example, one might incorporate distance attenuation into the Spatializer’s gain calculation for efficiency (multiplying it in with the panning gains). Or one might have an extra step after spatialization where each source’s contributions are scaled. If it’s separate, SpatialRenderer might call a function from GainCompensation to adjust the mixed buffers or source buffers.

- LFE routing already applies `masterGain * 0.95 / numSubs`; `subMix` trims on top:

  `LFE_routed_output × subMix`### Loudspeaker Mix Slider- **`SceneState.h`:** The Pose and Control’s data structure might include fields for each source like `userGain` or `attenuation`. Those fields would be set by UI/Control and read by this agent to know what values to apply.

### Focus Auto-Compensation Toggle- **`RENDERING.md`:** The rendering document should outline how distance and gain are handled. If it says, for example, “the engine applies a 1/distance rolloff on sources”, that is this agent’s responsibility. Ensure the doc aligns with what’s implemented.

- **Default:** OFF

- When ON: focus changes automatically update `loudspeakerMix` to keep perceived main loudness approximately constant- **Range:** ±10 dB (linear multiplier: ~0.316× to ~3.162×)

- Sub slider is **not** touched by auto-compensation

- User can manually override after the auto value is set- **Default:** 0 dB (1.0× — unity, no change)## Hard Real-Time Constraints

- **Formula:** Compute DBAP gain sum for a front reference position (`0, layoutRadius, 0`) at current `focus` → `gainSum`. Repeat at `focus=0` → `refGainSum`. Set `loudspeakerMix = refGainSum / gainSum`

- Runs once on the **control/main thread** when focus changes — not per audio block- **Application point:** Applied to **main speaker channels** in the internal

--- render buffer (`mRenderIO`) **after** DBAP rendering, **before** the copyApplying gains is relatively simple math, but because it touches potentially every sample for every source or channel, it’s part of the real-time path:

## Signal Chain to the real AudioIO output. Subwoofer channels are **not** affected.

```- **Interaction with master gain:** `masterGain` is applied upstream (source- **Lightweight Operations:** Gain adjustments are multiplications (and occasional additions if doing something like loudness EQ). These are very fast operations. The agent must ensure it doesn’t introduce anything heavier. No locks or memory allocs here either. Gains should be computed outside the audio loop if possible (like calculate a coefficient once per control update, then just multiply in audio loop).

Source audio

    │  audio × `masterGain` → DBAP). `loudspeakerMix` is an independent post-DBAP- **Smooth Transitions:** When changing gains (especially dynamically via user control or distance changes), do so in a way that avoids clicks. This means implementing gain interpolation or ramping on the audio thread. For example, if a source’s gain changes from 1.0 to 0.5, instead of an instant jump (which could click if the waveform is mid-cycle at a non-zero crossing), ramp it over a small number of samples (like over 64 samples) to smooth it. This interpolation must be done carefully in real-time: it’s essentially a linear ramp multiply, which is minor overhead. Plan for this in the code design.

    ├─► non-LFE:  × masterGain  ──►  al::Dbap::renderBuffer()

    │                                        │  trim. Effective main speaker level:- **Real-Time Gain Computation:** Calculating the distance attenuation factor might involve a sqrt or division if doing it in real-time. However, this can be done per source per frame (not per sample) which is negligible. Even better, when Pose updates positions, it could pre-calculate a new gain factor and just store it for the audio thread to use. So the audio thread might just read a ready-to-use `distanceGain` per source each frame. The key is to not recalc it unnecessarily within the inner sample loop.

    │                               main speaker channels in mRenderIO

    │                                        │  `DBAP_output × loudspeakerMix   (masterGain already baked in)`- **Limiters/Compression (if any):** A simple static gain is fine, but if in the future a dynamic compressor is added to prevent clipping, ensure it’s a low-complexity one (like a single-pole limiter, which is a couple multiplications per sample). Only consider this if needed, as complex multi-band compression would be too heavy for real-time without a lot of headroom.

    │                                  × loudspeakerMix

    │                                        │- **Thread Safety:** The gain values themselves may be updated by the control thread (when user changes volume or a new distance calculated). Use atomic or double-buffer for those values. For example, each source’s gain could be an atomic float that the audio thread reads. Or better, the Pose/Control agent updates a separate structure and signals the audio thread to copy new values between blocks. The Threading agent’s guidelines apply: we cannot have a lock around the gains in the audio loop, so it must be lock-free updates (like atomic exchange of a pointer to an array of gains, etc.).

    │                               [copy to real AudioIO output]

    │### Sub Mix Slider

    └─► LFE:  × masterGain × 0.95/numSubs  ──►  subwoofer channels in mRenderIO

                                                          │## Interaction with Other Agents

                                                    × subMix

                                                          │- **Range:** ±10 dB (linear multiplier: ~0.316× to ~3.162×)

                                               [copy to real AudioIO output]

```````- **Default:** 0 dB (1.0× — unity, no change)- **Spatializer (DBAP) Agent:** There is a close interaction here. Some of the responsibilities can be shared or moved between Spatializer and Compensation:



---- **Application point:** Applied to **subwoofer channels** in `mRenderIO` - If distance attenuation is done in Compensation, the Spatializer will output a mix assuming all sources are at nominal level, then the Compensation agent would scale each source’s contribution or final output accordingly.



## Implementation  **after** LFE routing, **before** the copy to real AudioIO output. - Alternatively, Spatializer could incorporate the distance attenuation in its gain calcs (multiplying distance factor into the gains it uses for panning). This is efficient and straightforward. In that case, the Compensation agent might just provide the distance factor to Spatializer. For clarity, let’s say the Compensation agent computes per-source distanceGain and the Spatializer multiplies by that each frame. This hybrid approach avoids an extra loop.



### New fields in `RealtimeConfig` (`RealtimeTypes.hpp`)  Main speaker channels are **not** affected. - For user gain, similarly, Spatializer could directly incorporate it. Or if Spatializer is unaware, Compensation could adjust the mixed output after the fact, but per source after mixing is tricky (once sources are summed, you can’t separate them to apply individual gains). So it’s better to integrate per-source gain in the mixing step.



```cpp- **Interaction with master gain:** LFE routing already applies - Therefore, expect that this agent works closely with Spatializer code: either by pre-adjusting source audio buffers or by adjusting the gain coefficients used in Spatializer.

std::atomic<float> loudspeakerMix{1.0f};      // post-DBAP main-channel trim (±10 dB)

std::atomic<float> subMix{1.0f};              // post-DBAP sub-channel trim  (±10 dB)  `masterGain * 0.95 / numSubs`. `subMix` is an independent trim on top:- **Pose and Control Agent:** Provides the raw data for distance (positions) and any user-driven gain changes. Pose updates positions; based on those, either Pose agent itself could compute new distance attenuation and store it (if formula is simple) or leave to this agent. Possibly, Compensation agent might register as an observer of position changes. Regardless, when a source moves, this agent needs to know the new distance. If using double-buffered scene state with distances, it can compute gain quickly.

std::atomic<bool>  focusAutoCompensation{false};

```  `LFE_routed_output × subMix` - Also, if the user via GUI changes a source’s volume or mutes it, Pose/Control will update a field. The Compensation agent will then apply that (maybe by updating the internal gain factor for that source).

  - There might also be global toggles like “turn off distance attenuation” for artistic reasons. That would likely be a flag that Pose/Control can set (through GUI), which Compensation agent will respect (e.g., if off, always use 1.0 for distance factor).

### Changes to `Spatializer.hpp` — `renderBlock()`

### Focus Auto-Compensation Toggle- **Streaming Agent:** Not directly related, but if a source has metadata about loudness (like LUFS value or such), Streaming could pass that info and Compensation could auto-adjust gain to normalize loudness between sources. This is a possible extension: auto-leveling different streams. If implemented, Streaming agent might set an initial gain for a source when loading it (to match a reference loudness), and Compensation agent would apply that as a base gain.

After DBAP + LFE render, before copy-to-output:

- **LFE Router Agent:** Compensation might adjust the LFE channel gain. For example, if calibration says “LFE +10dB”, Compensation could multiply the LFE channel by that factor at output. If we didn’t handle it in LFE Router, we can do it here by identifying the LFE channel in the mix and scaling it. The Output Remap/Backend might also incorporate fixed gain if needed, but logically it fits here as part of system calibration.

```cpp

const float spkMix = mConfig.loudspeakerMix.load(std::memory_order_relaxed);- **Default:** OFF- **Output Remap Agent:** Typically, Output Remap is just structural, but if it doesn’t handle per-channel trims, Compensation agent might do final per-channel adjustments. For instance, if the room calibration says the left surround is 2dB hotter than others, Compensation can apply a 0.8 factor to that channel’s samples. This would be done just before output. Alternatively, these could be baked into the mapping matrix in Output Remap. Decision needed: If using a matrix in Output Remap for downmix, that matrix can incorporate channel trims. If not, Compensation can multiply the final buffer per channel. Either approach works; we should just document clearly if any calibration is planned.

const float lfeMix = mConfig.subMix.load(std::memory_order_relaxed);

- **Behavior when ON:** When `focus` changes, the engine automatically- **Backend Adapter Agent:** If the user changes the system volume via OS or some external control, it might come through the backend (like an API callback). That could then be passed to Compensation to adjust master gain. Usually our engine’s master gain is separate from OS volume, but in integrated systems they might link. Not a big point unless needed.

if (spkMix != 1.0f) {

    for (unsigned int ch = 0; ch < renderChannels; ++ch) {  computes the ideal compensation and **updates the loudspeaker mix slider**- **GUI Agent:** The GUI provides interfaces to control volumes (master slider, per-source sliders perhaps) and toggle compensations. The Compensation agent will be on the receiving end of those controls. It should expose functions or use the control interface to set values. For example, `Compensation::setMasterVolume(0.8)` or `Compensation::setSourceGain(sourceId, 1.2)`. The GUI likely won’t call these directly in the audio thread context, but via the Pose/Control or a safe messaging system. The agent should also report current values if needed (for showing on UI, though UI can store the last set values itself).

        if (isSubwooferChannel(ch)) continue;

        float* buf = mRenderIO.outBuffer(ch);  to keep perceived main speaker loudness approximately constant.

        for (unsigned int f = 0; f < numFrames; ++f) buf[f] *= spkMix;

    }- **User override:** The slider is **not locked**. The user can manually## Data Structures & Interfaces

}

if (lfeMix != 1.0f) {  adjust it after the auto value is set. The auto value only re-applies

    for (int subCh : mSubwooferChannels) {

        float* buf = mRenderIO.outBuffer(subCh);  when `focus` changes again.- **Gain Values Storage:** An array or vector `sourceGains[numSources]` that stores the current gain multiplier for each source. This might be broken down into components:

        for (unsigned int f = 0; f < numFrames; ++f) buf[f] *= lfeMix;

    }- **Sub slider:** Auto-compensation does **not** touch `subMix`. - `userGain` (from UI settings),

}

```- **Formula:** Compute DBAP's total gain sum for a reference source position - `distanceGain` (from attenuation calc),



`isSubwooferChannel(ch)` — private helper, linear scan of `mSubwooferChannels`.  (front: `0, layoutRadius, 0`) at the current `focus` → `gainSum`. Repeat - and maybe other factors (like `loudnessGain` if normalizing).

Unity guard (`!= 1.0f`) makes the no-op case zero-cost.

  at `focus = 0` (uniform distribution) → `refGainSum`. Set: We could combine these into one final multiplier per source for efficiency, but conceptually keep track separately for clarity.

### `computeFocusCompensation()` — `Spatializer.hpp`

  `loudspeakerMix = refGainSum / gainSum`- **Master Gain:** A single float value for global volume. Possibly also a boolean for mute. This would be applied to all audio before output (e.g., multiply all channels by master gain in the final stage).

Called on the **main thread only**, not RT-safe (allocates a temp `AudioIOData`).

Renders a unit impulse at `(0, layoutRadius, 0)`, measures power, writes result  This is computed once on the control/main thread when `focus` changes —- **Distance Attenuation Model:** A function or table for converting distance to gain. Could be a simple function: `gain = 1 / (1 + rolloff * (d - dRef))` or a classic inverse-square: `min(1, (dRef / max(d, dRef))^2)` for example. Possibly parameters like a minimum distance (inside which no further gain increase) and a maximum distance (beyond which it's very low or silent). The agent might store such parameters (from config: e.g., rolloff factor, min distance).

to `mConfig.loudspeakerMix`. See Phase 8 threading notes for usage rules.

  not per audio block.- **Interfaces:**

### CLI flags — `main.cpp`  - `updateSourceDistance(id, distance)`: Called (perhaps by Pose/Control) when a source’s distance changes, to recalc that source’s distanceGain. Or the agent might itself compute the distance if given positions.



```## Signal Chain - `setSourceUserGain(id, gain)`: Adjusts the user-controlled gain for a source.

--speaker_mix <dB>    post-DBAP main trim  (default: 0, range: ±10)

--sub_mix <dB>        post-DBAP sub trim   (default: 0, range: ±10)- `setMasterGain(value)`, `setMute(on/off)`.

--auto_compensation   enable focus auto-compensation (default: off)

`````` - Internally, a method like`applyGainsToBuffer(sourceId, buffer)` might be used if this agent directly modifies source audio before mixing. But since mixing is happening in Spatializer, likely Spatializer will fetch the source’s gain from this agent’s data. Could be done by giving Spatializer a pointer or reference to the gains array (read-only in audio thread).



dB → linear: `linear = powf(10.0f, dB / 20.0f)`Source audio - Alternatively, `Compensation::applyMasterGainToOutput(float* interleavedBuffer, int numSamples, int numChannels)` could be used at final stage to scale everything by master volume (and maybe per-channel trims).



---    │- **Ramping Mechanism:** A small struct or variable per source for ramping, e.g., `float currentGain`, `float targetGain`, and some counter or increment per sample. The agent could compute an increment when a change is requested (like targetGain = new combined gain, then increment = (targetGain - currentGain) / rampSamples, and each audio sample or frame applies that). Another simpler approach is ramp per frame for minor changes (if frame size is small, stepping each frame is fine). But if large changes, better to ramp within frame to avoid a sudden jump at frame boundary.



## Real-Time Safety    ├─► non-LFE:  × masterGain  ──►  al::Dbap::renderBuffer()- **Clipping Prevention:** Could store a value for recent peak level or integrate a limiter state. This gets complicated, but a simple flag or counter if clipping occurred can be placed for debugging. Fully preventing clipping in real-time might involve dynamic reduction – which is like a limiter with state (attack/release), which if implemented should have state variables like a gain reduction factor that changes over time. This might be too advanced for now, but leaving structure to accommodate it later could be wise.



| Concern | Resolution |    │                                        │

|---|---|

| Atomic reads in callback | One `relaxed` load per block per slider — negligible |    │                               main speaker channels in mRenderIO## Development and Documentation Notes

| Per-channel multiply | O(channels × bufferSize) — trivially cheap |

| Unity guard | `!= 1.0f` skips loop entirely at defaults |    │                                        │

| `computeFocusCompensation()` | Main thread only, writes one atomic float |

| No locks, no allocation | All buffers pre-allocated; sliders are atomics |    │                                  × loudspeakerMixAs the Compensation and Gain Agent is developed:



---    │                                        │



## GUI Controls (Phase 9)    │                                        ▼- **Clarify Division of Work:** Decide exactly what is handled here versus in Spatializer or other parts. Document that clearly. For example: “Distance attenuation is calculated here and provided to Spatializer to apply in its mixing” or “We apply distance attenuation by scaling the source audio pre-mix.” The approach chosen needs to be consistently described. Update Spatializer’s doc accordingly if it changes.



| Control | Type | Range | Default |    │                               [copy to real AudioIO output]- **Formulas and Parameters:** Write down the chosen attenuation formula and any constants (like reference distance, max distance, rolloff exponent). This is important for consistency and tuning. If the formula is derived from some standard (like Unreal Engine uses a certain model), note that. Also, document the default master volume (likely 1.0) and any calibration offsets.

|---|---|---|---|

| Loudspeaker Mix | Slider + dB readout | −10 to +10 dB | 0 dB |    │- **Keep Master Doc Updated:** If this agent introduces a pipeline step (like a post-mix gain stage), ensure the master overview (realtime_master.md) mentions it. In our master doc, we included Compensation in the audio callback description; make sure it aligns with actual implementation plan (e.g., if integrated with Spatializer, it’s not a separate loop).

| Sub Mix | Slider + dB readout | −10 to +10 dB | 0 dB |

| Focus Auto-Compensation | Toggle | on / off | off |    └─► LFE:  × masterGain × 0.95/numSubs  ──►  subwoofer channels in mRenderIO- **Testing Plan:** Plan subjective and objective tests:



All write to `RealtimeConfig` atomics — same pattern as `masterGain`.                                                          │  - Subjective: move a source away and confirm it gets quieter smoothly, adjust a volume slider and check for no clicking and correct level change.



---                                                    × subMix  - Objective: measure output levels at various distances to ensure they match expected drop (like at 2x distance, ~ -6 dB if using inverse square).



## Explicit Non-Goals for v1                                                          │  - Document any deviations or if tuning needed (maybe initial formula felt too quiet, so you adjust rolloff).



| Feature | Reason deferred |                                                          ▼- **Integration with UI:** Ensure that any external control (like a UI slider for master volume) is accounted for. Document how quickly changes apply (immediately, ramped, etc.). If there’s any command interface, include it.

|---|---|

| Distance attenuation | Not in offline renderer |                                               [copy to real AudioIO output]- **Performance Considerations:** Even though gain is cheap, keep an eye out if doing per-sample ramps or lots of per-source operations. It’s likely fine, but note the complexity: e.g., “We multiply each sample by up to two factors (distance and user gain combined), which is negligible overhead.” If implementing a limiter, note its cost if known.

| Per-source gain trims | Not in offline renderer |

| Dynamic compression / limiting | Future work |````- **Update RENDERING.md:** That doc should reflect how loudness is handled. Possibly add a section “Distance attenuation: yes/no, formula; Master volume: yes; etc.” Make sure it’s consistent with what we do.

| Per-channel speaker calibration | Future work |

| Gain ramping / zipper-noise suppression | Sliders change infrequently |- **Future Enhancements:** If not implementing things like loudness EQ or advanced calibration now, note them as future work. E.g., “System could incorporate Fletcher-Munson loudness compensation at low volumes – not implemented in v1.” Or “No dynamic compression is currently used; careful content mixing is assumed to avoid clipping.” This sets expectations for anyone evaluating the engine’s capabilities later.


## Implementation

Through careful management of levels by the Compensation and Gain Agent, the engine will produce audio that not only is spatially correct but also perceptually balanced and free of unwanted volume issues, enhancing the overall quality of the experience.

### New fields in `RealtimeConfig` (`RealtimeTypes.hpp`)

```cpp
// Phase 6 — post-DBAP mix trims (±10 dB, stored as linear multiplier).
// Atomic: GUI / control thread may change these at runtime.
std::atomic<float> loudspeakerMix{1.0f};     // Main speaker trim (default 0 dB)
std::atomic<float> subMix{1.0f};             // Subwoofer trim   (default 0 dB)
// Focus auto-compensation: when true, dbapFocus changes auto-update loudspeakerMix.
std::atomic<bool>  focusAutoCompensation{false};
```````

### Changes to `Spatializer.hpp`

**In `renderBlock()`**, after the existing DBAP + LFE render loop and before
the copy-to-output loop, insert two passes over the internal render buffer:

```cpp
// Phase 6 — apply mix trims to mRenderIO before copying to real output
const float spkMix = mConfig.loudspeakerMix.load(std::memory_order_relaxed);
const float lfeMix = mConfig.subMix.load(std::memory_order_relaxed);

if (spkMix != 1.0f) {
    for (unsigned int ch = 0; ch < renderChannels; ++ch) {
        if (isSubwooferChannel(ch)) continue;   // skip subs
        float* buf = mRenderIO.outBuffer(ch);
        for (unsigned int f = 0; f < numFrames; ++f) buf[f] *= spkMix;
    }
}
if (lfeMix != 1.0f) {
    for (int subCh : mSubwooferChannels) {
        float* buf = mRenderIO.outBuffer(subCh);
        for (unsigned int f = 0; f < numFrames; ++f) buf[f] *= lfeMix;
    }
}
```

`isSubwooferChannel(ch)` is a small helper that checks `mSubwooferChannels`.
The unity guard (`!= 1.0f`) makes the no-op case zero-cost.

**New method `computeFocusCompensation()`** (called on control/main thread,
not audio thread, when `focus` changes and auto-compensation is on):

```cpp
float computeFocusCompensation() {
    // Render a unit impulse at a front reference position with current focus
    // and again at focus=0. Return the ratio of gain sums.
    // ...one-shot DBAP compute, ~numSpeakers multiplies, no audio thread impact.
    // Writes result to mConfig.loudspeakerMix.
}
```

### New CLI flags (`main.cpp`)

```
--speaker_mix <dB>    Loudspeaker mix trim in dB  (default: 0, range: ±10)
--sub_mix <dB>        Subwoofer mix trim in dB     (default: 0, range: ±10)
--auto_compensation   Enable focus auto-compensation (default: off)
```

dB → linear at parse time: `linear = powf(10.0f, dB / 20.0f)`

### GUI controls (Phase 9)

| Control                 | Type                           | Range         | Default |
| ----------------------- | ------------------------------ | ------------- | ------- |
| Loudspeaker Mix         | Horizontal slider + dB readout | −10 to +10 dB | 0 dB    |
| Sub Mix                 | Horizontal slider + dB readout | −10 to +10 dB | 0 dB    |
| Focus Auto-Compensation | Toggle switch                  | on / off      | off     |

All controls write to `RealtimeConfig` atomics — same thread-safety pattern
as `masterGain`. When auto-compensation is ON and focus changes, the
loudspeaker slider updates its displayed value to reflect the new computed
setting, but remains interactive.

## Real-Time Safety

| Concern                      | Resolution                                                |
| ---------------------------- | --------------------------------------------------------- |
| Atomic reads in callback     | One `relaxed` load per block per slider — negligible      |
| Per-channel multiply         | O(numChannels × bufferSize) — trivially cheap             |
| Unity guard                  | `!= 1.0f` check skips loop entirely at defaults           |
| `computeFocusCompensation()` | Runs on control/main thread only, writes one atomic float |
| No locks, no allocation      | All buffers pre-allocated; sliders are atomics            |

## Testing Plan

1. **Loudspeaker trim:** `--speaker_mix 6` → mains +6 dB, sub unchanged.
2. **Sub trim:** `--sub_mix -6` → sub −6 dB, mains unchanged.
3. **Focus sweep (auto ON):** sweep `focus` 0 → 3; loudspeaker slider should
   increase; perceived main loudness should stay roughly constant.
4. **Master gain interaction:** changing `--gain` scales both mains and sub
   uniformly; mix sliders still apply on top.
5. **No regression:** both sliders at 0 dB, auto off → output bit-identical
   to pre-Phase 6.

## Explicit Non-Goals for v1

| Feature                                 | Reason deferred                                   |
| --------------------------------------- | ------------------------------------------------- |
| Distance attenuation                    | Not in offline renderer; not needed for parity    |
| Per-source gain trims                   | Not in offline renderer                           |
| Dynamic compression / limiting          | Future work                                       |
| Per-channel speaker calibration         | Future work; layout-dependent                     |
| Gain ramping / zipper-noise suppression | Sliders change infrequently; add later if audible |
