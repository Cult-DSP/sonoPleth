# sonoPleth — Comprehensive Agent Context

**Last Updated:** February 10, 2026  
**Project:** sonoPleth - Open Spatial Audio Infrastructure  
**Lead Developer:** Lucian Parisi

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [Architecture & Data Flow](#architecture--data-flow)
3. [Core Components](#core-components)
4. [LUSID Scene Format](#lusid-scene-format)
5. [Spatial Rendering System](#spatial-rendering-system)
6. [File Structure & Organization](#file-structure--organization)
7. [Python Virtual Environment](#python-virtual-environment)
8. [Common Issues & Solutions](#common-issues--solutions)
9. [Development Workflow](#development-workflow)
10. [Testing & Validation](#testing--validation)
11. [Future Work & Known Limitations](#future-work--known-limitations)

---

## Project Overview

### Purpose

sonoPleth is a Python+C++ prototype for decoding and rendering Audio Definition Model (ADM) Broadcast WAV files (Dolby Atmos masters) to arbitrary speaker arrays using multiple spatialization algorithms.

### Key Features

- **Multi-format Input**: Dolby Atmos ADM BWF WAV files
- **Multi-spatializer Support**: DBAP (default), VBAP, LBAP
- **Arbitrary Speaker Layouts**: JSON-defined speaker positions
- **LUSID Scene Format**: v0.5.1 - canonical time-sequenced node graph for spatial audio
- **Zero-dependency Parser**: Python stdlib only for LUSID
- **Subwoofer/LFE Handling**: Automatic routing to designated subwoofer channels
- **Comprehensive Testing**: 106 LUSID tests + renderer tests

### Technology Stack

- **Python 3.8+**: Pipeline orchestration, ADM parsing, data processing
- **C++17**: High-performance spatial audio renderer (AlloLib-based)
- **AlloLib**: Audio spatialization framework (DBAP, VBAP, LBAP)
- **bwfmetaedit**: External tool for extracting ADM XML from WAV files
- **CMake 3.12+**: Build system for C++ components

---

## Architecture & Data Flow

### Complete Pipeline Flow

```
ADM BWF WAV File
    │
    ├─► bwfmetaedit → currentMetaData.xml (ADM XML)
    │
    ├─► checkAudioChannels.py → containsAudio.json
    │
    └─► analyzeADM/parser.py (lxml) → Python dicts
                                        │
                                        ▼
                              LUSID/src/xmlParser.py
                              (ADM dicts → LUSID scene)
                                        │
                                        ▼
                      processedData/stageForRender/scene.lusid.json
                              (CANONICAL FORMAT)
                                        │
                                        ├─► C++ JSONLoader::loadLusidScene()
                                        │         │
                                        │         ▼
                                        │   SpatialRenderer (DBAP/VBAP/LBAP)
                                        │         │
                                        │         ▼
                                        │   Multichannel WAV output
                                        │
                                        └─► (optional) analyzeRender.py → PDF report
```

### LUSID as Canonical Format

**IMPORTANT:** LUSID `scene.lusid.json` is the **source of truth** for spatial data. The old `renderInstructions.json` format is **deprecated** and moved to `old_schema/` directories.

The C++ renderer reads LUSID directly — no intermediate format conversion.

### Recent Architecture Changes (v0.5.1 → v0.5.2)

**Eliminated intermediate JSON files:**

- `objectData.json`, `directSpeakerData.json`, `globalData.json` no longer written to disk
- Data flows as Python dicts in memory: `parseMetadata()` → `packageForRender()` → `adm_to_lusid_scene()`
- Only `containsAudio.json` written to disk (consumed by stem splitter)

**XML Parsing Migration:**

- Created `xml_etree_parser.py` using Python stdlib (`xml.etree.ElementTree`)
- Eliminates `lxml` dependency from active LUSID code
- 2.3x faster than old lxml two-step pipeline (547ms vs 1253ms on 25MB XML)
- 5.5x more memory (175MB vs 32MB) — acceptable for typical ADM files (<100MB)
- Output parity confirmed — identical LUSID scenes

---

## Core Components

### 1. ADM Metadata Extraction & Parsing

#### `bwfmetaedit`

- **Purpose**: Extract ADM XML from BWF WAV file
- **Type**: External command-line tool
- **Installation**: `brew install bwfmetaedit` (macOS) or MediaArea website
- **Output**: `processedData/currentMetaData.xml`

#### `src/analyzeADM/parser.py`

- **Purpose**: Parse ADM XML using lxml
- **Key Functions**:
  - `parseMetadata(xmlPath)` → returns dict with `objectData`, `directSpeakerData`, `globalData`
  - `getGlobalData()`, `getDirectSpeakerData()` — extract specific ADM sections
- **Dependencies**: `lxml` (external)
- **Status**: Active in main pipeline, but may be replaced by stdlib parser

#### `src/analyzeADM/checkAudioChannels.py`

- **Purpose**: Detect which ADM channels actually contain audio
- **Key Functions**: `channelHasAudio(wavPath)` → dict mapping channel index to boolean
- **Output**: `processedData/containsAudio.json`
- **Why**: Skip silent channels in stem splitting (common in ADM beds)

### 2. LUSID Scene Format (v0.5.1)

#### `LUSID/src/scene.py` — Data Model

Core dataclasses for LUSID Scene v0.5:

- `LusidScene`: Top-level container (version, sampleRate, timeUnit, metadata, frames)
- `Frame`: Timestamped snapshot of all active nodes
- **5 Node Types**:
  - `AudioObjectNode`: Spatial source with `cart` [x,y,z]
  - `DirectSpeakerNode`: ✨ **NEW** — Fixed bed channel with `speakerLabel`, `channelID`
  - `LFENode`: Low-frequency effects (routed to subs, not spatialized)
  - `SpectralFeaturesNode`: Audio analysis metadata (centroid, flux, bandwidth)
  - `AgentStateNode`: AI/agent state data (ignored by renderer)

**Zero external dependencies** — stdlib only.

#### `LUSID/src/parser.py` — JSON Loader

- **Purpose**: Load and validate LUSID JSON files
- **Philosophy**: Warn but never crash — graceful fallback for all issues
- **Key Functions**:
  - `parse_file(path)` → `LusidScene` object
  - `parse_json(json_str)` → `LusidScene` object
- **Validation**: Warns on missing fields, invalid values, unknown types (auto-corrects)

#### `LUSID/src/xmlParser.py` — ADM to LUSID Converter

- **Purpose**: Convert pre-parsed ADM data dicts → LUSID scene
- **Key Functions**:
  - `adm_to_lusid_scene(object_data, direct_speaker_data, global_data, contains_audio)` → `LusidScene`
  - `load_processed_data_and_build_scene(processed_dir)` — convenience function
- **Channel Mapping**:
  - DirectSpeakers: Channel N → Group N → Node `N.1` (type: `direct_speaker`)
  - Channel 4 hardcoded as LFE (see `_DEV_LFE_HARDCODED` flag)
  - Audio Objects: Object N → Group (10+N) → Node `X.1` (type: `audio_object`)
- **Silent Channel Skipping**: Uses `containsAudio` dict to skip empty channels

#### `LUSID/src/xml_etree_parser.py` — ✨ **NEW** Single-Step XML Parser

- **Purpose**: Parse ADM XML → LUSID scene in one step (no intermediate dicts)
- **Dependencies**: Python stdlib only (`xml.etree.ElementTree`)
- **Performance**: 2.3x faster than lxml two-step pipeline
- **Key Functions**:
  - `parse_adm_xml_to_lusid_scene(xml_path, contains_audio)` → `LusidScene`
  - `parse_and_write_lusid_scene(xml_path, output_path, contains_audio)`
- **Status**: Ready for integration into main pipeline (not yet wired)

### 3. Audio Stem Splitting

#### `src/packageADM/splitStems.py`

- **Purpose**: Split multichannel ADM WAV into mono stems for rendering
- **Key Functions**:
  - `splitChannelsToMono(wavPath, output_dir, contains_audio_data)` → writes `X.1.wav` files
  - `mapEmptyChannels()` — marks silent channels for skipping
- **Output Naming Convention**:
  - DirectSpeakers: `1.1.wav`, `2.1.wav`, `3.1.wav`, etc.
  - LFE: `LFE.wav` (special case)
  - Audio Objects: `11.1.wav`, `12.1.wav`, etc.
- **Status**: Updated for in-memory dict support (Feb 10, 2026)

### 4. Spatial Rendering Packaging

#### `src/packageADM/packageForRender.py`

- **Purpose**: Orchestrate stem splitting and LUSID scene generation
- **Key Functions**: `packageForRender(adm_wav_path, output_dir, parsed_adm_data, contains_audio_data)`
- **Flow**:
  1. Split ADM WAV into mono stems
  2. Call `LUSID.xmlParser.adm_to_lusid_scene()`
  3. Write `scene.lusid.json` to `stageForRender/`
- **Status**: Updated for dict-based flow (no JSON intermediates)

### 5. C++ Spatial Renderer

#### `spatial_engine/src/JSONLoader.cpp` — LUSID Scene Parser

- **Purpose**: Parse LUSID JSON and load audio sources for rendering
- **Key Functions**:
  - `JSONLoader::loadLusidScene(path)` → `SpatialData` struct
  - Extracts `audio_object`, `direct_speaker`, `LFE` nodes
  - Converts timestamps using `timeUnit` + `sampleRate`
  - Source keys use node ID format (`"1.1"`, `"11.1"`)
  - Ignores `spectral_features`, `agent_state` nodes

#### `spatial_engine/src/renderer/SpatialRenderer.cpp` — Core Renderer

- **Purpose**: Render spatial audio using DBAP/VBAP/LBAP
- **Key Methods**:
  - `renderPerBlock()` — block-based rendering (default 64 samples)
  - `sanitizeDirForLayout()` — elevation compensation (RescaleAtmosUp default)
  - `directionToDBAPPosition()` — coordinate transform for AlloLib DBAP
  - `nearestSpeakerDir()` — fallback for VBAP coverage gaps
- **Robustness Features**:
  - Zero-block detection and retargeting
  - Fast-mover sub-stepping (angular delta > 0.25 rad)
  - LFE direct routing to subwoofer channels

#### AlloLib Spatializers

- **DBAP** (`al::Dbap`): Distance-Based Amplitude Panning
  - Works with any layout (no coverage gaps)
  - Uses inverse-distance weighting
  - `--dbap_focus` controls distance rolloff (default: 1.0)
  - **Coordinate quirk**: AlloLib applies internal transform `(x,y,z) → (x,-z,y)` — compensated automatically
- **VBAP** (`al::Vbap`): Vector Base Amplitude Panning
  - Triangulation-based (builds speaker mesh at startup)
  - Each source maps to 3 speakers (or 2 for 2D)
  - Can have coverage gaps at zenith/nadir
  - Fallback: Retarget to nearest speaker (90% toward speaker)
- **LBAP** (`al::Lbap`): Layer-Based Amplitude Panning
  - Optimized for multi-ring layouts
  - Groups speakers into horizontal layers
  - `--lbap_dispersion` controls zenith/nadir spread (default: 0.5)

### 6. Pipeline Orchestration

#### `runPipeline.py` — Main Entry Point

- **Purpose**: Orchestrate full ADM → spatial render pipeline
- **Flow**:
  1. Check initialization (`checkInit()`)
  2. Extract ADM XML (`extractMetadata()`)
  3. Detect audio channels (`channelHasAudio()`)
  4. Parse ADM XML (`parseMetadata()`)
  5. Package for render (`packageForRender()`)
  6. Build C++ renderer (`buildSpatialRenderer()`)
  7. Run spatial render (`runSpatialRender()`)
  8. Analyze render (`analyzeRender()` — optional PDF)

#### `runGUI.py` — Jupyter Notebook GUI

- **Purpose**: Interactive GUI for running pipeline with file pickers
- **Features**: File selection, layout picker, progress display
- **Flow**: Same as `runPipeline.py` but with UI

### 7. Analysis & Debugging

#### `src/analyzeRender.py`

- **Purpose**: Generate PDF analysis of rendered multichannel WAV
- **Features**: Per-channel dB plots, peak/RMS stats, spectrogram
- **Usage**: Automatically run if `create_pdf=True` in pipeline

#### `src/createRender.py` — Python Wrapper

- **Purpose**: Python interface to C++ renderer
- **Key Functions**: `runSpatialRender(source_folder, render_instructions, speaker_layout, output_file, **kwargs)`
- **CLI Options**: spatializer, dbap_focus, lbap_dispersion, master_gain, solo_source, debug_dir, etc.

---

## LUSID Scene Format

### JSON Structure (v0.5.1)

```json
{
  "version": "0.5",
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "metadata": {
    "title": "Scene name",
    "sourceFormat": "ADM",
    "duration": "00:09:26.000"
  },
  "frames": [
    {
      "time": 0.0,
      "nodes": [
        {
          "id": "1.1",
          "type": "direct_speaker",
          "cart": [-1.0, 1.0, 0.0],
          "speakerLabel": "RC_L",
          "channelID": "AC_00011001"
        },
        {
          "id": "4.1",
          "type": "LFE"
        },
        {
          "id": "11.1",
          "type": "audio_object",
          "cart": [-0.975753, 1.0, 0.0]
        }
      ]
    }
  ]
}
```

### Node Types & ID Convention

**Node ID Format: `X.Y`**

- **X** = group number (logical grouping)
- **Y** = hierarchy level (1 = parent, 2+ = children)

**Channel Assignment Convention:**

- Groups 1–10: DirectSpeaker bed channels
- Group 4: LFE (currently hardcoded — see `_DEV_LFE_HARDCODED`)
- Groups 11+: Audio objects

**Node Types:**

| Type                | ID Pattern | Required Fields                                   | Renderer Behavior                             |
| ------------------- | ---------- | ------------------------------------------------- | --------------------------------------------- |
| `audio_object`      | `X.1`      | `id`, `type`, `cart`                              | Spatialized (DBAP/VBAP/LBAP)                  |
| `direct_speaker`    | `X.1`      | `id`, `type`, `cart`, `speakerLabel`, `channelID` | Treated as static audio_object                |
| `LFE`               | `X.1`      | `id`, `type`                                      | Routes to subwoofers, bypasses spatialization |
| `spectral_features` | `X.2+`     | `id`, `type`, + data fields                       | Ignored by renderer                           |
| `agent_state`       | `X.2+`     | `id`, `type`, + data fields                       | Ignored by renderer                           |

### Coordinate System

**Cartesian Direction Vectors: `cart: [x, y, z]`**

- **x**: Left (−) / Right (+)
- **y**: Back (−) / Front (+)
- **z**: Down (−) / Up (+)
- Normalized to unit length by renderer
- Zero vectors replaced with front `[0, 1, 0]`

### Time Units

| Value            | Aliases  | Description                            |
| ---------------- | -------- | -------------------------------------- |
| `"seconds"`      | `"s"`    | Default. Timestamps in seconds         |
| `"samples"`      | `"samp"` | Sample indices (requires `sampleRate`) |
| `"milliseconds"` | `"ms"`   | Timestamps in milliseconds             |

**Always specify `timeUnit` explicitly** to avoid heuristic detection warnings.

### Source ↔ WAV File Mapping

| Node ID | WAV Filename | Description                |
| ------- | ------------ | -------------------------- |
| `1.1`   | `1.1.wav`    | DirectSpeaker (e.g., Left) |
| `4.1`   | `LFE.wav`    | LFE (special naming)       |
| `11.1`  | `11.1.wav`   | Audio object group 11      |

**Important:** Old `src_N` naming convention is deprecated.

---

## Spatial Rendering System

### Spatializer Comparison

| Feature          | DBAP (default)            | VBAP                  | LBAP                        |
| ---------------- | ------------------------- | --------------------- | --------------------------- |
| **Coverage**     | No gaps (works anywhere)  | Can have gaps         | No gaps                     |
| **Layout Req**   | Any layout                | Good 3D triangulation | Multi-ring layers           |
| **Localization** | Moderate                  | Precise               | Moderate                    |
| **Speakers/Src** | Distance-weighted (many)  | 3 speakers (exact)    | Layer interpolation         |
| **Best For**     | Unknown/irregular layouts | Dense 3D arrays       | Allosphere, TransLAB        |
| **Params**       | `--dbap_focus` (0.2–5.0)  | None                  | `--lbap_dispersion` (0–1.0) |

### Rendering Modes

| Mode     | Description                            | Performance | Accuracy | Recommended |
| -------- | -------------------------------------- | ----------- | -------- | ----------- |
| `block`  | Direction computed once per block (64) | Fast        | High     | ✓ Yes       |
| `sample` | Direction computed every sample        | Slow        | Highest  | Critical    |

### Elevation Compensation

**Default: RescaleAtmosUp**

- Maps Atmos-style elevations [0°, +90°] into layout's elevation range
- Prevents sources from becoming inaudible at zenith
- Options: `RescaleAtmosUp` (default), `RescaleFullSphere` (legacy "compress"), `Clamp` (hard clip)
- CLI: `--elevation_mode compress` or `--no-vertical-compensation`

### LFE (Low-Frequency Effects) Handling

**Detection & Routing:**

- Sources named "LFE" or node type `LFE` bypass spatialization
- Routed directly to all subwoofer channels defined in layout JSON
- Energy divided by number of subs
- Gain compensation: `dbap_sub_compensation = 0.95` (global var — TODO: make configurable)

**Layout JSON Subwoofer Definition:**

```json
{
  "speakers": [...],
  "subwoofers": [
    { "channel": 16 },
    { "channel": 17 }
  ]
}
```

**Buffer Sizing:**

- Output buffer sized to `max(maxSpeakerChannel, maxSubwooferChannel) + 1`
- Supports arbitrary subwoofer indices beyond speaker count

### Robustness Features

#### Zero-Block Detection & Fallback

- Detects when spatializer produces silence despite input energy
- Common with VBAP at coverage gaps
- **Fallback**: Retarget direction 90% toward nearest speaker
- Threshold: `kPannerZeroThreshold = 1e-6` (output sum)

#### Fast-Mover Sub-Stepping

- Detects sources moving >14° (~0.25 rad) within a block
- Subdivides block into 16-sample chunks with per-chunk direction
- Prevents "blinking" artifacts from rapid trajectory changes

#### Direction Validation

- All directions validated before use (NaN/Inf check)
- Zero-length vectors replaced with front `[0, 1, 0]`
- Warnings rate-limited (once per source, not per block)

### Render Statistics

**End-of-render diagnostics:**

- Overall peak, near-silent channels, clipping channels, NaN channels
- Direction sanitization summary (clamped/rescaled counts)
- Panner robustness summary (zero-blocks, retargets, sub-stepped blocks)

**Debug output** (`--debug_dir`):

- `render_stats.json` — per-channel RMS/peak, spatializer info
- `block_stats.log` — per-block processing stats (sampled)

### CLI Usage Examples

```bash
# Default render with DBAP
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render.wav

# Use VBAP for precise localization
./sonoPleth_spatial_render \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render_vbap.wav \
  --spatializer vbap

# DBAP with tight focus
./sonoPleth_spatial_render \
  --spatializer dbap \
  --dbap_focus 3.0 \
  --layout translab_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out render_tight.wav

# Debug single source with diagnostics
./sonoPleth_spatial_render \
  --solo_source "11.1" \
  --debug_dir ./debug_output/ \
  --layout allosphere_layout.json \
  --positions scene.lusid.json \
  --sources ./stageForRender/ \
  --out debug_source.wav
```

---

## File Structure & Organization

### Project Root

```
sonoPleth/
├── activate.sh                      # Reactivate venv (use: source activate.sh)
├── init.sh                          # One-time setup (use: source init.sh)
├── requirements.txt                 # Python dependencies (inc. lxml)
├── runPipeline.py                   # Main CLI entry point
├── runGUI.py                        # Jupyter notebook GUI
├── README.md                        # User documentation
├── internalDocsMD/                  # Main project documentation
│   ├── AGENTS.md                    # THIS FILE
│   ├── RENDERING.md                 # Spatial renderer docs
│   ├── TODO.md                      # Task list
│   ├── json_schema_info.md          # LUSID/layout JSON schemas
│   ├── dolbyMetadata.md             # Atmos channel labels
│   ├── 1-27-rendering-dev.md        # VBAP robustness notes (Jan 27)
│   ├── 1-28-vertical-dev.md         # Multi-spatializer notes (Jan 28)
│   └── DBAP-Testing.md              # DBAP focus testing (Feb 3)
├── LUSID/                           # LUSID Scene format library
│   ├── README.md                    # LUSID user docs
│   ├── schema/
│   │   └── lusid_scene_v0.5.schema.json
│   ├── src/
│   │   ├── __init__.py              # Public API
│   │   ├── scene.py                 # Data model (5 node types)
│   │   ├── parser.py                # LUSID JSON loader
│   │   ├── xmlParser.py             # ADM dicts → LUSID
│   │   ├── xml_etree_parser.py      # NEW: stdlib XML → LUSID
│   │   └── old_schema/
│   │       └── transcoder.py        # OBSOLETE: LUSID → renderInstructions
│   ├── tests/
│   │   ├── test_parser.py           # 42 tests
│   │   ├── test_xmlParser.py        # 28 tests
│   │   ├── test_xml_etree_parser.py # 36 tests
│   │   └── benchmark_xml_parsers.py # Performance comparison
│   └── internalDocs/
│       ├── AGENTS.md                # LUSID agent spec
│       ├── DEVELOPMENT.md           # LUSID dev notes
│       ├── conceptNotes.md          # Original design
│       └── xml_benchmark.md         # Benchmark results
├── src/
│   ├── analyzeADM/
│   │   ├── parser.py                # lxml ADM XML parser
│   │   ├── checkAudioChannels.py   # Detect silent channels
│   │   └── extractMetadata.py      # bwfmetaedit wrapper
│   ├── packageADM/
│   │   ├── packageForRender.py     # Orchestrator
│   │   ├── splitStems.py           # Multichannel → mono
│   │   └── old_schema/
│   │       └── createRenderInfo.py  # OBSOLETE: → renderInstructions
│   ├── analyzeRender.py             # PDF analysis generator
│   ├── createRender.py              # Python → C++ renderer wrapper
│   └── configCPP.py                 # C++ build utilities
├── spatial_engine/
│   ├── speaker_layouts/             # JSON speaker definitions
│   │   ├── allosphere_layout.json
│   │   └── translab-sono-layout.json
│   ├── src/
│   │   ├── main.cpp                 # Renderer CLI entry
│   │   ├── JSONLoader.cpp           # LUSID scene loader
│   │   ├── LayoutLoader.cpp         # Speaker layout loader
│   │   ├── WavUtils.cpp             # WAV I/O
│   │   ├── renderer/
│   │   │   ├── SpatialRenderer.cpp  # Core renderer
│   │   │   └── SpatialRenderer.hpp
│   │   └── old_schema_loader/       # OBSOLETE
│   │       ├── JSONLoader.cpp       # renderInstructions parser
│   │       └── JSONLoader.hpp
│   ├── spatialRender/
│   │   ├── CMakeLists.txt           # CMake config
│   │   └── build/                   # Build output dir
│   └── realtimeEngine/              # Future: live rendering
├── thirdparty/
│   └── allolib/                     # Git submodule (audio lib)
├── processedData/                   # Pipeline outputs
│   ├── currentMetaData.xml          # Extracted ADM XML
│   ├── containsAudio.json           # Channel audio detection
│   └── stageForRender/
│       ├── scene.lusid.json         # CANONICAL SPATIAL DATA
│       ├── 1.1.wav, 2.1.wav, ...    # Stem files
│       └── LFE.wav
└── utils/
    ├── getExamples.py               # Download test files
    └── deleteData.py                # Clean processedData/
```

### Obsolete Files (Archived)

**LUSID old schema:**

- `LUSID/src/old_schema/transcoder.py` — LUSID → renderInstructions.json
- `LUSID/tests/old_schema/test_transcoder.py`

**sonoPleth old schema:**

- `src/packageADM/old_schema/createRenderInfo.py` — processedData → renderInstructions
- `spatial_engine/src/old_schema_loader/JSONLoader.cpp/.hpp` — renderInstructions parser

**Reason:** LUSID is now the canonical format. C++ renderer reads LUSID directly.

---

## Python Virtual Environment

### Critical: Use Project Virtual Environment

sonoPleth uses a virtual environment located at the **project root** (`sonoPleth/bin/`).

**Activation:**

```bash
# First time setup (creates venv + installs deps)
source init.sh

# Subsequent sessions
source activate.sh
```

**Verification:**

- Check for `(sonoPleth)` prefix in terminal prompt
- Run `which python` → should show `/path/to/sonoPleth/bin/python`

### Common Mistake: Using System Python

**❌ Wrong:**

```bash
python3 runPipeline.py              # Uses system Python, missing deps
python3 LUSID/tests/benchmark*.py   # Missing lxml
```

**✅ Correct:**

```bash
python runPipeline.py               # Uses venv Python with all deps
python LUSID/tests/benchmark*.py    # venv has lxml
```

### Dependencies

**Python (requirements.txt):**

- `lxml` — ADM XML parsing (may be removable after xml_etree_parser migration)
- `soundfile` — Audio file I/O
- `numpy` — Numerical operations
- `matplotlib` — Render analysis plots
- Others: see `requirements.txt`

**External Tools:**

- `bwfmetaedit` — ADM metadata extraction
- `cmake`, `make`, C++ compiler — C++ renderer build

---

## Common Issues & Solutions

### ADM Parsing

**Issue:** `ModuleNotFoundError: No module named 'lxml'`  
**Solution:** Activate venv: `source activate.sh`

**Issue:** `bwfmetaedit command not found`  
**Solution:** Install: `brew install bwfmetaedit` (macOS)

**Issue:** Empty `objectData.json` after parsing  
**Solution:** Check ADM XML format. Some ADM files have non-standard structure.

### Stem Splitting

**Issue:** Stems have wrong naming (`src_1.wav` instead of `1.1.wav`)  
**Solution:** Updated code uses node IDs now. Re-run pipeline with latest code.

**Issue:** LFE stem missing  
**Solution:** Check `_DEV_LFE_HARDCODED` flag in `xmlParser.py`. LFE detection may need adjustment.

### Spatial Rendering

**Issue:** "Missing stems" / sources cutting out  
**Cause:** Directions outside layout's elevation coverage (VBAP gaps)  
**Solution:**

- Use DBAP instead of VBAP (no coverage gaps): `--spatializer dbap`
- Enable vertical compensation (default): RescaleAtmosUp
- Check "Direction Sanitization Summary" in render output

**Issue:** Sources at zenith/nadir are silent  
**Cause:** Layout doesn't have speakers at extreme elevations  
**Solution:** Use `--elevation_mode compress` (RescaleFullSphere) to map full [-90°, +90°] range

**Issue:** "Zero output" / silent channels  
**Cause:** AlloLib expects speaker angles in degrees, not radians  
**Solution:** Verify `LayoutLoader.cpp` converts radians → degrees:

```cpp
speaker.azimuth = s.azimuth * 180.0f / M_PI;
```

**Issue:** LFE too loud or too quiet  
**Cause:** `dbap_sub_compensation` global variable needs tuning  
**Current:** 0.95 (95% of original level)  
**Solution:** Adjust `dbap_sub_compensation` in `SpatialRenderer.cpp` (TODO: make CLI option)

**Issue:** Clicks / discontinuities in render  
**Cause:** Stale buffer data between blocks  
**Solution:** Renderer now clears buffers with `std::fill()` before each source — fixed in current code

**Issue:** DBAP sounds wrong / reversed  
**Cause:** AlloLib DBAP coordinate transform: `(x,y,z) → (x,-z,y)`  
**Solution:** Renderer compensates automatically in `directionToDBAPPosition()` — no action needed

### Building C++ Renderer

**Issue:** CMake can't find AlloLib  
**Solution:** Initialize submodule: `git submodule update --init --recursive`

**Issue:** Build fails with "C++17 required"  
**Solution:** Update CMake to 3.12+ and ensure compiler supports C++17

**Issue:** Changes to C++ code not reflected after rebuild  
**Solution:** Clean build: `rm -rf spatial_engine/spatialRender/build/ && python -c "from src.configCPP import buildSpatialRenderer; buildSpatialRenderer()"`

### LUSID Scene

**Issue:** Parser warnings about unknown fields  
**Solution:** LUSID parser is permissive — warnings are non-fatal. Check if field name is misspelled.

**Issue:** Frames not sorted by time  
**Solution:** Parser auto-sorts frames — no action needed. Warning is informational.

**Issue:** Duplicate node IDs within frame  
**Solution:** Parser keeps last occurrence — fix upstream data generation.

---

## Development Workflow

### Making Changes to Python Code

1. **Edit files** in `src/`, `LUSID/src/`, etc.
2. **Run tests**: `cd LUSID && python -m unittest discover -s tests -v`
3. **Test pipeline**: `python runPipeline.py sourceData/example.wav`
4. **Check output**: Verify `scene.lusid.json` and `render.wav` are correct

### Making Changes to C++ Renderer

1. **Edit files** in `spatial_engine/src/`
2. **Rebuild**:
   ```bash
   rm -rf spatial_engine/spatialRender/build/
   python -c "from src.configCPP import buildSpatialRenderer; buildSpatialRenderer()"
   ```
3. **Test manually**:
   ```bash
   ./spatial_engine/spatialRender/build/sonoPleth_spatial_render \
     --layout spatial_engine/speaker_layouts/allosphere_layout.json \
     --positions processedData/stageForRender/scene.lusid.json \
     --sources processedData/stageForRender/ \
     --out test_render.wav \
     --debug_dir debug/
   ```
4. **Check diagnostics** in `debug/render_stats.json`

### Adding a New Node Type to LUSID

1. **Define node class** in `LUSID/src/scene.py` inheriting from `Node`
2. **Add parsing logic** in `LUSID/src/parser.py` (`_parse_<type>()`)
3. **Update JSON Schema** in `LUSID/schema/lusid_scene_v0.5.schema.json`
4. **Write tests** in `LUSID/tests/test_parser.py`
5. **Update C++ loader** if renderer needs to handle new type
6. **Document** in `LUSID/README.md` and this file

### Adding a New Spatializer

1. **Add enum value** to `PannerType` in `SpatialRenderer.hpp`
2. **Initialize panner** in `SpatialRenderer` constructor
3. **Add CLI flag** in `main.cpp` argument parsing
4. **Update dispatch** in `renderPerBlock()` to call new panner
5. **Test** with various layouts
6. **Document** in `internalDocsMD/RENDERING.md`

### Git Workflow

```bash
# Fetch latest from origin
git fetch origin

# Create feature branch
git checkout -b feature/my-feature

# Make changes, commit
git add .
git commit -m "feat: description of changes"

# Push to origin
git push origin feature/my-feature

# Create PR on GitHub
```

---

## Testing & Validation

### LUSID Tests

```bash
cd LUSID
python -m unittest discover -s tests -v
```

**Coverage:**

- `test_parser.py` — 42 tests (data model, JSON parsing, validation)
- `test_xmlParser.py` — 28 tests (ADM → LUSID conversion, channels, LFE)
- `test_xml_etree_parser.py` — 36 tests (stdlib XML parser)
- **Total:** 106 tests, all passing

### Pipeline Integration Testing

```bash
# Test with example file
python runPipeline.py sourceData/driveExampleSpruce.wav

# Verify outputs
ls processedData/stageForRender/
# Should see: scene.lusid.json, 1.1.wav, 2.1.wav, ..., LFE.wav

# Check LUSID scene sanity
python -c "from LUSID.src import parse_file; scene = parse_file('processedData/stageForRender/scene.lusid.json'); print(scene.summary())"
```

### Renderer Smoke Test

```bash
# Build renderer
python -c "from src.configCPP import buildSpatialRenderer; buildSpatialRenderer()"

# Test DBAP render
./spatial_engine/spatialRender/build/sonoPleth_spatial_render \
  --layout spatial_engine/speaker_layouts/allosphere_layout.json \
  --positions processedData/stageForRender/scene.lusid.json \
  --sources processedData/stageForRender/ \
  --out test_dbap.wav \
  --spatializer dbap

# Check output exists and has correct channel count
ffprobe test_dbap.wav 2>&1 | grep "Stream.*Audio"
```

### Benchmarking

```bash
# XML parsing performance comparison
cd sonoPleth_root
python LUSID/tests/benchmark_xml_parsers.py

# Results documented in LUSID/internalDocs/xml_benchmark.md
```

### Validation Checklist

- [ ] LUSID tests pass: `cd LUSID && python -m unittest discover`
- [ ] Pipeline runs end-to-end without errors
- [ ] `scene.lusid.json` has expected frame/node counts
- [ ] Stem files exist and have audio content
- [ ] Renderer produces multichannel WAV with no NaN/clipping
- [ ] Render statistics show reasonable panner robustness metrics
- [ ] PDF analysis (if enabled) shows expected channel activity

---

## Future Work & Known Limitations

### High Priority

#### LUSID Integration Tasks

- [ ] **Wire `xml_etree_parser` into main pipeline**
  - Replace `sonoPleth parser.py → xmlParser.py` two-step with single `xml_etree_parser.parse_adm_xml_to_lusid_scene()`
  - Update `packageForRender.py` to call stdlib parser directly
  - Test end-to-end equivalence

- [ ] **Create LusidScene debug summary method**
  - Add `scene.summary()` method to `LusidScene` class
  - Replace old `analyzeMetadata.printSummary()` (reads from disk)
  - Print version, sampleRate, frame count, node counts by type

- [ ] **Label-based LFE detection**
  - Disable `_DEV_LFE_HARDCODED` flag in `xmlParser.py`
  - Detect LFE by checking `speakerLabel` for "LFE" substring
  - Test with diverse ADM files (not just channel 4)

- [ ] **Remove lxml dependency evaluation**
  - If `xml_etree_parser` fully replaces lxml usage, remove from `requirements.txt`
  - Audit codebase for remaining lxml imports
  - Update documentation

#### Renderer Enhancements

- [ ] **LFE gain control**
  - Make `dbap_sub_compensation` a configurable parameter (CLI flag or config file)
  - Currently hardcoded global var in `SpatialRenderer.cpp`
  - Depends on DBAP focus and layout — needs more testing

- [ ] **Spatializer auto-detection**
  - Analyze layout to recommend best spatializer
  - Heuristics: elevation span, ring detection, triangulation quality
  - Implement `--spatializer auto` CLI flag

- [ ] **Channel remapping**
  - Support arbitrary device channel assignments
  - Currently: output channels = consecutive indices
  - Layout JSON has `deviceChannel` field (not used by renderer)

- [ ] **Atmos mix fixes**
  - Test with diverse Atmos content (different bed configurations)
  - Validate DirectSpeaker handling matches Atmos spec

### Medium Priority

#### Performance Optimizations

- [ ] **Large scene optimization** (1000+ frames)
  - Current: 2823 frames loads in <1ms (acceptable)
  - Profile with 10000+ frame synthetic scenes
  - Consider lazy frame loading if needed

- [ ] **SIMD energy computation**
  - Use vector ops for sum-of-absolutes in zero-block detection
  - Currently: scalar loop

- [ ] **Parallel source processing**
  - Sources are independent within a block
  - Could parallelize `renderPerBlock()` loop

#### Feature Additions

- [ ] **Additional node types**
  - `reverb_zone` — spatial reverb metadata
  - `interpolation_hint` — per-node interpolation mode
  - `width` — source width parameter (DBAP/reverb)

- [ ] **Real-time rendering engine**
  - `spatial_engine/realtimeEngine/` placeholder exists
  - Live LUSID scene streaming (OSC, network, file watch)
  - Low-latency audio I/O

- [ ] **AlloLib player bundle**
  - Package renderer + player + layout loader as single allolib app
  - GUI for layout selection, playback control
  - Integration with AlloSphere dome

#### Pipeline Improvements

- [ ] **Stem splitting without intermediate files**
  - Currently: splits all channels → mono WAVs → C++ loads them
  - Alternative: pass audio buffers directly (Python → C++ via pybind11?)

- [ ] **Internal data structures instead of many JSONs**
  - Already done for LUSID (scene.lusid.json is canonical)
  - Cleanup: remove stale `renderInstructions.json` files from old runs

- [ ] **Debugging JSON with extended info**
  - Single debug JSON with all metadata (ADM, LUSID, render stats)
  - Useful for analysis tools and debugging

### Low Priority

#### Code Quality

- [ ] **Consolidate file deletion helpers**
  - Multiple files use different patterns for delete-before-write
  - Create single util function in `utils/`

- [ ] **Fix hardcoded paths**
  - `parser.py`, `packageForRender.py` have hardcoded `processedData/` paths
  - Make configurable via CLI or config file

- [ ] **Static object handling in render instructions**
  - LUSID handles static objects via single keyframe
  - Verify behavior matches expectations

#### Dependency Management

- [ ] **Stable builds for all dependencies**
  - Ensure `requirements.txt` pins versions
  - Git submodules should track specific commits (already done for AlloLib)

- [ ] **Partial submodule clones**
  - AlloLib is large — only clone parts actually used?
  - May not be worth complexity

- [ ] **Bundle as CLI tool**
  - Package entire pipeline as installable command (`pip install sonopleth`)
  - Single entry point: `sonopleth render <adm_file> --layout <layout>`

### Known Limitations

#### ADM Format Support

- **Assumption:** Standard EBU ADM BWF structure
- **Limitation:** Non-standard ADM files may fail to parse
- **Workaround:** Test with diverse ADM sources, add special cases as needed

#### Bed Channel Handling

- **Current:** DirectSpeakers treated as static audio_objects (1 keyframe)
- **Limitation:** No bed-specific features (e.g., "fixed gain" metadata)
- **Impact:** Minimal — beds are inherently static

#### LFE Detection

- **Current:** Hardcoded to channel 4 (`_DEV_LFE_HARDCODED = True`)
- **Limitation:** Non-standard LFE positions may not be detected
- **Planned Fix:** Label-based detection (check `speakerLabel` for "LFE")

#### Memory Usage

- **xml_etree_parser:** 5.5x more memory than lxml (175MB vs 32MB for 25MB XML)
- **Impact:** Acceptable for typical ADM files (<100MB)
- **Fallback:** lxml pathway preserved in `old_XML_parse/` if needed

#### Coordinate System Quirks

- **AlloLib DBAP:** Internal transform `(x,y,z) → (x,-z,y)`
- **Status:** Compensated automatically in `directionToDBAPPosition()`
- **Risk:** If AlloLib updates this, our compensation may break
- **Mitigation:** AlloLib source marked with `// FIXME test DBAP` — monitor upstream

#### VBAP Coverage Gaps

- **Issue:** VBAP can produce silence for directions outside speaker hull
- **Mitigation:** Zero-block detection + retarget to nearest speaker
- **Alternative:** Use DBAP (no coverage gaps)

---

## References

### Documentation

- [RENDERING.md](RENDERING.md) — Spatial renderer comprehensive docs
- [json_schema_info.md](json_schema_info.md) — LUSID & layout JSON schemas
- [LUSID/internalDocs/AGENTS.md](../LUSID/internalDocs/AGENTS.md) — LUSID-specific agent spec
- [LUSID/internalDocs/DEVELOPMENT.md](../LUSID/internalDocs/DEVELOPMENT.md) — LUSID dev notes
- [LUSID/internalDocs/xml_benchmark.md](../LUSID/internalDocs/xml_benchmark.md) — XML parser benchmarks

### External Resources

- [Dolby Atmos ADM Interoperability Guidelines](https://dolby.my.site.com/professionalsupport/s/article/Dolby-Atmos-IMF-IAB-interoperability-guidelines)
- [EBU Tech 3364: Audio Definition Model](https://tech.ebu.ch/publications/tech3364)
- [AlloLib Documentation](https://github.com/AlloSphere-Research-Group/AlloLib)
- [bwfmetaedit Tool](https://mediaarea.net/BWFMetaEdit)
- [Example ADM Files](https://zenodo.org/records/15268471)

### Version History

- **v0.5.2** (2026-02-10): XML parser migration, eliminate intermediate JSONs, benchmarking
- **v0.5.1** (2026-02-09): DirectSpeaker node type, LUSID as canonical format, xmlParser
- **v0.5.0** (2026-02-05): Initial LUSID Scene format
- **PUSH 3** (2026-01-28): LFE routing, multi-spatializer support (DBAP/VBAP/LBAP)
- **PUSH 2** (2026-01-27): Renamed VBAPRenderer → SpatialRenderer
- **PUSH 1** (2026-01-27): VBAP robustness (zero-block detection, fast-mover sub-stepping)

---

## Contact & Contribution

**Project Lead:** Lucian Parisi  
**Organization:** Cult DSP  
**Repository:** https://github.com/Cult-DSP/sonoPleth

For questions or contributions, open an issue or PR on GitHub.

---

**End of Agent Context Document**
