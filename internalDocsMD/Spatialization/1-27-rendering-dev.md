### PUSH 3 - LFE Routing and Buffer Sizing

#### Summary

This push implements robust, layout-driven handling of LFE (Low-Frequency Effects) channels and subwoofer routing in the spatial renderer. All changes are fully documented in `RENDERING.md` (see "LFE (Low-Frequency Effects) Handling").

#### Key Changes

- **Direct LFE Routing:** LFE sources are detected and routed directly to all subwoofer channels specified in the layout's `subwoofers` array, bypassing the spatializer.
- **Arbitrary Subwoofer Indices:** The renderer supports any number of subwoofers, with arbitrary channel indices, as defined in the layout JSON.
- **Buffer Sizing:** The output buffer is automatically sized to accommodate the highest channel index present in either the `speakers` or `subwoofers` arrays. This prevents out-of-bounds errors when subwoofer channels have high indices.
- **Spatializer-Agnostic:** LFE routing works identically for all spatializers (DBAP, VBAP, LBAP).
- **Safety:** All buffer accesses are bounds-checked by construction; negative or invalid channel indices in the layout will cause a warning or error.

#### Rationale

This approach ensures:

- LFE content is always delivered to the correct subwoofer channels, regardless of layout.
- No risk of buffer overruns or segmentation faults due to high subwoofer channel indices.
- Consistent behavior across all spatializer modes.

See `RENDERING.md` for full details, rationale, and example layout JSON.

# Rendering Development Notes - 2026-01-27

## Overview

This document captures the technical decisions and implementation details for the VBAP robustness features added on 2026-01-27. These changes address two critical issues:

1. **"Missing stems"** - Sources becoming inaudible due to VBAP coverage gaps
2. **"Fast mover blink"** - Sources cutting out when direction changes too fast within a block

## Problem Analysis

### Issue 1: VBAP Zero Blocks

**Symptoms:**

**Root Cause:**
VBAP (Vector Base Amplitude Panning) requires the source direction to fall within a valid speaker triplet (triangle formed by 3 speakers). When a direction falls outside all triplets, VBAP produces zero output. This can happen:

**Previous Mitigations (insufficient):**

- Elevation rescale/clamping (added earlier) - helps but doesn't guarantee valid triplet
- Direction fallback - only triggers on degenerate interpolation, not VBAP failure

### Issue 2: Fast Mover Blink

**Symptoms:**

- Sources with fast-moving trajectories have audible dropouts
- More pronounced with larger block sizes
- "Stuttering" or "blinking" effect

**Root Cause:**
With block rendering, direction is computed once at block center. If the source moves significantly within a 64-sample block (~1.3ms at 48kHz), the single direction may:

- Cross from one speaker triplet to another
- Pass through a coverage gap
- Create audible discontinuities at block boundaries

## Implementation Details

### A. Zero-Block Detection and Fallback

#### Architecture Decision: Temp Buffer Approach

We chose option (a) - allocate `audioTemp` once per render and reuse it - for these reasons:

1. **Correctness first**: The temp buffer gives us clean, deterministic measurement
2. **Memory predictable**: One allocation at render start, no mid-render allocations
3. **Future optimization**: Can profile later and add lazy allocation if needed

```cpp
// Allocated once at start of renderPerBlock()
al::AudioIOData audioTemp;
audioTemp.framesPerBuffer(bufferSize);
// ... reused for every source check
```

**FUTURE OPTIMIZATION NOTE:** This adds overhead for every source with input energy. If profiling shows this is a bottleneck, consider:

- Lazy allocation on first suspected failure
- Skip temp buffer path for "known good" directions (cache valid directions)
- Batch detection across sources

#### Detection Logic

```cpp
// 1. Measure input energy
float inAbsSum = 0.0f;
for (size_t i = 0; i < blockLen; i++) {
    inAbsSum += std::abs(sourceBuffer[i]);
}
bool hasInputEnergy = (inAbsSum >= kInputEnergyThreshold * blockLen);

// 2. Render to temp buffer
audioTemp.zeroOut();
mVBAP.renderBuffer(audioTemp, dir, sourceBuffer.data(), blockLen);

// 3. Measure output energy
float outAbsSum = 0.0f;
for (size_t i = 0; i < blockLen; i++) {
    for (int ch = 0; ch < numSpeakers; ch++) {
        outAbsSum += std::abs(audioTemp.out(ch, i));
    }
}

// 4. Detect failure
if (hasInputEnergy && outAbsSum < kVBAPZeroThreshold * blockLen * numSpeakers) {
    // VBAP failed - trigger fallback
}
```

