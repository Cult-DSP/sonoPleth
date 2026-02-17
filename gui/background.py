from __future__ import annotations

from PySide6.QtCore import Qt, QRectF
from PySide6.QtGui import QPainter, QPen
from PySide6.QtWidgets import QWidget


class RadialBackground(QWidget):
    # Non-interactive, subtle sacred-geometry background.
    # Concentric circles + crosshair + faint diagonals.

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAttribute(Qt.WA_TransparentForMouseEvents, True)
        self.setAutoFillBackground(False)

    def paintEvent(self, event):
        w = self.width()
        h = self.height()
        cx = w / 2.0
        cy = h / 2.0

        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)

        pen = QPen()
        pen.setWidthF(1.0)
        pen.setColor(Qt.black)
        p.setPen(pen)

        # Concentric circles
        p.setOpacity(0.045)
        max_r = max(w, h) * 0.65
        step = 60.0
        r = step
        while r < max_r:
            rect = QRectF(cx - r, cy - r, 2 * r, 2 * r)
            p.drawEllipse(rect)
            r += step

        # Crosshair
        p.setOpacity(0.035)
        p.drawLine(0, int(cy), w, int(cy))
        p.drawLine(int(cx), 0, int(cx), h)

        # Diagonals
        p.setOpacity(0.025)
        p.drawLine(0, 0, w, h)
        p.drawLine(0, h, w, 0)

        p.end()
