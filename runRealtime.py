"""
runRealtime.py — Python entry point for the sonoPleth Real-Time Spatial Audio Engine

Mirrors the structure of runPipeline.py / src/createRender.py but launches the
real-time engine instead of the offline renderer. This file is the Python-side
interface that the GUI will call.

Phase 1: Launches the realtime engine which opens the audio device and streams
silence. Future phases will add actual audio playback.

Usage (standalone):
    python runRealtime.py

Usage (from GUI or other Python code):
    from runRealtime import runRealtimeEngine
    success = runRealtimeEngine(
        speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
        scene="processedData/stageForRender/scene.lusid.json",
        sources="sourceData/lusid_package"
    )
"""

import subprocess
import signal
import sys
import os
from pathlib import Path


def runRealtimeEngine(
    scene="processedData/stageForRender/scene.lusid.json",
    speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
    sources="sourceData/lusid_package",
    samplerate=48000,
    buffersize=512,
    channels=60,
    gain=0.5,
    dbap_focus=1.5
):
    """
    Launch the real-time spatial audio engine.

    This function starts the C++ realtime engine as a subprocess. The engine
    opens the audio device and streams spatialized audio in real-time. It runs
    until the user sends Ctrl+C or the scene ends.

    Parameters
    ----------
    scene : str
        Path to the LUSID scene JSON file (positions/trajectories).
    speaker_layout : str
        Path to the speaker layout JSON file.
    sources : str
        Path to the folder containing mono source WAV files.
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback buffer (default: 512).
    channels : int
        Number of output channels (default: 60).
    gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5, range: 0.2–5.0).

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.
    """

    # ── Resolve paths ─────────────────────────────────────────────────────
    project_root = Path(__file__).parent.resolve()

    executable = (
        project_root
        / "spatial_engine"
        / "realtimeEngine"
        / "build"
        / "sonoPleth_realtime"
    )

    if not executable.exists():
        print(f"Error: Realtime engine executable not found at {executable}")
        print("Build it with:")
        print("  cd spatial_engine/realtimeEngine/build && cmake .. && make -j4")
        return False

    # Resolve input paths relative to project root
    scene_path = (project_root / scene).resolve()
    layout_path = (project_root / speaker_layout).resolve()
    sources_path = (project_root / sources).resolve()

    # ── Validate inputs ───────────────────────────────────────────────────

    if not scene_path.exists():
        print(f"Error: Scene file not found: {scene_path}")
        return False

    if not layout_path.exists():
        print(f"Error: Speaker layout not found: {layout_path}")
        return False

    if not sources_path.exists():
        print(f"Error: Sources folder not found: {sources_path}")
        return False

    if not (0.0 <= gain <= 1.0):
        print(f"Error: Invalid gain '{gain}'. Must be in range [0.0, 1.0].")
        return False

    if not (0.2 <= dbap_focus <= 5.0):
        print(f"Error: Invalid dbap_focus '{dbap_focus}'. Must be in range [0.2, 5.0].")
        return False

    # ── Build command ─────────────────────────────────────────────────────

    cmd = [
        str(executable),
        "--layout", str(layout_path),
        "--scene", str(scene_path),
        "--sources", str(sources_path),
        "--samplerate", str(samplerate),
        "--buffersize", str(buffersize),
        "--channels", str(channels),
        "--gain", str(gain),
    ]

    # ── Print launch info ─────────────────────────────────────────────────

    print("\n╔══════════════════════════════════════════════════════════╗")
    print("║     sonoPleth Real-Time Engine — Python Launcher        ║")
    print("╚══════════════════════════════════════════════════════════╝\n")
    print(f"  Scene:          {scene_path}")
    print(f"  Speaker layout: {layout_path}")
    print(f"  Sources:        {sources_path}")
    print(f"  Sample rate:    {samplerate} Hz")
    print(f"  Buffer size:    {buffersize} frames")
    print(f"  Channels:       {channels}")
    print(f"  Master gain:    {gain}")
    print(f"  DBAP focus:     {dbap_focus}")
    print(f"\n  Command: {' '.join(cmd)}\n")

    # ── Launch the engine ─────────────────────────────────────────────────
    # The engine runs until Ctrl+C. We forward the signal to the child
    # process so it can shut down cleanly.

    try:
        print("  Starting real-time engine...")
        print("  Press Ctrl+C to stop.\n")

        process = subprocess.Popen(cmd)

        # Wait for the process to finish (or be interrupted)
        process.wait()

        exit_code = process.returncode
        if exit_code == 0:
            print("\n✓ Real-time engine exited cleanly.")
            return True
        elif exit_code == -2 or exit_code == 130:
            # SIGINT (Ctrl+C) — this is the normal exit path
            print("\n✓ Real-time engine stopped by user (Ctrl+C).")
            return True
        else:
            print(f"\n✗ Real-time engine exited with code {exit_code}")
            return False

    except KeyboardInterrupt:
        # Python caught Ctrl+C before the subprocess did
        print("\n  Ctrl+C caught — stopping engine...")
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            print("  Engine did not stop in time, killing...")
            process.kill()
        print("✓ Real-time engine stopped.")
        return True

    except FileNotFoundError:
        print(f"\n✗ Could not launch executable: {executable}")
        print("  Make sure the realtime engine is built.")
        return False

    except Exception as e:
        print(f"\n✗ Unexpected error: {e}")
        return False


# ─────────────────────────────────────────────────────────────────────────────
# Standalone execution
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("sonoPleth Real-Time Engine Launcher")
    print("=" * 40)

    # Use defaults — same paths as the offline pipeline
    success = runRealtimeEngine(
        channels=2,       # Use 2 channels for local testing
        buffersize=256    # Smaller buffer for lower latency
    )

    sys.exit(0 if success else 1)
