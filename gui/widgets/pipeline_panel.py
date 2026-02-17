from __future__ import annotations

import re
from datetime import datetime

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QTextCursor, QColor
from PySide6.QtWidgets import (
    QFrame, QLabel, QVBoxLayout, QHBoxLayout, QPushButton,
    QProgressBar, QListWidget, QListWidgetItem,
)

from .stepper import Stepper
from .log_modal import LogModal

# Noisy lines to throttle — only update a rolling entry
_THROTTLE_RE = re.compile(r"(Channel\s+\d+/\d+|Block\s+\d+\s+\(\d+%\))", re.IGNORECASE)


class PipelinePanel(QFrame):
    run_clicked = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        self._raw_log = ""  # full raw text for modal

        lay = QVBoxLayout(self)
        lay.setContentsMargins(24, 22, 24, 22)
        lay.setSpacing(10)

        # --- Header row: title | stepper | RUN RENDER ---
        header = QHBoxLayout()
        header.setSpacing(12)

        title = QLabel("Pipeline", self)
        title.setObjectName("SectionTitle")
        header.addWidget(title, alignment=Qt.AlignLeft)

        header.addStretch(1)

        self.stepper = Stepper(steps=7, parent=self)
        header.addWidget(self.stepper, alignment=Qt.AlignVCenter)

        header.addStretch(1)

        self.run_btn = QPushButton("RUN RENDER", self)
        self.run_btn.setObjectName("PrimaryButton")
        self.run_btn.setMinimumHeight(46)
        self.run_btn.setMinimumWidth(160)
        self.run_btn.clicked.connect(self.run_clicked)
        header.addWidget(self.run_btn, alignment=Qt.AlignRight)

        lay.addLayout(header)

        # --- Progress bar ---
        self.progress_bar = QProgressBar(self)
        self.progress_bar.setRange(0, 100)
        self.progress_bar.setValue(0)
        self.progress_bar.setFixedHeight(4)
        self.progress_bar.setTextVisible(False)
        lay.addWidget(self.progress_bar)

        # --- Hint label ---
        self.hint = QLabel("Pipeline init → Run pipeline to render spatial audio", self)
        self.hint.setObjectName("Muted")
        lay.addWidget(self.hint)

        # --- Structured log list ---
        self.log_list = QListWidget(self)
        self.log_list.setObjectName("LogList")
        self.log_list.setMinimumHeight(180)
        self.log_list.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        self.log_list.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        lay.addWidget(self.log_list)

        # --- Footer row: View Full Logs ---
        footer = QHBoxLayout()
        footer.addStretch(1)
        self.logs_btn = QPushButton("View Full Logs", self)
        self.logs_btn.setObjectName("SecondaryButton")
        self.logs_btn.clicked.connect(self._show_raw_logs)
        footer.addWidget(self.logs_btn, alignment=Qt.AlignRight)
        lay.addLayout(footer)

        self._rolling_key = None  # key for throttled rolling entry

    # --- Public API ---

    def append_text(self, text: str):
        """Receive raw output, store it, and add filtered lines to the list."""
        self._raw_log += text

        for line in text.splitlines():
            stripped = line.strip()
            if not stripped:
                continue

            # Throttle noisy lines — update a single rolling entry
            m = _THROTTLE_RE.search(stripped)
            if m:
                key = m.group(0)[:8]  # e.g. "Channel " or "Block 12"
                self._update_rolling(stripped)
                continue

            self._add_log_line(stripped)

    def set_step(self, step_1based: int):
        self.stepper.set_step(step_1based)

    def set_progress(self, percent: int):
        self.progress_bar.setValue(percent)

    def set_done_all(self):
        self.stepper.set_done_all()
        self.progress_bar.setValue(100)

    def set_running(self, running: bool):
        self.run_btn.setEnabled(not running)
        self.run_btn.setText("RUNNING…" if running else "RUN RENDER")

    def clear(self):
        self.log_list.clear()
        self.stepper.set_step(0)
        self.progress_bar.setValue(0)
        self._raw_log = ""
        self._rolling_key = None

    # --- Internals ---

    def _add_log_line(self, message: str):
        ts = datetime.now().strftime("%H:%M")
        item = QListWidgetItem(f"  {ts}   {message}")
        # Style STEP lines slightly bolder
        if message.upper().startswith("STEP"):
            font = item.font()
            font.setBold(True)
            item.setFont(font)
        self.log_list.addItem(item)
        self.log_list.scrollToBottom()
        self._rolling_key = None  # reset throttle after a normal line

    def _update_rolling(self, message: str):
        """Replace the last item if it was also a throttled line."""
        ts = datetime.now().strftime("%H:%M")
        text = f"  {ts}   {message}"
        if self._rolling_key and self.log_list.count() > 0:
            last = self.log_list.item(self.log_list.count() - 1)
            if last:
                last.setText(text)
                self.log_list.scrollToBottom()
                return
        item = QListWidgetItem(text)
        item.setForeground(QColor(110, 110, 115))
        self.log_list.addItem(item)
        self.log_list.scrollToBottom()
        self._rolling_key = "throttled"

    def _show_raw_logs(self):
        modal = LogModal(self._raw_log, parent=self.window())
        modal.exec()
