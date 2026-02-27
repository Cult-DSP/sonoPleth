"""
RealtimeControlsPanel.py — Live runtime parameter sliders.

Five controls, all sent via OSC to al::ParameterServer (port 9009):
  - Master Gain      /realtime/gain             0.1 – 3.0    default 0.5
  - DBAP Focus       /realtime/focus            0.2 – 5.0    default 1.5
  - Speaker Mix dB   /realtime/speaker_mix_db   -10 – +10    default 0.0
  - Sub Mix dB       /realtime/sub_mix_db       -10 – +10    default 0.0
  - Auto Comp        /realtime/auto_comp        bool         default OFF
  - Elevation Mode   /realtime/elevation_mode   0/1/2        default 0

Sliders emit schedule_osc (40 ms debounce).
Checkbox, spinbox, and combobox changes emit send_osc (immediate).

All controls disabled while Idle / Launching / Exited.
Calling reset_to_defaults() restores values without firing OSC.

Phase 10 — GUI Agent.
"""

from __future__ import annotations

from typing import Callable, Optional

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QDoubleSpinBox,
    QFrame,
    QHBoxLayout,
    QLabel,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from ..realtime_runner import RealtimeRunnerState


def _card() -> QFrame:
    f = QFrame()
    f.setObjectName("Card")
    return f


# Defaults — must match C++ engine startup defaults (see agent_gui_UPDATED_v3.md §7)
DEFAULTS = {
    "gain":           0.5,
    "focus":          1.5,
    "speaker_mix_db": 0.0,
    "sub_mix_db":     0.0,
    "auto_comp":      False,
    "elevation_mode": 0,   # 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp
}

# Elevation mode names — index must match C++ ElevationMode enum values
ELEVATION_MODE_NAMES = [
    "Rescale Atmos Up (0°–90°)",    # 0 = RescaleAtmosUp  (default)
    "Rescale Full Sphere (±90°)",   # 1 = RescaleFullSphere
    "Clamp to Layout",              # 2 = Clamp
]


