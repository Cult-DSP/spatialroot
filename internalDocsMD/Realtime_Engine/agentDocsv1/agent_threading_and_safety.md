# Threading and Safety Agent

> **Implementation Status: ✅ COMPLETE (Phase 8, Feb 25 2026)**
> No new runtime mechanisms were needed — the engine was already correct.
> Phase 8 was a full cross-agent threading audit + documentation pass.
>
> **Phase 10 addendum (Feb 26 2026): OSC timing fix + runtime parameter flush**
> GUI Agent fix for "sliders move but values not updated" — see §OSC Runtime
> Parameter Delivery at the bottom of this file.
>
> **Feb 27 2026 addendum: `dbapFocus` race fixed + `elevationMode` runtime switch**
> `dbapFocus` promoted from plain `float` to `std::atomic<float>` — closes the
> pre-existing data race documented as Invariant 9. `elevationMode` added as a new
> runtime-switchable `std::atomic<int>` in `RealtimeConfig`, readable per-block by
> the audio thread and writable at any time via OSC. See §Elevation Mode Runtime
> Switch and the updated invariant table below.
>
> **What was implemented (Phase 8):**
>
> - Canonical threading model documented in `RealtimeTypes.hpp` (3-thread table,
>   memory order table, 6 invariants that must never be violated)
> - `Streaming.hpp`: shutdown ordering contract, acquire/release protocol documented
> - `Pose.hpp`: audio-thread ownership of `mPoses`/`mLastGoodDir` documented
> - `Spatializer.hpp`: `computeFocusCompensation()` marked MAIN THREAD ONLY + reason
> - `RealtimeBackend.hpp`: `processBlock()` threading annotations, null-guard hardened
>
> See `realtime_master.md` Phase 8 Completion Log for the full memory order audit
> table and invariant list.

## Overview

The **Threading and Safety Agent** defines and oversees the multi-threading model of the real-time spatial audio engine. This agent’s role is somewhat abstract compared to others: it doesn’t process audio data itself, but it establishes how different components (agents) run concurrently and how they communicate without compromising real-time performance or data integrity. It provides guidelines, utilities, and possibly base implementations (like thread classes or synchronization primitives) to ensure that each agent can operate in parallel where appropriate, and that the real-time audio thread is protected from any blocking or unsafe operations.

In essence, this agent’s “deliverable” is a threading architecture and a set of safety mechanisms (e.g., lock-free queues, double buffers) that all agents will use. It will coordinate the startup and shutdown of threads and enforce the priority scheme required for glitch-free audio. By treating threading and concurrency as a first-class concern, we avoid common pitfalls in real-time systems such as race conditions, deadlocks, priority inversion, and audio underruns due to thread scheduling issues.

## Responsibilities

- **Thread Architecture Design:** Determine how many threads are used and which agent runs on which thread or callback. For example:
  - Real-time Audio Thread: runs the Spatializer (and downstream processing like LFE, Compensation, Output Remap) typically via the audio API’s callback.
  - Streaming Thread(s): one or more threads to handle disk I/O or network I/O for audio data.
  - Control/Pose Thread: handles incoming control events and updates positions.
  - GUI/Main Thread: runs the user interface and issues commands to the engine.
  - Possibly additional threads for things like logging or background tasks if needed.
    This agent defines that blueprint so all team members know where their code will run.
- **Synchronization Strategy:** Lay out how data will be passed between threads safely:
  - Use of **Lock-Free Queues** for producer/consumer scenarios (e.g., Streaming -> Audio thread buffer transfer).
  - Use of **Double Buffering** for shared state (e.g., Pose updates vs audio reads of positions). In double buffering, the control thread writes to one copy of data while the audio thread reads from another, and then they swap pointers at a synchronization point (often at audio frame boundaries) using an atomic operation.
  - **Atomic Flags and Variables:** For simple signals or state (e.g., an atomic boolean for “engine running” or atomic index for current buffer).
  - Minimal use of mutexes: Only in places that do not affect the audio thread or where absolutely necessary (e.g., protecting a list of sources when adding/removing on the control thread, with the audio thread reading in a lock-free manner).
