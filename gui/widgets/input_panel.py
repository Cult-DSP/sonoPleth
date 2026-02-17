from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout, QPushButton, QWidget, QHBoxLayout, QFileDialog, QLineEdit

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

    def set_state(self, state: str):
        if state == 'inactive':
            self.icon.setStyleSheet("background: rgba(0,0,0,0.08); border-radius: 9px;")
            self.dot.setStyleSheet("background: transparent; border-radius: 4px;")
        elif state == 'ready':
            self.icon.setStyleSheet(
                "background: rgba(76,111,255,0.20); border-radius: 9px; border: 1px solid rgba(76,111,255,0.35);"
            )
            self.dot.setStyleSheet("background: transparent; border-radius: 4px;")
        elif state == 'active':
            self.icon.setStyleSheet("background: rgba(76,111,255,0.55); border-radius: 9px;")
            self.dot.setStyleSheet("background: #4CAF82; border-radius: 4px;")
        else:
            self.set_state('inactive')

class InputPanel(QFrame):
    file_selected = Signal(str)
    output_path_changed = Signal(str)


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

        # Output path field with file dialog
        output_row = QHBoxLayout()
        output_label = QLabel("Output Path:", self)
        output_label.setObjectName("Muted")
        output_row.addWidget(output_label)
        self.output_path_btn = QPushButton("Select Output File", self)
        self.output_path_btn.setObjectName("SecondaryButton")
        self.output_path_btn.clicked.connect(self._choose_output_path)
        output_row.addWidget(self.output_path_btn)
        self.output_path = "processedData/spatial_render.wav"
        self.output_path_btn.setToolTip(self.output_path)
        lay.addLayout(output_row)

        self.row_adm = StatusRow("ADM Source", self)
        lay.addWidget(self.row_adm)
        self.row_meta = StatusRow("Metadata Extracted", self)
        self.row_activity = StatusRow("Channel Activity Ready", self)

        lay.addWidget(self.row_meta)
        lay.addWidget(self.row_activity)
        lay.addStretch(1)

        self.row_adm.set_state('inactive')
        self.row_meta.set_state('inactive')
        self.row_activity.set_state('inactive')

    def _on_output_path_changed(self, text):
        self.output_path_changed.emit(text)

    def _choose_output_path(self):
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Select Output File",
            self.output_path,
            "WAV Files (*.wav);;All Files (*)"
        )
        if path:
            self.output_path = path
            self.output_path_btn.setToolTip(path)
            self.output_path_changed.emit(path)

    def _choose_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Select ADM WAV", "", "Audio/ADM (*.wav *.WAV);;All Files (*)"
        )
        if path:
            self.file_selected.emit(path)
            self.row_adm.set_state('ready')

    def set_progress_flags(self, metadata: bool, activity: bool):
        if metadata:
            self.row_meta.set_state('active')
        if activity:
            self.row_activity.set_state('active')

    def get_output_path(self) -> str:
        return self.output_path or "processedData/spatial_render.wav"
