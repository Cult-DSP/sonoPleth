# Realtime Engine Bug Audit — 2026-04-01

## How to use this document

This document is the **primary onboarding reference** for any agent or developer working on the realtime engine. Read this before touching code. Do not re-derive things already documented here.

### Bug numbering convention

Each distinct root-cause problem is assigned a **bug number** (integer). Each patch attempt for that bug is a **sub-number**:

- `8` — Bug 8: the problem description
- `8.1` — First patch attempt for Bug 8 (may be a plan, partial fix, or full fix)
- `8.2` — Second attempt (if 8.1 was insufficient or revised)

Bugs are listed **newest first**. Closed bugs follow open ones.

---

## Agent onboarding — read before anything else

### What this engine does

The realtime engine reads a `.lusid.json` scene file, streams audio from a multitrack WAV or ADM file, and renders spatial audio to a multichannel audio device in real time using DBAP panning. It is a C++ process. A Dear ImGui / GLFW desktop GUI (`gui/imgui/`) owns the engine and drives it via a direct C++ session API. OSC (port 9009) is a secondary control path available for external tools.

**No Python GUI exists.** The Python/PySide6 GUI was replaced in the C++ refactor. Any references to `gui/realtimeGUI/*.py` in older documents are obsolete.

---

### Build and run

```bash
# Run init.sh once before the first build (initializes submodules):
./init.sh

# Build everything (engine + offline + cult-transcoder + GUI):
./build.sh --gui

# Build engine only (faster for engine-only changes):
./build.sh --engine-only

# Launch the GUI (the normal way to run the engine):
./run.sh

# Run engine directly from the CLI (headless testing):
./build/spatial_engine/realtimeEngine/spatialroot_realtime \
    --layout spatial_engine/speaker_layouts/translab-sono-layout.json \
    --scene  processedData/stageForRender/SWALE-ATMOS-LFE.lusid.json \
    --adm    sourceData/SWALE-ATMOS-LFE.wav \
    --device "MOTU Pro Audio"     # omit to use system default
    --list-devices                # enumerate output devices then exit
```

Note: `./engine.sh` is a legacy standalone build script that outputs to `spatial_engine/realtimeEngine/build/`. Prefer `./build.sh` — it uses the unified CMake build at `build/` and is the canonical path.

### Test content

All test content lives in `sourceData/`. Corresponding LUSID scenes are in `processedData/stageForRender/`. File names match:

| Content | ADM WAV | LUSID scene |
|---|---|---|
| Swale | `sourceData/SWALE-ATMOS-LFE.wav` | `processedData/stageForRender/SWALE-ATMOS-LFE.lusid.json` |
| Ascent | `sourceData/ASCENT-ATMOS-LFE.wav` | `processedData/stageForRender/ASCENT-ATMOS-LFE.lusid.json` |
| Eden | `sourceData/EDEN-ATMOS-MIX-LFE.wav` | `processedData/stageForRender/EDEN-ATMOS-MIX-LFE.lusid.json` |
| Canyon | `sourceData/CANYON-ATMOS-LFE.wav` | *(no pre-built scene — transcode via GUI TRANSCODE tab or cult-transcoder)* |
| 360RA | `sourceData/360RA_test.wav` | `processedData/stageForRender/360RA_test.lusid.json` |

Speaker layouts: `spatial_engine/speaker_layouts/translab-sono-layout.json` (primary test), `allosphere_layout.json` (56-ch).

---

### Key source files and their roles

**C++ engine:**