- **Real-Time Safety Guidelines:** Establish a clear set of do’s and don’ts for code running on the audio thread:
  - Do not call blocking OS functions (file I/O, sleep, network, etc.).
  - Do not allocate memory or free memory.
  - Do not use locks or wait on conditions.
  - Do not call into external libraries that are not RT-safe (e.g., printing to console can block, certain audio library calls, etc.).
  - Keep the audio callback execution time consistent and as short as possible:contentReference[oaicite:7]{index=7}:contentReference[oaicite:8]{index=8}.
    This agent’s documentation serves to remind developers of these rules and provide alternatives (e.g., “if you need to log something from audio thread, push it into a lock-free log queue for a background thread to write to disk”).
- **Priority and CPU Affinity:** Ensure the audio thread runs at high priority (e.g., real-time priority class on Windows, SCHED_FIFO on Linux) and that other threads do not preempt it excessively. This might include setting thread priorities in code and advising to avoid heavy CPU load on cores that run the audio thread. Possibly provide utility functions to elevate priority of the audio callback thread (some APIs allow you to request RT priority).
- **Thread Lifecycle Management:** Provide a mechanism to cleanly start and stop threads. For example, when the engine initializes, spawn the streaming thread, control thread, etc., and when shutting down, signal them to exit and join them properly. This agent might implement those signals (like an atomic bool `shutdownRequested`) and sequences for orderly teardown, ensuring that the audio thread stops last after all feeders have stopped (to avoid use-after-free of data).
- **Debugging and Monitoring:** Possibly include facilities to monitor thread health (like detecting if streaming thread is falling behind or if audio thread’s callback timings are close to exceeding the deadline). This could be a simple high-water mark of processing time or counters of overruns. While not primary responsibility, defining how we’ll detect and log real-time violations is useful (perhaps via an atomic counter of missed deadlines or an audio xrun callback from the backend that increments a counter visible to the GUI).

## Relevant Internal Files

- **`mainplayer.cpp`:** Likely where threads are created and launched. For example, `std::thread` for streaming might be started here, and real-time thread priority might be set here if using a separate thread for audio (or if using an API callback, mainplayer registers the callback).
- **`ThreadUtils.h` / `.cpp`:** A utility module to wrap thread creation and set priorities, CPU affinities, etc. Could contain, for instance, a function `makeRealTime(threadHandle)` that uses OS-specific calls to boost the thread priority.
- **`LockFreeQueue.h`:** A template or class implementing a lock-free ring buffer for communication. The Streaming agent and others will use this. Implementations could be based on single-producer, single-consumer assumptions to keep it simple.
- **`DoubleBuffer.h`:** A small utility to manage double-buffered data. For instance, it might hold two copies of a structure and an atomic index indicating which is current. Pose and Control might utilize this for positions.
- **`AudioCallback.cpp`:** If the audio API allows setting a custom thread or callback function, the code there might call into our Spatializer. Ensure any thread attributes are set (like JACK or ASIO allow setting buffer sizes or have their own threads).
- **Documentation Files:** This agent’s design should be reflected in RENDERING.md (which likely covers real-time and threading considerations) and possibly in an `internalDocsMD/agents.md` index where threading model summary could be listed for quick reference.

## Hard Real-Time Constraints

This agent is essentially the enforcer of constraints:

- **Zero Tolerance for Audio Thread Blocking:** It sets up the system such that the audio thread can always run when needed. For instance, if using a condition variable for streaming, the audio thread will never wait on it; only the streaming thread might.
- **Lock-Free Data Access:** The patterns implemented must ensure that the audio thread’s view of data is valid without locks. If double buffering is used, the swap of buffers should be atomic and happen either on the audio thread at a safe point or on the control thread in coordination with audio. Often the audio thread does the swap at end of frame if a flag is set indicating new data is ready.
- **Priority Inversion Avoidance:** If the audio thread ever needs to occasionally lock something (ideally not, but suppose it had to lock a very small mutex for some minor section), ensure no lower-priority thread holds that lock for longer than a few microseconds. In general, prefer to design out such locks.
- **Memory Management:** Pre-allocate buffers for communication. For example, a lock-free queue should have a fixed size buffer. If it’s full, the producer might either drop data or overwrite old data (depending on acceptable policy) rather than allocate more. All such decisions should be made to avoid allocation in steady-state.
- **Testing in Real Conditions:** The agent should encourage testing under different CPU loads. For instance, if the OS schedules another process, do we still meet deadlines? If not, perhaps advise using real-time scheduling or reserving a CPU core for audio. This might go beyond our immediate implementation, but the documentation can mention recommendations (like “On Linux, consider isolating a core for the audio thread” or “Make sure to run with RT priorities to minimize dropouts”).

