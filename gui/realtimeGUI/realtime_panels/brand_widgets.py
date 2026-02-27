"""
brand_widgets.py — Purely decorative widgets for sonoPleth brand identity.

All widgets here are visual-only; they emit no signals and hold no state.
Reference: the corner-marks, eye-ornament, sacred-geometry background,
and section-label divider line from the HTML mockups.
"""
from __future__ import annotations
from typing import Optional
from PySide6.QtCore import Qt, QRectF, QPointF, QSize
from PySide6.QtGui import QPainter, QPen, QColor, QFont, QPainterPath
from PySide6.QtWidgets import QWidget, QSizePolicy


class CornerMarksWidget(QWidget):
    """
    Draws four L-shaped tick marks in the corners of its parent widget.
    Maps to: .corner-mark.tl/tr/bl/br in the HTML (on .card elements).

    Usage: instantiate with the card QFrame as parent; it will overlay it.
    Set geometry to match the parent's rect (use resizeEvent or fixed inset).

    Recommended: call place_on(card_frame) which sets geometry automatically.
    """

    MARK_LEN   = 10   # px arm length  (HTML: 12px)
    MARK_INSET = 8    # px from corner (HTML: top/left: 8px)

    def __init__(
        self,
        color: str = "#4a4a46",   # DARK: muted2 | LIGHT: use theme["muted2"]
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._color = QColor(color)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)

    def set_color(self, color: str) -> None:
        self._color = QColor(color)
        self.update()

    def place_on(self, parent: QWidget) -> None:
        """Resize self to fill parent. Call after parent is laid out."""
        self.setParent(parent)
        self.setGeometry(parent.rect())
        self.raise_()
        self.show()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self._color, 1.0)
        p.setPen(pen)
        w, h = self.width(), self.height()
        i, L = self.MARK_INSET, self.MARK_LEN

        # top-left
        p.drawLine(i, i + L, i, i)
        p.drawLine(i, i, i + L, i)
        # top-right
        p.drawLine(w - i - L, i, w - i, i)
        p.drawLine(w - i, i, w - i, i + L)
        # bottom-left
        p.drawLine(i, h - i - L, i, h - i)
        p.drawLine(i, h - i, i + L, h - i)
        # bottom-right
        p.drawLine(w - i - L, h - i, w - i, h - i)
        p.drawLine(w - i, h - i, w - i, h - i - L)


