Routing Architecture Refactor — Layout-Derived Output Routing (Revised)

---

## Summary Sentence

The engine renders to a compact internal bus and routes to a layout-defined output bus; the layout JSON is the only supported public routing source.

---

## 1. Terminology

### 1.1 Engine Naming Policy

| Concept | Canonical engine term | Notes |
|---|---|---|
| Compact, contiguous, DBAP-owned render bus | **internal bus** / **internal channels** | Never "render channels" in engine-facing code |
| Physical AudioIO output bus, layout-derived width | **output bus** / **output channels** | Never "device channels" in engine-facing code |
| Layout JSON field | **deviceChannel** | Keep this name — it is a schema field, not an engine concept |
| Bus width: internal side | **internalChannelCount** | Local variable name; accessor: `numInternalChannels()` |
| Bus width: output side | **outputChannelCount** | Local variable name; `mConfig.outputChannels` is the config store |
| Routing table | **output routing table** / `mOutputRouting` | Never "auto-remap" or "remap feature" |
| Output routing copy: identity layout | **identity copy** | renderChannels == outputChannels, all entries diagonal |
| Output routing copy: non-identity layout | **scatter routing** | Internal channels scattered into non-contiguous output slots |
| Subwoofer index: internal space | `mSubwooferInternalChannels` | Replaces `mSubwooferChannels` / `mSubwooferRenderChannels` |
| Subwoofer index: output space | `mSubwooferOutputChannels` | Replaces `mSubwooferDeviceChannels` |
| Internal-space classifier | `isInternalSubwooferChannel(int ch)` | Replaces `isSubwooferChannel` / `isSubwooferRenderChannel` |
| Output-space classifier | `isOutputSubwooferChannel(int ch)` | Replaces `isSubwooferDeviceChannel` |

### 1.2 What This Changes From the Previous Plan

The previous plan used "render/device" terminology as intermediate names.  
This revision adopts "internal/output" consistently throughout engine code, comments, logs, and accessors. The word "device" survives only when directly referencing the layout JSON field `deviceChannel`.

`numRenderChannels()` is renamed to `numInternalChannels()`. There is no strong compatibility reason to defer this — it is only called from `EngineSession.cpp` (one site) and the plan's own logging strings.

---

## 2. Two-Space Model

### Internal channel space

- Width: `internalChannelCount = numSpeakers + numSubwoofers`
- Channels `0..numSpeakers-1` — DBAP output per speaker
- Channels `numSpeakers..internalChannelCount-1` — LFE per subwoofer
- Compact, contiguous, 0-based, no gaps
- `mRenderIO.channelsOut()` always equals `internalChannelCount`
- All DBAP math, LFE routing, mix trims, NaN guard, and Phase 14 diagnostics operate in this space

### Output channel space

- Width: `outputChannelCount = max(all layout .deviceChannel values) + 1`
- Sparse — gaps are valid and intentional (e.g. Allosphere starts at channel 1)
- `mConfig.outputChannels` holds this value; it is written by `Spatializer::init()` and read by `RealtimeBackend` to open AudioIO
- No internal DSP code reads `mConfig.outputChannels` as internal bus width

### Routing table (the only bridge)

- Entries: `{internalChannel, outputChannel}` pairs derived from the layout JSON
- `mOutputRouting` (type: `OutputRemap`) owned by `Spatializer`
- Built once during `init()`, immutable during playback
- `mRemap` non-owning pointer set to `&mOutputRouting` after `init()`

---

## 3. Final Invariants

After `init()` returns `true`:

1. `mRenderIO.channelsOut() == numSpeakers + numSubwoofers` — always compact, never sparse.
2. `mConfig.outputChannels == max(all .deviceChannel) + 1` — output bus width only.
3. `mRemap != nullptr` — always set to `&mOutputRouting`.
4. `mSubwooferInternalChannels[j] == numSpeakers + j` for all `j` — contiguous by construction.
5. `isInternalSubwooferChannel(ch)` is only called with `ch < internalChannelCount`.
6. `isOutputSubwooferChannel(ch)` is only called with `ch < outputChannelCount`.
7. No internal DSP code reads `mConfig.outputChannels` as internal-bus width (enforced by audit in §6).
8. `checkIdentity()` returns `true` only when `outputChannelCount == internalChannelCount` AND all entries are diagonal AND full coverage holds — no ambiguity.
9. All DBAP math, proximity guard, onset fade, fast-mover sub-stepping are untouched.
10. `init()` returns `false`, logs a descriptive error, and refuses to start on any validation failure.
11. Every call to `buildAuto()` after a successful validation gate produces exactly `internalChannelCount` routing entries. Fewer entries after validation is a hard internal error, not a silent drop.
12. The scatter routing path (Phase 7, non-identity) owns its own output-bus clear. It must not rely on the caller having zeroed `io.outBuffer()`. The identity copy path relies on the backend pre-zero, which is valid because identity guarantees no unmapped output channels exist.
13. The offline renderer (`SpatialRenderer`) is not subject to any invariant here — it is a separate code path with its own channel model and is out of scope for this patch.

