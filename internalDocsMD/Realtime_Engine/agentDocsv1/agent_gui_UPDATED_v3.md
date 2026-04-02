# GUI Agent — Phase 10 (PySide6, Dedicated Real-Time GUI + Runtime Controls)

> **Implementation Status: ✅ Phase 10 Complete (Feb 2026)**
>
> **Current reality:** real-time C++ engine is complete through **Phase 8** (backend adapter, streaming,
> pose/control, DBAP spatializer, ADM direct streaming, compensation/gain, output remap,
> threading/safety audit). Phase 9 covered project init/config updates. **Phase 10 is GUI integration.**

> **Polish Updates (Feb 2026):** Added default speaker layout dropdown (AlloSphere/Translab) and default source folder (sourceData).

---

## Authoritative Decisions (locked)

| #   | Decision                                                                                                                                                               |
| --- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **Framework**: PySide6 — do **not** switch to ImGui.                                                                                                                   |
| 2   | **No bloat of existing GUI**: all realtime GUI code lives under `gui/realtimeGUI/`.                                                                                    |
| 3   | **Launcher**: `realtimeMain.py` at the project root — standalone script, its own window (NOT a tab in `gui/main.py`).                                                  |
| 4   | **Process launch**: `runRealtime.py` invoked via `QProcess` (mirrors offline pipeline pattern).                                                                        |
| 5   | **IPC / runtime controls**: AlloLib `al::Parameter` + `al::ParameterServer` (OSC) on port **9009**. GUI sends via `python-osc`. See `allolib_parameters_reference.md`. |
| 6   | **Pause/Play**: C++ engine changes **in-scope for this task** (add `config.paused` atomic + callback silence branch + ParameterServer wiring).                         |
| 7   | **`--remap` passthrough**: add to `runRealtime.py` so the GUI can pass the remap CSV path to the C++ engine.                                                           |
| 8   | **Stylesheet**: reuse `gui/styles.qss` (same light mode).                                                                                                              |
| 9   | **Scene visualization**: deferred (not in Phase 10).                                                                                                                   |
| 10  | **`python-osc` package**: required; add to `requirements.txt`.                                                                                                         |

---

## 0. Directory / File Layout

```
gui/realtimeGUI/                   ← new, self-contained
    __init__.py
    realtimeGUI.py                 ← main window (QWidget + layout)
    realtime_runner.py             ← RealtimeRunner (QProcess wrapper + OSC sender)
    realtime_panels/
        __init__.py
        RealtimeInputPanel.py      ← source, layout, remap, buffer size, scan toggle
        RealtimeControlsPanel.py   ← live sliders: gain, focus, speaker mix, sub mix, auto comp
        RealtimeLogPanel.py        ← live stdout/stderr console
        RealtimeTransportPanel.py  ← Start/Stop/Kill/Restart/Pause/Play + status pill

realtimeMain.py                    ← project-root launcher (new file, separate from gui/main.py)
```

**Existing code that must not be modified:**

- `gui/main.py`, `gui/pipeline_runner.py`, all existing widgets
- `gui/styles.qss` (read-only reference — both GUIs load it)

**Rule:** Keep realtime GUI code self-contained inside `gui/realtimeGUI/`. Default is **no reuse** from existing `gui/` code if it increases coupling.

---

## 1. Responsibilities

### 1.1 Launch-time configuration

Collect inputs and build the `runRealtime.py` command:

| Input                                 | Notes                                                              |
| ------------------------------------- | ------------------------------------------------------------------ |
| Source (ADM WAV or LUSID package dir) | Auto-detected by path type — no mode dropdown needed               |
| Speaker layout JSON                   | Required                                                           |
| Remap CSV                             | Optional (`--remap`); passed through `runRealtime.py` → C++ engine |
| Buffer size                           | Dropdown: 64 / 128 / 256 / **512** / 1024                          |
| Scan audio                            | Checkbox, default OFF (ADM path only; adds ~14s startup)           |

### 1.2 Runtime controls (live via OSC / ParameterServer)

All five live parameters update the running C++ engine via OSC (`al::ParameterServer`, port 9009).
See `allolib_parameters_reference.md` for the full C++ + Python implementation.

