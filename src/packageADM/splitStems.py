import soundfile as sf
import numpy as np
from pathlib import Path
import json
import os

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


def splitChannelsToMono(source_path, processed_dir="processedData", output_dir="processedData/stageForRender"):
    """
    Split a multichannel audio file into individual mono WAV files.
    Skips empty channels but preserves channel numbering.
    
    Parameters:
    -----------
    source_path : str
        Path to the source multichannel audio file
    processed_dir : str
        Directory containing processed data JSONs (default: "processedData")
    output_dir : str
        Directory to save the mono channel files (default: "processedData/stageForRender")
    
    Returns:
    --------
    tuple
        (total_channels, extracted_channels) - total and number actually written
    """
    # Load processed data and get empty channel mapping
    data = loadContainsAudioData(processed_dir)
    channel_audio_map = mapEmptyChannels(data)
    
    # Convert to absolute path to avoid issues when running from different directories
    outputPath = Path(os.path.abspath("processedData/stageForRender"))
    # SHOULD UPDATE THIS IN THE FUTURE
    
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
    for chanIndex in range(num_channels):
        chanNumber = chanIndex + 1  # 1-indexed channel numbers
        
        # Check if this channel contains audio
        has_audio = channel_audio_map.get(chanIndex, True)  # Default to True if not in map
        
        if not has_audio:
            print(f"  Channel {chanNumber}/{num_channels} -> SKIPPED (empty)")
            skipped_count += 1
            continue

        
        
        chanData = audio_data[:, chanIndex]
        output_file = outputPath / f"src_{chanNumber}.wav"
        
        try:
            if chanNumber == 4:
                sf.write(outputPath/f"LFE.wav", chanData, sample_rate)
                print(f"Wrote LFE data")
                extracted_count += 1
            else:
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