---

## 4. Validation Gate

Located at the top of `Spatializer::init()`, before routing construction. Returns `false` with a descriptive logged error on any of:

| Check | Error message |
|---|---|
| `layout.speakers.empty()` | "Speaker layout has no speakers." |
| Any `speaker.deviceChannel < 0` | "Speaker N has negative deviceChannel." |
| Any `subwoofer.deviceChannel < 0` | "Subwoofer N has negative deviceChannel." |
| Duplicate `deviceChannel` across speakers | "Duplicate speaker deviceChannel K." |
| Duplicate `deviceChannel` across subwoofers | "Duplicate subwoofer deviceChannel K." |
| Speaker `deviceChannel == subwoofer deviceChannel` | "Speaker and subwoofer share deviceChannel K." |

**Additional warning (non-fatal):**
- Any `deviceChannel` value > 127: emit `[Spatializer] WARNING: deviceChannel K on speaker/subwoofer N is suspiciously large — possible layout typo.` This threshold is advisory; it does not block startup. The number 127 is chosen as well above any real hardware channel count without being so tight as to reject unusual but valid sparse layouts.

**Empty routing table after construction:**
- After `buildAuto()` returns, if `mOutputRouting.entries().empty()` and the layout passed validation, this is a hard internal error. Log `[Spatializer] INTERNAL ERROR: routing table is empty after successful validation` and return `false`. This state is impossible by construction, but must be guarded explicitly.

**Implementation note:** build a `std::set<int>` across all speaker `deviceChannel` values first, then check each subwoofer against it. O(N log N), acceptable at init time.

---

## 5. Phase 7: Routing Stage — Explicit Behavior

### 5.0 Zeroing ownership audit

**Realtime path:**  
`RealtimeBackend::processBlock()` Step 1 explicitly zeroes all `io.channelsOut()` channels via `std::memset` before calling `renderBlock()`. `numChannels = io.channelsOut()` at that point equals `outputChannelCount` (the physical output bus). The zero runs every block unconditionally — including during pause fades, which have their own additional zeroing after the fact. This guarantee is present and labeled.

However, it is a *caller contract across a function boundary*: `renderBlock()` itself has no self-sufficiency. Any future backend, test harness, or alternate entry path that calls `renderBlock()` without prior zeroing would silently produce additive stale output on every scatter block.

**Offline path:**  
`SpatialRenderer::renderPerBlock()` calls `audioIO.zeroOut()` before rendering. However, `audioIO` is sized to `numSpeakers` (consecutive 0-based), and the offline renderer does not use `OutputRemap`, does not read `deviceChannel`, and explicitly documents this: *"We use array index i as the channel and ignore the original deviceChannel numbers."* The offline renderer is a completely separate code path — it has no routing table and is entirely out of scope for this patch. Do not attempt to unify the two paths.

**Decision — scatter path owns its own clearing:**  
The scatter path must self-clear the full output bus before scattering. It must not rely on the backend precondition. Reasons:

1. `renderBlock()` can in principle be called from any context (tests, alternative backends).
2. The guarantee's scope is bounded to the current single-backend implementation.
3. A scatter-path clear is cheap (one `memset` per block on `outputChannelCount` channels) and eliminates the cross-boundary contract entirely.

The identity path continues to rely on the backend pre-zero (which is always present in the realtime path) and does not need its own clear — because identity is only valid when `outputChannelCount == internalChannelCount`, so there are no unmapped output channels and no stale data risk.

### 5.1 Identity copy (fast path)

**Condition:** `mRemap->identity() == true`  
Valid only when `outputChannelCount == internalChannelCount` AND all routing entries are diagonal AND full bijective coverage holds.

**Behavior:** Direct contiguous copy, internal channel N → output channel N, for N in `0..internalChannelCount-1`.

**Zeroing:** Relies on the backend pre-zero (`RealtimeBackend::processBlock()` Step 1). No additional clearing needed — identity guarantees no unmapped output channels exist.

