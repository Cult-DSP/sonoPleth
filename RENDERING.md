# sonoPleth Rendering System

This document explains the VBAP (Vector Base Amplitude Panning) rendering system used to spatialize audio for the AlloSphere's 54-speaker array.

## Overview

The rendering pipeline takes:
1. **Mono source audio files** - individual audio stems
2. **Spatial trajectory data** - JSON with position keyframes over time
3. **Speaker layout** - AlloSphere 54-speaker configuration

And produces:
- **54-channel WAV file** - each channel corresponds to one speaker

## Architecture

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│  Source WAVs    │───>│  VBAPRenderer    │───>│ 54-channel WAV  │
│  (mono stems)   │    │  (AlloLib VBAP)  │    │  for AlloSphere │
└─────────────────┘    └────────┬─────────┘    └─────────────────┘
                                │
┌─────────────────┐    ┌────────┴─────────┐
│ Spatial JSON    │───>│ interpolateDir() │
│ (trajectories)  │    │ (direction over  │
└─────────────────┘    │  time)           │
                       └──────────────────┘
┌─────────────────┐
│ Layout JSON     │───> Speaker positions for VBAP triangulation
│ (speaker pos)   │
└─────────────────┘
```

## Key Files

| File | Purpose |
|------|---------|
| `vbapRender/src/main.cpp` | CLI entry point, argument parsing |
| `vbapRender/src/VBAPRenderer.cpp` | Core rendering logic |
| `vbapRender/src/VBAPRenderer.hpp` | Renderer class and config structs |
| `vbapRender/src/JSONLoader.cpp` | Parses spatial trajectory JSON |
| `vbapRender/src/LayoutLoader.cpp` | Parses speaker layout JSON |
| `vbapRender/src/WavUtils.cpp` | WAV I/O utilities |
| `vbapRender/allosphere_layout.json` | AlloSphere 54-speaker positions |

## CLI Usage

```bash
./sonoPleth_vbap_render \
  --layout allosphere_layout.json \
  --positions spatial.json \
  --sources ./mono_stems/ \
  --out output.wav \
  [OPTIONS]
```

### Required Arguments

| Flag | Description |
|------|-------------|
| `--layout FILE` | Speaker layout JSON (typically `allosphere_layout.json`) |
| `--positions FILE` | Spatial trajectory JSON with source keyframes |
| `--sources FOLDER` | Directory containing mono source WAV files |
| `--out FILE` | Output multichannel WAV path |

### Optional Arguments

| Flag | Default | Description |
|------|---------|-------------|
| `--master_gain FLOAT` | 0.25 | Global gain (prevents clipping from VBAP sum) |
| `--solo_source NAME` | (none) | Render only this source (for debugging) |
| `--t0 SECONDS` | 0 | Start rendering at this time |
| `--t1 SECONDS` | (end) | Stop rendering at this time |
| `--debug_dir DIR` | (none) | Output diagnostics to this directory |

### Examples

```bash
# Basic render with default 0.25 gain
./sonoPleth_vbap_render \
  --layout allosphere_layout.json \
  --positions renderInstructions.json \
  --sources ./stems/ \
  --out render.wav

# Debug a single source with diagnostics
./sonoPleth_vbap_render \
  --layout allosphere_layout.json \
  --positions renderInstructions.json \
  --sources ./stems/ \
  --out debug_source1.wav \
  --solo_source "source_1" \
  --debug_dir ./debug_output/

# Render just 10-20 second window at full gain
./sonoPleth_vbap_render \
  --layout allosphere_layout.json \
  --positions renderInstructions.json \
  --sources ./stems/ \
  --out segment.wav \
  --t0 10.0 --t1 20.0 \
  --master_gain 1.0
```

## Spatial Trajectory JSON Format

The `--positions` file defines source trajectories:

```json
{
  "sampleRate": 48000,
  "sources": {
    "source_1": [
      {"time": 0.0, "azimuth": 0.0, "elevation": 0.0, "distance": 1.0},
      {"time": 5.0, "azimuth": 1.57, "elevation": 0.5, "distance": 1.0}
    ],
    "source_2": [
      {"time": 0.0, "azimuth": -0.785, "elevation": 0.0, "distance": 1.0}
    ]
  }
}
```

- **time**: Keyframe time in seconds
- **azimuth**: Horizontal angle in radians (-π to π)
- **elevation**: Vertical angle in radians (-π/2 to π/2)
- **distance**: Normalized distance (typically 1.0)

Positions are linearly interpolated between keyframes.

## Speaker Layout JSON Format

The `--layout` file defines speaker positions:

```json
{
  "speakers": [
    {"id": 1, "azimuth": 0.0, "elevation": 0.0, "distance": 5.0},
    {"id": 2, "azimuth": 0.1163, "elevation": 0.0, "distance": 5.0}
  ]
}
```

**Important**: Angles are in **radians** in the JSON file. The loader converts them to degrees for AlloLib's VBAP implementation.

## Render Configuration

The `RenderConfig` struct controls rendering behavior:

```cpp
struct RenderConfig {
    float masterGain = 0.25f;           // Prevent VBAP summation clipping
    std::string soloSource = "";        // Solo mode: only render this source
    double t0 = -1.0;                   // Start time (-1 = beginning)
    double t1 = -1.0;                   // End time (-1 = end)
    bool debugDiagnostics = false;      // Enable diagnostic output
    std::string debugOutputDir = "";    // Where to write diagnostics
};
```

### Master Gain Rationale

VBAP works by distributing each source's energy across 2-3 speakers. When multiple sources are rendered, their contributions **accumulate**. With many sources pointing at similar directions, this can cause clipping.

The default `masterGain = 0.25f` provides ~12dB of headroom, which is sufficient for most productions. Adjust based on your source count and panning density.

## Render Statistics

After rendering, statistics are computed and logged:

```
Render Statistics:
  Overall peak: 0.847 (-1.4 dBFS)
  Near-silent channels (< -85 dBFS): 0/54
  Clipping channels (peak > 1.0): 0
  Channels with NaN: 0