| File | Role |
|---|---|
| `spatial_engine/realtimeEngine/src/Spatializer.hpp` | Core DBAP render loop. Proximity guard (Pass 1 soft zone + Pass 2 hard floor), fast-mover sub-stepping, Phase 6 mix trims (spkMix/lfeMix), Phase 7 device copy, Phase 14 diagnostic measurement points. **Most bugs touch this file.** |
| `spatial_engine/realtimeEngine/src/Pose.hpp` | Keyframe interpolation pipeline: SLERP → `safeDirForSource` → `sanitizeDirForLayout` → `directionToDBAPPosition`. Computes `SourcePose::position` (block center), `positionStart`, `positionEnd`. Pose is known clean — do not suspect it without evidence. |
| `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` | Audio callback controller. Owns `ControlSmooth` (50 ms exponential smoother for gain/focus), `processBlock()` Steps 1–6, per-block timing, CPU meter. All config values reach the audio thread exclusively via `mSmooth`. |
| `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp` | Shared types: `RealtimeConfig` atomics (written by OSC/session API, snapshotted by audio thread). `EngineState` diagnostic counters. `EngineOptions`, `SceneInput`, `LayoutInput`, `RuntimeParams`, `EngineStatus`, `DiagnosticEvents` structs. Threading model documented in header comments — read them. |
| `spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp` | Public session API. Wraps all subsystems. `main.cpp` and the GUI both use this exclusively. Contains the OSC `ParameterServer` and `OscParams` inner struct. |
| `spatial_engine/realtimeEngine/src/main.cpp` | Headless CLI entry point. Parses args, builds `EngineOptions`/`SceneInput`/`LayoutInput`/`RuntimeParams`, calls `EngineSession` API, runs the monitoring loop. |
| `spatial_engine/realtimeEngine/src/Streaming.hpp` | Per-source audio streaming from the multichannel ADM WAV. `parseChannelIndex()` maps source name → 0-based ADM channel: `"N.1" → N-1`, `"LFE" → 3`. |

**C++ GUI:**

| File | Role |
|---|---|
| `gui/imgui/src/App.hpp` / `App.cpp` | ImGui + GLFW desktop app. Owns `EngineSession`. `onStart()` always calls `resetRuntimeToDefaults()` before launching (fix for Bug 4 pattern). Controls engine via direct C++ setters (`mSession->setMasterGain()` etc.) — not OSC. Two tabs: ENGINE and TRANSCODE. |
| `gui/imgui/src/SubprocessRunner.hpp/.cpp` | Runs the `cult-transcoder` subprocess for ADM WAV → LUSID scene conversion. |
| `gui/imgui/src/main.cpp` | GLFW window setup, render loop, calls `App::tick()` each frame. |

---

### EngineSession API

All startup and runtime control goes through `EngineSession`. The GUI and headless `main.cpp` both use this.

**Startup sequence (must be called in this order):**
```cpp
EngineSession session;
session.configureEngine(opts);    // audio device, sample rate, buffer size, OSC port
session.loadScene(sceneIn);       // LUSID scene + source paths
session.applyLayout(layoutIn);    // speaker layout JSON + optional remap CSV
session.configureRuntime(params); // gain, focus, mix defaults
session.start();                  // launches audio thread + loader thread
```

**Runtime control (safe to call after `start()`):**
```cpp
session.setMasterGain(float);      // 0.0–3.0
session.setDbapFocus(float);       // 0.2–5.0
session.setSpeakerMixDb(float);    // ±10 dB
session.setSubMixDb(float);        // ±10 dB
session.setAutoCompensation(bool);
session.setElevationMode(ElevationMode);
session.setPaused(bool);
```

**Polling (call from main thread each frame/loop):**
```cpp
EngineStatus   status = session.queryStatus();   // time, CPU, RMS, masks, counters
DiagnosticEvents ev   = session.consumeDiagnostics(); // reloc/DOM/CLUSTER event latches
session.update();                                // processes pending focus compensation
```

---

### OSC parameter reference

Port: **9009** (default, set via `--osc_port`). Group prefix: `realtime`. All params are also writable via the direct C++ setter API above.

