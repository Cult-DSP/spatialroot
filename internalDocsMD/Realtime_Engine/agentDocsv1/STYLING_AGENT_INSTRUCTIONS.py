# ═══════════════════════════════════════════════════════════════════════════════
# spatialroot — Real-Time GUI  ·  Brand Styling Agent Instructions
# Phase 10 · Cult DSP Visual Identity
# ═══════════════════════════════════════════════════════════════════════════════
#
# PURPOSE
# -------
# This document tells a coding agent exactly how to translate the two HTML
# reference mockups (spatialroot-realtime-gui.html = DARK,
# spatialroot-realtime-gui-light.html = LIGHT) into the five PySide6 source files:
#
#   realtimeGUI.py               ← RealtimeWindow (QMainWindow)
#   RealtimeInputPanel.py        ← launch-time config
#   RealtimeTransportPanel.py    ← transport + status pill
#   RealtimeControlsPanel.py     ← live OSC sliders & toggle
#   RealtimeLogPanel.py          ← stdout/stderr console
#
# HARD CONSTRAINTS
# ----------------
# 1. Do NOT change any signal, slot, public method, or business logic.
#    Only add/replace: QSS strings, QFont calls, widget object names,
#    layout margins/spacing, and new purely-decorative QWidget subclasses.
# 2. All colour values must be sourced from the THEME TOKENS section below.
#    Never hardcode colours outside the theme dicts.
# 3. Support both DARK and LIGHT modes via a single THEME dict passed at
#    construction time. Default to LIGHT.
# 4. The HTML mockups are the visual specification. When in doubt, match them.
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 1 — NEW FILE: theme.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# Create realtime_panels/theme.py (same package as the panel files).
# This is the single source of truth for all colour and font constants.
#
# ┌─────────────────────────────────────────────────────────────────────────────
# │ theme.py
# └─────────────────────────────────────────────────────────────────────────────
"""
theme.py — Colour tokens and QSS factory for spatialroot / Cult DSP brand.

Two modes: DARK (default) and LIGHT.
Reference: spatialroot-realtime-gui.html (dark) / -light.html (light).
"""
from __future__ import annotations
from dataclasses import dataclass, field
from typing import Dict

# ── Colour token dicts ────────────────────────────────────────────────────────

DARK: Dict[str, str] = {
    # Backgrounds
    "bg":           "#0f0f0f",   # root window / scroll area
    "bg2":          "#141414",   # input fields, log area
    "surface":      "#1c1c1c",   # card faces
    # Borders
    "border":       "rgba(255,255,255,0.07)",
    "border_light": "rgba(255,255,255,0.12)",
    # Text
    "text":         "#e8e8e4",
    "muted":        "#6b6b66",
    "muted2":       "#4a4a46",
    # Status colours
    "green":        "#34C759",
    "green_bg":     "rgba(52,199,89,0.08)",
    "green_bd":     "rgba(52,199,89,0.20)",
    "yellow":       "#FFCC00",
    "yellow_bg":    "rgba(255,204,0,0.15)",
    "orange":       "#FF9F0A",
    "red":          "#FF3B30",
    "red_bg":       "rgba(255,59,48,0.12)",
    "red_bd":       "rgba(255,59,48,0.25)",
}

LIGHT: Dict[str, str] = {
    "bg":           "#f0ede8",
    "bg2":          "#e8e4de",
    "surface":      "#f7f5f1",
    "border":       "rgba(0,0,0,0.07)",
    "border_light": "rgba(0,0,0,0.11)",
    "text":         "#1a1a18",
    "muted":        "#7a7870",
    "muted2":       "#a8a49c",
    "green":        "#1e8a4a",
    "green_bg":     "rgba(30,138,74,0.08)",
    "green_bd":     "rgba(30,138,74,0.20)",
    "yellow":       "#a07800",
    "yellow_bg":    "rgba(160,120,0,0.12)",
    "orange":       "#b05a00",
    "red":          "#c0392b",
    "red_bg":       "rgba(192,57,43,0.08)",
    "red_bd":       "rgba(192,57,43,0.25)",
}

# State → (pill background, dot colour)
# The dot colours are state-specific; pill bg uses the theme's green/yellow etc.
PILL_STYLE: Dict[str, tuple[str, str]] = {
    "Idle":      ("rgba(110,110,115,0.15)", "#8E8E93"),
    "Launching": ("rgba(255,204,0,0.18)",   "#FFCC00"),
    "Running":   ("",                        ""),       # fill from theme["green_bg/green"]
    "Paused":    ("rgba(255,159,10,0.15)",  "#FF9F0A"),
    "Exited":    ("rgba(110,110,115,0.15)", "#8E8E93"),
    "Error":     ("",                        ""),       # fill from theme["red_bg/red"]
}
# Note: "Running" and "Error" pill colours are derived from the theme so they
# adapt correctly in LIGHT mode. Resolve them in TransportPanel._refresh():
#   Running → bg=theme["green_bg"],  dot=theme["green"]
#   Error   → bg=theme["red_bg"],    dot=theme["red"]


