# AlloLib Parameter System — Reference for spatialroot Real-Time Engine

> **Scope:** This document is the canonical reference for adding AlloLib's
> `al::Parameter` / `al::ParameterBool` / `al::ParameterBundle` /
> `al::ParameterServer` to the real-time engine (`spatial_engine/realtimeEngine/`).
> It covers the C++ engine side and the Python/GUI OSC-sender side.

---

## 1. Headers and Include Paths

All relevant headers live under `thirdparty/allolib/include/al/ui/`:

| Header                         | Provides                             |
| ------------------------------ | ------------------------------------ |
| `al/ui/al_Parameter.hpp`       | `al::Parameter`, `al::ParameterBool` |
| `al/ui/al_ParameterBundle.hpp` | `al::ParameterBundle`                |
| `al/ui/al_ParameterServer.hpp` | `al::ParameterServer`                |

The `al` CMake target (already linked in `CMakeLists.txt`) transitively exposes
all these headers. **No new `target_link_libraries` lines are needed.**

---

## 2. `al::Parameter` (float)

```cpp
#include "al/ui/al_Parameter.hpp"

// Constructor:
//   Parameter(name, group="", defaultValue=0, min=-99999, max=99999)
al::Parameter gain{"gain", "realtime", 0.5f, 0.0f, 1.0f};
al::Parameter focus{"focus", "realtime", 1.5f, 0.2f, 5.0f};
al::Parameter speakerMixDb{"speaker_mix_db", "realtime", 0.0f, -10.0f, 10.0f};
al::Parameter subMixDb{"sub_mix_db", "realtime", 0.0f, -10.0f, 10.0f};
```

- `get()` → `float` — thread-safe, lock-free (relies on float atomicity).
- `set(float value)` — thread-safe, triggers registered callbacks.
- The full OSC address is derived from `"/" + group + "/" + name`, e.g.
  `"/realtime/gain"`.

### Reading from the audio callback (safe)

```cpp
// In RealtimeBackend::processBlock() — read-only, lock-free:
float g = mGainParam->get();
```

`al::Parameter::get()` uses an atomic float internally — safe from any thread.

---

## 3. `al::ParameterBool` (boolean as float 0/1)

```cpp
#include "al/ui/al_Parameter.hpp"

// Constructor:
//   ParameterBool(name, group="", defaultValue=0, min=0, max=1)
al::ParameterBool autoComp{"auto_comp", "realtime", 0.0f};
al::ParameterBool paused{"paused", "realtime", 0.0f};
```

- Internally stores float `0.0` (false) or `1.0` (true).
- `get()` returns float; compare with `>= 0.5f` for bool.
- `set(0.0f)` → false; `set(1.0f)` → true.
- OSC address: `/realtime/auto_comp`, `/realtime/paused`.
- The GUI sends `<int>` 0 or 1; `ParameterBool` accepts both float and int
  payloads (handled in `setFields()`).

---

## 4. `al::ParameterBundle`

Groups related parameters under a common prefix. Used to organize the
`realtime` control group cleanly and to expose the whole group to the server
in one call.

```cpp
#include "al/ui/al_ParameterBundle.hpp"

al::ParameterBundle bundle{"realtime"};
bundle << gain << focus << speakerMixDb << subMixDb << autoComp << paused;
```

The bundle prefix is **already** embedded in each parameter's group name
(`"realtime"`) — so using `ParameterBundle` here is primarily for organization
and to allow `registerParameterBundle()` in one call.

---

## 5. `al::ParameterServer`

Listens for incoming OSC messages on a UDP port and routes them to registered
parameters by their full OSC address.

