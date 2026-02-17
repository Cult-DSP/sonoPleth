from __future__ import annotations

from PySide6.QtCore import Qt, QSize, QPointF
from PySide6.QtGui import QColor, QPainter, QPen, QFont
from PySide6.QtWidgets import QWidget, QHBoxLayout, QLabel


class StepMarker(QWidget):
    """Single step marker — circle or diamond shape."""

    def __init__(self, shape: str = "circle", size: int = 10, parent=None):
        super().__init__(parent)
        self._shape = shape  # "circle" or "diamond"
        self._size = size
        self.state = "inactive"  # inactive | active | done
        self.setFixedSize(QSize(size + 4, size + 4))  # small padding for antialiasing

    def set_state(self, state: str):
        self.state = state
        self.update()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)

        cx = self.width() / 2.0
        cy = self.height() / 2.0
        s = self._size / 2.0

        # Colors by state
        if self.state == "active":
            fill = QColor(76, 111, 255, 180)
            stroke = QColor(76, 111, 255, 200)
        elif self.state == "done":
            fill = QColor(28, 28, 30, 90)
            stroke = QColor(28, 28, 30, 100)
        else:
            fill = QColor(0, 0, 0, 22)
            stroke = QColor(0, 0, 0, 30)

        pen = QPen(stroke, 1.0)
        p.setPen(pen)
        p.setBrush(fill)

        if self._shape == "diamond":
            pts = [
                QPointF(cx, cy - s),
                QPointF(cx + s, cy),
                QPointF(cx, cy + s),
                QPointF(cx - s, cy),
            ]
            p.drawPolygon(pts)
        else:
            p.drawEllipse(QPointF(cx, cy), s, s)

        p.end()


class ConnectorLine(QWidget):
    """Thin line connecting step markers."""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setFixedSize(16, 2)

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(0, 0, 0, 18))
        p.drawRoundedRect(0, 0, self.width(), self.height(), 1, 1)
        p.end()


class Stepper(QWidget):
    """Pipeline stepper with alternating circle/diamond markers and an end label."""

    SHAPES = ["circle", "diamond", "circle", "diamond", "circle", "diamond", "circle"]

    def __init__(self, steps: int = 7, parent=None):
        super().__init__(parent)
        lay = QHBoxLayout(self)
        lay.setContentsMargins(0, 0, 0, 0)
        lay.setSpacing(0)

        self.markers: list[StepMarker] = []
        for i in range(steps):
            shape = self.SHAPES[i] if i < len(self.SHAPES) else "circle"
            marker = StepMarker(shape=shape, size=10, parent=self)
            self.markers.append(marker)
            lay.addWidget(marker, alignment=Qt.AlignVCenter)
            if i < steps - 1:
                lay.addWidget(ConnectorLine(self), alignment=Qt.AlignVCenter)

        # End label
        end_label = QLabel("Analyze", self)
        end_label.setStyleSheet("color: rgba(110,110,115,0.7); font-size: 10px; margin-left: 6px;")
        lay.addWidget(end_label, alignment=Qt.AlignVCenter)

        self.set_step(0)

    def set_step(self, step_1based: int):
        for i, marker in enumerate(self.markers, start=1):
            if step_1based == 0:
                marker.set_state("inactive")
            elif i < step_1based:
                marker.set_state("done")
            elif i == step_1based:
                marker.set_state("active")
            else:
                marker.set_state("inactive")

    def set_done_all(self):
        for marker in self.markers:
            marker.set_state("done")