class _ParamRow(QWidget):
    """
    One labelled row: QSlider + QDoubleSpinBox, kept in sync.

    Emits:
        value_changed_debounced(float)  — connect to schedule_osc (slider moves)
        value_changed_immediate(float)  — connect to send_osc (spinbox edits)
    """

    value_changed_debounced = Signal(float)
    value_changed_immediate = Signal(float)

    def __init__(
        self,
        label: str,
        minimum: float,
        maximum: float,
        default: float,
        decimals: int = 2,
        step: float = 0.01,
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._min = minimum
        self._max = maximum
        self._decimals = decimals
        self._SCALE = 10 ** decimals    # integer steps per unit

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(10)

        lbl = QLabel(label)
        lbl.setFixedWidth(150)
        lbl.setObjectName("Muted")
        layout.addWidget(lbl)

        self._slider = QSlider(Qt.Orientation.Horizontal)
        self._slider.setMinimum(int(minimum * self._SCALE))
        self._slider.setMaximum(int(maximum * self._SCALE))
        self._slider.setTickInterval(int(step * self._SCALE))
        layout.addWidget(self._slider, stretch=1)

        self._spin = QDoubleSpinBox()
        self._spin.setMinimum(minimum)
        self._spin.setMaximum(maximum)
        self._spin.setDecimals(decimals)
        self._spin.setSingleStep(step)
        self._spin.setFixedWidth(80)
        layout.addWidget(self._spin)

        # Wire slider ↔ spinbox without feedback loops
        self._updating = False
        self._slider.valueChanged.connect(self._slider_moved)
        self._spin.valueChanged.connect(self._spin_edited)

        self.set_value(default, emit=False)

    # ── Public ───────────────────────────────────────────────────────────

    def value(self) -> float:
        return self._spin.value()

    def set_value(self, v: float, emit: bool = True) -> None:
        v = max(self._min, min(self._max, v))
        self._updating = True
        self._spin.setValue(v)
        self._slider.setValue(int(round(v * self._SCALE)))
        self._updating = False
        if emit:
            self.value_changed_immediate.emit(v)

    def set_enabled(self, enabled: bool) -> None:  # type: ignore[override]
        self._slider.setEnabled(enabled)
        self._spin.setEnabled(enabled)

    # ── Internal ──────────────────────────────────────────────────────────

    def _slider_moved(self, int_val: int) -> None:
        if self._updating:
            return
        v = int_val / self._SCALE
        self._updating = True
        self._spin.setValue(v)
        self._updating = False
        self.value_changed_debounced.emit(v)

    def _spin_edited(self, v: float) -> None:
        if self._updating:
            return
        self._updating = True
        self._slider.setValue(int(round(v * self._SCALE)))
        self._updating = False
        self.value_changed_immediate.emit(v)


class RealtimeControlsPanel(QWidget):
    """
    Live runtime controls panel.

    Consumers connect:
        schedule_osc(address, value)   — for slider debounced sends
        send_osc(address, value)       — for immediate spinbox / checkbox sends

    Both are expected from the parent window / RealtimeRunner proxy.
    """

    # Emitted as (osc_address, value) — debounced (slider)
    osc_scheduled = Signal(str, float)
    # Emitted as (osc_address, value) — immediate (spinbox, checkbox)
    osc_immediate = Signal(str, float)

    def __init__(self, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._build_ui()
        self._set_all_enabled(False)

    # ── Public API ───────────────────────────────────────────────────────

    def reset_to_defaults(self) -> None:
        """Restore all controls to defaults without firing OSC sends."""
        self._gain_row.set_value(DEFAULTS["gain"], emit=False)
        self._focus_row.set_value(DEFAULTS["focus"], emit=False)
        self._spk_row.set_value(DEFAULTS["speaker_mix_db"], emit=False)
        self._sub_row.set_value(DEFAULTS["sub_mix_db"], emit=False)
        self._auto_comp_check.blockSignals(True)
        self._auto_comp_check.setChecked(DEFAULTS["auto_comp"])
        self._auto_comp_check.blockSignals(False)
        self._elev_mode_combo.blockSignals(True)
        self._elev_mode_combo.setCurrentIndex(DEFAULTS["elevation_mode"])
        self._elev_mode_combo.blockSignals(False)

    def flush_to_osc(self) -> None:
        """
        Emit current control values as immediate OSC sends.

        Call this once the engine's ParameterServer is confirmed listening so
        any values the user set during LAUNCHING are applied immediately.
        Non-default values (or simply all values) are pushed unconditionally.
        """
        self.osc_immediate.emit("/realtime/gain",           self._gain_row.value())
        self.osc_immediate.emit("/realtime/focus",          self._focus_row.value())
        self.osc_immediate.emit("/realtime/speaker_mix_db", self._spk_row.value())
        self.osc_immediate.emit("/realtime/sub_mix_db",     self._sub_row.value())
        self.osc_immediate.emit(
            "/realtime/auto_comp",
            1.0 if self._auto_comp_check.isChecked() else 0.0,
        )
        self.osc_immediate.emit(
            "/realtime/elevation_mode",
            float(self._elev_mode_combo.currentIndex()),
        )

    def update_state(self, state_name: str) -> None:
        """Enable/disable controls based on engine state."""
        try:
            s = RealtimeRunnerState(state_name)
        except ValueError:
            s = RealtimeRunnerState.ERROR
        active = s in (RealtimeRunnerState.RUNNING, RealtimeRunnerState.PAUSED)
        self._set_all_enabled(active)

    # ── UI construction ──────────────────────────────────────────────────

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(8)

        card = _card()
        layout = QVBoxLayout(card)
        layout.setContentsMargins(20, 16, 20, 16)
        layout.setSpacing(8)

        title = QLabel("Runtime Controls  (live OSC)")
        title.setObjectName("SectionTitle")
        layout.addWidget(title)

        self._gain_row  = _ParamRow("Master Gain",       0.1, 3.0,   DEFAULTS["gain"],           decimals=2, step=0.05)
        self._focus_row = _ParamRow("DBAP Focus",        0.2, 5.0,   DEFAULTS["focus"],          decimals=2, step=0.05)
        self._spk_row   = _ParamRow("Speaker Mix  (dB)", -10.0, 10.0, DEFAULTS["speaker_mix_db"], decimals=1, step=0.5)
        self._sub_row   = _ParamRow("Sub Mix  (dB)",     -10.0, 10.0, DEFAULTS["sub_mix_db"],     decimals=1, step=0.5)

        for row in (self._gain_row, self._focus_row, self._spk_row, self._sub_row):
            layout.addWidget(row)

        # Auto-compensation checkbox
        self._auto_comp_check = QCheckBox("Focus Auto-Compensation")
        self._auto_comp_check.setChecked(DEFAULTS["auto_comp"])
        layout.addWidget(self._auto_comp_check)

        # Vertical rescaling mode selector
        elev_row = QWidget()
        elev_layout = QHBoxLayout(elev_row)
        elev_layout.setContentsMargins(0, 0, 0, 0)
        elev_layout.setSpacing(10)
        elev_lbl = QLabel("Elevation Mode")
        elev_lbl.setFixedWidth(150)
        elev_lbl.setObjectName("Muted")
        elev_layout.addWidget(elev_lbl)
        self._elev_mode_combo = QComboBox()
        for name in ELEVATION_MODE_NAMES:
            self._elev_mode_combo.addItem(name)
        self._elev_mode_combo.setCurrentIndex(DEFAULTS["elevation_mode"])
        elev_layout.addWidget(self._elev_mode_combo, stretch=1)
        layout.addWidget(elev_row)

        root.addWidget(card)

        # Wire OSC signals
        self._gain_row.value_changed_debounced.connect(
            lambda v: self.osc_scheduled.emit("/realtime/gain", v))
        self._gain_row.value_changed_immediate.connect(
            lambda v: self.osc_immediate.emit("/realtime/gain", v))

        self._focus_row.value_changed_debounced.connect(
            lambda v: self.osc_scheduled.emit("/realtime/focus", v))
        self._focus_row.value_changed_immediate.connect(
            lambda v: self.osc_immediate.emit("/realtime/focus", v))

        self._spk_row.value_changed_debounced.connect(
            lambda v: self.osc_scheduled.emit("/realtime/speaker_mix_db", v))
        self._spk_row.value_changed_immediate.connect(
            lambda v: self.osc_immediate.emit("/realtime/speaker_mix_db", v))

        self._sub_row.value_changed_debounced.connect(
            lambda v: self.osc_scheduled.emit("/realtime/sub_mix_db", v))
        self._sub_row.value_changed_immediate.connect(
            lambda v: self.osc_immediate.emit("/realtime/sub_mix_db", v))

        self._auto_comp_check.stateChanged.connect(
            lambda s: self.osc_immediate.emit(
                "/realtime/auto_comp", 1.0 if s else 0.0))

        self._elev_mode_combo.currentIndexChanged.connect(
            lambda idx: self.osc_immediate.emit(
                "/realtime/elevation_mode", float(idx)))

    def _set_all_enabled(self, enabled: bool) -> None:
        for row in (self._gain_row, self._focus_row,
                    self._spk_row, self._sub_row):
            row.set_enabled(enabled)
        self._auto_comp_check.setEnabled(enabled)
        self._elev_mode_combo.setEnabled(enabled)
