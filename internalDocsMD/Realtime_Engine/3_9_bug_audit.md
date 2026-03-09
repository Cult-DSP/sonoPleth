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

---

## 2026-03-09 — Discontinuity Fixes: onset fade (Fix 1, PATCHED) and fast-mover sub-stepping (Fix 2, PLAN ONLY)

### Context coming into this session

- Per-sample DBAP rendering was tried and reverted (caused worse artifacts).
- `renderBuffer()` block-based path is restored and canonical.
- Auto-compensation is disabled as a no-op (constant 1.0 return).
- A stronger always-on proximity-guard change was tried and reverted: made
  the image blurrier / more diffuse overall.
- After reverting, the image sounds more normal but clicks remain.
- A confirmed **low-end pop at source-appearance (onset)** is present.
- The two leading remaining candidates are:
  1. Source onset / activation discontinuity — step-from-zero at first active block
  2. Block-boundary motion discontinuity — large angular jump in one callback

---

### Fix 1: Onset fade — PATCHED (2026-03-09)

#### Problem
When a source first becomes active (previous block was silence / EOF zero-fill,
current block has real signal), the transition is an instantaneous amplitude step 
from zero to the full-amplitude first sample. DBAP multiplies that step by speaker 
gains, producing a wideband click heard as a low-end thump/pop.

#### Design decisions
| Decision | Reason |
|---|---|
| Fixed per-source index (`si`) into `mSourceWasSilent[]` | No string-keyed map. `mPoses` / `mSourceOrder` are stable from `loadScene()` — same slot every block. |
| `prepareForSources(size_t n)` called on main thread before `start()` | Single allocation point. Audio thread never allocates. Guard `si < mSourceWasSilent.size()` keeps path safe if hook is accidentally skipped. |
| Fade only on first active block after silence | Subsequent blocks need no ramp; the discontinuity only exists at the 0→signal transition. |
| Energy gate (`kOnsetEnergyThreshold = 1e-10f`) | `getBlock()` writes exact `0.0f` for silence/EOF. Any real signal exceeds 1e-10f. |
| Fade length `kOnsetFadeSamples = 128` (~2.7 ms at 48 kHz) | Short enough not to audibly smear attacks. Long enough to suppress a step-from-zero pop. Adjustable later. |
| Ramp placed **before** master-gain multiply (DBAP path) | Gain is applied once, not twice. Proximity guard operates on post-ramp samples. |
| Same ramp applied in both LFE and DBAP paths | LFE onset can also pop. Logic is identical in both branches. |

#### Files changed
- `spatial_engine/realtimeEngine/src/Spatializer.hpp`
  - Added `kOnsetEnergyThreshold` and `kOnsetFadeSamples` constants
  - Added `mSourceWasSilent` member (preallocated `std::vector<uint8_t>`)
  - Added `prepareForSources(size_t n)` public method
  - Changed `for (const auto& pose : poses)` → `for (size_t si = 0; si < poses.size(); ++si)`
  - Inserted onset-fade block in LFE branch (after `getBlock`, before `subGain`)
  - Inserted onset-fade block in DBAP branch (after `getBlock`, before masterGain loop)
- `spatial_engine/realtimeEngine/src/main.cpp`
  - Added `spatializer.prepareForSources(pose.numSources())` after `spatializer.init()`,
    before `backend.start()`

#### RT-safety
- Zero allocation in audio callback.
- `mSourceWasSilent[si]` is a plain `uint8_t` array read/write — no atomics needed
  (audio thread is sole owner after `start()`).
- Energy accumulation: O(numFrames) per source per block. At 80 sources × 512 frames
  = 40,960 FMA ops per block. Negligible vs. DBAP render cost.
- Ramp inner loop: at most 128 multiply-assigns per source per transition block only
  (not steady state).

---

### Fix 2: Fast-mover sub-stepping — PLAN (not yet patched)

#### Problem
When a source moves a large angular distance between consecutive audio blocks (~10 ms
at 48 kHz / 512 frames), DBAP speaker gains jump from the old-block values to the
new-block values without any within-block interpolation. At fast-moving segments this
produces a perceivable click at the block boundary.

#### Why the previous "prev-center interpolation" approach was rejected
Interpolating from `mPrevPositions[si]` (last block's center position) to the current
block's center position introduces **temporal lag**: the sub-step positions span 
*last-block-center → current-block-center*, which is the time window of the *previous* 
block, not the current one. This blurs localization — the current block's audio is 
rendered with a position trajectory that lags one block behind reality.

#### Revised approach: current-block samples only

**Extend `SourcePose` with two new fields:**
```cpp
struct SourcePose {
    std::string name;
    al::Vec3f   position;       // DBAP position at block center (50%) — unchanged
    al::Vec3f   positionStart;  // DBAP position at block start (t = blockStart)
    al::Vec3f   positionEnd;    // DBAP position at block end   (t = blockEnd)
    bool        isLFE  = false;
    bool        isValid = true;
};
```

**Extend `Pose::computePositions()` signature:**

Change from:
```cpp
void computePositions(double blockCenterTimeSec)
```
To:
```cpp
void computePositions(double blockStartTimeSec, double blockEndTimeSec)
```
`blockCenterTimeSec = (blockStartTimeSec + blockEndTimeSec) / 2.0` is derived
internally. The existing `position` field continues to be computed at the midpoint.
`positionStart` and `positionEnd` are computed via the *identical* pipeline
(interpolateDirRaw → safeDirForSource → sanitizeDirForLayout → directionToDBAPPosition)
at `blockStartTimeSec` and `blockEndTimeSec` respectively.

**Caller change in `RealtimeBackend::processBlock()` (Step 2):**
```cpp
// Currently:
const double blockCtrSec = static_cast<double>(curFrame + numFrames / 2) / sampleRate;
mPose->computePositions(blockCtrSec);

// Becomes:
const double blockStartSec = static_cast<double>(curFrame)             / sampleRate;
const double blockEndSec   = static_cast<double>(curFrame + numFrames) / sampleRate;
mPose->computePositions(blockStartSec, blockEndSec);
```

**Fast-mover detection in `Spatializer::renderBlock()`:**

Use `positionStart` and `positionEnd` for the angular-change test, matching the
offline renderer's Q1/Q3 approach conceptually (start→end captures the total
angular span of the block; conservative, may trigger slightly more than the offline
test — acceptable):
```cpp
// Normalize positions to unit sphere for angular comparison
al::Vec3f d0 = pose.positionStart.normalized();
al::Vec3f d1 = pose.positionEnd.normalized();
float dotVal = std::clamp(d0.dot(d1), -1.0f, 1.0f);
float angleDelta = std::acos(dotVal);
bool isFastMover = (angleDelta > kFastMoverAngleRad);
```
`acos` is called once per source per block, only for non-LFE, non-skipped sources.
Not in an inner loop — cost is acceptable.

