# AlloLib Dependency Audit

**Date:** February 22, 2026  
**Branch:** ebu  
**Purpose:** Pre–Track B repo cleanup — reduce clone/build weight of `thirdparty/allolib`

---

## 1. Weight Analysis (current state)

| Item                                 | Size       | Notes                                       |
| ------------------------------------ | ---------- | ------------------------------------------- |
| `thirdparty/allolib/` (working tree) | 38 MB      | All source files on disk                    |
| `.git/modules/thirdparty/allolib`    | **511 MB** | Full history — 1,897 commits, never shallow |
| `external/json`                      | 16 MB      | nlohmann/json (header-only)                 |
| `external/imgui`                     | 5.1 MB     | Dear ImGui — not used by sonoPleth          |
| `external/glfw`                      | 4.5 MB     | Window/GL context — not used by sonoPleth   |
| `external/Gamma`                     | 2.3 MB     | DSP library — **linked by sonoPleth**       |
| `external/rtaudio`                   | 1.3 MB     | Audio I/O — needed for real-time path       |
| `external/stb`                       | 2.0 MB     | Image loading — not used by sonoPleth       |
| `external/dr_libs`                   | 744 KB     | Audio decoding — not used by sonoPleth      |
| `external/rtmidi`                    | 548 KB     | MIDI I/O — not used today                   |
| `external/glad`                      | 336 KB     | OpenGL loader — not used by sonoPleth       |
| `external/serial`                    | 324 KB     | Serial port — not used by sonoPleth         |
| `external/cpptoml`                   | 316 KB     | TOML parser — not used by sonoPleth         |
| `external/oscpack`                   | 692 KB     | OSC protocol — not used today               |

**Primary problem:** The `.git/modules` history (511 MB) dwarfs the working tree (38 MB).  
A `--depth 1` shallow clone reduces this to ~a few MB of pack objects.

---

## 2. `#include` Audit — sonoPleth → AlloLib

Scanned all `.cpp` / `.hpp` files under `spatial_engine/src/`.

### Direct AlloLib includes

| File                               | AlloLib Header                | Module |
| ---------------------------------- | ----------------------------- | ------ |
| `src/renderer/SpatialRenderer.hpp` | `al/math/al_Vec.hpp`          | math   |
| `src/renderer/SpatialRenderer.hpp` | `al/sound/al_Vbap.hpp`        | sound  |
| `src/renderer/SpatialRenderer.hpp` | `al/sound/al_Dbap.hpp`        | sound  |
| `src/renderer/SpatialRenderer.hpp` | `al/sound/al_Lbap.hpp`        | sound  |
| `src/renderer/SpatialRenderer.hpp` | `al/sound/al_Spatializer.hpp` | sound  |
| `src/renderer/SpatialRenderer.hpp` | `al/io/al_AudioIOData.hpp`    | io     |
| `src/LayoutLoader.hpp`             | `al/sound/al_Speaker.hpp`     | sound  |
| `src/vbap_src/VBAPRenderer.hpp`    | `al/math/al_Vec.hpp`          | math   |
| `src/vbap_src/VBAPRenderer.hpp`    | `al/sound/al_Vbap.hpp`        | sound  |
| `src/vbap_src/VBAPRenderer.hpp`    | `al/io/al_AudioIOData.hpp`    | io     |

### Transitive includes pulled by those headers

| Header                        | Pulls in                                                                        |
| ----------------------------- | ------------------------------------------------------------------------------- |
| `al/sound/al_Spatializer.hpp` | `al/io/al_AudioIOData.hpp`, `al/sound/al_Speaker.hpp`, `al/spatial/al_Pose.hpp` |
| `al/sound/al_Vbap.hpp`        | `al_Spatializer.hpp` → full chain above                                         |
| `al/sound/al_Dbap.hpp`        | `al_Spatializer.hpp` → full chain above                                         |
| `al/sound/al_Lbap.hpp`        | `al_Spatializer.hpp` → full chain above                                         |
| `al/spatial/al_Pose.hpp`      | `al/math/al_Vec.hpp`, `al/math/al_Mat.hpp`, `al/math/al_Quat.hpp`               |

### CMake link targets

```cmake
# spatial_engine/spatialRender/CMakeLists.txt
target_link_libraries(sonoPleth_spatial_render
    al      # full AlloLib static lib
    Gamma   # DSP library (AlloLib external)
)
```

