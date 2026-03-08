"""
realtime_runner.py — RealtimeRunner: QProcess wrapper + OSC sender for the
spatialroot Real-Time Spatial Audio Engine.

Responsible for:
  - Building and launching the runRealtime.py command via QProcess.
  - Forwarding stdout/stderr lines as signals.
  - Managing state transitions (IDLE → LAUNCHING → RUNNING → EXITED / ERROR).
  - Sending OSC messages to the engine's al::ParameterServer on port 9009.
  - Debouncing rapid slider sends with a 40 ms quiet-period timer.
  - Graceful stop (SIGTERM → 3s wait → SIGKILL).

Threading model:
  - All QProcess interaction and signal emission happens on the Qt main thread.
  - `SimpleUDPClient.send_message()` is UDP (fire-and-forget); it does not block.
  - No background threads are created here.

Phase 10 — GUI Agent.
"""

from __future__ import annotations

import os
import sys
from dataclasses import dataclass, field
from enum import Enum
from typing import Optional

from PySide6.QtCore import QObject, QProcess, Signal, QTimer
from PySide6.QtWidgets import QApplication

try:
    from pythonosc.udp_client import SimpleUDPClient  # type: ignore
    _PYTHONOSC_AVAILABLE = True
except ImportError:
    _PYTHONOSC_AVAILABLE = False


# ─────────────────────────────────────────────────────────────────────────────
# Config dataclass
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class RealtimeConfig:
    """All parameters required to launch the real-time engine."""
    source_path:    str
    speaker_layout: str
    remap_csv:      Optional[str] = None
    buffer_size:    int   = 512
    # REMOVED (Phase 3 — 2026-03-04): scan_audio is no longer a parameter.
    # cult-transcoder handles BW64 axml extraction internally; all channels
    # are assumed active. The --scan_audio CLI flag in runRealtime.py is removed.
    # scan_audio:     bool  = False   ← removed
    master_gain:    float = 0.5
    dbap_focus:     float = 1.5
    osc_port:       int   = 9009


# ─────────────────────────────────────────────────────────────────────────────
# State enum
# ─────────────────────────────────────────────────────────────────────────────

class RealtimeRunnerState(Enum):
    IDLE      = "Idle"
    LAUNCHING = "Launching"
    RUNNING   = "Running"
    PAUSED    = "Paused"   # GUI-side only — reflects last OSC send, not confirmed
    EXITED    = "Exited"
    ERROR     = "Error"


# ─────────────────────────────────────────────────────────────────────────────
# Debounced OSC sender
# ─────────────────────────────────────────────────────────────────────────────

