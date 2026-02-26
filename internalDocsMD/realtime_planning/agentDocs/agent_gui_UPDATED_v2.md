# GUI Agent — Phase 9 (PySide6, Dedicated Real-Time GUI + Runtime Controls)

> **Implementation Status: 🚧 Phase 9 (Feb 2026)**
>
> **Current reality:** real-time C++ engine is complete through **Phase 8** (backend adapter, streaming, pose/control,
> DBAP spatializer, ADM direct streaming, compensation/gain, output remap, threading/safety audit).  
> **Phase 9 is GUI integration.**
>
> **Decisions / constraints (authoritative)**
> - **Keep PySide6** as the GUI framework (do **not** switch to ImGui for the main app).
> - **Skip scene visualization for now**.
> - **Do NOT bloat the existing GUI code**: create a **new** dedicated GUI entry for realtime.
> - Real-time GUI should launch the realtime pipeline via **`runRealtime.py` using `QProcess`**.
> - Runtime sliders must be truly **live** (no restart) via a minimal **IPC control channel** (OSC/UDP recommended).
> - Add **Play / Pause / Restart** controls (requires a small new engine/launcher capability; see §6).

---

## 0. Directory / file layout (new realtime GUI parallel to existing)

We will add a parallel realtime GUI implementation so the existing offline GUI remains clean.

**Recommended structure:**
- `gui/` (existing offline GUI code unchanged)
- `gui/realtimeGUI/` (new folder)
  - `realtimeGUI.py` (new main entry point)
  - `realtime_panels/` (optional: keep code modular)
    - `RealtimeInputPanel.py`
    - `RealtimeControlsPanel.py`
    - `RealtimeLogPanel.py`
    - `RealtimeTransportPanel.py`
  - `realtime_runner.py` (new `RealtimeRunner` class wrapping `QProcess` + IPC sender)

**Rule:** Keep realtime GUI code self-contained inside `gui/realtimeGUI/` unless a small shared widget can be cleanly reused
(e.g., a file picker component). The default is **no reuse** if it increases coupling.

---

## 1. What the Real-Time GUI is responsible for

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
Phase 6 introduced three **live atomics** in `RealtimeConfig` intended for runtime control:

- `loudspeakerMix` (post-DBAP trim for non-sub channels)
- `subMix` (post-LFE trim for sub channels)
- `focusAutoCompensation` (toggle)

Additionally expose:
- **Master gain**
- **DBAP focus**

Because the GUI and engine run as separate processes, runtime controls require a small **IPC control channel** (see §5).

### 1.3 Transport controls (Play / Pause / Restart)
Add:
- **Play** (start or resume)
- **Pause** (pause audio processing without tearing down the app)
- **Restart** (relaunch from a clean state)

This requires a small addition to the engine/launcher interface; see §6.

---

## 2. Real-Time GUI: UI spec (no visualization)

### 2.1 Launch-time section

**Inputs**
- Source picker + text field (ADM WAV or LUSID package dir)
- Layout JSON picker + text field
- Remap CSV picker (optional)
- Buffer size dropdown: `64, 128, 256, 512, 1024` (default 512)
- ADM-only: `Scan audio` checkbox (default OFF)

**Buttons**
- **Start** (initial launch)
- **Stop** (graceful shutdown)
- **Kill** (force terminate)
- **Restart** (Stop + Start with same config)
- **Copy Command** (copies the last launched CLI)
- (Optional) **Open stage directory** (`processedData/stageForRender/`)

**Status**
- Status pill: `Idle / Launching / Running / Paused / Exited(code) / Error`
- Last exit code + timestamp

### 2.2 Runtime controls section (live via IPC)

These remain enabled while running (and while paused, if the engine supports updating state):

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
  Loudspeaker mix slider remains interactive.

**Control types**
- Sliders + numeric entry fields for all numeric parameters
- Checkbox / toggle for booleans
- File picker for remap CSV

### 2.3 Transport section (Play / Pause)

Transport is separate from process lifecycle:
- **Play**: resume processing if paused; if not running, starts.
- **Pause**: pauses processing (engine continues running; audio muted / processing halted).
- **Stop**: exits the process (full teardown).
- **Restart**: stop + relaunch.

If Pause is not yet implemented in the engine, the GUI must show it disabled with a tooltip “requires engine support”.

### 2.4 Logs section
- Live stdout + stderr console
- Cap last N lines (e.g., 2000)
- Clear button
- Optional search/filter

