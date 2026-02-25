"""
runRealtime.py — Python entry point for the sonoPleth Real-Time Spatial Audio Engine

Mirrors runPipeline.py: accepts the same inputs (ADM WAV or LUSID package +
speaker layout), runs the same preprocessing pipeline, then launches the
real-time C++ engine instead of the offline renderer.

Input types (identical to runPipeline.py):
  1. ADM WAV file  → extract ADM metadata → parse to LUSID scene → package
     (split stems + write scene.lusid.json) → launch real-time engine
  2. LUSID package directory (already has scene.lusid.json + mono WAVs)
     → validate → launch real-time engine directly

Usage (CLI):
    # From ADM source:
    python runRealtime.py sourceData/driveExampleSpruce.wav spatial_engine/speaker_layouts/allosphere_layout.json

    # From LUSID package:
    python runRealtime.py sourceData/lusid_package spatial_engine/speaker_layouts/allosphere_layout.json

Usage (from GUI or other Python code):
    from runRealtime import run_realtime_from_ADM, run_realtime_from_LUSID
    success = run_realtime_from_LUSID(
        "sourceData/lusid_package",
        "spatial_engine/speaker_layouts/allosphere_layout.json"
    )
"""

import subprocess
import signal
import sys
import os
from pathlib import Path

from src.config.configCPP import setupCppTools
from src.analyzeADM.extractMetadata import extractMetaData
from src.analyzeADM.checkAudioChannels import channelHasAudio, exportAudioActivity
from src.packageADM.packageForRender import packageForRender, writeSceneOnly


# ─────────────────────────────────────────────────────────────────────────────
# Core engine launcher (shared by both ADM and LUSID paths)
# ─────────────────────────────────────────────────────────────────────────────

def _launch_realtime_engine(
    scene_json,
    speaker_layout,
    sources_folder=None,
    adm_file=None,
    samplerate=48000,
    buffersize=512,
    gain=0.5,
    dbap_focus=1.5
):
    """
    Launch the C++ real-time engine as a subprocess.

    This is the final step of both the ADM and LUSID pipelines. It takes
    already-prepared paths (scene JSON, source input, speaker layout)
    and launches the C++ executable.

    Two input modes (mutually exclusive):
    - sources_folder: Path to folder containing mono source WAV files (LUSID mode)
    - adm_file: Path to multichannel ADM WAV file (ADM direct streaming mode)

    Parameters
    ----------
    scene_json : str or Path
        Path to scene.lusid.json (positions/trajectories).
    speaker_layout : str or Path
        Path to speaker layout JSON.
    sources_folder : str or Path, optional
        Path to folder containing mono source WAV files (X.1.wav, LFE.wav).
    adm_file : str or Path, optional
        Path to multichannel ADM WAV file for direct streaming (skips stem splitting).
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback (default: 512).
    gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5, range: 0.2–5.0).

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.

    Notes
    -----
    Output channel count is derived automatically from the speaker layout
    by the C++ engine (Spatializer::init). No channel count parameter needed.
    """

    # Validate mutually exclusive inputs
    if not sources_folder and not adm_file:
        print("✗ Error: Either sources_folder or adm_file must be provided.")
        return False
    if sources_folder and adm_file:
        print("✗ Error: sources_folder and adm_file are mutually exclusive.")
        return False

    use_adm = adm_file is not None

    project_root = Path(__file__).parent.resolve()

    executable = (
        project_root
        / "spatial_engine"
        / "realtimeEngine"
        / "build"
        / "sonoPleth_realtime"
    )

    if not executable.exists():
        print(f"✗ Error: Realtime engine executable not found at {executable}")
        print("  Build it with:")
        print("    cd spatial_engine/realtimeEngine/build && cmake .. && make -j4")
        return False

    # Resolve paths
    scene_path = Path(scene_json).resolve()
    layout_path = Path(speaker_layout).resolve()

    # Validate common paths
    if not scene_path.exists():
        print(f"✗ Error: Scene file not found: {scene_path}")
        return False
    if not layout_path.exists():
        print(f"✗ Error: Speaker layout not found: {layout_path}")
        return False
    if not (0.0 <= gain <= 1.0):
        print(f"✗ Error: Invalid gain '{gain}'. Must be in range [0.0, 1.0].")
        return False
    if not (0.2 <= dbap_focus <= 5.0):
        print(f"✗ Error: Invalid dbap_focus '{dbap_focus}'. Must be in range [0.2, 5.0].")
        return False

    # Build command based on input mode
    cmd = [
        str(executable),
        "--layout", str(layout_path),
        "--scene", str(scene_path),
        "--samplerate", str(samplerate),
        "--buffersize", str(buffersize),
        "--gain", str(gain),
    ]

    if use_adm:
        adm_path = Path(adm_file).resolve()
        if not adm_path.exists():
            print(f"✗ Error: ADM file not found: {adm_path}")
            return False
        cmd.extend(["--adm", str(adm_path)])
        source_label = f"ADM:  {adm_path} (direct streaming)"
    else:
        sources_path = Path(sources_folder).resolve()
        if not sources_path.exists():
            print(f"✗ Error: Sources folder not found: {sources_path}")
            return False
        cmd.extend(["--sources", str(sources_path)])
        source_label = f"Sources: {sources_path} (mono files)"

    # Print launch info
    print("\n╔══════════════════════════════════════════════════════════╗")
    print("║     sonoPleth Real-Time Engine — Launching               ║")
    print("╚══════════════════════════════════════════════════════════╝\n")
    print(f"  Scene:          {scene_path}")
    print(f"  {source_label}")
    print(f"  Speaker layout: {layout_path}")
    print(f"  Sample rate:    {samplerate} Hz")
    print(f"  Buffer size:    {buffersize} frames")
    print(f"  Master gain:    {gain}")
    print(f"  DBAP focus:     {dbap_focus}")
    print(f"  (Output channels derived from speaker layout)")
    print(f"\n  Command: {' '.join(cmd)}\n")

    # Launch — engine runs until Ctrl+C. We forward SIGINT so it shuts down cleanly.
    try:
        print("  Starting real-time engine...")
        print("  Press Ctrl+C to stop.\n")

        process = subprocess.Popen(cmd)
        process.wait()

        exit_code = process.returncode
        if exit_code == 0:
            print("\n✓ Real-time engine exited cleanly.")
            return True
        elif exit_code == -2 or exit_code == 130:
            # SIGINT (Ctrl+C) — normal exit path
            print("\n✓ Real-time engine stopped by user (Ctrl+C).")
            return True
        else:
            print(f"\n✗ Real-time engine exited with code {exit_code}")
            return False

    except KeyboardInterrupt:
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
# Pipeline entry points (mirror runPipeline.py)
# ─────────────────────────────────────────────────────────────────────────────

