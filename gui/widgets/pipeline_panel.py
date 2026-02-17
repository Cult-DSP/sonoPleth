from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QTextCursor
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout, QHBoxLayout, QTextEdit, QPushButton

from .stepper import Stepper

class PipelinePanel(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(22, 18, 22, 18)
        lay.setSpacing(12)

        header = QHBoxLayout()
        header.setSpacing(12)

        title = QLabel("Pipeline", self)
        title.setObjectName("SectionTitle")
        header.addWidget(title, alignment=Qt.AlignLeft)

        header.addStretch(1)

        self.stepper = Stepper(steps=7, parent=self)
        header.addWidget(self.stepper, alignment=Qt.AlignCenter)

        header.addStretch(1)

        self.logs_btn = QPushButton("View Full Logs", self)
        self.logs_btn.setObjectName("SecondaryButton")
        header.addWidget(self.logs_btn, alignment=Qt.AlignRight)

        lay.addLayout(header)

        hint = QLabel("Pipeline init → Run pipeline to render spatial audio", self)
        hint.setObjectName("Muted")
        lay.addWidget(hint)

        self.console = QTextEdit(self)
        self.console.setObjectName("Console")
        self.console.setReadOnly(True)
        self.console.setMinimumHeight(240)
        lay.addWidget(self.console)

        self._auto_scroll = True
        self.console.verticalScrollBar().valueChanged.connect(self._on_scroll)

    def _on_scroll(self, value: int):
        bar = self.console.verticalScrollBar()
        self._auto_scroll = (value >= bar.maximum() - 3)

    def append_text(self, text: str):
        self.console.moveCursor(QTextCursor.End)
        self.console.insertPlainText(text)
        if self._auto_scroll:
            self.console.moveCursor(QTextCursor.End)

    def set_step(self, step_1based: int):
        self.stepper.set_step(step_1based)

    def set_done_all(self):
        self.stepper.set_done_all()

    def clear(self):
        self.console.clear()
        self.stepper.set_step(0)
