import os

if os.name == "nt":
    from .configCPP_windows import setupCppTools
else:
    from .configCPP_posix import setupCppTools

__all__ = ["setupCppTools"]