## Interaction with Other Agents

- **Streaming Agent:** The Threading agent dictates how streaming passes data to audio. Likely it provides the lock-free queue and usage pattern. The Streaming Agent’s doc should reference using the queue from here. Also, Threading agent might suggest how many streaming threads (maybe one thread handles all sources sequentially, or a thread pool – depending on complexity).
- **Pose and Control Agent:** Provides the mechanism (double buffer or atomic variables) for Pose to update positions without locking audio. The Pose agent doc references double buffering as a technique; that stems from guidelines here. Possibly, the actual double buffer utility is implemented by Threading agent for Pose to use.
- **Spatializer Agent:** Receives data via these thread-safe methods. For instance, Spatializer will use an atomic pointer to current scene state provided by Pose agent (who got it via Threading agent’s mechanism). Spatializer might also signal back if something is wrong (like underrun). The Threading design might have a provision that if Spatializer finds no data for a source (underrun), it could flag it. That flag could be an atomic that Streaming thread monitors to maybe refill faster or so. This interplay can be noted.
- **Output Remap & Backend:** If the backend uses its own thread vs our audio thread, that’s crucial. Many audio APIs (like WASAPI in event mode, or some ALSA, or JACK) run the callback in a separate thread internally. So effectively, the “audio thread” might be created by the backend. The Threading agent should account for that scenario:
  - If backend creates thread, how do we set its priority? Possibly through API or OS calls after it starts (some APIs allow naming or hooking thread init).
  - If we instead pull model (like a dedicated thread that calls an output API in blocking mode), then we create the thread and can set priority directly.
  - Document whichever approach we take, and how the backend and threading coordinate.
- **GUI Agent:** The GUI is typically on the main thread (in many frameworks). The Threading agent ensures that GUI interactions with the engine happen through safe channels (like posting commands to control thread). It should discourage directly calling engine methods that might lock or conflict. Possibly define that the GUI never touches audio data directly; it always goes through Pose/Control or other safe interfaces.
- **Compensation/Gain Agent:** Might not need separate thread, it’s likely part of audio thread. But if any lengthy calibration loading or loudness analysis were needed (not currently), that would be done offline or at init. So mostly, it just follows the thread safety rules – e.g., any global volume changes from GUI should be atomic or message-based to avoid interfering with audio thread.
- **All Agents – Coordination:** The Threading agent likely organizes a high-level view: e.g., timeline of events each frame (Audio thread tick, control thread loop, etc.). It might specify that certain agents should signal others (like after Pose updates a buffer, it sets a flag that audio thread reads). Each agent doc references those flags/structures which are defined by the Threading strategy.

## Data Structures & Interfaces

- **Lock-Free Queue Implementation:** For example, a templated ring buffer with head/tail indices managed with atomics. Possibly single-producer, single-consumer usage for simplicity (e.g., streaming thread produces audio buffers into it, audio thread consumes). Provide methods like `push(buffer)` and `pop(buffer)` that are non-blocking and wait-free (except maybe when empty or full they fail immediately).
- **Double Buffer (for shared state):** This could be a struct:
  ```cpp
  template<typename T>
  class DoubleBuffer {
      T buffer[2];
      std::atomic<int> currentIndex;
  public:
      T* getWriteBuffer() { return &buffer[1 - currentIndex.load()]; }
      void publish() { currentIndex.store(1 - currentIndex.load()); }
      const T* getReadBuffer() const { return &buffer[currentIndex.load()]; }
  };
  ```

---

## OSC Runtime Parameter Delivery — Phase 10 Addendum (Feb 26 2026)

> **Bug fixed:** "sliders move but values are not updated" — reported after
> Phase 10 GUI implementation. Root cause: premature `RUNNING` state in the
> Python runner, leading to UDP packets dropped by the OS before the C++
> `al::ParameterServer` had bound its socket.

### The timing problem

The four-thread model at runtime:

| Thread                       | Owner            | What it does                                    |
| ---------------------------- | ---------------- | ----------------------------------------------- |
| Qt main thread               | Python / PySide6 | GUI event loop, OSC sends via `SimpleUDPClient` |
| QProcess subprocess (Python) | `runRealtime.py` | Scene load, WAV open, C++ binary exec           |
| C++ main thread              | `main.cpp`       | Engine init, monitoring loop, auto-comp         |
| C++ ParameterServer listener | AlloLib          | UDP recv → `registerChangeCallback` dispatch    |