---

## 3. Process model: `QProcess` + `RealtimeRunner`

### 3.1 Create a new `RealtimeRunner`
Implement `gui/realtimeGUI/realtime_runner.py`:

Responsibilities:
- Build the `runRealtime.py` command (repo-root working dir)
- Launch via `QProcess`
- Capture stdout/stderr → emit Qt signals to append logs
- Expose methods: `start(config)`, `stop_graceful()`, `kill()`, `restart()`
- Track state: idle/running/paused/exited and exit codes

**Do not reuse** the offline `PipelineRunner` unless it is clearly extracted into a shared base class without coupling.

### 3.2 Stop vs Kill
- Stop: attempt graceful shutdown (SIGINT / terminate). Wait briefly then kill if needed.
- Kill: hard terminate for stuck states.

---

## 4. Mode detection (ADM vs LUSID)
Use the same **auto-detect** logic as `runRealtime.py`. The GUI does not need a mode dropdown.

- `.wav` → ADM direct streaming mode
- directory containing `scene.lusid.json` → LUSID package mode

The GUI may display a small hint (“Detected ADM” / “Detected LUSID package”) but should not enforce extra steps.

---

## 5. Runtime control IPC (REQUIRED for live sliders)

### 5.1 Why IPC is required
Real-time engine runs in a separate process started by the GUI.  
To update `RealtimeConfig` atomics while running, GUI must send runtime updates via IPC.

### 5.2 Recommended IPC
- **OSC over UDP** on localhost (default port suggested: `9009`, configurable)

### 5.3 Message set (suggested)
- `/realtime/gain <float>`              (0..1)
- `/realtime/focus <float>`             (0.2..5)
- `/realtime/speaker_mix_db <float>`    (-10..10)
- `/realtime/sub_mix_db <float>`        (-10..10)
- `/realtime/auto_comp <int>`           (0/1)

### 5.4 GUI-side behavior
- Send on change with debouncing (e.g., 30–60ms) to avoid spamming.
- Display the active port in the UI.
- If IPC is not available yet, temporarily disable runtime sliders while running and label them “Apply on restart”.

### 5.5 Engine-side behavior
- Receive messages on a control thread (never audio callback)
- Write to atomics (or mailbox consumed by main thread)

---

## 6. NEW: Engine/launcher requirements for Play / Pause

To support Play/Pause cleanly, we need explicit capability in the realtime engine/launcher layer.

### 6.1 Minimum viable pause model (recommended)
Implement an engine flag/state like:
- `running` (process alive)
- `paused` (processing muted/disabled)

Pause behavior:
- audio callback outputs silence (or bypass) while `paused=true`
- state flips happen outside the audio callback (atomic flag read in callback is OK)

Control interface:
- IPC messages:
  - `/realtime/pause <int>` (0/1)
  - `/realtime/play <int>`  (0/1)  (or reuse pause=0)
Optionally:
- `/realtime/restart` (GUI can also just stop+start)

### 6.2 Document it
Add a short section to the realtime engine MD(s) describing:
- how pause is implemented RT-safely
- what IPC messages trigger it
- expected user-visible behavior

---

## 7. Defaults (must match engine/launcher defaults)

- `masterGain`: **0.5**
- `focus`: **1.5**
- `buffer_size`: **512**
- `loudspeakerMix_db`: **0 dB**
- `subMix_db`: **0 dB**
- `auto_compensation`: **OFF**
- `scan_audio`: **OFF**
- `remap_csv`: none

---

## 8. Testing checklist

### Launch
- ADM WAV + layout → start, verify audio
- ADM with scan_audio enabled → start, verify longer startup but succeeds
- LUSID package + layout → start, verify audio
- Remap CSV → verify routing changes

### Runtime controls (IPC)
- While running, change gain/focus/speaker mix/sub mix; verify immediate audible effect
- Toggle auto compensation ON; change focus; verify loudspeaker mix updates and remains bounded

### Transport
- Pause → audio mutes/halts but process remains running
- Play → resumes audio
- Restart → clean teardown and relaunch; no orphan processes

### Failure handling
- Missing layout/source → block Start with UI error
- Process crash → show exit code + preserve logs

---

## 9. Backlog / deferred
- Scene visualization (grid cube + root particle system) is **deferred** until after Phase 9 stability.
- CPU/xrun meters are deferred until engine emits stable machine-readable metrics.
