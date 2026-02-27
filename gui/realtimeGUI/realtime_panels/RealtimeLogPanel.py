"""
RealtimeLogPanel.py — Live stdout/stderr console for the real-time engine.

Features:
  - Scrolling QPlainTextEdit (monospace, read-only).
  - Capped at MAX_LINES (2000) — oldest lines dropped automatically.
  - Clear button.
  - Auto-scroll to bottom unless the user has scrolled up.

Phase 10 — GUI Agent.
"""

from __future__ import annotations

from typing import Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtWidgets import (
    QFrame,
    QHBoxLayout,
    QLabel,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)


MAX_LINES = 2000


def _card() -> QFrame:
    f = QFrame()
    f.setObjectName("Card")
    return f


class RealtimeLogPanel(QWidget):
    """Scrolling log console. Call append_line(text) to add output."""

    def __init__(self, theme: dict = None, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        from .theme import DARK
        self._theme = theme or DARK
        self._build_ui()

    # ── Public API ───────────────────────────────────────────────────────

    def append_line(self, text: str) -> None:
        """Append one line. Trims oldest lines when over MAX_LINES."""
        sb = self._log.verticalScrollBar()
        at_bottom = sb.value() >= sb.maximum() - 4

        # Colour-code by prefix (mirrors HTML log-line classes)
        colour = self._theme["muted"]        # default: .info
        if text.startswith("[GUI]"):
            colour = self._theme["muted2"]   # .gui
        elif "[stderr] Warning" in text or "[stderr] warning" in text:
            colour = self._theme["yellow"]   # .warn
        elif ("ParameterServer listening" in text
              or "DBAP renderer running"  in text
              or "Engine exited cleanly"  in text):
            colour = self._theme["green"]    # .ok
        elif "ERROR" in text or "error" in text.lower():
            colour = self._theme["red"]      # .error

        # Use HTML insertion for colour (QPlainTextEdit supports appendHtml)
        escaped = (text.replace("&", "&amp;")
                       .replace("<", "&lt;")
                       .replace(">", "&gt;"))
        self._log.appendHtml(
            f'<span style="color:{colour}; font-family:\'Space Mono\',monospace;">'
            f'{escaped}</span>'
        )

        # Trim excess (existing logic unchanged)
        doc = self._log.document()
        while doc.blockCount() > MAX_LINES:
            cursor = QTextCursor(doc.begin())
            cursor.select(QTextCursor.SelectionType.BlockUnderCursor)
            cursor.movePosition(
                QTextCursor.MoveOperation.NextCharacter,
                QTextCursor.MoveMode.KeepAnchor,
            )
            cursor.removeSelectedText()

        if at_bottom:
            self._log.moveCursor(QTextCursor.MoveOperation.End)

    def clear(self) -> None:
        self._log.clear()

    # ── UI construction ──────────────────────────────────────────────────

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(8)

        card = _card()
        layout = QVBoxLayout(card)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(8)

        header = QHBoxLayout()
        title = QLabel("ENGINE LOG")
        title.setObjectName("SectionTitle")
        title.setFont(QFont("Space Mono", 7))
        header.addWidget(title)
        # Phase tag
        phase_tag = QLabel("PHASE 10")
        phase_tag.setObjectName("PhaseTag")
        phase_tag.setFont(QFont("Space Mono", 6))
        header.addWidget(phase_tag)
        header.addSpacing(8)
        header.addStretch()
        clear_btn = QPushButton("Clear")
        clear_btn.setObjectName("ClearButton")
        clear_btn.setFont(QFont("Space Mono", 7))
        clear_btn.setFixedWidth(60)
        clear_btn.clicked.connect(self.clear)
        header.addWidget(clear_btn)
        layout.addLayout(header)

        self._log = QPlainTextEdit()
        self._log.setReadOnly(True)
        self._log.setMaximumBlockCount(MAX_LINES)
        mono = QFont("Space Mono")
        mono.setPointSize(9)
        mono.setStyleHint(QFont.StyleHint.Monospace)
        self._log.setFont(mono)
        self._log.setStyleSheet(
            "QPlainTextEdit { background: rgba(0,0,0,0.03); "
            "border: none; border-radius: 8px; padding: 8px; }"
        )
        self._log.setMinimumHeight(180)
        layout.addWidget(self._log)

        root.addWidget(card)
