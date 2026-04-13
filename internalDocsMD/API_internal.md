# EngineSession API — Internal Reference

**Last Updated:** April 2026  
**Source files:** `spatial_engine/realtimeEngine/src/EngineSession.hpp/.cpp`, `PUBLIC_DOCS/API.md`

---

## Contract

> `api_internal_contract.md` — Canonical post-refactor source of truth.

### Public Structs

| Struct | Purpose |
|---|---|
| `EngineOptions` | Core system settings (sample rate, buffer size, device name, OSC port, elevation mode) |
| `SceneInput` | Audio scene definition (LUSID scene path, sources folder, ADM file) |
| `LayoutInput` | Speaker layout path and optional remap CSV path |
| `RuntimeParams` | Initial gain/focus/mix values passed at configure time |
| `EngineStatus` | Side-effect-free snapshot of current state (playhead, CPU load, masks, RMS, xruns) |
| `DiagnosticEvents` | Relocation and cluster-change event flags + bitmask pairs; consumed once per call |

> **Note:** All structs are global (outside any namespace) to avoid polluting public interfaces with internal legacy types.

### Struct Fields

**`EngineOptions`** — passed to `configureEngine()`:
```cpp
struct EngineOptions {
    int sampleRate = 48000;
    int bufferSize = 512;
    std::string outputDeviceName;   // empty = system default
    int oscPort = 9009;             // 0 = disable OSC entirely
    ElevationMode elevationMode = ElevationMode::RescaleAtmosUp;
};
```

**`SceneInput`** — passed to `loadScene()`:
```cpp
struct SceneInput {
    std::string scenePath;          // path to scene.lusid.json
    std::string sourcesFolder;      // path to mono WAV stems (mono file mode)
    std::string admFile;            // path to multichannel ADM WAV (ADM mode)
};
```
`admFile` and `sourcesFolder` are mutually exclusive. If `admFile` is non-empty, ADM direct streaming is used.

**`LayoutInput`** — passed to `applyLayout()`:
```cpp
struct LayoutInput {
    std::string layoutPath;         // path to speaker layout JSON
    std::string remapCsvPath;       // optional; empty = identity remap
};
```

**`RuntimeParams`** — passed to `configureRuntime()`:
```cpp
struct RuntimeParams {
    float masterGain = 0.5f;        // linear, range 0.1–3.0
    float dbapFocus = 1.5f;         // DBAP rolloff exponent, range 0.2–5.0
    float speakerMixDb = 0.0f;      // post-DBAP main trim, range -10–+10 dB
    float subMixDb = 0.0f;          // post-DBAP sub trim, range -10–+10 dB
    bool autoCompensation = false;
};
```
`speakerMixDb` and `subMixDb` are converted dB → linear at store time: `powf(10.0f, dB / 20.0f)`.

---

### Lifecycle & Public Methods

The engine enforces a strict, linear initialization sequence:

1. `configureEngine(const EngineOptions&)` — stores sampleRate, bufferSize, outputDeviceName, oscPort, elevationMode into `mConfig`. Always returns `true`.
2. `loadScene(const SceneInput&)` — parses LUSID scene via `JSONLoader`, initializes `Streaming`. Returns `false` if scene file missing or no sources loaded.
3. `applyLayout(const LayoutInput&)` — requires `loadScene` to have succeeded (`mSceneData` guard). Loads speaker layout, initializes `Pose` and `Spatializer`.
4. `configureRuntime(const RuntimeParams&)` — writes gain/focus/mix atomics to `mConfig`. Loads remap CSV if path non-empty. **OSC ParameterServer is NOT started here — it starts in `start()`.**
5. `start()` — creates and starts `al::ParameterServer` (if `oscPort > 0`), registers OSC callbacks, starts `RealtimeBackend` + loader thread. Prints `"ParameterServer listening"` to stdout when OSC is active.

**Runtime Control (after `start()`):**

