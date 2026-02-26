Cult DSP — Open Spatial Audio Infrastructure  
Lead Developer: Lucian Parisi

# ADM Decoder Prototype

This repository contains a Python prototype for exploring and decoding
Audio Definition Model Broadcast WAV (ADM BWF) files — atmos masters —
with mapping to speaker arrays using multiple spatializers (DBAP, VBAP, LBAP).

## Quick Start

### First Time Setup

Run this **once** to set up everything:

**macOS/Linux:**

```bash
git clone https://github.com/lucianpar/sonoPleth.git
cd sonoPleth
source init.sh
```

**Windows (PowerShell):**

```powershell
git clone https://github.com/lucianpar/sonoPleth.git
cd sonoPleth
.\init.ps1
```

**Important:** Use `source init.sh` (not `./init.sh`) on macOS/Linux to ensure the virtual environment activates in your current shell. On Windows, the PowerShell/Command Prompt scripts handle activation automatically.

**If you need to reactivate the virtual environment in a new PowerShell session:**

```powershell
cd sonoPleth
. .\sonoPleth\bin\Activate.ps1
```

You'll know the virtual environment is active when you see `(sonoPleth)` in your PowerShell prompt.

The setup scripts will:

- Create a Python virtual environment (`sonoPleth/`)
- Install all Python dependencies
- Initialize git submodules (AlloLib, libbw64, libadm)
- Build the embedded ADM extractor (`sonopleth_adm_extract[.exe]`)
- Build the Spatial renderer (supports DBAP, VBAP, LBAP)
- Activate the virtual environment automatically

After setup completes, you'll see `(sonoPleth)` in your terminal prompt.

### Get Example Files

```bash
python utils/getExamples.py
```

This downloads example Atmos ADM files for testing.

### Run the Pipeline

```bash
python runPipeline.py sourceData/driveExampleSpruce.wav
```

**Command options:**

```bash
# Default mode (uses example file)
python runPipeline.py

# With custom ADM file
python runPipeline.py path/to/your_file.wav

# Full options
python runPipeline.py <adm_wav_file> <speaker_layout.json> <true|false>
```

**Arguments:**

- `adm_wav_file` - Path to ADM BWF WAV file (Atmos master)
- `speaker_layout.json` - Speaker layout JSON (default: `spatialRender/allosphere_layout.json`)
- `true|false` - Create PDF analysis of render (default: `true`)

---

## Spatial Rendering

The project supports three spatializers from AlloLib:

- **DBAP** (default) - Distance-Based Amplitude Panning, works with any layout
- **VBAP** - Vector Base Amplitude Panning, best for layouts with good 3D coverage
- **LBAP** - Layer-Based Amplitude Panning, designed for multi-ring layouts

See [`internalDocsMD/RENDERING.md`](internalDocsMD/RENDERING.md) for full documentation.

### Rebuilding the Renderers

If you need to rebuild after code changes:

#### Spatial Renderer (Offline Pipeline)

```bash
rm -rf spatial_engine/spatialRender/build
python -c "from src.config.configCPP import buildSpatialRenderer; buildSpatialRenderer()"
```

Or manually:

```bash
cd spatial_engine/spatialRender
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

#### Realtime Engine

```bash
rm -rf spatial_engine/realtimeEngine/build
python -c "from src.config.configCPP import buildRealtimeEngine; buildRealtimeEngine()"
```

Or manually:

```bash
cd spatial_engine/realtimeEngine
mkdir -p build && cd build
cmake ..
make -j$(nproc)
```

---

## Opening a New Terminal Session

**IMPORTANT:** If you close your terminal and come back later, you need to reactivate the virtual environment:

```bash
cd sonoPleth
source activate.sh
```

You'll know the virtual environment is active when you see `(sonoPleth)` at the start of your terminal prompt.

**Why?** Virtual environments only last for your current terminal session. This is standard Python practice and keeps your system Python clean and isolated from project dependencies.

---

## Troubleshooting

### "ModuleNotFoundError" or "command not found: python"

**Problem:** The virtual environment is not active.

**Solution:** Run this in your terminal:

```bash
source activate.sh
```

Check that you see `(sonoPleth)` in your prompt. If you don't see it, the venv is not active.

### Dependency or build errors

If you encounter dependency errors:

```bash
rm .init_complete
source init.sh
```

### Rebuilding the Renderers

After making changes to C++ source files (`spatial_engine/src/` or `spatial_engine/realtimeEngine/src/`), rebuild the renderers:

**Option 1: Force rebuild (recommended)**

```bash
# Rebuild spatial renderer
rm -rf spatial_engine/spatialRender/build/
python -c "from src.config.configCPP import buildSpatialRenderer; buildSpatialRenderer()"

