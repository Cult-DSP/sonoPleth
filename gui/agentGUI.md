# agentGUI_v2.md

## sonoPleth GUI: Visual Parity Pass v2 (PySide6, Light Mode)

Goal: Move the current GUI (screenshot 2026-02-17) toward close visual parity with the approved mockup (`sonoPleth-mockup.png`), focusing on layout, hierarchy, and widget fidelity.

This spec is written for a strong coding model. It assumes the GUI already runs and the pipeline works.

---

## Implementation Status

> All items from the v2 spec have been implemented. See acceptance checklist at bottom.

### Changes Made (Feb 17, 2026)

| Section                          | File(s) Changed                                                               | Status  |
| -------------------------------- | ----------------------------------------------------------------------------- | ------- |
| §1.1 Remove `box-shadow` CSS     | `gui/styles.qss`                                                              | ✅ Done |
| §1.2 Fix font alias warnings     | `gui/styles.qss`                                                              | ✅ Done |
| §2.1 Move RUN RENDER to Pipeline | `gui/widgets/pipeline_panel.py`, `gui/widgets/render_panel.py`, `gui/main.py` | ✅ Done |
| §2.2 Structured log list         | `gui/widgets/pipeline_panel.py`, `gui/widgets/log_modal.py` (new)             | ✅ Done |
| §3.1 StatusRow check badges      | `gui/widgets/input_panel.py`                                                  | ✅ Done |
| §3.2 Dropdown styling            | `gui/styles.qss`                                                              | ✅ Done |
| §3.3 Slider styling              | `gui/styles.qss`, `gui/widgets/render_panel.py`                               | ✅ Done |
| §3.4 Toggle switch               | `gui/widgets/switch_toggle.py` (new)                                          | ✅ Done |
| §4 Background lens               | `gui/background.py`                                                           | ✅ Done |
| §5 Spacing/hierarchy             | `gui/styles.qss`, all widget files                                            | ✅ Done |
| §6 Stepper redesign              | `gui/widgets/stepper.py`                                                      | ✅ Done |
| §7 Logging logic                 | `gui/widgets/pipeline_panel.py`                                               | ✅ Done |
| Drop shadow effects              | `gui/utils/effects.py` (new), `gui/main.py`                                   | ✅ Done |

### Files Added

- `gui/utils/__init__.py`
- `gui/utils/effects.py` — `apply_card_shadow()`, `apply_button_shadow()` helpers
- `gui/widgets/switch_toggle.py` — iOS-style animated toggle
- `gui/widgets/log_modal.py` — raw log viewer modal

### Files Modified

- `gui/styles.qss` — removed `-apple-system`, `box-shadow`, `opacity`; refined slider/dropdown/button/list styles
- `gui/widgets/stepper.py` — alternating circle/diamond markers, connector lines, "Analyze" end label
- `gui/widgets/input_panel.py` — `StatusBadge` with QPainter check marks replaces plain QFrame circles
- `gui/widgets/render_panel.py` — removed RUN button, added `SwitchToggle`, master gain slider now 0.0–1.0 with value pill
- `gui/widgets/pipeline_panel.py` — RUN RENDER in header row, `QListWidget` structured log, throttled noisy lines, raw log modal
- `gui/background.py` — central lens (radial gradient + highlight ring + center dot), fewer circles, edge fade
- `gui/main.py` — wired `pipeline_panel.run_clicked`, applied `QGraphicsDropShadowEffect` to cards and CTA button

---

## 0) Inputs and golden references

### 0.1 Golden target

- `docs/gui/reference/sonoPleth-mockup.png` (copy from your source mockup)

### 0.2 Current output to improve

- `docs/gui/screenshots/current.png` (captured at 1100×720)

### 0.3 Definition of done (visual)

- The app reads like the mockup at a glance:
  - The RUN RENDER button lives in the Pipeline area (not in Render Settings).
  - The Pipeline area is a structured list, not a giant console box.
  - Status rows are check badges, not radio circles.
  - Cards and controls have subtle depth (Qt-appropriate shadows).
  - The background geometry is subtle and centered with a “lens” feel.

---

## 1) Critical corrections (stop Qt warnings, improve fidelity)

### 1.1 Remove unsupported CSS like `box-shadow`

Qt stylesheets do not support CSS `box-shadow`. Replace any attempts with Qt effects:

- Use `QGraphicsDropShadowEffect` for:
  - Cards
  - The primary RUN RENDER button
  - Optional: dropdown fields
