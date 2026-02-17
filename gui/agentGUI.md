# agentGUI.md

## sonoPleth GUI v0.1 — PySide6 Implementation Specification (Light Mode)

This spec is for **agents** implementing the UI. It is intentionally strict and complete.  
Target aesthetic: **between “research lab instrument” and “boutique experimental tool”**, matching `sonoPleth-mockup.png` light mode.

---

## 0. Non-negotiables

- **No spatial interaction canvas** (sonoPleth is not SpatialSeed).
- “Spatial” presence is **purely vibe**: subtle, non-interactive geometry background and restrained circular motifs.
- **Minimal**: negative space, thin dividers, soft cards, SF-like typography.
- **Deterministic**: pipeline steps, logs, and progress are the center of the UX.

---

## 1. Technology

- Python 3.11+
- PySide6 (Qt for Python)
- QSS for styling
- QPainter for background geometry
- QProcess for running `runPipeline.py` with live stdout streaming

Forbidden: Electron/web UI, heavy glassmorphism, neon, animated particles.

---

## 2. Repo placement

Add a `gui/` package next to `runPipeline.py`:

```
sonoPleth/
  runPipeline.py
  gui/
    main.py
    styles.qss
    background.py
    pipeline_runner.py
    widgets/
      stepper.py
      header.py
      input_panel.py
      render_panel.py
      pipeline_panel.py
```

---

## 3. Window + layout

- Default size: **1100 × 720**
- Minimum: **1000 × 650**
- Outer padding: **24**
- Section spacing: **24**
- Panel spacing (between left and right cards): **48**

Main structure:

```
[Header]
[Left Input Card]   [Right Render Settings Card]
[Pipeline Card (full width): stepper + console + actions]
```

---

## 4. Visual tokens

### 4.1 Color palette (light mode)

- `--bg-main`: `#F4F4F2` (window)
- `--bg-panel`: `rgba(255,255,255,0.72)` (cards)
- `--stroke`: `rgba(0,0,0,0.06)` (hairline borders)
- `--text-main`: `#1C1C1E`
- `--text-sub`: `#6E6E73`
- `--accent`: `#4C6FFF` (muted blue)
- `--ok`: `#4CAF82` (status)

### 4.2 Radii + shadows

- Cards: **14px**
- Primary button: **14px**
- Hairline borders only
- Shadows are soft, wide, low opacity

### 4.3 Typography

- System UI font (SF Pro on macOS), fallback Inter.
- Title: 18px Medium
- Section: 16px Medium
- Body: 13px Regular
- Console: 12px Regular (optional monospace)

---

## 5. Header specification

Height: ~60px.

Left:

- App name: `sonoPleth`
  Center:
- Subtitle: `ADM → LUSID → Spatial Render`
  Right:
- Status dot + `Init Status: Ready`
- `About` text button

Border bottom: hairline.

---

## 6. Input panel (left card)

Title: `Input`

Primary control:

- Button: `SELECT ADM OR LUSID` (44px tall)

Status checklist:

- ADM Source
- Metadata Extracted
- Channel Activity Ready

---

## 7. Render settings (right card)

Title: `Render Settings`

Controls:

- Combo: DBAP Focus / DBAP / LBAP
- Resolution slider (0.1–1.0) + numeric pill
- Master gain slider (-20..+20)
- Create Analysis toggle

Primary action:

- `RUN RENDER` (52px height) with soft shadow.

---

## 8. Pipeline panel (bottom full-width card)

Title row:

- `Pipeline`
- Stepper (7 dots)
- Optional `View Full Logs` button

Console:

- Read-only
- Live streamed output
- Auto-scroll unless user scrolled up

Stepper: step markers parsed from output (prefer `STEP N`).

---

## 9. Background geometry

Non-interactive, very subtle.

- Concentric circles centered in window
- Crosshair + diagonals
- Alpha 0.03–0.06
- Stroke width 1px

---

## 10. Pipeline execution

Use QProcess:

- Start: `python runPipeline.py <args>`
- Merge stdout+stderr
- Append to console
- Parse step markers
- On finish: re-enable button and surface exit code

No blocking calls on UI thread.

---

## 11. Acceptance checklist

- Looks close to the mockup’s spacing/softness.
- Background geometry is barely visible but present.
- Output streams live while running.
- Stepper progresses without freezing.

---

## 12. First-run

```bash
pip install -r requirements.txt
python gui/main.py
```