**Sub-step rendering (4 sub-chunks, kNumSubSteps = 4):**

For fast-mover sources only, split `mSourceBuffer` into 4 equal sub-chunks and render
each sub-chunk into `mFastMoverScratch` with the interpolated position at that
sub-chunk's center time, then accumulate into `mRenderIO`:

```
Sub-chunk 0: samples [0..127]    center at 12.5% of block  → lerp(positionStart, positionEnd, 0.125)
Sub-chunk 1: samples [128..255]  center at 37.5%           → lerp(positionStart, positionEnd, 0.375)
Sub-chunk 2: samples [256..383]  center at 62.5%           → lerp(positionStart, positionEnd, 0.625)
Sub-chunk 3: samples [384..511]  center at 87.5%           → lerp(positionStart, positionEnd, 0.875)
```

Linear interpolation in DBAP-space Cartesian coordinates is acceptable: positions
lie on or near a sphere of radius `mLayoutRadius`, so linear interpolation stays
close to the sphere surface and `mLayoutRadius` is order-of-magnitude larger than
the proximity threshold `kMinSpeakerDist`. The proximity guard is re-run on each
sub-chunk position (same flip → guard → un-flip pattern as the main path).

**New members needed in `Spatializer`:**
```cpp
static constexpr float kFastMoverAngleRad = 0.25f;  // ~14.3°, matches offline renderer
static constexpr int   kNumSubSteps       = 4;       // 512 / 4 = 128-frame sub-chunks
// Pre-allocated scratch AudioIOData: subFrames × outputChannels.
// Sized at init(). Audio-thread-owned.
al::AudioIOData mFastMoverScratch;
```

**`mFastMoverScratch` sizing (in `init()`, after `mRenderIO` sizing):**
```cpp
{
    int subFrames = std::max(1, mConfig.bufferSize / kNumSubSteps);
    mFastMoverScratch.framesPerBuffer(subFrames);
    mFastMoverScratch.framesPerSecond(mConfig.sampleRate);
    mFastMoverScratch.channelsIn(0);
    mFastMoverScratch.channelsOut(computedOutputChannels);
}
```

**No `mPrevPositions` member needed.** All positions come from the current block.

#### RT-safety concerns for Fix 2

| Concern | Status |
|---|---|
| `acos()` per non-LFE source per block | Called once per source per block — not inside a sample loop. ~80 trig ops per block. Acceptable. |
| `mFastMoverScratch.zeroOut()` per sub-chunk | 4 × 128 × outputChannels float-zeroes per fast-mover source. One per source that triggers, not per sample. |
| Proximity guard runs 4× per fast-mover | 4 × numSpeakers vector ops. At 50 speakers: 200 vector ops per fast-mover source per block. Fine. |
| `numFrames % kNumSubSteps == 0` guard | Required. Prevents under-read from `mSourceBuffer`. With `bufferSize=512, kNumSubSteps=4` always true. Guard makes it robust to other sizes. |
| `positionStart.normalized()` | `al::Vec3f::normalized()` is a magnitude + divide. Called twice per source per block for the angular test. No allocation. |
| `positionStart`/`positionEnd` fields in `SourcePose` | Two extra `al::Vec3f` per source. For 80 sources: 80 × 2 × 12 bytes = 1.9 KB extra per block traversal. Negligible. |
| Caller signature change in `computePositions()` | One additional `double` parameter to compute per call site. Only one call site: `RealtimeBackend::processBlock()`. |

#### Files that will be touched (Fix 2, when implemented)
1. `RealtimeTypes.hpp` or `Pose.hpp` — extend `SourcePose` with `positionStart`, `positionEnd`
2. `Pose.hpp` — change `computePositions()` signature; add 2 extra position computations per source
3. `RealtimeBackend.hpp` — update `computePositions()` call in Step 2 of `processBlock()`
4. `Spatializer.hpp` — add constants, `mFastMoverScratch` member, sizing in `init()`, sub-step path in DBAP branch

LFE sources are excluded from fast-mover detection (they have no spatial position).

---

### Recommended patch order
1. ✅ Fix 1 (onset fade) — patched in this session. Verify pop is gone first.
2. Fix 2 (fast-mover sub-stepping) — implement after Fix 1 is confirmed audibly clean.
   The remaining clicks after Fix 1 will be more precisely attributable to block-boundary
   motion, making Fix 2 easier to evaluate in isolation.
