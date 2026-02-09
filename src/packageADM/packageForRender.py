
from src.packageADM.splitStems import splitChannelsToMono
from LUSID.src.xmlParser import load_processed_data_and_build_scene

# Updated 2026-02-09: Uses LUSID xmlParser instead of old createRenderInfo.
# The LUSID scene (scene.lusid.json) is now the canonical spatial data format
# read directly by the C++ renderer.

def packageForRender(sourceADM, processed_dir="processedData", output_dir="processedData/stageForRender"):
    """Package data for rendering by splitting stems and building the LUSID scene.
    
    1. Builds a LUSID scene from the intermediate ADM data JSONs
       (objectData.json, directSpeakerData.json, globalData.json, containsAudio.json)
       → writes scene.lusid.json into the output directory.
    2. Splits the multichannel ADM WAV into per-channel mono files
       using LUSID node ID naming (X.1.wav, LFE.wav).
    
    Args:
        sourceADM (str): Path to the source multichannel ADM WAV file.
        processed_dir (str): Directory containing intermediate processed data JSONs.
        output_dir (str): Directory to save packaged data for rendering.
    """
    print("Attempting to run package for render -- building LUSID scene and splitting stems...")
    
    # Build LUSID scene from processed data
    lusid_output = f"{output_dir}/scene.lusid.json"
    load_processed_data_and_build_scene(
        processed_dir=processed_dir,
        output_path=lusid_output,
    )
    
    # Split audio stems using LUSID node ID naming
    splitChannelsToMono(sourceADM, processed_dir=processed_dir, output_dir=output_dir)
    
    print(f"Packaged data for render in {output_dir}")


# if __name__ == "__main__":
#     packageForRender('sourceData/POE-ATMOS-FINAL.wav', processed_dir="processedData", output_dir="processedData/stageForRender")