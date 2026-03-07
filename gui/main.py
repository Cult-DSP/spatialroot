from __future__ import annotations

import os
import sys
import subprocess
from pathlib import Path

from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QMessageBox, QTabWidget

from background import RadialBackground
from pipeline_runner import PipelineRunner, PipelineConfig
from transcode_runner import TranscodeRunner
from utils.effects import apply_card_shadow, apply_button_shadow

from widgets.header import HeaderBar
from widgets.input_panel import InputPanel
from widgets.render_panel import RenderPanel
from widgets.pipeline_panel import PipelinePanel
from widgets.transcode_panel import TranscodePanel


def load_qss(app: QApplication, qss_path: Path):
    if qss_path.exists():
        app.setStyleSheet(qss_path.read_text(encoding="utf-8"))


class MainWindow(QWidget):
    def __init__(self, repo_root: str):
        super().__init__()
        self.setObjectName("Root")
        self.repo_root = repo_root
        self.setWindowTitle("spatialroot")
        self.resize(1100, 800)
        self.setMinimumSize(1000, 700)

        self.bg = RadialBackground(self)
        self.bg.lower()

        # ── Pipeline runner (Render tab) ───────────────────────────────
        self.runner = PipelineRunner(repo_root=self.repo_root, parent=self)
        self.runner.output.connect(self._on_output)
        self.runner.step_changed.connect(self._on_step)
        self.runner.started.connect(self._on_started)
        self.runner.finished.connect(self._on_finished)

        # ── Transcode runner (Transcode tab) ──────────────────────────
        self.transcode_runner = TranscodeRunner(repo_root=self.repo_root, parent=self)
        self.transcode_runner.output.connect(self._on_transcode_output)
        self.transcode_runner.finished.connect(self._on_transcode_finished)
        self.transcode_runner.started.connect(self._on_transcode_started)

        # ── Root layout ───────────────────────────────────────────────
        root = QVBoxLayout(self)
        root.setContentsMargins(24, 24, 24, 24)
        root.setSpacing(16)

        self.header = HeaderBar(self)
        root.addWidget(self.header)

        # ── Tab widget ────────────────────────────────────────────────
        self.tabs = QTabWidget(self)
        self.tabs.setObjectName("MainTabs")
        root.addWidget(self.tabs)

        # --- Render tab -----------------------------------------------
        render_tab = QWidget()
        render_layout = QVBoxLayout(render_tab)
        render_layout.setContentsMargins(0, 12, 0, 0)
        render_layout.setSpacing(24)

        main_row = QHBoxLayout()
        main_row.setSpacing(48)

        self.input_panel = InputPanel(self)
        self.input_panel.file_selected.connect(self._on_file_selected)
        self.input_panel.output_path_changed.connect(self._on_output_path_changed)
        main_row.addWidget(self.input_panel, 1)

        self.render_panel = RenderPanel(self)
        main_row.addWidget(self.render_panel, 1)

        render_layout.addLayout(main_row)

        self.pipeline_panel = PipelinePanel(self)
        self.pipeline_panel.run_clicked.connect(self._run_pipeline)
        render_layout.addWidget(self.pipeline_panel)

        self.runner.progress_changed.connect(self.pipeline_panel.set_progress)

        apply_card_shadow(self.input_panel)
        apply_card_shadow(self.render_panel)
        apply_card_shadow(self.pipeline_panel, blur=28, alpha=22, offset_y=6)
        apply_button_shadow(self.pipeline_panel.run_btn)

        self.tabs.addTab(render_tab, "Render")

        # --- Transcode tab --------------------------------------------
        transcode_tab = QWidget()
        transcode_layout = QVBoxLayout(transcode_tab)
        transcode_layout.setContentsMargins(0, 12, 0, 0)
        transcode_layout.setSpacing(0)

        self.transcode_panel = TranscodePanel(self)
        self.transcode_panel.run_clicked.connect(self._run_transcode)
        self.transcode_panel.open_report_clicked.connect(self._open_report)
        transcode_layout.addWidget(self.transcode_panel)
        transcode_layout.addStretch(1)

        apply_card_shadow(self.transcode_panel, blur=28, alpha=22, offset_y=6)

        self.tabs.addTab(transcode_tab, "Transcode")

        # ── State ─────────────────────────────────────────────────────
        self._source_path = None
        self._output_path = self.input_panel.get_output_path()
    def _on_output_path_changed(self, path: str):
        self._output_path = path.strip() or "processedData/spatial_render.wav"

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self.bg.setGeometry(self.rect())

    def _on_file_selected(self, path: str):
        self._source_path = path
        self.pipeline_panel.append_text(f"Selected: {path}\n")

    def _run_pipeline(self):
        if not self._source_path:
            QMessageBox.information(self, "Select Input", "Please select an ADM WAV file first.")
            return

        mode, resolution, gain, create_analysis, speaker_layout = self.render_panel.get_values()
        output_path = self.input_panel.get_output_path()

        cfg = PipelineConfig(
            source_path=self._source_path,
            speaker_layout=speaker_layout,
            render_mode=mode,
            resolution=resolution,
            master_gain=gain,
            create_analysis=create_analysis,
            output_path=output_path,
        )

        self.pipeline_panel.clear()
        self.pipeline_panel.append_text("Pipeline init → Run pipeline to render spatial audio\n")
        self.runner.run(cfg)

    def _on_started(self):
        self.pipeline_panel.set_running(True)
        self.pipeline_panel.append_text("Starting pipeline...\n")

    def _on_output(self, text: str):
        self.pipeline_panel.append_text(text)
        t = text.lower()
        if "extracting adm" in t or "metadata" in t:
            self.input_panel.set_progress_flags(metadata=True, activity=False)
        if "channel activity" in t:
            self.input_panel.set_progress_flags(metadata=True, activity=True)

    def _on_step(self, step: int):
        self.pipeline_panel.set_step(step)

    def _on_finished(self, exit_code: int):
        self.pipeline_panel.set_running(False)
        if exit_code == 0:
            self.pipeline_panel.set_done_all()
            self.pipeline_panel.append_text("\nDone.\n")
        else:
            self.pipeline_panel.append_text(f"\nPipeline failed with exit code {exit_code}.\n")

    # ── Transcode tab handlers ─────────────────────────────────────────

    def _run_transcode(self, in_path: str, in_format: str, out_path: str, lfe_mode: str):
        self.transcode_panel.set_running(True)
        self.transcode_runner.run(
            in_path=in_path,
            in_format=in_format,
            out_path=out_path,
            lfe_mode=lfe_mode,
        )

    def _on_transcode_started(self):
        self.transcode_panel.append_log("cult-transcoder started…")

    def _on_transcode_output(self, text: str):
        self.transcode_panel.append_log(text)

    def _on_transcode_finished(self, exit_code: int, report_path: str):
        success = (exit_code == 0)
        self.transcode_panel.set_finished(success, report_path or None)
        if success:
            self.transcode_panel.append_log("Transcode complete.")
        else:
            self.transcode_panel.append_log(f"cult-transcoder exited with code {exit_code}.")

    def _open_report(self, report_path: str):
        """Open the report JSON with the system default application."""
        import subprocess, sys
        try:
            if sys.platform == "darwin":
                subprocess.run(["open", report_path], check=False)
            elif sys.platform.startswith("linux"):
                subprocess.run(["xdg-open", report_path], check=False)
            else:
                os.startfile(report_path)  # Windows
        except Exception as e:
            QMessageBox.warning(self, "Open Report", f"Could not open report:\n{e}")


def main():
    UI_DEBUG = False  # Set to True to show widget boundaries for debugging
    app = QApplication(sys.argv)
    here = Path(__file__).resolve().parent
    load_qss(app, here / "styles.qss")
    
    if UI_DEBUG:
        # Add debug borders to all widgets
        debug_style = """
        QWidget {
            border: 1px solid red;
        }
        """
        app.setStyleSheet(app.styleSheet() + debug_style)

    repo_root = str(Path(__file__).resolve().parent.parent)
    win = MainWindow(repo_root=repo_root)
    win.show()
    win.activateWindow()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
