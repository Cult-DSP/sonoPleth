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

| File                                           | Purpose                           |
| ---------------------------------------------- | --------------------------------- |
| `spatial_engine/src/main.cpp`                  | CLI entry point, argument parsing |
| `spatial_engine/src/vbap_src/VBAPRenderer.cpp` | Core rendering logic              |
| `spatial_engine/src/vbap_src/VBAPRenderer.hpp` | Renderer class and config structs |
| `spatial_engine/src/JSONLoader.cpp`            | Parses spatial trajectory JSON    |
| `spatial_engine/src/LayoutLoader.cpp`          | Parses speaker layout JSON        |
| `spatial_engine/src/WavUtils.cpp`              | WAV I/O utilities                 |
| `spatial_engine/speaker_layouts/*.json`        | Speaker layout configurations     |
| `spatial_engine/vbapRender/CMakeLists.txt`     | CMake build configuration         |

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

| Flag               | Description                                              |
| ------------------ | -------------------------------------------------------- |
| `--layout FILE`    | Speaker layout JSON (typically `allosphere_layout.json`) |
| `--positions FILE` | Spatial trajectory JSON with source keyframes            |
| `--sources FOLDER` | Directory containing mono source WAV files               |
| `--out FILE`       | Output multichannel WAV path                             |

### Optional Arguments

| Flag                       | Default   | Description                                        |
| -------------------------- | --------- | -------------------------------------------------- |
| `--master_gain FLOAT`      | 0.5       | Global gain (prevents clipping from VBAP sum)      |
| `--solo_source NAME`       | (none)    | Render only this source (for debugging)            |
| `--t0 SECONDS`             | 0         | Start rendering at this time                       |
| `--t1 SECONDS`             | (end)     | Stop rendering at this time                        |
| `--render_resolution MODE` | block     | Render mode: `block` (recommended) or `sample`     |
| `--block_size N`           | 64        | Block size for direction updates (32-256)          |
| `--debug_dir DIR`          | (none)    | Output diagnostics to this directory               |

### Render Resolution Modes

