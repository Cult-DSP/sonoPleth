# Real-Time Spatial Audio Engine – Agent Overview

## Implementation Decisions (Updated 2026-02-24)

> These decisions were made during initial planning and override any conflicting
> assumptions in the agent sub-documents. Sub-documents remain useful for
> detailed design guidance but should be read in light of the notes below.

### Development Model – Sequential, Not Concurrent

The agent architecture segments responsibilities but **agents are implemented
one at a time, sequentially**, not in parallel. Each agent is:

1. Implemented based on the logical architecture order.
2. Tested in isolation and in integration with previously completed agents.
3. Documented (this file and the agent's own `.md` are updated).
4. The updated docs are handed to a **new context window** for the next agent task.

This ensures each step is stable before the next begins.

### Audio Backend – AlloLib

Continue using **AlloLib's AudioIO** (already a dependency via
`thirdparty/allolib`). No PortAudio or JACK for v1.

### Build System & File Location

- The real-time engine lives in **`spatial_engine/realtimeEngine/`**.
- It gets its own `CMakeLists.txt` (mirroring `spatialRender/CMakeLists.txt`).
- Links against the same AlloLib in `thirdparty/allolib`.

### Code Reuse Strategy – Header-Based Core, Reference Old CPP

- The real-time engine's core logic goes into **header files** (`.hpp`) inside
  `spatial_engine/realtimeEngine/`.
- Code may be **copied and adapted** from the offline `SpatialRenderer.cpp`
  (DBAP coordinate transforms, elevation sanitization, direction interpolation,
  LFE routing, gain logic, etc.).
- The old offline `.cpp` file stays untouched — the headers reference it in
  comments for provenance but do not `#include` it.
- Goal: the offline renderer continues to compile and work exactly as before.

### GUI – Integrate into Existing Qt GUI

- The real-time engine will be exposed as a **new mode / app** inside the
  existing PySide6 Qt GUI (`gui/`).
- This means adding a new panel or tab (alongside the current offline render
  panel) that launches and controls the real-time engine process.
- **Not** using ImGui. The AlloLib prototype's ImGui code
  (`mainplayer.hpp`) is reference only.

### Python Entry Point – `runRealtime.py`

- **`runRealtime.py`** at the project root mirrors `runPipeline.py` — it
  accepts the **same inputs** (ADM WAV file or LUSID package directory +
  speaker layout) and runs the **same preprocessing pipeline** (ADM extract
  → parse to LUSID → write scene.lusid.json).
- For **ADM sources**: preprocessing writes scene.lusid.json only (no stem
  splitting), then launches the C++ engine with `--adm` pointing to the
  original multichannel WAV for direct streaming.
- For **LUSID packages**: validates and launches with `--sources` pointing
  to the mono files folder.
- Two pipeline entry points:
  - `run_realtime_from_ADM(source_adm, layout)` — ADM preprocessing + direct streaming
  - `run_realtime_from_LUSID(package_dir, layout)` — direct launch from mono files
- CLI uses `checkSourceType()` to auto-detect ADM vs LUSID input, same
  pattern as `runPipeline.py`.
- No `--channels` parameter — channel count is derived from the speaker
  layout by the C++ engine's `Spatializer::init()`.
- Keeps everything **segmented** — the offline pipeline is never touched, and
  `runRealtime.py` can be debugged independently.
- The Qt GUI will call `runRealtime.py` via `QProcess` (same pattern as
  `pipeline_runner.py` calls `runPipeline.py`).

### Target Milestone – Replicate Pipeline in Real-Time

The first working version must:

1. Accept the same inputs as the offline pipeline: **ADM WAV file** or
   **LUSID package directory** + speaker layout JSON. Run the same
   preprocessing (ADM extract → parse → package) before launching.
2. Parse the LUSID scene (reuse existing `LUSID/` Python package — this part
   is straightforward and safe).
3. Stream the mono stems from disk (double-buffered, real-time safe).
4. Spatialize with DBAP in the AlloLib audio callback (reusing proven gain
   math from `SpatialRenderer.cpp`).
5. Route LFE to subwoofer channels (same logic as offline).
6. Output to hardware speakers via AlloLib AudioIO.
7. Be launchable from `runRealtime.py` and from the Qt GUI.

This effectively **replicates the offline pipeline but plays back in
real-time** instead of writing a WAV file.

### Agent Implementation Order

Based on the architecture's data-flow dependencies, the planned order is:

| Phase | Agent(s)                  | Why this order                                      | Status      |
| ----- | ------------------------- | --------------------------------------------------- | ----------- |
| 1     | **Backend Adapter**       | Need audio output before anything else is audible   | ✅ Complete |
| 2     | **Streaming**             | Need audio data to feed the mixer                   | ✅ Complete |
| 3     | **Pose and Control**      | Need positions before spatialization                | ✅ Complete |
| 4     | **Spatializer (DBAP)**    | Core mixing — depends on 1-3                        | ✅ Complete |
| —     | **ADM Direct Streaming**  | Optimization: skip stem splitting for ADM sources   | ✅ Complete |
| 5     | **LFE Router**            | Runs in audio callback after spatializer            | Not started |
| 6     | **Compensation and Gain** | Post-mix gain staging                               | Not started |
| 7     | **Output Remap**          | Final channel shuffle before hardware               | Not started |
| 8     | **Threading and Safety**  | Harden all inter-thread communication               | Not started |
| 9     | **GUI Agent**             | Qt integration, last because engine must work first | Not started |

