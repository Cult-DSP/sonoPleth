# Spatial Root — Packaging & Distribution

**Session date:** May 11, 2026  
**Branch:** `package`  
**Agent scope:** macOS `.app` bundle, runtime staging, asset/binary discovery, Linux GUI baseline.

---

## Overview

This pass established the baseline macOS bundle and install-tree structure for `spatialroot_gui`. Prior to this work, the GUI had no install rules and all path resolution assumed a repo-root developer build. After this pass:

- `spatialroot_gui` installs as a proper macOS `.app` bundle via `cmake --install`.
- Speaker layout presets are staged into `Contents/Resources/` on macOS.
- `cult-transcoder` is staged into `Contents/Resources/bin/` and is discoverable from the packaged bundle without env vars.
- Layout and transcoder lookup is package-relative first, repo-root last.
- Linux GUI packaging is pinned to X11/XWayland.

Notarization, DMG creation, CPack polish, and Homebrew formulae were **not** attempted and remain future work.

---

## Files Changed

| File | Change |
|------|--------|
| [CMakeLists.txt](../CMakeLists.txt) (line 11) | Added `include(GNUInstallDirs)`. Added `SPATIALROOT_INSTALL_RESOURCE_ROOT` and macOS bundle destination variables (`SPATIALROOT_MACOS_APP_BUNDLE_NAME`, `SPATIALROOT_MACOS_APP_RESOURCES_DESTINATION`). |
| [source/gui/imgui/CMakeLists.txt](../source/gui/imgui/CMakeLists.txt) (line 13) | Set `MACOSX_BUNDLE TRUE` + bundle metadata on `spatialroot_gui`. Added `install(TARGETS … BUNDLE …)` for macOS and `RUNTIME` for Linux. Added `install(DIRECTORY … speaker_layouts …)` for both platforms. Added Linux GLFW X11/Wayland pin. |
| [source/gui/imgui/cmake/Info.plist.in](../source/gui/imgui/cmake/Info.plist.in) | New file. Minimal macOS `Info.plist` template: bundle ID `com.cultdsp.spatialroot`, LSMinimumSystemVersion 11.0, NSHighResolutionCapable. CMake substitutes `MACOSX_BUNDLE_*` variables at configure time. |
| [source/gui/imgui/src/App.cpp](../source/gui/imgui/src/App.cpp) (line 124) | Added `currentExecutablePath()`, `executableDirectory()`, `macBundleResourcesDirectory()`, `installPrefixFromExecutable()`, `layoutPackagedSubpath()`. Updated layout search to: `SPATIALROOT_ASSET_ROOT` → bundle `Contents/Resources` → install-prefix `share/spatialroot` → executable-relative packaged path → repo-root fallback. Updated `cult-transcoder` search to: `SPATIALROOT_CULT_TRANSCODER` → executable-relative packaged locations → bundle `Contents/Resources/bin` → build-tree/dev fallbacks. |
| [source/gui/imgui/src/main.cpp](../source/gui/imgui/src/main.cpp) (line 13) | Updated usage comment to reflect package-relative lookup and repo-root fallback modes. |
| [internal/cult_transcoder/CMakeLists.txt](../internal/cult_transcoder/CMakeLists.txt) (line 258) | Added `install(TARGETS cult-transcoder …)` rules for both macOS (into `Contents/Resources/bin`) and Linux/other (into `${CMAKE_INSTALL_BINDIR}`), under `SpatialRootRuntime` component. |
| [internal/cult-allolib/external/rtaudio/CMakeLists.txt](../internal/cult-allolib/external/rtaudio/CMakeLists.txt) (line 343) | Suppressed or no-op'd unwanted rtaudio install rules that were polluting a full `cmake --install` run with vendored SDK/doc payloads. |

---

## Staged Tree (macOS)

Install command:
```
cmake --install build --prefix <dir> --component SpatialRootRuntime
```

Resulting layout:
```
<prefix>/
└── Spatial Root.app/
    └── Contents/
        ├── MacOS/
        │   └── Spatial Root          ← GUI executable
        ├── Info.plist                ← generated from Info.plist.in
        └── Resources/
            ├── speaker_layouts/      ← all JSON layout presets
            │   ├── stereo.json
            │   ├── quad.json
            │   └── ...
            └── bin/
                └── cult-transcoder   ← transcoder binary
```

