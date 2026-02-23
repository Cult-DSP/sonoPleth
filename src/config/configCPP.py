# src/configCPP.py
import os

if os.name == "nt":
    from .configCPP_windows import setupCppTools
else:
    # Option A: keep your existing logic here and skip this branch
    # Option B: move existing logic into configCPP_posix.py
    from .configCPP_posix import setupCppTools