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
import argparse

from PySide6.QtGui import QIcon
from PySide6.QtWidgets import QApplication

from gui.realtimeGUI.realtimeGUI import RealtimeWindow


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--theme", choices=["dark","light"], default="light")
    parser.add_argument("--repo-root", default=".")
    args = parser.parse_args()

    app = QApplication(sys.argv)
    app.setApplicationName("sonoPleth")

    here = Path(__file__).resolve().parent

    # Set application icon
    icon_path = here / "gui" / "miniLogo.png"
    if icon_path.exists():
        app.setWindowIcon(QIcon(str(icon_path)))
    else:
        print(f"[realtimeMain] Warning: icon not found at {icon_path}")

    win = RealtimeWindow(repo_root=str(here), theme=args.theme)
    win.show()
    win.activateWindow()

    sys.exit(app.exec())


if __name__ == "__main__":
    main()
