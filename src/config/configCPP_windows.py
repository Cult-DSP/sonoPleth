# src/configCPP_windows.py
import os
import subprocess
from pathlib import Path

def _cpu_count() -> int:
    return os.cpu_count() or 4

def _run(cmd, cwd=None) -> None:
    subprocess.check_call(cmd, cwd=cwd)

def _cmake_build(build_dir: Path, parallel: int) -> None:
    # Visual Studio generators are multi-config; Ninja is single-config.
    # Safe default: always pass --config Release; it is ignored by single-config generators.
    cfg = os.environ.get("SONOPLETH_CMAKE_CONFIG", "Release")
    _run(["cmake", "--build", str(build_dir), "--parallel", str(parallel), "--config", cfg])

def setupCppTools() -> bool:
    """
    Windows-only C++ tool setup.
    Keep behavior aligned with configCPP.py:
    - create/build required subprojects
    - return True on success, False on failure
    """
    try:
        parallel = int(os.environ.get("SONOPLETH_BUILD_PARALLEL", str(_cpu_count())))

        # TODO: Mirror your existing directory layout:
        # - locate repo root relative to this file
        repo_root = Path(__file__).resolve().parents[1]

        # Example: call your same build directories as in posix config:
        # build_dir = repo_root / "build" / "adm_extract"
        # source_dir = repo_root / "dependencies" / "adm_extract"
        #
        # Replace these with your real paths from your existing configCPP.py.
        #
        # _run(["cmake", "-S", str(source_dir), "-B", str(build_dir)], cwd=repo_root)
        # _cmake_build(build_dir, parallel)

        # If you have multiple tools to build, do each in sequence.

        return True
    except Exception as e:
        print(f"[Windows configCPP] setupCppTools failed: {e}")
        return False