```cpp
#include "al/ui/al_ParameterServer.hpp"

// Constructor: ParameterServer(address="127.0.0.1", port=9010, autoStart=true)
// spatialroot uses port 9009 (project default).
al::ParameterServer paramServer{"127.0.0.1", 9009};

// Register individual parameters:
paramServer.registerParameter(gain);
paramServer.registerParameter(focus);
paramServer.registerParameter(speakerMixDb);
paramServer.registerParameter(subMixDb);
paramServer.registerParameter(autoComp);
paramServer.registerParameter(paused);

// Or register the whole bundle at once:
paramServer.registerParameterBundle(bundle);

// Print registered parameters + port to stdout:
paramServer.print();
```

- `autoStart=true` (default) starts the OSC listener thread in the constructor.
- `serverRunning()` → bool — check if the port was successfully bound.
- `stopServer()` — call before engine shutdown to join the listener thread.
- The server runs on its **own internal thread** — never the audio callback.

### Shutdown order

```
backend.stop()          // audio thread exits
paramServer.stopServer()  // listener thread exits
streaming.shutdown()    // file handles closed
```

`stopServer()` must be called before the `ParameterServer` object is destroyed.
In `main.cpp`, declare `paramServer` after agents so it destructs first, or
call `stopServer()` explicitly in the shutdown sequence.

---

## 6. Full OSC Address Table

These are the canonical OSC addresses the engine listens on. The GUI must
send to these exact paths.

| Parameter          | OSC address                | Type    | Range     | Default |
| ------------------ | -------------------------- | ------- | --------- | ------- |
| Master gain        | `/realtime/gain`           | `float` | 0.0 – 1.0 | 0.5     |
| DBAP focus         | `/realtime/focus`          | `float` | 0.2 – 5.0 | 1.5     |
| Loudspeaker mix dB | `/realtime/speaker_mix_db` | `float` | -10 – +10 | 0.0     |
| Sub mix dB         | `/realtime/sub_mix_db`     | `float` | -10 – +10 | 0.0     |
| Auto compensation  | `/realtime/auto_comp`      | `float` | 0 or 1    | 0       |
| Pause/Play         | `/realtime/paused`         | `float` | 0 or 1    | 0       |

> **Port:** 9009 (fixed, localhost). If the port is already in use,
> `paramServer.serverRunning()` returns false — the engine must print a clear
> error and exit rather than silently running without a control channel.

---

## 7. Wiring Parameters into `RealtimeConfig` / `main.cpp`

The `al::Parameter` objects live in `main()` scope (or in a thin
`ControlPlane` struct). The audio callback reads them directly via `get()`.

### Option: replace atomics with Parameters (preferred for GUI parameters)

For the five GUI-controlled values (`gain`, `focus`, `speakerMixDb`,
`subMixDb`, `autoComp`, `paused`), the `al::Parameter` objects can **replace**
the corresponding `std::atomic<float>` fields in `RealtimeConfig` —
or `RealtimeConfig` can hold `al::Parameter*` pointers set before `start()`.

The safest pattern for this codebase (minimal `RealtimeConfig` change):

```cpp
// In main.cpp, after creating all parameters and paramServer:
config.masterGain.store(gain.get());              // seed atomics from params
// Then register callbacks so atomics stay in sync:
gain.registerChangeCallback([&](float v){
    config.masterGain.store(v, std::memory_order_relaxed);
});
focus.registerChangeCallback([&](float v){
    config.dbapFocus.store(v, std::memory_order_relaxed);
});
speakerMixDb.registerChangeCallback([&](float v){
    // convert dB → linear and store
    config.loudspeakerMix.store(std::pow(10.f, v / 20.f),
                                std::memory_order_relaxed);
});
subMixDb.registerChangeCallback([&](float v){
    config.subMix.store(std::pow(10.f, v / 20.f),
                        std::memory_order_relaxed);
});
autoComp.registerChangeCallback([&](float v){
    config.focusAutoCompensation.store(v >= 0.5f,
                                       std::memory_order_relaxed);
    if (v >= 0.5f) spatializer.computeFocusCompensation(); // main thread only
});
paused.registerChangeCallback([&](float v){
    config.paused.store(v >= 0.5f, std::memory_order_relaxed);
});
```