> **Note:** Phases 1-4 together form the minimum audible prototype (sound
> comes out of speakers). Phases 5-7 add correctness. Phase 8 hardens
> reliability. Phase 9 adds the user interface.

### Phase 1 Completion Log (Backend Adapter)

**Files created:**

| File                                                    | Purpose                                                                                                                                                                                                                                                 |
| ------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/CMakeLists.txt`          | Build system — links AlloLib + Gamma, shares `JSONLoader`/`LayoutLoader`/`WavUtils` from `../src/`                                                                                                                                                      |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | Shared data types: `RealtimeConfig` (device settings, paths, atomic gain/playback flags) and `EngineState` (frame counter, playback time, CPU load, xrun count)                                                                                         |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Agent 8 implementation — wraps AlloLib `AudioIO` with `init()`/`start()`/`stop()`/`shutdown()` lifecycle, static C-style callback dispatches to `processBlock()`, CPU load clamping                                                                     |
| `spatial_engine/realtimeEngine/src/main.cpp`            | CLI entry point — parses `--layout`/`--scene`/`--sources` + optional args, runs monitoring loop with status display, handles SIGINT for clean shutdown                                                                                                  |
| `runRealtime.py`                                        | Python launcher — mirrors `runPipeline.py` with same input types (ADM WAV or LUSID package). Runs preprocessing pipeline, then launches C++ executable. `run_realtime_from_ADM()` / `run_realtime_from_LUSID()` entry points. Handles Ctrl+C forwarding |

**Build & test results:**

- CMake configures successfully (AlloLib + Gamma link)
- `make -j4` compiles with zero errors
- Binary runs, opens audio device (2-channel test), streams silence for 3 seconds
- SIGINT handler triggers clean shutdown (stop → close → exit 0)
- Frame counter advances correctly (~144k frames in 3s at 48kHz)
- CPU load reports 0.0% (silence — trivial callback)

**What the next phase gets:**

- A working audio callback that currently outputs silence
- `processBlock(AudioIOData&)` is the insertion point for all future agents
- `RealtimeConfig` and `EngineState` are the shared state structs
- `runRealtime.py` is ready to call from the GUI (accepts same inputs as `runPipeline.py`)

### Phase 2 Completion Log (Streaming Agent)

**Files created/modified:**

| File                                                    | Action       | Purpose                                                                                                                                                                                                           |
| ------------------------------------------------------- | ------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`       | **Created**  | Agent 1 — double-buffered per-source WAV streaming with background loader thread. Each source gets two 5-second buffers (240k frames at 48kHz). Lock-free audio-thread reads via atomic buffer states.            |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | **Modified** | Added `Streaming*` pointer and `setStreaming()`/`cacheSourceNames()` methods. `processBlock()` now reads mono blocks from each source, sums with 1/N normalization × master gain, mirrors to all output channels. |
| `spatial_engine/realtimeEngine/src/main.cpp`            | **Modified** | Now loads LUSID scene via `JSONLoader::loadLusidScene()`, creates `Streaming`, opens all source WAVs, wires into backend, starts loader thread before audio, shuts down in correct order (backend → streaming).   |
| `internalDocsMD/AGENTS.md`                              | **Modified** | Added "Real-Time Spatial Audio Engine" section with architecture, file descriptions, build instructions, streaming design, run example. Updated file structure tree and Future Work.                              |

**Design decisions:**

- **libsndfile access**: Uses `<sndfile.h>` directly (same as `WavUtils.cpp`). Available transitively through Gamma → `find_package(LibSndFile QUIET)` → exported via PUBLIC link. No new dependencies.
- **Per-source double buffers**: Each source gets independent buffers (not a shared multichannel buffer). Simpler, avoids cross-source contention.
- **5-second chunk size** (240k frames): Balances memory (~1.8 MB per source, ~63 MB for 35 sources) against seek frequency. Only needs ~20 loads per source over a 98-second piece.
- **50% preload threshold**: Background thread starts loading the next chunk when playback passes the halfway point of the active buffer. Gives 2.5 seconds of runway before the buffer switch.
- **Single loader thread**: One thread services all sources sequentially. At 2ms poll interval and ~35 sources, worst-case full scan takes <1ms. Sufficient for current source count.
- **Mutable atomics for buffer state**: `stateA`, `stateB`, `activeBuffer` are `mutable` in `SourceStream` because the audio thread may switch buffers during a logically-const `getSample()` call.

**Build & test results:**

- `cmake .. && make -j4` compiles with zero errors
- 35 sources loaded successfully (34 audio objects + LFE), each ~98 seconds (4,703,695 frames at 48kHz)
- Ran for 69.7 seconds with 2 output channels, --gain 0.1
- CPU load: 0.0% (mono sum of 35 sources + memcpy is trivially fast)
- No xruns, no underruns, no file handle leaks
- Clean SIGINT shutdown: backend stops → streaming agent closes all SNDFILE handles → exit 0
- Background loader thread joins cleanly

