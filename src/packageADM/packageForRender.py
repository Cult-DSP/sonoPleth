
from src.packageADM.splitStems import splitChannelsToMono
from LUSID.src.xmlParser import adm_to_lusid_scene, write_lusid_scene

# Updated 2026-02-10: Accepts pre-parsed dicts directly — no intermediate JSON files.
# The LUSID scene (scene.lusid.json) is now the canonical spatial data format
# read directly by the C++ renderer.

def packageForRender(sourceADM, parsed_adm_data, contains_audio_data,
                     processed_dir="processedData",
                     output_dir="processedData/stageForRender"):
    """Package data for rendering by building the LUSID scene and splitting stems.
    
    1. Builds a LUSID scene from pre-parsed ADM dicts (passed in memory)
       → writes scene.lusid.json into the output directory.
    2. Splits the multichannel ADM WAV into per-channel mono files
       using LUSID node ID naming (X.1.wav, LFE.wav).
    
    Args:
        sourceADM (str): Path to the source multichannel ADM WAV file.
        parsed_adm_data (dict): Pre-parsed ADM metadata with keys:
            'objectData', 'globalData', 'directSpeakerData'.
            Returned by src.analyzeADM.parser.parseMetadata().
        contains_audio_data (dict): Per-channel audio activity data.
            Returned by src.analyzeADM.checkAudioChannels.channelHasAudio().
        processed_dir (str): Directory containing processed data (for splitStems).
        output_dir (str): Directory to save packaged data for rendering.
    """
    print("Attempting to run package for render -- building LUSID scene and splitting stems...")
    
    # Build LUSID scene directly from in-memory dicts (no intermediate JSON files)
    scene = adm_to_lusid_scene(
        object_data=parsed_adm_data['objectData'],
        direct_speaker_data=parsed_adm_data['directSpeakerData'],
        global_data=parsed_adm_data['globalData'],
        contains_audio=contains_audio_data,
    )
    
    lusid_output = f"{output_dir}/scene.lusid.json"
    write_lusid_scene(scene, lusid_output)
    
    # Split audio stems using LUSID node ID naming
    splitChannelsToMono(sourceADM, processed_dir=processed_dir, output_dir=output_dir)
    
    print(f"Packaged data for render in {output_dir}")


# if __name__ == "__main__":
#     packageForRender('sourceData/POE-ATMOS-FINAL.wav', processed_dir="processedData", output_dir="processedData/stageForRender")