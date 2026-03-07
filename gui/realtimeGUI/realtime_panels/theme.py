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

    /* ── Tab widget ── */
    QTabWidget::pane {{
        border: none;
        background: {t["bg"]};
    }}
    QTabBar {{
        background: {t["surface"]};
        border-bottom: 1px solid {t["border_light"]};
    }}
    QTabBar::tab {{
        background: transparent;
        color: {t["muted2"]};
        border: none;
        border-bottom: 2px solid transparent;
        padding: 8px 20px;
        font-size: 7pt;
        letter-spacing: 2px;
    }}
    QTabBar::tab:selected {{
        color: {t["text"]};
        border-bottom: 2px solid {t["text"]};
    }}
    QTabBar::tab:hover:!selected {{
        color: {t["muted"]};
        background: {t["border"]};
    }}
    """