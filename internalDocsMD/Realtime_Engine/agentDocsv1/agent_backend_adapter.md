````markdown
# Backend Adapter Agent

> **Implementation Status: ✅ COMPLETE (Phase 1, Feb 24 2026 — Phase 10 polish Feb 27 2026)**
> Implemented in `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`.
> See `realtime_master.md` Phase 1 Completion Log for details.
> Uses AlloLib AudioIO. Static C-style callback dispatches to `processBlock()`.
> Full agent-wiring via `setStreaming()` / `setPose()` / `setSpatializer()`.
> Thread ownership and safety contracts documented in Phase 8 (see `RealtimeTypes.hpp`).

## Overview

The **Backend Adapter Agent** serves as the abstraction layer between the audio engine’s processing pipeline and the operating system or hardware audio interface. Its job is to interface with whichever audio API or driver is in use (such as ALSA/Jack on Linux, CoreAudio on macOS, ASIO/WASAPI on Windows, or a cross-platform library like PortAudio or RtAudio) and feed the engine’s output into it in real time. It hides the specifics of the audio backend from the rest of the engine, presenting a uniform way to start, stop, and configure audio playback.

In practical terms, this agent encapsulates things like:

- Initializing the audio device (choosing output device, sample rate, buffer size, channel configuration).
- Providing a callback or loop where the Spatializer (and related processing) is called each audio frame to produce sound.
- Handling any device errors or reinitializations (e.g., device lost, or format not supported).
- Potentially abstracting differences between backends (for example, one could have an implementation for PortAudio and another for JACK, each hidden behind the same interface).

## Responsibilities

- **Audio Device Initialization:** Discover and open the appropriate audio output device with the required parameters (channel count, sample rate, buffer size). If the engine is to be cross-platform or backend-agnostic, the adapter may have logic to choose the best API or use one that is available. For instance, try WASAPI exclusive mode, if not available, fall back to DirectSound, etc., without other agents needing to know.
- **Callback Management:** Set up the mechanism for continuous audio data delivery. In many APIs, this means registering an audio callback function that the system will call periodically to request new samples. The Backend Adapter will connect that callback to the engine’s Spatializer (or master processing function).
  - If using a pull model (some APIs might have blocking writes), the Backend Adapter might create its own thread that repeatedly asks the engine for audio and writes it out.
  - In either case, ensure the callback or loop calls the engine in a timely manner and at the correct rate.