| Control              | OSC address                | Range     | Default |
| -------------------- | -------------------------- | --------- | ------- |
| Master Gain          | `/realtime/gain`           | 0.0 – 1.0 | 0.5     |
| DBAP Focus           | `/realtime/focus`          | 0.2 – 5.0 | 1.5     |
| Loudspeaker Mix (dB) | `/realtime/speaker_mix_db` | -10 – +10 | 0.0     |
| Sub Mix (dB)         | `/realtime/sub_mix_db`     | -10 – +10 | 0.0     |
| Auto Compensation    | `/realtime/auto_comp`      | 0 / 1     | 0       |

Controls are **enabled while Running or Paused** (OSC accepted while paused — updates take effect on resume).
Disabled while Idle / Launching / Exited.
Debounce: 40 ms quiet period before sending (see `allolib_parameters_reference.md §10`).

### 1.3 Transport controls

| Control          | Action                                                   |
| ---------------- | -------------------------------------------------------- |
| **Start**        | Build command from current inputs, launch via `QProcess` |
| **Stop**         | SIGTERM → wait 3s → SIGKILL if needed                    |
| **Kill**         | Immediate SIGKILL                                        |
| **Restart**      | Stop (graceful) → Start with same config                 |
| **Pause**        | Send `/realtime/paused 1.0` via OSC                      |
| **Play**         | Send `/realtime/paused 0.0` via OSC                      |
| **Copy Command** | Copy last-launched CLI string to clipboard               |

Pause/Play are process-independent — toggle audio via OSC without stopping the process.

---

## 2. UI Spec

### 2.1 Input Panel (`RealtimeInputPanel`)

```
[Source]     [Browse WAV / Dir]  [text field]        "Detected: ADM / LUSID package"
[Layout]     [AlloSphere ▼]      [Browse JSON]       [text field]  (dropdown + browse)
[Remap CSV]  [None ▼]            [Browse CSV]        [text field]  (dropdown + browse, optional)
[Buffer]     [64|128|256|512|1024 dropdown]
[Scan Audio] [checkbox, default OFF — greyed when LUSID detected]
```

Layout dropdown defaults: `AlloSphere` → `spatial_engine/speaker_layouts/allosphere_layout.json`, `Translab` → `spatial_engine/speaker_layouts/translab-sono-layout.json`. Browse button allows custom layouts.

Remap dropdown defaults: `None` (no remapping), `Allosphere Example` → `spatial_engine/remaping/exampleRemap.csv`. Browse button allows custom remap CSVs.

### 2.2 Transport Panel (`RealtimeTransportPanel`)

```
[Start]  [Stop]  [Kill]  [Restart]      Status: ● Idle
[Pause]  [Play]                         OSC port: 9009
[Copy Command]
```

Status pill values: `Idle` / `Launching` / `Running` / `Paused` / `Exited(N)` / `Error`

The "Paused" pill state is **GUI-side only** — reflects whether `/realtime/paused 1.0` was sent,
not a confirmed engine acknowledgement.

### 2.3 Controls Panel (`RealtimeControlsPanel`)

```
Master Gain    [slider 0.0–1.0]    [spinbox]
DBAP Focus     [slider 0.2–5.0]    [spinbox]
Speaker Mix dB [slider -10–+10]    [spinbox]
Sub Mix dB     [slider -10–+10]    [spinbox]
Auto Comp      [checkbox]
```

All controls disabled when engine is Idle / Launching / Exited.
On every Start, reset all controls to defaults (§7) — engine always starts fresh.

### 2.4 Log Panel (`RealtimeLogPanel`)

- Scrolling `QPlainTextEdit` (monospace, read-only)
- Capped at 2000 lines — oldest lines dropped
- **Clear** button
- Optional search / filter field

---

## 3. Process Model: `QProcess` + `RealtimeRunner`

### 3.1 `RealtimeRunner` class

Implement in `gui/realtimeGUI/realtime_runner.py`.

```python
class RealtimeRunnerState(Enum):
    IDLE      = "Idle"
    LAUNCHING = "Launching"
    RUNNING   = "Running"
    PAUSED    = "Paused"    # UI-side only
    EXITED    = "Exited"
    ERROR     = "Error"

class RealtimeRunner(QObject):
    output        = Signal(str)   # stdout/stderr line
    state_changed = Signal(str)   # new state name
    finished      = Signal(int)   # exit code

    def start(self, cfg: RealtimeConfig): ...
    def stop_graceful(self): ...   # SIGTERM → wait 3s → kill
    def kill(self): ...
    def restart(self): ...
    def send_osc(self, address: str, value: float): ...
    def pause(self): self.send_osc("/realtime/paused", 1.0)
    def play(self):  self.send_osc("/realtime/paused", 0.0)
```