class EyeOrnamentWidget(QWidget):
    """
    Draws the Cult DSP eye + root-tendril glyph as a faint watermark.
    Maps to: .eye-ornament SVG inside the Transport card.

    Place in the bottom-right of the transport card frame:
        eye = EyeOrnamentWidget(parent=transport_card)
        eye.setFixedSize(130, 90)
        # position via layout or manual move:
        eye.move(card.width() - 140, card.height() - 80)

    The widget is mouse-transparent and does not affect layout.
    """

    def __init__(
        self,
        stroke_color: str = "#ffffff",
        opacity: float = 0.06,        # DARK: 0.06 | LIGHT: 0.04
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFixedSize(130, 90)

    def set_theme(self, stroke_color: str, opacity: float) -> None:
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.update()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setOpacity(self._opacity)
        pen = QPen(self._stroke)
        p.setPen(pen)

        cx, ey = 65.0, 28.0   # eye centre

        # Eye ellipse (outer)
        pen.setWidthF(1.4)
        p.setPen(pen)
        p.drawEllipse(QRectF(cx - 50, ey - 22, 100, 44))

        # Iris circle
        p.drawEllipse(QRectF(cx - 12, ey - 12, 24, 24))

        # Pupil (filled)
        p.setBrush(self._stroke)
        p.drawEllipse(QRectF(cx - 5, ey - 5, 10, 10))
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Accent dots beside eye
        p.setBrush(self._stroke)
        for dx in (-18.0, +18.0):
            p.drawEllipse(QRectF(cx + dx - 1.5, ey - 3 - 1.5, 3, 3))
        for dx in (-10.0, +10.0):
            p.drawEllipse(QRectF(cx + dx - 1, ey - 8 - 1, 2, 2))
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Root / tendril lines downward
        pen.setWidthF(1.0)
        p.setPen(pen)
        p.drawLine(QPointF(cx, ey + 22), QPointF(cx, ey + 55))      # trunk

        pen.setWidthF(0.8)
        p.setPen(pen)
        p.drawLine(QPointF(cx, ey + 30), QPointF(cx - 12, ey + 44)) # left branch
        p.drawLine(QPointF(cx, ey + 30), QPointF(cx + 12, ey + 44)) # right branch

        pen.setWidthF(0.6)
        p.setPen(pen)
        p.drawLine(QPointF(cx, ey + 38), QPointF(cx - 8,  ey + 54)) # left sub
        p.drawLine(QPointF(cx, ey + 38), QPointF(cx + 8,  ey + 54)) # right sub


class SacredGeometryBackground(QWidget):
    """
    Renders faint concentric rings + hexagonal polygon grid.
    Maps to: .app::after pseudo-element in HTML (top-right corner of window).

    Usage in RealtimeWindow._build_ui():
        self._bg_geo = SacredGeometryBackground(parent=root_widget)
        self._bg_geo.lower()   # push behind everything
        # position top-right:
        self._bg_geo.setFixedSize(500, 500)
        self._bg_geo.move(root_widget.width() - 350, -100)
    """

    def __init__(
        self,
        stroke_color: str = "#ffffff",
        opacity: float = 0.05,
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)
        self.setAttribute(Qt.WidgetAttribute.WA_NoSystemBackground)
        self.setFixedSize(500, 500)

    def set_theme(self, stroke_color: str, opacity: float) -> None:
        self._stroke = QColor(stroke_color)
        self._opacity = opacity
        self.update()

    def paintEvent(self, _event) -> None:  # type: ignore[override]
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setOpacity(self._opacity)
        pen = QPen(self._stroke, 0.5)
        p.setPen(pen)
        p.setBrush(Qt.BrushStyle.NoBrush)

        cx, cy = 250.0, 250.0
        # Concentric circles  (HTML: r=60,100,140,180)
        for r in (60, 100, 140, 180):
            p.drawEllipse(QRectF(cx - r, cy - r, r * 2, r * 2))

        # Cross hairs
        p.drawLine(QPointF(20, cy), QPointF(480, cy))
        p.drawLine(QPointF(cx, 20), QPointF(cx, 480))

        # Diagonals
        p.drawLine(QPointF(72, 72), QPointF(428, 428))
        p.drawLine(QPointF(428, 72), QPointF(72, 428))

        # Outer hexagon
        import math
        def hex_pt(angle_deg: float, r: float) -> QPointF:
            a = math.radians(angle_deg)
            return QPointF(cx + r * math.cos(a), cy + r * math.sin(a))

        for r in (170, 120):
            pts = [hex_pt(90 + 60 * i, r) for i in range(6)]
            path = QPainterPath()
            path.moveTo(pts[0])
            for pt in pts[1:]:
                path.lineTo(pt)
            path.closeSubpath()
            p.drawPath(path)


class LogoGlyphWidget(QWidget):
    """
    24×24 px eye + root glyph for the header bar.
    Maps to: the inline SVG logo-glyph in the HTML header.
    """
    def __init__(self, stroke_color: str = "#e8e8e4", parent=None):
        super().__init__(parent)
        self._stroke = QColor(stroke_color)
        self.setFixedSize(24, 24)
        self.setAttribute(Qt.WidgetAttribute.WA_TransparentForMouseEvents)

    def set_color(self, color: str) -> None:
        self._stroke = QColor(color)
        self.update()

    def paintEvent(self, _event):
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        pen = QPen(self._stroke, 0.8)
        p.setPen(pen)
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Eye ellipse
        p.drawEllipse(QRectF(6, 4, 12, 8))

        # Iris
        p.drawEllipse(QRectF(10, 6, 4, 4))

        # Pupil
        p.setBrush(self._stroke)
        p.drawEllipse(QRectF(11.5, 7.5, 1, 1))
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Accent dots
        p.setBrush(self._stroke)
        p.drawEllipse(QRectF(4.5, 7.2, 1.6, 1.6))
        p.drawEllipse(QRectF(17.9, 7.2, 1.6, 1.6))
        p.drawEllipse(QRectF(9.0, 4.0, 1.0, 1.0))
        p.drawEllipse(QRectF(14.0, 4.0, 1.0, 1.0))
        p.setBrush(Qt.BrushStyle.NoBrush)

        # Root trunk + branches
        pen.setWidthF(0.7); p.setPen(pen)
        p.drawLine(QPointF(12, 12), QPointF(12, 20))

        pen.setWidthF(0.6); p.setPen(pen)
        p.drawLine(QPointF(12, 14), QPointF(8,  18))
        p.drawLine(QPointF(12, 14), QPointF(16, 18))

        pen.setWidthF(0.5); p.setPen(pen)
        p.drawLine(QPointF(12, 16), QPointF(9,  20))
        p.drawLine(QPointF(12, 16), QPointF(15, 20))