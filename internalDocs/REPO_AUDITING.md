# Repository Auditing — Internal Reference

**Last Updated:** May 12, 2026

---

## Repository Cleanup Audit

> `REPO_CLEANUP_AUDIT.md` — Phase 1 complete, February 23, 2026.

### Phase 1: Completed Removals ✅

Files removed February 23, 2026:
1. `internal/LUSID/src/old_XML_parse/` — archived XML parser
2. `internal/LUSID/tests/old_XML_parse/` — archived XML parser tests
3. `src/analyzeADM/old_XML_parse/` — archived XML parser
4. `src/packageADM/old_schema/` — old schema files
5. `src/packageADM/createRenderInfo.py` — deprecated wrapper
6. `source/spatial_engine/src/renderer/rendererUtils-TODO/` — empty directory
7. `src/adm_extract/build/` — build artifacts

All 79 LUSID tests pass post-removal.

### Items Marked to Leave

Per human review annotations in the audit:
- `data/processedData/` — all contents (render outputs, scene data, debug artifacts)
- `data/sourceData/` — 19 GB ADM WAV test files
- `quickCommands.txt` — useful dev command examples
- `internal/LUSID/tests/fixtures/sample_scene_v0.5.json` — test fixtures
- `internalDocs/` — all internal docs (superseded by current consolidation)

### Repository Size Context

| Item | Size |
|---|---|
| `data/sourceData/` | 19 GB (ADM WAV test files) |
| `data/processedData/` | 11 GB (rendered multichannel audio) |
| Python venv (removed Phase 6) | was 1.6 GB |
| Source code + submodules | ~122 MB |

Note: Python virtual environment was removed in Phase 6 (2026-03-31) as part of the C++ refactor. All Python build tooling and GUI code has been removed.

### Phase 2: Remaining Candidates (review needed)

- `.DS_Store` files throughout repo — `find . -name ".DS_Store" -delete`
- Stale submodule entries: `thirdparty/libbw64` and `thirdparty/libadm` are in `.gitmodules` at the spatialroot level but not used by any active `CMakeLists.txt` (`spatialroot_adm_extract` archived in Phase 3). Not harmful but confusing.

---

## AlloLib Dependency Audit

> `allolib-audit.md` — February 22, 2026. Pre-Track B cleanup. See also AGENTS.md §AlloLib Audit & Lightweighting.

### Weight Analysis

| Item | Size | Notes |
|---|---|---|
| `thirdparty/allolib/` working tree | 38 MB | All source files |
| `.git/modules/internal/cult-allolib` | **511 MB** | Full history — legacy deep-clone baseline |
| `external/Gamma` | 2.3 MB | DSP library — **linked by spatialroot** |
| `external/rtaudio` | 1.3 MB | Audio I/O — needed for realtime |
| `external/imgui` | 5.1 MB | Dear ImGui — not used by spatialroot directly |
| `external/glfw` | 4.5 MB | GLFW window — now used by GUI |
| `external/stb` | 2.0 MB | Image loading — not used |
| `external/dr_libs` | 744 KB | Audio decoding — not used (spatialroot uses libsndfile) |

**Primary problem:** submodule git history dwarfed the working tree. Shallow init addresses the history cost; modular CMake now addresses the unnecessary build surface.

**Status: Implemented.** `init.sh` initializes `internal/cult-allolib` with `--depth 1`, and `source/scripts/shallow-submodules.sh` remains the opt-in cleanup path for older deep clones. The current supported footprint reduction comes from the slimmed `cult-allolib` CMake defaults, not sparse checkout.

### AlloLib Modules — Usage Classification

**Required today (KEEP):**

| Module | What's used |
|---|---|
| `sound/` | `al_Vbap.hpp`, `al_Dbap.hpp`, `al_Lbap.hpp`, `al_Spatializer.hpp`, `al_Speaker.hpp` + `.cpp` files |
| `math/` | `al_Vec.hpp` + transitive math headers (Mat, Quat, Constants, Functions, Spherical) |
| `spatial/` | `al_Pose.hpp` (pulled by `al_Spatializer.hpp`) |
| `io/AudioIOData` | `al_AudioIOData.hpp` (buffer/channel descriptor) |
| `io/File + system/types` | low-level file, socket, thread, timing, variant, and utility support used by the kept runtime modules |
| `external/Gamma` | DSP primitives, linked by `spatialroot_spatial_render` |
| `external/json` | nlohmann/json — used by AlloLib CMake and spatialroot JSONLoader |