| Method | Writes to | Notes |
|---|---|---|
| `setPaused(bool)` | `mConfig.paused` | Only supported transport control. Stop/seek unsupported. |
| `update()` | — | Must be called regularly from main thread (~50ms). Dispatches `computeFocusCompensation()` when `mPendingAutoComp` is set. |
| `queryStatus() -> EngineStatus` | — | Lock-free snapshot. No state mutation. |
| `consumeDiagnostics() -> DiagnosticEvents` | — | Atomically exchanges event flags. Clears them on read. |
| `shutdown()` | — | Terminal. Destroy and recreate `EngineSession` to restart. |

**Phase 6 runtime setters (direct C++ control, no OSC required):**

| Method | Writes | Notes |
|---|---|---|
| `setMasterGain(float)` | `mConfig.masterGain` | Linear 0.1–3.0 |
| `setDbapFocus(float)` | `mConfig.dbapFocus` | Sets `mPendingAutoComp` if autoComp enabled |
| `setSpeakerMixDb(float)` | `mConfig.loudspeakerMix` | dB → linear conversion at store time |
| `setSubMixDb(float)` | `mConfig.subMix` | dB → linear conversion at store time |
| `setAutoCompensation(bool)` | `mConfig.focusAutoCompensation` | Sets `mPendingAutoComp` if enabling |
| `setElevationMode(ElevationMode)` | `mConfig.elevationMode` | Cast to int at store time |

All writes use `std::memory_order_relaxed`. Safe to call after `start()` and before `shutdown()`.

### Error Model

- **Synchronous:** Lifecycle methods return `bool`. On failure: `getLastError() -> std::string`.
- **Events/Diagnostics:** `consumeDiagnostics()` returns a `DiagnosticEvents` struct — one struct per call, event flags cleared atomically on read. Fields:
  - `renderRelocEvent` / `renderRelocPrev` / `renderRelocNext` — render-bus channel mask change
  - `deviceRelocEvent` / `deviceRelocPrev` / `deviceRelocNext` — device output channel mask change
  - `renderDomRelocEvent` / `deviceDomRelocEvent` — dominant-speaker mask change (render / device)
  - `renderClusterEvent` / `deviceClusterEvent` — top-4 cluster changed (render / device)
  - Each `*Prev`/`*Next` pair is a `uint64_t` bitmask.

### Threading Constraints

- **Main thread:** All lifecycle methods (`configureEngine` through `start`, and `shutdown`).
- **`update()`:** Must be called from main thread / UI event loop.
- **Audio thread:** Managed internally by `RealtimeBackend`. Host must not block backend threads. Inter-thread communication is wait-free internally (relaxed atomics + lock-free buffer swaps).

### Shutdown Order

Violating this sequence **will** cause deadlocks on macOS CoreAudio and ASIO:

1. `mParamServer->stopServer()` + `mParamServer.reset()` — kill network ingestion first
2. `mOscParams.reset()` — release parameter objects
3. `mBackend->shutdown()` + `mBackend.reset()` — halt audio callback gracefully
4. `mStreaming->shutdown()` + `mStreaming.reset()` — release disk I/O and memory buffers

`mPose`, `mSpatializer`, `mOutputRemap`, and `mSceneData` are destroyed implicitly via `unique_ptr` when `EngineSession` is destructed. They hold no OS-level resources.

### Explicit Exclusions

- **Restartable Stop/Seek:** Deferred — destroy and recreate `EngineSession` to reset or seek. Ring buffers and ADM block-streamers hold state that cannot be flushed atomically.
- **Granular CLI Features:** Debug toggles remain in `main.cpp` CLI parsing, not exposed in `EngineSession`.

---

## Design Rationale

> `api_derived_design.md` — Explains the "why" behind structural choices.

### Pimpl-style OSC and Parameter Lifetime

AlloLib parameters bind to internal memory topologies — exposing them directly risks lifetime violations. `EngineSession` uses a Pimpl-style `OscParams` struct (defined in `EngineSession.cpp`). `mParamServer` is entirely owned and destroyed by the session. OSC parameters are initialized in `start()` with values already in `mConfig` atomics (set by `configureRuntime()`), ensuring OSC and direct-setter state are always in sync at startup.

