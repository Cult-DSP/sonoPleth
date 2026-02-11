# forSonoPleth.md — Agent spec for sonoPleth ingestion of SpatialSeed LUSID packages

## Purpose

This document describes the exact layout and runtime contracts of a SpatialSeed LUSID package
as produced by `src/export/lusid_package.py`. It explains what files are present, how audio
paths are organized, which fields sonoPleth should consume from the LUSID scene, and the
validation rules the sonoPleth pipeline should apply when importing the package.

## High-level rules (summary)

- The package is a flat folder (no nested `audio/` directory) containing:
  - `scene.lusid.json` — canonical LUSID scene (v0.5.x)
  - `containsAudio.json` — channel metadata and ADM ordering (beds first, then objects)
  - `mir_summary.json` — per-node MIR feature summaries (optional consumers)
  - Mono WAV files for beds, LFE, and objects: `1.1.wav`, `2.1.wav`, ..., `LFE.wav`, `11.1.wav`, `12.1.wav`, ...
- Audio format must be 48 kHz, float32 WAV (v1 contract). sonoPleth should resample only if necessary, but prefer packages that adhere to 48 kHz.
- All audio file references in the package JSON files are relative filenames located in the package root.
- The LFE node is special: node id `4.1` exists in the scene but its audio file is `LFE.wav` (not `4.1.wav`).

## Package layout (exact)

At the package root the following files exist (exact names):

- `scene.lusid.json` — LUSID scene object. Contains nodes, frames (delta frames format), and metadata.
- `containsAudio.json` — array/object describing channels in ADM order. Used by export and playback hosts.
- `mir_summary.json` — MIR feature summaries per node (RMS, centroid, onset density, etc.). Not required by sonoPleth but helpful.
- WAV files — filenames follow the deterministic naming contract:
  - Beds (direct speakers): `1.1.wav`, `2.1.wav`, `3.1.wav`, `5.1.wav`, ... `10.1.wav`
  - LFE: `LFE.wav` (special case)
  - Objects: `11.1.wav`, `12.1.wav`, ... (objects allocated starting at group 11)

## How sonoPleth should resolve audio for a node

When processing `scene.lusid.json` sonoPleth should follow these steps to find the audio for a node:

1. If the node is a bed/direct-speaker or object, find the group's canonical filename in `containsAudio.json` (preferred).
2. If `containsAudio.json` is not present or does not contain the node, fall back to the deterministic rule: node id `X.1` -> `X.1.wav` located in the package root.
3. If the node id is `4.1`, use `LFE.wav` as the filename.

Examples (resolution rules):

```py
from pathlib import Path

def resolve_audio(package_root: Path, node_id: str, contains_audio: dict|None=None) -> Path | None:
    # 1) prefer containsAudio.json mapping
    if contains_audio:
        entry = contains_audio.get(node_id) or contains_audio.get_by_group(node_id)
        if entry and entry.get('filename'):
            candidate = package_root / entry['filename']
            if candidate.exists():
                return candidate

    # 2) special LFE mapping
    if node_id == '4.1':
        candidate = package_root / 'LFE.wav'
        if candidate.exists():
            return candidate

    # 3) deterministic node -> filename fallback
    candidate = package_root / f"{node_id}.wav"
    return candidate if candidate.exists() else None
```

## LUSID scene expectations (what sonoPleth expects to find)

- Every spatial audio source (audio_object) MUST have a keyframe at `t=0.0` (v1 contract). If frames are delta-only, ensure the initial state for each node is present.
- Node types of interest:
  - `direct_speaker` (beds): these are present for compatibility. In v1 they are silent but must be included.
  - `audio_object`: these reference swapped-in WAVs in the package and must include `cart` coordinates.
  - `LFE`: special node id `4.1`, type may be `LFE` or `direct_speaker` with LFE semantics; the loader must map it to `LFE.wav` audio.
- The `cart` coordinates for nodes are normalized to the cube `[-1,1]` for x,y,z with axes defined +X=right, +Y=front, +Z=up. sonoPleth must interpret these as normalized Cartesian coordinates.
- Time units in LUSID are seconds. sonoPleth must interpret frame timestamps as seconds.

## Delta frames behavior

SpatialSeed emits delta frames: each frame contains only nodes that changed since the previous frame. sonoPleth has two options:

