from __future__ import annotations

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import QFrame, QLabel, QVBoxLayout, QHBoxLayout, QComboBox, QSlider, QCheckBox, QPushButton

class RenderPanel(QFrame):
    run_clicked = Signal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("Card")

        lay = QVBoxLayout(self)
        lay.setContentsMargins(22, 26, 22, 22)
        lay.setSpacing(16)

        title = QLabel("Render Settings", self)
        title.setObjectName("SectionTitle")
        lay.addWidget(title)

        self.mode = QComboBox(self)
        self.mode.addItems(["dbap", "lbap"])
        self.mode.setEditable(False)
        lay.addWidget(self.mode)

        layout_label = QLabel("Speaker Layout", self)
        layout_label.setObjectName("Muted")
        lay.addWidget(layout_label)

        self.layout = QComboBox(self)
        self.layout.addItem("Allosphere", "spatial_engine/speaker_layouts/allosphere_layout.json")
        self.layout.addItem("Translab", "spatial_engine/speaker_layouts/translab-sono-layout.json")
        lay.addWidget(self.layout)

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

        gain_label = QLabel("Master Gain", self)
        gain_label.setObjectName("Muted")
        lay.addWidget(gain_label)

        self.gain_slider = QSlider(Qt.Horizontal, self)
        self.gain_slider.setMinimum(-20)
        self.gain_slider.setMaximum(20)
        self.gain_slider.setValue(0)
        lay.addWidget(self.gain_slider)

        ticks = QHBoxLayout()
        ticks.setContentsMargins(2, 0, 2, 0)
        a = QLabel("-20", self); a.setObjectName("Muted")
        b = QLabel("+", self); b.setObjectName("Muted")
        c = QLabel("+20", self); c.setObjectName("Muted")
        ticks.addWidget(a, alignment=Qt.AlignLeft)
        ticks.addWidget(b, alignment=Qt.AlignCenter)
        ticks.addWidget(c, alignment=Qt.AlignRight)
        lay.addLayout(ticks)

        self.analysis = QCheckBox("Create Analysis", self)
        self.analysis.setChecked(True)
        lay.addWidget(self.analysis)

        lay.addStretch(1)

        self.run_btn = QPushButton("RUN RENDER", self)
        self.run_btn.setObjectName("PrimaryButton")
        self.run_btn.setMinimumHeight(52)
        self.run_btn.clicked.connect(self.run_clicked)
        lay.addWidget(self.run_btn)

        self._update_res(self.res_slider.value())

    def _update_res(self, v: int):
        self.res_value_label.setText(f"{v/100:.1f}")

    def get_values(self):
        return (
            self.mode.currentText(),
            self.res_slider.value() / 100.0,
            float(self.gain_slider.value()),
            self.analysis.isChecked(),
            self.layout.currentData(),
        )

    def set_running(self, running: bool):
        self.run_btn.setEnabled(not running)
        self.run_btn.setText("RUNNING…" if running else "RUN RENDER")
