from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import QFrame, QLabel, QHBoxLayout, QWidget, QPushButton

class HeaderBar(QFrame):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("HeaderBar")

        lay = QHBoxLayout(self)
        lay.setContentsMargins(18, 12, 18, 12)
        lay.setSpacing(14)

        # Left
        left = QWidget(self)
        left_lay = QHBoxLayout(left)
        left_lay.setContentsMargins(0, 0, 0, 0)
        left_lay.setSpacing(10)

        self.title = QLabel("sonoPleth", self)
        self.title.setObjectName("Title")
        left_lay.addWidget(self.title, alignment=Qt.AlignVCenter)
        lay.addWidget(left, alignment=Qt.AlignLeft)

        # Center
        self.subtitle = QLabel("ADM → LUSID → Spatial Render", self)
        self.subtitle.setObjectName("Subtitle")
        lay.addWidget(self.subtitle, alignment=Qt.AlignCenter)

        # Right
        right = QWidget(self)
        right_lay = QHBoxLayout(right)
        right_lay.setContentsMargins(0, 0, 0, 0)
        right_lay.setSpacing(10)

        self.status_dot = QFrame(self)
        self.status_dot.setFixedSize(8, 8)
        self.status_dot.setStyleSheet("background: #4CAF82; border-radius: 4px;")
        right_lay.addWidget(self.status_dot)

        self.status_label = QLabel("Init Status: Ready", self)
        self.status_label.setObjectName("Muted")
        right_lay.addWidget(self.status_label)

        self.about_btn = QPushButton("About", self)
        self.about_btn.setFlat(True)
        self.about_btn.setStyleSheet("color: #6E6E73;")
        right_lay.addWidget(self.about_btn)

        lay.addWidget(right, alignment=Qt.AlignRight)
