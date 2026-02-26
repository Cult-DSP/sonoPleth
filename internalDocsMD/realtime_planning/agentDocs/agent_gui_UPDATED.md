# GUI Agent — Phase 9 (PySide6, Real-Time Launcher + Runtime Controls)

> **Implementation Status: 🚧 IN PROGRESS / NEEDS UPDATE (Phase 9, Feb 2026)**
>
> **Current reality (per realtime master):** the C++ real-time engine is complete through **Phase 8** (backend, streaming,
> pose/control, DBAP spatializer, ADM direct streaming, compensation/gain, output remap, threading audit).  
> **Phase 9 is the GUI integration layer**: extend the existing **PySide6** app (in `gui/`) with a **Real‑Time** tab/panel.
>
> **Decisions / constraints**
> - **Keep PySide6** (do **not** switch to ImGui for the main app UI).
> - **Skip scene visualization for now** (no OpenGL cube/root particle widget in Phase 9 v1).
> - Launch the real-time pipeline via **`runRealtime.py` using `QProcess`**.
> - Expose the Phase 6 “mix trims + auto compensation” as **runtime controls** (live, no restart), using a small IPC control channel
>   because the GUI and engine run in separate processes.

---

## 1. What the GUI is responsible for

### 1.1 Launch-time configuration (process start)
The GUI must collect inputs and launch the pipeline:

- **Source**: either
  - an **ADM WAV** file (`.wav`), or
  - a **LUSID package directory** (contains `scene.lusid.json` + mono WAVs).
- **Speaker layout JSON**
- **Optional output remap CSV** (Phase 7)
- **Buffer size** (frames)
- **(ADM only) Scan audio** toggle (optional; adds startup time but filters silent channels)

The GUI launches `runRealtime.py` with the corresponding arguments via `QProcess`.

### 1.2 Runtime controls (live, while audio is playing)
Phase 6 introduced three **live atomics** in `RealtimeConfig` that are explicitly intended to be controlled at runtime:

- `loudspeakerMix` (post-DBAP trim for non-sub channels)
- `subMix` (post-LFE trim for sub channels)
- `focusAutoCompensation` (toggle)

In addition, you will typically want runtime adjustment of:
- **Master gain**
- **DBAP focus**

Because the GUI and engine run as separate processes, runtime controls require a small **IPC control channel** (see §4).

---

## 2. Real‑Time tab: UI spec (no visualization)

### 2.1 Launch-time section (top of tab)

**Inputs**
- Source picker + text field (ADM WAV or LUSID package dir)
- Layout JSON picker + text field
- Remap CSV picker (optional)
- Buffer size dropdown: `64, 128, 256, 512, 1024` (default 512)
- ADM-only: `Scan audio` checkbox (default OFF)

**Buttons**
- **Start**
- **Stop** (graceful)
- **Kill** (force terminate)
- **Copy Command** (copies the last launched CLI)
- (Optional) **Open stage directory** (`processedData/stageForRender/`)

**Status**
- Status pill: `Idle / Running / Exited(code) / Error`
- Last exit code + timestamp

### 2.2 Runtime controls section (middle of tab)

These should remain enabled while running and send IPC messages on change:

1) **Master Gain** (linear)
- Range: `0.0–1.0`
- Default: `0.5`

2) **DBAP Focus** (linear)
- Range: `0.2–5.0`
- Default: `1.5`

3) **Loudspeaker Mix (dB)** (post-DBAP trim)
- Range: `-10 dB … +10 dB`
- Default: `0 dB`

4) **Sub Mix (dB)** (post-LFE trim)
- Range: `-10 dB … +10 dB`
- Default: `0 dB`

5) **Auto Compensation** (toggle)
- Default: OFF
- Behavior: when ON and focus changes, engine recomputes compensation and updates loudspeaker mix (clamped to ±10 dB).
  The loudspeaker mix slider must remain interactive.

