# sonoPleth — Comprehensive Agent Context

**Last Updated:** February 24, 2026  
**Project:** sonoPleth - Open Spatial Audio Infrastructure  
**Lead Developer:** Lucian Parisi

---

## Table of Contents

0. [🔎 Issues Found During Duration/RF64 Investigation](#-issues-found-during-durationrf64-investigation-feb-16-2026)
1. [Project Overview](#project-overview)
2. [Architecture & Data Flow](#architecture--data-flow)
3. [Core Components](#core-components)
4. [LUSID Scene Format](#lusid-scene-format)
5. [Spatial Rendering System](#spatial-rendering-system)
6. [Real-Time Spatial Audio Engine](#real-time-spatial-audio-engine)
7. [File Structure & Organization](#file-structure--organization)
8. [Python Virtual Environment](#python-virtual-environment)
9. [Common Issues & Solutions](#common-issues--solutions)
10. [Development Workflow](#development-workflow)
11. [Testing & Validation](#testing--validation)
12. [Future Work & Known Limitations](#future-work--known-limitations)

---

## 🔎 Issues Found During Duration/RF64 Investigation (Feb 16, 2026)

> Comprehensive catalog of all issues discovered while tracing the truncated-render bug.
> Items marked ✅ are fixed. Items marked ⚠️ need code changes. Items marked ℹ️ are observations.

| #   | Status   | Severity     | Issue                                                                                | Location                                            |
| --- | -------- | ------------ | ------------------------------------------------------------------------------------ | --------------------------------------------------- |
| 1   | ✅ FIXED | **Critical** | WAV 4 GB header overflow — `SF_FORMAT_WAV` wraps 32-bit size field                   | `WavUtils.cpp`                                      |
| 2   | ✅ FIXED | **High**     | `analyzeRender.py` trusted corrupted WAV header without cross-check                  | `src/analyzeRender.py`                              |
| 3   | ✅ FIXED | **Low**      | Stale `DEBUG` print statements left in renderer                                      | `SpatialRenderer.cpp`                               |
| 4   | ✅ FIXED | **Medium**   | `masterGain` default mismatch resolved — now consistently `0.5` across code and docs | `SpatialRenderer.hpp` · `main.cpp` · `RENDERING.md` |
| 5   | ✅ FIXED | **Medium**   | `dbap_focus` now forwarded for all DBAP-based modes, including plain `"dbap"`        | `runPipeline.py`                                    |
| 6   | ✅ FIXED | **Medium**   | `master_gain` exposed in Python pipeline — passed to C++ renderer as `--master_gain` | `src/createRender.py`                               |

CURRENT PROJECT:
Switching from BWF MetaEdit to embedded EBU parsing submodules (TRACK A — COMPLETE)

### Goal

Replace the external `bwfmetaedit` dependency with **embedded EBU libraries** while keeping the existing ADM parsing + LUSID conversion behavior unchanged. **Completed.**

- Output: `processedData/currentMetaData.xml` (ADM XML string extracted from WAV via `sonopleth_adm_extract`)
- Downstream modules unchanged: `src/analyzeADM/parser.py` (lxml) and `LUSID/src/xmlParser.py` continue to operate on the extracted XML file
- This is a **plumbing swap only**. ADM support not broadened in Track A (Track B documented below as future work).

### Documentation update obligations (MANDATORY)

Whenever a change impacts the toolchain dataflow, CLI flags, on-disk artifacts, or any cross-module contract, the agent **must update documentation in the same PR**.

For Track A (embedded ADM extractor) the following docs MUST be kept consistent:

- `AGENTS.md` (this file): Track A plan + non-goals + build wiring
- `LUSID/LUSID_AGENTS.md`: pipeline diagram reflects `sonopleth_adm_extract` (embedded); note added that Track A does **not** change LUSID parsing semantics
- `toolchain_AGENTS.md`: if any contract-level path/filename/artifact changes (should not happen in Track A), update it
- `CHANGELOG_TOOLCHAIN.md`: add an entry if the contract changes (new required/optional dependency, new artifact, changed path, new validation step). If Track A only changes the preferred extractor implementation but preserves outputs, record it as an **implementation** note only if your changelog policy allows; otherwise omit.

Rules:

- Do not leave docs in a contradictory state.
- If docs disagree, `toolchain_AGENTS.md` is the contract authority; resolve conflicts by updating the other docs accordingly.
- Keep changes minimal: Track A should only require a small pipeline-diagram + note update in `LUSID_AGENTS.md`.

### Repository constraints

- **Submodules must live in `thirdparty/`**
  - `thirdparty/libbw64` (EBU BW64/RF64 container I/O)
  - `thirdparty/libadm` (EBU ADM XML model + parser)
- Keep changes minimal and compatible with the current pipeline and GUI, especially:
  - `runPipeline.py` and `gui/pipeline_runner.py`
  - file outputs under `processedData/`

### Deliverable (Track A)

1. Add EBU submodules
   - Add git submodules in `thirdparty/`:
     - `thirdparty/libbw64`
     - `thirdparty/libadm`
   - Document how to initialize them: `git submodule update --init --recursive`

2. Build an **embedded ADM XML extractor tool**
   - Create a small C++ CLI tool in the sonoPleth repo that:
     - opens a WAV/RF64/BW64 file,
     - extracts the `axml` chunk (ADM XML),
     - writes it to a file path supplied by the user (or prints to stdout).
   - Recommended placement:
     - `tools/adm_extract/` (new)
     - CMake target name: `sonopleth_adm_extract`
   - The tool should not interpret ADM semantics; it is only a chunk extractor.
   - Keep the output stable: `processedData/currentMetaData.xml` remains the same format (raw ADM XML string).

3. Wire the pipeline to use the new tool (no semantic changes)
   - Update `src/analyzeADM/extractMetadata.py` to use the embedded tool exclusively:
     - Run `sonopleth_adm_extract` to generate `processedData/currentMetaData.xml`.
     - Raise `FileNotFoundError` with a clear message if the binary is not built.
   - Preserve current filenames and directories so everything downstream stays compatible.

4. Update `init.sh` to build the tool
   - `init.sh` should:
     - initialize submodules,
     - build the new extractor tool (via CMake),
     - continue building the spatial renderer as before.
   - Keep build artifacts in an existing or clearly documented build folder (e.g., `tools/adm_extract/build/`).

5. Testing (must pass)
   - End-to-end pipeline with a known-good Atmos ADM test file:
     - produces `processedData/currentMetaData.xml`
     - produces `processedData/stageForRender/scene.lusid.json`
     - renderer runs and outputs a WAV
   - LUSID unit tests still pass:
     - `cd LUSID && python -m unittest discover -s tests -v`

### Explicit non-goals (Track A)

- Do NOT change `LUSID/src/xml_etree_parser.py` / `LUSID/src/xmlParser.py` semantics in this task.
- Do NOT attempt to add Sony 360RA parsing support here.
- Do NOT restructure the ADM→LUSID conversion.
- Do NOT require EBU ADM Toolbox (EAT) in the main build.

### AlloLib Audit & Lightweighting — ✅ COMPLETE (Feb 22, 2026)

> Full details: [`internalDocsMD/allolib-audit.md`](allolib-audit.md)

**Problem:** `thirdparty/allolib` had full git history (1,897 commits). `.git/modules/thirdparty/allolib` = **511 MB**; working tree = 38 MB.

#### Headers directly `#include`d by sonoPleth

| Header                                                                                           | Module |
| ------------------------------------------------------------------------------------------------ | ------ |
| `al/math/al_Vec.hpp`                                                                             | math   |
| `al/sound/al_Vbap.hpp` · `al_Dbap.hpp` · `al_Lbap.hpp` · `al_Spatializer.hpp` · `al_Speaker.hpp` | sound  |
| `al/io/al_AudioIOData.hpp`                                                                       | io     |

CMake link targets: `al` + `Gamma`.

#### Keep list (required today)

`sound/` · `math/` · `spatial/` · `io/al_AudioIOData` · `system/` · `external/Gamma/` · `external/json/`

#### Likely-future list (real-time audio engine)

`io/al_AudioIO` · `app/` (App, AudioDomain, SimulationDomain) · `system/al_PeriodicThread` · `protocol/` (OSC) · `scene/` (PolySynth, DynamicScene) · `external/rtaudio/` · `external/rtmidi/` · `external/oscpack/`

#### Safe to trim (graphics/UI — unused, no near-term path)

`graphics/` · `ui/` · `sphere/` · `external/glfw/` (4.5 MB) · `external/imgui/` (5.1 MB) · `external/stb/` (2.0 MB) · `external/glad/` · `external/serial/` · `external/dr_libs/`

#### Changes applied

| File                                           | Change                                                                                                                                           |
| ---------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------ |
| `.gitmodules`                                  | Added `shallow = true` to `thirdparty/allolib` — fresh clones are depth=1 automatically                                                          |
| `src/configCPP.py` `initializeSubmodules()`    | Uses `--depth 1` — `init.sh` now initializes allolib shallow (~510 MB saved)                                                                     |
| `src/configCPP.py` `initializeEbuSubmodules()` | Uses `--depth 1` — libbw64/libadm also shallow                                                                                                   |
| `scripts/shallow-submodules.sh`                | **New.** One-time idempotent script to re-shallow an existing deep clone                                                                         |
| `scripts/sparse-allolib.sh`                    | **New, opt-in only.** Sparse working-tree checkout (~14 MB saved); ⚠️ fragile with AlloLib's unconditional CMakeLists — not run by default or CI |

**Default path (`init.sh` / CI):** full working tree, shallow history. Builds correctly with no CMake changes.
**To apply to an existing deep clone:** `./scripts/shallow-submodules.sh`
**Opt-in sparse tree:** `./scripts/sparse-allolib.sh` — read warnings in script before using.
**Future option if still too heavy:** minimal fork `Cult-DSP/allolib-sono` stripping graphics/UI/sphere (see `allolib-audit.md` §4 Step 3).

### Track B (FUTURE — DO NOT IMPLEMENT YET)

**Objective:** Add a profile adaptation layer inside LUSID to accept a wider range of ADM variants (Sony 360RA, edge-case Atmos exports, etc.).
Planned work:

- Add folder: `LUSID/src/adm_profiles/`
  - `detect_profile.py`
  - `atmos_adapter.py`
  - `sony360_adapter.py`
  - `common.py` (ID handling, time parsing incl. `S48000`, polar→cart, block compaction, mute gating)
- Sony 360RA needs:
  - Opaque string IDs (hex-like suffixes such as `...0a`)
  - `rtime/duration` parsing with `S####` suffix
  - mute-block handling (gain=0 segments)
  - block compaction to avoid massive frame counts

**Status:** Document only. Await further instructions before implementing Track B.

FUTURE ISSUES
| 7 | ⚠️ UNFIXED | **Low** | Double audio-channel scan — `exportAudioActivity()` then `channelHasAudio()` ~28 s wasted | `runPipeline.py` lines 86–88 |
| 8 | ⚠️ UNFIXED | **Low** | `sys.argv[1]` accessed before bounds check → potential `IndexError` | `runPipeline.py` line 158 |
| 9 | ℹ️ NOTE | **Info** | Large interleaved buffer (~11.3 GB peak for 56ch × 566s) allocated in one shot | `WavUtils.cpp` `writeMultichannelWav()` |
| 10 | ℹ️ NOTE | **Info** | Test file exercises only `audio_object` + `LFE` paths; `direct_speaker` untested at render level | Pipeline integration tests |

additional:
|

### Details for Unfixed Items

**#4 — masterGain default mismatch**

- `SpatialRenderer.hpp` line 76: `float masterGain = 0.5;`
- `main.cpp` line 48 (help text): `"default: 0.5"`
- `main.cpp` line 101 (comment): `// Default master gain: 0.5f`
- `RENDERING.md`: updated to document `0.5f`
- **Resolution**: Default value standardized to `0.5` across all locations.

**#5 — dbap_focus forwarded for all DBAP modes**

- `runPipeline.py` now sends `--dbap_focus` for both `"dbap"` and `"dbapfocus"` modes.
- **Resolution**: Ensures DBAP focus parameter is always respected.

**#6 — master_gain exposed**

- `src/createRender.py` `runSpatialRender()` now accepts a `master_gain` parameter.
- Passed as `--master_gain` to the C++ renderer.
- **Benefit**: Users can control master gain directly from the Python pipeline.

**#7 — Double audio-channel scan**

- `runPipeline.py` calls `exportAudioActivity()` (writes `containsAudio.json`) then immediately calls `channelHasAudio()` again.
- Both functions scan the entire WAV file (~14s each for 566s file).
- **Fix**: Use result of first scan directly; remove second call.

**#8 — sys.argv bounds check**

- `runPipeline.py` line 158: `source_path = sys.argv[1]` is reached before the `if len(sys.argv) < 2:` check on line 162.
- **Fix**: Move bounds check before first access.

**#9 — Large interleaved buffer**

- `WavUtils.cpp` allocates a single `std::vector<float>` of `totalSamples × channels` (56 × 27,168,000 = 1.52 billion floats ≈ 5.67 GB).
- Combined with the per-channel buffers already in memory, peak is ~11.3 GB.
- **Mitigation idea**: Chunked/streaming write (write N blocks at a time instead of all at once).

---

## Project Overview

### Purpose

sonoPleth is a Python+C++ prototype for decoding and rendering Audio Definition Model (ADM) Broadcast WAV files (Dolby Atmos masters) to arbitrary speaker arrays using multiple spatialization algorithms.

### Key Features

- **Multi-format Input**: Dolby Atmos ADM BWF WAV files
- **Multi-spatializer Support**: DBAP (default), VBAP, LBAP
- **Arbitrary Speaker Layouts**: JSON-defined speaker positions
- **LUSID Scene Format**: v0.5.2 - canonical time-sequenced node graph for spatial audio
- **ADM Duration Preservation**: Extracts and uses authoritative duration from ADM metadata (fixes truncated renders)
- **Zero-dependency Parser**: Python stdlib only for LUSID
- **Subwoofer/LFE Handling**: Automatic routing to designated subwoofer channels
- **Comprehensive Testing**: 106 LUSID tests + renderer tests

### Technology Stack

- **Python 3.8+**: Pipeline orchestration, ADM parsing, data processing
- **C++17**: High-performance spatial audio renderer (AlloLib-based)
- **AlloLib**: Audio spatialization framework (DBAP, VBAP, LBAP)
- **sonopleth_adm_extract**: Embedded EBU/libbw64-based tool for extracting ADM XML from WAV files (built by `init.sh`)
- **CMake 3.12+**: Build system for C++ components

---

## Architecture & Data Flow

### Complete Pipeline Flow

```
ADM BWF WAV File
    │
    ├─► sonopleth_adm_extract (embedded) → currentMetaData.xml (ADM XML)
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

### Recent Architecture Changes (v0.5.2)

**Eliminated intermediate JSON files:**

- `objectData.json`, `directSpeakerData.json`, `globalData.json` no longer written to disk
- Data flows as Python dicts in memory: `parseMetadata()` → `packageForRender()` → `adm_to_lusid_scene()`
- Only `containsAudio.json` written to disk (consumed by stem splitter)

**ADM Duration Preservation (v0.5.2):**

- **Problem**: Renderer was calculating duration from longest WAV file length, causing truncated renders when keyframes ended early
- **Solution**: Extract authoritative duration from ADM `<Duration>` field, store in LUSID `duration` field
- **Impact**: Ensures full composition duration is rendered (e.g., 9:26 ADM file now renders 9:26, not truncated 2:47)
- **Implementation**: Updated `xml_etree_parser.py`, `SpatialRenderer.cpp`, `VBAPRenderer.cpp`, and JSON schema

**RF64 Auto-Selection for Large Renders (v0.5.2, Feb 16 2026):**

- **Problem**: Standard WAV format uses unsigned 32-bit data-chunk size (max ~4.29 GB). Multichannel spatial renders for long compositions (e.g., 56 channels × 566s × 48kHz × 4B = 5.67 GB) caused the header to wrap around, making readers report truncated duration (~166s instead of 566s). The C++ renderer was producing correct output all along — only the WAV header was wrong.
- **Solution**: `WavUtils::writeMultichannelWav()` auto-selects `SF_FORMAT_RF64` when audio data exceeds 4 GB. RF64 (EBU Tech 3306) uses 64-bit size fields. Falls back to standard WAV for files under 4 GB (maximum compatibility).
- **Impact**: Renders of any size are now correctly readable by downstream tools.
- **Detection**: `analyzeRender.py` cross-checks file size against header-reported duration and warns if they disagree (catches pre-fix WAV files).
- **Implementation**: Updated `WavUtils.cpp`, `analyzeRender.py`

---

## Core Components

### 1. ADM Metadata Extraction & Parsing

#### `sonopleth_adm_extract` (embedded)

- **Purpose**: Extract ADM XML from BWF WAV file using the EBU libbw64 library
- **Type**: Embedded C++ CLI tool, built by `init.sh` / `src/configCPP.py`
- **Source**: `src/adm_extract/` — compiled to `src/adm_extract/build/sonopleth_adm_extract`
- **Output**: `processedData/currentMetaData.xml`
- **Error handling**: Raises `FileNotFoundError` with instructions to run `./init.sh` if binary not built

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

### 2. LUSID Scene Format (v0.5.2)

#### `LUSID/src/scene.py` — Data Model

Core dataclasses for LUSID Scene v0.5.2:

- `LusidScene`: Top-level container (version, sampleRate, timeUnit, **duration**, metadata, frames)
- `Frame`: Timestamped snapshot of all active nodes
- **5 Node Types**:
  - `AudioObjectNode`: Spatial source with `cart` [x,y,z]
  - `DirectSpeakerNode`: ✨ **NEW** — Fixed bed channel with `speakerLabel`, `channelID`
  - `LFENode`: Low-frequency effects (routed to subs, not spatialized)
  - `SpectralFeaturesNode`: Audio analysis metadata (centroid, flux, bandwidth)
  - `AgentStateNode`: AI/agent state data (ignored by renderer)

**Zero external dependencies** — stdlib only.

#### Duration Field (v0.5.2)

- **Purpose**: Preserve authoritative ADM duration to prevent truncated renders
- **Source**: Extracted from ADM `<Duration>` field (e.g., "00:09:26.000" → 566.0 seconds)
- **Usage**: C++ renderer prioritizes LUSID `duration` over WAV file length calculations
- **Type**: `float` (seconds), optional field (defaults to -1.0 if missing)

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

- **Purpose**: Orchestrate full ADM → spatial render pipeline or render from LUSID package
- **Input Types**:
  - ADM WAV file (.wav): Full pipeline from ADM extraction to render
  - LUSID package folder: Direct render from pre-packaged LUSID scene
- **Flow (ADM)**:
  1. Check initialization (`checkInit()`)
  2. Extract ADM XML (`extractMetadata()`)
  3. Detect audio channels (`channelHasAudio()`)
  4. Parse ADM XML (`parseMetadata()`)
  5. Package for render (`packageForRender()`)
  6. Build C++ renderer (`buildSpatialRenderer()`)
  7. Run spatial render (`runSpatialRender()`)
  8. Analyze render (`analyzeRender()` — optional PDF)
- **Flow (LUSID)**:
  1. Check initialization (`checkInit()`)
  2. Build C++ renderer (`buildSpatialRenderer()`)
  3. Run spatial render (`runSpatialRender()`)
  4. Analyze render (`analyzeRender()` — optional PDF)
- **CLI Usage**:

  ```bash
  python runPipeline.py <source> [speakerLayout] [renderMode] [resolution] [createAnalysis]
  ```

  - `<source>`: Path to ADM .wav file or LUSID package folder (named "lusid_package")
  - `[speakerLayout]`: Path to speaker layout JSON (default: allosphere_layout.json)
  - `[renderMode]`: Spatializer mode - "dbap", "lbap", "lbap" (default: "dbap")
  - `[resolution]`: Focus/dispersion parameter for dbapfocus/lbap (default: 1.5)
  - `[createAnalysis]`: Create PDF analysis (true/false, default: true)

#### `runGUI.py` — Jupyter Notebook GUI (DEPRECATED)

- **Purpose**: Original interactive GUI for running pipeline with file pickers
- **Status**: Replaced by PySide6 desktop GUI in `gui/` (Feb 2026)
- **Flow**: Same as `runPipeline.py` but with UI

#### `gui/` — PySide6 Desktop GUI

- **Purpose**: Native desktop application for configuring and running the spatial render pipeline
- **Entry point**: `python gui/main.py`
- **Framework**: PySide6 (Qt 6)

**Architecture — how the GUI connects to the pipeline:**

```
┌─────────────────────────────────────────────────────────────┐
│  gui/main.py  (MainWindow)                                  │
│                                                             │
│  ┌─────────────┐   ┌──────────────┐   ┌────────────────┐   │
│  │ InputPanel   │   │ RenderPanel  │   │ PipelinePanel  │   │
│  │ - file pick  │   │ - mode       │   │ - RUN RENDER   │   │
│  │ - output path│   │ - resolution │   │ - stepper      │   │
│  │ - status rows│   │ - gain       │   │ - log list     │   │
│  │              │   │ - layout     │   │ - progress bar │   │
│  │              │   │ - analysis   │   │ - View Logs    │   │
│  └──────────────┘   └──────────────┘   └───────┬────────┘   │
│                                                │ run_clicked │
│  ┌─────────────────────────────────────────────▼────────┐   │
│  │ PipelineRunner  (gui/pipeline_runner.py)              │   │
│  │ - Wraps QProcess                                      │   │
│  │ - Launches:  python -u runPipeline.py <args...>       │   │
│  │ - Streams stdout → PipelinePanel.append_text()        │   │
│  │ - Parses STEP markers → stepper.set_step()            │   │
│  │ - Parses % progress → progress_bar                    │   │
│  └───────────────────────┬──────────────────────────────┘   │
│                          │ QProcess                          │
└──────────────────────────┼──────────────────────────────────┘
                           ▼
               ┌───────────────────────┐
               │  runPipeline.py (CLI) │
               │  sys.argv positional: │
               │   [1] source path     │
               │   [2] speaker layout  │
               │   [3] render mode     │
               │   [4] resolution      │
               │   [5] master_gain     │
               │   [6] createAnalysis  │
               │   [7] output path     │
               └───────────┬───────────┘
                           ▼
               ┌───────────────────────┐
               │ src/createRender.py   │
               │ runSpatialRender()    │
               │  → subprocess.run()   │
               │    sonoPleth_spatial_  │
               │    render --layout …  │
               │    --master_gain …    │
               └───────────────────────┘
```

**Key design decisions:**

1. **QProcess, not threading**: The GUI spawns `runPipeline.py` as a child process via `QProcess`. This keeps the pipeline completely independent — any CLI change is automatically reflected in the GUI.
2. **Positional args**: `PipelineRunner.run()` builds a `sys.argv`-style argument list matching `runPipeline.py`'s `if __name__ == "__main__"` block. The order is: source, layout, mode, resolution, master_gain, createAnalysis, outputPath.
3. **Signal wiring**: `PipelineRunner` emits `output(str)`, `step_changed(int)`, `progress_changed(int)`, `started()`, and `finished(int)` signals. `MainWindow` connects these to the panel widgets.
4. **RUN button lives in PipelinePanel**, not RenderPanel, matching the mockup hierarchy.

**Widget inventory:**

| Widget                                      | File                            | Purpose                                                                                             |
| ------------------------------------------- | ------------------------------- | --------------------------------------------------------------------------------------------------- |
| `HeaderBar`                                 | `gui/widgets/header.py`         | Title bar, subtitle, init status dot                                                                |
| `InputPanel`                                | `gui/widgets/input_panel.py`    | File picker, output path, status badges with ✓ marks                                                |
| `RenderPanel`                               | `gui/widgets/render_panel.py`   | Mode dropdown, resolution slider+pill, gain slider+pill, layout dropdown, SwitchToggle for analysis |
| `PipelinePanel`                             | `gui/widgets/pipeline_panel.py` | RUN RENDER button, stepper, progress bar, structured log list, "View Full Logs" → `LogModal`        |
| `Stepper`                                   | `gui/widgets/stepper.py`        | Alternating circle/diamond markers with connector lines, "Analyze" end label                        |
| `SwitchToggle`                              | `gui/widgets/switch_toggle.py`  | iOS-style animated toggle (QPainter, QPropertyAnimation)                                            |
| `LogModal`                                  | `gui/widgets/log_modal.py`      | Full raw log dialog                                                                                 |
| `RadialBackground`                          | `gui/background.py`             | Concentric geometry + central lens gradient                                                         |
| `apply_card_shadow` / `apply_button_shadow` | `gui/utils/effects.py`          | `QGraphicsDropShadowEffect` helpers                                                                 |

**Styling**: `gui/styles.qss` — Qt-compatible stylesheet (no CSS `box-shadow`, no `-apple-system` font). Font stack: SF Pro Display → Helvetica Neue → Arial.

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

### JSON Structure (v0.5.2)

```json
{
  "version": "0.5",
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "duration": 566.0,
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

### Top-Level Fields

- **version**: LUSID format version (currently "0.5")
- **sampleRate**: Sample rate in Hz (must match audio files)
- **timeUnit**: Time unit for keyframes: `"seconds"` (default), `"samples"`, or `"milliseconds"`
- **duration**: **NEW in v0.5.2** - Total scene duration in seconds (from ADM metadata). Critical fix: ensures renderer uses authoritative ADM duration instead of calculating from WAV file lengths. Prevents truncated renders when keyframes end before composition end.
- **metadata**: Optional metadata object (source format, original duration string, etc.)
- **frames**: Array of time-ordered frames containing spatial nodes

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

## Real-Time Spatial Audio Engine

### Overview

The real-time engine (`spatial_engine/realtimeEngine/`) performs live spatial audio rendering. It reads the same LUSID scene files and source WAVs as the offline renderer but streams them through an audio device in real-time instead of rendering to a WAV file.

**Status:** Phase 4 complete (DBAP spatialization with layout-derived channels). Phase 5 (LFE Router) skipped — LFE pass-through already implemented in Spatializer. Phases 6–9 pending.

### Architecture — Agent Model

The engine follows a sequential agent architecture where each agent handles one stage of the audio processing chain. All agents share `RealtimeConfig` and `EngineState` structs defined in `RealtimeTypes.hpp`.

| Phase | Agent                         | Status      | File                     |
| ----- | ----------------------------- | ----------- | ------------------------ |
| 1     | **Backend Adapter** (Agent 8) | ✅ Complete | `RealtimeBackend.hpp`    |
| 2     | **Streaming** (Agent 1)       | ✅ Complete | `Streaming.hpp`          |
| 3     | **Pose** (Agent 2)            | ✅ Complete | `Pose.hpp`               |
| 4     | **Spatializer** (Agent 3)     | ✅ Complete | `Spatializer.hpp`        |
| —     | **ADM Direct Streaming**      | ✅ Complete | `MultichannelReader.hpp` |
| 5     | LFE Router (Agent 4)          | ⏭️ Skipped  | — (handled in Spatializer) |
| 6     | Compensation Agent (Agent 5)  | Not started | —                        |
| 7     | Output Remap (Agent 6)        | Not started | —                        |
| 8     | Transport Agent (Agent 7)     | Not started | —                        |
| 9     | Control Surface (Agent 9)     | Not started | —                        |

### Key Files

- **`RealtimeTypes.hpp`** — Shared data types: `RealtimeConfig` (sample rate, buffer size, layout-derived output channels, paths including `admFile` for ADM direct streaming, atomic gain/playing/shouldExit), `EngineState` (atomic frame counter, playback time, CPU load, source/speaker counts). Output channel count is computed from the speaker layout by `Spatializer::init()` — not user-specified.

- **`RealtimeBackend.hpp`** — Agent 8. Wraps AlloLib's `AudioIO` for audio I/O. Registers a static callback that dispatches to `processBlock()`. Phase 4: zeroes all output channels, calls Pose to compute positions, calls Spatializer to render DBAP-panned audio into all speaker channels including LFE routing to subwoofers.

- **`Streaming.hpp`** — Agent 1. Double-buffered disk streaming for audio sources. Supports two input modes: (1) **mono file mode** — each source opens its own mono WAV file (for LUSID packages via `--sources`), and (2) **ADM direct streaming mode** — a shared `MultichannelReader` opens one multichannel ADM WAV file, reads interleaved chunks, and de-interleaves individual channels into per-source buffers (for ADM sources via `--adm`, skipping stem splitting). In both modes, each source gets two pre-allocated 5-second buffers (240k frames at 48kHz). A background loader thread monitors consumption and preloads the next chunk into the inactive buffer when the active buffer is 50% consumed. The audio callback reads from buffers using atomic state flags — no locks on the audio thread. Key methods: `loadScene()` (mono mode), `loadSceneFromADM()` (multichannel mode), `loaderWorkerMono()`, `loaderWorkerMultichannel()`.

- **`MultichannelReader.hpp`** — Shared multichannel WAV reader for ADM direct streaming. Opens one `SNDFILE*` for the entire multichannel ADM WAV, pre-allocates a single interleaved read buffer (`chunkFrames × numChannels` floats, ~44MB for 48ch), and maintains a channel→SourceStream mapping. Called by the Streaming loader thread to read one interleaved chunk and distribute de-interleaved mono data to each mapped source's double buffer. Method implementations (`deinterleaveInto()`, `zeroFillBuffer()`) are at the bottom of `Streaming.hpp` (after `SourceStream` is fully defined) following standard C++ circular-header patterns.

- **`Pose.hpp`** — Agent 2. Source position interpolation and layout-aware transforms. At each audio block, SLERP-interpolates between LUSID keyframes to compute each source's direction, sanitizes elevation for the speaker layout (clamp, rescale-atmos-up, or rescale-full-sphere modes), and applies the DBAP coordinate transform (direction × layout radius → position). Outputs a flat `SourcePose` vector consumed by the spatializer. All math is adapted from `SpatialRenderer.cpp` with provenance comments.

- **`Spatializer.hpp`** — Agent 3. DBAP spatial audio panning. Builds `al::Speakers` from the speaker layout (radians → degrees, consecutive 0-based channels), computes `outputChannels` from the layout (`max(numSpeakers-1, max(subDeviceChannels)) + 1` — same formula as offline renderer), creates `al::Dbap` with configurable focus. At each audio block: spatializes non-LFE sources via `renderBuffer()` into an internal render buffer, routes LFE sources directly to subwoofer channels with `masterGain * 0.95 / numSubwoofers` compensation, then copies the render buffer to the real AudioIO output. The copy step is the future insertion point for Channel Remap (mapping logical render channels to physical device outputs). Nothing is hardcoded to any specific speaker layout. All math adapted from `SpatialRenderer.cpp` with provenance comments.

- **`main.cpp`** — CLI entry point. Parses arguments (`--layout`, `--scene`, `--sources` or `--adm`, `--samplerate`, `--buffersize`, `--gain`), loads LUSID scene via `JSONLoader`, loads speaker layout via `LayoutLoader`, creates Streaming (opens source WAVs — either individual mono files via `--sources` or one multichannel ADM via `--adm`), creates Pose (analyzes layout, stores keyframes), creates Spatializer (builds speakers, computes output channels from layout, creates DBAP), creates RealtimeBackend (opens AudioIO with layout-derived channel count), wires all agents together, starts audio, runs monitoring loop, handles SIGINT for clean shutdown. `--sources` and `--adm` are mutually exclusive; no `--channels` flag — channel count is always derived from the layout.

- **`runRealtime.py`** — Python launcher that mirrors `runPipeline.py`. Accepts the same inputs: ADM WAV file or LUSID package directory + speaker layout. For ADM sources, runs preprocessing (extract ADM metadata → parse to LUSID scene → write scene.lusid.json only — **no stem splitting**) then launches the C++ engine with `--adm` pointing to the original multichannel WAV. For LUSID packages, validates the package and launches with `--sources` pointing to the mono files folder. Provides `run_realtime_from_ADM()` and `run_realtime_from_LUSID()` entry points. Uses `checkSourceType()` to detect input type from CLI. No `--channels` parameter — channel count derived from speaker layout by the C++ engine.

### Build System

```bash
cd spatial_engine/realtimeEngine/build
cmake ..
make -j4
```

The CMake config (`CMakeLists.txt`) compiles `src/main.cpp` plus shared loaders from `spatial_engine/src/` (JSONLoader, LayoutLoader, WavUtils). Links `al` (AlloLib) and `Gamma` (DSP library, provides libsndfile transitively).

**Dependencies:** No new dependencies beyond what AlloLib/Gamma already provide. libsndfile comes through Gamma's CMake (`find_package(LibSndFile)` → exports via PUBLIC link).

### Run Examples

```bash
# Mono file mode (from LUSID package with pre-split stems):
./build/sonoPleth_realtime \
    --layout ../speaker_layouts/allosphere_layout.json \
    --scene ../../processedData/stageForRender/scene.lusid.json \
    --sources ../../sourceData/lusid_package \
    --gain 0.1 --buffersize 512

# ADM direct streaming mode (reads from original multichannel WAV):
./build/sonoPleth_realtime \
    --layout ../speaker_layouts/translab-sono-layout.json \
    --scene ../../processedData/stageForRender/scene.lusid.json \
    --adm ../../sourceData/SWALE-ATMOS-LFE.wav \
    --gain 0.5 --buffersize 512
```

Output channels are derived from the speaker layout automatically (e.g., 56 for the AlloSphere layout: 54 speakers at channels 0-53 + subwoofer at device channel 55).

### Streaming Agent Design

**Two input modes:**

1. **Mono file mode** (`--sources`): Each source opens its own mono WAV file. The loader thread iterates sources independently, loading the next chunk for each.
2. **ADM direct streaming mode** (`--adm`): A shared `MultichannelReader` opens one multichannel WAV. The loader thread reads one interleaved chunk (all channels) and de-interleaves into per-source buffers in a single pass. Eliminates the ~30-60 second stem splitting step entirely.

**Double-buffer pattern:** Each source has two float buffers (A and B). Buffer states cycle through `EMPTY → LOADING → READY → PLAYING`. The audio thread reads from the `PLAYING` buffer. When playback crosses 50% of the active buffer, the loader thread fills the inactive buffer with the next chunk from disk.

**Buffer swap is lock-free:** The audio thread atomically switches `activeBuffer` when it detects the other buffer is `READY` and contains the needed data. The mutex in `SourceStream` only protects `sf_seek()`/`sf_read_float()` calls and is only ever held by the loader thread.

**Source naming convention:** Source key (e.g., `"11.1"`) maps to WAV filename `"11.1.wav"` in mono mode. In ADM mode, source key `"11.1"` → ADM channel 11 → 0-based index 10. LFE source key `"LFE"` → channel index 3 (hardcoded standard ADM LFE position).

---

## File Structure & Organization

### Project Root

```
sonoPleth/
├── activate.sh                      # Reactivate venv (use: source activate.sh)
├── init.sh                          # One-time setup (use: source init.sh)
├── requirements.txt                 # Python dependencies (inc. lxml)
├── runPipeline.py                   # Main CLI entry point
├── runGUI.py                        # Jupyter notebook GUI (DEPRECATED)
├── README.md                        # User documentation
├── gui/                             # PySide6 desktop GUI (primary UI)
│   ├── main.py                      # App entry: MainWindow, QSS loader
│   ├── styles.qss                   # Qt stylesheet (light mode)
│   ├── background.py                # Radial geometry + lens focal point
│   ├── pipeline_runner.py           # QProcess wrapper for runPipeline.py
│   ├── agentGUI.md                  # GUI spec + implementation status
│   ├── widgets/
│   │   ├── header.py                # Title bar
│   │   ├── input_panel.py           # File picker, status badges
│   │   ├── render_panel.py          # Render settings (mode, gain, etc.)
│   │   ├── pipeline_panel.py        # RUN button, stepper, log list
│   │   ├── stepper.py               # Circle/diamond step markers
│   │   ├── switch_toggle.py         # iOS-style toggle
│   │   └── log_modal.py             # Raw log viewer
│   └── utils/
│       └── effects.py               # Drop shadow helpers
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
│   │   └── extractMetadata.py      # ADM extractor wrapper (sonopleth_adm_extract)
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
│   └── realtimeEngine/              # Real-time spatial audio engine
│       ├── CMakeLists.txt           # Build config (links AlloLib + Gamma)
│       ├── build/                   # Build output dir
│       └── src/
│           ├── main.cpp                # CLI entry point (--sources or --adm)
│           ├── RealtimeTypes.hpp       # Shared data types (config, state)
│           ├── RealtimeBackend.hpp     # Agent 8: AlloLib AudioIO wrapper
│           ├── Streaming.hpp           # Agent 1: double-buffered WAV streaming
│           ├── MultichannelReader.hpp  # ADM direct streaming (de-interleave)
│           ├── Pose.hpp                # Agent 2: source position interpolation
│           └── Spatializer.hpp         # Agent 3: DBAP spatial panning
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

- `sonopleth_adm_extract` — embedded ADM metadata extractor (built by `init.sh`; see `src/adm_extract/`)
- `cmake`, `make`, C++ compiler — C++ renderer build

---

## Common Issues & Solutions

### ADM Parsing

**Issue:** `ModuleNotFoundError: No module named 'lxml'`  
**Solution:** Activate venv: `source activate.sh`

**Issue:** `sonopleth_adm_extract` binary not found  
**Solution:** Run `./init.sh` to build the embedded ADM extractor.

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

**Issue:** Render duration appears truncated when read back (e.g., 166s instead of 566s)  
**Cause:** Standard WAV format header overflow. Audio data exceeds 4 GB (common with 54+ speaker layouts and compositions over ~7 minutes at 48kHz). The 32-bit data-chunk size wraps around modulo 2³², causing readers to see fewer samples than were actually written. The audio data on disk is correct — only the header is wrong.

**Fix:** `WavUtils.cpp` now auto-selects RF64 format for files over 4 GB. `analyzeRender.py` now detects and warns about this condition.

**Issue:** ⚠️ Master gain is louder than documented default  
**Cause:** `SpatialRenderer.hpp` declares `float masterGain = 0.5` but `main.cpp` help text and `RENDERING.md` both say `0.25`. Users relying on docs get 2× louder output.  
**Solution:** Pending — decide on correct default, then update hpp + docs to match.

### Pipeline Orchestration

**Issue:** ⚠️ `dbap_focus` ignored in default `"dbap"` render mode  
**Cause:** `runPipeline.py` only passes `--dbap_focus` when `renderMode == "dbapfocus"`, not when `renderMode == "dbap"`.  
**Solution:** Pending — forward `--dbap_focus` for all DBAP-based modes.

**Issue:** ⚠️ `master_gain` not controllable from Python pipeline  
**Cause:** `src/createRender.py` `runSpatialRender()` never builds a `--master_gain` CLI argument. The parameter is only accessible via direct C++ invocation.  
**Solution:** Pending — add `master_gain` kwarg to `createRender.py` and forward to C++.

**Issue:** ⚠️ Double audio-channel scan wastes ~28 seconds  
**Cause:** `runPipeline.py` calls `exportAudioActivity()` (writes `containsAudio.json`) then immediately calls `channelHasAudio()` again — both scan the entire WAV.  
**Solution:** Pending — use result of first scan directly; remove redundant second call.

**Issue:** ⚠️ `sys.argv[1]` accessed before bounds check  
**Cause:** `runPipeline.py` line 158 reads `sys.argv[1]` before the `if len(sys.argv) < 2:` guard on line 162. Crashes with `IndexError` when run with no arguments.  
**Solution:** Pending — move bounds check before first access.

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
5. \*\*Test with various layouts`
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

- [ ] **Fix masterGain default mismatch** ⚠️ _[Issues list #4]_
  - `SpatialRenderer.hpp` declares `0.5`, `main.cpp` help/comments say `0.25`, `RENDERING.md` says `0.25f`
  - Decide canonical default, update all three locations to match

- [ ] **Expose `master_gain` in Python pipeline** ⚠️ _[Issues list #6]_
  - Add `master_gain` parameter to `createRender.py` `runSpatialRender()`
  - Forward as `--master_gain` CLI argument to C++ executable

- [ ] **Forward `dbap_focus` for all DBAP modes** ⚠️ _[Issues list #5]_
  - `runPipeline.py` currently only sends `--dbap_focus` for `"dbapfocus"` mode
  - Should forward for plain `"dbap"` mode too (C++ supports it regardless)

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

- [ ] **Chunked/streaming WAV write** ℹ️ _[Issues list #9]_
  - `WavUtils.cpp` currently allocates a single interleaved buffer of `totalSamples × channels` (~5.67 GB for 56ch × 566s)
  - Peak memory ~11.3 GB with per-channel buffers on top
  - Write in chunks (e.g., 1s blocks) to reduce peak allocation

- [ ] **Eliminate double audio-channel scan** ⚠️ _[Issues list #7]_
  - `runPipeline.py` calls `exportAudioActivity()` then `channelHasAudio()` — both scan the full WAV (~14s each)
  - Use result of first scan directly; remove redundant second call (~28s savings)

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

- [ ] **Real-time rendering engine — remaining phases**
  - Phases 1-4 complete (Backend, Streaming, Pose, Spatializer) + ADM Direct Streaming optimization
  - Phase 5: LFE Router — ⏭️ Skipped (LFE pass-through already implemented in Spatializer.hpp)
  - Phase 6: Compensation Agent — per-channel gain/delay trim
  - Phase 7: Output Remap — logical-to-physical channel mapping (using layout `deviceChannel` fields)
  - Phase 8: Transport Agent — seek, loop, scene reload
  - Phase 9: Control Surface — GUI integration (Qt)

- [ ] **AlloLib player bundle**
  - Package renderer + player + layout loader as single allolib app
  - GUI for layout selection, playback control
  - Integration with AlloSphere dome

#### Pipeline Improvements

- [ ] **Fix `sys.argv` bounds check ordering** ⚠️ _[Issues list #8]_
  - `runPipeline.py` line 158 reads `sys.argv[1]` before the `len(sys.argv) < 2` guard
  - Move bounds check before first access to prevent `IndexError`

- [ ] **Add `direct_speaker` integration test coverage** ℹ️ _[Issues list #10]_
  - Current test file (ASCENT-ATMOS-LFE) only exercises `audio_object` + `LFE` paths
  - Need a test with active DirectSpeaker bed channels to exercise that renderer path

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
- [libbw64 (EBU)](https://github.com/ebu/libbw64)
- [Example ADM Files](https://zenodo.org/records/15268471)

### Known Issues

#### ✅ RESOLVED — WAV 4 GB Header Overflow (2026-02-16)

**Root Cause:** Standard WAV uses an unsigned 32-bit data-chunk size (max 4,294,967,295 bytes). A 56-channel × 566s × 48 kHz × 4-byte render produces 6,085,632,000 bytes, which wraps to 1,790,664,704 → readers see 166.54 s instead of 566 s. The C++ renderer was correct all along — only the WAV header was wrong.

**Fix:** `WavUtils.cpp` now auto-selects `SF_FORMAT_RF64` when data exceeds 4 GB. `analyzeRender.py` cross-checks file size vs header.

#### ⚠️ OPEN — masterGain Default Mismatch

- `SpatialRenderer.hpp` declares `float masterGain = 0.5;`
- `main.cpp` help text and comments say `0.25`
- `RENDERING.md` documents `0.25f`
- **Impact:** Users relying on documentation get 2× louder output than expected.

#### ⚠️ OPEN — runPipeline.py Robustness

- `sys.argv[1]` accessed before bounds check (line 158 vs check on line 162)
- Double audio-channel scan wastes ~28 s per run (calls both `exportAudioActivity()` and `channelHasAudio()`)
- **LUSID CLI branch bug (line 177):** `run_pipeline_from_LUSID()` is called with `outputRenderPath` which is never defined in the `__main__` block — will crash with `NameError` if a LUSID package is passed via CLI

#### ℹ️ NOTE — Large Interleaved Buffer Allocation

- `WavUtils.cpp` allocates a single `std::vector<float>` of `totalSamples × channels` (~5.67 GB for the 56-channel test case, ~11.3 GB peak with per-channel buffers).
- Works on high-memory machines but may OOM on constrained systems.
- **Mitigation:** Chunked/streaming write (future work).

## OS-Specific C++ Tool Configuration

**Updated:** February 23, 2026

sonoPleth now supports cross-platform C++ tool building with OS-specific implementations. The configuration system automatically detects the operating system and routes to the appropriate build scripts.

### Architecture

- **Router:** `src/config/configCPP.py` - Tiny OS detection and import routing
- **POSIX (Linux/macOS):** `src/config/configCPP_posix.py` - Uses `make -jN` for builds
- **Windows:** `src/config/configCPP_windows.py` - Uses `cmake --build --config Release` for Visual Studio compatibility

### Key Differences

| Aspect               | POSIX                                 | Windows                                         |
| -------------------- | ------------------------------------- | ----------------------------------------------- |
| Build Command        | `make -jN`                            | `cmake --build . --parallel N --config Release` |
| Executable Extension | None                                  | `.exe`                                          |
| Repo Root Resolution | `Path(__file__).resolve().parents[2]` | `Path(__file__).resolve().parents[2]`           |

### Functions

All OS implementations provide the same API:

- `setupCppTools()` - Main entry point, orchestrates all builds
- `initializeSubmodules()` - Initialize AlloLib submodule
- `initializeEbuSubmodules()` - Initialize libbw64/libadm submodules
- `buildAdmExtractor()` - Build ADM XML extraction tool
- `buildSpatialRenderer()` - Build spatial audio renderer

### Build Products

- **ADM Extractor:** `src/adm_extract/build/sonopleth_adm_extract[.exe]`
- **Spatial Renderer:** `spatial_engine/spatialRender/build/sonoPleth_spatial_render[.exe]`

### Integration

- **Init Script:** `init.sh` imports `src.config.configCPP` (updated path)
- **Idempotent:** All builds check for existing executables before rebuilding
- **Error Handling:** Clear error messages for missing dependencies (CMake, compilers)

### Version History

- **v0.5.2** (2026-02-16): RF64 auto-selection for large renders, WAV header overflow fix, analyzeRender.py file-size cross-check, debug print cleanup
- **v0.5.2** (2026-02-13): Duration field added to LUSID scene, ADM duration preservation, XML parser migration, eliminate intermediate JSONs
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