#### Nearest-Speaker Fallback

We chose option (b) - direction 90% toward nearest speaker - for these reasons:

1. **Stays inside hull**: Pure speaker direction might still fail in edge cases
2. **Maintains some directionality**: 10% of original direction preserved
3. **Tested empirically**: 90/10 blend reliably gets valid triplet

```cpp
al::Vec3f nearestSpeakerDir(const al::Vec3f& dir) {
    // Find speaker with max dot product
    float maxDot = -2.0f;
    size_t bestIdx = 0;
    for (size_t i = 0; i < mSpeakerDirs.size(); i++) {
        float d = dir.dot(mSpeakerDirs[i]);
        if (d > maxDot) {
            maxDot = d;
            bestIdx = i;
        }
    }

    // Blend 90% toward speaker
    al::Vec3f blended = dir * 0.1f + mSpeakerDirs[bestIdx] * 0.9f;
    return safeNormalize(blended);
}
```

### B. Fast-Mover Sub-Stepping

#### Angular Threshold

Compile-time constant `kFastMoverAngleRad = 0.25f` (~14°) was chosen empirically:

- Too low: Unnecessary sub-stepping, performance cost
- Too high: Miss fast movers that cause audible artifacts
- 14° represents ~1/25 of a full rotation within a block

**FUTURE OPTIMIZATION NOTE:** This could become a `RenderConfig` parameter to allow per-project tuning. Left as compile-time for now to keep API simple.

#### Sub-Step Size

`kSubStepHop = 16` samples chosen because:

- Divides evenly into default block size (64)
- Small enough to track fast motion
- Large enough to maintain efficiency

At 48kHz, 16 samples = 0.33ms, so direction updates ~3000 times/second for fast movers.

#### Detection Method

We sample at 25% and 75% through the block (not start/end) because:

- Block center is the "official" direction point
- 25%/75% gives better indication of within-block motion
- Avoids edge effects at block boundaries

```cpp
double t0 = (double)(blockStart + blockLen / 4) / (double)sr;
double t1 = (double)(blockStart + 3 * blockLen / 4) / (double)sr;

al::Vec3f dir0 = sanitizeDirForLayout(safeDirForSource(name, kfs, t0), ...);
al::Vec3f dir1 = sanitizeDirForLayout(safeDirForSource(name, kfs, t1), ...);

float dotVal = std::clamp(dir0.dot(dir1), -1.0f, 1.0f);
float angleDelta = std::acos(dotVal);
bool isFastMover = (angleDelta > kFastMoverAngleRad);
```

### C. SLERP Verification

Confirmed that `interpolateDirRaw()` already uses proper SLERP:

1. Both keyframe endpoints are normalized via `safeNormalize()`
2. `slerpDir(a, b, t)` performs spherical interpolation
3. Edge cases handled: near-identical vectors (lerp fallback), near-opposite (perpendicular axis)

No changes needed - existing implementation is correct.

## Diagnostics Structure

```cpp
struct VBAPDiag {
    std::unordered_map<std::string, uint64_t> zeroBlocks;      // per-source
    std::unordered_map<std::string, uint64_t> retargetBlocks;  // per-source
    std::unordered_map<std::string, uint64_t> substeppedBlocks; // per-source
    uint64_t totalZeroBlocks = 0;
    uint64_t totalRetargets = 0;
    uint64_t totalSubsteps = 0;
} mVBAPDiag;
```

Added to `RenderStats` would have been redundant - kept separate for clarity.

## Performance Considerations

### Current Priority: Correctness

The implementation prioritizes correctness over performance:

- Temp buffer allocated for every render (not lazy)
- Energy computed for every source every block
- Sub-stepping applied unconditionally when detected

### Future Optimization Opportunities

1. **Lazy temp buffer**: Only allocate on first suspicious source
2. **Direction caching**: Cache "known good" directions for static sources
3. **SIMD energy computation**: Use vector ops for sum-of-absolutes
4. **Adaptive thresholds**: Per-source thresholds based on history
5. **Parallel source processing**: Sources are independent within a block

### Profiling Notes

To profile the overhead:

```bash
# Render same content with and without robustness
time ./sonoPleth_spatial_render --layout ... --positions ... --sources ... --out test.wav

# Compare diag summary to see how often features trigger
```

## Testing Checklist