No other CMakeLists outside `thirdparty/` reference AlloLib (confirmed by search).

---

## 3. AlloLib Modules — Usage Classification

### 3a. KEEP LIST — Required today

These modules are **directly referenced** and must be present for the current
offline spatial renderer to compile and link.

| Module             | Headers used                                                                        | Source files compiled                                                               | Why needed                                                                                            |
| ------------------ | ----------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------- |
| **sound**          | `al_Vbap.hpp`, `al_Dbap.hpp`, `al_Lbap.hpp`, `al_Spatializer.hpp`, `al_Speaker.hpp` | `al_Vbap.cpp`, `al_Dbap.cpp`, `al_Lbap.cpp`, `al_Spatializer.cpp`, `al_Speaker.cpp` | Core spatializers (DBAP/VBAP/LBAP) and base class                                                     |
| **math**           | `al_Vec.hpp`                                                                        | `al_StdRandom.cpp` (minor)                                                          | 3-D vector arithmetic used throughout renderer                                                        |
| **spatial**        | `al_Pose.hpp`                                                                       | `al_Pose.cpp`, `al_HashSpace.cpp`                                                   | Pulled in by `al_Spatializer.hpp`; azimuth/elevation geometry                                         |
| **io/AudioIOData** | `al_AudioIOData.hpp`                                                                | `al_AudioIOData.cpp`                                                                | Buffer/channel descriptor passed to spatializers                                                      |
| **external/Gamma** | (linked as `Gamma` CMake target)                                                    | Full Gamma lib                                                                      | Linked by `sonoPleth_spatial_render`; provides DSP primitives used internally by AlloLib sound module |

#### Concrete paths to preserve in `thirdparty/allolib/`

```
include/al/sound/
include/al/math/al_Vec.hpp
include/al/math/al_Mat.hpp
include/al/math/al_Quat.hpp
include/al/math/al_Constants.hpp
include/al/math/al_Functions.hpp
include/al/math/al_Spherical.hpp
include/al/spatial/al_Pose.hpp
include/al/spatial/al_HashSpace.hpp
include/al/io/al_AudioIOData.hpp
include/al/system/al_Printing.hpp
include/al/system/al_Thread.hpp
include/al/system/al_Time.hpp
src/sound/
src/spatial/
src/io/al_AudioIOData.cpp
src/system/al_Printing.cpp
src/system/al_ThreadNative.cpp
src/system/al_Time.cpp
src/math/al_StdRandom.cpp
external/Gamma/
external/json/         (nlohmann — used by AlloLib CMake and sonoPleth JSONLoader)
external/CMakeLists.txt
CMakeLists.txt
```

---

### 3b. LIKELY-FUTURE LIST — Retain for near-term real-time audio engine

`spatial_engine/realtimeEngine/` exists as an empty placeholder. Real-time
development is explicitly planned (see AGENTS.md "Future Work"). The following
components should **not** be discarded even though they are unused today.

| Component                      | AlloLib paths                                                                                                                                                                                               | Rationale                                                                                                                 |
| ------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------- |
| **Audio IO / device backends** | `include/al/io/al_AudioIO.hpp`, `src/io/al_AudioIO.cpp`, `src/io/al_WindowGLFW.cpp`                                                                                                                         | Live audio device open/callback — the entry point for any real-time render loop                                           |
| **RtAudio**                    | `external/rtaudio/`                                                                                                                                                                                         | Cross-platform audio device backend used by `al_AudioIO`; macOS CoreAudio / JACK / ASIO                                   |
| **App / AudioDomain**          | `include/al/app/al_App.hpp`, `al_AudioDomain.hpp`, `al_ComputationDomain.hpp`, `al_SimulationDomain.hpp`; `src/app/al_App.cpp`, `al_AudioDomain.cpp`, `al_ComputationDomain.cpp`, `al_SimulationDomain.cpp` | `al::App` is the standard AlloLib real-time host loop — audio callback scheduling, sample-rate negotiation                |
| **System / threading**         | `include/al/system/al_PeriodicThread.hpp`, `src/system/al_PeriodicThread.cpp`                                                                                                                               | Real-time scheduling, periodic callbacks — needed alongside audio callback                                                |
| **Gamma DSP**                  | `external/Gamma/`                                                                                                                                                                                           | Already in KEEP list (linked today); also core DSP toolkit for oscillators, filters, envelopes in any real-time audio app |
| **OSC / network**              | `include/al/protocol/al_OSC.hpp`, `src/protocol/al_OSC.cpp`; `external/oscpack/`                                                                                                                            | LUSID scene streaming over network, live parameter control — highly plausible near-term need                              |
| **RtMidi**                     | `external/rtmidi/`                                                                                                                                                                                          | MIDI trigger / sync for real-time scene control                                                                           |
| **scene / PolySynth**          | `include/al/scene/al_PolySynth.hpp`, `al_DynamicScene.hpp`, `al_SynthVoice.hpp`; `src/scene/`                                                                                                               | AlloLib's voice-management layer — natural integration point for LUSID nodes as synthesis voices                          |

