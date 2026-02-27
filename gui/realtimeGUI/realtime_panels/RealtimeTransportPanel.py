"""
RealtimeTransportPanel.py — Start / Stop / Kill / Restart / Pause / Play
transport controls plus a status pill.

Phase 10 — GUI Agent.
"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import Signal, Qt
from PySide6.QtGui import QClipboard, QGuiApplication
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..realtime_runner import RealtimeRunnerState


def _card() -> QFrame:
    f = QFrame()
    f.setObjectName("Card")
    return f


# Pill background colours — inline style (QSS objectName approach for colour
# would need dynamic property tricks; simpler to just set stylesheet directly).
_PILL_COLORS: dict[str, str] = {
    RealtimeRunnerState.IDLE.value:      "rgba(110,110,115,0.15)",
    RealtimeRunnerState.LAUNCHING.value: "rgba(255,204,0,0.20)",
    RealtimeRunnerState.RUNNING.value:   "rgba(52,199,89,0.20)",
    RealtimeRunnerState.PAUSED.value:    "rgba(255,159,10,0.22)",
    RealtimeRunnerState.EXITED.value:    "rgba(110,110,115,0.15)",
    RealtimeRunnerState.ERROR.value:     "rgba(255,59,48,0.20)",
}
_PILL_DOT: dict[str, str] = {
    RealtimeRunnerState.IDLE.value:      "#8E8E93",
    RealtimeRunnerState.LAUNCHING.value: "#FFCC00",
    RealtimeRunnerState.RUNNING.value:   "#34C759",
    RealtimeRunnerState.PAUSED.value:    "#FF9F0A",
    RealtimeRunnerState.EXITED.value:    "#8E8E93",
    RealtimeRunnerState.ERROR.value:     "#FF3B30",
}


class RealtimeTransportPanel(QWidget):
    """
    Transport control bar.

    Signals
    -------
    start_requested()
    stop_requested()
    kill_requested()
    restart_requested()
    pause_requested()
    play_requested()
    copy_command_requested()
    """

    start_requested        = Signal()
    stop_requested         = Signal()
    kill_requested         = Signal()
    restart_requested      = Signal()
    pause_requested        = Signal()
    play_requested         = Signal()
    copy_command_requested = Signal()

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._current_state = RealtimeRunnerState.IDLE
        self._build_ui()

    # ── Public API ───────────────────────────────────────────────────────

    def update_state(self, state_name: str) -> None:
        """Called by the window whenever RealtimeRunner.state_changed fires."""
        try:
            self._current_state = RealtimeRunnerState(state_name)
        except ValueError:
            self._current_state = RealtimeRunnerState.ERROR
        self._refresh()

    def set_exit_code(self, code: int) -> None:
        """Append exit code to the status pill after engine finishes."""
        state = self._current_state
        label = state.value
        if state in (RealtimeRunnerState.EXITED, RealtimeRunnerState.ERROR):
            label = f"{state.value} ({code})"
        self._pill_label.setText(f"● {label}")

    # ── UI construction ──────────────────────────────────────────────────

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(8)

        card = _card()
        layout = QVBoxLayout(card)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(10)

        title = QLabel("Transport")
        title.setObjectName("SectionTitle")
        layout.addWidget(title)

        # Row 1: main transport buttons + status pill
        row1 = QHBoxLayout()
        row1.setSpacing(8)

        self._start_btn   = self._btn("Start",   "PrimaryButton")
        self._stop_btn    = self._btn("Stop",     "SecondaryButton")
        self._kill_btn    = self._btn("Kill",     "SecondaryButton")
        self._restart_btn = self._btn("Restart",  "SecondaryButton")

        for b in (self._start_btn, self._stop_btn,
                  self._kill_btn, self._restart_btn):
            row1.addWidget(b)

        row1.addStretch()

        # Status pill
        pill_frame = QFrame()
        pill_frame.setObjectName("Pill")
        pill_layout = QHBoxLayout(pill_frame)
        pill_layout.setContentsMargins(10, 4, 10, 4)
        self._pill_label = QLabel("● Idle")
        self._pill_label.setObjectName("Muted")
        pill_layout.addWidget(self._pill_label)
        row1.addWidget(pill_frame)

        layout.addLayout(row1)

        # Row 2: pause / play + osc port label + copy command
        row2 = QHBoxLayout()
        row2.setSpacing(8)

        self._pause_btn = self._btn("Pause", "SecondaryButton")
        self._play_btn  = self._btn("Play",  "SecondaryButton")

        row2.addWidget(self._pause_btn)
        row2.addWidget(self._play_btn)
        row2.addStretch()

        self._osc_label = QLabel("OSC port: 9009")
        self._osc_label.setObjectName("Muted")
        row2.addWidget(self._osc_label)

        row2.addSpacing(12)
        self._copy_btn = self._btn("Copy Command", "SecondaryButton")
        row2.addWidget(self._copy_btn)

        layout.addLayout(row2)
        root.addWidget(card)

        # Wire clicks
        self._start_btn.clicked.connect(self.start_requested)
        self._stop_btn.clicked.connect(self.stop_requested)
        self._kill_btn.clicked.connect(self.kill_requested)
        self._restart_btn.clicked.connect(self.restart_requested)
        self._pause_btn.clicked.connect(self.pause_requested)
        self._play_btn.clicked.connect(self.play_requested)
        self._copy_btn.clicked.connect(self.copy_command_requested)

        self._refresh()

    @staticmethod
    def _btn(label: str, obj_name: str) -> QPushButton:
        b = QPushButton(label)
        b.setObjectName(obj_name)
        return b

    # ── State-driven enable / disable ───────────────────────────────────

    def set_osc_port(self, port: int) -> None:
        self._osc_label.setText(f"OSC port: {port}")

    def _refresh(self) -> None:
        s = self._current_state
        idle_or_done = s in (RealtimeRunnerState.IDLE,
                              RealtimeRunnerState.EXITED,
                              RealtimeRunnerState.ERROR)
        active = s in (RealtimeRunnerState.LAUNCHING,
                       RealtimeRunnerState.RUNNING,
                       RealtimeRunnerState.PAUSED)
        running_or_paused = s in (RealtimeRunnerState.RUNNING,
                                   RealtimeRunnerState.PAUSED)

        self._start_btn.setEnabled(idle_or_done)
        self._stop_btn.setEnabled(active)
        self._kill_btn.setEnabled(active)
        self._restart_btn.setEnabled(active)
        self._pause_btn.setEnabled(s == RealtimeRunnerState.RUNNING)
        self._play_btn.setEnabled(s == RealtimeRunnerState.PAUSED)
        self._copy_btn.setEnabled(True)  # always available

        # Pill text + colour
        label = s.value
        self._pill_label.setText(f"● {label}")
        bg   = _PILL_COLORS.get(s.value, "rgba(110,110,115,0.15)")
        dot  = _PILL_DOT.get(s.value, "#8E8E93")
        self._pill_label.setStyleSheet(f"color: {dot};")
        self._pill_label.parentWidget().setStyleSheet(
            f"QFrame#Pill {{ background: {bg}; border: 1px solid rgba(0,0,0,0.06); "
            f"border-radius: 10px; }}"
        )
