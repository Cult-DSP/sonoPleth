Internal Context + File Dependency Notes (for Deep Research + RealtimeEngine.md)
Goal of the upcoming deep research doc

Produce a comprehensive, paper-grade RealtimeEngine.md design document for sonoPleth’s hard-realtime spatial engine, with:

External citations for real-world audio-engine practices (realtime constraints, buffering, ramping, CPU safety, backend comparisons, DBAP references, WAV/RF64 streaming considerations).

Internal references that point to repo files/paths for “how we do it in sonoPleth” (no need to quote lines; just reference the internal docs and code locations).

Core engine scope (explicit)

In-scope:

WAV/RF64 stem streaming into a realtime callback

DBAP spatial mixing (v1)

Runtime toggles (next-block responsiveness)

LFE bypass to subs

End-of-pipeline device channel remap

CPU safety ladder (quality state machine)

Speaker-only “resolution compensation” (manual slider v1; LUT later)

Thread model: loader thread + audio thread

Out-of-scope (mention only as upstream contract):

ADM parsing

LUSID generation/parsing pipeline details

“containsAudio” pipeline details beyond stating it exists and yields an ActiveMask

Locked design decisions (v1)
Audio / performance constraints

Sample rate: 48 kHz

Hard realtime: no I/O, no locks, no allocations in audio callback

Target machines: modern MacBook Pro / desktop i9 class

framesPerBuffer: TBD by deep research (doc will recommend typical ranges and tradeoffs)

Inputs

Audio: PCM WAV / RF64 only, decoded multichannel WAV treated as stems (variable channel count ≤ 128)

Scene: LUSID already parsed; core engine consumes a ready “source list” + pose sampling function/lookup

Source categories

Up to 128 sources possible simultaneously

~10 static bed sources:

node.type == direct_speaker (constant direction)

LFE:

node.type == LFE bypasses panner → routed to subs, divided by number of subs

Inactive sources

Preserve original indices; mark inactive via ActiveMask (no compaction/remap)

Prefer skipping inactive sources early (stream/deinterleave stage), with fallback to skip later if bugs emerge

Spatialization

DBAP only for v1

Motion: block-rate interpolation (block center time), consistent with offline engine practices

Elevation modes toggleable (next block ok)

Runtime toggles (v1)

Elevation mode

DBAP focus (range 0.1–5.0)

Master gain (global)

Speaker compensation slider -5 dB to +5 dB, speaker-bus only, 50 ms smoothing

Play/pause/restart/loop

Debug counters toggles

(Phase 2) solo source: affects speakers only, leave LFE alone

Output channel count + routing

Output channels sized to include high-index subs:

outChans = max(maxSpeakerIndex, maxSubIndex) + 1

Remapping happens at end (speaker bus → device channels)

Remap must be 1:1 only, reject invalid at load

CPU safety ladder

A quality state machine (automatic + reversible)

Degrade order:

gains every 1 block

gains every 2 blocks

gains every 4 blocks

optional safety mode: Top-K largest speaker gains per source (user-toggle only; lower priority)

future idea: auto-focus tightening (not v1)

CPU hot detection metric: callback time / buffer duration

Exact thresholds/hysteresis: TBD by deep research

What I need from each internal file (to reference paths and align terminology)

1. RENDERING.md

Purpose:

Confirms offline renderer’s “known-good” semantics we’re keeping:

block-rate direction evaluation, block center time

elevation mode definitions

output channel sizing rule

LFE routing expectations
Internal-reference usage:

Cite as “internal reference for offline renderer semantics & definitions”
What to preserve in doc:

The names of modes (e.g., RescaleAtmosUp, RescaleFullSphere, Clamp)

The conceptual flow used in offline rendering

2. AGENTS.md

Purpose:

Internal semantic conventions + mapping expectations:

direct speaker beds vs objects vs LFE handling
Internal-reference usage:

Cite as “internal operational rules + conventions”
What to preserve:

The “direct_speaker treated as static audio_object” concept

LFE behavior and any sub routing assumptions

3. SpatialRenderer.hpp / SpatialRenderer.cpp

Purpose:

The offline implementation we’re mirroring:

direction sanitization logic

DBAP focus usage

LFE/sub treatment patterns

any notes around compensation (there’s a hint that sub compensation needs updating)
Internal-reference usage:

Cite as “source of current DBAP and elevation logic; v1 uses same math”
What to preserve:

Where focus is applied conceptually

Where elevation sanitization lives

Where compensation hooks should plug in

4. streamingWAV.md

Purpose:

Documents current prototype streamer (double buffer, chunking, fallback behavior)
Internal-reference usage:

Cite as “prototype streaming approach; will be refined in realtime doc”
What to preserve:

The thread separation intent (“audio thread reads, loader thread fills”)

Chunking as a tunable dimension

5. mainplayer.hpp / mainplayer.cpp

Purpose:

Existing AlloLib player patterns we reuse:

metering patterns (phase 2)

current chunk strategy and limitations (slow prototype)

buffer switching approach
Internal-reference usage:

Cite as “reference implementation for AudioIO integration + remapping + diagnostics patterns”
What to preserve:

How you currently measure or track performance/metering

How you structure the callback and handle channel output

6. channelMapping.hpp

Purpose:

Defines channel remap configuration and constraints
Internal-reference usage:

Cite as “authoritative remapping format”
What to preserve:

Ensure doc states: remap applied at end, 1:1, reject duplicates

7. lusid_scene_v0.5.schema.json

Purpose:

Authoritative node typing:

audio_object, direct_speaker, LFE
Internal-reference usage:

Cite as “authoritative classification of source types”
What to preserve:

v1 uses node.type to classify sources (not group number assumptions)

direct_speaker uses fixed cart

LFE bypass semantics (engine contract)

8. realtime_context_notes.md (your compiled notes)

Purpose:

Consolidated decisions for v1
Internal-reference usage:

Cite as “internal design snapshot; superseded by RealtimeEngine.md once written”
What to preserve:

All locked decisions above

Deep research doc deliverables (what will be produced)

RealtimeEngine.md (paper-grade)

Rationale + design goals

Module breakdown (Streamer, Pose/Control, DBAP Mixer, LFE Router, Compensation, Remapper, CPU Safety Manager)

Thread model and lock-free parameter snapshot strategy

Buffering/chunk sizing discussion (with citations)

Ramping/smoothing & why

CPU safety state machine

WAV/RF64 streaming constraints and practical notes (citations)

Small DBAP background section (citations)

Appendix: backend comparison (AlloLib reference + PortAudio/JACK overview)

Appendix (inside same doc)

Backend comparison and portability notes

Future features list

LUT focus compensation calibration procedure (cited)

Solo speakers-only (phase 2)

Label-based direct speaker routing (phase 2)

Seek/scrub (phase 2)

Optional Top-K safety toggle
