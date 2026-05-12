# Alpha Release Notes

## May 2026 Alpha Hardening

This snapshot captures the main internal-facing outcomes from the May 10-11, 2026 hardening passes.

### API and Runtime Control

- `RuntimeParams::defaults()` is now the single default source for API, CLI, and GUI.
- `getRuntimeParams()` and `resetRuntimeParams()` are part of the maintained `EngineSession` contract.
- `configureRuntime()` is safe before or after `start()` and no longer owns output-routing setup.

### Diagnostics and Audio Truthfulness

- startup-stage stdout/stderr capture now feeds `getFailureDiagnostics()` so GUI launch failures surface the same detail previously visible only in the terminal
- backend/API labeling and sample-rate status now come from backend truth rather than GUI platform guesses
- RtAudio-backed builds remain conservative before stream open and authoritative after open/start

### Packaging and GUI Workflow

- the GUI now has a documented package-relative asset/binary discovery model and a staged macOS `.app` bundle layout
- Transcoder-tab hardening tightened command previews, path validation, and success criteria
- runtime path overrides (`SPATIALROOT_ASSET_ROOT`, `SPATIALROOT_CULT_TRANSCODER`, `SPATIALROOT_SPATIAL_RENDER`) are the supported escape hatches for packaged or nonstandard launches

## Known Limits

- offline rendering remains a maintained but not yet fully promoted feature; GUI controls stay hidden until parity and workflow validation are complete
- `package-adm-wav` still has known backend limitations on some large/complex files and should be treated as an active hardening area rather than a finished release feature
- signing/notarization is still a later release-engineering phase