```

If `--debug_dir` is specified, detailed stats are written to:
- `render_stats.json` - Per-channel RMS and peak levels
- `block_stats.log` - Per-block processing stats (sampled)

## Algorithm Details

### Interpolation (`interpolateDir`)

Direction is computed at each audio block's timestamp by linearly interpolating between spatial keyframes:

1. Find bracketing keyframes k1 (before t) and k2 (after t)
2. Interpolate azimuth, elevation, distance
3. Convert to Cartesian unit vector
4. Validate (NaN check, magnitude check)

**Edge cases handled**:
- No keyframes → default direction (0, 1, 0)
- Single keyframe → use that position
- Time before first keyframe → use first keyframe
- Time after last keyframe → use last keyframe
- Zero-length keyframe interval → snap to k2

### VBAP Rendering

For each audio block (512 samples):

1. Zero the output accumulator
2. For each source:
   a. Fill source buffer with samples (zero-padded beyond source length)
   b. Compute direction vector at block time
   c. Call AlloLib's `mVBAP.renderBuffer()` which:
      - Finds optimal speaker triplet for direction
      - Calculates VBAP gains
      - Mixes source into speaker channels
3. Apply master gain
4. Copy to output buffer

### Time Unit Detection

Some ADM parsers output keyframe times in **samples** instead of **seconds**. The renderer auto-detects this:

```cpp
// If maxTime >> durationSec but matches sample count, convert
if (maxTime > durationSec * 10.0 && maxTime <= totalSamples * 1.1) {
    // Convert from samples to seconds
    for (auto &kf : keyframes) kf.time /= sampleRate;
}
```

## Common Issues

### "Zero output" / Silent channels

**Cause**: AlloLib expects speaker angles in **degrees**, not radians.

**Fix**: Verify `LayoutLoader::loadLayout()` converts radians to degrees:
```cpp
speaker.azimuth = s.azimuth * 180.0f / M_PI;
speaker.elevation = s.elevation * 180.0f / M_PI;
```

### "Robotic" / time-warped distortion

**Cause**: NaN values from interpolation causing VBAP to malfunction.

**Fix**: The updated `interpolateDir()` handles all edge cases and validates output.

### Missing sources / tracks

**Cause**: Source name mismatch between spatial JSON and WAV filenames.

**Fix**: Enable `--debug_dir` and check logs. Source names must match exactly.

### Clicks / discontinuities

**Cause**: Stale buffer data from previous blocks.

**Fix**: `std::fill(sourceBuffer.begin(), sourceBuffer.end(), 0.0f)` before each source.

### Accumulated noise

**Cause**: Using `+=` instead of `=` when copying rendered samples.

**Fix**: Output copy uses assignment: `out.samples[ch][i] = sample`

## Building

```bash
cd vbapRender/build
cmake ..
make
```

The executable is `sonoPleth_vbap_render` in the build directory.

## Integration with Pipeline

The Python pipeline (`runPipeline.py`) calls the renderer via subprocess:

```python
subprocess.run([
    './vbapRender/build/sonoPleth_vbap_render',
    '--layout', 'vbapRender/allosphere_layout.json',
    '--positions', 'processedData/stageForRender/renderInstructions.json',
    '--sources', 'sourceData/',
    '--out', 'output.wav'
])
```

## Debugging Workflow

1. **Solo a single source**:
   ```bash
   --solo_source "problematic_source" --debug_dir ./debug/
   ```

2. **Render a short segment**:
   ```bash
   --t0 10.0 --t1 15.0
   ```

3. **Check render stats**:
   Look for NaN channels, unexpected silence, or clipping.

4. **Full gain for single source**:
   ```bash
   --solo_source "source" --master_gain 1.0
   ```

5. **Compare against original**:
   A single-source render should match the original mono file (with panning applied).
