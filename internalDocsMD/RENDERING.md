# sonoPleth Rendering System

This document explains the spatial rendering system used to spatialize audio for the AlloSphere's speaker array.

## Overview

The rendering pipeline supports three spatializers:

- **DBAP** (Distance-Based Amplitude Panning) - default, works with any layout
- **VBAP** (Vector Base Amplitude Panning) - best for layouts with good 3D coverage
- **LBAP** (Layer-Based Amplitude Panning) - designed for multi-ring layouts

The pipeline takes:

1. **Mono source audio files** - individual audio stems
2. **Spatial trajectory data** - JSON with position keyframes over time
3. **Speaker layout** - Speaker configuration (e.g., AlloSphere 54-speaker)

And produces:

- **N-channel WAV file** - each channel corresponds to one speaker

## Architecture

```
┌─────────────────┐    ┌───────────────────┐    ┌─────────────────┐
│  Source WAVs    │───>│  SpatialRenderer  │───>│ N-channel WAV   │
│  (mono stems)   │    │  (DBAP/VBAP/LBAP) │    │  for speakers   │
└─────────────────┘    └────────┬──────────┘    └─────────────────┘
                                │
┌─────────────────┐    ┌────────┴─────────┐
│ Spatial JSON    │───>│ interpolateDir() │
                                ## LFE

                                See the full section above: "LFE (Low-Frequency Effects) Handling" for detailed documentation on LFE routing, subwoofer buffer sizing, and spatializer support.
│ (trajectories)  │    │ (direction over  │
└─────────────────┘    │  time)           │
                       └──────────────────┘
┌─────────────────┐
│ Layout JSON     │───> Speaker positions
│ (speaker pos)   │
└─────────────────┘
```

## Key Files

| File                                              | Purpose                           |
| ------------------------------------------------- | --------------------------------- |
| `spatial_engine/src/main.cpp`                     | CLI entry point, argument parsing |
| `spatial_engine/src/renderer/SpatialRenderer.cpp` | Core rendering logic              |
| `spatial_engine/src/renderer/SpatialRenderer.hpp` | Renderer class and config structs |
| `spatial_engine/src/JSONLoader.cpp`               | Parses spatial trajectory JSON    |
| `spatial_engine/src/LayoutLoader.cpp`             | Parses speaker layout JSON        |
| `spatial_engine/src/WavUtils.cpp`                 | WAV I/O utilities                 |
| `spatial_engine/speaker_layouts/*.json`           | Speaker layout configurations     |
| `spatial_engine/spatialRender/CMakeLists.txt`     | CMake build configuration         |

## CLI Usage

```bash
./sonoPleth_spatial_render \
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

### Spatializer Options

| Flag                      | Default | Description                                  |
| ------------------------- | ------- | -------------------------------------------- |
| `--spatializer TYPE`      | dbap    | Spatializer: `vbap`, `dbap`, or `lbap`       |
| `--dbap_focus FLOAT`      | 1.0     | DBAP focus/rolloff exponent (range: 0.2-5.0) |
| `--lbap_dispersion FLOAT` | 0.5     | LBAP dispersion threshold (range: 0.0-1.0)   |

### General Options

| Flag                       | Default  | Description                                                                                                                                                       |
| -------------------------- | -------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--master_gain FLOAT`      | 0.25     | Global gain (prevents clipping from panning sum)                                                                                                                  |
| `--solo_source NAME`       | (none)   | Render only this source (for debugging)                                                                                                                           |
| `--t0 SECONDS`             | 0        | Start rendering at this time                                                                                                                                      |
| `--t1 SECONDS`             | (end)    | Stop rendering at this time                                                                                                                                       |
| `--render_resolution MODE` | block    | Render mode: `block` (recommended) or `sample`                                                                                                                    |
| `--block_size N`           | 64       | Block size for direction updates (32-256)                                                                                                                         |
| `--elevation_mode MODE`    | (legacy) | Legacy flag; accepts `compress` (maps to RescaleFullSphere) or `clamp`. Prefer `--vertical-compensation` / `--no-vertical-compensation` (default: RescaleAtmosUp) |
| `--force_2d`               | (off)    | Force 2D mode (flatten all elevations to z=0)                                                                                                                     |
| `--debug_dir DIR`          | (none)   | Output diagnostics to this directory                                                                                                                              |

