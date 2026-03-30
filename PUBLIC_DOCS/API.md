# Spatial Root EngineSession API V1 Internal Implementation Spec

This document defines the intended V1 public C++ session API for the Spatial Root realtime engine. It is an internal implementation spec for extracting the current orchestration logic from main.cpp into an embeddable EngineSession layer. The goal is to make the CLI, GUI, and future embedding paths use the same runtime control surface.

## Overview

EngineSession is the intended embeddable C++ runtime façade for the Spatial Root realtime engine. It should extract the orchestration currently implemented in main.cpp into a reusable session object while preserving existing startup, runtime, and shutdown behavior.

V1 goals:

- expose a typed C++ runtime API
- keep CLI behavior functionally unchanged
- preserve current backend wiring and shutdown ordering
- keep OSC compatibility for the current GUI
- avoid exposing debug-era diagnostics as part of the stable public API

## Core API and lifecycle

The current runtime source of truth is main.cpp. The V1 EngineSession refactor should preserve this sequence:

- configure engine options
- load LUSID scene and stage source input mode
- construct and load Streaming
- load speaker layout
- construct and initialize Pose
- construct and initialize Spatializer
- prepare source state and optional output remap
- optionally start OSC parameter handling
- initialize backend
- wire Streaming, Pose, and Spatializer into backend
- start loader thread
- start backend/audio
- shut down in ordered sequence

## Ownership model

EngineSession should own the runtime objects that are currently stack-allocated in main.cpp, including:

- RealtimeConfig
- EngineState
- loaded SpatialData
- loaded SpeakerLayoutData
- Streaming
- Pose
- Spatializer
- RealtimeBackend
- optional OutputRemap
- optional OSC parameter server state

RealtimeBackend should continue receiving raw pointers to Streaming, Pose, and Spatializer as it does now. The refactor should move ownership, not redesign callback wiring.

## Intended V1 public API surface

Required methods:

- `bool configureEngine(const EngineOptions&)`
- `bool loadScene(const SceneInput&)`
- `bool applyLayout(const LayoutInput&)`
- `bool configureRuntime(const RuntimeParams&)`
- `bool start()`
- `void setPaused(bool)`
- `void shutdown()`
- `void update()`
- `EngineStatus queryStatus() const`
- `DiagnosticEvents consumeDiagnostics()`
- `std::string getLastError() const`

### Method preconditions

- `configureEngine()` must be called before anything else
- `loadScene()` requires configured engine options
- `applyLayout()` requires a loaded scene
- `configureRuntime()` can happen before `start()`
- `start()` requires scene + layout + runtime config to be valid
- `shutdown()` should be safe to call even after partial failure

Not part of V1 public API:

- `stop()`
- `seek()`
- CLI banner/help/device-list presentation
- restartable transport semantics unless the implementation explicitly supports them

## Public types

The API requires these configurations and return types. Fields must map to the current backend requirements:

- **EngineOptions**: `sampleRate` (int), `bufferSize` (int), `outputDeviceName` (string), `oscPort` (int), `elevationMode` (int)
- **SceneInput**: `scenePath` (string), `sourcesFolder` (string), `admFile` (string)
- **LayoutInput**: `layoutPath` (string), `remapCsvPath` (string)
- **RuntimeParams**: `masterGain` (float), `dbapFocus` (float), `speakerMixDb` (float), `subMixDb` (float), `autoCompensation` (bool)
- **EngineStatus**: `timeSec` (double), `cpuLoad` (float), `renderActiveMask` / `deviceActiveMask` / `renderDomMask` / `deviceDomMask` (uint64_t), `mainRms` / `subRms` (float), `xruns` (size_t), `nanGuardCount` / `speakerProximityCount` (uint64_t), `paused` (bool), `isExitRequested` (bool)
- **DiagnosticEvents**: Contains relocation event polling metrics required by the CLI.

### OSC behavior

If `oscPort > 0` in `EngineOptions`, OSC server startup happens inside `start()`. If `0`, no OSC objects are created.

## Status surface

V1 should expose a simple snapshot-style `queryStatus()` method returning current operational values mapping to the fields described in the EngineStatus type above.

## Quick Start Example

```cpp
#include "EngineSession.hpp"
#include <iostream>

int main() {
    EngineSession session;

    EngineOptions engine;
    engine.sampleRate = 48000;
    engine.bufferSize = 512;
    engine.oscPort = 0; // disable osc
    engine.elevationMode = 0;

    if (!session.configureEngine(engine)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    SceneInput scene;
    scene.scenePath = "scene.lusid.json";
    scene.sourcesFolder = "/path/to/media";

    if (!session.loadScene(scene)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    LayoutInput layout;
    layout.layoutPath = "layout.json";

    if (!session.applyLayout(layout)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    RuntimeParams runtime;
    runtime.masterGain = 0.5f;
    runtime.dbapFocus = 1.5f;

    if (!session.configureRuntime(runtime)) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    if (!session.start()) {
        std::cerr << session.getLastError() << "\n";
        return 1;
    }

    session.setPaused(false);

    EngineStatus s = session.queryStatus();
    std::cout << "Paused: " << s.paused << "\n";

    session.shutdown();
    return 0;
}
```

## Main-thread runtime work

The current engine performs supervisory work on the main thread in main.cpp, including status polling and deferred operations. For V1, the host must regularly call `update()` on the main thread to process auto-compensation state digestion, and `consumeDiagnostics()` for CLI relocation and clustering readouts.

## Device and channel-count behavior

The layout determines the engine’s required output channel count. Physical device compatibility is ultimately validated when the backend opens the selected audio device. V1 should preserve the current behavior in which layout-derived channel count is established before backend startup, while device-opening failure is treated as a backend/start failure rather than a layout-parse failure.

## Shutdown ordering

shutdown() must preserve the current runtime teardown order:

- stop OSC server first
- stop/shutdown backend second
- shut down streaming last

shutdown() should tolerate partially initialized sessions and should clean up only the subsystems that were successfully created.

The agent must preserve this ordering while moving orchestration out of main.cpp.

## Build integration note

The V1 refactor should produce an embeddable library-facing API layer centered on EngineSession. Do not commit this document to a final CMake target name unless the build system is updated accordingly. The implementation should prefer a minimal extraction that allows the CLI target to call the same EngineSession logic used by host applications.

## Agent implementation constraints

- extract orchestration from main.cpp into EngineSession
- keep core DSP and streaming classes intact unless changes are required for ownership/lifecycle
- preserve current startup order
- preserve current shutdown order
- keep OSC compatibility for the current GUI path
- do not make the public API shell out to the CLI
- do not expose debug monitoring surfaces as stable public API
- do not invent new public methods unless required by actual backend constraints
- keep main.cpp as a thin CLI adapter after refactor