**What the next phase gets:**

- `StreamingAgent::getBlock(sourceName, startFrame, numFrames, outBuffer)` — lock-free mono block read from any source
- `StreamingAgent::sourceNames()` — list of all loaded source keys
- `StreamingAgent::isLFE(sourceName)` — LFE detection for routing in Phase 5
- The audio callback in `processBlock()` is the insertion point for Pose Agent (Phase 3) and Spatializer Agent (Phase 4)
- Current mono mix (equal-sum to all channels) will be replaced by per-source DBAP panning in Phase 4

### Phase 3 Completion Log (Pose — Source Position Interpolation)

**Files created/modified:**

| File                                                    | Action       | Purpose                                                                                                                                                                                                                                                            |
| ------------------------------------------------------- | ------------ | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `spatial_engine/realtimeEngine/src/Pose.hpp`            | **Created**  | Agent 2 — source position interpolation and layout-aware transforms. SLERP between LUSID keyframes, elevation sanitization (3 modes), DBAP coordinate transform. Outputs `SourcePose` vector per audio block.                                                      |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | **Modified** | Added `ElevationMode` enum (Clamp, RescaleAtmosUp, RescaleFullSphere) and `elevationMode` field to `RealtimeConfig`.                                                                                                                                               |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | **Modified** | Added `Pose*` pointer and `setPose()` method. `processBlock()` now calls `mPose->computePositions(blockCenterSec)` at step 1.5, computing per-source positions before the audio mixing loop. Positions are computed but not yet used for spatialization (Phase 4). |
| `spatial_engine/realtimeEngine/src/main.cpp`            | **Modified** | Now loads speaker layout via `LayoutLoader::loadLayout()`, creates `Pose`, calls `pose.loadScene(scene, layout)` to analyze layout and store keyframes, wires Pose into backend via `backend.setPose(&pose)`. Updated to Phase 3 banner and help text.             |
| `internalDocsMD/AGENTS.md`                              | **Modified** | Updated Phase 3 row to ✅ Complete, added `Pose.hpp` description to Key Files section.                                                                                                                                                                             |

**Design decisions:**

- **SLERP interpolation** (not linear Cartesian): Prevents direction vectors from passing through near-zero magnitude when keyframes are far apart on the sphere. Adapted from `SpatialRenderer::slerpDir()`.
- **Three elevation modes**: `Clamp` (hard clip to layout bounds), `RescaleAtmosUp` (default, maps [0,π/2] → layout), `RescaleFullSphere` (maps [-π/2,π/2] → layout). Identical to offline renderer.
- **DBAP coordinate transform**: Our system (y-forward, x-right, z-up) → AlloLib DBAP internal does `Vec3d(pos.x, -pos.z, pos.y)`, so we pre-compensate with `(x, z, -y)`. Adapted from `SpatialRenderer::directionToDBAPPosition()`.
- **Layout radius = median speaker distance**: Same calculation as offline renderer constructor. Used to scale unit direction vectors to DBAP positions at the speaker ring distance.
- **2D detection**: If speaker elevation span < 3°, all directions are flattened to the horizontal plane (z=0). Same threshold as offline renderer.
- **Fallback chain for degenerate directions**: (1) normalize raw interpolation, (2) last-good cached direction, (3) nearest keyframe direction, (4) front (0,1,0). Same logic as `SpatialRenderer::safeDirForSource()`.
- **Block-center sampling**: Position is computed at the center of each audio block (`frameCounter + bufferSize/2`) per design doc specification.
- **Pre-allocated output vector**: `mPoses` and `mSourceOrder` are allocated once at `loadScene()` time. `computePositions()` updates entries in-place — no allocation on the audio thread.
- **`std::map` for keyframe lookup**: Acceptable because `computePositions()` iterates sequentially through the pre-built source order, not doing random lookups. The map is read-only during playback.

**Build & test results:**

- `cmake --build .` compiles with zero errors
- 35 sources loaded, 54 speakers + 1 subwoofer in AlloSphere layout
- Layout analysis: median radius 5.856m, elevation [-27.7°, 32.7°], 3D mode
- Ran for 8.6 seconds with 2 output channels, --gain 0.3
- CPU load: 0.0% (SLERP + transforms for 35 sources add negligible overhead)
- No xruns, no crashes
- Clean shutdown via SIGINT

**What the next phase gets:**

- `Pose::computePositions(blockCenterTimeSec)` — called once per audio block, updates internal pose vector
- `Pose::getPoses()` → `const vector<SourcePose>&` — per-source `{name, position, isLFE, isValid}` ready for DBAP
- Positions are in DBAP-ready coordinates (pre-transformed), scaled to layout radius
- LFE sources have `isLFE=true` → route to subwoofer channels, skip DBAP
- The spatializer (Phase 4) will iterate `getPoses()` and compute per-speaker gain coefficients

### Phase 4 Completion Log (Spatializer — DBAP Spatial Panning)

**Files created/modified:**