def make_qss(t: Dict[str, str]) -> str:
    """
    Return the master QSS string for QApplication.setStyleSheet().
    `t` is either DARK or LIGHT.

    Maps to HTML:
      body            → QWidget#Root
      .card           → QFrame#Card
      .section-label  → QLabel#SectionTitle
      .field-label    → QLabel#Muted
      .field-input    → QLineEdit (all)
      .btn-browse     → QPushButton#FileButton
      .btn-primary    → QPushButton#PrimaryButton
      .btn-secondary  → QPushButton#SecondaryButton
      .select-field   → QComboBox (all)
      .log-area       → QPlainTextEdit (in LogPanel)
      .pill           → QFrame#Pill   (colours set dynamically)
      scrollbar       → QScrollBar
    """
    return f"""
    /* ── Root / window ── */
    QWidget#Root {{
        background: {t["bg"]};
    }}
    QMainWindow {{
        background: {t["bg"]};
    }}

    /* ── Cards ── */
    QFrame#Card {{
        background: {t["surface"]};
        border: 1px solid {t["border_light"]};
        border-radius: 4px;
    }}

    /* ── Header bar ── */
    QFrame#HeaderBar {{
        background: {t["surface"]};
        border-bottom: 1px solid {t["border_light"]};
    }}

    /* ── Typography defaults ── */
    QLabel {{
        color: {t["text"]};
        background: transparent;
    }}
    QLabel#SectionTitle {{
        color: {t["muted"]};
        font-size: 8pt;
        letter-spacing: 3px;
    }}
    QLabel#Muted {{
        color: {t["muted"]};
        font-size: 8pt;
    }}
    QLabel#Muted2 {{
        color: {t["muted2"]};
        font-size: 8pt;
    }}
    QLabel#Title {{
        color: {t["text"]};
        font-size: 12pt;
    }}
    QLabel#Subtitle {{
        color: {t["muted"]};
        font-size: 8pt;
        letter-spacing: 2px;
    }}

    /* ── Line edits (= .field-input in HTML) ── */
    QLineEdit {{
        background: {t["bg2"]};
        border: 1px solid {t["border_light"]};
        border-radius: 3px;
        color: {t["muted"]};
        padding: 5px 8px;
        font-size: 9pt;
    }}
    QLineEdit:enabled {{
        color: {t["text"]};
        border-color: {t["border_light"]};
    }}
    QLineEdit:disabled {{
        color: {t["muted2"]};
        background: {t["bg"]};
    }}
    QLineEdit[placeholderText] {{
        color: {t["muted2"]};
    }}

    /* ── Combo boxes (= .select-field in HTML) ── */
    QComboBox {{
        background: {t["bg2"]};
        border: 1px solid {t["border_light"]};
        border-radius: 3px;
        color: {t["text"]};
        padding: 5px 8px;
        font-size: 9pt;
    }}
    QComboBox:disabled {{
        color: {t["muted2"]};
        background: {t["bg"]};
    }}
    QComboBox::drop-down {{
        border: none;
        width: 18px;
    }}
    QComboBox QAbstractItemView {{
        background: {t["surface"]};
        border: 1px solid {t["border_light"]};
        color: {t["text"]};
        selection-background-color: {t["border_light"]};
    }}

    /* ── Buttons ── */
    /* Primary  = .btn-primary  (Start equivalent when active) */
    QPushButton#PrimaryButton {{
        background: {t["text"]};
        color: {t["bg"]};
        border: 1px solid {t["text"]};
        border-radius: 3px;
        padding: 7px 20px;
        font-size: 8pt;
        letter-spacing: 2px;
        font-weight: bold;
    }}
    QPushButton#PrimaryButton:hover {{
        background: {t["muted"]};
    }}
    QPushButton#PrimaryButton:disabled {{
        opacity: 0.3;
        background: {t["muted2"]};
        border-color: {t["muted2"]};
    }}

    /* Secondary = .btn-secondary (Stop, Restart, Pause, etc.) */
    QPushButton#SecondaryButton {{
        background: transparent;
        color: {t["muted"]};
        border: 1px solid {t["border_light"]};
        border-radius: 3px;
        padding: 7px 14px;
        font-size: 8pt;
        letter-spacing: 2px;
    }}
    QPushButton#SecondaryButton:hover {{
        color: {t["text"]};
        border-color: {t["muted"]};
        background: {t["border"]};
    }}
    QPushButton#SecondaryButton:disabled {{
        color: {t["muted2"]};
        border-color: {t["border"]};
        opacity: 0.4;
    }}

    /* File browse = .btn-browse */
    QPushButton#FileButton {{
        background: transparent;
        color: {t["muted"]};
        border: 1px solid {t["border_light"]};
        border-radius: 3px;
        padding: 5px 10px;
        font-size: 8pt;
        letter-spacing: 1px;
    }}
    QPushButton#FileButton:hover {{
        color: {t["text"]};
        border-color: {t["muted"]};
    }}
    QPushButton#FileButton:disabled {{
        color: {t["muted2"]};
        opacity: 0.4;
    }}

    /* Kill button — danger style (.btn-danger) */
    QPushButton#KillButton {{
        background: transparent;
        color: {t["red"]};
        border: 1px solid {t["red_bd"]};
        border-radius: 3px;
        padding: 7px 14px;
        font-size: 8pt;
        letter-spacing: 2px;
    }}
    QPushButton#KillButton:hover {{
        background: {t["red_bg"]};
        border-color: {t["red"]};
    }}
    QPushButton#KillButton:disabled {{
        opacity: 0.25;
    }}

    /* Clear / small secondary */
    QPushButton#ClearButton, QPushButton#SmallButton {{
        background: transparent;
        color: {t["muted2"]};
        border: 1px solid {t["border"]};
        border-radius: 3px;
        padding: 3px 10px;
        font-size: 7pt;
        letter-spacing: 1px;
    }}
    QPushButton#ClearButton:hover, QPushButton#SmallButton:hover {{
        color: {t["muted"]};
        border-color: {t["border_light"]};
    }}

    /* ── Checkboxes ── */
    QCheckBox {{
        color: {t["muted"]};
        font-size: 8pt;
        letter-spacing: 1px;
        spacing: 8px;
    }}
    QCheckBox::indicator {{
        width: 13px;
        height: 13px;
        border: 1px solid {t["border_light"]};
        border-radius: 2px;
        background: {t["bg2"]};
    }}
    QCheckBox::indicator:checked {{
        background: {t["text"]};
        border-color: {t["text"]};
    }}
    QCheckBox:disabled {{
        color: {t["muted2"]};
    }}

    /* ── Sliders (_ParamRow) ── */
    QSlider::groove:horizontal {{
        height: 2px;
        background: {t["bg2"]};
        border-radius: 1px;
    }}
    QSlider::sub-page:horizontal {{
        background: {t["muted"]};
        border-radius: 1px;
    }}
    QSlider::handle:horizontal {{
        width: 10px;
        height: 10px;
        margin: -4px 0;
        border-radius: 5px;
        background: {t["surface"]};
        border: 1.5px solid {t["text"]};
    }}
    QSlider::handle:horizontal:hover {{
        border-color: {t["muted"]};
        background: {t["text"]};
    }}
    QSlider:disabled::groove:horizontal {{
        background: {t["bg"]};
    }}
    QSlider:disabled::handle:horizontal {{
        border-color: {t["muted2"]};
        background: {t["bg2"]};
    }}

    /* ── SpinBox (_ParamRow) ── */
    QDoubleSpinBox {{
        background: {t["bg2"]};
        border: 1px solid {t["border"]};
        border-radius: 3px;
        color: {t["muted"]};
        padding: 3px 6px;
        font-size: 9pt;
    }}
    QDoubleSpinBox:enabled {{
        color: {t["text"]};
    }}
    QDoubleSpinBox:disabled {{
        color: {t["muted2"]};
        background: {t["bg"]};
    }}
    QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {{
        width: 0; height: 0; border: none;
    }}

    /* ── Log panel ── */
    QPlainTextEdit {{
        background: {t["bg2"]};
        border: 1px solid {t["border"]};
        border-radius: 3px;
        color: {t["muted"]};
        padding: 8px;
        font-size: 9pt;
    }}

    /* ── Pill frame (colours set dynamically in code) ── */
    QFrame#Pill {{
        border-radius: 10px;
        border: 1px solid {t["border_light"]};
    }}

    /* ── Phase tag ── */
    QLabel#PhaseTag {{
        color: {t["muted2"]};
        border: 1px solid {t["border"]};
        border-radius: 2px;
        padding: 1px 6px;
        font-size: 7pt;
        letter-spacing: 2px;
    }}

    /* ── Scroll area / bars ── */
    QScrollArea {{
        border: none;
        background: transparent;
    }}
    QScrollBar:vertical {{
        background: transparent;
        width: 4px;
        margin: 0;
    }}
    QScrollBar::handle:vertical {{
        background: {t["border_light"]};
        border-radius: 2px;
        min-height: 20px;
    }}
    QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {{
        height: 0;
    }}
    QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {{
        background: none;
    }}
    """


# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 2 — NEW FILE: brand_widgets.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# Create realtime_panels/brand_widgets.py.
# Contains purely decorative QWidget subclasses that have no logic.
# Import these into the panel files as needed.
#
# ┌─────────────────────────────────────────────────────────────────────────────
# │ brand_widgets.py
# └─────────────────────────────────────────────────────────────────────────────
"""
brand_widgets.py — Purely decorative widgets for spatialroot brand identity.

All widgets here are visual-only; they emit no signals and hold no state.
Reference: the corner-marks, eye-ornament, sacred-geometry background,
and section-label divider line from the HTML mockups.
"""
from __future__ import annotations
from typing import Optional
from PySide6.QtCore import Qt, QRectF, QPointF, QSize
from PySide6.QtGui import QPainter, QPen, QColor, QFont, QPainterPath
from PySide6.QtWidgets import QWidget, QSizePolicy


class CornerMarksWidget(QWidget):
    """
    Draws four L-shaped tick marks in the corners of its parent widget.
    Maps to: .corner-mark.tl/tr/bl/br in the HTML (on .card elements).

    Usage: instantiate with the card QFrame as parent; it will overlay it.
    Set geometry to match the parent's rect (use resizeEvent or fixed inset).

    Recommended: call place_on(card_frame) which sets geometry automatically.
    """

    MARK_LEN   = 10   # px arm length  (HTML: 12px)
    MARK_INSET = 8    # px from corner (HTML: top/left: 8px)

    def __init__(
        self,
        color: str = "#4a4a46",   # DARK: muted2 | LIGHT: use theme["muted2"]
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._color = QColor(color)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)

    def set_color(self, color: str) -> None:
        self._color = QColor(color)
        self.update()

    def place_on(self, parent: QWidget) -> None:
        """Resize self to fill parent. Call after parent is laid out."""
        self.setParent(parent)
        self.setGeometry(parent.rect())
        self.raise_()
        self.show()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self._color, 1.0)
        p.setPen(pen)
        w, h = self.width(), self.height()
        i, L = self.MARK_INSET, self.MARK_LEN

        # top-left
        p.drawLine(i, i + L, i, i)
        p.drawLine(i, i, i + L, i)
        # top-right
        p.drawLine(w - i - L, i, w - i, i)
        p.drawLine(w - i, i, w - i, i + L)
        # bottom-left
        p.drawLine(i, h - i - L, i, h - i)
        p.drawLine(i, h - i, i + L, h - i)
        # bottom-right
        p.drawLine(w - i - L, h - i, w - i, h - i)
        p.drawLine(w - i, h - i, w - i, h - i - L)