**Do not reuse** `PipelineRunner`. `RealtimeRunner` is a clean, independent class.

### 3.2 `RealtimeConfig` dataclass

```python
@dataclass
class RealtimeConfig:
    source_path:    str
    speaker_layout: str
    remap_csv:      Optional[str] = None
    buffer_size:    int   = 512
    scan_audio:     bool  = False
    master_gain:    float = 0.5
    dbap_focus:     float = 1.5
    osc_port:       int   = 9009
```

### 3.3 Command building

`RealtimeRunner.start()` invokes `runRealtime.py` (repo root, `-u` unbuffered):

```
python -u runRealtime.py <source> <layout> <gain> <focus> <buffersize>
    [--remap <csv>]
    [--scan_audio]
    [--osc_port 9009]
```

### 3.4 Stop sequence

```python
def stop_graceful(self):
    self.proc.terminate()                   # SIGTERM
    if not self.proc.waitForFinished(3000):
        self.proc.kill()                    # SIGKILL after 3s
```

---

## 4. Mode Detection (ADM vs LUSID)

Auto-detect using the same logic as `runRealtime.py::checkSourceType()`:

- Path ends in `.wav` → ADM
- Path is a directory containing `scene.lusid.json` → LUSID package
- Otherwise → inline error, Start blocked

Display a small hint label below the source field:
`"Detected: ADM WAV"` / `"Detected: LUSID package"` / `"⚠ Unrecognized — select a .wav or LUSID package dir"`

The Scan Audio checkbox is only relevant for ADM sources; grey it out when LUSID is detected.

---

## 5. IPC — AlloLib ParameterServer / OSC

See `allolib_parameters_reference.md` for the complete C++ and Python implementation.

**Summary:**

- Engine starts `al::ParameterServer` on `127.0.0.1:9009` (default, configurable via `--osc_port`).
- Engine prints `"[ParameterServer] Listening on 127.0.0.1:9009"` at startup.
- GUI creates `pythonosc.udp_client.SimpleUDPClient("127.0.0.1", 9009)` in `RealtimeRunner`.
- Sends are debounced (40 ms) from slider `valueChanged` signals.
- Dropped packets (engine not running) are silent — UDP has no ACK.
  GUI guards by only sending when state is Running or Paused.

**Port conflict handling:**

- Engine: if `paramServer.serverRunning()` is false after construction → print error + `config.shouldExit = true`.
- GUI: parse stdout for bind-fail message → show inline error ("Port 9009 in use — check for other running instances").

---

## 6. C++ Engine Changes Required (in-scope for Phase 10)

See `allolib_parameters_reference.md` for code patterns.

### 6.1 `RealtimeTypes.hpp`

- Add `std::atomic<bool> paused{false}` alongside `playing` and `shouldExit`.
- Add threading doc comment (same pattern as `playing`).

### 6.2 `main.cpp`

- Add `#include "al/ui/al_Parameter.hpp"` and `#include "al/ui/al_ParameterServer.hpp"`.
- Declare six `al::Parameter` / `al::ParameterBool` objects (gain, focus, speakerMixDb, subMixDb, autoComp, paused) with correct ranges and defaults.
- Create `al::ParameterServer paramServer{"127.0.0.1", osc_port}`.
- Register all parameters with `paramServer`.
- Add `registerChangeCallback` for each → writes to `RealtimeConfig` atomics:
  - dB → linear conversion for mix params
  - `pendingAutoComp` flag for `autoComp` (consumed in main monitoring loop, never in callback)
  - direct bool for `paused`
- Add `--osc_port <int>` CLI flag (default 9009).
- Call `paramServer.print()` on startup.
- Add `paramServer.stopServer()` in shutdown sequence (before `streaming.shutdown()`).
- Add `pendingAutoComp` flag check in main monitoring loop (calls `spatializer.computeFocusCompensation()` on main thread — never in ParameterServer callback or audio callback).