The `QProcess::started` signal fires when `runRealtime.py` **spawns** as a
Python process — not when the C++ binary launches, and not when
`al::ParameterServer` binds its UDP socket. The time between `started` and
the server being ready can be **several seconds** (scene JSON parse, WAV file
open, binary load, AlloLib audio device init).

Any `SimpleUDPClient.send_message()` call during that window succeeds at the
socket level (UDP is connectionless) but the packet is **silently discarded**
by the OS because nothing has called `bind()` on port 9009 yet.

### The fix: stdout sentinel + deferred state transition

`main.cpp` already prints this line immediately after
`paramServer.serverRunning()` succeeds:

```
[Main] ParameterServer listening on 127.0.0.1:<port>
```

`RealtimeRunner._on_stdout()` scans every incoming stdout line for this
sentinel. Until it appears, the runner stays in `LAUNCHING` state, and both
`send_osc()` and `schedule_osc()` return early (state guard):

```python
# realtime_runner.py
_ENGINE_READY_SENTINEL = "ParameterServer listening"

def _on_started(self) -> None:
    # Stay LAUNCHING — do NOT set RUNNING here
    self.output.emit("[Runner] Process started — waiting for ParameterServer…")

def _on_stdout(self) -> None:
    for line in data.splitlines():
        self.output.emit(line)
        if (self._state == RealtimeRunnerState.LAUNCHING
                and self._ENGINE_READY_SENTINEL in line):
            self._set_state(RealtimeRunnerState.RUNNING)
            self.engine_ready.emit()   # ← new Signal()
```

### Flush on ready: applying queued GUI values

When the GUI transitions from `LAUNCHING` → `RUNNING`, the `engine_ready`
signal triggers `RealtimeControlsPanel.flush_to_osc()`, which immediately
sends all current control panel values as OSC messages:

```python
# realtimeGUI.py
runner.engine_ready.connect(controls_panel.flush_to_osc)
```

```python
# RealtimeControlsPanel.flush_to_osc()
def flush_to_osc(self) -> None:
    self.osc_immediate.emit("/realtime/gain",           self._gain_row.value())
    self.osc_immediate.emit("/realtime/focus",          self._focus_row.value())
    self.osc_immediate.emit("/realtime/speaker_mix_db", self._spk_row.value())
    self.osc_immediate.emit("/realtime/sub_mix_db",     self._sub_row.value())
    v = 1.0 if self._auto_comp_check.isChecked() else 0.0
    self.osc_immediate.emit("/realtime/auto_comp", v)
    self.osc_immediate.emit(
        "/realtime/elevation_mode",
        float(self._elev_mode_combo.currentIndex()),
    )
```

This guarantees the engine always starts with the values the user has
configured in the GUI, regardless of how long loading took.

### Threading implications

The `engine_ready` signal is emitted from `_on_stdout`, which runs on the
**Qt main thread** (QProcess stdout callbacks are dispatched there). The
`flush_to_osc()` call and the subsequent `SimpleUDPClient.send_message()`
calls are therefore also on the main thread — correct, and consistent with
all other OSC sends from the GUI.

The sentinel string is printed by `main.cpp` from the **C++ main thread**,
which is also where `paramServer.serverRunning()` is called. There is no
race: if `serverRunning()` returns true, the socket is already bound and
the listener thread is already running before the sentinel line reaches
Python's stdout reader.

### Invariant added (Phase 10)

> **Invariant 7:** The Python GUI runner **must not** send OSC messages until
> the C++ ParameterServer has confirmed it is listening (stdout sentinel
> received). `RealtimeRunnerState.LAUNCHING` enforces this via the state guard
> in `send_osc()` / `schedule_osc()`. Bypassing the state guard (e.g., calling
> `_debouncer.send_now()` directly) during `LAUNCHING` is **prohibited**.

### Files changed (Phase 10 OSC fix)

| File                                                       | Change                                                                              |
| ---------------------------------------------------------- | ----------------------------------------------------------------------------------- |
| `gui/realtimeGUI/realtime_runner.py`                       | `_on_started` stays `LAUNCHING`; `_on_stdout` sentinel probe; `engine_ready` Signal |
| `gui/realtimeGUI/realtime_panels/RealtimeControlsPanel.py` | Added `flush_to_osc()` method                                                       |
| `gui/realtimeGUI/realtimeGUI.py`                           | `runner.engine_ready.connect(controls_panel.flush_to_osc)`                          |