def run_realtime_from_ADM(
    source_adm_file,
    source_speaker_layout,
    master_gain=0.5,
    dbap_focus=1.5,
    samplerate=48000,
    buffersize=512,
    scan_audio=False
):
    """
    Run the complete ADM → real-time spatial audio pipeline.

    Same preprocessing as runPipeline.run_pipeline_from_ADM:
      1. Setup C++ tools (idempotent)
      2. Analyze audio channels for content  ← skipped by default (scan_audio=False)
      3. Extract ADM metadata from source WAV
      4. Parse ADM XML to LUSID scene
      5. Write scene.lusid.json (no stem splitting — ADM direct streaming)
    Then launches the real-time engine instead of the offline renderer.

    Parameters
    ----------
    source_adm_file : str
        Path to source ADM WAV file.
    source_speaker_layout : str
        Path to speaker layout JSON.
    master_gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5).
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback (default: 512).
    scan_audio : bool
        Whether to run the full per-channel audio activity scan before
        parsing the ADM XML (default: False).

        When False (default): skips channelHasAudio() and exportAudioActivity().
        A synthetic "all channels active" result is passed to the LUSID parser
        instead, saving ~14 seconds of up-front I/O on large ADM files.
        Use this for real-time playback where startup latency matters.

        When True: runs the full scan and writes processedData/containsAudio.json.
        Useful if the LUSID parser needs accurate per-channel silence data
        (e.g., to suppress truly silent channels from the scene graph).

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.
    """

    # Step 0: Check initialization
    project_root = Path(__file__).parent.resolve()
    init_flag = project_root / ".init_complete"
    if not init_flag.exists():
        print("\n" + "!" * 80)
        print("⚠ WARNING: Project not initialized!")
        print("!" * 80)
        print("\nPlease run: ./init.sh")
        return False

    # Step 1: Setup C++ tools (idempotent — only builds if needed)
    print("\n" + "=" * 80)
    print("STEP 1: Verifying C++ tools and dependencies")
    print("=" * 80)
    if not setupCppTools():
        print("\n✗ Error: C++ tools setup failed")
        print("\nTry re-initializing:")
        print("  rm .init_complete && ./init.sh")
        return False

    processed_data_dir = "processedData"

    # Step 2: Audio channel analysis (optional — skipped by default)
    print("\n" + "=" * 80)
    if scan_audio:
        print("STEP 2: Analyzing audio channels and extracting ADM metadata")
        print("=" * 80)
        print("Scanning audio channels for content (scan_audio=True)...")
        exportAudioActivity(source_adm_file, output_path="processedData/containsAudio.json", threshold_db=-100)
        contains_audio_data = channelHasAudio(source_adm_file, threshold_db=-100, printChannelUpdate=False)
    else:
        print("STEP 2: Skipping audio channel scan (scan_audio=False — default)")
        print("=" * 80)
        # Build a synthetic all-channels-active result so the LUSID parser
        # treats every channel as containing audio. The real-time engine
        # handles silent channels gracefully during streaming — no scan needed.
        import soundfile as sf
        _info = sf.info(source_adm_file)
        contains_audio_data = {
            "sample_rate": _info.samplerate,
            "threshold_db": -100,
            "elapsed_seconds": 0.0,
            "channels": [
                {"channel_index": i, "rms_db": 0.0, "contains_audio": True}
                for i in range(_info.channels)
            ]
        }
        print(f"  Treating all {_info.channels} channels as active (no scan performed).")
        print("  Pass scan_audio=True or --scan_audio on the CLI to run the full scan.")

    # Extract ADM XML metadata from WAV
    print("Extracting ADM metadata from WAV file...")
    extracted_metadata = extractMetaData(source_adm_file, "processedData/currentMetaData.xml")

    if extracted_metadata:
        xml_path = extracted_metadata
        print(f"Using extracted XML metadata at {xml_path}")
    else:
        print("Using default XML metadata file")
        xml_path = "data/POE-ATMOS-FINAL-metadata.xml"

    # Step 3: Parse ADM XML to LUSID scene
    print("\n" + "=" * 80)
    print("STEP 3: Parsing ADM metadata to LUSID scene")
    print("=" * 80)
    from LUSID.src.xml_etree_parser import parse_adm_xml_to_lusid_scene
    lusid_scene = parse_adm_xml_to_lusid_scene(xml_path, contains_audio=contains_audio_data)
    lusid_scene.summary()

    # Step 4: Write scene.lusid.json ONLY (no stem splitting — ADM direct streaming)
    print("\n" + "=" * 80)
    print("STEP 4: Writing scene.lusid.json (ADM direct streaming — no stem splitting)")
    print("=" * 80)
    scene_json_path = writeSceneOnly(lusid_scene, processed_data_dir)
    print(f"✓ Scene written to: {scene_json_path}")
    print("  (Skipping stem splitting — engine reads directly from ADM WAV)")

    # Step 5: Launch real-time engine in ADM direct streaming mode
    print("\n" + "=" * 80)
    print("STEP 5: Launching real-time engine (ADM direct streaming)")
    print("=" * 80)
    return _launch_realtime_engine(
        scene_json=scene_json_path,
        speaker_layout=source_speaker_layout,
        adm_file=source_adm_file,
        samplerate=samplerate,
        buffersize=buffersize,
        gain=master_gain,
        dbap_focus=dbap_focus
    )