| File                                                    | Action       | Purpose                                                                                                                                                                                                                                                                          |
| ------------------------------------------------------- | ------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/Spatializer.hpp`     | **Created**  | Agent 3 — DBAP spatial audio panning. Builds `al::Speakers` from layout, computes output channels from layout, creates `al::Dbap`, renders all sources via internal render buffer, routes LFE to subwoofers. Internal render buffer is the future Channel Remap insertion point. |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`   | **Modified** | `outputChannels` default changed from 60 → 0. Now computed from speaker layout by `Spatializer::init()`. Added documentation comment explaining the formula.                                                                                                                     |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | **Modified** | Added `Spatializer*` pointer and `setSpatializer()` method. `processBlock()` now calls Spatializer `renderBlock()` instead of the Phase 2 mono-mix fallback. Pipeline: zero outputs → Pose positions → Spatializer render → update state → CPU monitor.                          |
| `spatial_engine/realtimeEngine/src/main.cpp`            | **Modified** | Removed `--channels` CLI argument. Creates `Spatializer`, calls `init(layout)` which computes `outputChannels` into config. Backend reads the layout-derived channel count. Updated help text, banner, and config printout.                                                      |
| `internalDocsMD/AGENTS.md`                              | **Modified** | Updated Phase 4 row to ✅ Complete, added `Spatializer.hpp` description, updated run example (no `--channels`), updated file tree.                                                                                                                                               |

**Design decisions:**

