import json
import os
import csv
from pathlib import Path


# this file is for creating a json in stageForRender that contains spatial instructions for VBAP / DBAP rendering

# to do - add flag for static / dynamic objects - static objects only need one position, dynamic need time series

def loadProcessedData(processed_dir="processedData"):
    """Load all JSON files from processedData directory.
    
    Returns:
        dict: with, 'directSpeakerData', 'objectData', 'globalData'
    """
    data = {}

    # Load direct speaker data
    speaker_path = os.path.join(processed_dir, "directSpeakerData.json")
    if os.path.exists(speaker_path):
        with open(speaker_path, 'r') as f:
            data['directSpeakerData'] = json.load(f)
        print(f"Loaded directSpeakerData from {speaker_path}")
    else:
        data['directSpeakerData'] = {}
        print(f"Warning: {speaker_path} not found")
    
    # Load object data
    object_path = os.path.join(processed_dir, "objectData.json")
    if os.path.exists(object_path):
        with open(object_path, 'r') as f:
            data['objectData'] = json.load(f)
        print(f"Loaded objectData from {object_path}")
    else:
        data['objectData'] = {}
        print(f"Warning: {object_path} not found")

    channels_contains_audio_path = os.path.join(processed_dir, "containsAudio.json")
    if os.path.exists(channels_contains_audio_path):
        with open(channels_contains_audio_path, 'r') as f:
            data['containsAudio'] = json.load(f)
        print(f"Loaded containsAudio from {channels_contains_audio_path}")
    else:
        data['containsAudio'] = {}
        print(f"Warning: {channels_contains_audio_path} not found")

    # Load global data for sample rate
    global_path = os.path.join(processed_dir, "globalData.json")
    if os.path.exists(global_path):
        with open(global_path, 'r') as f:
            data['globalData'] = json.load(f)
        print(f"Loaded globalData from {global_path}")
    else:
        data['globalData'] = {}
        print(f"Warning: {global_path} not found")

    
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



def assignChannels(data):
    """Assign channel numbers to DirectSpeakers and objects.
    
    DirectSpeakers get channels 1-10 (in order of appearance)
    Objects get channels 11+ (in order of appearance in objectData)
    
    Args:
        data (dict): Loaded processed data containing directSpeakerData and objectData
    
    Returns:
        tuple: (channel_mapping dict, audio_status dict)
            - channel_mapping: name -> channel number
            - audio_status: name -> has_audio (True/False)
    """
    channel_mapping = {}
    audio_status = {}
    channel_counter = 1
    
    # Get audio status map (0-indexed)
    channel_audio_map = mapEmptyChannels(data)
    
    # Assign channels 1-10 to DirectSpeakers (in order)
    for speaker_name in data.get('directSpeakerData', {}).keys():  
        channel_mapping[speaker_name] = channel_counter
        # Check if this channel (0-indexed in audio map) contains audio
        audio_status[speaker_name] = channel_audio_map.get(channel_counter - 1, False)
        channel_counter += 1
    
    # Assign channels 11+ to objects (in order, skip empty objects)
    for obj_name, blocks in data.get('objectData', {}).items():
        if blocks:  # Only assign channel if object has data
            channel_mapping[obj_name] = channel_counter
            # Check if this channel (0-indexed in audio map) contains audio
            audio_status[obj_name] = channel_audio_map.get(channel_counter - 1, False)
            channel_counter += 1
    
    print(f"\nChannel assignments:")
    print(f"  DirectSpeakers: channels 1-{len(data.get('directSpeakerData', {}))}")
    print(f"  Objects: channels {len(data.get('directSpeakerData', {})) + 1}-{channel_counter - 1}")
    print(f"  Total channels assigned: {len(channel_mapping)}")
    
    return channel_mapping, audio_status


def parseTimecodeToSeconds(timecode):
    """Convert timecode string (HH:MM:SS.SSSSS) to seconds."""
    try:
        hours, minutes, seconds = timecode.split(':')
        return int(hours) * 3600 + int(minutes) * 60 + float(seconds)
    except:
        return 0.0
    