The callbacks fire on the **ParameterServer's listener thread** (not the audio
thread). Writing to `std::atomic` fields with `relaxed` order from this thread
is safe — the audio callback polls them with `relaxed` reads (same contract as
all other atomics in the threading model, Phase 8).

> **Threading note:** `computeFocusCompensation()` is MAIN-THREAD-ONLY
> (allocates temporary DSP objects). Calling it from the ParameterServer
> listener thread violates this contract. Use a flag + pick it up in the main
> monitoring loop instead:
>
> ```cpp
> std::atomic<bool> pendingAutoComp{false};
> autoComp.registerChangeCallback([&](float v){
>     config.focusAutoCompensation.store(v >= 0.5f, std::memory_order_relaxed);
>     if (v >= 0.5f) pendingAutoComp.store(true, std::memory_order_release);
> });
> // In main monitoring loop:
> if (pendingAutoComp.exchange(false, std::memory_order_acquire))
>     spatializer.computeFocusCompensation();
> ```

---

## 8. Pause / Play — `config.paused` Atomic

`paused` is a new `std::atomic<bool>` in `RealtimeConfig` (add alongside
`playing` and `shouldExit`).

### Engine side (`RealtimeBackend.hpp` — `processBlock`)

```cpp
// At the top of processBlock(), before all other processing:
if (mConfig.paused.load(std::memory_order_relaxed)) {
    // Output silence — zero all output channels
    for (uint32_t ch = 0; ch < io.channelsOut(); ++ch)
        for (uint64_t fr = 0; fr < io.framesPerBuffer(); ++fr)
            io.out(ch, fr) = 0.0f;
    return;  // skip spatializer, streaming, etc.
}
```

This is RT-safe: one atomic load per callback, no locks, no allocation.
The `paused` flag is written by the ParameterServer callback thread
(relaxed store) and read by the audio thread (relaxed load) — same pattern
as `playing` and `masterGain`.

### State transitions

| GUI action  | OSC message              | Engine effect                            |
| ----------- | ------------------------ | ---------------------------------------- |
| Pause       | `/realtime/paused 1.0`   | audio callback outputs silence           |
| Play/Resume | `/realtime/paused 0.0`   | audio callback resumes normal processing |
| Stop        | SIGTERM / QProcess::kill | engine process exits (full teardown)     |
| Restart     | Stop + new Start         | new process launched with same config    |

> **Streaming state:** When paused, the streaming loader thread continues
> running and the playback position (`frameCounter`) continues advancing.
> This means "pause" is a **mute**, not a seek. When resumed, audio picks up
> at the current buffer position — no gap or seek is performed.
> True pause-at-position (freezing `frameCounter`) is a future improvement.

---

## 9. CMakeLists.txt — No Changes Required

The `al` CMake target already links the full AlloLib library, which includes
the UI subsystem (`al::Parameter`, `al::ParameterServer`, etc.). The
`al/ui/al_ParameterServer.hpp` header is already in the include path via:

```cmake
target_include_directories(spatialroot_realtime PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../thirdparty/allolib/include
    ...
)
target_link_libraries(spatialroot_realtime al Gamma)
```

**No new CMake changes needed.** Just add the `#include` directives to
`main.cpp`.

---

## 10. Python / GUI OSC Sender (`python-osc`)

The GUI uses `python-osc` (`pip install python-osc`) to send OSC messages to
the engine's `ParameterServer`.

```python
from pythonosc.udp_client import SimpleUDPClient

OSC_PORT = 9009
_osc = SimpleUDPClient("127.0.0.1", OSC_PORT)

# Send a parameter update:
_osc.send_message("/realtime/gain", 0.7)
_osc.send_message("/realtime/focus", 2.0)
_osc.send_message("/realtime/speaker_mix_db", -3.0)
_osc.send_message("/realtime/sub_mix_db", 0.0)
_osc.send_message("/realtime/auto_comp", 1.0)   # float, not int
_osc.send_message("/realtime/paused", 1.0)       # pause
_osc.send_message("/realtime/paused", 0.0)       # resume
```

