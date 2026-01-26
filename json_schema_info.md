# JSON Schema Documentation

This document describes the JSON file formats used by the sonoPleth spatial audio renderer.

## Spatial Instructions JSON (`renderInstructions.json`)

This file contains the spatial trajectory data for each audio source.

### Schema

```json
{
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "sources": {
    "source_name_1": [
      { "time": 0.0, "cart": [x, y, z] },
      { "time": 1.5, "cart": [x, y, z] }
    ],
    "source_name_2": [...]
  }
}
```

### Fields

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `sampleRate` | integer | **Yes** | - | Sample rate in Hz (must match audio files) |
| `timeUnit` | string | No | `"seconds"` | Unit for keyframe timestamps (see below) |
| `sources` | object | **Yes** | - | Map of source names to keyframe arrays |

### Time Units (`timeUnit`)

The `timeUnit` field specifies how keyframe timestamps should be interpreted:

| Value | Aliases | Description |
|-------|---------|-------------|
| `"seconds"` | `"s"` | **Default.** Times are in seconds (e.g., `1.5` = 1.5 seconds) |
| `"samples"` | `"samp"` | Times are sample indices (e.g., `48000` = 1 second at 48kHz) |
| `"milliseconds"` | `"ms"` | Times are in milliseconds (e.g., `1500` = 1.5 seconds) |

**Important:** Always specify `timeUnit` explicitly to avoid ambiguity. The renderer has legacy heuristic detection but it may produce warnings or incorrect results.

### Keyframe Format

Each keyframe specifies a position at a point in time:

```json
{
  "time": 0.0,
  "cart": [x, y, z]
}
```

| Field | Type | Description |
|-------|------|-------------|
| `time` | number | Timestamp in the unit specified by `timeUnit` |
| `cart` | array[3] | Cartesian direction vector `[x, y, z]` |

#### Cartesian Coordinates

The `cart` array specifies a **direction vector** (not absolute position):
- **x**: Left (-) to Right (+)
- **y**: Back (-) to Front (+)
- **z**: Down (-) to Up (+)

The vector will be normalized to unit length by the renderer. Zero-length vectors will trigger a fallback to a nearby valid direction.

### Example

```json
{
  "sampleRate": 48000,
  "timeUnit": "seconds",
  "sources": {
    "src_vocal": [
      { "time": 0.0, "cart": [0.0, 1.0, 0.0] },
      { "time": 2.5, "cart": [0.7, 0.7, 0.0] },
      { "time": 5.0, "cart": [-0.7, 0.7, 0.0] }
    ],
    "src_drums": [
      { "time": 0.0, "cart": [0.0, 1.0, -0.3] }
    ]
  }
}
```

This example:
- Uses seconds for timestamps
- Has a vocal source that moves from front → front-right → front-left
- Has a drums source that stays fixed slightly below front

---

## Speaker Layout JSON (`allosphere_layout.json`)

This file defines the speaker positions for the target playback system.

### Schema

```json
{
  "speakers": [
    {
      "azimuth": 0.0,
      "elevation": 0.0,
      "radius": 5.0,
      "deviceChannel": 1
    }
  ]
}
```

### Fields

| Field | Type | Description |
|-------|------|-------------|
| `speakers` | array | Array of speaker definitions |

### Speaker Definition

| Field | Type | Unit | Description |
|-------|------|------|-------------|
| `azimuth` | number | **radians** | Horizontal angle (0 = front, positive = right) |
| `elevation` | number | **radians** | Vertical angle (0 = horizon, positive = up) |
| `radius` | number | meters | Distance from center (typically 5.0 for AlloSphere) |
| `deviceChannel` | integer | - | Hardware channel number (informational, not used for rendering) |

**Note:** The renderer uses consecutive channel indices (0, 1, 2, ...) regardless of `deviceChannel` values. Channel remapping to hardware channels should be done during playback.

---

## Validation

The renderer performs the following validation:

1. **Keyframe validation**: Drops keyframes with NaN/Inf values
2. **Zero vector handling**: Replaces zero-length direction vectors with front (0, 1, 0)
3. **Time sorting**: Sorts keyframes by time ascending
4. **Duplicate removal**: Collapses keyframes with identical timestamps
5. **Time unit detection**: Falls back to heuristic if `timeUnit` not specified (with warning)

---

## Best Practices

1. **Always specify `timeUnit`** - Don't rely on heuristic detection
2. **Use seconds** when possible - Most intuitive and least error-prone
3. **Normalize direction vectors** - While the renderer normalizes them, pre-normalized vectors are cleaner
4. **Match sample rates** - Ensure `sampleRate` matches your audio files
5. **Source naming** - Use consistent naming between JSON and WAV files (e.g., `src_vocal` → `src_vocal.wav`)