def deleteRenderInstructionsJSON(output_path):
    """
    Delete the renderInstructions.json file if it exists.
    """
    file_path = Path(output_path).resolve()
    if file_path.exists():
        try:
            file_path.unlink()
            print(f"Deleted: {file_path}")
        except Exception as e:
            print(f"Warning: Could not delete {file_path}: {e}")
    else:
        print(f"No file to delete at: {file_path}")

    


def createRenderInfoJSON(processed_dir="processedData", output_path="processedData/stageForRender/renderInstructions.json"):
    """Create spatial instructions JSON with timestamped position data.
    
    JSON format:
    {
      "sources": {
        "channel_name": [
          { "time": 0.0, "cart": [x, y, z] },
          { "time": 2.0, "cart": [x, y, z] }
        ]
      }
    }
    
    Only includes channels that contain audio (skips empty channels).
    
    Args:
        processed_dir (str): Directory containing processed JSON files
        output_path (str): Where to save the JSON file
    
    Returns:
        int: Number of sources written
    """

    #delete existing render instructions if necessary 
    deleteRenderInstructionsJSON(output_path)
    # Load all processed data
    data = loadProcessedData(processed_dir)
    
    # Assign channel numbers and get audio status
    channel_mapping, audio_status = assignChannels(data)
    
    sources = {}
    sources_with_audio = 0
    sources_without_audio = 0

    #Process direct speaker and single out LFE:
    for idx, (speaker_name, speaker_info) in enumerate(data.get('directSpeakerData', {}).items(), start=1):
        if idx == 4:
            # Output as "LFE" with time 0.0 and no cart data
            sources["LFE"] = [
                {
                    "time": 0.0
                    # No "cart" field
                }
            ]
            sources_with_audio += 1
            continue  # Skip further processing for LFE
        if not audio_status.get(speaker_name, False):
            sources_without_audio += 1
            continue
        channel_num = channel_mapping[speaker_name]
        sources[f"src_{channel_num}"] = [
            {
                "time": 0.0,
                "cart": [
                    speaker_info.get('x', 0.0),
                    speaker_info.get('y', 0.0),
                    speaker_info.get('z', 0.0)
                ]
            }
        ]
    sources_with_audio += 1

    # Process audio objects
    for obj_name, blocks in data.get('objectData', {}).items():
        if not blocks:
            continue
        
        # Skip if channel has no audio
        if not audio_status.get(obj_name, False):
            sources_without_audio += 1
            continue
        
        channel_num = channel_mapping[obj_name]
        position_list = []
        
        # Add all position blocks with timestamps
        for block in blocks:
            time_seconds = parseTimecodeToSeconds(block.get('rtime', '00:00:00.00000'))
            position_list.append({
                "time":  #round(time_seconds, 4)
                time_seconds,
                "cart": [
                    block.get('x', 0.0),
                    block.get('y', 0.0),
                    block.get('z', 0.0)
                ]
            })
        
        # Sort by time
        position_list.sort(key=lambda p: p['time'])
        sources[f"src_{channel_num}"] = position_list
        sources_with_audio += 1
    
    # Create output structure
    output_data = {
        "sampleRate": int(data.get('globalData', {}).get('SampleRate', 48000)),
        "sources": sources
    }
    
    # Write to JSON
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    
    with open(output_path, 'w') as jsonfile:
        json.dump(output_data, jsonfile, indent=2)
    
    print(f"\nSpatial instructions JSON saved to {output_path}")
    print(f"  Sources with audio: {sources_with_audio}")
    print(f"  Sources without audio (skipped): {sources_without_audio}")
    print(f"  Total sources in JSON: {len(sources)}")
    
    return len(sources)


# if __name__ == "__main__":
#     # Test the function
#     source_count = createRenderInfoJSON()
#     print(f"\nJSON export complete with {source_count} sources")
