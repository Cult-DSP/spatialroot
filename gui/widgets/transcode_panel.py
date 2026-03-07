"""
transcode_panel.py — Phase 5: CULT Transcoder panel for spatialroot GUI.

Standalone card that lets the user:
  - Select an input file (ADM WAV, ADM XML, or LUSID package dir)
  - Choose --in-format (auto-detect from extension, or explicit override)
  - Choose --lfe-mode (hardcoded | speaker-label)
  - Set an output .lusid.json path
  - Run cult-transcoder transcode and view streaming output
  - Open the report JSON with the system default app on success
"""

from __future__ import annotations

import os
import sys
import subprocess
from pathlib import Path
from typing import Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QComboBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
)


# ---------------------------------------------------------------------------
# TranscodePanel
# ---------------------------------------------------------------------------

class TranscodePanel(QFrame):
    """
    Card that drives the cult-transcoder CLI.

    Signals
    -------
    run_clicked(in_path, in_format, out_path, lfe_mode)
        Emitted when the user presses TRANSCODE. The parent is responsible
        for constructing and starting a TranscodeRunner.
    open_report_clicked(report_path)
        Emitted when the user presses "Open Report".
    """

    run_clicked = Signal(str, str, str, str)   # in_path, in_format, out_path, lfe_mode
    open_report_clicked = Signal(str)           # report_path

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        self._report_path: Optional[str] = None

        lay = QVBoxLayout(self)
        lay.setContentsMargins(24, 22, 24, 22)
        lay.setSpacing(14)

        # ── Title ──────────────────────────────────────────────────────
        title = QLabel("Transcode", self)
        title.setObjectName("SectionTitle")
        lay.addWidget(title)

        subtitle = QLabel(
            "Convert ADM WAV / ADM XML → LUSID JSON via cult-transcoder", self
        )
        subtitle.setObjectName("Muted")
        lay.addWidget(subtitle)

        # ── Input file ─────────────────────────────────────────────────
        lay.addWidget(self._section_label("Input File"))

        in_row = QHBoxLayout()
        in_row.setSpacing(8)

        self.in_path_edit = QLineEdit(self)
        self.in_path_edit.setPlaceholderText(
            "Select ADM WAV, ADM XML, or LUSID package directory…"
        )
        self.in_path_edit.textChanged.connect(self._on_in_path_changed)
        in_row.addWidget(self.in_path_edit, 1)

        self.in_browse_btn = QPushButton("Browse", self)
        self.in_browse_btn.setObjectName("SecondaryButton")
        self.in_browse_btn.setFixedWidth(80)
        self.in_browse_btn.clicked.connect(self._browse_input)
        in_row.addWidget(self.in_browse_btn)

        lay.addLayout(in_row)

        self.in_hint = QLabel("", self)
        self.in_hint.setObjectName("Muted")
        lay.addWidget(self.in_hint)

        # ── Options row: in-format + lfe-mode ─────────────────────────
        opts_row = QHBoxLayout()
        opts_row.setSpacing(24)

        # in-format
        fmt_col = QVBoxLayout()
        fmt_col.setSpacing(4)
        fmt_col.addWidget(self._section_label("In Format"))
        self.fmt_combo = QComboBox(self)
        self.fmt_combo.addItem("Auto-detect", "auto")
        self.fmt_combo.addItem("ADM WAV  (adm_wav)", "adm_wav")
        self.fmt_combo.addItem("ADM XML  (adm_xml)", "adm_xml")
        fmt_col.addWidget(self.fmt_combo)
        opts_row.addLayout(fmt_col)

        # lfe-mode
        lfe_col = QVBoxLayout()
        lfe_col.setSpacing(4)
        lfe_col.addWidget(self._section_label("LFE Mode"))
        self.lfe_combo = QComboBox(self)
        self.lfe_combo.addItem("Hardcoded  (ch4, default)", "hardcoded")
        self.lfe_combo.addItem("Speaker Label  (speakerLabel attr)", "speaker-label")
        self.lfe_combo.setToolTip(
            "hardcoded: 4th DirectSpeaker is LFE (Phase 2 default).\n"
            "speaker-label: reads <speakerLabel>LFE</speakerLabel> from XML."
        )
        lfe_col.addWidget(self.lfe_combo)
        opts_row.addLayout(lfe_col)

        opts_row.addStretch(1)
        lay.addLayout(opts_row)

        # ── Output path ────────────────────────────────────────────────
        lay.addWidget(self._section_label("Output LUSID JSON"))

        out_row = QHBoxLayout()
        out_row.setSpacing(8)

        self.out_path_edit = QLineEdit(self)
        self.out_path_edit.setPlaceholderText(
            "processedData/stageForRender/scene.lusid.json"
        )
        self.out_path_edit.setText("processedData/stageForRender/scene.lusid.json")
        out_row.addWidget(self.out_path_edit, 1)

        self.out_browse_btn = QPushButton("Browse", self)
        self.out_browse_btn.setObjectName("SecondaryButton")
        self.out_browse_btn.setFixedWidth(80)
        self.out_browse_btn.clicked.connect(self._browse_output)
        out_row.addWidget(self.out_browse_btn)

        lay.addLayout(out_row)

        # ── Action row ─────────────────────────────────────────────────
        action_row = QHBoxLayout()
        action_row.setSpacing(12)

        self.run_btn = QPushButton("TRANSCODE", self)
        self.run_btn.setObjectName("PrimaryButton")
        self.run_btn.setMinimumHeight(46)
        self.run_btn.setMinimumWidth(160)
        self.run_btn.clicked.connect(self._on_run_clicked)
        action_row.addWidget(self.run_btn)

        action_row.addStretch(1)

        self.open_report_btn = QPushButton("Open Report", self)
        self.open_report_btn.setObjectName("SecondaryButton")
        self.open_report_btn.setEnabled(False)
        self.open_report_btn.clicked.connect(self._on_open_report)
        action_row.addWidget(self.open_report_btn)

        lay.addLayout(action_row)

        # ── Status label ───────────────────────────────────────────────
        self.status_label = QLabel("Ready", self)
        self.status_label.setObjectName("TranscodeStatus")
        lay.addWidget(self.status_label)

        # ── Log list ───────────────────────────────────────────────────
        from PySide6.QtWidgets import QListWidget
        self.log_list = QListWidget(self)
        self.log_list.setObjectName("LogList")
        self.log_list.setMinimumHeight(120)
        self.log_list.setMaximumHeight(200)
        self.log_list.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        lay.addWidget(self.log_list)

    # ── Public API ─────────────────────────────────────────────────────

    def set_running(self, running: bool) -> None:
        self.run_btn.setEnabled(not running)
        self.run_btn.setText("TRANSCODING…" if running else "TRANSCODE")
        self.in_browse_btn.setEnabled(not running)
        self.out_browse_btn.setEnabled(not running)
        self.fmt_combo.setEnabled(not running)
        self.lfe_combo.setEnabled(not running)
        if running:
            self._set_status("Running…", "neutral")
            self.open_report_btn.setEnabled(False)
            self.log_list.clear()

    def set_finished(self, success: bool, report_path: Optional[str]) -> None:
        self.set_running(False)
        if success:
            self._set_status("✓ Success", "success")
            if report_path:
                self._report_path = report_path
                self.open_report_btn.setEnabled(True)
        else:
            self._set_status("✗ Failed — check log below", "fail")
            self._report_path = report_path  # may still exist as fail-report
            if report_path and Path(report_path).exists():
                self.open_report_btn.setEnabled(True)

    def append_log(self, text: str) -> None:
        from datetime import datetime
        ts = datetime.now().strftime("%H:%M:%S")
        for line in text.splitlines():
            line = line.strip()
            if not line:
                continue
            self.log_list.addItem(f"  {ts}   {line}")
        self.log_list.scrollToBottom()

    def get_in_path(self) -> str:
        return self.in_path_edit.text().strip()

    def get_out_path(self) -> str:
        return self.out_path_edit.text().strip() or "processedData/stageForRender/scene.lusid.json"

    def get_resolved_in_format(self) -> str:
        """Return the explicit in-format, or auto-detect from the input path."""
        explicit = self.fmt_combo.currentData()
        if explicit != "auto":
            return explicit
        path = self.get_in_path().lower()
        if path.endswith(".wav"):
            return "adm_wav"
        if path.endswith(".xml"):
            return "adm_xml"
        # LUSID package dir → no transcode needed; caller should handle
        return "adm_wav"  # safe default

    def get_lfe_mode(self) -> str:
        return self.lfe_combo.currentData()

    # ── Private ────────────────────────────────────────────────────────

    @staticmethod
    def _section_label(text: str) -> QLabel:
        lbl = QLabel(text, None)
        lbl.setObjectName("Muted")
        return lbl

    def _set_status(self, text: str, state: str) -> None:
        """state: neutral | success | fail"""
        self.status_label.setText(text)
        colour = {
            "neutral": "#6E6E73",
            "success": "#3A9E6E",
            "fail":    "#C0392B",
        }.get(state, "#6E6E73")
        self.status_label.setStyleSheet(f"color: {colour}; font-size: 12px;")

    def _browse_input(self) -> None:
        """
        macOS: prefer getExistingDirectory first so LUSID package folders
        are selectable (native dialog only allows files otherwise).
        Other platforms: try file first, then directory.
        """
        if sys.platform == "darwin":
            # First offer directory picker for LUSID packages
            path = QFileDialog.getExistingDirectory(
                self, "Select LUSID Package Directory or press Cancel for file", "sourceData"
            )
            if not path:
                path, _ = QFileDialog.getOpenFileName(
                    self, "Select Input File", "sourceData",
                    "ADM / XML Files (*.wav *.xml *.json);;All Files (*)"
                )
        else:
            path, _ = QFileDialog.getOpenFileName(
                self, "Select Input File", "sourceData",
                "ADM / XML Files (*.wav *.xml *.json);;All Files (*)"
            )
            if not path:
                path = QFileDialog.getExistingDirectory(
                    self, "Select LUSID Package Directory", "sourceData"
                )
        if path:
            self.in_path_edit.setText(path)

    def _browse_output(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self, "Select Output LUSID JSON",
            self.out_path_edit.text() or "processedData/stageForRender/scene.lusid.json",
            "LUSID JSON (*.json);;All Files (*)"
        )
        if path:
            self.out_path_edit.setText(path)

    def _on_in_path_changed(self, text: str) -> None:
        text = text.strip()
        if not text:
            self.in_hint.setText("")
            return

        p = Path(text)
        if not p.exists():
            self.in_hint.setText("⚠ Path does not exist")
            self.in_hint.setStyleSheet("color: #C0392B; font-size: 11px;")
            return

        if p.is_file() and text.lower().endswith(".wav"):
            self.in_hint.setText("Detected: ADM WAV")
            self.in_hint.setStyleSheet("color: #3A9E6E; font-size: 11px;")
            # Auto-select adm_wav if on auto
            if self.fmt_combo.currentData() == "auto":
                self.fmt_combo.setCurrentIndex(0)
        elif p.is_file() and text.lower().endswith(".xml"):
            self.in_hint.setText("Detected: ADM XML")
            self.in_hint.setStyleSheet("color: #3A9E6E; font-size: 11px;")
        elif p.is_dir() and (p / "scene.lusid.json").exists():
            self.in_hint.setText("Detected: LUSID package (no transcode needed)")
            self.in_hint.setStyleSheet("color: #6E6E73; font-size: 11px;")
        else:
            self.in_hint.setText("⚠ Unrecognised — select a .wav, .xml, or LUSID package dir")
            self.in_hint.setStyleSheet("color: #C0392B; font-size: 11px;")

    def _on_run_clicked(self) -> None:
        in_path = self.get_in_path()
        if not in_path:
            self._set_status("✗ Select an input file first", "fail")
            return
        out_path = self.get_out_path()
        in_format = self.get_resolved_in_format()
        lfe_mode = self.get_lfe_mode()
        self.run_clicked.emit(in_path, in_format, out_path, lfe_mode)

    def _on_open_report(self) -> None:
        if self._report_path and Path(self._report_path).exists():
            self.open_report_clicked.emit(self._report_path)
