Clean build. Here is a complete summary of everything that was done.

Summary
Problem
When engine startup failed, the GUI's engine log panel showed only a brief high-level error (e.g. loadScene failed: No source files could be loaded). Detailed diagnostic output from Streaming, RealtimeBackend, JSONLoader, and LayoutLoader — device names, available device lists, LUSID parse warnings, WAV open errors — appeared only in the process terminal (stdout/stderr), not in the log panel.

Design: Stream-tee capture
The fix uses a TeeStreamBuf (writes to two std::streambuf\* simultaneously) and an RAII StageCapture guard that redirects both std::cout and std::cerr through the tee during each startup stage. Terminal output is unchanged — every byte still goes to the terminal. On failure, the captured duplicate is formatted into a structured block and stored in EngineSession::mFailureDiagnostics. Successful stages discard the capture buffer silently.

Thread-safety
In start(), cap.restore() is called explicitly before mStreaming->startLoader() — so the rdbuf redirect is never in place when the background loader thread begins writing to stdout/cerr.

Files changed
File Change
EngineSession.hpp Added getFailureDiagnostics(), storeFailureDiagnostics(), mFailureDiagnostics
EngineSession.cpp Added TeeStreamBuf, StageCapture; wired into loadScene, applyLayout, start; added implementations
App.hpp Added appendFailureDiagnostics() declaration
App.cpp Added appendFailureDiagnostics() helper; called at all 5 failure points in doLaunchEngine()
internalDocs/devHistory.md Changelog entry
internalDocs/AGENTS.md Pattern guide for future agents adding startup stages
PUBLIC_DOCS/API.md getFailureDiagnostics() documented with format, thread-safety, and embedding guidance
What the failure log looks like now

[Engine] loadScene failed: No source channels could be loaded from ADM.
=== Failure diagnostics ===
Stage: load scene (ADM streaming)
Scene: /tmp/.../scene.lusid.json
ADM: /path/to/source.wav
Error: No source channels could be loaded from ADM.
Terminal output:
[Streaming] Loading 12 spatial sources from ADM file...
[Streaming] FATAL: Failed to open ADM file.
=== End failure diagnostics ===
Manual validation scenarios
Missing scene file → JSONLoader throws, exception message + [EngineSession] Loading LUSID scene: ... captured → appears in log
Bad layout JSON → LayoutLoader throws "Cannot open layout JSON" → captured in applyLayout
Missing WAV sources → Streaming prints [Streaming] WARNING: Missing source WAV: per file → captured, appears in log
ADM open failure → [Streaming] FATAL: Failed to open ADM file. → captured
Wrong audio device → [Backend] FATAL: Output device not found: "..." + available device list → captured in start backend-init phase
Successful playback → capture buffer discarded; engine log unchanged from before
Limitations
mBackend->start() failures (audio stream start after loader thread is running) include context (stage, paths, error) but no captured stdout/stderr, since the loader thread is active at that point. This failure path is uncommon and the terminal still shows any relevant output.
