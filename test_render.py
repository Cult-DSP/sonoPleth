#!/usr/bin/env python3

import sys
import os
sys.path.append('src')

from createRender import runSpatialRender

print("Testing runSpatialRender directly...")

success = runSpatialRender(
    source_folder="processedData/stageForRender",
    render_instructions="processedData/stageForRender/scene.lusid.json",
    speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
    output_file="test_direct.wav",
    spatializer="dbap",
    dbap_focus=1.0
)

print(f"Result: {success}")