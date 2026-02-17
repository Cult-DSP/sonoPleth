"""iOS-style toggle switch widget for sonoPleth GUI."""
from __future__ import annotations

from PySide6.QtCore import Qt, Signal, QRectF, QPropertyAnimation, Property, QEasingCurve
from PySide6.QtGui import QPainter, QColor
from PySide6.QtWidgets import QWidget


class SwitchToggle(QWidget):
    """Painted pill-track toggle with smooth animated thumb."""

    toggled = Signal(bool)

    def __init__(self, checked: bool = True, parent=None):
        super().__init__(parent)
        self.setFixedSize(44, 24)
        self.setCursor(Qt.PointingHandCursor)
        self._checked = checked
        self._thumb_x = 22.0 if checked else 2.0

        self._anim = QPropertyAnimation(self, b"thumb_x", self)
        self._anim.setDuration(150)
        self._anim.setEasingCurve(QEasingCurve.InOutCubic)

    # Property for animation
    def _get_thumb_x(self) -> float:
        return self._thumb_x

    def _set_thumb_x(self, val: float):
        self._thumb_x = val
        self.update()

    thumb_x = Property(float, _get_thumb_x, _set_thumb_x)

    def isChecked(self) -> bool:
        return self._checked

    def setChecked(self, checked: bool):
        if self._checked == checked:
            return
        self._checked = checked
        self._animate()
        self.toggled.emit(checked)

    def mousePressEvent(self, event):
        self.setChecked(not self._checked)

    def _animate(self):
        self._anim.stop()
        self._anim.setStartValue(self._thumb_x)
        self._anim.setEndValue(22.0 if self._checked else 2.0)
        self._anim.start()

    def paintEvent(self, event):
        p = QPainter(self)
        p.setRenderHint(QPainter.Antialiasing, True)

        w, h = self.width(), self.height()

        # Track
        if self._checked:
            track_color = QColor(76, 175, 130, 180)
        else:
            track_color = QColor(0, 0, 0, 30)

        p.setPen(Qt.NoPen)
        p.setBrush(track_color)
        p.drawRoundedRect(QRectF(0, 0, w, h), h / 2, h / 2)

        # Thumb
        thumb_d = h - 4
        p.setBrush(QColor(255, 255, 255, 245))
        p.setPen(QColor(0, 0, 0, 18))
        p.drawEllipse(QRectF(self._thumb_x, 2, thumb_d, thumb_d))

        p.end()