**Width handling:** Do not use `std::min(renderChannels, numOutputChannels)` in the new code. Since identity requires `outputChannelCount == internalChannelCount`, use the internal count explicitly. The `std::min()` silently masks a mismatch that should never occur in a validated state.

### 5.2 Scatter routing (non-identity path)

**Condition:** `mRemap->identity() == false`

**Behavior — self-contained two-step:**

**Step A — Self-clear the full output bus:**  
At the start of the scatter branch, `renderBlock()` zeros all `outputChannelCount` channels of `io.outBuffer()` itself:
```cpp
// Scatter path owns its own output-bus clear.
// Do NOT rely on the backend pre-zero — renderBlock() must be
// self-sufficient regardless of calling context.
const unsigned int numOutputChannels = io.channelsOut(); // == outputChannelCount
for (unsigned int ch = 0; ch < numOutputChannels; ++ch)
    std::memset(io.outBuffer(ch), 0, numFrames * sizeof(float));
```

**Step B — Scatter:** For each routing entry `{internalCh, outputCh}`, assign `mRenderIO.outBuffer(internalCh)` into `io.outBuffer(outputCh)`. Use `=` (assignment), not `+=` (accumulate), since Step A guarantees a clean slate:
```cpp
for (const auto& entry : mRemap->entries()) {
    // Post-validation guards: should never fire, but kept as last-resort protection.
    if (static_cast<unsigned int>(entry.layout) >= renderChannels) continue;
    if (static_cast<unsigned int>(entry.device) >= numOutputChannels) continue;
    const float* src = mRenderIO.outBuffer(entry.layout);
    float* dst = io.outBuffer(entry.device);
    std::memcpy(dst, src, numFrames * sizeof(float));
}
```

**Unmapped output channels:** Zeroed by Step A. Their silence is enforced by this path's own code, not by any caller contract.

**Note on `+=` vs `=`:** The existing scatter implementation uses `+=` to support many-to-one fan-in (multiple internal channels accumulating into one output channel). The routing table in this patch maps each internal channel to exactly one output channel with no fan-in, so `memcpy` (`=`) is correct and slightly faster. If fan-in is ever required (e.g., a future mix-down scenario), revert to the `+=` loop form.

### 5.3 Summary table

| Layout type | Path | Output bus cleared by | Unmapped channels |
|---|---|---|---|
| Identity (translab) | Fast copy | Backend pre-zero (Step 1) — no gaps possible | None — all output channels mapped |
| Non-identity (allosphere) | Scatter routing | `renderBlock()` self-clear at Phase 7 start | Zeroed by self-clear |

---

## 6. `mConfig.outputChannels` Audit

**Required before patching.** Grep every read of `mConfig.outputChannels` and classify each site.

### Known sites (from current grep):