| OSC address | Type | Range | Default | Notes |
|---|---|---|---|---|
| `/realtime/gain` | float | 0.1–3.0 | 0.5 | Master gain |
| `/realtime/focus` | float | 0.2–5.0 | 1.5 | DBAP rolloff exponent |
| `/realtime/speaker_mix_db` | float | -10–10 | 0.0 | Post-DBAP main trim |
| `/realtime/sub_mix_db` | float | -10–10 | 0.0 | Post-DBAP sub trim |
| `/realtime/auto_comp` | float (bool) | 0/1 | 0 | Focus auto-compensation |
| `/realtime/paused` | float (bool) | 0/1 | 0 | Pause/resume transport |
| `/realtime/elevation_mode` | float (int) | 0/1/2 | 0 | 0=RescaleAtmosUp, 1=RescaleFullSphere, 2=Clamp |

---

### Threading model

Three threads. This matters for any change touching shared state.

| Thread | Role | Owns |
|---|---|---|
| **Main thread** | Startup, monitoring loop, `session.update()`, GUI render loop. | Agent object lifetimes, `EngineSession` setters |
| **Audio thread** | AlloLib RT callback at real-time priority. Runs `processBlock()` every buffer. **Must never allocate, lock, or do I/O.** | `EngineState` writes, `mRenderIO`, `mLastGoodDir` (Pose) |
| **Loader thread** | Background WAV streaming (`Streaming::loaderWorker()`). Reads next chunk from disk into inactive double-buffer slot. | `SNDFILE*` via `fileMutex`, inactive buffer write |

**Key rules:** Config atomics are written by OSC/main, snapshotted by audio thread once per block (relaxed loads — one-block lag is inaudible). Audio thread never writes back to config. `Spatializer::computeFocusCompensation()` must only be called from the main thread, never while audio is running.

---

### Speaker layout quick reference (translab-sono-layout.json)

- 16 main speakers (channels 0–15), 2 subs (channels 16, 17)
- Ring 0 (ch 0–7): elevation ~1.75° (near-horizontal)
- Ring 1 (ch 8–15): elevation ~38–44° (elevated)
- Layout is 3D (`mLayoutIs2D = false`) → `RescaleAtmosUp` fires for all sources with z > 0
- Median radius ~5.065 m (`mLayoutRadius`, used as DBAP position scale)
- Sub channels 16–17 always excluded from `domMask` and DBAP gain output

---

### DBAP render path summary (one source, one block)

```
getBlock() → onset-fade ramp (Bug 1.1) → masterGain multiply
→ [fast-mover? angleDelta > 0.25 rad]
    YES → 4 sub-chunks, each:
            lerp positionStart→positionEnd → renorm to mLayoutRadius
            → Pass 1 soft guard → Pass 2 hard guard
            → renderBuffer into mFastMoverScratch → accumulate into mRenderIO
    NO  → [guard-blend? (Bug 9.1 — planned)]
            YES → 4 sub-chunks: first 2 use mPrevSafePos[si], last 2 use safePos
            NO  → Pass 1 soft guard → Pass 2 hard guard → single renderBuffer(mRenderIO, safePos)
→ Phase 6: spkMix trim (mains), lfeMix trim (subs)
→ Phase 14 pre-copy measurement (render-bus active mask, DOM/CLUSTER latches)
→ Phase 7: identity copy mRenderIO → io.outBuffer()
→ Phase 14 post-copy measurement (device active mask)
```

---

### Proximity guard structure (as of 2026-03-28)

Both the normal path and each fast-mover sub-step apply two passes. The coordinate flip `rp = Vec3f(pos.x, -pos.z, pos.y)` is applied before the guard and undone after — all guard math is in DBAP-internal space, not pose space.

**Pass 1 — soft zone** (`kGuardSoftZone = 0.45 m`): single scan. Symmetric parabolic bump: `u = (dist - kMin) / (kSoft - kMin)`, `push = zoneWidth * u * (1-u)`. Zero effect at both boundaries, peak at midpoint. No iteration.

**Pass 2 — hard floor** (`kMinSpeakerDist = 0.15 m`): convergence loop (`kGuardMaxIter = 4`). Scans all speakers; if any source is within 0.15 m, pushes out and restarts. Breaks when no speaker fires.

