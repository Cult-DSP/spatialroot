"""
realtimeGUI.py — RealtimeWindow: top-level PySide6 window for the
spatialroot Real-Time Spatial Audio Engine GUI.

Wires together:
  RealtimeInputPanel     → launch-time config
  RealtimeTransportPanel → Start / Stop / Kill / Restart / Pause / Play
  RealtimeControlsPanel  → live OSC sliders (gain, focus, mix, auto-comp)
  RealtimeLogPanel       → stdout/stderr console

Delegates all process management to RealtimeRunner (QProcess + OSC sender).

Phase 10 — GUI Agent.
"""

from __future__ import annotations

from pathlib import Path
from typing import Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QClipboard, QGuiApplication, QFont, QFontDatabase
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QScrollArea,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

from .realtime_runner import RealtimeConfig, RealtimeRunner, RealtimeRunnerState
from .realtime_transcoder_runner import RealtimeTranscoderRunner
from .realtime_panels.theme import DARK, LIGHT, make_qss
from .realtime_panels.brand_widgets import SacredGeometryBackground
from .realtime_panels.RealtimeInputPanel import RealtimeInputPanel
from .realtime_panels.RealtimeTransportPanel import RealtimeTransportPanel
from .realtime_panels.RealtimeControlsPanel import RealtimeControlsPanel
from .realtime_panels.RealtimeLogPanel import RealtimeLogPanel
from .realtime_panels.RealtimeTranscodePanel import RealtimeTranscodePanel