## Spatializer Comparison

### DBAP (Distance-Based Amplitude Panning)

**Default spatializer.** Best for general use.

- Works with **any speaker layout** - no coverage gaps
- Uses inverse-distance weighting from source position to speakers
- `--dbap_focus` controls distance attenuation:
  - Lower values (0.2-0.8): Wider spread, more speakers active
  - Default (1.0): Natural distance law
  - Higher values (2.0-5.0): Tighter focus, fewer speakers active

**Recommended for:**

- Unknown or irregular speaker layouts
- Layouts with potential coverage gaps
- General purpose spatial audio

**Technical note:** AlloLib's DBAP internally applies a coordinate transform: `Vec3d(pos.x, -pos.z, pos.y)`. The renderer compensates for this automatically. See `SpatialRenderer::directionToDBAPPosition()` for details.

### VBAP (Vector Base Amplitude Panning)

Classic triangulation-based panning.

- **Best for layouts with good 3D coverage** (dense, well-distributed speakers)
- Builds a mesh of speaker triplets at startup
- Each source maps to exactly 3 speakers (or 2 for 2D layouts)
- Can have **coverage gaps** at zenith/nadir or sparse regions

**Recommended for:**

- Dense 3D speaker arrays (like AlloSphere)
- When precise localization is important
- Layouts with known good triangulation

**Caveats:**

- May produce zero output for directions outside speaker hull
- Renderer includes fallback to nearest speaker for coverage gaps

### LBAP (Layer-Based Amplitude Panning)

Designed for multi-layer speaker setups.

- Optimized for **multi-ring layouts** (e.g., floor/ear/ceiling rings)
- Groups speakers into horizontal layers
- Interpolates within and between layers
- `--lbap_dispersion` controls zenith/nadir signal spread:
  - 0.0: No dispersion at poles
  - 0.5 (default): Moderate spread
  - 1.0: Maximum dispersion to adjacent layers

**Recommended for:**

- Speaker arrays with distinct elevation rings
- TransLAB and similar multi-layer setups
- When VBAP has zenith/nadir issues

## Examples

```bash
# Default render with DBAP (reads LUSID scene directly)
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stems/ \
  --out render.wav

# Use VBAP for precise localization
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stems/ \
  --out render_vbap.wav \
  --spatializer vbap

# Use LBAP for multi-ring layout with high dispersion
./sonoPleth_spatial_render \
  --layout translab_layout.json \
  --positions scene.lusid.json \
  --sources ./stems/ \
  --out render_lbap.wav \
  --spatializer lbap \
  --lbap_dispersion 0.8

# DBAP with tight focus
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stems/ \
  --out render_tight.wav \
  --spatializer dbap \
  --dbap_focus 3.0

# Debug a single source with diagnostics
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stems/ \
  --out debug_source1.wav \
  --solo_source "source_1" \
  --debug_dir ./debug_output/

# Render just 10-20 second window at full gain
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
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

| Value            | Description                                      |
| ---------------- | ------------------------------------------------ |
| `"seconds"`      | Default. Times are in seconds (e.g., `1.5`)      |
| `"samples"`      | Times are sample indices (e.g., `48000` = 1 sec) |
| `"milliseconds"` | Times are in ms (e.g., `1500` = 1.5 sec)         |

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
enum class PannerType {
    DBAP,   // Distance-Based Amplitude Panning (default)
    VBAP,   // Vector Base Amplitude Panning
    LBAP    // Layer-Based Amplitude Panning
};

enum class ElevationMode {
  Clamp,           // Hard clip elevation to layout bounds
  RescaleAtmosUp,  // Map [0,+90°] (Atmos-style) into layout range (DEFAULT)
  RescaleFullSphere // Map [-90°,+90°] (full-sphere) into layout range (legacy "compress")
};

struct RenderConfig {
    // Spatializer selection
    PannerType pannerType = PannerType::DBAP;  // Default: DBAP
    float dbapFocus = 1.0f;                     // DBAP focus exponent (0.2-5.0)
    float lbapDispersion = 0.5f;                // LBAP dispersion threshold (0.0-1.0)

    // General render options
    float masterGain = 0.25f;                   // Prevent summation clipping
    std::string soloSource = "";                // Solo mode: only render this source
    double t0 = -1.0;                           // Start time (-1 = beginning)
    double t1 = -1.0;                           // End time (-1 = end)
    bool debugDiagnostics = false;              // Enable diagnostic output
    std::string debugOutputDir = "";            // Where to write diagnostics
    std::string renderResolution = "block";     // "block" (recommended) or "sample"
    int blockSize = 64;                         // Direction update interval (samples)
  ElevationMode elevationMode = ElevationMode::RescaleAtmosUp;
    bool force2D = false;                       // Force 2D mode
};
```