- **Layout-derived output channels** (not user-specified): `outputChannels = max(numSpeakers-1, max(subwooferDeviceChannels)) + 1`. Same formula as offline `SpatialRenderer.cpp` (lines 837-842). For the Allosphere layout: `max(53, 55) + 1 = 56`. Removed the `--channels` CLI flag entirely.
- **Internal render buffer (`mRenderIO`)**: All rendering (DBAP + LFE) goes into an `al::AudioIOData` buffer sized to `outputChannels`. The copy step from render buffer → real AudioIO is the future **Channel Remap insertion point**, where logical render channels will be mapped to physical device outputs (like `channelMapping.hpp`'s `defaultChannelMap` does for the Allosphere ADM player). Currently identity mapping.
- **Nothing hardcoded to any layout**: No Allosphere-specific values anywhere. Channel count, speaker positions, subwoofer channels — all derived from the layout JSON at runtime. Works with any speaker layout.
- **Consecutive 0-based speaker channels**: Same as offline renderer. `al::Speaker` gets indices 0..N-1 for N speakers. The hardware `deviceChannel` numbers from the layout JSON (1-based, non-consecutive with gaps) are only used for subwoofer routing. Future Channel Remap will handle mapping render channels to hardware channels.
- **LFE into render buffer** (not directly to AudioIO): LFE sources write into `mRenderIO` subwoofer channels, so all audio flows through the same remap point. Consistent with the design where the copy step is the single point of channel routing.
- **Sub compensation**: `masterGain * 0.95 / numSubwoofers` — same formula as offline `SpatialRenderer::renderPerBlock()`.
- **DBAP panning**: Uses `al::Dbap::renderBuffer()` directly. Source audio is pre-multiplied by `masterGain` before DBAP accumulates into speaker channels. Focus parameter is configurable via `RealtimeConfig::dbapFocus`.

**Build & test results:**

- `cmake .. && make -j4` compiles with zero errors
- 35 sources loaded, 54 speakers + 1 subwoofer in AlloSphere layout
- Output channels derived from layout: 56 (speakers 0-53, sub at deviceChannel 55)
- AudioIO opened with 56 output channels
- Internal render buffer: 56 channels × 512 frames
- Ran for 6 seconds with `--gain 0.1`
- CPU load: 0.0% (DBAP for 35 sources × 54 speakers + LFE is trivially fast)
- No xruns, no assertion failures, no crashes
- Clean shutdown via SIGINT/kill

**What the next phase gets:**

- `Spatializer::renderBlock(io, streaming, poses, frame, numFrames)` — renders all sources into the real AudioIO output
- Layout-derived `config.outputChannels` — backend opens AudioIO with the right channel count automatically
- Internal render buffer (`mRenderIO`) — future Channel Remap agent replaces the identity copy loop with a mapping table
- LFE routing already handled (subwoofer channels from layout, no DBAP on LFE sources)
- Phases 1-4 together form the **minimum audible spatial prototype** — sound comes out of the correct speakers based on LUSID scene positions

### ADM Direct Streaming Completion Log (Optimization — Skip Stem Splitting)

**Files created/modified:**

| File                                                       | Action       | Purpose                                                                                                                                                                                                                                                                                                   |
| ---------------------------------------------------------- | ------------ | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `spatial_engine/realtimeEngine/src/MultichannelReader.hpp` | **Created**  | Shared multichannel WAV reader. Opens one SNDFILE\*, pre-allocates interleaved buffer (chunkFrames × numChannels), de-interleaves channels into per-source SourceStream double buffers. Method implementations in Streaming.hpp (after SourceStream definition).                                          |
| `spatial_engine/realtimeEngine/src/Streaming.hpp`          | **Modified** | Added `loadSceneFromADM()` (creates buffer-only SourceStreams, maps to MultichannelReader), `loaderWorkerMultichannel()` (one bulk read fills all mapped streams), `SourceStream::initBuffersOnly()` (allocates buffers without file handle), `parseChannelIndex()` (source key → 0-based channel index). |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`      | **Modified** | Added `std::string admFile` to `RealtimeConfig` for ADM direct streaming path.                                                                                                                                                                                                                            |
| `spatial_engine/realtimeEngine/src/main.cpp`               | **Modified** | Added `--adm <path>` CLI flag (mutually exclusive with `--sources`). Dispatches to `loadSceneFromADM()` or `loadScene()`. Updated help text.                                                                                                                                                              |
| `src/packageADM/packageForRender.py`                       | **Modified** | Added `writeSceneOnly()` function — writes scene.lusid.json without splitting stems.                                                                                                                                                                                                                      |
| `runRealtime.py`                                           | **Modified** | `_launch_realtime_engine()` now accepts `adm_file` parameter (uses `--adm` flag). `run_realtime_from_ADM()` calls `writeSceneOnly()` instead of `packageForRender()`, skipping stem splitting entirely.                                                                                                   |
| `internalDocsMD/AGENTS.md`                                 | **Modified** | Updated all realtime engine descriptions for dual-mode streaming, added MultichannelReader.hpp docs, updated run examples.                                                                                                                                                                                |

**Design decisions:**

- **Shared MultichannelReader (Option A)**: One SNDFILE\*, one interleaved buffer (~44MB for 48ch), shared across all sources. Much more memory-efficient than per-source multichannel handles (Option B, rejected).
- **Channel mapping derived in C++**: Source key `"11.1"` → extract number before dot → subtract 1 → channel index 10. `"LFE"` → hardcoded index 3 (standard ADM LFE position). No LUSID schema changes needed.
- **Audio thread completely untouched**: SourceStream's double-buffer + getSample/getBlock are identical in both modes. The audio callback doesn't know or care whether data came from mono files or de-interleaved multichannel.
- **Mono path preserved**: `--sources` still works exactly as before (zero regression risk).
- **Separate loader workers**: `loaderWorkerMono()` (original per-source iteration) and `loaderWorkerMultichannel()` (one bulk read per cycle) avoid any conditional branching in the hot loop.
- **`writeSceneOnly()`**: Factored out of `packageForRender()` to write just the scene.lusid.json without stem splitting. Used by the real-time ADM path; offline pipeline still uses full `packageForRender()`.

**Build & test results:**

- `cmake .. && make -j4` compiles with zero errors
- `--help` shows new `--adm` flag and updated usage
- Error handling: `--adm` + `--sources` together rejected; neither provided rejected
- LUSID package path (mono): ✅ No regression — 78 sources, allosphere layout, 56 output channels
- ADM WAV path (direct streaming): ✅ Full pipeline works — SWALE-ATMOS-LFE.wav, 24 sources mapped from 48ch ADM, translab layout, 18 output channels
- ADM pipeline skips Step 4 (stem splitting) — saves ~30-60 seconds and ~2.9GB disk I/O

---

## Critical Implementation Details for Future Context Windows

> These details were captured at the end of the ADM Direct Streaming implementation
> session to preserve context that isn't obvious from reading code alone.

### Full LFE Channel Mapping Chain (Python → C++)

The LFE channel mapping crosses two codebases and involves a hardcoded flag:

1. **Python LUSID parser** (`LUSID/src/xml_etree_parser.py` line 44):
   `_DEV_LFE_HARDCODED = True` — when `True`, any DirectSpeaker at 1-based
   ADM channel 4 is tagged as LFE (function `_is_lfe_channel()`). When `False`,
   it falls back to checking `speakerLabel` for the substring "lfe".

2. **LUSID scene JSON output**: The LFE source gets key `"LFE"` (from the
   `LFENode(id=f"{group_id}.1")` construction at line 371). In the SWALE test
   file, the DirectSpeaker group containing channel 4 has `group_id = "4"`,
   so the LFE source key becomes `"4.1"` in the scene — but its **type** is
   `"lfe"` in the JSON, so the C++ engine identifies it as LFE.

3. **C++ `parseChannelIndex()`** (`Streaming.hpp`): For source key `"LFE"` or
   any source flagged as LFE, returns hardcoded index 3 (0-based ADM channel 4).
   For normal sources like `"11.1"`, extracts the number before the dot and
   subtracts 1 → channel index 10.

4. **C++ `Spatializer.hpp`**: LFE sources (identified by `pose.isLFE`) bypass
   DBAP entirely and route directly to subwoofer channels from the speaker layout.

**Implication**: Changing `_DEV_LFE_HARDCODED` to `False` in the parser will
make LFE detection label-based, but the C++ `parseChannelIndex()` hardcoded
index 3 would still need to be made dynamic (read from the scene JSON).

### Shared Loaders — Offline ↔ Real-Time Code Sharing

The real-time engine compiles three `.cpp` files from the offline renderer's
`spatial_engine/src/` directory (see `realtimeEngine/CMakeLists.txt` lines 29-33):

```
add_executable(sonoPleth_realtime
    src/main.cpp
    ../src/JSONLoader.cpp      # Parses scene.lusid.json → SpatialData struct
    ../src/LayoutLoader.cpp    # Parses speaker_layout.json → SpeakerLayout struct
    ../src/WavUtils.cpp        # WAV I/O (used for metadata only in realtime)
)
```

These are the **exact same source files** the offline renderer
(`spatial_engine/spatialRender/`) uses. Any changes to these shared files affect
both the offline and real-time pipelines. The offline renderer is at
`spatial_engine/spatialRender/build/sonoPleth_spatial_render`.

### Circular Header Pattern (MultichannelReader ↔ Streaming)

`MultichannelReader.hpp` forward-declares `struct SourceStream` and declares
methods `deinterleaveInto(SourceStream&, ...)` and `zeroFillBuffer(SourceStream&, ...)`.
The **implementations** of these methods are placed at the very bottom of
`Streaming.hpp` (after `SourceStream` is fully defined) as inline free-standing
functions within the `MultichannelReader` class scope. This is standard C++ for
resolving circular dependencies between header-only types. If you move
`SourceStream` to its own header, the method impls should move with it.

### Known Bug — `runPipeline.py` Line 177

In the `if __name__ == "__main__"` CLI block, the LUSID branch calls:

```python
run_pipeline_from_LUSID(sourceADMFile, sourceSpeakerLayout, renderMode, createRenderAnalysis, outputRenderPath)
```

But `outputRenderPath` is **never defined** in this code path (it's only a
default parameter in `run_pipeline_from_ADM()`). This will crash with
`NameError` if someone runs the offline pipeline CLI with a LUSID package input.
**Not a real-time engine issue** — but worth knowing about.

### Future Optimization Opportunity — Skip Channel Analysis

The current `run_realtime_from_ADM()` still runs Steps 1-3 of the offline
pipeline before launching the C++ engine:

| Step | What it does                           | Time       |
| ---- | -------------------------------------- | ---------- |
| 1    | `extractMetaData()` — bwfmetaedit      | ~14s       |
| 2    | `channelHasAudio()` — scan full WAV    | ~14s       |
| 3    | `parse_adm_xml_to_lusid_scene()`       | ~1s        |
| 4    | ~~`packageForRender()` — split stems~~ | ~~30-60s~~ |
| —    | `writeSceneOnly()` — JSON only         | <1s        |

Steps 1-2 take ~28s. A future optimization could **skip step 2** (audio
channel analysis) entirely for the real-time path, since the C++ engine can
detect silence per-source during streaming. Step 1 (metadata extraction) is
still required because the LUSID parser reads the extracted XML.

### Exact File Paths for Key Artifacts

| Artifact                          | Path                                                                                        |
| --------------------------------- | ------------------------------------------------------------------------------------------- |
| Real-time C++ executable          | `spatial_engine/realtimeEngine/build/sonoPleth_realtime`                                    |
| Offline C++ executable            | `spatial_engine/spatialRender/build/sonoPleth_spatial_render`                               |
| CMakeLists (real-time)            | `spatial_engine/realtimeEngine/CMakeLists.txt`                                              |
| Shared JSONLoader                 | `spatial_engine/src/JSONLoader.cpp` / `.hpp`                                                |
| Shared LayoutLoader               | `spatial_engine/src/LayoutLoader.cpp` / `.hpp`                                              |
| Shared WavUtils                   | `spatial_engine/src/WavUtils.cpp` / `.hpp`                                                  |
| LUSID XML parser                  | `LUSID/src/xml_etree_parser.py`                                                             |
| LUSID scene model                 | `LUSID/src/scene.py`                                                                        |
| Package/scene writer              | `src/packageADM/packageForRender.py` (has both `packageForRender()` and `writeSceneOnly()`) |
| Python launcher                   | `runRealtime.py` (project root)                                                             |
| Speaker layouts dir               | `spatial_engine/speaker_layouts/`                                                           |
| Processed data (scene JSON, etc.) | `processedData/stageForRender/scene.lusid.json`                                             |
| ADM extracted metadata            | `processedData/currentMetaData.xml`                                                         |
| LUSID schema                      | `LUSID/schema/lusid_scene_v0.5.schema.json`                                                 |
| Design doc (streaming/DBAP)       | `internalDocsMD/realtime_planning/realtimeEngine_designDoc.md`                              |
| ADM streaming design doc          | `internalDocsMD/realtime_planning/agentDocs/agent_adm_direct_streaming.md`                  |

### Verified Test Commands (End-to-End)

```bash
# ADM direct streaming (SWALE test file, TransLab layout):
python runRealtime.py sourceData/SWALE-ATMOS-LFE.wav \
    spatial_engine/speaker_layouts/translab-sono-layout.json

# LUSID package (pre-split mono stems, Allosphere layout):
python runRealtime.py sourceData/lusid_package \
    spatial_engine/speaker_layouts/allosphere_layout.json 0.3 1.5 512

# C++ engine directly (ADM mode):
cd spatial_engine/realtimeEngine
./build/sonoPleth_realtime \
    --layout ../speaker_layouts/translab-sono-layout.json \
    --scene ../../processedData/stageForRender/scene.lusid.json \
    --adm ../../sourceData/SWALE-ATMOS-LFE.wav \
    --gain 0.5 --buffersize 512

# C++ engine directly (mono mode):
./build/sonoPleth_realtime \
    --layout ../speaker_layouts/allosphere_layout.json \
    --scene ../../processedData/stageForRender/scene.lusid.json \
    --sources ../../sourceData/lusid_package \
    --gain 0.1 --buffersize 512

# Build from scratch:
cd spatial_engine/realtimeEngine/build
cmake ..
make -j4
```

### Python Environment

- **Venv**: `sonoPleth/bin/python` (Python 3.12.2)
- **Activation**: `source activate.sh` (from project root)
- **Key packages**: PySide6 (GUI), lxml (legacy XML), gdown (example downloads)

---

## Architecture Overview

This real-time spatial audio engine is designed as a collection of specialized **agents**, each handling a distinct aspect of audio processing. Splitting functionality into separate agents enables **sequential, testable development** where each piece is verified before the next begins. The engine's goal is to render spatial audio with minimal latency and no glitches, even under heavy load, by carefully coordinating these components.

Key design goals include:

- **Hard Real-Time Performance:** The audio processing must complete within each audio callback frame (e.g. on a 512-sample buffer at 48 kHz, ~10.7ms per callback) to avoid underruns or glitches. Each agent is designed to do its work within strict time budgets:contentReference[oaicite:0]{index=0}.
- **Modularity:** Each agent has a clear responsibility and interface, making the system easier to maintain and allowing multiple team members to work in parallel.
- **Thread Safety:** Agents communicate via thread-safe or lock-free structures to avoid blocking the high-priority audio thread. No dynamic memory allocation or unbounded waits occur in the audio callback.
- **Scalability:** The architecture should handle multiple audio sources and outputs, scaling with available CPU cores by distributing work across threads where possible.

## Agents and Responsibilities

Below is a summary of each agent in the system and its primary responsibilities:

- **Streaming Agent:** Handles input audio streams (file, network, or live sources). It reads, decodes, and buffers audio data for each source, providing timely audio buffers to the engine.
- **Pose and Control Agent:** Manages dynamic source and listener states (positions, orientations, and control commands). It processes external controls (e.g., from GUI or network) and updates the shared scene data (source positions, activation, etc.).
- **Spatializer (DBAP) Agent:** Core audio processing module that spatializes audio using Distance-Based Amplitude Panning (DBAP). It computes gain coefficients for each source-to-speaker path and mixes source audio into the appropriate output channels based on spatial positions.
- **LFE Router Agent:** Extracts and routes low-frequency content to the Low-Frequency Effects (LFE) channel. It ensures subwoofer output is properly generated (e.g., by low-pass filtering content or routing dedicated LFE sources) without affecting main channel clarity.
- **Output Remap Agent:** Maps and adapts the engine’s output channel layout to the actual audio output hardware or desired format. This includes reordering or downmixing channels to match the device configuration (e.g., mapping internal channels to sound card output channels).
- **Compensation and Gain Agent:** Manages gain staging and loudness compensation. It applies distance attenuation curves for sources, overall volume control, and any calibration needed to ensure consistent playback levels (including compensating for multiple speaker contributions or environment tuning).
- **Threading and Safety Agent:** Oversees the multi-threading model and ensures real-time safety. It defines how threads (audio callback thread, streaming thread, control thread, GUI thread, etc.) interact and shares data, using lock-free queues or double-buffering to maintain synchronization without blocking.
- **Backend Adapter Agent:** Abstracts the audio hardware or API (e.g., CoreAudio, ASIO, ALSA, PortAudio). It provides a unified interface for the engine to output audio, handling device initialization, buffer callbacks or threads, and bridging between the engine’s audio buffers and the OS/hardware.
- **GUI Agent:** Handles the graphical user interface and user interaction. It displays system state (levels, positions, statuses) and allows the user to adjust parameters (e.g., moving sound sources, changing volumes, selecting output device) in a way that’s safe for the real-time engine.

Each agent has its own detailed document (located in `internalDocsMD/realtime_planning/agentDocs/`) describing its role, constraints, and interfaces in depth. Developers responsible for each component should refer to those documents for implementation guidance.

## Data Flow and Interactions

The spatial audio engine’s processing pipeline flows through these agents as follows:

1. **Input Stage (Streaming):** Audio sources are ingested by the **Streaming Agent** (from files or streams). Decoded audio frames for each source are buffered in memory.
2. **Control Updates:** Concurrently, the **Pose and Control Agent** receives updates (e.g., new source positions, user commands) and updates a shared scene state. This state includes each source’s position and orientation, as well as global controls like mute or gain changes.
3. **Audio Callback Processing:** On each audio callback (or frame tick):
   - The **Spatializer (DBAP) Agent** reads the latest audio frame from each source (provided by the Streaming agent via a lock-free buffer) and the latest positional data (from Pose and Control agent’s shared state). It calculates the gain for each source on each speaker using the DBAP algorithm and mixes the sources into the spatial audio output buffer (one channel per speaker output).
   - The **Compensation and Gain Agent** applies any additional per-source gain adjustments (e.g., distance attenuation beyond the geometric panning of DBAP, or user-defined gain trims) either just before mixing or during the spatialization step. It ensures the combined output stays within desired levels and prevents clipping.
   - As part of the mixing process, the **LFE Router Agent** extracts low-frequency content. For example, it might low-pass filter the summed signal or individual source channels and send those frequencies to a dedicated LFE output channel. If a source is flagged as LFE-only, this agent routes that source’s content directly to the subwoofer channel.
   - After sources are mixed into a set of output channels (including the LFE channel if present), the **Output Remap Agent** takes this intermediate multichannel output and reorders or downmixes it according to the actual output configuration. For instance, if the engine’s internal spatial layout is different from the sound card’s channel order, it swaps channels as needed. If the output device has fewer channels than produced (e.g., rendering a multichannel scene on stereo output), it downmixes appropriately (with pre-defined gains to preserve balance).
4. **Output Stage:** The **Backend Adapter Agent** interfaces with the audio hardware or API. It either provides the engine’s output buffer directly to the hardware driver or calls the system’s audio callback with the mixed/remapped audio. This agent handles specifics like buffer format (interleaved vs planar audio), sample rate conversion (if needed), and ensuring the audio thread meets the API’s timing requirements.
5. **User Interface Loop:** In parallel with the audio processing, the **GUI Agent** runs on the main/UI thread. It fetches state (e.g., current source positions, levels, streaming status) in a thread-safe manner (often via copies or atomics provided by other agents) and presents it to the user. When the user interacts (for example, moving a sound source in the UI or changing master volume), the GUI Agent passes those commands to the relevant agents (Pose and Control for position changes, or Compensation and Gain for volume adjustments, etc.) without directly interfering with the audio thread.

This data flow ensures that heavy I/O (disk or network reads, GUI operations) and control logic are offloaded to separate threads, while the time-critical audio mixing runs on the dedicated real-time thread. Communication between threads is handled by shared data structures that are updated in a controlled way.

## Real-Time Considerations

Real-time audio processing imposes strict constraints that all agents must respect for the system to function without audio dropouts:

- **No Blocking Calls in Audio Thread:** The audio callback (Spatializer and subsequent processing) must never wait on locks, file I/O, network, or any operation that could block. Agents like Streaming or GUI must use double-buffering or lock-free queues to deliver data to the audio thread, so the audio processing can run without pausing:contentReference[oaicite:1]{index=1}.
- **No Dynamic Memory Allocation in Callback:** All memory required for audio processing should be pre-allocated. For example, audio buffers for mixing and any filter coefficients should be initialized ahead of time. This avoids unpredictable delays from memory allocation or garbage collection during the audio loop.
- **Time-Bound Processing:** Each audio callback must complete within the allotted frame time (e.g., a few milliseconds). Algorithms used (such as the DBAP calculations, filtering for LFE, etc.) should be optimized (using efficient math and avoiding overly complex operations per sample). Worst-case execution time must be considered, especially when the number of sources or speakers scales up.
- **Thread Priorities:** The audio processing thread (or callback) should run at a high priority or real-time scheduling class as allowed by the OS. Background threads (streaming, control, GUI) run at lower priorities to ensure the audio thread isn’t starved of CPU time. The Threading and Safety agent will outline how to set this up and avoid priority inversions.
- **Synchronization Strategy:** Shared data (like source positions, audio buffers) is synchronized in a lock-free manner. For example, the Pose and Control agent might maintain two copies of the positions and atomically swap pointers to publish new data to the audio thread, or use atomics for small updates. The goal is to eliminate heavy locks in the audio path while still keeping data consistent.
- **Buffering and Latency:** The Streaming agent should keep a small buffer of audio data ready in advance (e.g., a few blocks) so that momentary disk or network delays don’t cause underruns. However, excessive buffering adds latency, so a balance is required. Similarly, any control or GUI commands that need to take effect (e.g., mute or move source) should be applied at frame boundaries to maintain sync.

All agents must cooperate under these constraints. If any agent fails to meet real-time requirements (for instance, if decoding audio takes too long, or a lock is held too long), the whole system can suffer an audible glitch. During development, agents should test under stress conditions to ensure timing stays within limits. Logging in the audio thread should be minimal or absent (since I/O can block); use lightweight telemetry (e.g., atomic counters or ring buffers for logs) if needed to diagnose issues without hurting performance.

## Development and Documentation Notes

This master document and the individual agent documents are living plans for the implementation. As development progresses:

- **Maintain Consistency:** Developers should keep the design aligned across documents. If an interface between agents changes, update both this overview and the respective agent docs to reflect it.
- **Progress Updates:** Each agent lead should update `internalDocsMD/agents.md` (the central index of agents) with status and any noteworthy changes. For example, mark when an agent is implemented or note decisions that affect other agents.
- **Cross-Referencing:** Ensure that references to code (e.g., `SpatialRenderer.cpp`, `mainplayer.cpp`) remain accurate. If code structure changes (like files are renamed or new helper classes are created), update the documentation accordingly.
- **Rendering Pipeline Documentation:** If the processing pipeline is modified (for instance, adding a new processing stage or altering the order of operations), update the global `RENDERING.md` document. This ensures our real-time audio rendering approach is clearly recorded for future maintainers.
- **Parallel Development Coordination:** Given that agents are being implemented in parallel, schedule regular sync-ups to discuss integration points. Use this document as a guide to verify that all assumptions (data formats, call sequences, thread responsibilities) match across agents.

By following this plan and keeping documentation up-to-date, the team can build a robust real-time spatial audio engine. This overview will serve as a roadmap, and each agent’s detailed document will provide the specific guidance needed for individual implementation and eventual integration into a cohesive system.
