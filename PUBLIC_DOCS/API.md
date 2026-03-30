# Spatial Root Realtime Engine API V1

This document provides external developers with a public-facing guide to embedding and interacting with the `EngineSession` API for the Spatial Root realtime engine.

## Overview

The `EngineSession` class is a stable, embeddable C++ runtime abstraction. It removes the necessity of wrapping terminal execution, fully isolating the engine’s orchestration lifecycle, threading, and resource management into a linkable component (`EngineSessionCore`).

### Key Capabilities:

1. **Isolated Orchestration:** Initialization, starting, and strict teardown sequences are precisely defined to avoid audio thread blocking or dangling pointers.
2. **Type Safety:** Typed struct configurations (`EngineOptions`, `SceneInput`, etc.) prevent leakage of internal parameters and ambiguous arguments.
3. **Transport/UI Bindings:** Features like pausing (`setPaused()`) and robust metric polling (`queryStatus()`, `consumeDiagnostics()`) are completely thread-safe and non-mutating.

## Core API & Lifecycle

To safely host the engine, you must transition state in a strict linear sequence from the **Main Thread**:

1. **`configureEngine`**: Allocates the audio backend and defines device requirements.
2. **`loadScene`**: Parses architectural data (e.g., LUSID JSON or ADM files), stages media paths, and primes core engine tracks.
3. **`applyLayout`**: Configures the logical speaker array, elevation rendering modes, and remaps signals.
4. **`configureRuntime`**: Finalizes transport parameters (start volume, DBAP focus) and binds OSC parameters.
5. **`start`**: Ignites the audio device and instantiates processing callbacks.

If an operation fails, calling `getLastError()` will provide a human-readable diagnosis of why the method returned `false`.

### The Update (Tick) Loop

Once started, the engine handles rendering on an isolated, high-priority audio thread. However, you must periodically tick the engine loop from your Application's Main Thread (or UI loop) using **`session.update()`**.

The `update()` method exists specifically to:

1. Dispatch asynchronous structural updates safely (e.g., Spatializer Focus Compensation offsets).
2. Avoid blocking the wait-free audio thread with generic compute work.

## Quick Start Example

This simplified lifecycle demonstrates minimal requirements to bootstrap, play, and gracefully bring down the audio context.

```cpp
#include "EngineSession.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    EngineSession session;

    // 1. Configure Engine Device Constraints
    EngineOptions eCfg;
    eCfg.sampleRate = 48000;
    eCfg.bufferSize = 512;
    if (!session.configureEngine(eCfg)) {
        std::cerr << "Engine Fail: " << session.getLastError() << "\n";
        return 1;
    }

    // 2. Load the Audio Scene Composition
    SceneInput sCfg;
    sCfg.scenePath = "scene.lusid.json";
    sCfg.sourcesFolder = "/path/to/media/";
    if (!session.loadScene(sCfg)) return 1;

    // 3. Apply Target Output Format
    LayoutInput lCfg;
    lCfg.layoutPath = "stereo.json";
    if (!session.applyLayout(lCfg)) return 1;

    // 4. Set Run-time Initial Parameters
    RuntimeParams rCfg;
    if (!session.configureRuntime(rCfg)) return 1;

    // 5. Ignite Application Processing Callback
    if (!session.start()) {
        std::cerr << "Start Fail: " << session.getLastError() << "\n";
        return 1;
    }

    // Application Tick / Render Loop (e.g., 60 FPS)
    while (application_is_running) {

        // CRITICAL: process asynchronous engine tasks (Focus Compensation)
        session.update();

        // Retrieve non-blocking operational snapshots
        EngineStatus status = session.queryStatus();
        DiagnosticEvents diags = session.consumeDiagnostics();

        if (diags.renderRelocEvent) {
            std::cout << "Warning: Relocation fault detected.\n";
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    // 6. Automatically resolves blocking threads and network I/O gracefully
    session.shutdown();

    return 0;
}
```

## Critical Notes & Constraints

### Layout Hardware Dependencies

**Mismatched speaker counts generate a fatal fast-fail.**
If your configured speaker layout file requires more output channels than your physical audio device permits (e.g., initializing a 7-channel layout on a 2-channel built-in output), the engine is intentionally designed to reject the `applyLayout()` layer to prevent index out-of-boundary faults in the callback. Ensure you employ a `stereo.json` layout fallback when working locally.

### Teardown Safety

Always utilize `shutdown()` prior to the host application terminating or allowing the `EngineSession` pointer to go out of scope. This executes a strict order of operations:

1. Suspends OpenSoundControl (OSC) ingest servers.
2. Flushes the audio-device callback.
3. Stops streaming file I/O safely and unmaps memory.

## Building and Distributing with CMake

The audio engine has been deliberately containerized into the `EngineSessionCore` CMake target to facilitate embeddability.

To consume `EngineSessionCore` inside a custom parent project:

1. Bring the Spatial Root dependency tree into your repository (e.g. via git submodule).
2. Configure your top-level `CMakeLists.txt` to include the library as a subdirectory.

```cmake
# Adjust path to where your spatialroot relative path is stored
add_subdirectory(thirdparty/spatialroot/spatial_engine/realtimeEngine)

# Your host wrapper executable
add_executable(MyHostApp src/main.cpp)

# Link the exposed core framework (Note: This automatically chains AlloLib requirements)
target_link_libraries(MyHostApp PRIVATE EngineSessionCore)
```

**Note:** Depending on your build environment, ensure that `C++17` is active, as `EngineSessionCore` leverages modern standard libraries heavily for parsing operations.