### Debouncing (required in GUI sliders)

Slider `valueChanged` fires on every pixel drag — ~60 events/second. Send
after a short quiet period (30–60 ms) to avoid flooding the UDP socket:

```python
from PySide6.QtCore import QTimer

class DebouncedOSCSender:
    def __init__(self, address, client, delay_ms=40):
        self._address = address
        self._client = client
        self._timer = QTimer()
        self._timer.setSingleShot(True)
        self._timer.setInterval(delay_ms)
        self._pending = None
        self._timer.timeout.connect(self._flush)

    def send(self, value):
        self._pending = value
        self._timer.start()   # restarts if already running

    def _flush(self):
        if self._pending is not None:
            self._client.send_message(self._address, float(self._pending))
            self._pending = None
```

### Failure handling

If the engine is not running (port not bound), `SimpleUDPClient.send_message`
silently drops the packet — UDP has no ACK.

#### Critical: `LAUNCHING` ≠ `RUNNING` — wait for the ParameterServer sentinel

`runRealtime.py` spawns a Python process that does significant preprocessing
(scene load, WAV open, binary exec) **before** the C++ engine binds port 9009.
The QProcess `started` signal fires when the Python subprocess starts — not
when the `al::ParameterServer` is listening. Any OSC message sent between
`started` and the server being ready is **silently dropped**.

**The fix:** keep the runner in `LAUNCHING` state (which blocks all OSC sends)
and watch the engine's stdout for the sentinel line printed by `main.cpp`
immediately after `paramServer.serverRunning()` succeeds:

```
[Main] ParameterServer listening on 127.0.0.1:<port>
```

When this line is detected, transition to `RUNNING` and emit `engine_ready`.

```python
# In RealtimeRunner._on_stdout():
_ENGINE_READY_SENTINEL = "ParameterServer listening"

for line in data.splitlines():
    self.output.emit(line)
    if (self._state == RealtimeRunnerState.LAUNCHING
            and self._ENGINE_READY_SENTINEL in line):
        self._set_state(RealtimeRunnerState.RUNNING)
        self.engine_ready.emit()        # ← new Signal()
```

#### Flush current GUI values when engine becomes ready

Connect `engine_ready` to `controls_panel.flush_to_osc()` so whatever the
user set during loading is immediately applied to the engine:

```python
# In realtimeGUI.py:
runner.engine_ready.connect(controls_panel.flush_to_osc)
```

```python
# In RealtimeControlsPanel.flush_to_osc():
def flush_to_osc(self) -> None:
    """Push all current control values to the engine immediately."""
    self.osc_immediate.emit("/realtime/gain",           self._gain_row.value())
    self.osc_immediate.emit("/realtime/focus",          self._focus_row.value())
    self.osc_immediate.emit("/realtime/speaker_mix_db", self._spk_row.value())
    self.osc_immediate.emit("/realtime/sub_mix_db",     self._sub_row.value())
    v = 1.0 if self._auto_comp_check.isChecked() else 0.0
    self.osc_immediate.emit("/realtime/auto_comp", v)
```

#### State guard summary

| Runner state | OSC sends allowed? | Notes                                         |
| ------------ | ------------------ | --------------------------------------------- |
| `IDLE`       | ✗                  | No process running                            |
| `LAUNCHING`  | ✗                  | Process running but ParameterServer not bound |
| `RUNNING`    | ✓                  | ParameterServer confirmed listening           |
| `PAUSED`     | ✓                  | Engine muted; server still listening          |
| `EXITED`     | ✗                  | Process gone                                  |
| `ERROR`      | ✗                  | Process gone                                  |
