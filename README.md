# F1 Tracker

A 480×480 ESPHome touchscreen that renders the Formula 1 season on a Guition
ESP32-4848S040 — the same board, font stack and UI cues as `sky-tracker`.

- **Race day:** the circuit map for the weekend's track, the drivers in
  starting order, a nationality flag per driver and the circuit's country flag
  in the header.
- **Non-race day:** a carousel of three card types, 15–120 s each — every
  circuit's map with facts, a profile of every current driver, and a profile of
  an F1 legend. Each profile carries a portrait, a nationality flag and the
  full career record.
- **One driver is watched.** It ships watching **Max Verstappen** and raises an
  alert when he is racing, when his grid slot is known, when his result lands,
  and when he passes a career milestone — a full-width banner for events, a
  brief full-screen takeover for milestones. The watched driver is a setting.
- **The season is resolved at runtime**, never fixed — the device follows the
  calendar forward on its own, year after year, with no firmware update.
  Sprints are first-class race days.
- Latitude and longitude are stored for **one purpose only** — computing
  sunrise and sunset so the display can auto-dim (a checkbox setting).

**[REQUIREMENTS.md](REQUIREMENTS.md) is the living design document.** It records
every decision, the measurement behind it, and the open questions. Read it
first; it is the project.

## Layout
| Path | Contents |
|---|---|
| `REQUIREMENTS.md` | the design document (§12 is the decision log) |
| `f1-tracker.yaml` | ESPHome shell — hardware, 23 entities, LVGL pages. **Builds.** |
| `f1-tracker/` | C++ headers. `f1_panel.h` (HW-7 soft reset) done; data task, state machine and map drawing at M1+ |
| `deploy.sh` | validate + compile + **fail on any warning in our own code** |
| `tools/` | offline generators — `gen_circuits.py`, `gen_calendar.py`, `gen_glyphs.py` — plus `preview_circuits.py` and `mock_order_page.py`, which render the real generated data at 480×480 so layout is measured rather than guessed |
| `reference/mockups/` | rendered layout mockups with the measured verdict |
| `tests/` | host tests — **1823 checks**, `make -C tests` |
| `reference/f1-circuits/` | vendored GeoJSON circuit traces (MIT) |
| `reference/samples/` | captured API responses used as offline fixtures |
| `reference/CREDITS.md` | third-party sources and licences |

## Building
```
python3 -m venv .venv && .venv/bin/pip install esphome
cp secrets.yaml.example secrets.yaml      # then fill it in
./deploy.sh                               # validate, compile, warning gate
```
**M0–M7 are built**, bar the items that need a board. ESPHome 2026.9.1, image **2.74 MB of a
7.75 MB app slot (33.7 %)**, RAM **42.5 %**, **10,587 host checks passing**. The data task runs
on core 1 and double-buffers its store, so a fetch never blocks rendering. Nothing is flashed
yet — see REQUIREMENTS.md §14.1 for what needs a board.

Everything the device shows is compiled in, so the carousel runs with no
network at all:

| Asset | Size |
|---|---|
| 40 circuit traces | 19,176 B |
| 48 country flags (RGB565, two sizes) | 28.1 kB |
| 23 driver + 32 legend profiles | ~12 kB |
| 48 portraits (240×320 JPEG) | 0.98 MB |
| Circuit facts for 41 circuits | ~9 kB |
| Season calendar | ~5 kB |

```
make -C tests                          # 10,431 checks

# regenerate (all cached; a rerun costs no API requests)
python3 tools/gen_circuits.py --all-circuits reference/samples/jolpica-all-circuits.json
python3 tools/gen_calendar.py          # season from /current/, never pinned
python3 tools/gen_drivers.py           # careers, with the pre-1994 pole guard
python3 tools/gen_flags.py
python3 tools/gen_facts.py             # fastest lap, last winner, most wins
python3 tools/gen_portraits.py         # Commons, fails on an unattributable image

# render the real generated data at 480x480, so layout is measured not guessed
python3 tools/render_pages.py --font /path/to/RobotoMono.ttf   # every page
python3 tools/preview_circuits.py                              # all 40 traces
python3 tools/mock_order_page.py --font /path/to/RobotoMono.ttf
```

Every page is rendered from the **generated headers** — the same bytes the
firmware carries — so a layout problem shows up without a board. See
`reference/mockups/all-pages.png`.

```
```

## Data sources
| Source | Role | Licence |
|---|---|---|
| [jolpi.ca](https://jolpi.ca/) | calendar, circuits, drivers, qualifying, results, standings | CC BY-NC-SA 4.0 |
| [openf1.org](https://openf1.org/) | live session state, actual grid, running order, tyres, flags | CC BY-NC-SA 4.0 |

Ergast is dead (HTTP 404 as of 2026-10-01) and is not used.

**Data: [jolpi.ca](https://jolpi.ca/) · [openf1.org](https://openf1.org/)** —
attribution is required by both licences, and the device shows it in a footer
with links in its web UI.

> **Free tiers only — no subscription.** OpenF1's free tier serves historical
> data; "live" is defined as 30 minutes before a session until 30 minutes
> after, and that window is a paid tier we decline. The device **computes the
> window and never requests inside it** (NET-14), so it is a season tracker and
> a race-weekend companion rather than a live timing screen.
>
> This does not affect anything above: the circuit map, the carousel, the
> facts, the starting order, both sets of flags and the auto-dim were never
> live features. The race page is **clock-driven** — it counts down to lights
> out, then to the result — and the interesting session data (fastest lap, pit
> stops, tyre strategy, flags that occurred) arrives on a **post-session
> summary** half an hour after the race. See REQUIREMENTS.md §3.6.1, §3.6.2 and
> §8.3.1.

## Sibling projects
| Project | Relationship |
|---|---|
| `sky-tracker` (`/Volumes/config/esphome/`) | the installed reference: UI cues, flags, platform config, settings page |
| `plane-tracker` (`../plane-tracker`) | document style, requirement-ID convention, and 65 decisions already distilled from `sky-tracker` |

Driver and legend portraits come from Wikimedia Commons with the photographer
credited on each card. Portraits are the one thing an occasional reflash is
needed for: the device follows the season by itself, but a driver who joins
after the firmware was built gets a text-only card until the next build. All licensing terms are accepted; this is a personal,
non-commercial, single-device project.

Not affiliated with, endorsed by, or connected to Formula 1. `F1`, `FORMULA 1`
and `FIA` are trademarks of their respective owners; no logos or wordmarks are
used. Personal, non-commercial use.
