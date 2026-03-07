"""
transcode_runner.py — Phase 5: QProcess wrapper for cult-transcoder.

Resolves the binary path from project_root / cult_transcoder/build/cult-transcoder
(same convention as runRealtime.py — avoids future hardcoding issues).

Signals
-------
output(str)     — streaming stdout/stderr text
finished(int, str)  — exit_code, report_path (may be empty if not known)
started()
"""

from __future__ import annotations

import os
from pathlib import Path
from typing import Optional

from PySide6.QtCore import QObject, QProcess, Signal


def resolve_cult_binary(repo_root: str) -> Path:
    """
    Return the cult-transcoder binary path.
    Mirrors the convention used in runRealtime.py:
        project_root / cult_transcoder / build / cult-transcoder
    Windows adds .exe automatically (QProcess handles it), but we still
    check the explicit .exe path on Windows.
    """
    root = Path(repo_root)
    base = root / "cult_transcoder" / "build" / "cult-transcoder"
    if os.name == "nt":
        win = base.with_suffix(".exe")
        return win if win.exists() else base
    return base


class TranscodeRunner(QObject):
    """
    Drives cult-transcoder for a single transcode job.

    Usage
    -----
        runner = TranscodeRunner(repo_root, parent=self)
        runner.output.connect(panel.append_log)
        runner.finished.connect(self._on_transcode_done)
        runner.run(in_path, in_format, out_path, lfe_mode)
    """

    output = Signal(str)           # streaming text
    finished = Signal(int, str)    # exit_code, report_path
    started = Signal()

    def __init__(self, repo_root: str, parent=None):
        super().__init__(parent)
        self._repo_root = repo_root
        self._report_path: Optional[str] = None

        self._proc = QProcess(self)
        self._proc.setProcessChannelMode(QProcess.MergedChannels)
        self._proc.readyReadStandardOutput.connect(self._on_ready_read)
        self._proc.started.connect(self.started)
        self._proc.finished.connect(self._on_finished)

    # ── Public API ─────────────────────────────────────────────────────

    def is_running(self) -> bool:
        return self._proc.state() != QProcess.NotRunning

    def stop(self) -> None:
        if self.is_running():
            self._proc.terminate()

    def run(
        self,
        in_path: str,
        in_format: str,
        out_path: str,
        lfe_mode: str = "hardcoded",
        report_path: Optional[str] = None,
    ) -> None:
        if self.is_running():
            return

        binary = resolve_cult_binary(self._repo_root)
        if not binary.exists():
            msg = (
                f"cult-transcoder binary not found at {binary}\n"
                f"Run: cmake --build cult_transcoder/build"
            )
            self.output.emit(msg)
            self.finished.emit(127, "")
            return

        # Default report path: <out>.report.json
        if report_path is None:
            report_path = out_path + ".report.json"
        self._report_path = report_path

        args = [
            "transcode",
            "--in", in_path,
            "--in-format", in_format,
            "--out", out_path,
            "--out-format", "lusid_json",
            "--report", report_path,
            "--lfe-mode", lfe_mode,
        ]

        self._proc.setWorkingDirectory(self._repo_root)
        self._proc.start(str(binary), args)

    # ── Private ────────────────────────────────────────────────────────

    def _on_ready_read(self) -> None:
        data = bytes(self._proc.readAllStandardOutput()).decode("utf-8", errors="replace")
        if data:
            self.output.emit(data)

    def _on_finished(self, exit_code: int, _status) -> None:
        self.finished.emit(exit_code, self._report_path or "")