# Rebuild realtime engine
rm -rf spatial_engine/realtimeEngine/build/
python -c "from src.config.configCPP import buildRealtimeEngine; buildRealtimeEngine()"
```

**Option 2: Clean and rebuild**

```bash
# Clean and rebuild spatial renderer
cd spatial_engine/spatialRender/build/
make clean
make -j$(sysctl -n hw.ncpu)
cd ../../../

# Clean and rebuild realtime engine
cd spatial_engine/realtimeEngine/build/
make clean
make -j$(sysctl -n hw.ncpu)
cd ../../../
```

**Option 3: Manual CMake build**

```bash
# Full manual rebuild of spatial renderer
cd spatial_engine/spatialRender/
rm -rf build/
mkdir build && cd build/
cmake ..
make -j$(sysctl -n hw.ncpu)

# Full manual rebuild of realtime engine
cd spatial_engine/realtimeEngine/
rm -rf build/
mkdir build && cd build/
cmake ..
make -j$(sysctl -n hw.ncpu)
```

The built executables will be at:

- `spatial_engine/spatialRender/build/sonoPleth_spatial_render`
- `spatial_engine/realtimeEngine/build/sonoPleth_realtime`

## Manual Setup

If `init.sh` fails, you can set up manually:

```bash
# 1. Create virtual environment
python3 -m venv sonoPleth

# 2. Install Python dependencies
sonoPleth/bin/pip install -r requirements.txt

# 3. Initialize submodules and build all C++ tools (ADM extractor + renderer)
sonoPleth/bin/python -c "from src.config.configCPP import setupCppTools; setupCppTools()"
```

## Utilities

- `init.sh` - One-time setup script (creates venv, installs dependencies, builds C++ tools, activates venv)
- `activate.sh` - Reactivates the virtual environment in new terminal sessions (use: `source activate.sh`)
- `utils/getExamples.py` - Downloads example ADM files
- `utils/deleteData.py` - Cleans processed data directory
- `src/config/configCPP.py` - C++ build utilities (use `buildSpatialRenderer()` and `buildRealtimeEngine()` to rebuild renderers)

## Pipeline Overview

1. **Check Initialization** - Verify all dependencies are installed
2. **Setup C++ Tools** - Initialize AlloLib, libbw64, libadm submodules; build embedded ADM extractor and spatial renderer
3. **Extract Metadata** - Use embedded `sonopleth_adm_extract` to extract ADM XML from WAV
4. **Parse ADM** - Convert ADM XML to internal data structure
5. **Analyze Audio** - Detect which channels contain audio content
6. **Package for Render** - Split audio stems (X.1.wav naming) and build LUSID scene (scene.lusid.json)
7. **Spatial Render** - Generate multichannel spatial audio (renderer reads LUSID scene directly)
8. **Analyze Render** - Create PDF with dB analysis of each output channel

## Spatial Renderer Options

The spatial renderer supports multiple spatializers (DBAP, VBAP, LBAP) and render resolution modes:

| Mode     | Description                                              | Recommended       |
| -------- | -------------------------------------------------------- | ----------------- |
| `block`  | Compute direction once per block (default, blockSize=64) | ✓ Yes             |
| `sample` | Compute direction for every sample (highest accuracy)    | For critical work |
| `smooth` | _Deprecated_ - gain interpolation can cause artifacts    | No                |

### Command Line Usage

```bash
./sonoPleth_spatial_render <input.json> <layout.json> <output.wav> [options]

Options:
  --render_resolution <mode>  Set render mode: block (recommended), sample, smooth
  --block_size <n>            Set block size for block mode (default: 64)
  --spatializer <type>        Set spatializer: dbap (default), vbap, lbap
```

### JSON Time Units

The LUSID scene JSON supports an explicit `timeUnit` field:

```json
{
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "sources": [...]
}
```

Valid values: `"seconds"` (default), `"samples"`, `"milliseconds"`

For detailed documentation, see:

- [RENDERING.md](internalDocsMD/RENDERING.md) - Full rendering documentation
- [json_schema_info.md](internalDocsMD/json_schema_info.md) - JSON schema reference

## Testing Files

Example ADM files: https://zenodo.org/records/15268471

## Requirements

### Essential

- **Python 3.8+** - Core runtime for the Python components
- **CMake 3.12+** - Required to build the spatial audio renderer and embedded ADM extractor (C++17)
- **Build tools** - make, clang/gcc compiler toolchain

### Platform-specific notes

- **macOS**: Fully supported via `./init.sh`
- **Windows/Linux**: CMake + make/ninja required to build `sonopleth_adm_extract`

### ADM extraction

- **Primary**: `sonopleth_adm_extract` (embedded, built by `init.sh`) — no external install needed
