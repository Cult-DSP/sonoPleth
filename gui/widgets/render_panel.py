from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QFrame, QLabel, QVBoxLayout, QHBoxLayout, QComboBox, QSlider, QPushButton,
)
from .switch_toggle import SwitchToggle


class RenderPanel(QFrame):
    """Render Settings card — no run button (lives in Pipeline now)."""

    run_clicked = Signal()  # kept for wiring, emitted from PipelinePanel

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(24, 28, 24, 24)
        lay.setSpacing(18)

        title = QLabel("Render Settings", self)
        title.setObjectName("SectionTitle")
        lay.addWidget(title)

        # --- Render mode dropdown ---
        self.mode = QComboBox(self)
        self.mode.addItems(["DBAP Focus", "DBAP", "LBAP"])
        self.mode.setEditable(False)
        lay.addWidget(self.mode)

        # --- Speaker layout dropdown ---
        layout_label = QLabel("Speaker Layout", self)
        layout_label.setObjectName("Muted")
        lay.addWidget(layout_label)

        self.layout = QComboBox(self)
        self.layout.addItem("Allosphere", "spatial_engine/speaker_layouts/allosphere_layout.json")
        self.layout.addItem("Translab", "spatial_engine/speaker_layouts/translab-sono-layout.json")
        lay.addWidget(self.layout)

        # --- Resolution slider ---
        res_label = QLabel("Resolution", self)
        res_label.setObjectName("Muted")
        lay.addWidget(res_label)

        res_row = QHBoxLayout()
        res_row.setSpacing(12)

        self.res_slider = QSlider(Qt.Horizontal, self)
        self.res_slider.setMinimum(10)
        self.res_slider.setMaximum(200)
        self.res_slider.setValue(50)
        self.res_slider.valueChanged.connect(self._update_res)
        res_row.addWidget(self.res_slider, 1)

        self.res_value = QFrame(self)
        self.res_value.setObjectName("Pill")
        pill_lay = QHBoxLayout(self.res_value)
        pill_lay.setContentsMargins(10, 0, 10, 0)
        self.res_value_label = QLabel("0.5", self.res_value)
        self.res_value_label.setObjectName("Muted")
        pill_lay.addWidget(self.res_value_label, alignment=Qt.AlignCenter)
        res_row.addWidget(self.res_value, 0)

        lay.addLayout(res_row)

        # --- Master Gain slider ---
        gain_label = QLabel("Master Gain", self)
        gain_label.setObjectName("Muted")
        lay.addWidget(gain_label)

        gain_row = QHBoxLayout()
        gain_row.setSpacing(12)

        self.gain_slider = QSlider(Qt.Horizontal, self)
        self.gain_slider.setMinimum(0)
        self.gain_slider.setMaximum(100)
        self.gain_slider.setValue(50)
        self.gain_slider.valueChanged.connect(self._update_gain)
        gain_row.addWidget(self.gain_slider, 1)

        self.gain_value = QFrame(self)
        self.gain_value.setObjectName("Pill")
        gpill_lay = QHBoxLayout(self.gain_value)
        gpill_lay.setContentsMargins(10, 0, 10, 0)
        self.gain_value_label = QLabel("0.50", self.gain_value)
        self.gain_value_label.setObjectName("Muted")
        gpill_lay.addWidget(self.gain_value_label, alignment=Qt.AlignCenter)
        gain_row.addWidget(self.gain_value, 0)

        lay.addLayout(gain_row)

        ticks = QHBoxLayout()
        ticks.setContentsMargins(2, 0, 2, 0)
        a = QLabel("0.0", self); a.setObjectName("Muted")
        b = QLabel("0.5", self); b.setObjectName("Muted")
        c = QLabel("1.0", self); c.setObjectName("Muted")
        ticks.addWidget(a, alignment=Qt.AlignLeft)
        ticks.addWidget(b, alignment=Qt.AlignCenter)
        ticks.addWidget(c, alignment=Qt.AlignRight)
        lay.addLayout(ticks)

        # --- Create Analysis toggle ---
        analysis_row = QHBoxLayout()
        analysis_row.setSpacing(12)
        analysis_label = QLabel("Create Analysis", self)
        analysis_label.setObjectName("Muted")
        analysis_row.addWidget(analysis_label)
        analysis_row.addStretch(1)
        self.analysis = SwitchToggle(checked=True, parent=self)
        analysis_row.addWidget(self.analysis)
        lay.addLayout(analysis_row)

        lay.addStretch(1)

        self._update_res(self.res_slider.value())
        self._update_gain(self.gain_slider.value())

    # --- value helpers ---

    def _update_res(self, v: int):
        self.res_value_label.setText(f"{v / 100:.1f}")

    def _update_gain(self, v: int):
        self.gain_value_label.setText(f"{v / 100:.2f}")

    def _mode_key(self) -> str:
        text = self.mode.currentText().lower().replace(" ", "")
        # Map display names to pipeline keys
        mapping = {"dbapfocus": "dbapfocus", "dbap": "dbap", "lbap": "lbap"}
        return mapping.get(text, "dbap")

    def get_values(self):
        return (
            self._mode_key(),
            self.res_slider.value() / 100.0,
            self.gain_slider.value() / 100.0,
            self.analysis.isChecked(),
            self.layout.currentData(),
        )

    def set_running(self, running: bool):
        pass  # RUN button now lives in PipelinePanel
