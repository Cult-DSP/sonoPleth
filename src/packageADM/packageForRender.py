
from src.packageADM.splitStems import splitChannelsToMono
from LUSID.src.xml_etree_parser import write_lusid_scene

# Updated 2026-02-10: Now accepts LusidScene object directly from single-step XML parsing.
# Eliminates intermediate dicts and JSON files entirely.

def packageForRender(sourceADM, lusid_scene, contains_audio_data,
                     processed_dir="processedData",
                     output_dir="processedData/stageForRender"):
    """Package data for rendering by writing the LUSID scene and splitting stems.

    1. Writes the provided LusidScene to scene.lusid.json
       → the canonical spatial data format read directly by the C++ renderer.
    2. Splits the multichannel ADM WAV into per-channel mono files
       using LUSID node ID naming (X.1.wav, LFE.wav).

    Args:
        sourceADM (str): Path to the source multichannel ADM WAV file.
        lusid_scene (LusidScene): Pre-built LUSID scene object.
            Created by LUSID.src.xml_etree_parser.parse_adm_xml_to_lusid_scene().
        contains_audio_data (dict): Per-channel audio activity data.
            Returned by src.analyzeADM.checkAudioChannels.channelHasAudio().
        processed_dir (str): Directory containing processed data (for splitStems).
        output_dir (str): Directory to save packaged data for rendering.
    """
    print("Attempting to run package for render -- writing LUSID scene and splitting stems...")

    # Write LUSID scene directly (no intermediate processing needed)
    lusid_output = f"{output_dir}/scene.lusid.json"
    write_lusid_scene(lusid_scene, lusid_output)
    
    # Split audio stems using LUSID node ID naming
    splitChannelsToMono(sourceADM, processed_dir=processed_dir, output_dir=output_dir, contains_audio_data=contains_audio_data)
    
    print(f"Packaged data for render in {output_dir}")


# if __name__ == "__main__":
#     packageForRender('sourceData/POE-ATMOS-FINAL.wav', processed_dir="processedData", output_dir="processedData/stageForRender")