### Master Gain Rationale

All spatializers work by distributing each source's energy across multiple speakers. When multiple sources are rendered, their contributions **accumulate**. With many sources pointing at similar directions, this can cause clipping.

The default `masterGain = 0.25f` provides ~12dB of headroom. Adjust based on your source count and panning density.

## Render Statistics

After rendering, statistics are computed and logged:

```
Rendering 1234567 samples (25.72 sec) to 16 speakers from 8 sources
Spatializer: DBAP (focus=1.0)
  NOTE: DBAP uses coordinate transform (x,y,z)->(x,z,-y) for AlloLib compatibility
  Master gain: 0.25
  Render resolution: block (block size: 64)
  Elevation mode: rescale_atmos_up

Render Statistics:
  Overall peak: 0.847 (-1.4 dBFS)
  Near-silent channels (< -85 dBFS): 0/16
  Clipping channels (peak > 1.0): 0
  Channels with NaN: 0

Direction Sanitization Summary:
  Layout type: 3D
  Elevation range: [-45.0°, 60.0°]
  Clamped elevations: 0
  Rescaled (AtmosUp): 12847
  Rescaled (FullSphere): 0
  Invalid/fallback directions: 0

DBAP Robustness Summary:
  All blocks rendered normally (no panner failures or fast motion detected)
```

## LFE (Low-Frequency Effects) Handling

### Overview

The renderer now supports robust, layout-driven handling of LFE (subwoofer) channels. LFE sources are **not spatialized**; instead, they are routed directly to the subwoofer channel(s) as defined in the speaker layout JSON. This ensures correct bass management and prevents spatialization artifacts for LFE content.

### Key Features

- **Direct Routing:** LFE sources are detected and routed directly to all subwoofer channels specified in the layout's `subwoofers` array (see below), bypassing the spatializer.
- **Arbitrary Subwoofer Indices:** The renderer supports any number of subwoofers, with arbitrary channel indices, as defined in the layout JSON.
- **Buffer Sizing:** The output buffer is automatically sized to accommodate the highest channel index present in either the `speakers` or `subwoofers` arrays. This prevents out-of-bounds errors when subwoofer channels have high indices.
- **Spatializer-Agnostic:** LFE routing works identically for all spatializers (DBAP, VBAP, LBAP).

### Speaker Layout JSON Example

```json
{
  "speakers": [
    { "id": 1, "channel": 0, ... },
    { "id": 2, "channel": 1, ... }
  ],
  "subwoofers": [
    { "id": "LFE1", "channel": 16 },
    { "id": "LFE2", "channel": 17 }
  ]
}
```

### Implementation Details