class RealtimeWindow(QMainWindow):
    """
    Standalone main window for the real-time engine GUI.

    Parameters
    ----------
    repo_root : str
        Absolute path to the spatialroot project root. Passed to RealtimeRunner
        so QProcess working directory resolves runRealtime.py correctly.
    """

    def __init__(
        self,
        repo_root: str = ".",
        theme: str = "light",         # "dark" | "light"
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._theme = DARK if theme == "dark" else LIGHT
        self._repo_root = repo_root
        self._runner = RealtimeRunner(repo_root=repo_root, parent=self)
        self._tc_runner = RealtimeTranscoderRunner(repo_root=repo_root, parent=self)
        self._load_fonts()
        self._build_ui()
        self._connect_runner()
        # Apply master QSS AFTER _build_ui so objectNames are set
        from PySide6.QtWidgets import QApplication
        QApplication.instance().setStyleSheet(make_qss(self._theme))
        self.setWindowTitle("Spatial Root — Real-Time Engine")
        self.resize(820, 900)

    def _load_fonts(self) -> None:
        """
        Register Space Mono + Cormorant Garamond if shipped in a fonts/ dir.
        Falls back silently to system monospace if not found.
        """
        fonts_dir = Path(__file__).parent / "fonts"
        if fonts_dir.exists():
            for ttf in fonts_dir.glob("*.ttf"):
                QFontDatabase.addApplicationFont(str(ttf))

    # ── UI construction ──────────────────────────────────────────────────

    def _build_ui(self) -> None:
        # Root widget with background
        root_widget = QWidget()
        root_widget.setObjectName("Root")
        self.setCentralWidget(root_widget)

        root_layout = QVBoxLayout(root_widget)
        root_layout.setContentsMargins(0, 0, 0, 0)
        root_layout.setSpacing(0)

        # ── Header bar ────────────────────────────────────────────────
        header = QFrame()
        header.setObjectName("HeaderBar")
        header.setFixedHeight(48)
        header_layout = QHBoxLayout(header)
        header_layout.setContentsMargins(20, 0, 20, 0)
        header_layout.setSpacing(0)

        # Logo glyph (SVG rendered by LogoGlyphWidget — see brand_widgets)
        from .realtime_panels.brand_widgets import LogoGlyphWidget
        logo_glyph = LogoGlyphWidget(
            stroke_color=self._theme["text"], parent=header
        )
        logo_glyph.setFixedSize(24, 24)
        header_layout.addWidget(logo_glyph)
        header_layout.addSpacing(10)

        # Wordmark: "Spatial Root  Real-Time Engine"
        wordmark = QLabel("Spatial Root")
        wordmark.setObjectName("Title")
        wordmark.setFont(QFont("Cormorant Garamond", 15))
        header_layout.addWidget(wordmark)
        header_layout.addSpacing(6)
        sub = QLabel("Real-Time Engine")
        sub.setObjectName("Subtitle")
        sub.setFont(QFont("Cormorant Garamond", 11))
        header_layout.addWidget(sub)

        header_layout.addStretch()

        # Pipeline label (centred absolutely — use a fixed-width spacer trick)
        pipeline_lbl = QLabel("ADM  →  LUSID  ⇒  Spatial Render")
        pipeline_lbl.setObjectName("Muted2")
        pipeline_lbl.setFont(QFont("Space Mono", 7))
        pipeline_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        header_layout.addWidget(pipeline_lbl)

        header_layout.addStretch()

        # Running status dot + label
        self._header_dot = QLabel("●")
        self._header_dot.setObjectName("Muted")
        self._header_dot.setFont(QFont("Space Mono", 8))
        header_layout.addWidget(self._header_dot)
        header_layout.addSpacing(5)
        self._header_state_lbl = QLabel("IDLE")
        self._header_state_lbl.setObjectName("Muted")
        self._header_state_lbl.setFont(QFont("Space Mono", 7))
        header_layout.addWidget(self._header_state_lbl)

        root_layout.addWidget(header)

        # ── Scrollable content area — "Engine" tab ────────────────────
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(20, 16, 20, 20)
        content_layout.setSpacing(10)

        # Panels
        self._input_panel     = RealtimeInputPanel(theme=self._theme, parent=content)
        self._transport_panel = RealtimeTransportPanel(theme=self._theme, parent=content)
        self._controls_panel  = RealtimeControlsPanel(theme=self._theme, parent=content)
        self._log_panel       = RealtimeLogPanel(theme=self._theme, parent=content)

        content_layout.addWidget(self._input_panel)
        content_layout.addWidget(self._transport_panel)
        content_layout.addWidget(self._controls_panel)
        content_layout.addWidget(self._log_panel)
        content_layout.addStretch()

        scroll.setWidget(content)

        # ── Transcode tab ─────────────────────────────────────────────
        transcode_scroll = QScrollArea()
        transcode_scroll.setWidgetResizable(True)
        transcode_scroll.setFrameShape(QFrame.Shape.NoFrame)
        transcode_scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        transcode_content = QWidget()
        transcode_layout = QVBoxLayout(transcode_content)
        transcode_layout.setContentsMargins(20, 16, 20, 20)
        transcode_layout.setSpacing(10)

        self._transcode_panel = RealtimeTranscodePanel(theme=self._theme, parent=transcode_content)
        transcode_layout.addWidget(self._transcode_panel)
        transcode_layout.addStretch()

        transcode_scroll.setWidget(transcode_content)

        # ── QTabWidget wrapping both tabs ─────────────────────────────
        self._tabs = QTabWidget()
        self._tabs.setFont(QFont("Space Mono", 7))
        self._tabs.addTab(scroll, "ENGINE")
        self._tabs.addTab(transcode_scroll, "TRANSCODE")

        root_layout.addWidget(self._tabs)

        # Sacred geometry background
        self._bg_geo = SacredGeometryBackground(
            stroke_color=self._theme["text"],
            opacity=0.05 if self._theme is DARK else 0.07,
            parent=root_widget,
        )
        self._bg_geo.move(root_widget.width() - 350, -100)
        self._bg_geo.lower()

    # ── Runner signal wiring ─────────────────────────────────────────────

    def _connect_runner(self) -> None:
        r = self._runner

        r.output.connect(self._log_panel.append_line)
        r.state_changed.connect(self._on_state_changed)
        r.state_changed.connect(self._update_header_state)
        r.finished.connect(self._on_engine_finished)
        # When the C++ ParameterServer is confirmed listening, flush the current
        # GUI control values so the engine starts with whatever the user has set.
        r.engine_ready.connect(self._controls_panel.flush_to_osc)

        # Transport panel → runner
        t = self._transport_panel
        t.start_requested.connect(self._on_start)
        t.stop_requested.connect(self._runner.stop_graceful)
        t.kill_requested.connect(self._runner.kill)
        t.restart_requested.connect(self._runner.restart)
        t.pause_requested.connect(self._runner.pause)
        t.play_requested.connect(self._runner.play)
        t.copy_command_requested.connect(self._copy_command)

        # Controls panel → runner OSC
        c = self._controls_panel
        c.osc_scheduled.connect(self._runner.schedule_osc)
        c.osc_immediate.connect(self._runner.send_osc)

        # ── Transcoder runner ────────────────────────────────────────
        tc = self._tc_runner
        tc.output.connect(self._transcode_panel.append_line)
        tc.started.connect(lambda: self._transcode_panel.set_running(True))
        tc.finished.connect(self._on_transcode_finished)

        # Transcode panel → runner
        self._transcode_panel.run_requested.connect(self._on_run_transcode)

    # ── Transport handlers ───────────────────────────────────────────────

    def _on_start(self) -> None:
        err = self._input_panel.validate()
        if err:
            self._log_panel.append_line(f"[GUI] Cannot start: {err}")
            return

        cfg = RealtimeConfig(
            source_path    = self._input_panel.get_source_path(),
            speaker_layout = self._input_panel.get_layout_path(),
            remap_csv      = self._input_panel.get_remap_csv(),
            buffer_size    = self._input_panel.get_buffer_size(),
            # scan_audio removed — Phase 3 (2026-03-04): cult-transcoder handles
            # BW64 extraction internally; all channels assumed active.
        )

        # Reset controls to defaults before each launch
        self._controls_panel.reset_to_defaults()
        # Update OSC port label in transport panel
        self._transport_panel.set_osc_port(cfg.osc_port)

        self._log_panel.append_line(
            f"[GUI] Starting engine — source: {cfg.source_path}"
        )
        self._runner.start(cfg)

    def _on_state_changed(self, state_name: str) -> None:
        self._transport_panel.update_state(state_name)
        self._controls_panel.update_state(state_name)

        try:
            s = RealtimeRunnerState(state_name)
        except ValueError:
            s = RealtimeRunnerState.ERROR

        # Lock input fields while engine is active
        running = s in (RealtimeRunnerState.LAUNCHING,
                        RealtimeRunnerState.RUNNING,
                        RealtimeRunnerState.PAUSED)
        self._input_panel.set_enabled_for_state(running)

    def _on_engine_finished(self, exit_code: int) -> None:
        self._transport_panel.set_exit_code(exit_code)
        self._log_panel.append_line(
            f"[GUI] Engine process finished (exit code {exit_code})."
        )

    def _copy_command(self) -> None:
        cmd = self._runner.last_command
        if cmd:
            QGuiApplication.clipboard().setText(cmd)
            self._log_panel.append_line(f"[GUI] Copied to clipboard: {cmd}")
        else:
            self._log_panel.append_line("[GUI] No command to copy — start the engine first.")

    def _update_header_state(self, state_name: str) -> None:
        self._header_state_lbl.setText(state_name.upper())
        colours = {
            "Idle":      self._theme["muted2"],
            "Launching": self._theme["yellow"],
            "Running":   self._theme["green"],
            "Paused":    self._theme["orange"],
            "Exited":    self._theme["muted2"],
            "Error":     self._theme["red"],
        }
        c = colours.get(state_name, self._theme["muted2"])
        self._header_dot.setStyleSheet(f"color: {c};")
        self._header_state_lbl.setStyleSheet(f"color: {c};")

    # ── Transcoder handlers ──────────────────────────────────────────────

    def _on_run_transcode(
        self, in_path: str, in_format: str, out_path: str, lfe_mode: str
    ) -> None:
        self._transcode_panel.append_line(
            f"[GUI] Starting transcode — {in_path}"
        )
        self._tc_runner.run(
            in_path=in_path,
            in_format=in_format,
            out_path=out_path,
            lfe_mode=lfe_mode,
        )

    def _on_transcode_finished(self, exit_code: int, report_path: str) -> None:
        success = exit_code == 0
        self._transcode_panel.set_finished(success, report_path or None)

    def resizeEvent(self, event) -> None:
        super().resizeEvent(event)
        if hasattr(self, "_bg_geo"):
            cw = self.centralWidget()
            self._bg_geo.move(cw.width() - 350, -100)
