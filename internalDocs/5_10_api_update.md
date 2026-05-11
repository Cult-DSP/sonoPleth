Changed API surface
RuntimeParams struct (EngineSession.hpp):

Added static RuntimeParams defaults() — inline, returns zero-initialized struct with member defaults (0 dB gain/mix, 1.5 focus). This is the single canonical source for all callers.
New public methods (EngineSession.hpp/.cpp):

RuntimeParams getRuntimeParams() const — reads the four live atomics, converts linear→dB via linearToDb (zero/negative→-60 dB, never NaN/inf), and clamps. Reflects setters, OSC changes, configureRuntime, and resetRuntimeParams.
bool resetRuntimeParams() — calls configureRuntime(RuntimeParams::defaults()). Safe before and after start(). Does not restart playback, reload scene/layout, or affect transport.
configureRuntime refactored — no longer touches mSpatializer or OutputRemap. Now: sanitize→clamp params, write atomics, sync OSC if the server is already running. Safe to call before applyLayout().

New private helpers: sanitizeRuntimeParams(), applyRuntimeParamsToConfig(), configureOutputRouting(). The routing setup (CSV scaffolding + layout-derived routing) moved to configureOutputRouting(), called at the end of applyLayout().

Changed files
File Change
source/spatial_engine/realtimeEngine/src/EngineSession.hpp RuntimeParams::defaults(), getRuntimeParams, resetRuntimeParams, three private helper declarations
source/spatial_engine/realtimeEngine/src/EngineSession.cpp Helper functions, three private implementations, refactored configureRuntime, new getRuntimeParams/resetRuntimeParams, updated setters and applyLayout, updated start() OSC init
source/spatial_engine/realtimeEngine/src/main.cpp CLI uses RuntimeParams::defaults() as base; no more duplicated literals
source/gui/imgui/src/App.cpp Controls always enabled pre-run; Reset Parameters button; resetRuntimeToDefaults dropped from onStart; resetRuntimeToDefaults implementation uses RuntimeParams::defaults()
internalDocs/API_internal.md Updated struct doc, lifecycle table, method table
internalDocs/REALTIME_ENGINE.md Updated gain system note, added DBAP focus row to table
internalDocs/AGENTS.md Updated Runtime Control Plane section
internalDocs/devHistory.md New entry dated May 10, 2026
Intentional limitations
OSC bidirectional sync: Individual setters (setMasterGainDb etc.) do not sync OSC-visible parameter values — they only write atomics. configureRuntime() and resetRuntimeParams() do sync OSC params when the server is running. getRuntimeParams() always reflects OSC changes because OSC callbacks write the same atomics. This asymmetry is documented and safe — it avoids any risk of callback feedback loops from the setter path.
