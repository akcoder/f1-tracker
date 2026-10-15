# F1 Tracker

ESPHome firmware for a Guition ESP32-4848S040 (480x480, LVGL 9, ESP-IDF). No hardware yet: nothing
here is verified on a device. The spec is `REQUIREMENTS.md` (sky-tracker's style: areas, `ID  [x]/[ ]`
paragraphs, a decision table, a change log); read it before changing behaviour, and keep it in step.

## Layout
- `f1-tracker.yaml` the firmware; `f1-tracker/*.h` its C++ (`f1_*`); `f1_dino*.h` is this project's own.
- `tests/` host tests (`make -C tests`); `tools/` generators and `render_pages.py`; `art/`, `docs/`.
- Shared code lives in `../tracker-common` (github.com/akcoder/tracker-common, public), consumed as an
  external component and packages at the tag in `tracker_common_ref`. Siblings: `../sky-tracker`,
  `../plane-tracker`. The dinosaur is f1's, not common.

## Commands
- `./deploy.sh` validate, glyph check, render drift, compile, no warnings in our code (needs `.venv`,
  `secrets.yaml`). `--config` validates only.
- `YAML=dev/f1-tracker-dev.yaml ./deploy.sh` builds against the local `../tracker-common` checkout.
- `make -C tests` host tests (-Werror); needs `../tracker-common` beside this repo or `TC=`.
- `tools/render_pages.py --font /tmp/RobotoMono.ttf` redraws `docs/renders`.

## Rules
- One branch and one PR per piece of work; don't open many. Commit, push or merge only when asked.
- Don't start unrequested work. Where this project differs from sibling projects, show the difference
  and ask.
- Renders: commit only pictures whose pixels really changed. Other fonts redraw every PNG, so after
  `deploy.sh` run `git checkout docs/renders` unless you meant to change them.
- Pin `tracker_common_ref` to a tag. Fix shared code in tracker-common, release a tag, then bump.
- LVGL on this board: scaled RGB565 images draw nothing (use RGB565A8 or pre-scaled data); avoid many
  translucent arc/line draws in one callback; the update card and dim sit on `lv_layer_top`.
- Top-row buttons start at y=10. Titles are sentence case (UI-194).
- Keep the console baud at or below 230400 (macOS misreads faster rates from this chip); opening the
  serial port resets the board.
- Every string drawn must be in the fonts: `tools/check_glyphs.py` (part of `deploy.sh`).