### 6.3 `RealtimeBackend.hpp` — `processBlock()`

Add pause guard at the very top of `processBlock()`:

```cpp
if (mConfig.paused.load(std::memory_order_relaxed)) {
    for (uint32_t ch = 0; ch < io.channelsOut(); ++ch)
        for (uint64_t fr = 0; fr < io.framesPerBuffer(); ++fr)
            io.out(ch, fr) = 0.0f;
    return;
}
```

RT-safe: one atomic load per callback, no locks, no allocation.
`paused` written by ParameterServer listener thread (relaxed store), read by audio thread (relaxed load)
— same contract as `playing` and `masterGain` per the Phase 8 threading model.

### 6.4 `runRealtime.py`

- Add `remap_csv=None` and `osc_port=9009` keyword args to `_launch_realtime_engine()`.
- Pass `--remap <path>` to `cmd` when `remap_csv` is set.
- Pass `--osc_port <port>` to `cmd`.
- Add `[--remap <csv>]` and `[--osc_port <port>]` to CLI argument parsing and help text.

---

## 7. Defaults (GUI must match engine startup defaults)

| Parameter           | Default  |
| ------------------- | -------- |
| `masterGain`        | **0.5**  |
| `focus`             | **1.5**  |
| `buffer_size`       | **512**  |
| `loudspeakerMix_db` | **0 dB** |
| `subMix_db`         | **0 dB** |
| `auto_compensation` | **OFF**  |
| `scan_audio`        | **OFF**  |
| `remap_csv`         | none     |
| `osc_port`          | **9009** |

On every Start, reset all runtime control widgets to these defaults.

---

## 8. `realtimeMain.py` (project-root launcher)

```python
#!/usr/bin/env python3
"""realtimeMain.py — Launch the spatialroot Real-Time GUI as a standalone window."""
import sys
from pathlib import Path
from PySide6.QtWidgets import QApplication
from gui.realtimeGUI.realtimeGUI import RealtimeWindow

def main():
    app = QApplication(sys.argv)
    here = Path(__file__).resolve().parent
    qss = here / "gui" / "styles.qss"
    if qss.exists():
        app.setStyleSheet(qss.read_text(encoding="utf-8"))
    win = RealtimeWindow(repo_root=str(here))
    win.show()
    win.activateWindow()
    sys.exit(app.exec())

if __name__ == "__main__":
    main()
```

---

## 9. Testing Checklist

### Launch

- [ ] ADM WAV + layout → Start → audio plays
- [ ] ADM WAV + scan_audio ON → Start → longer startup but succeeds
- [ ] LUSID package + layout → Start → audio plays
- [ ] Remap CSV → Start → verify routing changes
- [ ] Missing layout → Start blocked with inline error
- [ ] Missing source → Start blocked with inline error
- [ ] LUSID detected → Scan Audio checkbox greyed out

### Runtime controls (IPC)

- [ ] While Running, move gain slider → immediate audible change
- [ ] While Running, move focus slider → audible spatial change
- [ ] While Running, move speaker mix → level change on mains
- [ ] While Running, move sub mix → level change on sub
- [ ] Toggle auto comp ON, change focus → loudspeaker mix updates, stays in ±10 dB
- [ ] Controls reset to defaults on next Start

### Transport

- [ ] Pause → audio goes silent, process stays alive
- [ ] Play (after Pause) → audio resumes at current buffer position
- [ ] Stop → process exits cleanly (exit 0), controls disabled
- [ ] Kill → process terminates immediately
- [ ] Restart → clean teardown + relaunch, no orphan processes
- [ ] Copy Command → clipboard contains a valid CLI invocation

### Failure / edge cases

- [ ] Engine crash → exit code shown in status pill, logs preserved
- [ ] Port 9009 in use → engine fails to start, error visible in log
- [ ] Clicking Stop twice → second click is a no-op
- [ ] Rapid slider movement → debounce fires once per 40 ms

---

## 10. Backlog / Deferred

- Scene visualization (3D grid + particles) — after Phase 10 stability
- CPU / xrun meters — after engine emits machine-readable metrics
- Configurable OSC port via GUI field (currently set at launch via `--osc_port`)
- True pause-at-position (freeze `frameCounter`) — currently "pause = mute"
