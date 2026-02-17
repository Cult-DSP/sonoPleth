# sonoPleth PySide6 GUI scaffold (Light Mode)

This is a starter scaffold aligned to `sonoPleth-mockup.png` (light mode).

## Install

From repo root:

```bash
pip install -r equirements.txt
```

## Run

```bash
python gui/main.py
```

## Notes

- The GUI runs `runPipeline.py` via QProcess and now streams output live to the console using Python's unbuffered mode (`-u` flag). This ensures real-time progress and logs, matching the CLI experience.
- Render mode / resolution / gain / analysis toggle are wired.
- Speaker layout selection is fully functional.