| File | Line | Site classification | Action required |
|---|---|---|---|
| `EngineSession.cpp:133` | Log: "Output channels (from layout)" | Output-space — correct | Update log string to say "output channels" |
| `EngineSession.cpp:158` | `mOutputRemap->load(mRemapCsv, mConfig.outputChannels, mConfig.outputChannels)` | **Stale/wrong — passes outputChannels as both renderChannels and deviceChannels** | Fix: pass `mSpatializer->numInternalChannels()` as first arg, `mConfig.outputChannels` as second |
| `RealtimeBackend.hpp:97` | Log: "Output channels:" | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:153` | `devMaxOut < mConfig.outputChannels` | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:158` | Error log | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:168` | Error log | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:182` | Opens AudioIO with this channel count | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:207` | Comment | Output-space / backend — correct | Update comment to "output channels" terminology |
| `RealtimeBackend.hpp:212` | Validation: `actualOutChannels < mConfig.outputChannels` | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:215` | Error log | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:226` | `actualOutChannels > mConfig.outputChannels` | Output-space / backend — correct | No change needed |
| `RealtimeBackend.hpp:228` | Log | Output-space / backend — correct | No change needed |
| `Spatializer.hpp:234` (current) | Writes the computed value | Write site — this is the assignment | Will be replaced by the patch |
| `RealtimeTypes.hpp:161` | Comment | Documentation — update | Update comment |

**The only stale/wrong read is `EngineSession.cpp:158`** — it passes `mConfig.outputChannels` as the internal-bus width (`renderChannels` arg to `load()`). This must be fixed to `mSpatializer->numInternalChannels()`.

**Required grep sweep before coding:** Run `grep -rn "outputChannels"` across the full `spatial_engine/` and `gui/` trees to confirm no additional read sites exist outside those listed. The above list is derived from the current codebase state but must be verified as exhaustive.

---

## 7. `buildAuto()` Failure Policy

### Decision: hard-fail assertion after validation

The previous plan said "out-of-range entries are dropped" in `buildAuto()`. This is wrong after the hard validation gate runs.

**Rationale:** If the validation gate passed, every `deviceChannel` value in the layout is non-negative and non-duplicate. The `buildAuto()` loop constructs entries from those exact values, using `internalChannelCount` and `outputChannelCount` computed from those same values. An out-of-range entry at this point is not a user error — it is an internal engine bug: either the routing table construction is wrong, or the bus width computation is inconsistent with the entry generation.

**Required behavior in `buildAuto()`:**
- Do not silently drop entries.
- If any constructed entry has `internalCh < 0 || internalCh >= internalChannelCount`, log `[OutputRouting] INTERNAL ERROR: constructed entry has out-of-range internal channel %d (max %d)` and either assert (debug builds) or return without populating the table (which triggers the empty-table guard in `init()`).
- If any constructed entry has `outputCh < 0 || outputCh >= outputChannelCount`, same treatment.
- In both cases, this is a programming error, not a runtime condition.

**Contrast with `load()` (CSV path):** The CSV `load()` method may still silently drop out-of-range entries from user-provided CSV files, because those are genuinely untrusted external inputs. The policy difference is intentional.

---

## 8. File-by-File Patch Plan

### `spatial_engine/realtimeEngine/src/Spatializer.hpp`

**`init()` — lines 201–309 (current subwoofer + channel count + routing block)**

Replace:
```cpp
mSubwooferChannels.push_back(sub.deviceChannel);
int maxChannel = mNumSpeakers - 1;
for (int subCh : mSubwooferChannels) { if (subCh > maxChannel) maxChannel = subCh; }
int computedOutputChannels = maxChannel + 1;
mConfig.outputChannels = computedOutputChannels;
mRenderIO.channelsOut(computedOutputChannels);
mFastMoverScratch.channelsOut(computedOutputChannels);
```

With:
```cpp
// ── Validation gate ──────────────────────────────────────────────────────
// [see §4 above — returns false with descriptive error on any violation]

// ── Internal space: subwoofer internal channel indices ──────────────────
// LFE sources occupy internal channels numSpeakers..numSpeakers+numSubs-1.
mSubwooferInternalChannels.clear();
mSubwooferOutputChannels.clear();
for (int j = 0; j < numSubs; ++j) {
    mSubwooferInternalChannels.push_back(mNumSpeakers + j);
    mSubwooferOutputChannels.push_back(layout.subwoofers[j].deviceChannel);
}

// ── Compute internal and output bus widths ───────────────────────────────
const int internalChannelCount = mNumSpeakers + numSubs;

int maxOutputCh = 0;
for (const auto& spk : layout.speakers)
    maxOutputCh = std::max(maxOutputCh, spk.deviceChannel);
for (int oc : mSubwooferOutputChannels)
    maxOutputCh = std::max(maxOutputCh, oc);
const int outputChannelCount = maxOutputCh + 1;

// outputChannelCount → config for the backend to open AudioIO.
// internalChannelCount → internal only, never stored in mConfig.
mConfig.outputChannels = outputChannelCount;

mRenderIO.channelsOut(internalChannelCount);
mFastMoverScratch.channelsOut(internalChannelCount);

// ── Build layout-derived output routing table ────────────────────────────
// Speaker i   (internal=i)               → output=layout.speakers[i].deviceChannel
// Sub j       (internal=numSpeakers+j)   → output=layout.subwoofers[j].deviceChannel
{
    std::vector<RemapEntry> entries;
    entries.reserve(internalChannelCount);
    for (int i = 0; i < mNumSpeakers; ++i)
        entries.push_back({i, layout.speakers[i].deviceChannel});
    for (int j = 0; j < numSubs; ++j)
        entries.push_back({mNumSpeakers + j, layout.subwoofers[j].deviceChannel});
    mOutputRouting.buildAuto(std::move(entries), internalChannelCount, outputChannelCount);
}
// Guard: routing table must be non-empty after successful validation.
if (mOutputRouting.entries().empty()) {
    std::cerr << "[Spatializer] INTERNAL ERROR: routing table is empty after successful validation." << std::endl;
    return false;
}
mRemap = &mOutputRouting;
```

**`renderBlock()` — LFE routing loop (lines 396–404)**

Change `mSubwooferChannels` → `mSubwooferInternalChannels`. The loop writes to `mRenderIO.outBuffer(subCh)` where `subCh` is an internal channel index — correct by construction.

**`renderBlock()` — Phase 6 mix trim (lines 668–686)**

Change all `isSubwooferChannel(ch)` → `isInternalSubwooferChannel(ch)`.  
These iterate `ch = 0..internalChannels-1` (internal space).

**`renderBlock()` — Phase 14 internal-bus diagnostic (lines 747–828)**

Change all `isSubwooferChannel(ch)` → `isInternalSubwooferChannel(ch)`.  
These iterate internal space. (4 call-sites.)

**`renderBlock()` — Phase 14 output-bus diagnostic (lines 869–944)**

Change all `isSubwooferChannel(ch)` → `isOutputSubwooferChannel(ch)`.  
These iterate `ch = 0..outputChannelCount-1` (output space). (3 call-sites.)

**`renderBlock()` — Phase 7 copy block (lines 830–867)**

Replace the existing Phase 7 block in full:

```cpp
// ── Phase 7: Route internal bus → output bus ─────────────────────────────
const unsigned int numOutputChannels = io.channelsOut(); // == outputChannelCount