- Keep shadows subtle:
  - Cards: blur 28–36, alpha 20–35, offset (0, 8)
  - Primary button: blur 36–46, alpha 35–55, offset (0, 10)

Acceptance:

- Terminal warnings about `Unknown property box-shadow` disappear.
- Cards and button match the soft elevation seen in the mockup.

### 1.2 Fix font alias warnings

Qt does not recognize `-apple-system` the way browsers do.

- Use this font family stack in QSS:
  - `SF Pro Display`, `SF Pro Text`, `Helvetica Neue`, `Arial`
- Do not include `-apple-system` in QSS.

Acceptance:

- Terminal warnings about missing font family aliases are gone.
- Typography looks calmer and closer to the mockup.

---

## 2) Layout restructure to match mockup

### 2.1 Move RUN RENDER into Pipeline section

Target behavior from mockup:

- Render Settings card contains:
  - render mode dropdown
  - resolution slider with value pill
  - master gain slider with tick labels
  - Create Analysis toggle
- Pipeline card contains:
  - stepper centered in header row
  - RUN RENDER button on the right side of the Pipeline header row
  - View Full Logs button is lower right or near header right, secondary style

Implementation:

- Remove RUN button from Render Settings panel.
- Add a primary `RUN RENDER` pill button to `PipelinePanel` header row.

Acceptance:

- When the pipeline is running, the Pipeline header button becomes `RUNNING…` and stays visually dominant.
- Render Settings card no longer has a bottom-run CTA.

### 2.2 Pipeline card structure (replace big console)

The current giant QTextEdit is visually unlike the mockup.

Replace with a “structured log list” UI:

- Use `QListWidget` or `QTableWidget`:
  - Column 1: time (muted, narrow)
  - Column 2: message (primary)
- Add subtle row spacing and avoid heavy borders.
- Keep scrolling, but the list should feel like a timeline, not a terminal.

Implementation details:

- Create a small “log model” wrapper:
  - `add_log_line(timestamp: str, message: str, level: optional)`
- When stdout arrives:
  - parse step lines and important messages into the list
  - do not dump every raw line by default
- Keep raw logs accessible via “View Full Logs”:
  - click opens a modal with a raw `QPlainTextEdit` showing full stdout stream

Acceptance:

- Pipeline area resembles mockup: a concise list with timestamps and steps.
- Raw output still accessible, but not visually dominant.

---

## 3) Widget fidelity upgrades

### 3.1 Input status rows (remove radio look completely)

Mockup uses:

- circular check badge on the left
- label text
- optional green active dot on the right of the first item

Implementation:

- Replace any QRadioButton usage with a custom `StatusRow` widget:
  - left: 18px circle badge
  - inside badge: check mark for completed states (drawn via QPainter, or a simple “✓” label centered)
  - label: muted gray
  - optional right dot: 8px green

States:

- inactive: gray circle outline, no check, no dot
- complete: slightly darker outline + check
- active: complete + green dot

Acceptance:

- No UI element looks like a selectable radio group.
- Status rows match the mockup visual language.

### 3.2 Dropdown styling (match pill field)

Mockup dropdown is a rounded field with a subtle arrow.

- Ensure `QComboBox` has:
  - radius 12
  - background high opacity
  - subtle border
- Replace the default arrow if it looks too “Qt”.
  - Option: provide a small custom down-chevron icon in QSS using `image: url(...)` for the drop-down indicator.

Acceptance:

- The dropdown reads like a macOS style field, not a default Qt box.

### 3.3 Slider styling to match mockup

Mockup sliders:

- thin track
- subtle filled portion
- knob feels soft and dimensional
- resolution has a value pill on the right

Implementation:

- Use QSS for track and handle.
- Consider adding a subtle knob shadow via a custom paint handle only if needed.
- For master gain, include tick labels:
  - left: -20
  - center: +
  - right: +20

Acceptance:

- Sliders look closer to the mockup than default Qt.

### 3.4 Toggle switch (Create Analysis)

Qt checkbox indicator styling is rarely enough to look like the mockup toggle.
Implement a custom `SwitchToggle` widget:

- state: on/off
- painted pill track + circular thumb using QPainter
- hover effect subtle
- size close to the mockup (about 44×24)

Acceptance:

- The toggle looks like an iOS-style switch and sits cleanly on the row.

---

## 4) Background system to match mockup (center “lens” + subtle geometry)

The current background is too uniform and lacks the central focal element seen in the mockup.

### 4.1 Geometry density and opacity