## Runtime control smoothing & pause-click fixes (Phase 10 polishing)

> **Status: ✅ Implemented** — `RealtimeBackend.hpp` rewritten. Build verified clean (`[100%] Built target spatialroot_realtime`).

### Problem statement

Before this pass, `processBlock` had three race/quality defects:

| #   | Defect                                                                           | Symptom                                                          |
| --- | -------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| 1   | Each runtime control was read multiple times per block from `std::atomic` fields | Incoherent parameter state mid-block; unnecessary atomic traffic |
| 2   | Parameter changes applied instantaneously                                        | Audible zipper noise when adjusting gain/focus/mix sliders       |
| 3   | Pause/resume implemented as a hard mute (`memset → return`)                      | Audible click on every pause and resume                          |

### Solution A — Per-block control snapshot

At the very start of `processBlock`, all runtime controls are read **exactly once** into a local `ControlSnapshot`:

```cpp
struct ControlSnapshot {
    float masterGain   = 1.0f;
    float focus        = 1.0f;
    float loudspeakerMix = 1.0f;
    float subMix       = 1.0f;
    bool  autoComp     = false;
};
```

`masterGain`, `loudspeakerMix`, `subMix`, `focusAutoCompensation`, and `paused` are `std::atomic` and are loaded with `.load()`. `dbapFocus` is also `std::atomic<float>` (promoted from plain `float` on Feb 27 2026 — see §`dbapFocus` data race fix below) and is loaded with `.load(std::memory_order_relaxed)`.

The snapshot values are then stored as `mSmooth.target` for the smoothing step.

### Solution B — Exponential smoothing (tau = 50 ms)

Each block, the smoothed state advances toward the snapshot target:

$$\alpha = 1 - e^{-\Delta t / \tau}, \quad \tau = 50\,\text{ms}$$

where $\Delta t = \text{framesPerBuffer} / \text{sampleRate}$. This costs one `std::exp` call per block (~negligible vs. audio work).

```cpp
double alpha = 1.0 - std::exp(-blockDurSec / mSmooth.tauSec);
auto& s = mSmooth.smoothed;
auto& t = mSmooth.target;
s.masterGain     += alpha * (t.masterGain     - s.masterGain);
s.loudspeakerMix += alpha * (t.loudspeakerMix - s.loudspeakerMix);
s.subMix         += alpha * (t.subMix         - s.subMix);
s.focus          += alpha * (t.focus          - s.focus);
s.autoComp        = t.autoComp;   // boolean — instant, no smoothing
```

The smoothed values are passed to `Spatializer::renderBlock()` via a stack-allocated `ControlsSnapshot ctrl` built from `mSmooth.smoothed`. They are **never written back into `mConfig`** — see Invariant 12 below.

### Solution C — Pause/resume fade (8 ms linear ramp)

Hard-mute pause guard **removed**. Instead, a per-sample linear ramp `mPauseFade ∈ [0, 1]` is multiplied onto every output sample in Step 4.

**State machine:**

| State                | `mPauseFade` | `mPauseFadeStep` | `mPauseFadeFramesLeft` |
| -------------------- | ------------ | ---------------- | ---------------------- |
| Playing normally     | `1.0`        | `0.0`            | `0`                    |
| Pause edge detected  | 1→0          | `< 0` (fade-out) | `fadeFrames`           |
| Fully paused         | `0.0`        | `0.0`            | `0`                    |
| Resume edge detected | 0→1          | `> 0` (fade-in)  | `fadeFrames`           |

`fadeFrames = ceil(kPauseFadeMs * sampleRate / 1000)`, `kPauseFadeMs = 8.0`.

**Fully-paused optimisation:** once `mPauseFade` reaches 0 and `mPrevPaused` is still `true`, the block is zeroed and returned early **without advancing `frameCounter`**. This means playback position is preserved across pauses.

### Solution D — Per-channel gain-ramp anchors (placeholder)

`mPrevChannelGains` and `mNextChannelGains` (both `std::vector<float>`, resized to `io.channelsOut()`) are reserved for future per-block gain interpolation. Currently the identity ramp (constant gain across the block) is applied — this eliminates per-channel step artefacts when speaker count changes between blocks. Full DBAP top-K interpolation is deferred to a future pass.

---

### Threading invariants (additions to master list)

**Invariant 8 — Single atomic read per block:**  
The audio callback loads each `std::atomic` control field **at most once** per `processBlock` invocation, storing the result in the local `ControlSnapshot`. No atomic is re-read inside the rendering loops.