bool useIdentity = (mRemap == nullptr) || mRemap->identity();

if (useIdentity) {
    // Identity copy: internal ch N → output ch N, contiguous.
    // Valid only because checkIdentity() guarantees
    // outputChannelCount == internalChannelCount (no gaps).
    // Relies on backend pre-zero (RealtimeBackend::processBlock() Step 1).
    // No std::min() guard — both widths are equal by invariant.
    for (unsigned int ch = 0; ch < renderChannels; ++ch) {
        const float* src = mRenderIO.outBuffer(ch);
        float*       dst = io.outBuffer(ch);
        for (unsigned int f = 0; f < numFrames; ++f)
            dst[f] += src[f];
    }
} else {
    // Scatter routing: self-clear the full output bus, then scatter.
    // renderBlock() owns this clear — do not rely on the caller.
    for (unsigned int ch = 0; ch < numOutputChannels; ++ch)
        std::memset(io.outBuffer(ch), 0, numFrames * sizeof(float));

    for (const auto& entry : mRemap->entries()) {
        // Last-resort guards. These should never fire after a successful
        // validation + buildAuto(). If they do, it is an internal engine bug.
        if (static_cast<unsigned int>(entry.layout) >= renderChannels)   continue;
        if (static_cast<unsigned int>(entry.device) >= numOutputChannels) continue;
        const float* src = mRenderIO.outBuffer(entry.layout);
        float*       dst = io.outBuffer(entry.device);
        std::memcpy(dst, src, numFrames * sizeof(float));
    }
}
```

Note: the identity path retains `+=` (add into pre-zeroed buffer) for behavioral consistency with pre-patch code. The scatter path uses `std::memcpy` (`=`) because the self-clear guarantees a clean slate and there is no fan-in in the layout-derived routing table.

**Accessor staging — `numInternalChannels()` + deprecated alias:**

Add `numInternalChannels()` as the canonical accessor and keep `numRenderChannels()` as a `[[deprecated]]` forwarding alias. There are currently zero external callers of `numRenderChannels()` (confirmed by grep — only the definition in `Spatializer.hpp:952`), so the alias is cheap insurance rather than a necessity. It lets any future caller or test that references the old name produce a compile-time warning rather than a hard error, and allows the removal to be a separate trivial commit.

```cpp
/// Width of the internal bus (numSpeakers + numSubwoofers).
/// Safe to call from the main thread after init().
unsigned int numInternalChannels() const {
    return static_cast<unsigned int>(mRenderIO.channelsOut());
}