**OSC parameter ranges** (from `OscParams` in `EngineSession.cpp`):

| Parameter | AlloLib type | Range | Default |
|---|---|---|---|
| `gain` | `al::Parameter` | 0.1–3.0 | 0.5 |
| `focus` | `al::Parameter` | 0.2–5.0 | 1.5 |
| `speaker_mix_db` | `al::Parameter` | -10–+10 | 0.0 |
| `sub_mix_db` | `al::Parameter` | -10–+10 | 0.0 |
| `auto_comp` | `al::ParameterBool` | 0/1 | 0 |
| `paused` | `al::ParameterBool` | 0/1 | 0 |
| `elevation_mode` | `al::Parameter` | 0–2 | 0 |

### Separation of Status and Diagnostics

- `queryStatus()` — lock-free immediate snapshot (playhead, CPU load, channel masks, RMS)
- `consumeDiagnostics()` — event flags for relocation/cluster events; prevents the host from missing transient events between polling intervals

### Main-Thread Tick (`update()`)

Audio thread strictness forbids heavy configuration matrix recalculation during a callback. `computeFocusCompensation()` is deferred to the main thread via the `mPendingAutoComp` flag. `update()` checks the flag, clears it, and calls `mSpatializer->computeFocusCompensation()`.

---

## Hard Constraints

> `api_mismatch_ledger.md` — Do not attempt to refactor around these without a fundamental engine rewrite.

1. **Staged Setup is Non-Negotiable:** `applyLayout()` checks `if (!mSceneData)` and fails immediately if called before `loadScene()`. Object counts from `loadScene` dictate memory allocations required before `applyLayout` can construct the spatial matrix.
2. **Restartable Stop/Seek is Unsafe:** Ring buffers and ADM block-streamers hold state that cannot be flushed atomically. Transport is strictly `setPaused(bool)`.
3. **OSC Ownership:** `mParamServer` cannot be shared with the host. Must be spun up and torn down inside `EngineSession` to guarantee valid AlloLib parameter scoping.
4. **Shutdown Sequence:** `mParamServer->stopServer()` → `mOscParams.reset()` → `mBackend->shutdown()` → `mStreaming->shutdown()` — any other order **will** deadlock on CoreAudio/ASIO.

---

## Validation & Known Gotchas

> `new_context.md` — Phase complete notes and structural gotchas.

**What was validated:** `EngineSessionCore` extracted into a distinct linkable CMake library (`EngineSessionCore` static target). Type definitions restructured to prevent `AlloLib`/threading leakage. `internal_validation_runner.cpp` smoke test proved robust execution and clean teardown.

**Layout vs. device channel count mismatch:** Mismatches between layout channel counts and hardware channel counts trigger a fatal fast-fail. Public docs must describe `EngineOptions` device fallback behavior and layout configuration dependency.

**`uint64_t` bitmask channel cap:** `EngineStatus` uses `uint64_t` bitmasks for channel masks, implicitly capping the engine at 64 output channels.

**`dbapFocus` default divergence:** `RealtimeConfig` atomic default is `1.0f`; `RuntimeParams::dbapFocus` default is `1.5f`. The `1.5f` value from `RuntimeParams` is written to the atomic by `configureRuntime()`. The `1.0f` atomic default is only ever seen if the engine is started without calling `configureRuntime()` first — do not do this.

**Key file pointers:**
- Public API entry point: `spatial_engine/realtimeEngine/src/EngineSession.hpp`
- Implementation: `spatial_engine/realtimeEngine/src/EngineSession.cpp`
- Core library CMake target: `spatial_engine/realtimeEngine/CMakeLists.txt` (`EngineSessionCore`)
- Working reference: `spatial_engine/realtimeEngine/src/internal_validation_runner.cpp`
- Test assets: `sourceData/lusid_package/scene.lusid.json`, `spatial_engine/speaker_layouts/stereo.json`
