from src.configCPP import setupCppTools
from src.analyzeADM.extractMetadata import extractMetaData
from src.analyzeADM.checkAudioChannels import channelHasAudio, exportAudioActivity
from src.packageADM.packageForRender import packageForRender
from src.createRender import runVBAPRender
from src.analyzeRender import analyzeRenderOutput
from createFromLUSID import run_pipeline_from_LUSID
from pathlib import Path
import subprocess
import sys
import os



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



def run_pipeline_from_ADM(sourceADMFile, sourceSpeakerLayout, renderMode="dbap", resolution=1.5, createRenderAnalysis=True, master_gain=0.5):
    """
    Run the complete ADM to spatial audio pipeline
    
    Args:
        sourceADMFile: path to source ADM WAV file
        sourceSpeakerLayout: path to speaker layout JSON
        renderMode: spatializer type (default: "dbap")
        resolution: spatializer-specific parameter (e.g., dbap_focus or lbap_dispersion)
        createRenderAnalysis: whether to create render analysis PDF
        master_gain: master gain for the renderer (default: 0.5)
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

    # -- Parse ADM XML directly to LUSID scene (single-step, no intermediate JSONs) --
    print("Parsing ADM metadata to LUSID scene...")
    from LUSID.src.xml_etree_parser import parse_adm_xml_to_lusid_scene
    lusid_scene = parse_adm_xml_to_lusid_scene(xmlPath, contains_audio=contains_audio_data)
    lusid_scene.summary()

    # -- Package for render: LUSID scene flows directly to stem splitter --
    print("\nPackaging audio for render...")
    packageForRender(sourceADMFile, lusid_scene, contains_audio_data, processedDataDir)

    print(f"\nRunning {renderMode.upper()} spatial renderer...")
    # Call runSpatialRender with appropriate parameters based on renderMode
    from src.createRender import runSpatialRender
    spatializer = renderMode
    extra_kwargs = {}
    if renderMode == 'dbap':  # Include default "dbap" mode
        extra_kwargs['dbap_focus'] = resolution
    elif renderMode == "lbap":
        extra_kwargs['lbap_dispersion'] = resolution
    runSpatialRender(
        source_folder="processedData/stageForRender",
        render_instructions="processedData/stageForRender/scene.lusid.json",
        speaker_layout=sourceSpeakerLayout,
        output_file=finalOutputRenderFile,
        spatializer=spatializer,
        master_gain=master_gain,  # Pass master_gain to runSpatialRender
        **extra_kwargs
    )

    if createRenderAnalysis:
        print("\nAnalyzing rendered spatial audio...")
        analyzeRenderOutput(
            render_file=finalOutputRenderFile,
            output_pdf=finalOutputRenderAnalysisPDF
        )

    print("\nDone")

    
def checkSourceType(arg):
    if not os.path.exists(arg):
        return "Path does not exist"
    
    if os.path.isfile(arg):
        if arg.lower().endswith('.wav'):
            return "ADM"
    
    if os.path.isdir(arg):
        if os.path.basename(arg) == "lusid_package":
            return "LUSID"
    
    return "Wrong Input Type"


if __name__ == "__main__":
    # CLI mode - parse arguments
    
    sourceType = checkSourceType(sys.argv[1])
    if len(sys.argv) >= 2:
        sourceADMFile = sys.argv[1]
        sourceSpeakerLayout = sys.argv[2] if len(sys.argv) >= 3 else "spatial_engine/speaker_layouts/allosphere_layout.json"
        renderMode = sys.argv[3] if len(sys.argv) >= 4 else "dbap"
        resolution = float(sys.argv[4]) if len(sys.argv) >= 5 else 1.5
        master_gain = float(sys.argv[5]) if len(sys.argv) >= 6 else 0.5  # Added master_gain argument
        createRenderAnalysis = True if len(sys.argv) < 7 else sys.argv[6].lower() in ['true', '1', 'yes']

        if sourceType == "ADM":
            print("Running pipeline from ADM source...")
            run_pipeline_from_ADM(sourceADMFile, sourceSpeakerLayout, renderMode, resolution, createRenderAnalysis, master_gain)
        elif sourceType == "LUSID":
            print("Running pipeline from LUSID source...")
            run_pipeline_from_LUSID(sourceADMFile, sourceSpeakerLayout, renderMode, createRenderAnalysis)
    
    else:
        # default mode
        print("Usage: python runPipeline.py <sourceADMFile> [sourceSpeakerLayout] [renderMode] [resolution] [master_gain] [createAnalysis]")
        print("\nRunning with default configuration...")

        sourceADMFile = "sourceData/driveExampleSpruce.wav"
        sourceSpeakerLayout = "spatial_engine/speaker_layouts/allosphere_layout.json"
        master_gain = 0.5
        createRenderAnalysis = True

        run_pipeline_from_ADM(sourceADMFile, sourceSpeakerLayout, "dbap", 1.5, createRenderAnalysis, master_gain)