def run_realtime_from_LUSID(
    source_lusid_package,
    source_speaker_layout,
    master_gain=0.5,
    dbap_focus=1.5,
    samplerate=48000,
    buffersize=512
):
    """
    Run real-time engine from an existing LUSID package.

    Same validation as src/createFromLUSID.run_pipeline_from_LUSID but launches
    the real-time engine instead of the offline renderer. No preprocessing
    needed — the LUSID package already contains scene.lusid.json + mono WAVs.

    Parameters
    ----------
    source_lusid_package : str
        Path to LUSID package directory (must contain scene.lusid.json).
    source_speaker_layout : str
        Path to speaker layout JSON.
    master_gain : float
        Master gain 0.0–1.0 (default: 0.5).
    dbap_focus : float
        DBAP focus/rolloff exponent (default: 1.5).
    samplerate : int
        Audio sample rate in Hz (default: 48000).
    buffersize : int
        Frames per audio callback (default: 512).

    Returns
    -------
    bool
        True if the engine ran and exited cleanly, False on error.
    """

    # Validate LUSID package
    package_path = Path(source_lusid_package)
    if not package_path.exists():
        print(f"✗ Error: LUSID package directory not found: {source_lusid_package}")
        return False

    scene_file = package_path / "scene.lusid.json"
    if not scene_file.exists():
        print(f"✗ Error: scene.lusid.json not found in package: {scene_file}")
        return False

    layout_path = Path(source_speaker_layout)
    if not layout_path.exists():
        print(f"✗ Error: Speaker layout file not found: {source_speaker_layout}")
        return False

    print(f"✓ LUSID package: {package_path}")
    print(f"✓ Scene file:    {scene_file}")
    print(f"✓ Speaker layout: {layout_path}")

    # Launch real-time engine directly (no preprocessing needed)
    return _launch_realtime_engine(
        scene_json=str(scene_file),
        speaker_layout=str(layout_path),
        sources_folder=str(package_path),
        samplerate=samplerate,
        buffersize=buffersize,
        gain=master_gain,
        dbap_focus=dbap_focus
    )