| Mode     | Description                                                  |
| -------- | ------------------------------------------------------------ |
| `block`  | **Recommended.** Direction computed at block center. Use small block size (32-64) for smooth motion. |
| `sample` | Direction computed per sample. Very slow, use for debugging only. |
| `smooth` | **DEPRECATED.** May cause artifacts. Use `block` instead.    |

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
  "timeUnit": "seconds",
  "sources": {
    "source_1": [
      { "time": 0.0, "cart": [0.0, 1.0, 0.0] },
      { "time": 5.0, "cart": [1.0, 0.0, 0.0] }
    ],
    "source_2": [{ "time": 0.0, "cart": [-0.707, 0.707, 0.0] }]
  }
}
```

- **sampleRate**: Sample rate in Hz (must match audio files)
- **timeUnit**: Time unit for keyframes: `"seconds"` (default), `"samples"`, or `"milliseconds"`
- **time**: Keyframe timestamp (in units specified by `timeUnit`)
- **cart**: Cartesian direction vector [x, y, z] (will be normalized)

**See `json_schema_info.md` for complete schema documentation.**

### Time Units

Always specify `timeUnit` explicitly to avoid ambiguity:

| Value          | Description                                      |
| -------------- | ------------------------------------------------ |
| `"seconds"`    | Default. Times are in seconds (e.g., `1.5`)      |
| `"samples"`    | Times are sample indices (e.g., `48000` = 1 sec) |
| `"milliseconds"` | Times are in ms (e.g., `1500` = 1.5 sec)       |

Positions are interpolated using **spherical linear interpolation (SLERP)** between keyframes, which prevents artifacts when directions are far apart on the sphere.

### Keyframe Sanitation

The loader automatically sanitizes keyframes:

1. **Sorting**: Keyframes are sorted by time (ascending)
2. **Deduplication**: Multiple keyframes at same time are collapsed (keeps last)
3. **Validation**: NaN/Inf values cause keyframe to be dropped
4. **Zero direction fix**: `[0,0,0]` vectors are replaced with front `[0,1,0]`

Warnings are printed for any sanitation actions.

## Speaker Layout JSON Format

The `--layout` file defines speaker positions:

```json
{
  "speakers": [
    { "id": 1, "azimuth": 0.0, "elevation": 0.0, "distance": 5.0 },
    { "id": 2, "azimuth": 0.1163, "elevation": 0.0, "distance": 5.0 }
  ]
}
```

**Important**: Angles are in **radians** in the JSON file. The loader converts them to degrees for AlloLib's VBAP implementation.

## Render Configuration

The `RenderConfig` struct controls rendering behavior:

```cpp
struct RenderConfig {
    float masterGain = 0.5f;            // Prevent VBAP summation clipping
    std::string soloSource = "";        // Solo mode: only render this source
    double t0 = -1.0;                   // Start time (-1 = beginning)
    double t1 = -1.0;                   // End time (-1 = end)
    bool debugDiagnostics = false;      // Enable diagnostic output
    std::string debugOutputDir = "";    // Where to write diagnostics
    std::string renderResolution = "block";  // "block" (recommended) or "sample"
    int blockSize = 64;                 // Direction update interval (samples)
};
```

### Master Gain Rationale

VBAP works by distributing each source's energy across 2-3 speakers. When multiple sources are rendered, their contributions **accumulate**. With many sources pointing at similar directions, this can cause clipping.

The default `masterGain = 0.5f` provides ~6dB of headroom. Adjust based on your source count and panning density.

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

### Interpolation (`interpolateDirRaw` with SLERP)

Direction is computed at each audio block's **center** timestamp using **spherical linear interpolation (SLERP)**:

1. Find bracketing keyframes k1 (before t) and k2 (after t)
2. Normalize both keyframe directions to unit vectors
3. Apply SLERP interpolation on the unit sphere
4. Validate result (NaN check, magnitude check)

**Why SLERP instead of linear interpolation?**

Linear Cartesian interpolation can create near-zero vectors when keyframes are far apart on the sphere (the interpolated vector passes through the origin). SLERP stays on the unit sphere surface, preventing degenerate directions.

**Edge cases handled**:

- No keyframes → fallback direction from nearest keyframe
- Single keyframe → use that position (normalized)
- Time before first keyframe → use first keyframe
- Time after last keyframe → use last keyframe
- Zero-length keyframe interval → snap to k2

### Safe Direction Fallback (`safeDirForSource`)

If interpolation produces an invalid direction:

1. Try **last-good direction** for this source
2. If no last-good, use **nearest keyframe direction** (by time)
3. As absolute last resort, use front direction (0, 1, 0)

Warnings are rate-limited (once per source) with reason logged (NaN vs near-zero magnitude).

### VBAP Rendering

For each audio block (default 64 samples):

1. Zero the output accumulator
2. For each source:
   a. Fill source buffer with samples (zero-padded beyond source length)
   b. Compute direction vector at **block center** time (reduces edge bias)
   c. Call AlloLib's `mVBAP.renderBuffer()` which:
   - Finds optimal speaker triplet for direction
   - Calculates VBAP gains
   - Mixes source into speaker channels
3. Apply master gain
4. Copy to output buffer

### Time Unit Handling

**Primary method**: Explicit `timeUnit` field in JSON (see `json_schema_info.md`).

**Legacy fallback**: If `timeUnit` is not specified, heuristic detection is attempted:

```cpp
// If maxTime >> durationSec but matches sample count, likely samples not seconds
if (maxTime > durationSec * 10.0 && maxTime <= totalSamples * 1.1) {
    // Auto-convert with WARNING
}
```

Always specify `timeUnit` in your JSON to avoid warnings and potential misdetection.

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

**Fix**: The updated direction system handles all edge cases:

- `safeDirForSource()` validates all directions before use
- Invalid directions fall back to last-good or default (0,1,0)
- Warnings are rate-limited (once per source, not per block)

### Missing sources / tracks

**Cause**: Source name mismatch between spatial JSON and WAV filenames.

**Fix**: Enable `--debug_dir` and check logs. Source names must match exactly.

### Clicks / discontinuities

**Cause**: Stale buffer data from previous blocks.

**Fix**: `std::fill(sourceBuffer.begin(), sourceBuffer.end(), 0.0f)` before each source.

### Accumulated noise

**Cause**: Using `+=` instead of `=` when copying rendered samples.

**Fix**: Output copy uses assignment: `out.samples[ch][i] = sample`

### Warning spam about degenerate directions

**Cause**: Previous versions warned on every block with bad direction data.

**Fix**: New `safeDirForSource()` system:

- Warns only **once per source** (not per block)
- Uses last-good direction if available
- Prints fallback summary at end of render showing affected sources

### Zero-length direction vectors

**Cause**: Keyframes with `cart: [0, 0, 0]` (often from distance=0 being applied to coordinates).

**Fix**: JSONLoader now detects zero directions at load time and replaces with front (0,1,0).

## Building

```bash
cd spatial_engine/vbapRender/build
cmake ..
make
```

The executable is `sonoPleth_vbap_render` in the build directory.

Or use the Python setup:

```python
from src.configCPP import setupCppTools
setupCppTools()
```

## Integration with Pipeline

The Python pipeline (`runPipeline.py`) calls the renderer via subprocess:

```python
subprocess.run([
    './spatial_engine/vbapRender/build/sonoPleth_vbap_render',
    '--layout', 'spatial_engine/speaker_layouts/allosphere_layout.json',
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
