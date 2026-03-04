"""
RealtimeInputPanel.py — Launch-time configuration panel.

Collects:
  - Source path (ADM WAV or LUSID package directory) — with auto-detection hint
  - Speaker layout JSON path
  - Remap CSV path (optional)
  - Buffer size (64 / 128 / 256 / 512 / 1024 dropdown)
  - Scan Audio checkbox (only relevant for ADM; greyed when LUSID detected)

Phase 10 — GUI Agent.
"""

from __future__ import annotations

import os
import sys
from pathlib import Path
from typing import Optional

from PySide6.QtCore import Signal, Qt, QTimer
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QFrame,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


def _card() -> QFrame:
    f = QFrame()
    f.setObjectName("Card")
    return f


class RealtimeInputPanel(QWidget):
    """
    Launch-time configuration panel.

    Emits `config_changed` whenever any field changes — the parent window
    reads `get_source_path()`, `get_layout_path()`, etc. on Start.
    """

    config_changed = Signal()

    def __init__(self, theme: dict = None, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        from .theme import DARK
        self._theme = theme or DARK
        self._build_ui()
        self._connect_signals()

    # ── Public read API ──────────────────────────────────────────────────

    def get_source_path(self) -> str:
        return self._source_edit.text().strip()

    def get_layout_path(self) -> str:
        return self._layout_edit.text().strip()

    def get_remap_csv(self) -> Optional[str]:
        selected = self._remap_combo.currentData()
        if selected is not None:
            return selected
        v = self._remap_edit.text().strip()
        return v if v else None

    def get_buffer_size(self) -> int:
        return int(self._buffer_combo.currentText())

    def get_scan_audio(self) -> bool:
        return self._scan_check.isChecked()

    def set_enabled_for_state(self, running: bool) -> None:
        """Disable all inputs while the engine is running."""
        for w in (self._source_edit, self._source_btn,
                  self._layout_combo, self._layout_edit, self._layout_btn,
                  self._remap_combo, self._remap_edit, self._remap_btn,
                  self._buffer_combo, self._scan_check):
            w.setEnabled(not running)

    # ── UI construction ──────────────────────────────────────────────────

    def _build_ui(self) -> None:
        root_layout = QVBoxLayout(self)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(8)

        card = _card()
        layout = QVBoxLayout(card)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(8)

        title = QLabel("INPUT CONFIGURATION")
        title.setObjectName("SectionTitle")
        from PySide6.QtGui import QFont
        title.setFont(QFont("Space Mono", 7))
        # Add a horizontal line after it (mimics section-label::after in HTML):
        # Use a QFrame as a separator — add to layout immediately after title.
        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet(f"color: {self._theme['border_light']}; margin-bottom: 6px;")
        layout.addWidget(title)
        layout.addWidget(sep)

        # Source row
        layout.addWidget(self._make_row_label("Source"))
        src_row = QHBoxLayout()
        self._source_edit = QLineEdit()
        self._source_edit.setPlaceholderText("ADM WAV file or LUSID package directory…")
        self._source_btn = QPushButton("Browse")
        self._source_btn.setObjectName("FileButton")
        self._source_btn.setFont(QFont("Space Mono", 7))
        self._source_btn.setFixedWidth(80)
        src_row.addWidget(self._source_edit)
        src_row.addWidget(self._source_btn)
        layout.addLayout(src_row)

        self._source_hint = QLabel("")
        self._source_hint.setObjectName("Muted")
        layout.addWidget(self._source_hint)

        # Layout row
        layout.addWidget(self._make_row_label("Speaker Layout"))
        lay_row = QHBoxLayout()
        self._layout_combo = QComboBox()
        self._layout_combo.addItem("AlloSphere", "spatial_engine/speaker_layouts/allosphere_layout.json")
        self._layout_combo.addItem("Translab", "spatial_engine/speaker_layouts/translab-sono-layout.json")
        self._layout_combo.setCurrentIndex(0)  # Default to AlloSphere
        self._layout_combo.setFont(QFont("Space Mono", 8))
        self._layout_combo.setFixedWidth(100)
        lay_row.addWidget(self._layout_combo)
        self._layout_edit = QLineEdit()
        self._layout_edit.setText("spatial_engine/speaker_layouts/allosphere_layout.json")
        self._layout_btn = QPushButton("Browse")
        self._layout_btn.setObjectName("FileButton")
        self._layout_btn.setFont(QFont("Space Mono", 7))
        self._layout_btn.setFixedWidth(80)
        lay_row.addWidget(self._layout_edit)
        lay_row.addWidget(self._layout_btn)
        layout.addLayout(lay_row)

        # Remap CSV row
        layout.addWidget(self._make_row_label("Remap CSV  (optional)"))
        remap_row = QHBoxLayout()
        self._remap_combo = QComboBox()
        self._remap_combo.addItem("None", None)
        self._remap_combo.addItem("Allosphere Example", "spatial_engine/remaping/exampleRemap.csv")
        self._remap_combo.setCurrentIndex(0)  # Default to None
        self._remap_combo.setFont(QFont("Space Mono", 8))
        self._remap_combo.setFixedWidth(150)
        remap_row.addWidget(self._remap_combo)
        self._remap_edit = QLineEdit()
        self._remap_edit.setPlaceholderText("channel_remap.csv (leave blank for identity)…")
        self._remap_btn = QPushButton("Browse")
        self._remap_btn.setObjectName("FileButton")
        self._remap_btn.setFont(QFont("Space Mono", 7))
        self._remap_btn.setFixedWidth(80)
        remap_row.addWidget(self._remap_edit)
        remap_row.addWidget(self._remap_btn)
        layout.addLayout(remap_row)

        # Buffer + scan row
        opts_row = QHBoxLayout()
        opts_row.addWidget(self._make_row_label("Buffer Size"))
        self._buffer_combo = QComboBox()
        for v in ("64", "128", "256", "512", "1024"):
            self._buffer_combo.addItem(v)
        self._buffer_combo.setCurrentText("512")
        self._buffer_combo.setFont(QFont("Space Mono", 8))
        self._buffer_combo.setFixedWidth(100)
        opts_row.addWidget(self._buffer_combo)
        opts_row.addSpacing(20)
        self._scan_check = QCheckBox("Scan Audio  (ADM only, +~14s startup)")
        self._scan_check.setFont(QFont("Space Mono", 7))
        opts_row.addWidget(self._scan_check)
        opts_row.addStretch()
        layout.addLayout(opts_row)

        root_layout.addWidget(card)

        from .brand_widgets import CornerMarksWidget
        # At end of _build_ui(), after root_layout.addWidget(card):
        marks = CornerMarksWidget(color=self._theme["muted2"], parent=card)
        # Delay geometry assignment until card is shown:
        QTimer.singleShot(0, lambda: marks.setGeometry(card.rect()))

    def _make_row_label(self, text: str) -> QLabel:
        lbl = QLabel(text.upper())
        lbl.setObjectName("Muted")
        lbl.setFont(QFont("Space Mono", 7))
        lbl.setFixedWidth(110)
        return lbl

    # ── Signal wiring ──────────────────────────────────────────────────

    def _connect_signals(self) -> None:
        self._source_btn.clicked.connect(self._browse_source)
        self._layout_combo.currentIndexChanged.connect(self._on_layout_combo_changed)
        self._layout_btn.clicked.connect(self._browse_layout)
        self._remap_combo.currentIndexChanged.connect(self._on_remap_combo_changed)
        self._remap_btn.clicked.connect(self._browse_remap)
        self._source_edit.textChanged.connect(self._on_source_changed)
        self._layout_edit.textChanged.connect(lambda _: self.config_changed.emit())
        self._remap_edit.textChanged.connect(lambda _: self.config_changed.emit())
        self._buffer_combo.currentIndexChanged.connect(lambda _: self.config_changed.emit())
        self._scan_check.stateChanged.connect(lambda _: self.config_changed.emit())

    # ── Browse handlers ──────────────────────────────────────────────────

    def _on_layout_combo_changed(self) -> None:
        selected_path = self._layout_combo.currentData()
        self._layout_edit.setText(selected_path)
        self.config_changed.emit()

    def _on_remap_combo_changed(self) -> None:
        selected_path = self._remap_combo.currentData()
        if selected_path is not None:
            self._remap_edit.setText(selected_path)
        else:
            self._remap_edit.clear()
        self.config_changed.emit()

    def _browse_source(self) -> None:
        # On macOS the native open-file dialog may not allow selecting
        # directories — prefer directory selection first there.
        path = ""
        if sys.platform == "darwin":
            path = QFileDialog.getExistingDirectory(self, "Select LUSID Package Directory", "sourceData")
            if not path:
                path, _ = QFileDialog.getOpenFileName(
                    self, "Select ADM WAV File", "sourceData", "WAV Files (*.wav);;All Files (*)"
                )
        else:
            # Default: try file first, then directory
            path, _ = QFileDialog.getOpenFileName(
                self, "Select ADM WAV File", "sourceData", "WAV Files (*.wav);;All Files (*)"
            )
            if not path:
                path = QFileDialog.getExistingDirectory(self, "Select LUSID Package Directory", "sourceData")

        if path:
            print(f"[DEBUG] _browse_source selected: {path}")
            self._source_edit.setText(path)

    def _browse_layout(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Speaker Layout JSON", "spatial_engine/speaker_layouts", "JSON Files (*.json);;All Files (*)"
        )
        if path:
            self._layout_edit.setText(path)
            # Also update combo to "Custom" or something? For now, just set the text.

    def _browse_remap(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self, "Select Remap CSV", "spatial_engine/remaping", "CSV Files (*.csv);;All Files (*)"
        )
        if path:
            self._remap_edit.setText(path)

    # ── Source auto-detection hint ────────────────────────────────────────

    def _on_source_changed(self, text: str) -> None:
        text = text.strip()
        hint, is_adm, is_lusid = self._detect_source(text)
        print(f"[DEBUG] _on_source_changed: '{text}' -> hint='{hint}', is_adm={is_adm}, is_lusid={is_lusid}")
        self._source_hint.setText(hint)
        self._source_hint.setFont(QFont("Space Mono", 7))
        if is_adm or is_lusid:
            self._source_hint.setStyleSheet(f"color: {self._theme['green']};")
        else:
            self._source_hint.setStyleSheet(f"color: {self._theme['red']};")
        self._scan_check.setEnabled(is_adm or text == "")
        self.config_changed.emit()

    def _detect_source(self, path: str) -> tuple[str, bool, bool]:
        """Returns (hint_text, is_adm, is_lusid)."""
        if not path:
            return "", False, False
        exists = os.path.exists(path)
        is_file = os.path.isfile(path)
        is_dir = os.path.isdir(path)
        scene_exists = os.path.exists(os.path.join(path, "scene.lusid.json")) if is_dir else False
        print(f"[DEBUG] _detect_source: path='{path}', exists={exists}, is_file={is_file}, is_dir={is_dir}, scene_exists={scene_exists}")
        if not exists:
            return "⚠ Path does not exist", False, False
        if os.path.isfile(path) and path.lower().endswith(".wav"):
            return "Detected: ADM WAV", True, False
        if is_dir and scene_exists:
            return "Detected: LUSID package", False, True
        return "⚠ Unrecognized — select a .wav file or LUSID package directory", False, False

    def validate(self) -> Optional[str]:
        """Return an error string if inputs are invalid, else None."""
        src = self.get_source_path()
        lay = self.get_layout_path()
        if not src:
            return "Source path is required."
        _, is_adm, is_lusid = self._detect_source(src)
        if not is_adm and not is_lusid:
            return "Source must be a .wav (ADM) file or a LUSID package directory containing scene.lusid.json."
        if not lay:
            return "Speaker layout path is required."
        if not os.path.isfile(lay):
            return f"Speaker layout file not found: {lay}"
        remap = self.get_remap_csv()
        if remap and not os.path.isfile(remap):
            return f"Remap CSV not found: {remap}"
        return None
