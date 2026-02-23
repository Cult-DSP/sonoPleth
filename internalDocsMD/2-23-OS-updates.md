# OS-Specific C++ Tool Configuration Updates

**Date:** February 23, 2026  
**Author:** Lucian Parisi  
**Status:** Complete

## Overview

sonoPleth has been updated to support cross-platform C++ tool building with OS-specific implementations. This enables Windows developers to build and use the spatial audio toolchain natively, while maintaining full compatibility with existing POSIX (Linux/macOS) workflows.

## Changes Made

### 1. Router Implementation (`src/config/configCPP.py`)

**Purpose:** Tiny OS detection and import routing module.

**Before:**

```python
# Complex conditional logic mixed with implementation
```

**After:**

```python
import os

if os.name == "nt":
    from .configCPP_windows import setupCppTools
else:
    from .configCPP_posix import setupCppTools

__all__ = ["setupCppTools"]
```

**Benefits:**

- Clean separation of concerns
- Easy to extend for additional platforms
- Minimal maintenance overhead

### 2. POSIX Configuration Fixes (`src/config/configCPP_posix.py`)

**Key Changes:**

- Added `get_repo_root()` helper function for consistent path resolution
- Updated all `Path(__file__).parent.parent.resolve()` calls to use `get_repo_root()`

**Helper Function:**

```python
def get_repo_root() -> Path:
    # __file__ = repo/src/config/configCPP_posix.py
    return Path(__file__).resolve().parents[2]
```

**Files Updated:**

- `initializeSubmodules()` - Line ~67
- `buildSpatialRenderer()` - Line ~124
- `runCmake()` - Line ~165
- `initializeEbuSubmodules()` - Line ~247
- `buildAdmExtractor()` - Line ~323

**Rationale:** Correct repo root depth after moving from `src/configCPP.py` to `src/config/configCPP.py`.

### 3. Windows Implementation (`src/config/configCPP_windows.py`)

**Replaced:** Stub implementation with full Windows-native build system.

**Key Windows-Specific Features:**

- Executable naming: `exe("name")` → `"name.exe"`
- Build command: `cmake --build . --parallel N --config Release`
- Visual Studio multi-config support with `--config Release`
- Same submodule initialization and build logic as POSIX

**Build Functions Implemented:**

- `setupCppTools()` - Main orchestration
- `initializeSubmodules()` - AlloLib submodule init
- `initializeEbuSubmodules()` - EBU library submodules
- `buildAdmExtractor()` - ADM extraction tool
- `buildSpatialRenderer()` - Spatial renderer
- `runCmake()` - CMake configuration and build

### 4. Init Script Update (`init.sh`)

**Change:** Updated import path on line 49.

**Before:**

```bash
from src.configCPP import setupCppTools
```

**After:**

```bash
from src.config.configCPP import setupCppTools
```

**Rationale:** Reflects new module location in `src/config/` directory.

## Technical Details

### Platform Detection

Uses Python's `os.name` for reliable OS detection:

- `"nt"` → Windows
- Other → POSIX (Linux/macOS)

### Build System Differences

| Platform | Build Tool | Command                                         | Config  |
| -------- | ---------- | ----------------------------------------------- | ------- |
| POSIX    | make       | `make -jN`                                      | N/A     |
| Windows  | cmake      | `cmake --build . --parallel N --config Release` | Release |

### Path Resolution

Both platforms use identical repo root calculation:

```python
Path(__file__).resolve().parents[2]
```

This works because both files are at `repo/src/config/configCPP_*.py`.

### Executable Products

| Tool             | POSIX Path                                                    | Windows Path                                                      |
| ---------------- | ------------------------------------------------------------- | ----------------------------------------------------------------- |
| ADM Extractor    | `src/adm_extract/build/sonopleth_adm_extract`                 | `src/adm_extract/build/sonopleth_adm_extract.exe`                 |
| Spatial Renderer | `spatial_engine/spatialRender/build/sonoPleth_spatial_render` | `spatial_engine/spatialRender/build/sonoPleth_spatial_render.exe` |

## Testing & Validation

### POSIX (macOS/Linux)

- Verified existing builds still work
- Confirmed repo root resolution accuracy
- Tested submodule initialization

### Windows

- Implemented full build pipeline
- Added Visual Studio generator support
- Tested CMake configuration and parallel builds

### Cross-Platform

- Router correctly imports appropriate module
- `init.sh` uses updated import path
- All functions maintain same API

## Benefits

1. **Windows Support:** Native Windows development without WSL/Cygwin
2. **Maintainability:** Separate OS-specific logic, shared router
3. **Consistency:** Same build products and behavior across platforms
4. **Extensibility:** Easy to add support for additional platforms
5. **Reliability:** Proper path resolution prevents build failures

## Future Considerations

- **Testing:** Add CI/CD for both POSIX and Windows builds
- **Documentation:** Update user guides with Windows setup instructions
- **Dependencies:** Document CMake and Visual Studio requirements for Windows
- **Performance:** Compare build times between make and cmake approaches

## Files Modified

- `src/config/configCPP.py` - New router implementation
- `src/config/configCPP_posix.py` - Added helper, fixed paths
- `src/config/configCPP_windows.py` - Full implementation
- `init.sh` - Updated import path
- `internalDocsMD/AGENTS.md` - Added OS configuration section
- `internalDocsMD/2-23-OS-updates.md` - This documentation

## Verification Commands

### Test Router Import

```bash
# POSIX
python -c "from src.config.configCPP import setupCppTools; print('Import successful')"

# Windows (PowerShell)
python -c "from src.config.configCPP import setupCppTools; print('Import successful')"
```

### Test Repo Root Resolution

```bash
python -c "from src.config.configCPP_posix import get_repo_root; print(get_repo_root())"
# Should print: /path/to/sonoPleth
```

### Test Init Script

```bash
./init.sh
# Should complete C++ tool setup without import errors
```

---

**End of OS Updates Documentation**
