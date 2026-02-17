"""Drop-shadow helpers for sonoPleth GUI cards and buttons."""
from __future__ import annotations

from PySide6.QtWidgets import QWidget, QGraphicsDropShadowEffect
from PySide6.QtGui import QColor


def apply_card_shadow(widget: QWidget, blur: float = 32, alpha: int = 28, offset_y: float = 8):
    """Subtle card elevation shadow."""
    fx = QGraphicsDropShadowEffect(widget)
    fx.setBlurRadius(blur)
    fx.setColor(QColor(0, 0, 0, alpha))
    fx.setOffset(0, offset_y)
    widget.setGraphicsEffect(fx)


def apply_button_shadow(widget: QWidget, blur: float = 40, alpha: int = 45, offset_y: float = 10):
    """Slightly stronger shadow for primary CTA buttons."""
    fx = QGraphicsDropShadowEffect(widget)
    fx.setBlurRadius(blur)
    fx.setColor(QColor(0, 0, 0, alpha))
    fx.setOffset(0, offset_y)
    widget.setGraphicsEffect(fx)