`guardFiredForSource` is set when Pass 2 fires. `speakerProximityCount` is incremented per source-block where the hard floor fires.

---

### Diagnostic output key

```
t=42.5s  CPU=23%  rDom=0xffff  dDom=0xffff  rBus=0x3ffff  dev=0x3ffff  mainRms=0.031  subRms=0.004  Xrun=0  NaN=0  SpkG=14  PLAYING
[RELOC-RENDER]   t=42.5s  mask: 0xffff → 0x3ffff       ← sub channels appearing (expected)
[DOM-RENDER]     t=42.5s  dom:  0xffff → 0x3ffff        ← dominant-set change (mains only)
[CLUSTER-RENDER] t=42.5s  top4: 0x0123 → 0x0456        ← 2+ of top-4 mains changed
```

| Field | Meaning |
|---|---|
| `rBus` / `dev` | Active channel bitmask: render bus / device output. `0x3ffff` = 18 ch (16 main + 2 sub). If these differ → output-layer problem. |
| `[RELOC]` toggling `0x3ffff ↔ 0xffff` | Sub channels turning on/off with LFE content. **Expected, not a bug.** |
| `[CLUSTER]` | 2+ of the top-4 mains changed. Correlate with audible pops/relocations. |
| `SpkG` | `speakerProximityCount` this 500 ms window. Nonzero = hard-floor guard is active. |
| `NaN` | `nanGuardCount`. Should be 0. |
| `CPU` | Wall-clock callback load as `elapsed_µs / block_budget_µs`, capped at 2.0. |

---

### What not to do

- Do not redesign auto-compensation (returns 1.0f — plumbing exists, math was wrong; deferred).
- Do not reopen broad device/output mismatch analysis — Bug 3.2 + Bug 8.1 resolved the structural issue.
- Do not add per-sample or per-source logging in the audio callback (RT-unsafe).
- Do not change DBAP render granularity (block-based is canonical; per-sample was tried and reverted).
- Do not re-litigate the structural parity audit — confirmed complete (`3-8-bug-diagnoses.md`). Numerical comparison is deferred as Bug 10.
- Do not modify `runPipeline.py` — deprecated. Headless runs use the engine binary directly.

---

## Change log template

Use this format for every new patch attempt. Copy and fill in.

```
### Bug N.M — [Short description] — STATUS (date)

**Problem:** One-paragraph description of the failure mode and observable symptom.

**Root cause:** Where in the code the problem originates. File path and approximate location.

**Approach:** What this patch does and why.

**Files changed:**
- `path/to/file.hpp`: description of change

**RT-safety:** Any concerns and why they are acceptable (or not).

**Test result:** What happened when tested. Pass / partial / fail. Relevant log output.

**Status:** PATCHED | REVERTED | PLAN | IN PROGRESS
```

---

## Open bugs

---

### Bug 9 — Fast-mover intra-block guard transition pop

**Root cause:** In the fast-mover sub-step path (`Spatializer.hpp`), the proximity guard is applied independently per sub-step. When a source sweeps through the `kMinSpeakerDist = 0.15 m` boundary mid-block, adjacent sub-steps have different guard states. DBAP gain on the dominant speaker changes ~23% at the boundary (`focus=1.5`: `0.14^-3 ≈ 364` vs `0.15^-3 ≈ 296`). This step falls at a 128-sample boundary — audible as a click or high-pitched transient.

**Evidence:** Ascent pops at specific timestamps (~115 s, ~198 s, ~345 s). `SpkG` and `NaN` stay near 0 during these events, ruling out the hard-floor guard and clamp as direct causes. The intra-block step is geometrically guaranteed whenever a fast-moving source crosses the guard boundary within a block.

