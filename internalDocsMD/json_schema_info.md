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

| Field        | Type    | Required | Default     | Description                                |
| ------------ | ------- | -------- | ----------- | ------------------------------------------ |
| `sampleRate` | integer | **Yes**  | -           | Sample rate in Hz (must match audio files) |
| `timeUnit`   | string  | No       | `"seconds"` | Unit for keyframe timestamps (see below)   |
| `sources`    | object  | **Yes**  | -           | Map of source names to keyframe arrays     |

### Time Units (`timeUnit`)

The `timeUnit` field specifies how keyframe timestamps should be interpreted:

| Value            | Aliases  | Description                                                   |
| ---------------- | -------- | ------------------------------------------------------------- |
| `"seconds"`      | `"s"`    | **Default.** Times are in seconds (e.g., `1.5` = 1.5 seconds) |
| `"samples"`      | `"samp"` | Times are sample indices (e.g., `48000` = 1 second at 48kHz)  |
| `"milliseconds"` | `"ms"`   | Times are in milliseconds (e.g., `1500` = 1.5 seconds)        |

**Important:** Always specify `timeUnit` explicitly to avoid ambiguity. The renderer has legacy heuristic detection but it may produce warnings or incorrect results.

### Keyframe Format

Each keyframe specifies a position at a point in time:

```json
{
  "time": 0.0,
  "cart": [x, y, z]
}
```

| Field  | Type     | Description                                   |
| ------ | -------- | --------------------------------------------- |
| `time` | number   | Timestamp in the unit specified by `timeUnit` |
| `cart` | array[3] | Cartesian direction vector `[x, y, z]`        |

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
    "src_drums": [{ "time": 0.0, "cart": [0.0, 1.0, -0.3] }]
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
  ],
  "subwoofers": [
    {
      "channel": 16
    },
    {
      "channel": 17
    }
  ]
}
```

### Fields

| Field        | Type  | Description                                           |
| ------------ | ----- | ----------------------------------------------------- |
| `speakers`   | array | Array of speaker definitions                          |
| `subwoofers` | array | (Optional) Array of subwoofer/LFE channel definitions |

### Speaker Definition

| Field           | Type    | Unit        | Description                                                     |
| --------------- | ------- | ----------- | --------------------------------------------------------------- |
| `azimuth`       | number  | **radians** | Horizontal angle (0 = front, positive = right)                  |
| `elevation`     | number  | **radians** | Vertical angle (0 = horizon, positive = up)                     |
| `radius`        | number  | meters      | Distance from center (typically 5.0 for AlloSphere)             |
| `deviceChannel` | integer | -           | Hardware channel number (informational, not used for rendering) |

### Subwoofer Definition

| Field     | Type    | Description                             |
| --------- | ------- | --------------------------------------- |
| `channel` | integer | Output channel index for this subwoofer |

**Note:** The renderer uses consecutive channel indices (0, 1, 2, ...) for speakers regardless of `deviceChannel` values. Subwoofer channels can be non-consecutive or beyond the speaker count. Channel remapping to hardware channels should be done during playback.

---

## Subwoofer / LFE Handling

The renderer provides special handling for Low Frequency Effects (LFE) and subwoofer channels:

### Automatic LFE Detection

If a source is named **`LFE`** in the spatial instructions JSON, it will bypass spatialization and be sent directly to all defined subwoofer channels. This source:

- **Does not** get processed by the spatializer (VBAP/DBAP/LBAP)
- **Is distributed equally** to all subwoofer channels defined in the speaker layout
- **Receives gain compensation** via `dbap_sub_compensation` factor (currently 0.95)
- **Follows master gain** like all other sources

### Example

If your speaker layout has:

```json
"subwoofers": [{ "channel": 16 }, { "channel": 17 }]
```

And your spatial instructions include:

```json
"sources": {
  "LFE": [...],
  "src_vocals": [...]
}
```

Then:

- `src_vocals` will be spatialized across speakers 0-15 using the selected spatializer
- `LFE` will be sent directly to channels 16 and 17 (split evenly, with ~0.95x gain factor)

### Subwoofer Output Channels

Subwoofer channels are defined in the speaker layout JSON and can be:

- **Beyond speaker count** (e.g., channels 16-17 when there are 16 speakers)
- **Non-consecutive** (e.g., channels 16, 18, 22)
- **Mixed with speaker channels** (though typically placed after)

The output WAV file will have enough channels to accommodate the highest subwoofer channel index.

### Important Notes

1. **Only sources named "LFE"** trigger this behavior - other source names will be spatialized normally
2. **LFE sources still require spatial data** in the JSON (though it's not used for positioning)
3. **Gain compensation** (`dbap_sub_compensation = 0.95`) is applied to prevent clipping when distributing to multiple subs
4. If no subwoofers are defined in the layout, the LFE source will be skipped (not rendered)

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
6. **LFE/Subwoofer channels** - Name LFE sources exactly `"LFE"` to bypass spatialization and route to subwoofers
7. **Subwoofer layout** - Define `subwoofers` array in speaker layout JSON if your system has dedicated LFE channels