**Invariant 9 — ~~`dbapFocus` plain-float data race (accepted, future hardening)~~ CLOSED (Feb 27 2026):**  
`RealtimeConfig::dbapFocus` is now `std::atomic<float>`. Previously it was a plain `float` written by the ParameterServer OSC listener thread and read by the audio thread — a data race that was technically UB even though de-facto atomic on x86-64/ARM64. All read sites use `.load(std::memory_order_relaxed)`, all write sites use `.store(v, std::memory_order_relaxed)`. Files updated: `RealtimeTypes.hpp`, `main.cpp` (4 sites), `RealtimeBackend.hpp` (snapshot), `Spatializer.hpp` (`init()` + `computeFocusCompensation()` prints).

**Invariant 10 — `frameCounter` not advanced while fully paused:**  
When `mPauseFade == 0.0f` and `paused == true`, the block returns after zeroing output without incrementing `frameCounter` or `playbackTimeSec`. Pose interpolation therefore resumes from the exact frame it left off.

**Invariant 11 — No heap allocation in processBlock:**  
`mPrevChannelGains` and `mNextChannelGains` are resized (and thus heap-allocated) only when `io.channelsOut()` changes — a configuration event, not a per-block event. Per-block execution path is allocation-free.

**Invariant 12 — Audio thread never writes back to `mConfig` target atomics:**  
`mConfig.masterGain`, `mConfig.loudspeakerMix`, `mConfig.subMix`, and `mConfig.dbapFocus` are written **exclusively** by the OSC listener thread (via `registerChangeCallback`). The audio thread reads them once per block (Step A snapshot) and then operates only on `mSmooth.smoothed`. Writing smoothed intermediates back into these fields would corrupt the target, causing the smoother to converge toward a moving point that always trails just behind itself — the "smoother eats its own output" feedback loop, which manifests as parameters appearing stuck or barely responsive. The fix: `Spatializer::renderBlock()` accepts a `const ControlsSnapshot& ctrl` built from `mSmooth.smoothed`; no write-back to `mConfig` occurs anywhere in the audio callback.

**Invariant 13 — `Spatializer::renderBlock()` reads zero config atomics for live params:**  
`masterGain`, `focus`, `loudspeakerMix`, and `subMix` inside `renderBlock()` are read exclusively from the `ControlsSnapshot ctrl` argument. `mDBap->setFocus(ctrl.focus)` is called once per block at the top of `renderBlock()` — this was also a bug fix: previously `al::Dbap::mFocus` was baked at `init()` time and never updated, making focus changes appear to have no effect at runtime.

---

### `ControlsSnapshot` struct (defined in `Spatializer.hpp`)

```cpp
struct ControlsSnapshot {
    float masterGain     = 1.0f;
    float focus          = 1.0f;
    float loudspeakerMix = 1.0f;
    float subMix         = 1.0f;
};
```

Stack-allocated in `processBlock()`, passed by `const&` to `renderBlock()`. Lifetime: one audio block.

---

### Private members summary

| Member                 | Type                      | Purpose                                            |
| ---------------------- | ------------------------- | -------------------------------------------------- |
| `mSmooth.target`       | `ControlSnapshot`         | Raw atomic snapshot for this block                 |
| `mSmooth.smoothed`     | `ControlSnapshot`         | Exponentially-smoothed running state               |
| `mSmooth.tauSec`       | `double`                  | Smoothing time constant (0.050 s)                  |
| `kPauseFadeMs`         | `static constexpr double` | Fade duration in ms (8.0)                          |
| `mPrevPaused`          | `bool`                    | Previous block's pause state for edge detection    |
| `mPauseFade`           | `float`                   | Current fade envelope [0, 1]                       |
| `mPauseFadeStep`       | `float`                   | Per-sample increment (signed; negative = fade out) |
| `mPauseFadeFramesLeft` | `unsigned int`            | Samples remaining in active fade                   |
| `mPrevChannelGains[]`  | `std::vector<float>`      | Gain anchor at block start (per output channel)    |
| `mNextChannelGains[]`  | `std::vector<float>`      | Gain anchor at block end (per output channel)      |

### Files changed

