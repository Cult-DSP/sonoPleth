import subprocess
import os
from pathlib import Path


# ---------------------------------------------------------------------------
# Locate the embedded ADM extractor binary (built by init.sh / configCPP.py).
# It lives at:  tools/adm_extract/build/sonopleth_adm_extract
# relative to the project root (two levels up from this file's directory).
# ---------------------------------------------------------------------------
_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_EMBEDDED_EXTRACTOR = _PROJECT_ROOT / "src" / "adm_extract" / "build" / "sonopleth_adm_extract"


def extractMetaData(wavPath, outXmlPath):
    """Extract ADM XML from a BW64/RF64/WAV file and write it to outXmlPath.

    Preference order:
      1. Embedded tool  — tools/adm_extract/build/sonopleth_adm_extract
      2. bwfmetaedit    — system-installed fallback (kept for compatibility)

    Output path is always outXmlPath (processedData/currentMetaData.xml).
    Downstream modules (parser.py, xml_etree_parser.py) are unchanged.
    """
    print("Extracting ADM metadata from WAV file...")

    # --- Prefer embedded extractor ----------------------------------------
    if _EMBEDDED_EXTRACTOR.exists():
        cmd = [str(_EMBEDDED_EXTRACTOR), "--in", wavPath, "--out", outXmlPath]
        try:
            subprocess.run(cmd, check=True)
            print(f"Exported ADM metadata to {outXmlPath} (via sonopleth_adm_extract)")
            return outXmlPath
        except subprocess.CalledProcessError as e:
            print(f" ERROR running sonopleth_adm_extract: {e}")
            print(" Falling back to bwfmetaedit...")
        # fall through to bwfmetaedit on error
    else:
        print(f" NOTE: Embedded extractor not found at {_EMBEDDED_EXTRACTOR}")
        print("       Run init.sh (or configCPP.buildAdmExtractor()) to build it.")
        print("       Falling back to bwfmetaedit...")

    # --- Fallback: bwfmetaedit -------------------------------------------
    cmd = [
        "bwfmetaedit",
        f"--out-xml={outXmlPath}",
        wavPath,
    ]
    try:
        subprocess.run(cmd, check=True)
        print(f"Exported ADM metadata to {outXmlPath} (via bwfmetaedit)")
    except subprocess.CalledProcessError as e:
        print(f" ERROR running bwfmetaedit: {e}")
    except FileNotFoundError:
        print(" ERROR: Neither sonopleth_adm_extract nor bwfmetaedit found!")
        print(" Build embedded tool: run init.sh")
        print(" Or install bwfmetaedit: brew install bwfmetaedit")
        print("   https://mediaarea.net/BWFMetaEdit")

    return outXmlPath