/// @deprecated Use numInternalChannels().
[[deprecated("Use numInternalChannels()")]]
unsigned int numRenderChannels() const {
    return numInternalChannels();
}
```

Internal code in `Spatializer.hpp` itself should call `numInternalChannels()` directly — do not call the alias from within the same file. The alias exists for external callers only.

**`isSubwooferChannel()` — remove and replace (line 1049):**

Remove `isSubwooferChannel()`.

Add:
```cpp
// Internal-space helper: ch is in 0..internalChannelCount-1
bool isInternalSubwooferChannel(int ch) const {
    for (int ic : mSubwooferInternalChannels)
        if (ic == ch) return true;
    return false;
}
// Output-space helper: ch is in 0..outputChannelCount-1
bool isOutputSubwooferChannel(int ch) const {
    for (int oc : mSubwooferOutputChannels)
        if (oc == ch) return true;
    return false;
}
```

**Private member changes (lines 1119–1157):**

| Old | New | Note |
|---|---|---|
| `std::vector<int> mSubwooferChannels` | `std::vector<int> mSubwooferInternalChannels` | Internal indices: numSpeakers, numSpeakers+1, … |
| *(new)* | `std::vector<int> mSubwooferOutputChannels` | Physical output channels from layout `.deviceChannel` |
| `const OutputRemap* mRemap = nullptr` | keep | Non-owning pointer, initialized to `&mOutputRouting` in `init()` |
| *(new)* | `OutputRemap mOutputRouting` | Owned routing table |

**Doc comment updates:**

- Class header (lines 12–16): replace old formula with two-space model description using internal/output terminology.
- `init()` comment block: replace "outputChannels formula" with internal/output split.
- Threading READ-ONLY list (lines 62–66): update `mSubwooferChannels` → `mSubwooferInternalChannels`, `mSubwooferOutputChannels`.
- Internal render buffer comment (lines 277–298): remove stale "future Channel Remap agent" language; state that the routing is now active.

---

### `spatial_engine/realtimeEngine/src/OutputRemap.hpp`

**Add `buildAuto()` after `load()` (after line 179):**

```cpp
// Build the output routing table from layout-derived entries (no CSV).
// Called once by Spatializer::init() on the main thread, before start().
// internalChannels: width of the internal bus (numSpeakers + numSubwoofers).
// outputChannels:   width of the physical output bus (max deviceChannel + 1).
// Post-validation: all entries must be in range. Out-of-range entries are
// a hard internal error, not a silent drop.
void buildAuto(std::vector<RemapEntry> entries,
               int internalChannels,
               int outputChannels) {
    mEntries.clear();
    mMaxDeviceIndex = -1;
    for (auto& e : entries) {
        if (e.layout < 0 || e.layout >= internalChannels) {
            std::cerr << "[OutputRouting] INTERNAL ERROR: out-of-range internal channel "
                      << e.layout << " (max " << internalChannels - 1 << ")" << std::endl;
            mEntries.clear();
            return;
        }
        if (e.device < 0 || e.device >= outputChannels) {
            std::cerr << "[OutputRouting] INTERNAL ERROR: out-of-range output channel "
                      << e.device << " (max " << outputChannels - 1 << ")" << std::endl;
            mEntries.clear();
            return;
        }
        mEntries.push_back(e);
        if (e.device > mMaxDeviceIndex) mMaxDeviceIndex = e.device;
    }
    mIdentity = checkIdentity(internalChannels, outputChannels);
    std::cout << "[OutputRouting] " << mEntries.size()
              << " layout-derived routing entries"
              << (mIdentity ? " — identity copy active" : " — scatter routing active")
              << std::endl;
}
```

**`checkIdentity()` — update signature and guard (line 211):**

```cpp
bool checkIdentity(int internalChannels, int outputChannels) const {
    // Identity requires internal and output bus widths to be equal.
    // Any gap in output channel numbering means output > internal,
    // which makes scatter routing necessary.
    if (outputChannels != internalChannels) return false;
    if (static_cast<int>(mEntries.size()) != internalChannels) return false;
    std::vector<bool> covered(internalChannels, false);
    for (const auto& e : mEntries) {
        if (e.layout != e.device) return false;
        if (covered[e.layout])   return false; // duplicate
        covered[e.layout] = true;
    }
    for (bool c : covered) if (!c) return false;
    return true;
}
```

**Header comment block (lines 1–40):** Replace all "render/device" language with internal/output. Remove references to CSV as the primary routing mechanism. State that `buildAuto()` is the standard path and `load()` is the legacy CSV path.

**`RemapEntry` field names:** The fields are currently `layout` (internal) and `device` (output). These are used by both `buildAuto()` and the legacy `load()` path. Do not rename them in this patch — the struct is used internally only and the field names are not user-visible. Add a comment clarifying: "`layout` = internal channel index; `device` = output channel index."

---

### `spatial_engine/realtimeEngine/src/RealtimeTypes.hpp`

**`RealtimeConfig::outputChannels` comment (lines 157–166):**

```cpp
// Physical output bus width — derived from the speaker layout's deviceChannel
// values: outputChannelCount = max(all .deviceChannel values) + 1.
// Set by Spatializer::init() and read by RealtimeBackend::init() to open
// AudioIO with the correct physical channel count.
// This is NOT the internal bus width. Internal bus width (numSpeakers +
// numSubwoofers) is owned by Spatializer and never stored in RealtimeConfig.
int outputChannels = 0;
```

---

### `spatial_engine/realtimeEngine/src/EngineSession.cpp`

**`configureRuntime()` — remap block (lines 155–165):**

Fix the stale bug: `mConfig.outputChannels` is currently passed as both `renderChannels` and `deviceChannels` to `load()`. After this patch:

```cpp
mOutputRemap = std::make_unique<OutputRemap>();
if (!mRemapCsv.empty()) {
    // LEGACY: CSV remap is deprecated. Layout-derived output routing is now standard.
    // This path remains temporarily as internal scaffolding during validation only.
    std::cerr << "[EngineSession] WARNING: --remap CSV is deprecated. "
              << "Physical output routing is now derived from the speaker layout JSON. "
              << "CSV support will be removed after validation." << std::endl;
    bool remapOk = mOutputRemap->load(mRemapCsv,
        mSpatializer->numInternalChannels(),  // internal bus width
        mConfig.outputChannels);              // output bus width
    if (remapOk) {
        mSpatializer->setRemap(mOutputRemap.get());
    } else {
        std::cout << "[EngineSession] CSV load failed — retaining layout-derived routing." << std::endl;
    }
} else {
    std::cout << "[EngineSession] Layout-derived output routing active ("
              << mSpatializer->numInternalChannels() << " internal → "
              << mConfig.outputChannels << " output channels)." << std::endl;
    // mRemap is already set to &mOutputRouting by Spatializer::init().
}
```

---

### `spatial_engine/realtimeEngine/src/EngineSession.hpp`

**`LayoutInput::remapCsvPath` deprecation comment:**

```cpp
// DEPRECATED: physical output routing is now derived from layout deviceChannel values.
// This field is retained temporarily as internal scaffolding only.
// Not a supported user workflow. Will be removed after layout-routing validation.
std::string remapCsvPath;
```

---

### `spatial_engine/realtimeEngine/src/main.cpp`

**`--remap` flag (line 159):**

```cpp
// DEPRECATED: CSV remap path. Not a supported user workflow.
// Retained temporarily as internal scaffolding. Will be removed.
layoutIn.remapCsvPath = getArgString(argc, argv, "--remap");
```

---

### `gui/imgui/src/App.cpp` and `App.hpp`

**Current plan** keeps the CSV field visible with a `TextDisabled` deprecation label. This is insufficient for the product intent.

**Revised treatment:** Move the CSV field into a collapsible "Legacy / Internal" section that is visually hidden by default:

```cpp
if (ImGui::CollapsingHeader("Legacy / Internal (not a supported workflow)")) {
    ImGui::TextDisabled("REMAP CSV — deprecated. Output routing is now layout-derived.");
    // ... existing Browse button, kept functional for internal validation only
}
```

This makes the CSV path visually non-primary without removing it from the build. Users who need it for validation can expand the section; users following normal workflow will never see it.

**`App.hpp:78`:**

```cpp
std::string mRemapPath; // DEPRECATED — remove after layout-routing validation
```

---

## 9. Remaining Single-Bus Assumptions — Audit List

### 9.0 Offline renderer — out of scope (confirmed)

`SpatialRenderer::renderPerBlock()` (`spatial_engine/src/renderer/SpatialRenderer.cpp`) sizes its `audioIO` to `numSpeakers` (consecutive 0-based) and calls `audioIO.zeroOut()` unconditionally. It does not use `OutputRemap`, does not read `deviceChannel`, and explicitly documents that device channel numbering is ignored: output channels in the offline WAV are consecutive and can be remapped externally. This is a completely separate code path with its own channel model. This patch must not touch it.

The following are likely sites of residual single-bus assumptions outside the already-identified list. These must be verified by grep before coding begins.

1. **`mFastMoverScratch`** — sized to `computedOutputChannels` in the current code. After the patch it must be sized to `internalChannelCount`. Verify: search for all uses of `mFastMoverScratch.channelsOut(` and confirm only one write site exists.

2. **Phase 14 loop bounds** — the device-bus diagnostic loop (lines 869–944) iterates `ch = 0..numOutputChannels-1`. Verify that `numOutputChannels` is taken from `io.channelsOut()` (the real AudioIO buffer, which equals `outputChannelCount`) and not from `mRenderIO.channelsOut()` (which equals `internalChannelCount`).

3. **`spatialRender/` offline renderer** — `spatialroot_spatial_render` has its own channel calculation logic. Verify it does not share `Spatializer.hpp` and that its own channel model is unaffected by this patch.

4. **AlloLib `al::AudioIO` framesPerBuffer / channelsOut interaction** — confirm that calling `mRenderIO.channelsOut(internalChannelCount)` followed by `io.channelsOut(outputChannelCount)` for the real AudioIO device produces independent buffer allocations with no aliasing.

5. **Any log strings that say "render channels"** — sweep with `grep -rn "render channel"` and replace with "internal channels" in all non-schema, non-history text.

6. **`numRenderChannels()` call sites** — there is at least one in `EngineSession.cpp:188` (current plan). After renaming to `numInternalChannels()`, confirm there are no other callers via grep.

---

## 10. CSV Deprecation and Removal Path

### Immediate (this patch)

- GUI: CSV field demoted to collapsed "Legacy / Internal" section, hidden by default.
- CLI `--remap`: adds deprecation comment; behavior unchanged.
- `EngineSession.cpp`: adds `[WARNING]` to stderr when `--remap` is used.
- `EngineSession.hpp`: `remapCsvPath` gets deprecation comment.
- No removal yet.

### After validation (next step, not this patch)

When layout-derived routing has been verified against all target layouts (translab + allosphere), in a separate commit:

1. Remove `--remap` flag from `main.cpp`.
2. Remove `remapCsvPath` from `EngineSession.hpp` and `LayoutInput`.
3. Remove the CSV branch from `EngineSession.cpp::configureRuntime()`.
4. Remove the CSV UI block from `App.cpp` entirely.
5. Remove `mRemapPath` from `App.hpp`.
6. The `OutputRemap::load()` method may remain in `OutputRemap.hpp` for reference or future tooling use, but it is no longer called from any engine path.

---

## 11. Risks and Test Plan

### Risks

| Risk | Mitigation |
|---|---|
| Internal/output bus size mismatch passed to mRenderIO vs mFastMoverScratch | Audit all `channelsOut()` write sites; add invariant check in `init()` that `mRenderIO.channelsOut() == internalChannelCount` after init |
| Backend pre-zero guarantee for scatter path not actually upheld | Add explicit comment documenting precondition; grep `RealtimeBackend` for io buffer zeroing behavior before coding |
| `numRenderChannels()` rename breaks a call site not yet found | Grep exhaustively before renaming; rename is safe to defer to last if uncertain |
| Phase 14 device-bus diagnostic uses wrong loop bound after split | Verify `numOutputChannels` source in that block is `io.channelsOut()` not `mRenderIO.channelsOut()` |
| `EngineSession.cpp:158` stale bug (outputChannels as both args) is copied into the new path | Explicitly listed as required fix in §6; cannot be missed |

### Test Plan

**Build verification:**
- `./build.sh --engine-only` — fix any compile errors from renamed members before proceeding.

**Identity layout (translab):**
- Consecutive `deviceChannel` values `0..17`, subwoofer at `18` or consecutive.
- Expected: log shows `identity copy active`, `mOutputRouting.identity() == true`.
- Verify audio output is correct and nanGuardCount stays 0.

**Sparse layout (allosphere):**
- Speakers starting at `deviceChannel 1` (or with gaps in the sequence).
- Expected: log shows `scatter routing active`, routing entries listed, device channels match layout JSON.
- Verify audio routes to correct physical outputs.
- Verify unmapped output channels (e.g. channel 0 if allosphere starts at 1) produce no audio.

**Leading gap test:**
- Layout where lowest speaker `deviceChannel` is > 0 (e.g. all channels start at `deviceChannel 4`).
- Expected: output channels `0..3` are silent; speaker audio appears at channels `4+`.

**Middle gap test:**
- Layout where `deviceChannel` values are non-consecutive (e.g. 0, 1, 3, 5 — missing 2 and 4).
- Expected: output channels 2 and 4 are silent; speakers at 0, 1, 3, 5 receive audio.

**Trailing gap test:**
- Layout where `outputChannelCount` is larger than the highest speaker index (e.g. speakers at 0–7, subwoofer at 16, outputChannelCount = 17).
- Expected: output channels 8–15 are silent.

**Validation failures:**
- Layout with duplicate speaker `deviceChannel` → hard fail, engine refuses to start, descriptive error logged.
- Layout with speaker and subwoofer sharing `deviceChannel` → same.
- Layout with negative `deviceChannel` → same.
- Layout with no speakers → same.

**Legacy CSV path (internal validation only):**
- Run with `--remap` flag: confirm `[WARNING]` in stderr, CSV still applied correctly with `numInternalChannels()` as the internal-bus argument (not `outputChannels`).

**Phase 14 diagnostic stability:**
- After routing patch, confirm `renderDomMask` and `deviceDomMask` remain stable (no spurious relocation events).
- In identity case, both masks should be equal.
- In scatter case, masks should reflect the output-channel layout.
