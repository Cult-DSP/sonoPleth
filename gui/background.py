from __future__ import annotations

from PySide6.QtCore import Qt, QRectF, QPointF
from PySide6.QtGui import QPainter, QPen, QRadialGradient, QColor
from PySide6.QtWidgets import QWidget


class RadialBackground(QWidget):
    """Non-interactive background: subtle concentric geometry with a centered lens focal point."""

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

        # --- Central lens: soft radial gradient disk ---
        lens_r = min(w, h) * 0.30
        grad = QRadialGradient(QPointF(cx, cy), lens_r)
        grad.setColorAt(0.0, QColor(255, 255, 255, 35))
        grad.setColorAt(0.5, QColor(240, 240, 238, 18))
        grad.setColorAt(1.0, QColor(244, 244, 242, 0))
        p.setPen(Qt.NoPen)
        p.setBrush(grad)
        p.drawEllipse(QPointF(cx, cy), lens_r, lens_r)

        # Faint highlight ring around lens
        pen = QPen(QColor(255, 255, 255, 22), 1.2)
        p.setPen(pen)
        p.setBrush(Qt.NoBrush)
        p.drawEllipse(QPointF(cx, cy), lens_r * 0.92, lens_r * 0.92)

        # Tiny center dot
        p.setPen(Qt.NoPen)
        p.setBrush(QColor(0, 0, 0, 18))
        p.drawEllipse(QPointF(cx, cy), 3, 3)

        # --- Concentric circles (fewer, fading toward edges) ---
        pen = QPen()
        pen.setWidthF(0.8)
        max_r = max(w, h) * 0.3
        step = 110.0
        r = step
        while r < max_r:
            fade = max(0.0, 1.0 - (r / max_r))
            opacity = 0.2 * fade
            pen.setColor(QColor(0, 0, 0, int(255 * opacity)))
            p.setPen(pen)
            p.setBrush(Qt.NoBrush)
            p.drawEllipse(QRectF(cx - r, cy - r, 2 * r, 2 * r))
            r += step

        # --- Crosshair ---
        p.setOpacity(0.2)
        pen.setColor(QColor(0, 0, 0))
        pen.setWidthF(0.6)
        p.setPen(pen)
        p.drawLine(0, int(cy), w, int(cy))
        p.drawLine(int(cx), 0, int(cx), h)

        # --- Faint diagonals ---
        p.setOpacity(0.03)
        p.drawLine(0, 0, w, h)
        p.drawLine(0, h, w, 0)

        p.setOpacity(1.0)
        p.end()
