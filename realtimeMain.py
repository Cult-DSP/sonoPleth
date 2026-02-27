#!/usr/bin/env python3
"""
realtimeMain.py — Standalone launcher for the sonoPleth Real-Time Engine GUI.

Usage:
    python realtimeMain.py

Opens a standalone PySide6 window (separate from the offline pipeline GUI).
Loads gui/styles.qss for consistent styling.

Phase 10 — GUI Agent.
"""

import sys
from pathlib import Path

from PySide6.QtWidgets import QApplication

from gui.realtimeGUI.realtimeGUI import RealtimeWindow


def main() -> None:
    app = QApplication(sys.argv)
    app.setApplicationName("sonoPleth Real-Time")

    here = Path(__file__).resolve().parent

    # Load shared stylesheet
    qss_path = here / "gui" / "styles.qss"
    if qss_path.exists():
        app.setStyleSheet(qss_path.read_text(encoding="utf-8"))
    else:
        print(f"[realtimeMain] Warning: stylesheet not found at {qss_path}")

    win = RealtimeWindow(repo_root=str(here))
    win.show()
    win.activateWindow()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