Linux install tree (`<prefix>/bin/cult-transcoder`, `<prefix>/share/spatialroot/speaker_layouts/`) is wired in but not validated on this pass.

---

## Discovery Order

### Speaker layouts

1. `SPATIALROOT_ASSET_ROOT` env var (directory containing `source/speaker_layouts/`)
2. macOS bundle `Contents/Resources/speaker_layouts/`
3. Install-prefix `<prefix>/share/spatialroot/speaker_layouts/`
4. Executable-relative packaged fallback
5. Repo-root `source/speaker_layouts/` (dev fallback)

### `cult-transcoder`

1. `SPATIALROOT_CULT_TRANSCODER` env var
2. Executable-relative packaged locations
3. macOS bundle `Contents/Resources/bin/cult-transcoder`
4. Build-tree / dev fallbacks (`build/internal/cult_transcoder/cult-transcoder`)

---

## Linux GUI Decision

GLFW is forced to X11 on Linux (`GLFW_BUILD_X11=ON`, `GLFW_BUILD_WAYLAND=OFF`). The initial release target is X11/XWayland. Wayland package dependencies were not added to `init.sh`. This decision is intentional and should not be reversed without also validating a full Wayland release path and updating `init.sh`.

---

## Validation Results

| Command | Result |
|---------|--------|
| `cmake -S . -B build -DSPATIALROOT_BUILD_GUI=ON` | ✅ passed |
| `cmake --build build --target spatialroot_gui cult-transcoder --parallel 8` | ✅ passed |
| `cmake --install build --prefix /private/tmp/spatialroot-stage.x8TkcZ --component SpatialRootRuntime` | ✅ passed |
| `test -f '.../Spatial Root.app/Contents/Resources/speaker_layouts/stereo.json'` | ✅ passed |
| `test -x '.../Spatial Root.app/Contents/Resources/bin/cult-transcoder'` | ✅ passed |
| `test -f 'source/speaker_layouts/stereo.json'` (source tree) | ✅ passed |
| `test -x 'build/internal/cult_transcoder/cult-transcoder'` (build tree) | ✅ passed |
| `'<stage>/Spatial Root.app/Contents/MacOS/Spatial Root' --help` from `/private/tmp` | ✅ passed (exited 1 before GLFW/App logging — expected for `--help` path without display) |
| Interactive GUI launch from staged bundle | ⚠️ not validated — `open` unavailable in this environment (`kLSServerCommunicationErr -10822`); staged files and lookup paths are correct |

---

## Remaining Package Blockers

| Item | Status |
|------|--------|
| Full `cmake --install build --prefix <dir>` (no component filter) stages vendored third-party SDK/doc payloads | Known — use `--component SpatialRootRuntime` for now; a later pass should prune dependency install rules before CPack work |
| Interactive GUI launch outside repo root | Not validated in this environment — staged files are correct, human verification needed |
| `spatialroot_spatial_render` packaging | Out of scope for this pass; offline-render UI is hidden and not a current blocker |
| Notarization, DMG, CPack, Homebrew | Not attempted — future distribution pass |
| macOS Dock icon | Unresolved (see [agents.md](agents.md) item 5 reference in MEMORY.md) |

---

## Key Implementation Notes

### Info.plist

`source/gui/imgui/cmake/Info.plist.in` is a CMake-template plist (not a static file). CMake substitutes `${MACOSX_BUNDLE_EXECUTABLE_NAME}`, `${MACOSX_BUNDLE_BUNDLE_NAME}`, `${MACOSX_BUNDLE_SHORT_VERSION_STRING}`, and `${MACOSX_BUNDLE_BUNDLE_VERSION}` from the `set_target_properties(spatialroot_gui … MACOSX_BUNDLE_*)` values in the GUI `CMakeLists.txt`. Bundle version comes from `PROJECT_VERSION` (currently `0.1.0`).

### Component install

All install rules use `COMPONENT SpatialRootRuntime`. This isolates the runtime from vendored build-only payloads and makes the staged tree reproducible. Always use `--component SpatialRootRuntime` when staging for packaging.

### `GNUInstallDirs`

`include(GNUInstallDirs)` was added to the root `CMakeLists.txt`. This provides `CMAKE_INSTALL_BINDIR` (`bin`), `CMAKE_INSTALL_DATADIR` (`share`), and `CMAKE_INSTALL_LIBDIR` (`lib`) — used by the install rules in the GUI and `cult-transcoder` CMakeLists files.
