from __future__ import annotations

import os
import sys
import re
from dataclasses import dataclass
from typing import Optional, List

from PySide6.QtCore import QObject, QProcess, Signal

STEP_RE = re.compile(r"\bSTEP\s*([1-9]\d*)\b", re.IGNORECASE)
PROGRESS_RE = re.compile(r"(\d{1,3})%")

@dataclass
class PipelineConfig:
    source_path: str
    speaker_layout: Optional[str] = None
    render_mode: Optional[str] = None
    resolution: Optional[float] = None
    master_gain: Optional[float] = None
    create_analysis: bool = True
    output_path: Optional[str] = None

class PipelineRunner(QObject):
    output = Signal(str)
    step_changed = Signal(int)   # 1-based
    finished = Signal(int)       # exit code
    started = Signal()
    progress_changed = Signal(int)  # 0-100

    def __init__(self, repo_root: str, parent=None):
        super().__init__(parent)
        self.repo_root = repo_root
        self.proc = QProcess(self)
        self.proc.setProcessChannelMode(QProcess.MergedChannels)
        self.proc.readyReadStandardOutput.connect(self._on_ready_read)
        self.proc.started.connect(self.started)
        self.proc.finished.connect(self._on_finished)
        self._last_step = 0
        self._last_progress = 0

    def is_running(self) -> bool:
        return self.proc.state() != QProcess.NotRunning

    def stop(self):
        if self.is_running():
            self.proc.terminate()

    def run(self, cfg: PipelineConfig):
        if self.is_running():
            return

        py = sys.executable or "python"
        script = os.path.join(self.repo_root, "runPipeline.py")
        # Use unbuffered output for streaming logs
        args: List[str] = ["-u", script, cfg.source_path]

        # Signature:
        # runPipeline.py <sourceADMFile> [speakerLayout] [renderMode] [resolution] [master_gain] [createAnalysis] [outputRenderPath]
        if cfg.speaker_layout is not None:
            args.append(cfg.speaker_layout)
        if cfg.render_mode is not None:
            args.append(cfg.render_mode)
        if cfg.resolution is not None:
            args.append(str(cfg.resolution))
        if cfg.master_gain is not None:
            args.append(str(cfg.master_gain))
        args.append("1" if cfg.create_analysis else "0")
        if cfg.output_path:
            args.append(cfg.output_path)

        self._last_step = 0
        self._last_progress = 0
        self.proc.setWorkingDirectory(self.repo_root)
        self.proc.start(py, args)

    def _on_ready_read(self):
        data = bytes(self.proc.readAllStandardOutput()).decode("utf-8", errors="replace")
        if not data:
            return
        self.output.emit(data)

        for m in STEP_RE.finditer(data):
            step = int(m.group(1))
            if step > self._last_step:
                self._last_step = step
                self.step_changed.emit(step)

        for m in PROGRESS_RE.finditer(data):
            progress = int(m.group(1))
            if progress > self._last_progress:
                self._last_progress = progress
                self.progress_changed.emit(progress)

    def _on_finished(self, exit_code: int, _status):
        self.finished.emit(exit_code)