1. Native delta processing (preferred): apply changes to nodes listed in each frame and hold previous state for unchanged nodes.
2. Full-frame expansion (fallback): expand delta frames into full snapshots by carrying forward the last-known state for all nodes at each frame. This is more tolerant if the renderer expects full frames.

sonoPleth loader should ensure an initial full snapshot at t=0.0 is constructed before playing frames.

## containsAudio.json contract (what it presents)

`containsAudio.json` describes channels in ADM order (beds first, then objects). Important fields sonoPleth should use:

- `sample_rate`: integer (expected 48000)
- `threshold_db`: float (used to decide contains_audio)
- `channels`: array of channel descriptors with fields such as:
  - `channel_index` (ADM index)
  - `group_id` (LUSID group id string, e.g., `11.1`)
  - `filename` (relative filename in the package root, e.g., `11.1.wav` or `LFE.wav`)
  - `contains_audio`: boolean (beds/LFE are false in v1)
  - `rms_db`: measured RMS in dB (float), may be -200.0 for silent beds

sonoPleth should trust `containsAudio.json` for channel ordering and which channels contain usable audio. If missing, fall back to deterministic filename rules.

## Validation rules for sonoPleth on import

When sonoPleth ingests a package, run these checks and surface warnings/errors:

1. Required files present: `scene.lusid.json`, `containsAudio.json`, `mir_summary.json` (mir optional but encouraged), and at least one object WAV.
2. All WAV files referenced in `containsAudio.json` exist and report sample rate 48000. If sample rate differs, log a warning and resample or reject based on user policy.
3. Every `audio_object` node in `scene.lusid.json` has an initial keyframe at `t=0.0`.
4. All `cart` coordinates are within [-1,1]; if outside, clamp and log a clamp event (SpatialSeed also logs clamps — keep records consistent).
5. Channel ordering in `containsAudio.json` matches LUSID ADM ordering convention: beds 1.1,2.1,3.1,LFE,5.1..10.1, then objects 11.1,12.1,...

## Error handling and fallbacks

- Missing WAVs: if an object WAV is missing, sonoPleth may substitute a silent buffer and log an error (don't crash the whole import).
- Missing `containsAudio.json`: fall back to filename-based resolution and warn the user about missing metadata.
- Missing `mir_summary.json`: continue; MIR is optional for rendering but useful for coupling motion to audio features.
- If a frame sequence lacks t=0.0 for a node, treat the node as static at origin (0,0,0) and log an import error.

## Best practices for producing packages

- Produce packages where all WAVs are 48 kHz float32 and placed in the package root.
- Ensure `containsAudio.json` is complete and deterministic — it is the authoritative mapping for channel order and filenames.
- Keep LFE named `LFE.wav` and include node `4.1` in `scene.lusid.json` even if silent.
- Provide `mir_summary.json` to enable audio-reactive rendering inside sonoPleth.

## Quick example: channel ordering snippet

Example `containsAudio.json` channels ordering (beds first, then objects):

```json
{
  "sample_rate": 48000,
  "threshold_db": -60.0,
  "channels": [
    {
      "channel_index": 1,
      "group_id": "1.1",
      "filename": "1.1.wav",
      "contains_audio": false,
      "rms_db": -200.0
    },
    {
      "channel_index": 2,
      "group_id": "2.1",
      "filename": "2.1.wav",
      "contains_audio": false,
      "rms_db": -200.0
    },
    {
      "channel_index": 4,
      "group_id": "4.1",
      "filename": "LFE.wav",
      "contains_audio": false,
      "rms_db": -200.0
    },
    {
      "channel_index": 11,
      "group_id": "11.1",
      "filename": "11.1.wav",
      "contains_audio": true,
      "rms_db": -12.3
    }
  ]
}
```

## Notes for sonoPleth implementers

- Prefer `containsAudio.json` for authoritative channel order and filenames.
- Expect delta frames and implement state-carrying when applying them.
- Respect the `cart` normalization and clamp policy; log clamp events (the SpatialSeed pipeline logs them too).
- If you implement an import validation UI, surface missing-t=0 errors, missing WAVs, sample-rate mismatches, and invalid channel order to the user before rendering.

## Revision history

- 2026-02-11: Initial spec drafted (v1) based on SpatialSeed export contracts; covers package layout, audio resolution, containsAudio contract, and validation rules.

---
