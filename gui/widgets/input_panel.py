from __future__ import annotations

import os
import sys
from PySide6.QtCore import Qt, Signal, QRectF
from PySide6.QtGui import QPainter, QColor, QPen, QFont
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout, QPushButton, QWidget, QHBoxLayout, QFileDialog, QLineEdit


class StatusBadge(QWidget):
    """18×18 circle badge with a check mark for completed states."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(20, 20)
        self._state = "inactive"  # inactive | ready | active

    def set_state(self, state: str):
        self._state = state
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        r = QRectF(1, 1, 18, 18)

        if self._state == "inactive":
            pen = QPen(QColor(0, 0, 0, 40), 1.2)
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(r)
        elif self._state in ("ready", "active"):
            # Filled circle
            fill = QColor(110, 110, 115, 55) if self._state == "ready" else QColor(76, 111, 255, 50)
            border = QColor(110, 110, 115, 80) if self._state == "ready" else QColor(76, 111, 255, 100)
            pen = QPen(border, 1.2)
            p.setPen(pen)
            p.setBrush(fill)
            p.drawEllipse(r)

            # Draw check mark
            check_color = QColor(60, 60, 62) if self._state == "ready" else QColor(76, 111, 255, 220)
            pen = QPen(check_color, 1.6)
            pen.setCapStyle(Qt.RoundCap)
            pen.setJoinStyle(Qt.RoundJoin)
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            # Check mark path: short left stroke + long right stroke
            p.drawLine(6, 10, 9, 13)
            p.drawLine(9, 13, 14, 7)

        p.end()


class StatusRow(QWidget):
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 4, 0, 4)
        lay.setSpacing(10)

        self.badge = StatusBadge(self)
        lay.addWidget(self.badge, alignment=Qt.AlignVCenter)

        self.label = QLabel(text, self)
        self.label.setObjectName("Muted")
        lay.addWidget(self.label, alignment=Qt.AlignVCenter)

        lay.addStretch(1)

        # Green active dot
        self.dot = QWidget(self)
        self.dot.setFixedSize(8, 8)
        self.dot.setVisible(False)
        lay.addWidget(self.dot, alignment=Qt.AlignVCenter)

    def set_state(self, state: str):
        self.badge.set_state(state)
        if state == "active":
            self.dot.setVisible(True)
            self.dot.setStyleSheet("background: #4CAF82; border-radius: 4px;")
            self.label.setStyleSheet("color: #1C1C1E; font-size: 12px;")
        elif state == "ready":
            self.dot.setVisible(False)
            self.label.setStyleSheet("color: #3C3C3E; font-size: 12px;")
        else:
            self.dot.setVisible(False)
            self.label.setStyleSheet("color: #6E6E73; font-size: 12px;")

class InputPanel(QFrame):
    file_selected = Signal(str)
    output_path_changed = Signal(str)


    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(22, 26, 22, 22)
        lay.setSpacing(16)

        title = QLabel("Input", self)
        title.setObjectName("SectionTitle")
        lay.addWidget(title)

        self.select_btn = QPushButton("SELECT INPUT FILE", self)
        self.select_btn.setObjectName("SecondaryButton")
        self.select_btn.clicked.connect(self._choose_file)
        self.select_btn.setMinimumHeight(44)
        lay.addWidget(self.select_btn)

        # Output path field with file dialog
        output_row = QHBoxLayout()
        output_label = QLabel("Output Path:", self)
        output_label.setObjectName("Muted")
        output_row.addWidget(output_label)
        self.output_path_btn = QPushButton("Select Output File", self)
        self.output_path_btn.setObjectName("SecondaryButton")
        self.output_path_btn.clicked.connect(self._choose_output_path)
        output_row.addWidget(self.output_path_btn)
        self.output_path = "processedData/spatial_render.wav"
        self.output_path_btn.setToolTip(self.output_path)
        lay.addLayout(output_row)

        self.row_adm = StatusRow("ADM Source", self)
        lay.addWidget(self.row_adm)
        self.row_meta = StatusRow("Metadata Extracted", self)
        self.row_activity = StatusRow("Channel Activity Ready", self)

        lay.addWidget(self.row_meta)
        lay.addWidget(self.row_activity)
        lay.addStretch(1)

        self.row_adm.set_state('inactive')
        self.row_meta.set_state('inactive')
        self.row_activity.set_state('inactive')

    def _on_output_path_changed(self, text):
        self.output_path_changed.emit(text)

    def _choose_output_path(self):
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Select Output File",
            self.output_path,
            "WAV Files (*.wav);;All Files (*)"
        )
        if path:
            self.output_path = path
            self.output_path_btn.setToolTip(path)
            self.output_path_changed.emit(path)

    def _choose_file(self):
        """
        Accept ADM WAV, ADM XML, LUSID JSON, or a LUSID package directory.
        macOS: prefer directory picker first (native dialog blocks folder selection
        when only file selection is active) — ported from RealtimeInputPanel.
        """
        if sys.platform == "darwin":
            # On macOS, offer directory first for LUSID package folders
            path = QFileDialog.getExistingDirectory(
                self, "Select LUSID Package Directory or press Cancel for file", ""
            )
            if not path:
                path, _ = QFileDialog.getOpenFileName(
                    self, "Select Input File", "",
                    "ADM / LUSID Files (*.wav *.xml *.json);;All Files (*)"
                )
        else:
            path, _ = QFileDialog.getOpenFileName(
                self, "Select Input File", "",
                "ADM / LUSID Files (*.wav *.xml *.json);;All Files (*)"
            )
            if not path:
                path = QFileDialog.getExistingDirectory(
                    self, "Select LUSID Package Directory", ""
                )
        if path:
            self.file_selected.emit(path)
            self.row_adm.set_state('ready')

    def set_progress_flags(self, metadata: bool, activity: bool):
        if metadata:
            self.row_meta.set_state('active')
        if activity:
            self.row_activity.set_state('active')

    def get_output_path(self) -> str:
        return self.output_path or "processedData/spatial_render.wav"
