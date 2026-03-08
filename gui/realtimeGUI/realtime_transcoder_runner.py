"""
realtime_transcoder_runner.py — QProcess wrapper for the cult-transcoder
`transcode` sub-command.

Mirrors the RealtimeRunner pattern:
  - Resolves the binary the same way runRealtime.py does
    (project_root / "cult_transcoder" / "build" / "cult-transcoder")
  - Streams stdout + stderr to the `output` signal (stderr prefixed "[stderr] ")
  - Emits `finished(exit_code, report_path)` on completion
  - Emits `started()` once the process has actually launched

Typical usage (from RealtimeWindow)
------------------------------------
    self._tc_runner = RealtimeTranscoderRunner(repo_root=repo_root, parent=self)
    self._tc_runner.output.connect(self._transcode_panel.append_line)
    self._tc_runner.started.connect(lambda: self._transcode_panel.set_running(True))
    self._tc_runner.finished.connect(self._on_transcode_finished)

    self._tc_runner.run(
        in_path="source.wav",
        in_format="adm_wav",
        out_path="out/scene.lusid.json",
        lfe_mode="hardcoded",
    )

Phase 5 — GUI Transcoding Tab.
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QObject, QProcess, Signal


class RealtimeTranscoderRunner(QObject):
    """
    Thin QProcess wrapper for `cult-transcoder transcode`.

    Signals
    -------
    output(str)
        A single line of text from stdout or stderr.
    started()
        The subprocess has been launched successfully.
    finished(int, str)
        exit_code, report_path (empty string if not resolved).
    """

    output   = Signal(str)
    started  = Signal()
    finished = Signal(int, str)

    def __init__(self, repo_root: str = ".", parent: Optional[QObject] = None) -> None:
        super().__init__(parent)
        self._repo_root = str(repo_root)
        self._proc: Optional[QProcess] = None
        self._report_path: str = ""
        self._pending_args: list[str] = []

    # ── Public API ────────────────────────────────────────────────────────

    @property
    def is_running(self) -> bool:
        return (
            self._proc is not None
            and self._proc.state() != QProcess.ProcessState.NotRunning
        )

    def run(
        self,
        in_path: str,
        in_format: str,
        out_path: str,
        lfe_mode: str = "hardcoded",
        report_path: Optional[str] = None,
    ) -> None:
        """
        Launch `cult-transcoder transcode …`.

        Parameters
        ----------
        in_path    : str  — absolute or repo-relative input file / directory
        in_format  : str  — "adm_wav" | "adm_xml" | "lusid_json"
        out_path   : str  — destination .lusid.json path (may be empty → auto)
        lfe_mode   : str  — "hardcoded" | "speaker-label"
        report_path: str  — explicit report JSON path; auto-derived when None
        """
        if self.is_running:
            self.output.emit("[GUI] Transcoder is already running — please wait.")
            return

        binary = self._resolve_binary()
        if not binary.exists():
            self.output.emit(
                f"[error] cult-transcoder binary not found at: {binary}\n"
                "Run `cmake --build cult_transcoder/build` first."
            )
            self.finished.emit(1, "")
            return

        # ── Auto-derive output path if not provided ──────────────────
        if not out_path:
            in_stem = Path(in_path).stem
            # Strip .lusid suffix if the stem already contains it
            if in_stem.endswith(".lusid"):
                in_stem = in_stem[: -len(".lusid")]
            out_path = str(Path(in_path).parent / f"{in_stem}.lusid.json")

        # ── Auto-derive report path ───────────────────────────────────
        if not report_path:
            out_stem = Path(out_path).stem
            if out_stem.endswith(".lusid"):
                out_stem = out_stem[: -len(".lusid")]
            report_path = str(Path(out_path).parent / f"{out_stem}_report.json")

        self._report_path = report_path

        args = self._build_args(in_path, in_format, out_path, lfe_mode, report_path)
        self._pending_args = args

        self.output.emit(
            f"[GUI] Running: {binary} {' '.join(args)}"
        )

        proc = QProcess(self)
        proc.setWorkingDirectory(self._repo_root)
        # Ensure output directories exist for transcoder output
        os.makedirs(os.path.join(self._repo_root, "processedData"), exist_ok=True)
        os.makedirs(os.path.join(self._repo_root, "processedData", "stageForRender"), exist_ok=True)
        proc.readyReadStandardOutput.connect(self._on_stdout)
        proc.readyReadStandardError.connect(self._on_stderr)
        proc.finished.connect(self._on_finished)
        proc.started.connect(self.started)

        self._proc = proc
        proc.start(str(binary), args)

        if not proc.waitForStarted(3000):
            err = proc.errorString()
            self.output.emit(f"[error] Failed to start cult-transcoder: {err}")
            self.finished.emit(1, "")
            self._proc = None

    def abort(self) -> None:
        """Kill the running transcode process (if any)."""
        if self._proc and self._proc.state() != QProcess.ProcessState.NotRunning:
            self._proc.kill()

    # ── Internal ─────────────────────────────────────────────────────────

    def _resolve_binary(self) -> Path:
        root = Path(self._repo_root)
        base = root / "cult_transcoder" / "build" / "cult-transcoder"
        if os.name == "nt":
            win = base.with_suffix(".exe")
            return win if win.exists() else base
        return base

    def _build_args(
        self,
        in_path: str,
        in_format: str,
        out_path: str,
        lfe_mode: str,
        report_path: str,
    ) -> list[str]:
        return [
            "transcode",
            "--in",         in_path,
            "--in-format",  in_format,
            "--out",        out_path,
            "--out-format", "lusid_json",
            "--report",     report_path,
            "--lfe-mode",   lfe_mode,
        ]

    def _on_stdout(self) -> None:
        if not self._proc:
            return
        raw = self._proc.readAllStandardOutput().data()
        for line in raw.decode("utf-8", errors="replace").splitlines():
            if line.strip():
                self.output.emit(line)

    def _on_stderr(self) -> None:
        if not self._proc:
            return
        raw = self._proc.readAllStandardError().data()
        for line in raw.decode("utf-8", errors="replace").splitlines():
            if line.strip():
                self.output.emit(f"[stderr] {line}")

    def _on_finished(self, exit_code: int, exit_status: QProcess.ExitStatus) -> None:
        # Drain any remaining output
        self._on_stdout()
        self._on_stderr()

        rp = self._report_path if Path(self._report_path).exists() else ""
        if exit_code == 0:
            self.output.emit(f"[ok] Transcode finished (exit 0). Report: {rp or '(none)'}")
        else:
            self.output.emit(f"[error] Transcode exited with code {exit_code}.")

        self.finished.emit(exit_code, rp)
        self._proc = None