- Reduce line opacity overall.
- Reduce the number of circles.
- Ensure lines fade toward edges.

### 4.2 Add a central “lens” focal element

Mockup includes a subtle center disk with depth.
Implement:

- In the background paint:
  - draw a soft radial gradient circle at center
  - add a faint highlight ring
  - add a tiny dot or inner ring

Keep it extremely subtle. This should not compete with UI.

Acceptance:

- Background reads as a technical field with a soft center focal point.
- Cards remain high readability.

---

## 5) Spacing and hierarchy pass (match mockup proportions)

### 5.1 Card padding and alignment

- Input and Render Settings cards should align top edges and share similar padding.
- Increase internal padding to feel airy:
  - 24–30px range depending on density

### 5.2 Header spacing

Mockup header is light and calm:

- add breathing room around the title
- keep divider line hairline

### 5.3 Pipeline header row composition

Pipeline row in mockup:

- left: “Pipeline”
- center: stepper
- right: RUN RENDER (primary)
- View Full Logs is secondary and does not steal attention

Acceptance:

- Layout matches mockup structure without requiring user to interpret.

---

## 6) Stepper redesign to match mockup

Mockup stepper is not plain dots. It alternates shapes (circle, diamond) and has a faint label “Analyze”.

Implementation:

- Replace current dot-only stepper with a custom-painted `StepperWidget`:
  - 7 markers total
  - shapes alternate: circle and diamond
  - active marker filled with accent
  - completed markers slightly darker than inactive
  - optional end label: “Analyze” in muted text

Acceptance:

- Stepper reads like the mockup and is centered in the Pipeline header.

---

## 7) Logging logic changes (to support the new Pipeline list)

### 7.1 Parse and summarize instead of dumping raw

In `PipelineRunner`:

- Keep full raw buffer for “View Full Logs” modal.
- Create a filter layer for the main list:
  - show step changes
  - show key events (extract, package, render, analyze)
  - optionally throttle spam lines like “Channel X/Y scanned…”

### 7.2 Throttle noisy lines

If lines match patterns like:

- `Channel \d+/\d+ scanned`
- `Block \d+ \(\d+%\)`
  Then:
- update a single rolling entry instead of adding a new row each time
- or update a progress bar/pill only

Acceptance:

- Pipeline list remains readable and calm during long runs.

---

## 8) Build a visual regression harness (strongly recommended)

To drive parity with the mockup, add a simple screenshot export:

- `python gui/tools/capture_ui.py`
  - launches the app
  - sets fixed window size 1100×720
  - waits 250 ms
  - captures `window.grab()` to `docs/gui/screenshots/latest.png`

Optional pixel diff:

- `python gui/tools/diff_mockup.py`
  - loads `reference/sonoPleth-mockup.png` and `latest.png`
  - outputs a diff heatmap and a numeric score

Acceptance:

- Agent can iterate and verify progress without subjective guessing.

---

## 9) File plan (what to change)

Expected files:

- `gui/styles.qss`: remove browser-only properties, refine fields and typography
- `gui/widgets/pipeline_panel.py`: move RUN button here, rebuild pipeline UI structure
- `gui/widgets/render_panel.py`: remove run button, refine dropdown and sliders
- `gui/widgets/input_panel.py`: implement StatusRow check badges
- `gui/widgets/stepper.py`: rebuild as custom stepper with alternating shapes
- `gui/background.py`: implement faded geometry + central lens
- `gui/pipeline_runner.py`: add filtered log stream + raw log buffer
- `gui/widgets/log_modal.py` (new): raw logs modal
- `gui/utils/effects.py` (new): apply_drop_shadow helpers

---

## 10) Acceptance checklist (must pass)

Visual:

- [x] RUN RENDER button is in Pipeline header row and looks like a floating pill with shadow.
- [x] Pipeline area is a clean list with timestamps, not a large console box.
- [x] Status indicators are check badges, not radio circles.
- [x] Toggle switch looks like a real switch.
- [x] Background has subtle geometry with a centered lens focal point.
- [x] No Qt warnings about unsupported CSS properties.

Functional:

- [x] Pipeline still runs, output streams.
- [x] "View Full Logs" shows raw text.
- [x] UI remains responsive.
- [x] Noisy log lines (channel scans, block progress) are throttled to a single rolling entry.

---

## 11) Delivery requirements

- [x] Update `gui/agentGUI.md` with this v2 plan and implementation status.
- [ ] Add before/after screenshots in `docs/gui/screenshots/`.
- [x] Make small commits per section with clear messages.
