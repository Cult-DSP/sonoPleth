"""Raw log modal for sonoPleth GUI."""
from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QDialog, QVBoxLayout, QPlainTextEdit, QPushButton


class LogModal(QDialog):
    """Full raw log viewer opened by 'View Full Logs'."""

    def __init__(self, raw_text: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Full Pipeline Logs")
        self.resize(750, 520)
        self.setMinimumSize(500, 350)

        lay = QVBoxLayout(self)
        lay.setContentsMargins(16, 16, 16, 16)
        lay.setSpacing(12)

        self.text = QPlainTextEdit(self)
        self.text.setReadOnly(True)
        self.text.setPlainText(raw_text)
        self.text.setStyleSheet(
            "font-family: 'SF Mono', 'Menlo', 'Consolas', monospace; "
            "font-size: 11px; "
            "background: #FAFAF8; "
            "border: 1px solid rgba(0,0,0,0.06); "
            "border-radius: 8px; "
            "padding: 10px;"
        )
        lay.addWidget(self.text)

        close_btn = QPushButton("Close", self)
        close_btn.setObjectName("SecondaryButton")
        close_btn.clicked.connect(self.close)
        lay.addWidget(close_btn, alignment=Qt.AlignRight)
