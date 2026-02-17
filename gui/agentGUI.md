# Agent Instructions: Refine sonoPleth PySide6 GUI to Match Design Goal (Light Mode)

Goal: Move the current working GUI (screenshot 2026-02-16) closer to the approved light-mode mockup aesthetic: **research-lab instrument × boutique tool**.  
This is a step-by-step implementation plan for an agent. Execute in order. Make small, controlled changes. Validate after each step.

---

## 0) Non-negotiables (Do not deviate)

- No SpatialSeed-style interactive spatial canvas.
- “Spatial” is **pure vibe only**: subtle geometry background, restrained circular motifs.
- Light mode only for this pass.
- Minimize scope creep: do not add new sections/tabs unless explicitly instructed.
- Make changes incrementally, committing after each stage.

---

## 1) Create a controlled baseline

### 1.1 Snapshot the current state

- Create a branch: `gui/light-polish-v1`.
- Take a screenshot of the current UI in the same window size as the mockup (1100×720) and save to `docs/gui/screenshots/before.png`.

### 1.2 Confirm the code path being run

- Ensure `python gui/main.py` launches the same UI shown in the screenshot.
- If there are multiple entry points, standardize: only `gui/main.py` is used for the GUI.

### 1.3 Add a quick “UI debug toggle” (temporary)

- Add a boolean in `gui/main.py` (or a config file): `UI_DEBUG=False`.
- When `UI_DEBUG=True`, show widget boundaries by applying a thin colored border via stylesheet.
- This is to rapidly diagnose spacing/overlap issues.
- Remove or disable before final merge.

Acceptance:

- Agent can reproduce the screenshot state consistently.

---

## 2) Fix the largest visual mismatch: background vs card contrast

Current issue: geometry background is too visible through the cards. The UI reads washed out and cluttered.

### 2.1 Reduce background visibility (RadialBackground)

Edit `gui/background.py`:

- Reduce overall opacity by ~2–3×.
  - Example: circles from `0.045` → `0.018`
  - crosshair from `0.035` → `0.012`
  - diagonals from `0.025` → `0.008`
- Increase circle step spacing to reduce density.
  - Example: `step = 60` → `step = 90` or `120`
- OPTIONAL but recommended: add radial fade so lines are strongest near center and fade toward edges.
  - Implement by drawing circles with opacity scaled by `(1 - r/max_r)`.

Acceptance:

- Background geometry is barely visible; only noticeable as a subtle technical texture.

### 2.2 Increase card opacity

Edit `gui/styles.qss`:

- Increase `QFrame#Card` background alpha.
  - Example: `rgba(255,255,255,0.72)` → `rgba(255,255,255,0.90)`
- Keep hairline border: `rgba(0,0,0,0.06)`.
- Add _one_ soft shadow style (subtle). If QSS shadow support is limited, do NOT fake it with heavy borders.
  - Prefer leaving shadows minimal rather than over-styling.

Acceptance:

- Cards read as stable surfaces sitting on top of the geometry field.

Commit: `chore(gui): reduce background + increase card opacity`

---

## 3) Fix the Render Settings header/field overlap bug

Current issue: “Render Settings” appears visually corrupted/overlapping with dropdowns.

### 3.1 Diagnose

- Enable `UI_DEBUG=True` to show widget boundaries.
- Inspect the render panel layout for:
  - insufficient top padding
  - label sitting in the same layout row as the combo box
  - editable QComboBox text rendering weirdly

### 3.2 Fix

In `gui/widgets/render_panel.py`:

- Ensure `self.mode.setEditable(False)`.
- Ensure the header label is in its own row above controls:
  - `QLabel("Render Settings")`
  - then `QComboBox`
- Increase card top padding slightly if needed:
  - `lay.setContentsMargins(22, 26, 22, 22)` (add +4 top)
- Add consistent spacing between label/control blocks:
  - `lay.setSpacing(16)` (if currently too tight)

Acceptance:

- “Render Settings” title renders cleanly and does not collide with any control.
- Dropdown text is crisp; no stacked text artifacts.

Commit: `fix(gui): resolve Render Settings overlap`

---

## 4) Replace radio-button-like status indicators with custom status rows

Current issue: left-side statuses look like default Qt radios (breaks Apple/boutique vibe).

### 4.1 Replace with non-interactive status chips

In `gui/widgets/input_panel.py`:

- Do NOT use `QRadioButton` / `QCheckBox` for status.
- Use a `StatusRow` widget with:
  - Left: 18px circular badge
  - Right: optional 8px green dot for “active”
  - Text: muted gray
- States:
  - `inactive`: gray badge, no dot
  - `ready`: accent-outline badge, optional check glyph later (not required)
  - `active`: accent badge + green dot