**Relevant code:**
- `Spatializer.hpp` — fast-mover `for (int j = 0; j < kNumSubSteps; ++j)` block in the DBAP branch. Guard runs inside this loop on each `subSafePos` independently.
- `Spatializer.hpp` — normal-path `if (!isFastMover)` block: add `mPrevSafePos` blending for guard-transition blocks.
- `Spatializer.hpp` — `prepareForSources(size_t n)`: extend to allocate 3 new per-source state vectors.

---

### Bug 9.1 — Pre-guard endpoint blending — PLAN

**Scope note:** This is a targeted test patch for one specific pop mechanism (intra-block guard-state discontinuity in the fast-mover path). Not all remaining pops are guard-related; this patch should be evaluated as a diagnostic step, not a final universal fix.

**Sequencing:** Implement the normal-path continuity fix first and retest. Then implement the fast-mover patch as a separate change.

---

**Approach (normal path — cross-block guard-transition):** When guard fired this block or last block, blend `mPrevSafePos[si]` (sub-steps 0–1) → `safePos` (sub-steps 2–3) using the existing scratch infrastructure.

New state in `Spatializer.hpp` private section:
```cpp
std::vector<al::Vec3f>  mPrevSafePos;    // guard-resolved position from last block
std::vector<uint8_t>    mPrevSafeValid;  // 1 = initialized
std::vector<uint8_t>    mPrevGuardFired; // 1 = guard fired last block
```

Normal-path logic (`if (!isFastMover)` block):
```cpp
const bool doBlend = mPrevSafeValid[si] && (guardFiredForSource || mPrevGuardFired[si]);

if (doBlend) {
    for (int j = 0; j < kNumSubSteps; ++j) {
        const al::Vec3f& subPos = (j < kNumSubSteps / 2) ? mPrevSafePos[si] : safePos;
        mFastMoverScratch.zeroOut();
        mDBap->renderBuffer(mFastMoverScratch, subPos,
                            mSourceBuffer.data() + j * subFrames, subFrames);
        // accumulate into mRenderIO at frame offset j * subFrames
    }
} else {
    mDBap->renderBuffer(mRenderIO, safePos, mSourceBuffer.data(), numFrames);
}
mPrevSafePos[si]    = safePos;
mPrevSafeValid[si]  = 1u;
mPrevGuardFired[si] = guardFiredForSource ? 1u : 0u;
```

Move `subFrames` definition above the `isFastMover` branch so both paths share it.

---

**Approach (fast-mover path — revised):** Pre-guard both endpoints with the full guard (Pass 1 + Pass 2 convergence) before the sub-step loop. These serve as smooth trajectory anchors. Inside the loop, lerp between the pre-guarded endpoints, then apply only Pass 2 (hard-floor) as a **single scan** safety net — no Pass 1, no convergence loop.

Rationale for this structure:
- Pre-guarded endpoints eliminate the large divergent corrections between adjacent sub-steps that caused the original gain step.
- Pass 1 is omitted inside the loop because the soft-zone bump reintroduces the same divergent correction the fix is meant to eliminate.
- The single-scan Pass 2 catches the case where the lerped path (a chord) passes back through a hard-floor region between two hard-floor-safe endpoints. Because both endpoints are already clear, any mid-path intrusion will be small; a single scan is sufficient and avoids overcorrection.

Caveat: single-scan Pass 2 is a safety net, not a full geometric guarantee. If a lerped sub-step lands in a region where multiple speakers overlap, a single scan may not fully resolve it. This is an acceptable tradeoff for a test patch.