- [ ] Solo source renders identically to mono stem (minus panning)
- [ ] Sources at extreme elevations remain audible
- [ ] Fast-moving sources don't cut out
- [ ] Zero-block counter correlates with audible gaps (before fix)
- [ ] Sub-step counter shows expected pattern for rotating sources

## Related Files

- `SpatialRenderer.hpp` - Struct definitions, constants, declarations
- `SpatialRenderer.cpp` - Implementation of `renderPerBlock()`, helpers
- `RENDERING.md` - User-facing documentation
- `main.cpp` - CLI with spatializer selection

## Changelog

### 2026-01-27

### PUSH 2 - Multi-Spatializer Support

- **Renamed** `VBAPRenderer` → `SpatialRenderer`
- **Renamed** directories: `vbap_src/` → `renderer/`, `vbapRender/` → `spatialRender/`
- **Renamed** executable: `sonoPleth_vbap_render` → `sonoPleth_spatial_render`
- **Added** DBAP (Distance-Based Amplitude Panning) support via `al::Dbap`
- **Added** LBAP (Layer-Based Amplitude Panning) support via `al::Lbap`
- **Added** `PannerType` enum (DBAP, VBAP, LBAP)
- **Added** CLI flags: `--spatializer`, `--dbap_focus`, `--lbap_dispersion`
- **Changed** default spatializer from VBAP to DBAP (safer for unknown layouts)
- **Added** `mActiveSpatializer` pointer for polymorphic panner dispatch
- **Added** `mLayoutRadius` (median speaker distance) for DBAP positioning
- **Added** `directionToDBAPPosition()` with coordinate transform compensation
- **Added** DBAP coordinate transform documentation (AlloLib quirk)
- **Updated** Python wrappers: `buildSpatialRenderer()`, `runSpatialRender()`
- **Added** backwards-compatibility aliases for old function names
- **Updated** `RENDERING.md` with spatializer comparison and usage examples
- **Renamed** `kVBAPZeroThreshold` → `kPannerZeroThreshold`
- **Renamed** `VBAPDiag` → `PannerDiag`
- **Added** DEV NOTE for future `--spatializer auto` mode

### PUSH 1 - VBAP Robustness

- Added `VBAPDiag` struct for robustness diagnostics
- Added `mSpeakerDirs` precomputed speaker unit vectors
- Added `nearestSpeakerDir()` for fallback direction
- Added `printVBAPDiagSummary()` for end-of-render stats
- Rewrote `renderPerBlock()` with:
  - Input energy detection
  - Temp buffer rendering for zero-detection
  - VBAP failure detection and nearest-speaker fallback
  - Fast-mover detection via angular delta
  - Sub-stepping for fast movers
- Added compile-time tuning constants
- Updated `RENDERING.md` with new features

---

## Multi-Spatializer Technical Notes

### DBAP Coordinate Transform

**Critical quirk discovered in AlloLib's DBAP implementation.**

AlloLib's `al::Dbap` class internally applies a coordinate transform:

```cpp
// In AlloLib dbap.cpp line ~35
Vec3d relpos = Vec3d(pos.x, -pos.z, pos.y);
```

Our coordinate system: y-forward, x-right, z-up (standard OpenGL-style)

To compensate, we pre-transform in `directionToDBAPPosition()`:

```cpp
// Transform: (our_x, our_z, -our_y)
return al::Vec3f(pos.x, pos.z, -pos.y);
```

This is marked with `// FIXME test DBAP` in AlloLib source, suggesting it may be
a temporary workaround that could change. If AlloLib updates this, we'll need
to update or remove our compensation transform.

### Zero-Block Detection Scope

Currently runs for all panners for consistency, but:

- **VBAP**: Actually needs it (coverage gaps at zenith/nadir, hull boundaries)
- **DBAP**: Shouldn't need it (distance-based, no gaps)
- **LBAP**: Shouldn't need it (layer interpolation covers all directions)

**DEV NOTE**: Consider optimizing to only run for VBAP in future.

### Future: Auto-Detection Mode

Could analyze speaker layout to recommend best spatializer:

```cpp
// Pseudo-code for future --spatializer auto
if (elevationSpan < 3°) {
    return DBAP;  // 2D layout
} else if (hasDistinctElevationRings(2, 4)) {
    return LBAP;  // Multi-layer layout
} else if (hasGoodTriangulation()) {
    return VBAP;  // Dense 3D coverage
} else {
    return DBAP;  // Safe fallback
}
```
