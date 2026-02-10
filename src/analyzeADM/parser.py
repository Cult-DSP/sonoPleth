import json
from lxml import etree
import os

# heavy usage of claude sonnet (in copilot) for dealing with ebu formatting 

'''on EBU:
Namespace: Uses "urn:ebu:metadata-schema:ebuCore_2016" - the 2016 version of EBU's metadata schema

The parser looks for audioChannelFormat elements with typeDefinition="Objects" - these represent individual audio stems or elements that can move in 3D spac

- Position Blocks: Each object has multiple audioBlockFormat elements that define its position over time:

- rtime: Relative start time (e.g., "00:03:09.59419")
duration: How long this position lasts
position elements with coordinates X, Y, Z

- Spatial Metadata: Additional properties like:

- cartesian: Whether coordinates are Cartesian (1) or spherical (0)
width, depth, height: Object size/spread in space
diffuse: How spread out the sound is

'''

def extractObjectPositions(xml_path):
    """extract positions from XML ADM metadata in EBU format.

        returns dict mapping object names to lists of position blocks
        Each  block is a dict with: 'rtime', 'duration', 'x', 'y', 'z'
    """
    # Updated namespace for ebuCore_2016
    ns = {"ebu": "urn:ebu:metadata-schema:ebuCore_2016"}
    tree = etree.parse(xml_path)
    
    objects = {}
    
    # Find all audioChannelFormat elements with typeDefinition="Objects"
    for channel in tree.xpath("//ebu:audioChannelFormat[@typeDefinition='Objects']", namespaces=ns):
        name = channel.attrib.get("audioChannelFormatName", "Unnamed")
        
        # Get all audioBlockFormat elements for this channel
        blocks = []
        for block in channel.xpath(".//ebu:audioBlockFormat", namespaces=ns):
            # Extract time information
            rtime = block.attrib.get("rtime", "00:00:00.00000")
            duration = block.attrib.get("duration", "00:00:00.00000")
            
            # Extract position coordinates
            position_data = {
                'rtime': rtime,
                'duration': duration,
                'x': 0.0,
                'y': 0.0,
                'z': 0.0
            }
            
            # Get position elements
            for pos in block.xpath(".//ebu:position", namespaces=ns):
                coord = pos.attrib.get("coordinate", "")
                value = float(pos.text) if pos.text else 0.0
                
                if coord == "X":
                    position_data['x'] = value
                elif coord == "Y":
                    position_data['y'] = value
                elif coord == "Z":
                    position_data['z'] = value
            
            # Extract additional metadata if present
            # Get channel ID from the parent channel element. not sure how i should format this yet 
            channel_id = channel.attrib.get("audioChannelFormatID", "")
            if channel_id:
                position_data['channelID'] = channel_id
                
            cartesian = block.find(".//ebu:cartesian", namespaces=ns)
            if cartesian is not None:
                position_data['cartesian'] = int(cartesian.text) if cartesian.text else 1
            
            width = block.find(".//ebu:width", namespaces=ns)
            if width is not None:
                position_data['width'] = float(width.text) if width.text else None
            depth = block.find(".//ebu:depth", namespaces=ns)
            if depth is not None:
                position_data['depth'] = float(depth.text) if depth.text else None
                
            height = block.find(".//ebu:height", namespaces=ns)
            if height is not None:
                position_data['height'] = float(height.text) if height.text else None
            
            blocks.append(position_data)
        
        if blocks:
            objects[name] = blocks
    
    return objects

def saveObjectData(objectsDict, outputPath="processedData/objectData.json"):
    os.makedirs(os.path.dirname(outputPath), exist_ok=True)

    with open(outputPath, 'w') as f:
        json.dump(objectsDict, f, indent=2)

    print(f"Saved object data to {outputPath}")

def loadObjectData(inputPath="processedData/objectData.json"):
    if not os.path.exists(inputPath):
        raise FileNotFoundError(f"No object data file found at {inputPath}")

    with open(inputPath, 'r') as f:
        objectsDict = json.load(f)

    print(f"Loaded object data from {inputPath}")
    return objectsDict



def parseTimecodeToSeconds(timecode):
    """Convert timecode string (HH:MM:SS.SSSSS) to seconds."""
    hours, minutes, seconds = timecode.split(':')
    return int(hours) * 3600 + int(minutes) * 60 + float(seconds)


def getPositionAtTime(blocks, time_seconds):
    """
    returns dict of Position data at the specified time, or None if not found
    """
    for block in blocks:
        start_time = parseTimecodeToSeconds(block['rtime'])
        duration = parseTimecodeToSeconds(block['duration'])
        end_time = start_time + duration
        
        if start_time <= time_seconds < end_time:
            return block
    
    return None

