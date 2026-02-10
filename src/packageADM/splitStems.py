import soundfile as sf
import numpy as np
from pathlib import Path
import json
import os

# ---------------------------------------------------------------------------
# Developer flag — mirrors LUSID/src/xml_etree_parser.py _DEV_LFE_HARDCODED
# When True: channel 4 is always written as LFE.wav (hardcoded index).
# When False: LFE detection should come from label matching (not yet implemented here).
# Keep in sync with LUSID/src/xml_etree_parser.py.
# ---------------------------------------------------------------------------
_DEV_LFE_HARDCODED = True

def loadContainsAudioData(processed_dir="processedData"):
    data = {}
    channels_contains_audio_path = os.path.join(processed_dir, "containsAudio.json")
    if os.path.exists(channels_contains_audio_path):
        with open(channels_contains_audio_path, 'r') as f:
            data['containsAudio'] = json.load(f)
        print(f"Loaded containsAudio from {channels_contains_audio_path}")
    else:
        data['containsAudio'] = {}
        print(f"Warning: {channels_contains_audio_path} not found")
    
    return data


def mapEmptyChannels(data):
    """Map which channels contain audio based on containsAudio data.
    
    Args:
        data (dict): Loaded processed data containing containsAudio info
    
    Returns:
        dict: Mapping of channel index -> contains_audio (True/False)
    """
    channel_audio_map = {}
    contains_audio_info = data.get('containsAudio', {})
    for channel_info in contains_audio_info.get('channels', []):
        channel_index = channel_info.get('channel_index')
        contains_audio = channel_info.get('contains_audio', False)
        channel_audio_map[channel_index] = contains_audio
    return channel_audio_map


def splitChannelsToMono(source_path, processed_dir="processedData", output_dir="processedData/stageForRender", contains_audio_data=None):
    """
    Split a multichannel audio file into individual mono WAV files.
    Skips empty channels but preserves channel numbering.
    
    Parameters:
    -----------
    source_path : str
        Path to the source multichannel audio file
    processed_dir : str
        Directory containing processed data JSONs (used only if contains_audio_data not provided)
    output_dir : str
        Directory to save the mono channel files (default: "processedData/stageForRender")
    contains_audio_data : dict, optional
        Pre-loaded contains_audio data dict. If None, reads from JSON file.
    """
    # Load processed data and get empty channel mapping
    if contains_audio_data is not None:
        # Use provided in-memory data
        data = {'containsAudio': contains_audio_data}
    else:
        # Fall back to reading from JSON file
        data = loadContainsAudioData(processed_dir)
    
    channel_audio_map = mapEmptyChannels(data)
    
    # Use the provided output_dir parameter, convert to absolute path to avoid issues 
    # when running from different directories
    outputPath = Path(os.path.abspath(output_dir))
    
    # Clear existing WAV files if directory exists
    if outputPath.exists():
        print(f"Clearing existing files in {outputPath}")
        deleted_count = 0
        for file_path in outputPath.glob("*.wav"):
            try:
                file_path.unlink()
                deleted_count += 1
            except Exception as e:
                print(f"  Warning: Could not delete {file_path.name}: {e}")
        print(f"  Deleted {deleted_count} existing WAV files")
    else:
        outputPath.mkdir(parents=True, exist_ok=True)
        print(f"Created directory: {outputPath}")
    
    # Read the audio file
    print(f"\nReading ADM for splitting: {source_path}")
    audio_data, sample_rate = sf.read(source_path)
    
    # Get number of channels
    if audio_data.ndim == 1:
        num_channels = 1
        audio_data = audio_data.reshape(-1, 1)
    else:
        num_channels = audio_data.shape[1]
    
    print(f"Splitting {num_channels} channels at {sample_rate} Hz...")
    print(f"Skipping empty channels based on containsAudio.json\n")
    
    extracted_count = 0
    skipped_count = 0
    
    # Split each channel and save as mono file (only if contains audio)
    # WAV naming convention (LUSID v0.5):
    #   DirectSpeakers + AudioObjects: "{group}.1.wav" where group = 1-based channel number
    #   LFE: "LFE.wav"
    #   The group numbering matches LUSID node IDs: DirectSpeakers get groups 1–N,
    #   AudioObjects get groups N+1+, all in ADM channel order.
    for chanIndex in range(num_channels):
        chanNumber = chanIndex + 1  # 1-indexed channel numbers
        
        # Check if this channel contains audio
        has_audio = channel_audio_map.get(chanIndex, True)  # Default to True if not in map
        
        if not has_audio:
            print(f"  Channel {chanNumber}/{num_channels} -> SKIPPED (empty)")
            skipped_count += 1
            continue

        chanData = audio_data[:, chanIndex]

        # LFE detection
        is_lfe = (_DEV_LFE_HARDCODED and chanNumber == 4)

        try:
            if is_lfe:
                sf.write(outputPath / "LFE.wav", chanData, sample_rate)
                print(f"  Channel {chanNumber}/{num_channels} -> LFE.wav")
                extracted_count += 1
            else:
                # LUSID node ID naming: group = chanNumber, hierarchy = 1
                output_file = outputPath / f"{chanNumber}.1.wav"
                sf.write(output_file, chanData, sample_rate)
                print(f"  Channel {chanNumber}/{num_channels} -> {output_file.name}")
                extracted_count += 1
        except Exception as e:
            print(f"  Channel {chanNumber}/{num_channels} -> ERROR: {e}")
            continue
    
    print(f"\n✓ Extracted {extracted_count}/{num_channels} mono files to {output_dir}")
    print(f"✓ Skipped {skipped_count} empty channels")
    return num_channels, extracted_count


# if __name__ == "__main__":
#     # Example usage
#     source_file = "sourceData/POE-ATMOS-FINAL.wav"
#     total, extracted = splitChannelsToMono(source_file)
#     print(f"\nTotal channels: {total}")
#     print(f"Extracted channels: {extracted}, skipped channels: {total - extracted}")