class EyeOrnamentWidget(QWidget):
    """
    Draws the Cult DSP eye + root-tendril glyph as a faint watermark.
    Maps to: .eye-ornament SVG inside the Transport card.

    Place in the bottom-right of the transport card frame:
        eye = EyeOrnamentWidget(parent=transport_card)
        eye.setFixedSize(130, 90)
        # position via layout or manual move:
        eye.move(card.width() - 140, card.height() - 80)

    The widget is mouse-transparent and does not affect layout.
    """

    def __init__(
        self,
        stroke_color: str = "#ffffff",
        opacity: float = 0.06,        # DARK: 0.06 | LIGHT: 0.04
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFixedSize(130, 90)

    def set_theme(self, stroke_color: str, opacity: float) -> None:
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.update()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setOpacity(self._opacity)
        pen = QPen(self._stroke)
        p.setPen(pen)

        cx, ey = 65.0, 28.0   # eye centre

        # Eye ellipse (outer)
        pen.setWidthF(1.4)
        p.setPen(pen)
        p.drawEllipse(QRectF(cx - 50, ey - 22, 100, 44))

        # Iris circle
        p.drawEllipse(QRectF(cx - 12, ey - 12, 24, 24))

        # Pupil (filled)
        p.setBrush(self._stroke)
        p.drawEllipse(QRectF(cx - 5, ey - 5, 10, 10))
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Accent dots beside eye
        p.setBrush(self._stroke)
        for dx in (-18.0, +18.0):
            p.drawEllipse(QRectF(cx + dx - 1.5, ey - 3 - 1.5, 3, 3))
        for dx in (-10.0, +10.0):
            p.drawEllipse(QRectF(cx + dx - 1, ey - 8 - 1, 2, 2))
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Root / tendril lines downward
        pen.setWidthF(1.0)
        p.setPen(pen)
        p.drawLine(QPointF(cx, ey + 22), QPointF(cx, ey + 55))      # trunk

        pen.setWidthF(0.8)
        p.setPen(pen)
        p.drawLine(QPointF(cx, ey + 30), QPointF(cx - 12, ey + 44)) # left branch
        p.drawLine(QPointF(cx, ey + 30), QPointF(cx + 12, ey + 44)) # right branch

        pen.setWidthF(0.6)
        p.setPen(pen)
        p.drawLine(QPointF(cx, ey + 38), QPointF(cx - 8,  ey + 54)) # left sub
        p.drawLine(QPointF(cx, ey + 38), QPointF(cx + 8,  ey + 54)) # right sub


class SacredGeometryBackground(QWidget):
    """
    Renders faint concentric rings + hexagonal polygon grid.
    Maps to: .app::after pseudo-element in HTML (top-right corner of window).

    Usage in RealtimeWindow._build_ui():
        self._bg_geo = SacredGeometryBackground(parent=root_widget)
        self._bg_geo.lower()   # push behind everything
        # position top-right:
        self._bg_geo.setFixedSize(500, 500)
        self._bg_geo.move(root_widget.width() - 350, -100)
    """

    def __init__(
        self,
        stroke_color: str = "#ffffff",
        opacity: float = 0.05,
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFixedSize(500, 500)

    def set_theme(self, stroke_color: str, opacity: float) -> None:
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.update()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setOpacity(self._opacity)
        pen = QPen(self._stroke, 0.5)
        p.setPen(pen)
        p.setBrush(Qt.BrushStyle.NoBrush)

        cx, cy = 250.0, 250.0
        # Concentric circles  (HTML: r=60,100,140,180)
        for r in (60, 100, 140, 180):
            p.drawEllipse(QRectF(cx - r, cy - r, r * 2, r * 2))

        # Cross hairs
        p.drawLine(QPointF(20, cy), QPointF(480, cy))
        p.drawLine(QPointF(cx, 20), QPointF(cx, 480))

        # Diagonals
        p.drawLine(QPointF(72, 72), QPointF(428, 428))
        p.drawLine(QPointF(428, 72), QPointF(72, 428))

        # Outer hexagon
        import math
        def hex_pt(angle_deg: float, r: float) -> QPointF:
            a = math.radians(angle_deg)
            return QPointF(cx + r * math.cos(a), cy + r * math.sin(a))

        for r in (170, 120):
            pts = [hex_pt(90 + 60 * i, r) for i in range(6)]
            path = QPainterPath()
            path.moveTo(pts[0])
            for pt in pts[1:]:
                path.lineTo(pt)
            path.closeSubpath()
            p.drawPath(path)


# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 3 — CHANGES TO realtimeGUI.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# All changes are styling-only. Do not alter _connect_runner(), _on_start(),
# _on_state_changed(), _on_engine_finished(), or _copy_command().
#
# 3.1  IMPORTS — add at the top of realtimeGUI.py
# ─────────────────────────────────────────────────
#
#   from .realtime_panels.theme import DARK, LIGHT, make_qss
#   from .realtime_panels.brand_widgets import SacredGeometryBackground
#   from PySide6.QtGui import QFont, QFontDatabase
#   from PySide6.QtCore import Qt
#
# 3.2  FONT LOADING — add load_fonts() and call it in __init__ before _build_ui
# ─────────────────────────────────────────────────────────────────────────────
#
#   def _load_fonts(self) -> None:
#       """
#       Register Space Mono + Cormorant Garamond if shipped in a fonts/ dir.
#       Falls back silently to system monospace if not found.
#       """
#       fonts_dir = Path(__file__).parent / "fonts"
#       if fonts_dir.exists():
#           for ttf in fonts_dir.glob("*.ttf"):
#               QFontDatabase.addApplicationFont(str(ttf))
#
# 3.3  __init__ SIGNATURE — add theme parameter
# ─────────────────────────────────────────────
#
#   def __init__(
#       self,
#       repo_root: str = ".",
#       theme: str = "dark",         # "dark" | "light"
#       parent: Optional[QWidget] = None,
#   ) -> None:
#       super().__init__(parent)
#       self._theme = DARK if theme == "dark" else LIGHT
#       self._repo_root = repo_root
#       self._runner = RealtimeRunner(repo_root=repo_root, parent=self)
#       self._load_fonts()
#       self._build_ui()
#       self._connect_runner()
#       # Apply master QSS AFTER _build_ui so objectNames are set
#       QApplication.instance().setStyleSheet(make_qss(self._theme))
#       self.setWindowTitle("spatialroot — Real-Time Engine")
#       self.resize(820, 900)
#
# 3.4  _build_ui() — changes to make
# ────────────────────────────────────
#
# a) ROOT WIDGET
#    Set objectName so QSS targets it:
#        root_widget.setObjectName("Root")        ← already done, keep it
#
# b) HEADER BAR
#    Current: QVBoxLayout with title + subtitle stacked.
#    Change to: QHBoxLayout mirroring the HTML header (logo left, pipeline
#    centre, status right).
#
#    Replace the entire header block with:
#    ─────────────────────────────────────────────────────────────────
#    header = QFrame()
#    header.setObjectName("HeaderBar")
#    header.setFixedHeight(48)
#    header_layout = QHBoxLayout(header)
#    header_layout.setContentsMargins(20, 0, 20, 0)
#    header_layout.setSpacing(0)
#
#    # Logo glyph (SVG rendered by LogoGlyphWidget — see brand_widgets)
#    from .realtime_panels.brand_widgets import LogoGlyphWidget
#    logo_glyph = LogoGlyphWidget(
#        stroke_color=self._theme["text"], parent=header
#    )
#    logo_glyph.setFixedSize(24, 24)
#    header_layout.addWidget(logo_glyph)
#    header_layout.addSpacing(10)
#
#    # Wordmark: "spatialroot  Real-Time Engine"
#    wordmark = QLabel("spatialroot")
#    wordmark.setObjectName("Title")
#    wordmark.setFont(QFont("Cormorant Garamond", 15))
#    header_layout.addWidget(wordmark)
#    header_layout.addSpacing(6)
#    sub = QLabel("Real-Time Engine")
#    sub.setObjectName("Subtitle")
#    sub.setFont(QFont("Cormorant Garamond", 11))
#    header_layout.addWidget(sub)
#
#    header_layout.addStretch()
#
#    # Pipeline label (centred absolutely — use a fixed-width spacer trick)
#    pipeline_lbl = QLabel("ADM  →  LUSID  ⇒  Spatial Render")
#    pipeline_lbl.setObjectName("Muted2")
#    pipeline_lbl.setFont(QFont("Space Mono", 7))
#    pipeline_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
#    header_layout.addWidget(pipeline_lbl)
#
#    header_layout.addStretch()
#
#    # Running status dot + label
#    self._header_dot = QLabel("●")
#    self._header_dot.setObjectName("Muted")
#    self._header_dot.setFont(QFont("Space Mono", 8))
#    header_layout.addWidget(self._header_dot)
#    header_layout.addSpacing(5)
#    self._header_state_lbl = QLabel("IDLE")
#    self._header_state_lbl.setObjectName("Muted")
#    self._header_state_lbl.setFont(QFont("Space Mono", 7))
#    header_layout.addWidget(self._header_state_lbl)
#
#    root_layout.addWidget(header)
#    ─────────────────────────────────────────────────────────────────
#
#    Then wire the header status to the runner in _connect_runner():
#        r.state_changed.connect(self._update_header_state)
#
#    And add:
#    def _update_header_state(self, state_name: str) -> None:
#        self._header_state_lbl.setText(state_name.upper())
#        colours = {
#            "Idle":      self._theme["muted2"],
#            "Launching": self._theme["yellow"],
#            "Running":   self._theme["green"],
#            "Paused":    self._theme["orange"],
#            "Exited":    self._theme["muted2"],
#            "Error":     self._theme["red"],
#        }
#        c = colours.get(state_name, self._theme["muted2"])
#        self._header_dot.setStyleSheet(f"color: {c};")
#        self._header_state_lbl.setStyleSheet(f"color: {c};")
#
# c) SACRED GEOMETRY BACKGROUND
#    After adding the scroll area, insert the background widget:
#
#    self._bg_geo = SacredGeometryBackground(
#        stroke_color=self._theme["text"],
#        opacity=0.05 if self._theme is DARK else 0.07,
#        parent=root_widget,
#    )
#    self._bg_geo.move(root_widget.width() - 350, -100)
#    self._bg_geo.lower()
#
#    Also override resizeEvent to reposition it:
#    def resizeEvent(self, event) -> None:
#        super().resizeEvent(event)
#        if hasattr(self, "_bg_geo"):
#            cw = self.centralWidget()
#            self._bg_geo.move(cw.width() - 350, -100)
#
# d) PANEL CONSTRUCTION — pass theme down
#    Change every panel constructor call to include the theme:
#
#    self._input_panel     = RealtimeInputPanel(theme=self._theme, parent=content)
#    self._transport_panel = RealtimeTransportPanel(theme=self._theme, parent=content)
#    self._controls_panel  = RealtimeControlsPanel(theme=self._theme, parent=content)
#    self._log_panel       = RealtimeLogPanel(theme=self._theme, parent=content)
#
# e) SCROLL AREA MARGINS
#    content_layout.setContentsMargins(20, 16, 20, 20)
#    content_layout.setSpacing(10)        ← was 12, change to 10 (HTML: gap:10px)
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 4 — CHANGES TO RealtimeInputPanel.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# 4.1  ADD theme parameter to __init__
# ─────────────────────────────────────
#
#   def __init__(self, theme: dict = None, parent=None):
#       super().__init__(parent)
#       from .theme import DARK
#       self._theme = theme or DARK
#       self._build_ui()
#       self._connect_signals()
#
# 4.2  CARD FRAME
#    The existing `card = _card()` (QFrame#Card) is correct.
#    Add CornerMarksWidget overlay after the card is fully laid out:
#
#    from .brand_widgets import CornerMarksWidget
#    # At end of _build_ui(), after root_layout.addWidget(card):
#    marks = CornerMarksWidget(color=self._theme["muted2"], parent=card)
#    # Delay geometry assignment until card is shown:
#    QTimer.singleShot(0, lambda: marks.setGeometry(card.rect()))
#
# 4.3  SECTION TITLE LABEL
#    Change:  title = QLabel("Input Configuration")
#    To:
#    title = QLabel("INPUT CONFIGURATION")
#    title.setObjectName("SectionTitle")
#    title.setFont(QFont("Space Mono", 7))
#    # Add a horizontal line after it (mimics section-label::after in HTML):
#    # Use a QFrame as a separator — add to layout immediately after title.
#    sep = QFrame(); sep.setFrameShape(QFrame.Shape.HLine)
#    sep.setStyleSheet(f"color: {self._theme['border_light']}; margin-bottom: 6px;")
#    layout.addWidget(title)
#    layout.addWidget(sep)
#
# 4.4  FIELD LABELS (Source, Speaker Layout, Remap CSV, Buffer Size)
#    Each label already uses _make_row_label().  Update _make_row_label():
#
#    def _make_row_label(self, text: str) -> QLabel:
#        lbl = QLabel(text.upper())
#        lbl.setObjectName("Muted")
#        lbl.setFont(QFont("Space Mono", 7))
#        lbl.setFixedWidth(110)
#        return lbl
#
# 4.5  SOURCE HINT LABEL
#    self._source_hint already exists.  Set its font:
#        self._source_hint.setFont(QFont("Space Mono", 7))
#    And update _on_source_changed to set colour dynamically:
#
#    def _on_source_changed(self, text: str) -> None:
#        text = text.strip()
#        hint, is_adm, is_lusid = self._detect_source(text)
#        self._source_hint.setText(hint)
#        if is_adm or is_lusid:
#            self._source_hint.setStyleSheet(f"color: {self._theme['green']};")
#        else:
#            self._source_hint.setStyleSheet(f"color: {self._theme['red']};")
#        self._scan_check.setEnabled(is_adm or text == "")
#        self.config_changed.emit()
#
# 4.6  BROWSE BUTTONS
#    All three browse QPushButtons already have objectName "FileButton" — good.
#    Add font:
#        self._source_btn.setFont(QFont("Space Mono", 7))
#        self._layout_btn.setFont(QFont("Space Mono", 7))
#        self._remap_btn.setFont(QFont("Space Mono", 7))
#
# 4.7  COMBO BOXES
#    Add font to all QComboBox widgets:
#        self._layout_combo.setFont(QFont("Space Mono", 8))
#        self._remap_combo.setFont(QFont("Space Mono", 8))
#        self._buffer_combo.setFont(QFont("Space Mono", 8))
#
# 4.8  SCAN CHECKBOX
#        self._scan_check.setFont(QFont("Space Mono", 7))
#
# 4.9  LAYOUT SPACING
#    layout.setContentsMargins(20, 16, 20, 16)
#    layout.setSpacing(8)           ← was 10
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 5 — CHANGES TO RealtimeTransportPanel.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# 5.1  ADD theme parameter (same pattern as InputPanel above)
#
# 5.2  KILL BUTTON objectName
#    Change: self._kill_btn = self._btn("Kill", "SecondaryButton")
#    To:     self._kill_btn = self._btn("Kill", "KillButton")
#    This picks up the danger/red styling from QSS.
#
# 5.3  EYE ORNAMENT WATERMARK
#    After constructing the card frame but before adding buttons, insert:
#
#    from .brand_widgets import EyeOrnamentWidget
#    self._eye = EyeOrnamentWidget(
#        stroke_color=self._theme["text"],
#        opacity=0.06 if self._theme.get("bg") == "#0f0f0f" else 0.04,
#        parent=card,
#    )
#    # Position bottom-right; update on resize via resizeEvent override.
#    # Use a QTimer to defer until card has been laid out:
#    QTimer.singleShot(0, lambda: self._eye.move(
#        card.width() - self._eye.width() - 8,
#        card.height() - self._eye.height() - 4,
#    ))
#
# 5.4  SECTION TITLE
#    title.setText("TRANSPORT")
#    title.setFont(QFont("Space Mono", 7))
#
# 5.5  BUTTON FONTS
#    Add to the _btn() static method:
#        b.setFont(QFont("Space Mono", 7))
#
# 5.6  STATUS PILL  (_refresh method)
#    The pill QFrame already has objectName "Pill".
#    Change _refresh() pill colour logic to:
#
#    def _refresh(self) -> None:
#        ...  # existing enable/disable logic unchanged
#
#        s = self._current_state
#        label = s.value.upper()
#        self._pill_label.setText(f"●  {label}")
#        self._pill_label.setFont(QFont("Space Mono", 7))
#
#        # Map state → colours
#        from ..realtime_panels.theme import PILL_STYLE
#        bg, dot = PILL_STYLE.get(s.value, ("rgba(110,110,115,0.15)", "#8E8E93"))
#        # Override Running and Error with theme-adaptive colours:
#        if s.value == "Running":
#            bg  = self._theme["green_bg"]
#            dot = self._theme["green"]
#        elif s.value == "Error":
#            bg  = self._theme["red_bg"]
#            dot = self._theme["red"]
#
#        self._pill_label.setStyleSheet(f"color: {dot}; background: transparent;")
#        pill_frame = self._pill_label.parentWidget()
#        pill_frame.setStyleSheet(
#            f"QFrame#Pill {{ background: {bg}; "
#            f"border: 1px solid {dot.replace(')', ', 0.3)').replace('rgb', 'rgba') if 'rgba' not in dot else dot}; "
#            f"border-radius: 10px; }}"
#        )
#        # Simpler border approach — just use a fixed low-alpha version:
#        pill_frame.setStyleSheet(
#            f"QFrame#Pill {{ background: {bg}; "
#            f"border: 1px solid rgba(128,128,128,0.2); "
#            f"border-radius: 10px; }}"
#        )
#
# 5.7  OSC LABEL
#    self._osc_label.setFont(QFont("Space Mono", 7))
#    self._osc_label.setObjectName("Muted2")
#
# 5.8  LAYOUT SPACING
#    layout.setContentsMargins(20, 16, 20, 16)
#    layout.setSpacing(8)
#    row1 and row2: setSpacing(6)
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 6 — CHANGES TO RealtimeControlsPanel.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# 6.1  ADD theme parameter (same pattern)
#      Pass it down into each _ParamRow constructor (for future use).
#
# 6.2  SECTION TITLE
#    title.setText("RUNTIME CONTROLS")
#    title.setFont(QFont("Space Mono", 7))
#    # Add OSC address hint inline (maps to the HTML subtitle span):
#    osc_hint = QLabel("live osc → 127.0.0.1:9009")
#    osc_hint.setObjectName("Muted2")
#    osc_hint.setFont(QFont("Space Mono", 6))
#    # Place in the same HBoxLayout row as the title, or add right-aligned.
#
# 6.3  _ParamRow — label fonts
#    In _ParamRow.__init__():
#        lbl.setFont(QFont("Space Mono", 7))
#        self._spin.setFont(QFont("Space Mono", 8))
#
#    Add an OSC address micro-label to the right of the spinbox:
#    (Map to .param-osc in HTML — shows "/gain", "/focus" etc.)
#
#    self._osc_lbl = QLabel(osc_address_suffix)  # pass as new __init__ param
#    self._osc_lbl.setObjectName("Muted2")
#    self._osc_lbl.setFont(QFont("Space Mono", 6))
#    self._osc_lbl.setFixedWidth(52)
#    self._osc_lbl.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
#    layout.addWidget(self._osc_lbl)
#
#    Update _ParamRow instantiation calls in _build_ui() to pass osc labels:
#    self._gain_row  = _ParamRow("Master Gain",      ..., osc_label="/gain")
#    self._focus_row = _ParamRow("DBAP Focus",       ..., osc_label="/focus")
#    self._spk_row   = _ParamRow("Speaker Mix (dB)", ..., osc_label="/spk_mix")
#    self._sub_row   = _ParamRow("Sub Mix (dB)",     ..., osc_label="/sub_mix")
#
# 6.4  AUTO-COMP CHECKBOX
#    self._auto_comp_check.setText("FOCUS AUTO-COMPENSATION")
#    self._auto_comp_check.setFont(QFont("Space Mono", 7))
#
# 6.5  LAYOUT SPACING
#    layout.setContentsMargins(20, 16, 20, 16)
#    layout.setSpacing(8)
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 7 — CHANGES TO RealtimeLogPanel.py
# ═══════════════════════════════════════════════════════════════════════════════
#
# 7.1  ADD theme parameter (same pattern)
#
# 7.2  SECTION TITLE
#    title.setText("ENGINE LOG")
#    title.setFont(QFont("Space Mono", 7))
#
# 7.3  PHASE TAG LABEL
#    Add a QLabel#PhaseTag showing "PHASE 10" to the right of the title,
#    in the header HBoxLayout (between title and clear button):
#
#    phase_tag = QLabel("PHASE 10")
#    phase_tag.setObjectName("PhaseTag")
#    phase_tag.setFont(QFont("Space Mono", 6))
#    header.addWidget(phase_tag)
#    header.addSpacing(8)
#
# 7.4  CLEAR BUTTON
#    clear_btn.setObjectName("ClearButton")   ← change from "SecondaryButton"
#    clear_btn.setFont(QFont("Space Mono", 7))
#
# 7.5  LOG WIDGET FONT
#    The QPlainTextEdit already sets a monospace font.
#    Replace with:
#        mono = QFont("Space Mono")
#        mono.setPointSize(9)
#        mono.setStyleHint(QFont.StyleHint.Monospace)
#        self._log.setFont(mono)
#
# 7.6  COLOURED LOG LINES (append_line enhancement)
#    Wrap append_line() to detect prefixes and apply HTML colour:
#
#    def append_line(self, text: str) -> None:
#        sb = self._log.verticalScrollBar()
#        at_bottom = sb.value() >= sb.maximum() - 4
#
#        # Colour-code by prefix (mirrors HTML log-line classes)
#        colour = self._theme["muted"]        # default: .info
#        if text.startswith("[GUI]"):
#            colour = self._theme["muted2"]   # .gui
#        elif "[stderr] Warning" in text or "[stderr] warning" in text:
#            colour = self._theme["yellow"]   # .warn
#        elif ("ParameterServer listening" in text
#              or "DBAP renderer running"  in text
#              or "Engine exited cleanly"  in text):
#            colour = self._theme["green"]    # .ok
#        elif "ERROR" in text or "error" in text.lower():
#            colour = self._theme["red"]      # .error
#
#        # Use HTML insertion for colour (QPlainTextEdit supports appendHtml)
#        escaped = (text.replace("&", "&amp;")
#                       .replace("<", "&lt;")
#                       .replace(">", "&gt;"))
#        self._log.appendHtml(
#            f'<span style="color:{colour}; font-family:\'Space Mono\',monospace;">'
#            f'{escaped}</span>'
#        )
#
#        # Trim excess (existing logic unchanged)
#        doc = self._log.document()
#        while doc.blockCount() > MAX_LINES:
#            cursor = QTextCursor(doc.begin())
#            cursor.select(QTextCursor.SelectionType.BlockUnderCursor)
#            cursor.movePosition(
#                QTextCursor.MoveOperation.NextCharacter,
#                QTextCursor.MoveMode.KeepAnchor,
#            )
#            cursor.removeSelectedText()
#
#        if at_bottom:
#            self._log.moveCursor(QTextCursor.MoveOperation.End)
#
# 7.7  LAYOUT SPACING
#    layout.setContentsMargins(20, 16, 20, 16)
#    layout.setSpacing(8)
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 8 — ADDITIONAL BRAND WIDGET: LogoGlyphWidget
# ═══════════════════════════════════════════════════════════════════════════════
#
# Add this class to brand_widgets.py (referenced in Section 3.4b above).
# It renders the eye + root glyph at small size for the header logo.
#
# class LogoGlyphWidget(QWidget):
#     """
#     24×24 px eye + root glyph for the header bar.
#     Maps to: the inline SVG logo-glyph in the HTML header.
#     """
#     def __init__(self, stroke_color: str = "#e8e8e4", parent=None):
#         super().__init__(parent)
#         self._stroke = QColor(stroke_color)
#         self.setFixedSize(24, 24)
#         self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
#
#     def set_color(self, color: str) -> None:
#         self._stroke = QColor(color)
#         self.update()
#
#     def paintEvent(self, _event):
#         p = QPainter(self)
#         p.setRenderHint(QPainter.RenderHint.Antialiasing)
#         pen = QPen(self._stroke, 0.8)
#         p.setPen(pen)
#         p.setBrush(Qt.BrushStyle.NoBrush)
#
#         # Eye ellipse
#         p.drawEllipse(QRectF(6, 4, 12, 8))
#
#         # Pupil
#         p.setBrush(self._stroke)
#         p.drawEllipse(QRectF(10.5, 6.5, 3, 3))
#         p.setBrush(Qt.BrushStyle.NoBrush)
#
#         # Dot in pupil (bg colour)
#         # (skip — too small at 24px)
#
#         # Accent dots
#         p.setBrush(self._stroke)
#         p.drawEllipse(QRectF(4.5, 7.2, 1.6, 1.6))
#         p.drawEllipse(QRectF(17.9, 7.2, 1.6, 1.6))
#         p.drawEllipse(QRectF(9.0, 4.0, 1.0, 1.0))
#         p.drawEllipse(QRectF(14.0, 4.0, 1.0, 1.0))
#         p.setBrush(Qt.BrushStyle.NoBrush)
#
#         # Root trunk + branches
#         pen.setWidthF(0.7); p.setPen(pen)
#         p.drawLine(QPointF(12, 12), QPointF(12, 20))
#
#         pen.setWidthF(0.6); p.setPen(pen)
#         p.drawLine(QPointF(12, 14), QPointF(8,  18))
#         p.drawLine(QPointF(12, 14), QPointF(16, 18))
#
#         pen.setWidthF(0.5); p.setPen(pen)
#         p.drawLine(QPointF(12, 16), QPointF(9,  20))
#         p.drawLine(QPointF(12, 16), QPointF(15, 20))
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 9 — FONT FILES
# ═══════════════════════════════════════════════════════════════════════════════
#
# Place these files in:  <repo_root>/realtime_panels/fonts/
#
# Required:
#   SpaceMono-Regular.ttf
#   SpaceMono-Bold.ttf
#   CormorantGaramond-Light.ttf
#   CormorantGaramond-LightItalic.ttf
#   CormorantGaramond-Regular.ttf
#
# Free download sources:
#   Space Mono:          https://fonts.google.com/specimen/Space+Mono
#   Cormorant Garamond:  https://fonts.google.com/specimen/Cormorant+Garamond
#
# If fonts are absent, PySide6 will silently fall back to the system monospace.
# The QSS and QFont calls are written to degrade gracefully.
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 10 — LAUNCH ENTRYPOINT CHANGES
# ═══════════════════════════════════════════════════════════════════════════════
#
# Wherever RealtimeWindow is instantiated (e.g. main.py or __main__.py):
#
#   import sys, argparse
#   from PySide6.QtWidgets import QApplication
#   from .realtimeGUI import RealtimeWindow
#
#   def main():
#       parser = argparse.ArgumentParser()
#       parser.add_argument("--theme", choices=["dark","light"], default="light")
#       parser.add_argument("--repo-root", default=".")
#       args = parser.parse_args()
#
#       app = QApplication(sys.argv)
#       app.setApplicationName("spatialroot")
#
#       win = RealtimeWindow(repo_root=args.repo_root, theme=args.theme)
#       win.show()
#       sys.exit(app.exec())
#
#   if __name__ == "__main__":
#       main()
#
#
# ═══════════════════════════════════════════════════════════════════════════════
# SECTION 11 — VISUAL QA CHECKLIST
# ═══════════════════════════════════════════════════════════════════════════════
#
# After implementing, verify against the HTML mockups:
#
# □ Window background matches --bg exactly (dark: #0f0f0f, light: #f0ede8)
# □ Card frames have surface background + hairline border + 4px radius
# □ Corner tick marks appear on the Input Configuration card (all 4 corners)
# □ Eye ornament watermark is faintly visible bottom-right of Transport card
# □ Sacred geometry rings/hexagons faintly visible top-right of window
# □ Header: logo glyph left, pipeline label centred, state dot+label right
# □ All labels use Space Mono; wordmark uses Cormorant Garamond
# □ Section titles are ALL CAPS, spaced, muted colour, with a hairline rule
# □ Browse buttons use FileButton style (transparent bg, muted border)
# □ Kill button uses red/danger colouring
# □ Status pill background + dot colour changes with each engine state
# □ Sliders: 2px groove, dot handle with surface fill + text border
# □ Log lines: muted default, green for OK events, yellow for warnings, red for errors
# □ Scrollbar is 4px, minimal, no arrows
# □ LIGHT mode: all backgrounds warm off-white, text deep charcoal, borders dark
# □ No logic, signals, or public APIs were altered
