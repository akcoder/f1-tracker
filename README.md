# F1 Tracker

A 480×480 ESPHome touchscreen that renders the Formula 1 season on a Guition
ESP32-4848S040 — the same board, font stack and UI cues as `sky-tracker`.

- **Race day:** the circuit map for the weekend's track, the drivers in
  starting order, a nationality flag per driver and the circuit's country flag
  in the header.
- **Non-race day:** a carousel through every circuit's map, 15–120 s each, with
  facts about the circuit.
- Latitude and longitude are stored for **one purpose only** — computing
  sunrise and sunset so the display can auto-dim (a checkbox setting).

**[REQUIREMENTS.md](REQUIREMENTS.md) is the living design document.** It records
every decision, the measurement behind it, and the open questions. Read it
first; it is the project.

## Layout
| Path | Contents |
|---|---|
| `REQUIREMENTS.md` | the design document (§12 is the decision log) |
| `f1-tracker.yaml` | ESPHome shell — hardware, entities, LVGL pages *(M0)* |
| `f1-tracker/` | C++ headers: data task, state machine, map drawing *(M1+)* |
| `tools/` | offline generators: circuits, calendar, facts, flags *(M1+)* |
| `tests/` | host tests, `make -C tests` *(M1+)* |
| `reference/f1-circuits/` | vendored GeoJSON circuit traces (MIT) |
| `reference/samples/` | captured API responses used as offline fixtures |
| `reference/CREDITS.md` | third-party sources and licences |

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

Not affiliated with, endorsed by, or connected to Formula 1. `F1`, `FORMULA 1`
and `FIA` are trademarks of their respective owners; no logos or wordmarks are
used. Personal, non-commercial use.