def getGlobalData(xmlPath, outputPath="processedData/globalData.json"):
    """Extract all fields from the XML file's <Technical> section.
    
    If outputPath is None, skip writing to disk and just return the dict.
    """
    tree = etree.parse(xmlPath)
    technicalData = tree.find(".//Technical")

    if technicalData is None:
        raise ValueError(f"No <Technical> section found in {xmlPath}")
    
    global_data = {}   
    for elem in technicalData:
        tag = elem.tag.strip()
        text = elem.text.strip() if elem.text else ""
        global_data[tag] = text
    
    if outputPath is not None:
        os.makedirs(os.path.dirname(outputPath), exist_ok=True)
        with open(outputPath, 'w') as f:
            json.dump(global_data, f, indent=2)
        print(f"Saved technical metadata to {outputPath}")

    return global_data

def getDirectSpeakerData(xmlPath, outputPath="processedData/directSpeakerData.json"):
    """Extract all DirectSpeaker channel data from the XML file.
    
    If outputPath is None, skip writing to disk and just return the dict.
    """
    ns = {"ebu": "urn:ebu:metadata-schema:ebuCore_2016"}
    tree = etree.parse(xmlPath)
    
    direct_speakers = {}
    
    # Find all audioChannelFormat elements with typeDefinition="DirectSpeakers"
    for channel in tree.xpath("//ebu:audioChannelFormat[@typeDefinition='DirectSpeakers']", namespaces=ns):
        channel_name = channel.attrib.get("audioChannelFormatName", "Unnamed")
        channel_id = channel.attrib.get("audioChannelFormatID", "")
        
        # Get the audioBlockFormat element for this channel
        block = channel.find(".//ebu:audioBlockFormat", namespaces=ns)
        if block is None:
            continue
            
        speaker_data = {
            'channelID': channel_id,
            'channelName': channel_name,
            'blockID': block.attrib.get("audioBlockFormatID", ""),
            'x': 0.0,
            'y': 0.0,
            'z': 0.0,
            'speakerLabel': '',
            'cartesian': 1 # 1 = cartesian, 0 = spherical
        }
        
        # Get speaker label
        speaker_label = block.find(".//ebu:speakerLabel", namespaces=ns)
        if speaker_label is not None and speaker_label.text:
            speaker_data['speakerLabel'] = speaker_label.text.strip()
        
        # Get cartesian flag
        cartesian = block.find(".//ebu:cartesian", namespaces=ns)
        if cartesian is not None and cartesian.text:
            speaker_data['cartesian'] = int(cartesian.text)
        
        # Get position coordinates
        for pos in block.xpath(".//ebu:position", namespaces=ns):
            coord = pos.attrib.get("coordinate", "")
            value = float(pos.text) if pos.text else 0.0
            
            if coord == "X":
                speaker_data['x'] = value
            elif coord == "Y":
                speaker_data['y'] = value
            elif coord == "Z":
                speaker_data['z'] = value
        
        direct_speakers[channel_name] = speaker_data
    
    if not direct_speakers:
        raise ValueError(f"No DirectSpeaker channels found in {xmlPath}")
    
    if outputPath is not None:
        os.makedirs(os.path.dirname(outputPath), exist_ok=True)
        with open(outputPath, 'w') as f:
            json.dump(direct_speakers, f, indent=2)
        print(f"Saved DirectSpeaker data to {outputPath}")

    return direct_speakers


def parseMetadata(xmlPath, ToggleExportJSON = True, TogglePrintSummary = True):
    """CALLS OTHER FUNCTIONS - parses metadata from XML file, optionally exports to JSON and prints summary.

    Returns
    -------
    dict
        A dict with keys: 'objectData', 'globalData', 'directSpeakerData'.
        All values are the parsed Python dicts (no disk I/O required by callers).

    NOTE (2026-02-10): Intermediate JSON writing is now skipped by default.
    The returned dicts are passed directly to adm_to_lusid_scene() in memory.
    TODO: Create a debug/print summary function that works from the LUSID scene
    object instead of reading from disk.
    """
    objectsDict = extractObjectPositions(xmlPath)
    print("Extracted object position data")

    globalDict = getGlobalData(xmlPath, outputPath=None)
    print("Extracted global technical metadata")

    directSpeakerDict = getDirectSpeakerData(xmlPath, outputPath=None)
    print("Extracted DirectSpeaker channel metadata")

    if ToggleExportJSON:
        saveObjectData(objectsDict, outputPath="processedData/objectData.json")

    if TogglePrintSummary:
        # TODO: Replace with LUSID scene-based summary function.
        # For now, only print if the JSON was exported (needs file on disk).
        if ToggleExportJSON:
            from src.analyzeADM.analyzeMetadata import printSummary
            printSummary(objectDataPath="processedData/objectData.json", togglePositionChanges=False)
        else:
            print(f"  Parsed {len(objectsDict)} audio objects, "
                  f"{len(directSpeakerDict)} direct speakers")

    return {
        'objectData': objectsDict,
        'globalData': globalDict,
        'directSpeakerData': directSpeakerDict,
    }