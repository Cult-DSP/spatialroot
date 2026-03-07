"""
RealtimeTranscodePanel.py — Offline-transcode panel for the Realtime GUI.

Wraps the cult-transcoder CLI (`transcode` sub-command) so users can convert
an ADM WAV / ADM XML file to a LUSID JSON package without leaving the GUI.

Controls exposed:
  - Input file (any type; macOS dir-first workaround for LUSID packages)
  - Auto-detected format hint
  - In-format override  (auto | adm_wav | adm_xml | lusid_json)
  - LFE mode           (hardcoded | speaker-label)
  - Output path
  - TRANSCODE button
  - Live log output (same HTML-colour pattern as RealtimeLogPanel)
  - Inline status label (idle → running → success / failure)
  - Open Report button (system `open` / `xdg-open` / `os.startfile`)

Panel follows the exact _card() / theme-token / Space Mono conventions used
by every other panel in the realtime GUI.

Phase 5 — GUI Transcoding Tab.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QScrollBar,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)


def _card() -> QFrame:
    f = QFrame()
    f.setObjectName("Card")
    return f


def _sep(theme: dict) -> QFrame:
    sep = QFrame()
    sep.setFrameShape(QFrame.Shape.HLine)
    sep.setStyleSheet(f"color: {theme['border_light']}; margin-bottom: 6px;")
    return sep


MAX_LOG_LINES = 2000


class RealtimeTranscodePanel(QWidget):
    """
    Offline transcoding panel.

    Signals
    -------
    run_requested(in_path, in_format, out_path, lfe_mode)
        Emitted when the user clicks TRANSCODE.
    open_report_requested(report_path)
        Emitted when the user clicks OPEN REPORT.
    """

    run_requested    = Signal(str, str, str, str)
    open_report_requested = Signal(str)

    def __init__(self, theme: dict = None, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        from .theme import DARK
        self._theme = theme or DARK
        self._report_path: Optional[str] = None
        self._log_line_count = 0
        self._build_ui()

    # ── Public API ────────────────────────────────────────────────────────

    def get_in_path(self) -> str:
        return self._in_edit.text().strip()

    def get_out_path(self) -> str:
        return self._out_edit.text().strip()

    def get_resolved_in_format(self) -> str:
        """Returns the effective --in-format value (never 'auto')."""
        selected = self._in_format_combo.currentData()
        if selected and selected != "auto":
            return selected
        # Try to infer from the path
        p = self.get_in_path()
        if p:
            pl = p.lower()
            if pl.endswith(".wav"):
                return "adm_wav"
            if pl.endswith(".xml"):
                return "adm_xml"
            if pl.endswith(".lusid.json") or pl.endswith(".json"):
                return "lusid_json"
        return "adm_wav"  # safe default

    def get_lfe_mode(self) -> str:
        return self._lfe_combo.currentData() or "hardcoded"

    def set_running(self, running: bool) -> None:
        """Lock / unlock UI while a transcode is in progress."""
        for w in (self._in_edit, self._in_btn,
                  self._in_format_combo,
                  self._out_edit, self._out_btn,
                  self._lfe_combo,
                  self._transcode_btn):
            w.setEnabled(not running)
        self._open_report_btn.setEnabled(False)
        if running:
            self._set_status("running", "Transcoding…")
        # log is always read-only; leave it enabled so user can scroll

    def set_finished(self, success: bool, report_path: Optional[str] = None) -> None:
        """Called by the window once the transcode process has exited."""
        for w in (self._in_edit, self._in_btn,
                  self._in_format_combo,
                  self._out_edit, self._out_btn,
                  self._lfe_combo,
                  self._transcode_btn):
            w.setEnabled(True)

        self._report_path = report_path
        if report_path and Path(report_path).exists():
            self._open_report_btn.setEnabled(True)
        else:
            self._open_report_btn.setEnabled(False)

        if success:
            self._set_status("success", "Transcode complete.")
        else:
            self._set_status("error", "Transcode failed — see log above.")

    def append_line(self, text: str) -> None:
        """Append a log line with HTML colour coding (same as RealtimeLogPanel)."""
        t = self._theme
        text = text.rstrip()

        if text.startswith("[error]") or text.startswith("[ERROR]") or "error" in text.lower():
            colour = t["red"]
        elif text.startswith("[warn") or text.startswith("[WARN"):
            colour = t["orange"]
        elif text.startswith("[ok]") or text.startswith("[OK]") or text.startswith("[done]"):
            colour = t["green"]
        elif text.startswith("[GUI]"):
            colour = t["muted"]
        else:
            colour = t["muted"]

        safe = (
            text.replace("&", "&amp;")
                .replace("<", "&lt;")
                .replace(">", "&gt;")
        )
        html = f'<span style="color:{colour};">{safe}</span>'
        self._log.appendHtml(html)

        self._log_line_count += 1
        if self._log_line_count > MAX_LOG_LINES:
            # Trim oldest lines from the document
            cursor = self._log.textCursor()
            cursor.movePosition(cursor.MoveOperation.Start)
            cursor.movePosition(
                cursor.MoveOperation.Down,
                cursor.MoveMode.KeepAnchor,
                self._log_line_count - MAX_LOG_LINES,
            )
            cursor.removeSelectedText()
            self._log_line_count = MAX_LOG_LINES

        # Auto-scroll to bottom
        sb: QScrollBar = self._log.verticalScrollBar()
        sb.setValue(sb.maximum())

    # ── UI construction ───────────────────────────────────────────────────

    def _build_ui(self) -> None:
        t = self._theme
        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(8)

        card = _card()
        layout = QVBoxLayout(card)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(8)

        # ── Section title ────────────────────────────────────────────
        title = QLabel("ADM → LUSID TRANSCODE")
        title.setObjectName("SectionTitle")
        title.setFont(QFont("Space Mono", 7))
        layout.addWidget(title)
        layout.addWidget(_sep(t))

        # ── Input file ───────────────────────────────────────────────
        lbl_in = self._row_label("Input File")
        layout.addWidget(lbl_in)

        in_row = QHBoxLayout()
        self._in_edit = QLineEdit()
        self._in_edit.setPlaceholderText("ADM WAV, ADM XML, or LUSID package directory…")
        self._in_btn = QPushButton("Browse")
        self._in_btn.setObjectName("FileButton")
        self._in_btn.setFont(QFont("Space Mono", 7))
        self._in_btn.setFixedWidth(80)
        in_row.addWidget(self._in_edit)
        in_row.addWidget(self._in_btn)
        layout.addLayout(in_row)

        # Hint label (auto-detection)
        self._in_hint = QLabel("")
        self._in_hint.setObjectName("Muted")
        self._in_hint.setFont(QFont("Space Mono", 7))
        layout.addWidget(self._in_hint)

        # ── In-format + LFE mode row ─────────────────────────────────
        opts_row = QHBoxLayout()
        opts_row.setSpacing(16)

        # In-format override
        opts_row.addWidget(self._row_label("In-Format Override"))
        self._in_format_combo = QComboBox()
        self._in_format_combo.addItem("Auto-detect", "auto")
        self._in_format_combo.addItem("ADM WAV",     "adm_wav")
        self._in_format_combo.addItem("ADM XML",     "adm_xml")
        self._in_format_combo.addItem("LUSID JSON",  "lusid_json")
        self._in_format_combo.setCurrentIndex(0)
        self._in_format_combo.setFont(QFont("Space Mono", 8))
        self._in_format_combo.setFixedWidth(130)
        opts_row.addWidget(self._in_format_combo)

        opts_row.addSpacing(24)

        # LFE mode
        opts_row.addWidget(self._row_label("LFE Mode"))
        self._lfe_combo = QComboBox()
        self._lfe_combo.addItem("Hardcoded",     "hardcoded")
        self._lfe_combo.addItem("Speaker Label", "speaker-label")
        self._lfe_combo.setCurrentIndex(0)
        self._lfe_combo.setFont(QFont("Space Mono", 8))
        self._lfe_combo.setFixedWidth(130)
        opts_row.addWidget(self._lfe_combo)

        opts_row.addStretch()
        layout.addLayout(opts_row)

        # ── Output path ──────────────────────────────────────────────
        layout.addWidget(self._row_label("Output Path  (LUSID JSON)"))

        out_row = QHBoxLayout()
        self._out_edit = QLineEdit()
        self._out_edit.setPlaceholderText("output.lusid.json (leave blank to auto-generate)…")
        self._out_btn = QPushButton("Browse")
        self._out_btn.setObjectName("FileButton")
        self._out_btn.setFont(QFont("Space Mono", 7))
        self._out_btn.setFixedWidth(80)
        out_row.addWidget(self._out_edit)
        out_row.addWidget(self._out_btn)
        layout.addLayout(out_row)

        # ── Action row: TRANSCODE button + status + Open Report ──────
        action_row = QHBoxLayout()
        self._transcode_btn = QPushButton("TRANSCODE")
        self._transcode_btn.setObjectName("PrimaryButton")
        self._transcode_btn.setFont(QFont("Space Mono", 8))
        self._transcode_btn.setFixedWidth(140)
        action_row.addWidget(self._transcode_btn)

        action_row.addSpacing(12)

        self._status_lbl = QLabel("IDLE")
        self._status_lbl.setObjectName("Muted")
        self._status_lbl.setFont(QFont("Space Mono", 7))
        action_row.addWidget(self._status_lbl)

        action_row.addStretch()

        self._open_report_btn = QPushButton("OPEN REPORT")
        self._open_report_btn.setObjectName("SecondaryButton")
        self._open_report_btn.setFont(QFont("Space Mono", 7))
        self._open_report_btn.setEnabled(False)
        action_row.addWidget(self._open_report_btn)

        layout.addLayout(action_row)

        # ── Log output ────────────────────────────────────────────────
        layout.addWidget(self._row_label("Transcode Log"))

        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMinimumHeight(200)
        self._log.setSizePolicy(
            QSizePolicy.Policy.Expanding,
            QSizePolicy.Policy.Expanding,
        )
        self._log.setFont(QFont("Space Mono", 8))
        layout.addWidget(self._log)

        root_layout.addWidget(card)

        # ── Signal connections ────────────────────────────────────────
        self._in_btn.clicked.connect(self._browse_input)
        self._out_btn.clicked.connect(self._browse_output)
        self._in_edit.textChanged.connect(self._update_hint)
        self._transcode_btn.clicked.connect(self._on_transcode_clicked)
        self._open_report_btn.clicked.connect(self._on_open_report)

    # ── Helpers ───────────────────────────────────────────────────────────

    def _row_label(self, text: str) -> QLabel:
        lbl = QLabel(text.upper())
        lbl.setObjectName("Muted")
        lbl.setFont(QFont("Space Mono", 7))
        return lbl

    def _set_status(self, state: str, message: str) -> None:
        """
        state: 'idle' | 'running' | 'success' | 'error'
        """
        t = self._theme
        colours = {
            "idle":    t["muted2"],
            "running": t["yellow"],
            "success": t["green"],
            "error":   t["red"],
        }
        c = colours.get(state, t["muted2"])
        self._status_lbl.setText(message.upper())
        self._status_lbl.setStyleSheet(f"color: {c};")

    # ── File browse ───────────────────────────────────────────────────────

    def _browse_input(self) -> None:
        """
        macOS workaround: offer directory picker first (for LUSID packages),
        fallback to file picker. Same pattern as RealtimeInputPanel._browse_source().
        """
        if sys.platform == "darwin":
            # On macOS, QFileDialog cannot show both files and folders at once.
            # First attempt a directory selection.
            d = QFileDialog.getExistingDirectory(
                self,
                "Select Input File or LUSID Package Directory",
                self._in_edit.text() or str(Path.home()),
            )
            if d:
                self._in_edit.setText(d)
                return
            # If cancelled, fall through to the file picker.

        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select ADM WAV / XML Input File",
            self._in_edit.text() or str(Path.home()),
            "Audio / XML Files (*.wav *.xml *.json);;All Files (*)",
        )
        if path:
            self._in_edit.setText(path)

    def _browse_output(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Save LUSID JSON Output",
            self._out_edit.text() or str(Path.home()),
            "LUSID JSON (*.lusid.json *.json);;All Files (*)",
        )
        if path:
            self._out_edit.setText(path)

    # ── Auto-detect hint ─────────────────────────────────────────────────

    def _update_hint(self, text: str) -> None:
        text = text.strip()
        if not text:
            self._in_hint.setText("")
            return
        hint, label = self._detect_input(text)
        self._in_hint.setText(hint)

    def _detect_input(self, path: str):
        """
        Returns (hint_string, format_label).
        """
        p = Path(path)
        if not p.exists():
            return ("⚠  path not found", "unknown")
        if p.is_dir():
            # Check for LUSID package hallmarks
            json_files = list(p.glob("*.lusid.json"))
            if json_files:
                return (f"✓  LUSID package directory  ({json_files[0].name})", "lusid_json")
            return ("⚠  directory detected — no .lusid.json found inside", "dir")
        # File
        s = path.lower()
        if s.endswith(".wav"):
            return ("✓  ADM WAV file", "adm_wav")
        if s.endswith(".xml"):
            return ("✓  ADM XML file", "adm_xml")
        if s.endswith(".lusid.json") or s.endswith(".json"):
            return ("✓  LUSID JSON file", "lusid_json")
        return ("?  unknown file type — format override required", "unknown")

    # ── Action slots ──────────────────────────────────────────────────────

    def _on_transcode_clicked(self) -> None:
        in_path  = self.get_in_path()
        out_path = self.get_out_path()

        if not in_path:
            self._set_status("error", "No input file selected.")
            self.append_line("[GUI] Cannot transcode: no input path specified.")
            return

        in_format = self.get_resolved_in_format()
        lfe_mode  = self.get_lfe_mode()

        self.run_requested.emit(in_path, in_format, out_path, lfe_mode)

    def _on_open_report(self) -> None:
        if not self._report_path:
            return
        rp = str(self._report_path)
        self.open_report_requested.emit(rp)
        # Also open it right away via the OS — the window may not connect
        # open_report_requested if it doesn't need to.
        _open_with_os(rp)


# ── Standalone helper ─────────────────────────────────────────────────────────

def _open_with_os(path: str) -> None:
    """Open a file / directory with the system default application."""
    try:
        if sys.platform == "darwin":
            subprocess.Popen(["open", path])
        elif sys.platform.startswith("win"):
            os.startfile(path)  # type: ignore[attr-defined]
        else:
            subprocess.Popen(["xdg-open", path])
    except Exception as exc:
        # Non-fatal; GUI window may display its own error via the signal
        print(f"[RealtimeTranscodePanel] Failed to open {path!r}: {exc}", file=sys.stderr)
