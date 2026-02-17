from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout, QPushButton, QWidget, QHBoxLayout, QFileDialog

class StatusRow(QWidget):
    def __init__(self, text: str, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(10)

        self.icon = QFrame(self)
        self.icon.setFixedSize(18, 18)
        self.icon.setStyleSheet("background: rgba(0,0,0,0.08); border-radius: 9px;")
        lay.addWidget(self.icon, alignment=Qt.AlignVCenter)

        self.label = QLabel(text, self)
        self.label.setObjectName("Muted")
        lay.addWidget(self.label, alignment=Qt.AlignVCenter)

        lay.addStretch(1)

        self.dot = QFrame(self)
        self.dot.setFixedSize(8, 8)
        self.dot.setStyleSheet("background: transparent; border-radius: 4px;")
        lay.addWidget(self.dot, alignment=Qt.AlignVCenter)

    def set_checked(self, checked: bool, active_dot: bool = False):
        if checked:
            self.icon.setStyleSheet(
                "background: rgba(76,111,255,0.20); border-radius: 9px; border: 1px solid rgba(76,111,255,0.35);"
            )
        else:
            self.icon.setStyleSheet("background: rgba(0,0,0,0.08); border-radius: 9px;")

        if active_dot:
            self.dot.setStyleSheet("background: #4CAF82; border-radius: 4px;")
        else:
            self.dot.setStyleSheet("background: transparent; border-radius: 4px;")

class InputPanel(QFrame):
    file_selected = Signal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(22, 22, 22, 22)
        lay.setSpacing(20)

        title = QLabel("Input", self)
        title.setObjectName("SectionTitle")
        lay.addWidget(title)

        self.select_btn = QPushButton("SELECT ADM OR LUSID", self)
        self.select_btn.setObjectName("SecondaryButton")
        self.select_btn.clicked.connect(self._choose_file)
        self.select_btn.setMinimumHeight(44)
        lay.addWidget(self.select_btn)

        self.row_adm = StatusRow("ADM Source", self)
        lay.addWidget(self.row_adm)
        self.row_meta = StatusRow("Metadata Extracted", self)
        self.row_activity = StatusRow("Channel Activity Ready", self)

        lay.addWidget(self.row_meta)
        lay.addWidget(self.row_activity)
        lay.addStretch(1)

        self.row_adm.set_checked(False)
        self.row_meta.set_checked(False)
        self.row_activity.set_checked(False)

    def _choose_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select ADM WAV", "", "Audio/ADM (*.wav *.WAV);;All Files (*)"
        )
        if path:
            self.file_selected.emit(path)
            self.row_adm.set_checked(True, active_dot=True)

    def set_progress_flags(self, metadata: bool, activity: bool):
        self.row_meta.set_checked(metadata, active_dot=metadata)
        self.row_activity.set_checked(activity, active_dot=activity)