---

### 3c. TRIM / DO-NOT-NEED LIST — Safe to exclude from working tree

These are unused today **and** have no plausible near-term path to use in
sonoPleth's audio pipeline. They are also the heaviest contributors to
working-tree size.

| Component                           | AlloLib paths                                                                                      | Size        | Why safe to drop                                              |
| ----------------------------------- | -------------------------------------------------------------------------------------------------- | ----------- | ------------------------------------------------------------- |
| **Graphics**                        | `include/al/graphics/`, `src/graphics/`                                                            | ~248 KB src | sonoPleth has its own PySide6 GUI; no OpenGL rendering needed |
| **GLFW**                            | `external/glfw/`                                                                                   | 4.5 MB      | Window creation — only needed for graphics                    |
| **OpenGL/glad**                     | `external/glad/`                                                                                   | 336 KB      | GL loader — graphics only                                     |
| **ImGui**                           | `external/imgui/`, `src/io/al_Imgui.cpp`, `src/io/al_imgui_impl.cpp`, `include/al/io/al_Imgui.hpp` | 5.1 MB      | Immediate-mode debug UI — sonoPleth uses Qt                   |
| **stb**                             | `external/stb/`                                                                                    | 2.0 MB      | Image/font loading — graphics only                            |
| **dr_libs**                         | `external/dr_libs/`                                                                                | 744 KB      | Audio file decoding — sonoPleth uses libsndfile               |
| **serial**                          | `external/serial/`                                                                                 | 324 KB      | Serial port — no hardware I/O in sonoPleth                    |
| **UI layer**                        | `include/al/ui/`, `src/ui/`                                                                        | 324 KB      | AlloLib parameter/preset GUI — replaced by Qt                 |
| **Sphere**                          | `include/al/sphere/`, `src/sphere/`                                                                | 52 KB       | AlloSphere-specific projection — not a target venue           |
| **types/Color**                     | `include/al/types/al_Color.hpp`, `src/types/al_Color.cpp`                                          | —           | Color type — graphics only                                    |
| **Window / GLFW IO**                | `include/al/io/al_Window.hpp`, `src/io/al_Window.cpp`, `src/io/al_WindowGLFW.cpp`                  | —           | Window management — graphics only                             |
| **ControlNav / File / MIDI IO**     | `al_ControlNav`, `al_File`, `al_PersistentConfig`, `al_SerialIO`, `al_Toml`, `al_CSVReader`        | —           | Utility IO not used by renderer                               |
| **App graphics domains**            | `al_GUIDomain`, `al_OpenGLGraphicsDomain`, `al_OmniRendererDomain`, `al_StateDistributionDomain`   | —           | Graphics rendering domains                                    |
| **Distributed / CommandConnection** | `al_DistributedApp`, `al_CommandConnection`                                                        | —           | Multi-machine cluster support — not in scope                  |

---

## 4. Lightweighting Plan (ordered by priority)

### Step 1 — Shallow history (highest impact, zero working-tree change)

**Action:** Re-initialize allolib with `--depth 1` to discard the 511 MB of
git history while keeping the exact pinned commit on disk unchanged.

**Expected saving:** ~510 MB from `.git/modules/thirdparty/allolib`  
**Risk:** None — working tree is identical. Only history is lost locally.  
**Script:** `scripts/shallow-submodules.sh` (see §5)

This is implemented as the **default** going forward:

