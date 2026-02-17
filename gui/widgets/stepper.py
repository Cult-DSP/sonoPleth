from __future__ import annotations

from PySide6.QtCore import Qt, QSize
from PySide6.QtGui import QColor, QPainter
from PySide6.QtWidgets import QWidget, QHBoxLayout

class StepDot(QWidget):
    def __init__(self, diameter=10, parent=None):
        super().__init__(parent)
        self.d = diameter
        self.state = "inactive"  # inactive | active | done
        self.setFixedSize(QSize(diameter, diameter))

    def set_state(self, state: str):
        self.state = state
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)

        if self.state == "inactive":
            c = QColor(0, 0, 0, int(255 * 0.14))
        elif self.state == "active":
            c = QColor(76, 111, 255, int(255 * 0.65))
        else:
            c = QColor(76, 175, 130, int(255 * 0.55))

        p.setPen(Qt.NoPen)
        p.setBrush(c)
        p.drawEllipse(0, 0, self.d, self.d)
        p.end()

class Stepper(QWidget):
    def __init__(self, steps=7, parent=None):
        super().__init__(parent)
        self.dots = [StepDot(10, self) for _ in range(steps)]
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(12)
        for d in self.dots:
            lay.addWidget(d, alignment=Qt.AlignCenter)
        self.set_step(0)

    def set_step(self, step_1based: int):
        for i, dot in enumerate(self.dots, start=1):
            if step_1based == 0:
                dot.set_state("inactive")
            elif i < step_1based:
                dot.set_state("done")
            elif i == step_1based:
                dot.set_state("active")
            else:
                dot.set_state("inactive")

    def set_done_all(self):
        for dot in self.dots:
            dot.set_state("done")