# ─────────────────────────────────────────────────────────────────────────────
# Source type detection (same logic as runPipeline.py)
# ─────────────────────────────────────────────────────────────────────────────

def checkSourceType(arg):
    """
    Detect whether the input is an ADM WAV file or a LUSID package directory.

    Returns 'ADM' for .wav files, 'LUSID' for directories containing
    scene.lusid.json, or an error string otherwise.
    """
    if not os.path.exists(arg):
        return "Path does not exist"

    if os.path.isfile(arg):
        if arg.lower().endswith('.wav'):
            return "ADM"

    if os.path.isdir(arg):
        # Check for scene.lusid.json inside the directory (more robust
        # than checking basename — works for any package directory name)
        if os.path.exists(os.path.join(arg, "scene.lusid.json")):
            return "LUSID"

    return "Wrong Input Type"


# ─────────────────────────────────────────────────────────────────────────────
# CLI entry point
# ─────────────────────────────────────────────────────────────────────────────

if __name__ == "__main__":
    print("sonoPleth Real-Time Engine Launcher")
    print("=" * 60)

    if len(sys.argv) >= 2:
        source_input = sys.argv[1]
        source_type = checkSourceType(source_input)
        source_speaker_layout = sys.argv[2] if len(sys.argv) >= 3 else "spatial_engine/speaker_layouts/allosphere_layout.json"
        master_gain = float(sys.argv[3]) if len(sys.argv) >= 4 else 0.5
        dbap_focus = float(sys.argv[4]) if len(sys.argv) >= 5 else 1.5
        buffersize = int(sys.argv[5]) if len(sys.argv) >= 6 else 512
        scan_audio = "--scan_audio" in sys.argv  # flag: default OFF

        if source_type == "ADM":
            print(f"Detected ADM source: {source_input}")
            success = run_realtime_from_ADM(
                source_input, source_speaker_layout,
                master_gain=master_gain, dbap_focus=dbap_focus,
                buffersize=buffersize, scan_audio=scan_audio
            )
        elif source_type == "LUSID":
            print(f"Detected LUSID package: {source_input}")
            success = run_realtime_from_LUSID(
                source_input, source_speaker_layout,
                master_gain=master_gain, dbap_focus=dbap_focus,
                buffersize=buffersize
            )
        elif source_type == "Path does not exist":
            print(f"✗ Error: Path does not exist: {source_input}")
            success = False
        else:
            print(f"✗ Error: Unrecognized input type for: {source_input}")
            print("  Expected: ADM WAV file (.wav) or LUSID package directory (containing scene.lusid.json)")
            success = False

        sys.exit(0 if success else 1)

    else:
        print("\nUsage:")
        print("  python runRealtime.py <source> [speaker_layout] [master_gain] [dbap_focus] [buffersize] [--scan_audio]")
        print("\nArguments:")
        print("  <source>          ADM WAV file (.wav) or LUSID package directory")
        print("  [speaker_layout]  Speaker layout JSON (default: allosphere_layout.json)")
        print("  [master_gain]     Master gain 0.0–1.0 (default: 0.5)")
        print("  [dbap_focus]      DBAP focus/rolloff 0.2–5.0 (default: 1.5)")
        print("  [buffersize]      Audio buffer size in frames (default: 512)")
        print("  [--scan_audio]    Run full per-channel audio activity scan before")
        print("                    parsing ADM metadata (ADM path only, default: OFF).")
        print("                    Adds ~14s startup time but filters truly silent channels.")
        print("\nExamples:")
        print("  # From ADM WAV (runs full preprocessing pipeline, scan skipped):")
        print("  python runRealtime.py sourceData/driveExampleSpruce.wav")
        print("")
        print("  # From ADM WAV with audio scan enabled:")
        print("  python runRealtime.py sourceData/driveExampleSpruce.wav allosphere_layout.json 0.5 1.5 512 --scan_audio")
        print("")
        print("  # From LUSID package (skips preprocessing entirely):")
        print("  python runRealtime.py sourceData/lusid_package")
        print("")
        print("  # With custom layout and settings:")
        print("  python runRealtime.py sourceData/lusid_package spatial_engine/speaker_layouts/allosphere_layout.json 0.3 1.5 256")
        print("\nNote: Output channels are derived automatically from the speaker layout.")
        sys.exit(1)