```cpp
auto applyGuard = [&](al::Vec3f posePos) -> al::Vec3f {
    al::Vec3f rp(posePos.x, -posePos.z, posePos.y);  // flip to DBAP space
    // Pass 1: soft zone (parabolic bump)
    for (const auto& spkVec : mSpeakerPositions) { /* ... */ }
    // Pass 2: hard floor convergence loop
    for (int iter = 0; iter < kGuardMaxIter; ++iter) { /* ... */ }
    return al::Vec3f(rp.x, rp.z, -rp.y);  // un-flip
};

al::Vec3f guardedStart = applyGuard(pose.positionStart);  // full guard
al::Vec3f guardedEnd   = applyGuard(pose.positionEnd);    // full guard

for (int j = 0; j < kNumSubSteps; ++j) {
    float alpha = (static_cast<float>(j) + 0.5f) / static_cast<float>(kNumSubSteps);
    al::Vec3f subPos = guardedStart + alpha * (guardedEnd - guardedStart);
    float mag = subPos.mag();
    if (mag > 1e-7f) subPos = (subPos / mag) * mLayoutRadius;

    // Safety net: Pass 2 only, single scan (no convergence loop, no Pass 1)
    for (const auto& spkVec : mSpeakerPositions) {
        float dist = (subPos - spkVec).mag();
        if (dist < kMinSpeakerDist && dist > 1e-7f) {
            al::Vec3f dir = (subPos - spkVec) / dist;
            subPos = spkVec + dir * kMinSpeakerDist;
        }
    }

    mFastMoverScratch.zeroOut();
    mDBap->renderBuffer(mFastMoverScratch, subPos,
                        mSourceBuffer.data() + j * subFrames, subFrames);
    // accumulate...
}
```

---

**Files to change:** `Spatializer.hpp` only.

**RT-safety:** Zero allocation. State vectors are audio-thread-owned after `start()`. Negligible memory.

**Status:** PLAN (normal-path continuity fix PATCHED 2026-04-01; fast-mover patch pending retest)

---

## Closed bugs

---

### Bug 8 — Non-deterministic output device routing

**Root cause:** `RealtimeBackend::init()` opened whatever macOS picked as the system default. If the default was not the MOTU (e.g., MacBook built-in), audio went to the wrong device with no error.

---

### Bug 8.1 — Explicit --device flag + GUI device picker — PATCHED

**C++ engine** (`RealtimeBackend.hpp` `init()`): if `mConfig.outputDeviceName` is non-empty, iterates `al::AudioDevice::numDevices()`, lowercases names for comparison, resolves to the matching device, validates `channelsOutMax()` ≥ required, calls `mAudioIO.deviceOut(selectedDev)` before `mAudioIO.init()`. Prints a list of available devices on mismatch.

**CLI** (`main.cpp`): `--device <name>` sets `opts.outputDeviceName`; `--list-devices` enumerates all output devices and exits.

**GUI** (`gui/imgui/src/App.cpp`): `scanDevices()` populates `mDeviceList` via `al::AudioDevice`. Rendered as a Scan button + combo dropdown in the ENGINE tab. Selected name passed through `EngineOptions::outputDeviceName` at start.

**Status:** PATCHED

---

### Bug 7 — Guard-induced relocation and buzzing

**Root cause:** The proximity guard was a single sequential pass. After pushing a source away from speaker K, the new position could fall inside speaker K+1's zone, which pushed again in sequence. Result was order-dependent — small input-position differences between consecutive blocks produced different push sequences → different DBAP gain clusters each block → audible relocation and buzzing. No soft entry zone existed before the hard floor, causing sudden position snaps.

---

### Bug 7.1 — Guard convergence loop — PATCHED (2026-03-28)

**File:** `Spatializer.hpp`. Added `kGuardMaxIter = 4` constant. Replaced single-pass guard in both the normal path and the fast-mover per-sub-step path with a convergence loop: outer `for (iter < kGuardMaxIter)`, inner speaker scan with a `pushed` flag; breaks early when no speaker fires. Result is geometrically consistent and order-independent.

**Status:** PATCHED

---

### Bug 7.2 — Soft-repulsion outer zone — PATCHED (2026-03-28)

**File:** `Spatializer.hpp`. Added `kGuardSoftZone = 0.45f` (~line 1021). Added Pass 1 before the existing convergence loop (now Pass 2) in both the normal-path guard (~lines 453–489) and the fast-mover per-sub-step guard (~lines 549–577). Pass 1: single scan, symmetric parabolic bump — `u = (dist - kMin) / (kSoft - kMin)`, `push = zoneWidth * u * (1-u)`. Zero at both boundaries, peak at midpoint. No iteration.