- **Detection:** LFE sources are identified by name or metadata (see code for details).
- **Routing:** For each LFE source, its signal is copied directly to all subwoofer channels for every block. The volume is divided by the number of subwoofers to ensure even energy distribution across channels.
- **Buffer Allocation:** The output buffer (`MultiWavData.samples`) is resized to `max(maxSpeakerChannel, maxSubwooferChannel) + 1` to ensure all channels are valid.
- **Safety:** All buffer accesses are bounds-checked by construction; negative or invalid channel indices in the layout will cause a warning or error.
- **Levels:** For now uses a global parameter dbap_sub_compensation to scale the output before dividing by number of subs. Currently a global var, plans to update this.

### Rationale

This approach ensures:

- LFE content is always delivered to the correct subwoofer channels, regardless of layout.
- No risk of buffer overruns or segmentation faults due to high subwoofer channel indices.
- Consistent behavior across all spatializer modes.

### Related Files

- `spatial_engine/src/renderer/SpatialRenderer.cpp` — LFE routing and buffer sizing logic
- `spatial_engine/src/renderer/SpatialRenderer.hpp` — Renderer config and member variables
- `spatial_engine/speaker_layouts/*.json` — Layouts with subwoofer definitions

---

The **Direction Sanitization Summary** shows:

- **Layout type**: Whether the layout is treated as 2D or 3D
- **Elevation range**: The min/max speaker elevations detected from the layout
  - **Sanitized elevations**: Counts of how many directions were adjusted to fit the layout (clamped / rescaledAtmosUp / rescaledFullSphere)
- **Invalid/fallback directions**: Degenerate directions that needed fallback handling

The **Panner Robustness Summary** shows (varies by spatializer):

- **Zero-output blocks**: Blocks where the panner produced silence despite input (coverage gaps)
- **Retargets**: How many times the renderer fell back to nearest speaker
- **Sub-stepped blocks**: Blocks that were subdivided due to fast source motion

If `--debug_dir` is specified, detailed stats are written to:

- `render_stats.json` - Per-channel RMS and peak levels, spatializer info
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

### Direction Sanitization (`sanitizeDirForLayout`)

After direction interpolation, directions are **sanitized** to fit within the speaker layout's representable range. This prevents sources from becoming inaudible when their directions fall outside the layout's elevation coverage.

**For 3D layouts:**

1. Convert direction to spherical coordinates (azimuth, elevation)
2. Apply elevation mode (default: RescaleAtmosUp):

- **RescaleAtmosUp** (default): Map source elevations from [0°, +90°] (typical Atmos-style range) into the layout's [minEl, maxEl]. This preserves upward-only Atmos elevation cues while keeping layout bounds.
- **RescaleFullSphere**: Map source elevations from [-90°, +90°] (full-sphere content) into the layout's [minEl, maxEl]. This reproduces the previous "compress" behavior.
- **Clamp**: Hard clip elevation to [minEl, maxEl] (disable vertical compensation)

3. Convert back to Cartesian unit vector

**For 2D layouts** (elevation span < 3°):

- Flatten direction to z=0 (horizontal plane)
- Re-normalize to unit vector

**Layout bounds** are computed automatically from speaker positions at startup:

```
Layout elevation range: [-45.0°, 60.0°] (span: 105.0°)
```

### Robustness Features

The renderer includes several robustness features to prevent audio dropouts:

#### Zero-Block Detection and Fallback

Spatializers (especially VBAP) can produce near-zero output when a direction falls outside the speaker hull or in a coverage gap. The renderer detects this and automatically retargets to the nearest speaker:

1. **Input energy test**: Skip expensive checks for silent blocks
2. **Render to temp buffer**: Measure panner output energy
3. **Detect failure**: If output is ~silent despite input energy, panner failed
4. **Retarget**: Use direction 90% toward nearest speaker (stays inside hull)

**DEV NOTE**: Zero-block detection currently runs for all panners for consistency. DBAP and LBAP shouldn't need it (they don't have coverage gaps), so this could be optimized to VBAP-only in future.

#### Fast-Mover Sub-Stepping