### 4.2 Make them visually consistent

- Align text baseline.
- Ensure row spacing is 14–16px.

Acceptance:

- Status list reads like an instrument status panel, not interactive form controls.

Commit: `refactor(gui): replace status radios with custom StatusRow`

---

## 5) Restore primary CTA fidelity during RUNNING state

Current issue: “RUNNING…” should still look like the main button; not a floating label.

### 5.1 Keep the button shape constant

In `gui/widgets/render_panel.py`:

- When running:
  - `setEnabled(False)`
  - keep pill button styling unchanged
  - change text to `RUNNING…`
- Optional: add a tiny spinner icon (only if minimal and easy). If not, skip.

### 5.2 Ensure hover/press states don’t break when disabled

In QSS:

- Ensure disabled state is subtle:
  - slightly lower opacity, but keep readable.

Acceptance:

- The primary button remains visually dominant even while disabled.

Commit: `polish(gui): improve RUNNING button state`

---

## 6) Convert console percent spam into a real progress indicator

Current issue: console prints block percentages; user should not need to read logs to know progress.

### 6.1 Parse percentage from stdout

In `gui/pipeline_runner.py` or `gui/main.py`:

- Add regex: `r"(\\d{1,3})%"`
- Track latest percent (0–100).
- Emit a new signal `progress_changed(int)` from PipelineRunner OR update UI in main.

### 6.2 Display progress in the Pipeline panel

In `gui/widgets/pipeline_panel.py`:

- Add a slim progress bar under the stepper OR a small percent pill near the stepper.
  Preferred:
- A thin `QProgressBar` styled minimally (height ~4–6px, rounded ends).
  Or:
- A `Pill` showing `57%`.

Keep it subtle.

Acceptance:

- When rendering, progress updates smoothly as new `%` lines appear.
- Console remains available for detail but progress is visible at a glance.

Commit: `feat(gui): add progress indicator from pipeline output`

---

## 7) Typography + hierarchy tightening

Goal: match mockup calmness: clear hierarchy, restrained weights.

### 7.1 Update QSS font sizes/weights

In `gui/styles.qss`:

- Title: 18px medium
- Section titles: 16px medium
- Muted labels: 12px gray
- Console: 12px

### 7.2 Normalize spacing

- Ensure consistent internal card padding (22–26px).
- Ensure card headings have breathing room above first control (12–16px).

Acceptance:

- Text hierarchy matches mockup: calm, readable, uncluttered.

Commit: `polish(gui): typography and spacing normalization`

---

## 8) Micro-polish: scrollbars, focus, alignment

### 8.1 Minimal scrollbars

- Style QTextEdit scrollbar to be thin and low contrast.
- Avoid high-contrast track.

### 8.2 Focus outlines

- Reduce harsh focus rings.
- Replace with subtle accent outline on focused fields (hairline).

### 8.3 Align cards and elements

- Ensure left and right top cards align vertically and have similar heights.
- Ensure consistent corner radii across cards/buttons/pills.

Acceptance:

- UI feels cohesive and “designed,” not “default Qt.”

Commit: `polish(gui): scrollbars, focus, alignment`

---

## 9) Stepper semantics improvement (optional if needed)

If the pipeline does not emit `STEP N` reliably:

- Map important phrases from `runPipeline.py` output to steps:
  - “Verifying C++ tools” → step 1
  - “Extracting ADM metadata” → step 2
  - “Channel activity” → step 3
  - “Parsing ADM → LUSID” → step 4
  - “Packaging audio” → step 5
  - “Running … renderer” → step 6
  - “Analyzing …” → step 7

Acceptance:

- Stepper advances meaningfully even without explicit STEP markers.

Commit: `feat(gui): robust step mapping from log phrases`

---

## 10) Final validation + deliverables

### 10.1 Side-by-side comparison

- Capture `docs/gui/screenshots/after.png`
- Create `docs/gui/screenshots/compare.png` (before/after montage if easy).

### 10.2 Run-through checklist

- Launch GUI at 1100×720.
- Select ADM file.
- Start render.
- Confirm:
  - No overlap artifacts
  - Button state is correct
  - Progress updates
  - Background is subtle
  - Console autoscroll behaves correctly

### 10.3 Remove debug styling

- Ensure `UI_DEBUG=False` by default.
- Remove debug borders if any were introduced.

Commit: `chore(gui): finalize light-mode polish v1`

---

## 11) Definition of Done

- Visual: matches mockup direction (soft panels, subtle geometry, crisp typography).
- Usability: progress visible without reading logs; logs remain accessible.
- Stability: UI does not freeze during pipeline run.
- Consistency: no default Qt “form” look (especially status indicators).