| File                                                       | Change                                                                                                                                                                                          |
| ---------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp`    | Full `processBlock` rewrite; `ControlsSnapshot` passed to `renderBlock()`; smoothed values never written back to `mConfig`; `#include <cmath>` added                                            |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`        | Added `ControlsSnapshot` struct (file-scope); `renderBlock()` takes `const ControlsSnapshot& ctrl`; removed all `mConfig` atomic reads for live params; `mDBap->setFocus(ctrl.focus)` per-block |
| `spatial_engine/realtimeEngine/src/main.cpp`               | `gainParam` range `0.0–1.0` → `0.1–3.0`                                                                                                                                                         |
| `runRealtime.py`                                           | Gain validation guard updated to `[0.1, 3.0]`                                                                                                                                                   |
| `gui/realtimeGUI/realtime_panels/RealtimeControlsPanel.py` | Master Gain slider range and step updated                                                                                                                                                       |

---

## `dbapFocus` data race fix + `elevationMode` runtime switch (Feb 27 2026)

> **Status: ✅ Implemented** — Build verified clean (`[100%] Built target spatialroot_realtime`, zero warnings).

### `dbapFocus` promoted to `std::atomic<float>` (closes Invariant 9)

`RealtimeConfig::dbapFocus` was previously a plain `float`. It was written by the ParameterServer OSC listener thread and read by the audio thread's per-block snapshot in `RealtimeBackend::processBlock()` Step A. This is a data race per the C++ memory model — technically undefined behaviour even though it is de-facto atomic on x86-64 and ARM64 due to single-instruction aligned 4-byte stores.

**Fix:** `dbapFocus` is now `std::atomic<float>`. All read sites use `.load(std::memory_order_relaxed)`. All write sites use `.store(v, std::memory_order_relaxed)`. The relaxed ordering is correct for the same reason as all other live-control atomics: a one-block stale read of a gain/focus value is inaudible and does not constitute a logical data dependency.

| File                  | Change                                                                                         |
| --------------------- | ---------------------------------------------------------------------------------------------- |
| `RealtimeTypes.hpp`   | `float dbapFocus` → `std::atomic<float> dbapFocus{1.0f}`; added to memory-ordering table       |
| `main.cpp`            | Init: `.store()`; 2× prints: `.load()`; `focusParam` seed: `.load()`; OSC callback: `.store()` |
| `RealtimeBackend.hpp` | Step A snapshot: `.load(relaxed)`; removed stale "plain float" comment                         |
| `Spatializer.hpp`     | `init()` and `computeFocusCompensation()` print: `.load()`                                     |

### `elevationMode` — new runtime-switchable atomic

`RealtimeConfig::elevationMode` replaces the previous static `ElevationMode` field (which was only settable at startup via `--elevation_mode` CLI arg and had no OSC path).

**Type:** `std::atomic<int>` storing `static_cast<int>(ElevationMode::*)`.

| Value | `ElevationMode`     | Meaning                                                                               |
| ----- | ------------------- | ------------------------------------------------------------------------------------- |
| `0`   | `RescaleAtmosUp`    | Maps Atmos-style `[0°, +90°]` content into the layout's elevation range. **Default.** |
| `1`   | `RescaleFullSphere` | Maps the full `[-90°, +90°]` sphere into the layout's elevation range.                |
| `2`   | `Clamp`             | Hard-clips elevation to the layout's min/max bounds.                                  |

These modes are identical to the offline renderer's `SpatialRenderer::sanitizeDirForLayout()`. The realtime engine's `Pose::sanitizeDirForLayout()` uses the same math, ported in `Pose.hpp`.

#### Threading model for `elevationMode`

| Thread                                  | Operation                                                    | Ordering                                     |
| --------------------------------------- | ------------------------------------------------------------ | -------------------------------------------- |
| OSC listener (ParameterServer)          | `.store(mode, relaxed)` on `/realtime/elevation_mode` change | `relaxed` — no dependent data                |
| Audio thread (`Pose::computePositions`) | `.load(relaxed)` once at top of per-block loop               | `relaxed` — stale-by-one-block is acceptable |

**Why relaxed is correct:** An elevation mode switch is not sample-accurate. The audio consequence of applying the switch one block (10–11 ms at 512/48k) later than the OSC message arrived is indistinguishable from applying it at the exact block boundary. No dependent data follows the atomic — the loaded value is used to select a branch in `sanitizeDirForLayout()`, not to order other memory operations.

**Why `elevationMode` is NOT in `ControlsSnapshot` / the smoother:**  
`ControlsSnapshot` in `RealtimeBackend` exists to smooth continuous float parameters (gain, focus, mix) to eliminate zipper noise. `elevationMode` is a discrete 3-value enum. Smoothing an integer index is meaningless, and the only consumer (`Pose`) reads it directly from the atomic — routing it through the backend snapshot would add indirection for no benefit.

