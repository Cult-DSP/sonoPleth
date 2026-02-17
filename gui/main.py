from __future__ import annotations

import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication, QWidget, QVBoxLayout, QHBoxLayout, QMessageBox

from background import RadialBackground
from pipeline_runner import PipelineRunner, PipelineConfig

from widgets.header import HeaderBar
from widgets.input_panel import InputPanel
from widgets.render_panel import RenderPanel
from widgets.pipeline_panel import PipelinePanel


def load_qss(app: QApplication, qss_path: Path):
    if qss_path.exists():
        app.setStyleSheet(qss_path.read_text(encoding="utf-8"))


class MainWindow(QWidget):
    def __init__(self, repo_root: str):
        super().__init__()
        self.setObjectName("Root")
        self.repo_root = repo_root
        self.setWindowTitle("sonoPleth")
        self.resize(1100, 800)
        self.setMinimumSize(1000, 700)

        self.bg = RadialBackground(self)
        self.bg.lower()

        self.runner = PipelineRunner(repo_root=self.repo_root, parent=self)
        self.runner.output.connect(self._on_output)
        self.runner.step_changed.connect(self._on_step)
        self.runner.started.connect(self._on_started)
        self.runner.finished.connect(self._on_finished)

        root = QVBoxLayout(self)
        root.setContentsMargins(24, 24, 24, 24)
        root.setSpacing(24)

        self.header = HeaderBar(self)
        root.addWidget(self.header)

        main_row = QHBoxLayout()
        main_row.setSpacing(48)

        self.input_panel = InputPanel(self)
        self.input_panel.file_selected.connect(self._on_file_selected)
        main_row.addWidget(self.input_panel, 1)

        self.render_panel = RenderPanel(self)
        self.render_panel.run_clicked.connect(self._run_pipeline)
        main_row.addWidget(self.render_panel, 1)

        root.addLayout(main_row)

        self.pipeline_panel = PipelinePanel(self)
        root.addWidget(self.pipeline_panel)

        self._source_path = None

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

        cfg = PipelineConfig(
            source_path=self._source_path,
            speaker_layout=speaker_layout,
            render_mode=mode,
            resolution=resolution,
            master_gain=gain,
            create_analysis=create_analysis,
        )

        self.pipeline_panel.clear()
        self.pipeline_panel.append_text("Pipeline init → Run pipeline to render spatial audio\n")
        self.runner.run(cfg)

    def _on_started(self):
        self.render_panel.set_running(True)
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
        self.render_panel.set_running(False)
        if exit_code == 0:
            self.pipeline_panel.set_done_all()
            self.pipeline_panel.append_text("\nDone.\n")
        else:
            self.pipeline_panel.append_text(f"\nPipeline failed with exit code {exit_code}.\n")


def main():
    app = QApplication(sys.argv)
    here = Path(__file__).resolve().parent
    load_qss(app, here / "styles.qss")

    repo_root = "/Users/lucian/Projects/sonoPleth"
    win = MainWindow(repo_root=repo_root)
    win.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
