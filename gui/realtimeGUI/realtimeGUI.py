"""
realtimeGUI.py — RealtimeWindow: top-level PySide6 window for the
sonoPleth Real-Time Spatial Audio Engine GUI.

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
from PySide6.QtGui import QClipboard, QGuiApplication
from PySide6.QtWidgets import (
    QFrame,
    QLabel,
    QMainWindow,
    QScrollArea,
    QVBoxLayout,
    QWidget,
)

from .realtime_runner import RealtimeConfig, RealtimeRunner, RealtimeRunnerState
from .realtime_panels.RealtimeInputPanel import RealtimeInputPanel
from .realtime_panels.RealtimeTransportPanel import RealtimeTransportPanel
from .realtime_panels.RealtimeControlsPanel import RealtimeControlsPanel
from .realtime_panels.RealtimeLogPanel import RealtimeLogPanel


class RealtimeWindow(QMainWindow):
    """
    Standalone main window for the real-time engine GUI.

    Parameters
    ----------
    repo_root : str
        Absolute path to the sonoPleth project root. Passed to RealtimeRunner
        so QProcess working directory resolves runRealtime.py correctly.
    """

    def __init__(
        self,
        repo_root: str = ".",
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._repo_root = repo_root
        self._runner = RealtimeRunner(repo_root=repo_root, parent=self)
        self._build_ui()
        self._connect_runner()
        self.setWindowTitle("sonoPleth — Real-Time Engine")
        self.resize(820, 900)

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
        header.setFixedHeight(56)
        header_layout = QVBoxLayout(header)
        header_layout.setContentsMargins(24, 10, 24, 10)
        title_lbl = QLabel("sonoPleth  Real-Time Engine")
        title_lbl.setObjectName("Title")
        subtitle_lbl = QLabel("Phase 10 — GUI Agent")
        subtitle_lbl.setObjectName("Subtitle")
        header_layout.addWidget(title_lbl)
        root_layout.addWidget(header)

        # ── Scrollable content area ───────────────────────────────────
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setHorizontalScrollBarPolicy(Qt.ScrollBarPolicy.ScrollBarAlwaysOff)

        content = QWidget()
        content_layout = QVBoxLayout(content)
        content_layout.setContentsMargins(20, 16, 20, 20)
        content_layout.setSpacing(12)

        # Panels
        self._input_panel     = RealtimeInputPanel(parent=content)
        self._transport_panel = RealtimeTransportPanel(parent=content)
        self._controls_panel  = RealtimeControlsPanel(parent=content)
        self._log_panel       = RealtimeLogPanel(parent=content)

        content_layout.addWidget(self._input_panel)
        content_layout.addWidget(self._transport_panel)
        content_layout.addWidget(self._controls_panel)
        content_layout.addWidget(self._log_panel)
        content_layout.addStretch()

        scroll.setWidget(content)
        root_layout.addWidget(scroll)

    # ── Runner signal wiring ─────────────────────────────────────────────

    def _connect_runner(self) -> None:
        r = self._runner

        r.output.connect(self._log_panel.append_line)
        r.state_changed.connect(self._on_state_changed)
        r.finished.connect(self._on_engine_finished)

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
            scan_audio     = self._input_panel.get_scan_audio(),
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