#### OSC address and GUI

| Layer              | Detail                                                                                                                                |
| ------------------ | ------------------------------------------------------------------------------------------------------------------------------------- |
| OSC address        | `/realtime/elevation_mode`                                                                                                            |
| Value type         | `float` carrying integer `0.0`, `1.0`, or `2.0` (standard AlloLib practice — no `al::ParameterInt`)                                   |
| C++ callback       | `elevModeParam.registerChangeCallback([&](float v) { int mode = clamp(round(v), 0, 2); config.elevationMode.store(mode, relaxed); })` |
| CLI flag           | `--elevation_mode <0\|1\|2>` (default `0`)                                                                                            |
| GUI control        | `QComboBox` in `RealtimeControlsPanel`; items: _Rescale Atmos Up (0°–90°)_, _Rescale Full Sphere (±90°)_, _Clamp to Layout_           |
| Flush on ready     | Included in `flush_to_osc()` — value pushed to engine when `engine_ready` fires                                                       |
| Disabled when idle | `_elev_mode_combo.setEnabled(enabled)` in `_set_all_enabled()`                                                                        |

#### Updated `RealtimeTypes.hpp` memory-ordering table entry

```
│ ::dbapFocus                 │ relaxed (audio snapshots in step A;    │
│                             │ OSC listener writes — one-block lag    │
│                             │ is inaudible. Was plain float before,  │
│                             │ which was a data race — now fixed.)    │
│ ::elevationMode             │ relaxed (stale-by-one-block is fine;   │
│                             │ mode switch is not sample-accurate)    │
```

#### Updated `Pose.hpp` threading section (live atomics note)

`Pose.hpp` previously listed `mConfig` as "read-only after `loadScene()`". This was inaccurate — `mConfig.elevationMode` is now a live atomic. The threading section was updated to:

```
LIVE ATOMIC (read by audio thread per-block, written by OSC listener):
  mConfig.elevationMode — loaded once at the top of computePositions() via
    relaxed atomic load. Stale-by-one-block is acceptable: elevation mode
    switches are not sample-accurate operations. No other mConfig fields
    are read by Pose during playback.
```

### New invariants

**Invariant 14 — `elevationMode` is the only `mConfig` field read by `Pose` during playback:**  
All other `mConfig` fields accessed by `Pose` (`sampleRate`, `bufferSize`, etc.) are set before `loadScene()` and never change during playback. `elevationMode` is the single exception — it is a live atomic read once per block at the top of `computePositions()`. No other `mConfig` reads occur inside `computePositions()`.

**Invariant 15 — `elevationMode` is never smoothed:**  
The exponential smoother in `RealtimeBackend` operates only on continuous float parameters (`masterGain`, `focus`, `loudspeakerMix`, `subMix`). `elevationMode` is discrete and is read directly by `Pose` — it must not be added to `ControlSnapshot` or the smoother. An instantaneous enum switch on block boundaries is the correct and intended behaviour.

### Files changed

| File                       | Change                                                                                                                                                               |
| -------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `RealtimeTypes.hpp`        | `dbapFocus`: `float` → `std::atomic<float>`; `elevationMode`: new `std::atomic<int>{RescaleAtmosUp}`; memory-ordering table updated; struct-level comment updated    |
| `Pose.hpp`                 | Threading comment: `mConfig` removed from "read-only" list; new "LIVE ATOMIC" section for `elevationMode`; `computePositions()` loads atomic with `relaxed`          |
| `RealtimeBackend.hpp`      | Step A snapshot: `dbapFocus` `.load(relaxed)`; `SmoothedState` comment documents why `elevationMode` is excluded                                                     |
| `Spatializer.hpp`          | `dbapFocus` reads in `init()` and `computeFocusCompensation()` → `.load()`                                                                                           |
| `main.cpp`                 | All `dbapFocus` sites → atomic; `--elevation_mode` CLI flag parsed; `elevModeParam` registered with OSC callback; startup summary prints mode name; `--help` updated |
| `RealtimeControlsPanel.py` | `QComboBox` with 3 mode labels; `currentIndexChanged` → `/realtime/elevation_mode`; added to `flush_to_osc()`, `reset_to_defaults()`, `_set_all_enabled()`           |