- `.gitmodules` gets `shallow = true` for `thirdparty/allolib`
- `init.sh` uses `git submodule update --init --recursive --depth 1`

### Step 2 — Sparse checkout of working tree (opt-in only)

**Action:** Use `git sparse-checkout` inside the submodule to limit the
working tree to the Keep + Likely-Future paths, skipping graphics/UI/stb/glad.

**Expected saving:** ~14 MB from working tree (glfw 4.5MB, imgui 5.1MB,
stb 2.0MB, glad 336KB, graphics src/include, ui src)  
**Risk:** **High fragility** — sparse checkout state is stored in the
submodule's `.git` dir (now `.git/modules/thirdparty/allolib`). If
`git submodule update` changes the pinned commit, sparse checkout patterns
may need to be reapplied. CMake will also fail if it tries to compile any
trimmed source files (AlloLib builds _all_ sources by default).

**Mitigation required before enabling:**

1. Patch or override `thirdparty/allolib/CMakeLists.txt` (or use a
   `ALLOLIB_SOURCES_OVERRIDE` variable) so that it only compiles files
   present in the working tree.
2. Keep a full checkout as the CI default; sparse mode is developer opt-in.

**Script:** `scripts/sparse-allolib.sh` — explicitly marked opt-in (see §6)

### Step 3 — Minimal fork / vendored subset (future, if Step 1+2 insufficient)

If Step 1 leaves the build still too heavy (e.g., imgui/glfw compile time is
unacceptable), the long-term solution is to fork AlloLib into
`Cult-DSP/allolib-sono` and remove the graphics/UI/window subsystems from
the fork's `CMakeLists.txt` and `external/CMakeLists.txt`.

**API surface to preserve in fork:** everything in the Keep + Likely-Future
lists (§3a, §3b).  
**Files to remove from fork:**

- `src/graphics/`, `include/al/graphics/`
- `src/ui/`, `include/al/ui/`
- `src/sphere/`, `include/al/sphere/`
- `external/glfw/`, `external/imgui/`, `external/stb/`, `external/glad/`,
  `external/serial/`, `external/dr_libs/`
- `src/io/al_Window*.cpp`, `src/io/al_Imgui*.cpp`

**When to do this:** only if Step 1 shallow clone + optional sparse checkout
does not meet build-time / clone-size targets for CI.

---

## 5. `scripts/shallow-submodules.sh` — what it does

See the script itself for full implementation. Summary:

1. For each submodule listed in `.gitmodules`, checks whether its git store
   is already shallow (`is-shallow-repository`).
2. If not shallow, deinits the submodule, removes the cached `.git/modules`
   entry, re-adds with `--depth 1`, and re-checks out the same commit.
3. Prints a before/after size comparison.
4. Is **idempotent** — safe to run multiple times.

Default: processes `thirdparty/allolib`, `thirdparty/libbw64`,
`thirdparty/libadm`, `LUSID` (all current submodules).

---

## 6. `scripts/sparse-allolib.sh` — what it does

⚠️ **OPT-IN ONLY.** Do not run this in CI or as part of `init.sh`. ⚠️

See the script itself. Summary:

1. Enables sparse checkout mode inside the allolib submodule working tree.
2. Writes a `.git/modules/thirdparty/allolib/info/sparse-checkout` patterns
   file containing exactly the Keep + Likely-Future paths from §3a/§3b.
3. Runs `git sparse-checkout reapply` to remove trimmed-list files from the
   working tree.
4. Prints a warning that `cmake` may fail if AlloLib's `CMakeLists.txt` tries
   to compile a trimmed file.

To restore a full checkout: `git sparse-checkout disable` inside the
`thirdparty/allolib` submodule directory.

---

## 7. Summary table

| Tactic                       | Saving                                          | Risk                     | Default?                            |
| ---------------------------- | ----------------------------------------------- | ------------------------ | ----------------------------------- |
| `--depth 1` shallow clone    | ~510 MB (git history)                           | None                     | **Yes** — `init.sh` + `.gitmodules` |
| Sparse checkout working tree | ~14 MB (source files)                           | Medium (CMake fragility) | **No** — opt-in script only         |
| Minimal fork                 | ~20 MB working tree + never pulls graphics deps | Low (one-time effort)    | Future option if above insufficient |
