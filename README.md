Cult DSP — Open Spatial Audio Infrastructure  
Lead Developer: Lucian Parisi
# ADM Decoder Prototype

This repository contains a Python prototype for exploring and decoding
Audio Definition Model Broadcast WAV (ADM BWF) files — atmos masters —
with mapping to the AlloSphere speaker layout.

## Quick Start

### First Time Setup

Run this **once** to set up everything:

```bash
git clone https://github.com/lucianpar/sonoPleth.git
cd sonoPleth
source init.sh
```

**Important:** Use `source init.sh` (not `./init.sh`) to ensure the virtual environment activates in your current shell.

The `init.sh` script will:

- Create a Python virtual environment (`sonoPleth/`)
- Install all Python dependencies
- Install `bwfmetaedit` (via Homebrew)
- Initialize git submodules (AlloLib)
- Build the VBAP renderer
- Activate the virtual environment automatically

After `source init.sh` completes, you'll see `(sonoPleth)` in your terminal prompt

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
- `speaker_layout.json` - Speaker layout JSON (default: `vbapRender/allosphere_layout.json`)
- `true|false` - Create PDF analysis of render (default: `true`)

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

## Manual Setup

If `init.sh` fails, you can set up manually:

```bash
# 1. Create virtual environment
python3 -m venv sonoPleth

# 2. Install Python dependencies
sonoPleth/bin/pip install -r requirements.txt

# 3. Install bwfmetaedit
brew install bwfmetaedit

# 4. Initialize submodules and build renderer
sonoPleth/bin/python -c "from utils.configCPP import setupCppTools; setupCppTools()"
```

## Utilities

- `init.sh` - One-time setup script (creates venv, installs dependencies, builds C++ tools, activates venv)
- `activate.sh` - Reactivates the virtual environment in new terminal sessions (use: `source activate.sh`)
- `utils/getExamples.py` - Downloads example ADM files
- `utils/deleteData.py` - Cleans processed data directory

## Pipeline Overview

1. **Check Initialization** - Verify all dependencies are installed
2. **Setup C++ Tools** - Install bwfmetaedit, initialize AlloLib submodule, build VBAP renderer
3. **Extract Metadata** - Use bwfmetaedit to extract ADM XML from WAV
4. **Parse ADM** - Convert ADM XML to internal data structure
5. **Analyze Audio** - Detect which channels contain audio content
6. **Package for Render** - Split audio stems and create spatial instruction JSON
7. **VBAP Render** - Generate multichannel spatial audio using VBAP
8. **Analyze Render** - Create PDF with dB analysis of each output channel

## VBAP Renderer Options

The VBAP renderer supports multiple render resolution modes:

| Mode | Description | Recommended |
|------|-------------|-------------|
| `block` | Compute direction once per block (default, blockSize=64) | ✓ Yes |
| `sample` | Compute direction for every sample (highest accuracy) | For critical work |
| `smooth` | *Deprecated* - gain interpolation can cause artifacts | No |

### Command Line Usage

```bash
./sonoPleth_vbap_render <input.json> <layout.json> <output.wav> [options]

Options:
  --render_resolution <mode>  Set render mode: block (recommended), sample, smooth
  --block_size <n>            Set block size for block mode (default: 64)
```

### JSON Time Units

The spatial trajectory JSON supports an explicit `timeUnit` field:

```json
{
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "sources": [...]
}
```

Valid values: `"seconds"` (default), `"samples"`, `"milliseconds"`

For detailed documentation, see:
- [RENDERING.md](spatial_engine/vbapRender/RENDERING.md) - Full rendering documentation
- [json_schema_info.md](spatial_engine/vbapRender/json_schema_info.md) - JSON schema reference

## Testing Files

Example ADM files: https://zenodo.org/records/15268471

## Requirements

### Essential

- **Python 3.8+** - Core runtime for the Python components
- **CMake and build tools** - Required to build the VBAP spatial audio renderer

### Platform-specific installation methods

- **macOS**: Homebrew for easy `bwfmetaedit` installation (`brew install bwfmetaedit`)
- **Windows/Linux**: Manual installation of `bwfmetaedit` from [MediaArea website](https://mediaarea.net/BWFMetaEdit)

### Required external tool

- **bwfmetaedit** - Extracts ADM metadata from WAV files. Install via:
  - macOS: `brew install bwfmetaedit`
  - Other platforms: Download from https://mediaarea.net/BWFMetaEdit