class DebouncedOSCSender(QObject):
    """
    Queues an OSC send and fires it after a 40 ms quiet period.

    Each call to `schedule(address, value)` restarts the timer, so rapid slider
    moves collapse into a single send 40 ms after the last move.

    Usage:
        debouncer = DebouncedOSCSender(parent=runner)
        debouncer.set_client(SimpleUDPClient("127.0.0.1", 9009))
        debouncer.schedule("/realtime/gain", 0.75)
    """

    DEBOUNCE_MS = 40

    def __init__(self, parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._client: Optional[SimpleUDPClient] = None
        self._pending_address: str = ""
        self._pending_value: float = 0.0
        self._timer = QTimer(self)
        self._timer.setSingleShot(True)
        self._timer.timeout.connect(self._fire)

    def set_client(self, client: Optional[SimpleUDPClient]) -> None:
        self._client = client

    def schedule(self, address: str, value: float) -> None:
        """Schedule an OSC send, debounced by DEBOUNCE_MS."""
        self._pending_address = address
        self._pending_value = value
        self._timer.start(self.DEBOUNCE_MS)

    def send_now(self, address: str, value: float) -> None:
        """Send immediately (no debounce) — used for discrete toggles."""
        if self._client is not None:
            try:
                self._client.send_message(address, value)
            except Exception:
                pass  # UDP: fire-and-forget; dropped packets are silent

    def _fire(self) -> None:
        if self._client is not None and self._pending_address:
            try:
                self._client.send_message(self._pending_address, self._pending_value)
            except Exception:
                pass


# ─────────────────────────────────────────────────────────────────────────────
# RealtimeRunner
# ─────────────────────────────────────────────────────────────────────────────

class RealtimeRunner(QObject):
    """
    Manages the real-time engine subprocess and OSC communication.

    Signals
    -------
    output(str)           Emitted for each stdout/stderr line received.
    state_changed(str)    Emitted whenever the runner state transitions.
                          Value is `RealtimeRunnerState.value` (display string).
    finished(int)         Emitted when the process exits; carries the exit code.
    """

    output        = Signal(str)
    state_changed = Signal(str)
    finished      = Signal(int)
    # Emitted once the C++ ParameterServer is confirmed listening (stdout probe).
    # Connect this to flush current GUI control values to the engine.
    engine_ready  = Signal()

    def __init__(self, repo_root: str = ".", parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._repo_root = repo_root
        self._proc: Optional[QProcess] = None
        self._state = RealtimeRunnerState.IDLE
        self._last_cmd: str = ""
        self._config: Optional[RealtimeConfig] = None
        self._osc_client: Optional[SimpleUDPClient] = None
        self._debouncer = DebouncedOSCSender(parent=self)

    # ── Public API ──────────────────────────────────────────────────────

    @property
    def state(self) -> RealtimeRunnerState:
        return self._state

    @property
    def last_command(self) -> str:
        return self._last_cmd

    def start(self, cfg: RealtimeConfig) -> None:
        """Build command from cfg and launch via QProcess."""
        if self._state not in (RealtimeRunnerState.IDLE,
                               RealtimeRunnerState.EXITED,
                               RealtimeRunnerState.ERROR):
            self.output.emit("[Runner] Already running — stop first.")
            return

        self._config = cfg
        args = self._build_args(cfg)
        program = sys.executable
        self._last_cmd = f"python -u runRealtime.py {' '.join(args)}"

        # Create OSC client
        if _PYTHONOSC_AVAILABLE:
            try:
                self._osc_client = SimpleUDPClient("127.0.0.1", cfg.osc_port)
                self._debouncer.set_client(self._osc_client)
            except Exception as e:
                self.output.emit(f"[Runner] Warning: could not create OSC client: {e}")
                self._osc_client = None
                self._debouncer.set_client(None)
        else:
            self.output.emit(
                "[Runner] Warning: python-osc not installed — "
                "live OSC controls will not work. "
                "Install with: pip install python-osc"
            )

        # Set up QProcess
        self._proc = QProcess(self)
        self._proc.setWorkingDirectory(self._repo_root)
        # Ensure processedData directory exists, similar to offline pipeline
        os.makedirs(os.path.join(self._repo_root, "processedData"), exist_ok=True)
        # Ensure processedData/stageForRender directory exists for transcoder output
        os.makedirs(os.path.join(self._repo_root, "processedData", "stageForRender"), exist_ok=True)
        self._proc.readyReadStandardOutput.connect(self._on_stdout)
        self._proc.readyReadStandardError.connect(self._on_stderr)
        self._proc.finished.connect(self._on_finished)
        self._proc.started.connect(self._on_started)

        self._set_state(RealtimeRunnerState.LAUNCHING)
        self._proc.start(program, ["-u", "runRealtime.py"] + args)

        if not self._proc.waitForStarted(3000):
            self._set_state(RealtimeRunnerState.ERROR)
            self.output.emit("[Runner] ERROR: Process failed to start within 3 seconds.")

    def stop_graceful(self) -> None:
        """SIGTERM → wait 3 s → SIGKILL."""
        if self._proc is None or self._state in (RealtimeRunnerState.IDLE,
                                                  RealtimeRunnerState.EXITED,
                                                  RealtimeRunnerState.ERROR):
            return
        self._proc.terminate()
        if not self._proc.waitForFinished(3000):
            self._proc.kill()

    def kill(self) -> None:
        """Immediate SIGKILL."""
        if self._proc is not None:
            self._proc.kill()

    def restart(self) -> None:
        """Graceful stop then re-start with the same config."""
        if self._config is None:
            self.output.emit("[Runner] No previous config to restart with.")
            return
        cfg = self._config
        self.stop_graceful()
        # Wait for the finished signal to fire before starting again.
        # We queue the start so the event loop processes the exit first.
        QTimer.singleShot(200, lambda: self.start(cfg))

    def send_osc(self, address: str, value: float) -> None:
        """Send an OSC message immediately (no debounce) if engine is active."""
        if self._state not in (RealtimeRunnerState.RUNNING,
                               RealtimeRunnerState.PAUSED):
            return
        self._debouncer.send_now(address, value)

    def schedule_osc(self, address: str, value: float) -> None:
        """Send an OSC message with 40 ms debounce — use for slider valueChanged."""
        if self._state not in (RealtimeRunnerState.RUNNING,
                               RealtimeRunnerState.PAUSED):
            return
        self._debouncer.schedule(address, value)

    def pause(self) -> None:
        """Send /realtime/paused 1.0 and update GUI state."""
        self.send_osc("/realtime/paused", 1.0)
        if self._state == RealtimeRunnerState.RUNNING:
            self._set_state(RealtimeRunnerState.PAUSED)

    def play(self) -> None:
        """Send /realtime/paused 0.0 and update GUI state."""
        self.send_osc("/realtime/paused", 0.0)
        if self._state == RealtimeRunnerState.PAUSED:
            self._set_state(RealtimeRunnerState.RUNNING)

    # ── Private helpers ──────────────────────────────────────────────────

    def _build_args(self, cfg: RealtimeConfig) -> list[str]:
        """Build the argument list for runRealtime.py."""
        args = [
            cfg.source_path,
            cfg.speaker_layout,
            str(cfg.master_gain),
            str(cfg.dbap_focus),
            str(cfg.buffer_size),
            "--osc_port", str(cfg.osc_port),
        ]
        if cfg.remap_csv:
            args += ["--remap", cfg.remap_csv]
        # REMOVED (Phase 3 — 2026-03-04): --scan_audio flag removed from runRealtime.py.
        # cult-transcoder owns BW64 extraction; scan_audio no longer exists.
        # if cfg.scan_audio:
        #     args.append("--scan_audio")
        return args

    def _set_state(self, new_state: RealtimeRunnerState) -> None:
        if self._state != new_state:
            self._state = new_state
            self.state_changed.emit(new_state.value)

    def _on_started(self) -> None:
        # Stay in LAUNCHING — we advance to RUNNING only when the C++ engine
        # confirms its ParameterServer is up (see _on_stdout probe below).
        self.output.emit("[Runner] Process started — waiting for ParameterServer…")

    # Sentinel printed by main.cpp right after paramServer.serverRunning() check
    _ENGINE_READY_SENTINEL = "ParameterServer listening"

    def _on_stdout(self) -> None:
        if self._proc is None:
            return
        data = bytes(self._proc.readAllStandardOutput()).decode("utf-8", errors="replace")
        for line in data.splitlines():
            self.output.emit(line)
            # Advance from LAUNCHING → RUNNING the moment the C++ ParameterServer
            # confirms it is bound and listening.  This ensures the OSC client
            # only starts sending once the UDP port is actually open.
            if (self._state == RealtimeRunnerState.LAUNCHING
                    and self._ENGINE_READY_SENTINEL in line):
                self._set_state(RealtimeRunnerState.RUNNING)
                self.engine_ready.emit()

    def _on_stderr(self) -> None:
        if self._proc is None:
            return
        data = bytes(self._proc.readAllStandardError()).decode("utf-8", errors="replace")
        for line in data.splitlines():
            self.output.emit(f"[stderr] {line}")

    def _on_finished(self, exit_code: int, exit_status: QProcess.ExitStatus) -> None:
        if exit_code == 0 or exit_code == -2 or exit_code == 130:
            self._set_state(RealtimeRunnerState.EXITED)
            self.output.emit(f"[Runner] Engine exited cleanly (code {exit_code}).")
        else:
            self._set_state(RealtimeRunnerState.ERROR)
            self.output.emit(f"[Runner] Engine exited with error code {exit_code}.")
        self._osc_client = None
        self._debouncer.set_client(None)
        self.finished.emit(exit_code)