Sources that move very fast can "blink" out when direction changes significantly within a single block. The renderer detects this and subdivides:

1. **Measure angular delta**: Sample directions at 25% and 75% through block
2. **Threshold check**: If angle > ~14° (0.25 rad), block is "fast"
3. **Sub-step render**: Render in 16-sample chunks with per-chunk direction

#### Diagnostics Output

```
VBAP Robustness Summary:
  Total zero-output blocks detected: 47
  Total retargets to nearest speaker: 47
  Total sub-stepped blocks (fast motion): 1203
  Zero-block sources (top 5):
    source_height_only: 35 blocks
    source_moving_up: 12 blocks
  Fast-mover sources (top 5):
    source_spinning: 892 sub-stepped blocks
    source_orbital: 311 sub-stepped blocks
```

#### Tuning Constants

These are compile-time constants (developer-tunable, may become config options in future):

| Constant                | Default | Description                                 |
| ----------------------- | ------- | ------------------------------------------- |
| `kInputEnergyThreshold` | 1e-4    | Per-sample threshold for "has input energy" |
| `kPannerZeroThreshold`  | 1e-6    | Output sum threshold for "panner failed"    |
| `kFastMoverAngleRad`    | 0.25    | ~14° - triggers sub-stepping                |
| `kSubStepHop`           | 16      | Sub-step size in samples                    |

### Spatializer-Specific Rendering

For each audio block (default 64 samples):

1. Zero the output accumulator
2. For each source:
   a. Fill source buffer with samples (zero-padded beyond source length)
   b. Compute direction vector at **block center** time (reduces edge bias)
   c. **For DBAP**: Convert direction to position via `directionToDBAPPosition()`
   d. Call the active spatializer's `renderBuffer()`
3. Apply master gain
4. Copy to output buffer

**DBAP-specific**: Directions are converted to positions by scaling by `layoutRadius` (median speaker distance). The coordinate transform `(x,y,z) -> (x,z,-y)` compensates for AlloLib's internal DBAP coordinate swap.

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

### "Missing stems" / Sources cutting out

**Cause**: Source directions are outside the speaker layout's elevation coverage. VBAP cannot reproduce directions that fall outside the speaker hull.

**Fix**: The renderer now automatically sanitizes directions:

- By default vertical compensation is ON (RescaleAtmosUp). To preserve Atmos-style elevations use the default behavior which remaps source elevations in [0°, +90°] into the layout's range.
- Legacy flag: `--elevation_mode compress` maps to `RescaleFullSphere` and will remap [-90°, +90°] into the layout's range (equivalent to the older "compress" behavior).
- To disable vertical compensation entirely use `--no-vertical-compensation` (Clamp).
- Check the "Direction Sanitization Summary" in render output. High counts in the rescale/clamp counters indicate many out-of-range directions were adjusted.

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
cd spatial_engine/spatialRender/build
cmake ..
make
```

The executable is `sonoPleth_spatial_render` in the build directory.

Or use the Python setup:

```python
from src.configCPP import setupCppTools
setupCppTools()
```

## Integration with Pipeline

The Python pipeline (`runPipeline.py`) calls the renderer via subprocess:

```python
from src.createRender import runSpatialRender

# Use DBAP (default) — reads LUSID scene directly
runSpatialRender(
    source_folder="processedData/stageForRender",
    render_instructions="processedData/stageForRender/scene.lusid.json",
    speaker_layout="spatial_engine/speaker_layouts/allosphere_layout.json",
    output_file="processedData/completedRenders/spatial_render.wav",
    spatializer="dbap",
    dbap_focus=1.5
)

# Or use VBAP
runSpatialRender(spatializer="vbap")

# Or use LBAP with custom dispersion
runSpatialRender(spatializer="lbap", lbap_dispersion=0.7)
```

````

## Debugging Workflow

1. **Solo a single source**:

   ```bash
   --solo_source "problematic_source" --debug_dir ./debug/
````

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
