from src.configCPP import setupCppTools
from src.analyzeADM.extractMetadata import extractMetaData
from src.analyzeADM.parser import parseMetadata
from src.analyzeADM.checkAudioChannels import channelHasAudio, exportAudioActivity
from src.packageADM.packageForRender import packageForRender
from src.createRender import runVBAPRender
from src.analyzeRender import analyzeRenderOutput
from pathlib import Path
import subprocess
import sys


# Current pipeline:
# 0. Check initialization - if not initialized, prompt to run ./init.sh
# 1. Setup C++ tools - install bwfmetaedit, initialize git submodules (allolib), build spatial renderer (only if needed)
# 2. Extract ADM metadata from source WAV using bwfmetaedit
# 3. Parse ADM metadata into internal data structure (optionally export JSON for analysis)
# 4. Analyze audio channels for content (generate containsAudio.json)
# 5. Run packageForRender - split stems (X.1.wav naming) and build LUSID scene (scene.lusid.json)
# 6. Run spatial renderer - create multichannel spatial render (reads LUSID scene directly)
# 7. Analyze render output - create PDF with dB analysis of each channel in final render


def check_initialization():
    """
    Check if init.sh has been run by looking for .init_complete flag file.
    
    Returns:
    --------
    bool
        True if initialized, False otherwise
    """
    project_root = Path(__file__).parent.resolve()
    init_flag = project_root / ".init_complete"
    
    if init_flag.exists():
        return True
    
    print("\n" + "!"*80)
    print("⚠ WARNING: Project not initialized!")
    print("!"*80)
    print("\nPlease run the initialization script first:")
    print("  ./init.sh")
    print("\nThis will:")
    print("  1. Create Python virtual environment")
    print("  2. Install Python dependencies")
    print("  3. Setup C++ tools (bwfmetaedit, allolib, VBAP renderer)")
    print("\nAfter initialization, run the pipeline again.")
    print("="*80 + "\n")
    return False


def run_pipeline(sourceADMFile, sourceSpeakerLayout, createRenderAnalysis=True):
    """
    Run the complete ADM to spatial audio pipeline
    
    Args:
        sourceADMFile: path to source ADM WAV file
        sourceSpeakerLayout: path to speaker layout JSON
        createRenderAnalysis: whether to create render analysis PDF
    """
    # Step 0: Check if project has been initialized
    if not check_initialization():
        return False
    
    # Step 1: Setup C++ tools and dependencies (only runs if needed - idempotent)
    # Note: If you encounter dependency errors, delete .init_complete and re-run ./init.sh
    print("\n" + "="*80)
    print("STEP 1: Verifying C++ tools and dependencies")
    print("="*80)
    if not setupCppTools():
        print("\n✗ Error: C++ tools setup failed")
        print("\nTry re-initializing:")
        print("  rm .init_complete && ./init.sh")
        return False
    
    processedDataDir = "processedData"
    finalOutputRenderFile = "processedData/spatial_render.wav"
    finalOutputRenderAnalysisPDF = "processedData/spatial_render_analysis.pdf"

    # -- Audio channel analysis (still writes containsAudio.json for splitStems) --
    print("\nChecking audio channels for content...")
    exportAudioActivity(sourceADMFile, output_path="processedData/containsAudio.json", threshold_db=-100)
    # Also get the result dict in memory for passing to LUSID
    contains_audio_data = channelHasAudio(sourceADMFile, threshold_db=-100, printChannelUpdate=False)

    # -- Extract ADM XML metadata from WAV --
    print("Extracting ADM metadata from WAV file...")
    extractedMetadata = extractMetaData(sourceADMFile, "processedData/currentMetaData.xml")

    if extractedMetadata:
        xmlPath = extractedMetadata
        print(f"Using extracted XML metadata at {xmlPath}")
    else:
        print("Using default XML metadata file")
        xmlPath = "data/POE-ATMOS-FINAL-metadata.xml"

    # -- Parse ADM metadata into dicts (no intermediate JSON files) --
    print("Parsing ADM metadata...")
    parsed_adm_data = parseMetadata(xmlPath, ToggleExportJSON=False, TogglePrintSummary=True)

    # -- Package for render: dicts flow directly to LUSID scene builder --
    print("\nPackaging audio for render...")
    packageForRender(sourceADMFile, parsed_adm_data, contains_audio_data, processedDataDir)

    print("\nRunning DBAP spatial renderer...")
    # Minimal change: call runSpatialRender with DBAP
    from src.createRender import runSpatialRender
    runSpatialRender(
        source_folder="processedData/stageForRender",
        render_instructions="processedData/stageForRender/scene.lusid.json",
        speaker_layout=sourceSpeakerLayout,
        output_file=finalOutputRenderFile,
        spatializer="dbap"
    )

    if createRenderAnalysis:
        print("\nAnalyzing rendered spatial audio...")
        analyzeRenderOutput(
            render_file=finalOutputRenderFile,
            output_pdf=finalOutputRenderAnalysisPDF
        )

    print("\nDone")


if __name__ == "__main__":
    # CLI mode - parse arguments
    if len(sys.argv) >= 2:
        sourceADMFile = sys.argv[1]
        sourceSpeakerLayout = sys.argv[2] if len(sys.argv) >= 3 else "spatial_engine/speaker_layouts/allosphere_layout.json"
        createRenderAnalysis = True if len(sys.argv) < 4 else sys.argv[3].lower() in ['true', '1', 'yes']
        
        run_pipeline(sourceADMFile, sourceSpeakerLayout, createRenderAnalysis)
    
    else:
        # default mode
        print("Usage: python runPipeline.py <sourceADMFile> [sourceSpeakerLayout] [createAnalysis]")
        print("\nRunning with default configuration...")
        
        sourceADMFile = "sourceData/driveExampleSpruce.wav"
        sourceSpeakerLayout = "spatial_engine/speaker_layouts/allosphere_layout.json"
        createRenderAnalysis = True
        
        run_pipeline(sourceADMFile, sourceSpeakerLayout, createRenderAnalysis)