**Important:** Label clearly which controls are “live” vs “apply on restart”. In Phase 9 v1,
all five above are intended to be **live**.

### 2.3 Logs section (bottom of tab)

- Live stdout + stderr console
- Cap to last N lines (e.g., 2000) to keep UI snappy
- Clear button
- Optional search/filter box

---

## 3. Process model: `QProcess` contract

### 3.1 Start
- Launch `python runRealtime.py ...` from the repo root (or ensure working directory is repo root).
- Capture stdout/stderr and append to the GUI log console.

### 3.2 Stop / Kill
- Stop: attempt graceful shutdown (SIGINT / terminate). Wait briefly; if hung, fall back to kill.
- Kill: hard terminate (only for stuck states).

### 3.3 “Apply on restart” fallback (only if IPC not ready)
If IPC is not implemented yet, you may temporarily:
- Keep runtime sliders visible but disabled while running, with a note “changes apply on restart”.

However, the realtime pipeline docs explicitly treat these as live controls; IPC is the intended solution.

---

## 4. Runtime control IPC (Phase 9a, REQUIRED for live sliders)

### 4.1 Why IPC is needed
PySide6 GUI launches the engine as an external process via `runRealtime.py`.  
To update `RealtimeConfig` atomics while running, the GUI must send messages to the engine.

### 4.2 Recommended IPC shape (minimal + robust)
Use one of:
- **OSC over UDP** (simple, common in audio tooling), or
- **Plain UDP** with a tiny text protocol, or
- **Local TCP socket**.

OSC is recommended because it’s easy to inspect and extend.

### 4.3 Message set (suggested)
Define these addresses (or equivalents):

- `/realtime/gain <float>`              (0..1)
- `/realtime/focus <float>`             (0.2..5)
- `/realtime/speaker_mix_db <float>`    (-10..10)
- `/realtime/sub_mix_db <float>`        (-10..10)
- `/realtime/auto_comp <int>`           (0/1)

Engine-side handler:
- runs on **main/control thread**, never in audio callback
- writes to the `RealtimeConfig` atomics (or to a mailbox consumed by main thread)

### 4.4 Port selection
- Choose a default localhost port (e.g., `9009`) and make it configurable.
- GUI must display the active port when running.

### 4.5 Wiring note
If you already have a “pose/control” input channel planned, reuse it. Otherwise, implement a small dedicated control receiver
that only writes atomics.

---

## 5. Required plumbing updates (to reflect current repo state)

### 5.1 `runRealtime.py` currently exposes only:
- source, layout, master gain, dbap focus, buffer size, and `--scan_audio`.

It does **not** currently forward:
- `--remap <path>`
- `--speaker_mix <dB>`
- `--sub_mix <dB>`
- `--auto_compensation`

Phase 9 must either:
1) extend `runRealtime.py` to accept and forward these flags to the C++ engine at launch, **and**
2) implement IPC so the GUI can change the values live.

### 5.2 C++ engine already supports (per Phase 6/7):
- `--speaker_mix <dB>`
- `--sub_mix <dB>`
- `--auto_compensation`
- `--remap <path>`

Phase 9 should treat these as canonical.

---

## 6. Testing checklist

### Launch path
- ADM WAV + layout → start, verify audio
- ADM with scan_audio enabled → start, verify longer startup but succeeds
- LUSID package + layout → start, verify audio
- Optional remap CSV → verify routing changes

### Runtime controls (IPC)
- While running, change gain/focus/speaker mix/sub mix; verify immediate audible effect
- Toggle auto compensation ON; change focus; verify loudspeaker mix updates and remains bounded
- Stop and restart repeatedly; ensure no orphan processes

### Failure handling
- Missing layout/source → block Start with UI error
- Process crashes → status shows exit code, logs preserved

---

## 7. “Scene visualization” (explicitly deferred)
We are deferring the brand-forward scene visualization component (grid cube + root particle system)
until after Phase 9 runtime controls are stable. Keep this in the backlog; do not implement in v1.