- **Buffer Format Conversion (if necessary):** Ensure the data passed to the OS is in the format it expects. If the Output Remap agent already produced the correct format (interleaved vs planar, correct sample type), the adapter can often just pass it through. If not, the adapter might perform final conversion. For example, PortAudio expects an interleaved buffer in the callback if using that mode; if our engine keeps planar floats, the adapter could interleave them (but we planned to do that in Output Remap).
- **Latency and Buffer Tuning:** Work with the chosen API to set an appropriate buffer size for low latency without underruns. The adapter might allow configuration of this (like a user sets 256-sample buffers). It should also possibly query the actual latency and expose it if needed (for sync purposes, though we might not need that in engine logic, it's good to note).
- **Error Handling and Recovery:** If the audio device stops or an underrun occurs (in some APIs this triggers callbacks or flags), the adapter should log or handle it (e.g., try to restart the stream). In serious errors, it might propagate an event to the GUI (like “audio device error”).
- **Backend Selection (if multiple available):** Provide an interface to select among different backends or devices. For instance, the GUI might list audio devices and when the user selects one, the Backend Adapter will stop current stream and reinit with the new device. Internally, if supporting multiple backends (like PortAudio vs JACK), perhaps compiled as separate modules, the adapter could decide based on config or runtime which one to use.

## Relevant Internal Files

- **`AudioBackendAdapter.cpp` / `.h`:** The main implementation of this agent. It might contain logic to choose and initialize the API. Possibly a base class `AudioBackendAdapter` with virtual methods if multiple implementations exist, but likely a single unified class for now.
- **`AudioCallback.h`:** Definition of the static or member function that will be used as the audio callback. For example, in PortAudio you set a C function pointer; we might have a static function in this file that calls our engine’s processing routine.
- **Platform/Backend Specific files:** If we integrate with a specific API, e.g., `PortAudioBackend.cpp` or `WASAPIDriver.cpp`. However, if using a cross-platform library, that might not be needed; we just call PortAudio or RtAudio directly in the adapter.
- **Configuration files or settings:** Possibly a config that stores default device or desired latency. The adapter would read these and attempt to apply them.
- **Logging/Error outputs:** If there's a logging system (maybe using an atomic queue as per Threading guidelines), the adapter would use it to report errors. E.g., log if device open fails or if buffer underflows are detected.

## Hard Real-Time Constraints

The Backend Adapter is partly on the boundary of our real-time system and the OS:

- **Callback as Real-Time Entry:** The audio callback (invoked by the OS/driver) is our real-time audio thread. The adapter must treat it with the same caution as we treat our internal audio thread. That means the callback function implemented in the adapter should do nothing but call into the Spatializer/engine processing and return as quickly as possible. All RT constraints (no locks, no allocations, etc.) apply inside that callback. Often, the callback will call something like `Engine.process(outputBuffer, frames)` which in turn runs our Spatializer and other agents.
- **Minimal Overhead:** The adapter’s code in the callback should be minimal (just routing data). Any heavy lifting like device setup or error handling is done outside the callback (on the main thread or separate thread). For example, if an error occurs, the callback might set a flag and output silence, but actual recovery (like reopening device) happens outside.
- **Thread Priority:** Usually the OS ensures the audio callback thread has high priority (especially if using ASIO or similar). If not, the adapter should attempt to boost it (some APIs allow explicitly setting the mode, e.g., PortAudio can use PaUnixThreadConfigureScheduler for Linux RT). We need to ensure the audio thread priority aligns with what Threading agent expects. Document how this is achieved or if the OS does it by default.
- **Synchronization:** The adapter might interact with other threads when starting/stopping:
  - Starting: might need to signal the audio thread loop to begin. E.g., if using a separate thread that calls blocking writes, that thread we create is the audio thread. We then signal it to run. We must avoid race conditions in startup/shutdown (like audio thread starting to call engine while engine not fully ready). Use proper initialization order (e.g., initialize all engine data structures before starting backend callback).
  - Stopping: ensure to stop the audio callback safely. Many APIs require stopping stream then joining callback. We should coordinate this so that as we tear down, the callback is not accessing freed data. Often done by stopping the stream (which blocks until callbacks done or flushes).
- **Buffer Size Consistency:** The engine should know the buffer size (frames per callback). Typically, we fix that when opening device. Make sure all agents (Spatializer, Streaming etc.) are aware of this size if needed. Ideally they operate dynamically per frame, but some might have assumptions (like maybe streaming reads in chunks of that size). The adapter should feed this info to those who need it (perhaps via config in Spatializer).
- **Sample Rate Consistency:** Similar to above; ensure the engine is running at the device sample rate. If we needed resampling (say an audio file at a different rate), Streaming agent would handle it. The adapter should avoid on-the-fly resampling. Instead, open device at the engine’s intended sample rate (or adjust engine initialization if device only supports another).
- **Memory Copy vs Direct Buffer:** If possible, avoid extra buffer copies. For example, if the API’s callback gives us an output buffer pointer (in interleaved format), and our Output Remap already prepared interleaved data, we can directly copy or even have our processing fill that buffer. If it expects a certain format, we might have to convert. But keep it minimal.

## Interaction with Other Agents

- **Output Remap Agent:** The Backend Adapter relies on Output Remap to provide correctly ordered channel data. The adapter should be configured with the same channel count as Output Remap’s output. The mapping from engine channels to device channels is handled already, so the adapter just passes the buffer. If for some reason the backend’s channel order is different and we didn’t handle it in Output Remap, the adapter might do a final swap. But ideally, Output Remap has done it so adapter can be ignorant of actual channel semantics.
- **Threading and Safety Agent:** The backend’s audio callback is effectively the real-time thread we’ve been designing around. The Threading agent likely prescribes that this callback thread is the high-priority one. The Backend Adapter must ensure it aligns (maybe by setting the thread to RT priority if the API didn’t). Also, the adapter should follow the shutdown sequence advised: e.g., maybe Threading agent says “stop all non-RT threads, then stop audio thread”. The adapter’s `stopStream()` should be called at the right time. Possibly coordinate with Threading agent to implement graceful shutdown (like ensure streaming thread is done or paused before closing device to avoid audio thread waiting on data).
- **Spatializer (and pipeline):** The adapter directly invokes the Spatializer processing. It might call a function like `spatialMixer->render(outputBuffer, nFrames)`. That function will in turn gather from Streaming, Pose, etc., but from adapter’s perspective it’s one call. Need to ensure any context needed (like current time or buffer index) is handled. If Spatializer or others require an incrementing frame counter or timestamp, the adapter could supply one if needed (usually not, but e.g., some systems track sample time).
- **GUI/Pose Control:** If the user changes output settings (like chooses a different audio device or toggles between speakers/headphones which require reconfiguring channels), the GUI or control would trigger the Backend Adapter to reinitialize. The adapter should expose an interface (like `setOutputDevice(deviceId)` or `switchOutputMode(mode)`). Implementation will involve stopping current stream and starting a new one. This ties into the multi-thread coordination: it needs to pause the engine possibly during reinit. So interactions: Pose/GUI says change device, Backend Adapter performs it, and informs maybe Pose or others if channel count changed (in case that affects any logic).
- **Streaming Agent:** Largely independent except for the fact that if sample rate changes or buffer size drastically changes, streaming might need to know to adjust its buffer strategy. Ideally sample rate is fixed from start for a session. The adapter might get the actual sample rate from device if the requested wasn’t available and had to use something else; if so, it should inform the engine (maybe through a global config state). But let’s assume we choose a supported common sample rate upfront.
- **Compensation and Gain Agent:** If the backend informs actual hardware volume (like on mobile devices, there’s concept of system volume), we might incorporate that. But likely we treat engine volume separate. However, some APIs like WASAPI shared mode might be affected by system volume. If needed, the adapter could read system volume and adjust master gain in Compensation agent. This is a stretch goal; for now, probably not needed unless specified.

## Data Structures & Interfaces

- **Backend Context:** A structure containing handles to the audio API objects (e.g., a PortAudio stream pointer, or device IDs, etc.), as well as our settings (sampleRate, bufferSize, numChannels).
- **Audio Callback Function:** For instance, in C style:
  ```cpp
  static int audioCallback(const void* input, void* output, unsigned long frames,
                            const PaStreamCallbackTimeInfo* timeInfo,
                            PaStreamCallbackFlags statusFlags, void* userData) {
      return g_engine->processAudio((float*)output, frames);
  }
  ```

### Block-level control snapshot, smoothing, and pause fade (Phase 10 polish — Feb 27 2026)

**Status: ✅ Implemented and verified (cmake build: zero errors)**

#### Problem

The original `processBlock()` had three correctness/quality issues:

| Issue                                    | Symptom                                                           |
| ---------------------------------------- | ----------------------------------------------------------------- |
| Repeated atomic reads inside inner loops | Redundant atomic traffic; inconsistent parameter values mid-block |
| Hard-mute on pause (`memset` + `return`) | Audible click transient on every Pause/Play                       |
| No smoothing of OSC slider changes       | Step discontinuity in gain/focus → potential click                |

#### Solution implemented

**A — Atomic snapshot (one read per param per block)**

At the very top of `processBlock()`, all runtime-control atomics (`masterGain`, `loudspeakerMix`, `subMix`, `focusAutoCompensation`, `paused`) and the plain-float `dbapFocus` are read once into a `ControlSnapshot` struct. Nothing below that point in the block reads the config atomics.

**B — Exponential smoothing toward snapshot targets**

```
alpha = 1 − exp(−dt / τ)    where τ = 50 ms (default)
smoothed = smoothed + alpha × (target − smoothed)
```

One `std::exp` call per block (negligible cost). Smoothed values are passed into `Spatializer::renderBlock()` via a stack-allocated `ControlsSnapshot` struct — they are **never written back into `mConfig`** (see feedback-loop fix below).

At 512 frames / 48 kHz (10.7 ms block): α ≈ 0.19 → 95% settling in ~4 blocks (~43 ms). Slider moves produce a short ramp rather than a step.

**C — Pause/resume fade (8 ms linear ramp)**

On a pause edge: arm a fade-OUT → `mPauseFade` ramps 1→0 over `kPauseFadeMs = 8 ms` frames.  
On a resume edge: arm a fade-IN → `mPauseFade` ramps 0→1 over the same duration.

The ramp is applied per-sample after all rendering in Step 4. When fully paused (fade complete, `mPauseFade == 0`), buffers are cleared and `frameCounter` is **not** advanced (playback position stays frozen).

**D — Per-channel gain anchors (placeholder for block-boundary ramp)**

`mPrevChannelGains[]` / `mNextChannelGains[]` are maintained per block. Currently identity (all 1.0). When implemented with DBAP top-K precomputation, the copy-to-output loop will `lerp(prev, next, t)` per frame to eliminate speaker-switch clicks.

#### Private members added

| Member                  | Type                      | Purpose                                        |
| ----------------------- | ------------------------- | ---------------------------------------------- |
| `ControlSnapshot`       | inner struct              | per-block snapshot target + smoothed values    |
| `SmoothedState mSmooth` | struct                    | holds `smoothed`, `target`, `tauSec`           |
| `kPauseFadeMs`          | `static constexpr double` | 8 ms fade duration                             |
| `mPrevPaused`           | `bool`                    | previous block's pause state (edge detection)  |
| `mPauseFade`            | `float`                   | current fade envelope [0, 1]                   |
| `mPauseFadeStep`        | `float`                   | per-sample delta (signed)                      |
| `mPauseFadeFramesLeft`  | `unsigned int`            | samples remaining in ramp                      |
| `mPrevChannelGains[]`   | `vector<float>`           | block-start per-channel gains                  |
| `mNextChannelGains[]`   | `vector<float>`           | block-end per-channel gains (identity for now) |

#### processBlock() call order (updated)

```
1. Snapshot all atomics + dbapFocus into mSmooth.target
2. Exponential smooth: mSmooth.smoothed → target (alpha = 1-exp(-dt/tau))
3. Detect pause edge → arm fade ramp if changed
4. Resize/shift per-channel gain anchors (identity placeholder)
5. Zero output channels
6. Pose::computePositions(blockCenter)
7. Build ControlsSnapshot from mSmooth.smoothed; call Spatializer::renderBlock(..., ctrl)
   ↳ mConfig atomics are NOT written — they remain the OSC thread's exclusive domain
8. Apply pause fade per-sample (scale output, freeze frameCounter if fully paused)
9. Update frameCounter, playbackTimeSec, cpuLoad
```

#### Feedback-loop bug fix (Feb 27 2026) — "smoother eats its own output"

An earlier implementation wrote `mSmooth.smoothed.*` back into the `mConfig` atomics before calling `renderBlock()`. This caused the smoother to converge toward a moving target: each block's Step A re-read the partially-smoothed value as the new target, meaning the true OSC-set destination was overwritten immediately. Parameters appeared stuck or barely responsive.

**Fix:** introduced `ControlsSnapshot` (a plain struct defined in `Spatializer.hpp`) and changed `renderBlock()` to accept `const ControlsSnapshot& ctrl`. The audio thread now has a strict ownership model:

| Role                 | `mConfig` atomics                              | `mSmooth.smoothed`                             |
| -------------------- | ---------------------------------------------- | ---------------------------------------------- |
| **Written by**       | OSC listener thread only                       | Audio thread only                              |
| **Read by**          | Audio thread (Step A snapshot, once per block) | Audio thread (Step 7, into `ControlsSnapshot`) |
| **Never written by** | Audio thread                                   | OSC thread                                     |

`Spatializer::renderBlock()` reads **only** from `ctrl` for `masterGain`, `focus`, `loudspeakerMix`, and `subMix`. It calls `mDBap->setFocus(ctrl.focus)` at the top of each block — also fixed here (previously `mDBap->mFocus` was baked at `init()` and never updated at runtime).

#### Master gain range expanded (Feb 27 2026)

Gain range changed from `[0.0, 1.0]` to `[0.1, 3.0]`, default remains `0.5`.

| Location                             | Before               | After                |
| ------------------------------------ | -------------------- | -------------------- |
| `main.cpp` `gainParam` min/max       | `0.0f – 1.0f`        | `0.1f – 3.0f`        |
| `runRealtime.py` validation guard    | `0.0 ≤ gain ≤ 1.0`   | `0.1 ≤ gain ≤ 3.0`   |
| `RealtimeControlsPanel.py` row range | `0.0–1.0, step 0.01` | `0.1–3.0, step 0.05` |

#### Threading notes

- `dbapFocus` is a plain `float` written by the ParameterServer listener thread. The snapshot in Step 1 reads it with the "one-buffer lag" contract (same as the atomics). No UB because `float` reads/writes are atomic on ARM and x86 at the hardware level, but note this is not formally guaranteed by C++. A future hardening pass could make it `std::atomic<float>`.
- All other smoothing / fade state is written AND read exclusively on the audio thread — no synchronization needed.
````