**Test result (Canyon Test 1, 2026-03-28):** `SpkG` still at 30 from ~148 s; CLUSTER events every ~0.5 s; audible relocation ~150 s, buzzing ~175 s. Render path confirmed correct (`rBus == dev` throughout). Remaining Canyon symptoms were OS device routing — addressed by Bug 8.1.

**Status:** PATCHED

---

### Bug 6 — No visibility into channel-relocation events

**Root cause:** No diagnostics to distinguish whether channel failures were in `mRenderIO` (render layer) or `io.outBuffer()` (device/output layer). `mAudioIO.cpu()` capped at 1.0 and was unreliable.

---

### Bug 6.1 — Phase 14: channel-relocation diagnostic + CPU meter — PATCHED (2026-03-25)

Added to `RealtimeTypes.hpp`: `renderActiveMask`, `deviceActiveMask`, DOM/CLUSTER relocation event latches, `mainRmsTotal`, `subRmsTotal`, `callbackCpuLoad`. Added pre-copy and post-copy measurement blocks to `Spatializer.hpp` `renderBlock()`. Added `mCallbackStart` wall-clock capture to `RealtimeBackend.hpp`. Added event printing to monitoring loop in `main.cpp`.

**Status:** PATCHED

---

### Bug 6.2 — Phase 14A: DOM mask anchored to mains only — PATCHED (2026-03-27)

`domThresh` was anchored to global `maxMs` including subs, causing spurious DOM events when sub RMS fluctuated. Fixed in `Spatializer.hpp`: added `maxMainMs` (same loop, `!isSubwooferChannel` guard); changed threshold to `maxMainMs * kDomRelThresh`; added `!isSubwooferChannel` guard to `domMask` bit-setting. Applied in both pre-copy and post-copy measurement blocks.

**Status:** PATCHED

---

### Bug 6.3 — Phase 14B: Top-4 cluster tracking — PATCHED (2026-03-27)

DOM events too sensitive to single-channel threshold crossings. Added O(4 × channels) top-4 main-channel selection per block; `[CLUSTER]` event fires only when new top-4 overlaps previous by fewer than 3 channels (2+ changed). Added 8 new atomic fields to `RealtimeTypes.hpp`. `[CLUSTER-RENDER]` / `[CLUSTER-DEVICE]` printed in monitoring loop.

**Status:** PATCHED

---

### Bug 5 — Smoother cold-start transient

**Root cause:** `ControlSmooth::smoothed` hardcoded to `{masterGain=1.0, focus=1.0}` at construction, regardless of CLI values. First ~200 ms of every run played at wrong gain and focus.

---

### Bug 5.1 — Smoother pre-seeding — PATCHED (2026-03-09), re-applied (2026-03-18)

`RealtimeBackend` constructor seeds `mSmooth.smoothed` and `mSmooth.target` from `mConfig` atomics before any block runs. Was patched in Session 3 but absent from the file by Session 5 — re-applied.

**File:** `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` — constructor body.

**Status:** PATCHED

---

### Bug 4 — Stale slider state sent to re-launched engine

**Root cause (original, Python GUI):** Restart button bypassed `reset_to_defaults()`. OSC flush on `engine_ready` sent whatever sliders were showing from the previous run.

**Status in C++ imgui GUI:** `App::onStart()` always calls `resetRuntimeToDefaults()` before launching (`mGain=0.5, mFocus=1.5, mSpkMixDb=0, mSubMixDb=0, mAutoComp=false`). The GUI controls `EngineSession` via direct C++ setters — not OSC — so there is no OSC flush race. This bug pattern does not exist in the current codebase.

---

### Bug 4.1 — Restart control reset (Python GUI) — PATCHED, superseded

Added `_on_restart()` in Python GUI to call `reset_to_defaults()` before relaunch. The Python GUI no longer exists; the fix is structurally baked into `App::onStart()` in the C++ imgui GUI.

