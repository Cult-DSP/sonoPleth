import subprocess
from pathlib import Path


# ---------------------------------------------------------------------------
# Locate the embedded ADM extractor binary (built by init.sh / configCPP.py).
# Lives at: src/adm_extract/build/sonopleth_adm_extract
# relative to the project root (two levels up from this file's directory).
# ---------------------------------------------------------------------------
_PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
_EMBEDDED_EXTRACTOR = _PROJECT_ROOT / "src" / "adm_extract" / "build" / "sonopleth_adm_extract"


def extractMetaData(wavPath, outXmlPath):
    """Extract ADM XML from a BW64/RF64/WAV file and write it to outXmlPath.

    Uses the embedded sonopleth_adm_extract binary (built via init.sh).
    Raises FileNotFoundError if the binary has not been built yet.

    Output path is always outXmlPath (processedData/currentMetaData.xml).
    Downstream modules (parser.py, xml_etree_parser.py) are unchanged.
    """
    print("Extracting ADM metadata from WAV file...")

    if not _EMBEDDED_EXTRACTOR.exists():
        raise FileNotFoundError(
            f"sonopleth_adm_extract not found at {_EMBEDDED_EXTRACTOR}\n"
            "Run ./init.sh (or configCPP.buildAdmExtractor()) to build it."
        )

    cmd = [str(_EMBEDDED_EXTRACTOR), "--in", wavPath, "--out", outXmlPath]
    subprocess.run(cmd, check=True)
    print(f"Exported ADM metadata to {outXmlPath}")
    return outXmlPath

