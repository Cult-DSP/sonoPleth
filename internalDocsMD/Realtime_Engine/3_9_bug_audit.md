Root Cause Assessment
Most Plausible Categories (ranked)
Category A — Proximity guard is systematically altering all near-speaker sources (HIGH confidence)

The guard (kMinSpeakerDist = 0.15) was calibrated on one Eden source (21.1 at 0.049 m). It is set 3× the worst observed case. For Swale 360RA content, DirectSpeaker channels naturally land at small but non-zero distances from their target speakers (~0.02–0.10 m, depending on how ADM azimuth/elevation maps through directionToDBAPPosition and the Translab layout radius). Every one of those sources that falls inside 0.15 m gets pushed outward every single block. The localization gain toward the intended speaker is reduced; energy redistributes to neighbors. This is steady-state, always-on, and has no analog in the offline renderer (which has no guard at all). It explains:

flatter image / reduced presence (persistent, not burst)
gains out of proportion for 360RA
right-side bias if the fallback push direction in DBAP-internal space ((0, kMinSpeakerDist, 0) when dist < 1e-7) happens to land at a specific azimuthal sector after the un-flip
Category B — Guard fallback direction is not neutral (MEDIUM confidence, secondary to A)

When dist < 1e-7 (source exactly on a speaker), the guard doesn't push "away from the speaker" — it unconditionally uses (0, kMinSpeakerDist, 0) in DBAP-internal space. After the un-flip to pose-space that is (0, 0, -kMinSpeakerDist). That is a fixed angular direction, and if multiple sources trigger this path, they're all pushed to the same spot. This could produce a systematic bias toward a specific speaker cluster — consistent with the Swale "skewed right" observation. Whether this fires in practice depends on whether Swale sources ever reach exactly-zero distance, which requires the DirectSpeaker ADM positions to hit the speaker vector exactly. The more likely case is small-but-nonzero distances (Category A applies instead).

Category C — Block-boundary amplitude modulation from guard threshold crossing (MEDIUM confidence for the 130s buzz)

If a Swale source at ~130 s is in slow motion (within its trajectory window, not yet clamped to final keyframe), and its distance to the nearest speaker is oscillating near the 0.15 m boundary on alternate blocks, the DBAP gain for that speaker alternates between two distinct values every ~10 ms. That AM rate is ~86 Hz for 512-frame buffers at 48 kHz. It would be audible as a buzz. Once the source hits its final keyframe and holds, the distance is fixed and the AM stops. The observation "high-pitched buzzing around 130 s" with previously confirmed proximity-guard-related artifacts is consistent with this.

Category D — Steady-state smoother seeding offset (LOW confidence for steady-state symptoms)

ControlSmooth::smoothed initialises at hardcoded focus=1.0 regardless of the --focus CLI flag (1.5 default). It ramps to 1.5 in ~200 ms. At 130 s this has been irrelevant for ~129 s. Does not explain steady-state artifacts. Documented but not yet shown as fixed in the code I read.

Test First
Category A — it is the only candidate that:

Is realtime-only (no counterpart in the offline renderer)
Is active every block for many sources simultaneously in steady state
Is directly observable without code changes (mState.speakerProximityCount is already being incremented)
Has a minimal, reversible diagnostic step (see below)
Smallest Safe Next Change
Add one log line in main.cpp (or wherever playback status is printed) that periodically prints speakerProximityCount, e.g. every 5 seconds of playback time. No audio behavior changes. If the count is climbing continuously during Swale playback (hundreds or thousands per second in steady state), Category A is confirmed as the dominant path.

That output directly tells you whether the guard is a steady-state phenomenon or only fires transiently on the Eden source-21.1 case it was designed for.

Questions
Is speakerProximityCount currently printed anywhere during playback, and if so, what are typical values during the Swale late section? Or has it not been checked yet?

For the 130s Swale buzz: is it present from measure one of that section every run (i.e., deterministic at that timestamp), or does it vary slightly between runs? Deterministic → guard firing at a fixed held-source position. Variable → amplitude modulation from a source still in motion near the threshold.

What is the Translab layout's speaker radius — roughly? This determines how large 0.15 m is relative to the layout, which changes whether Category A's gain effect is audible.

For the "skewed right" in Swale late section: does the offline render (same scene, same focus) sound balanced, or does offline also lean right? If offline is balanced and realtime leans right, that's a realtime-only distortion and the fallback-direction bias (Category B) becomes more interesting.

The proximity guard comment says the 0.049 m worst case is "source 21.1 at t=47.79 s" — that is from Eden. Has the guard ever been verified to fire at all during a Swale-only playback run (i.e., is Swale content actually close to any speakers, or was the guard only needed for Eden geometry)?