**Status:** PATCHED (superseded by C++ GUI)

---

### Bug 3 — Engine starts silently with wrong channel count

**Root cause:** After `mAudioIO.init()`, the OS-negotiated channel count was never validated. If the system default device had fewer channels than the layout, the engine started and silently dropped speaker channels.

---

### Bug 3.1 — Post-open channel validation using wrong accessor — REVERTED (2026-03-09)

Used `mAudioIO.channelsOut()` which returns `mNumO` (the requested count, always equal to `mConfig.outputChannels`). Guard was always false. Superseded by 3.2.

**Status:** REVERTED

---

### Bug 3.2 — Correct accessor: channelsOutDevice() — PATCHED (2026-03-09)

Changed `mAudioIO.channelsOut()` → `mAudioIO.channelsOutDevice()`. Returns `oParams.nChannels` — clamped to `min(requested, deviceMax)` by RtAudio. Engine now correctly refuses to start when negotiated hardware channels < layout requirements.

**File:** `spatial_engine/realtimeEngine/src/RealtimeBackend.hpp` — `init()`, `actualOutChannels` variable.

**Status:** PATCHED

---

### Bug 2 — Block-boundary motion discontinuity

**Root cause:** One DBAP position per block: gains are constant within the block, step at the boundary. For sources moving a large angular distance between blocks, the gain vector jumped instantaneously — audible as a click.

---

### Bug 2.1 — Fast-mover sub-stepping — PATCHED (2026-03-18)

Sources with angular change > `kFastMoverAngleRad = 0.25 rad` (~14.3°) per block rendered as 4 sub-chunks of 128 frames each. Each sub-chunk uses a position linearly interpolated between `positionStart` and `positionEnd`, renormalized to `mLayoutRadius`, then guarded. `mFastMoverScratch` pre-allocated in `init()`.

Extended `Pose::computePositions()` from `(double blockCenterTimeSec)` to `(double blockStartSec, double blockEndSec)`. `positionStart` / `positionEnd` computed via `computePositionAtTimeReadOnly()` — does not mutate `mLastGoodDir`.

**Files:** `Spatializer.hpp`, `Pose.hpp`, `RealtimeBackend.hpp`

**Status:** PATCHED

---

### Bug 1 — Source onset pop

**Root cause:** On the first active block after silence, `getBlock()` returned full-amplitude samples immediately. DBAP multiplied this step-from-zero by speaker gains — wideband low-end thump.

---

### Bug 1.1 — Onset fade — PATCHED (2026-03-09)

128-sample linear ramp (`kOnsetFadeSamples = 128`, ~2.7 ms at 48 kHz) on first active block after silence. Gate: block energy > `kOnsetEnergyThreshold = 1e-10f`. Applied before masterGain multiply in both LFE and DBAP paths. `mSourceWasSilent[]` preallocated by `prepareForSources(size_t n)`. Zero allocation in audio callback.

**Files:** `Spatializer.hpp`, `main.cpp`

**Status:** PATCHED

---

## Deferred / open questions

- **Bug 10 (deferred): Numerical parity with offline renderer.** Structural parity confirmed (`3-8-bug-diagnoses.md`). Not numerically verified. Primary concern: `splitStems.py` may write mono WAVs at PCM_16 rather than FLOAT. Check: `sf.info('processedData/stageForRender/1.1.wav').subtype` should be `'FLOAT'`. Full procedure in `3-8-bug-diagnoses.md` Section 5.
- **`kMinSpeakerDist` tuning:** 0.15 m may be too large for 360RA DirectSpeaker content (sources at 0.02–0.10 m from target speakers). Consider reducing to ~0.05 m after Bug 9 is confirmed fixed.
- **Remap load-order bug:** `outputRemap.load()` passes `config.outputChannels` for both `renderChannels` and `deviceChannels`. `deviceChannels` should be the post-open actual count. Latent; only affects `--remap` path.