**Needed for realtime path (already in use):**
- `io/al_AudioIO.hpp` + `al_AudioIO.cpp` — live audio device
- `external/rtaudio/` — cross-platform audio device backend
- `protocol/al_OSC.hpp` — OSC parameter server
- `external/oscpack/` — OSC protocol impl
- `ui/al_Parameter.hpp` etc. — AlloLib parameter system for OSC

### Verified Active Spatial Root Usage

Direct Spatial Root include/link evidence from the May 2026 audit:

- `RealtimeBackend.hpp`, `main.cpp`, and `Spatializer.hpp` use `al/io/al_AudioIO.hpp`
- `EngineSession.cpp` uses `al/ui/al_Parameter.hpp` and `al/ui/al_ParameterServer.hpp`
- `Pose.hpp` uses `al/math/al_Vec.hpp`
- `LayoutLoader.hpp` uses `al/sound/al_Speaker.hpp`
- `Spatializer.hpp` and `SpatialRenderer.hpp` use the DBAP/VBAP/LBAP speaker/spatializer stack
- realtime and offline CMake targets both link the compatibility target `al` plus `Gamma` and `SndFile::sndfile`

**Disabled in the supported minimal build:**
- `graphics/` sources and targets — no OpenGL dependency in the engine/offline path
- `app/` sources and targets — not built by default in the slim `cult-allolib` profile
- windowing / ImGui wrappers (`al_Window*`, `al_Imgui*`) — not built unless graphics is enabled
- `sphere/` sources — not built in the current minimal path
- `external/glfw/` within `cult-allolib` — not required for the minimal engine/offline build
- `external/imgui/` within `cult-allolib` — not required; Spatial Root uses its own GUI stack under `thirdparty/imgui`
- `external/stb/` — 2.0 MB (image loading)
- `external/dr_libs/` — 744 KB (audio decoding; spatialroot uses libsndfile)
- `external/glad/` — OpenGL loader
- `external/serial/` — removed from the verified minimal build path
- `external/cpptoml/` — removed from the verified minimal build path

### Minimal Module Split Status

The May 2026 lightweighting pass formalized a Spatial Root-oriented module split in `internal/cult-allolib/CMakeLists.txt`:

- `cult_allolib_core`
- `cult_allolib_file`
- `cult_allolib_audio`
- `cult_allolib_osc`
- `cult_allolib_spatial`
- `cult_allolib_params`
- compatibility target `al`

Default supported profile:

- ON: audio, osc, parameters, spatial, file, timing, types
- OFF: graphics, app, VR, MIDI, examples, tests, docs, serial, cpptoml-backed config helpers

Result: the minimal Spatial Root engine/offline build no longer configures AlloLib graphics/app stacks or pulls `serial` / `cpptoml` into the verified build path.

### Quarantined or Removed Content

Removed outright in the audited pass:

- `internal/cult-allolib/.github/workflows/`
- `internal/cult-allolib/doc/`
- `internal/cult-allolib/examples/`
- `internal/cult-allolib/test/`
- stray placeholder files under `internal/cult-allolib/include/al/sphere/` and `src/sphere/`

Still intentionally quarantined behind OFF-by-default options:

- graphics/windowing code
- app/domain infrastructure
- VR helpers
- vendored example/test trees that are outside the verified minimal build path

### Verification

Verification runs from the May 2026 audit:

```bash
cmake -S . -B /private/tmp/spatialroot-audit-build2 -DSPATIALROOT_BUILD_GUI=OFF -DSPATIALROOT_BUILD_CULT=OFF
cmake --build /private/tmp/spatialroot-audit-build2 --target spatialroot_realtime spatialroot_spatial_render -j4
```

Observed result:

- clean configure succeeded
- `spatialroot_realtime` built successfully
- `spatialroot_spatial_render` built successfully
- no OpenGL/GLFW/AlloLib graphics requirement remained in the minimal engine/offline path

### Lightweighting Plan

| Tactic | Saving | Risk | Status |
|---|---|---|---|
| `--depth 1` shallow clone | large `.git/modules` reduction for new setups | None | **✅ Done** — `init.sh` |
| Slim `cult-allolib` CMake defaults | avoids graphics/app/VR/examples/tests/docs build overhead | Low | **✅ Done** — supported path |
| Sparse checkout working tree | modest source-tree reduction only | Medium (stale + fragile) | **Not recommended** |

**To apply to an existing deep clone:** `./source/scripts/shallow-submodules.sh`  
**Current recommendation:** use the slimmed `cult-allolib` defaults and avoid sparse checkout unless you are doing one-off local experiments.
