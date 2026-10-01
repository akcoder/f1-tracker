# Third-party sources and licences

## Circuit geometry — `f1-circuits`
- **Source:** https://github.com/bacinger/f1-circuits
- **Licence:** MIT (confirmed via the GitHub API, 2026-10-01)
- **Vendored:** `reference/f1-circuits/` — 40 circuit GeoJSON files plus
  `f1-locations.geojson`
- **Use:** `LineString` traces in lon/lat, projected and fitted offline into
  `f1_circuits.h` (§5 of REQUIREMENTS.md)
- **Note:** MIT imposes **no licence on this firmware**. This is geometry, not
  artwork — unlike `plane-tracker`, whose tar1090 aircraft artwork made that
  firmware GPL-2.0-or-later.

## Country flags — `flag-icons`
- **Source:** https://github.com/lipis/flag-icons, version 7.5.0
- **Licence:** MIT
- **Via:** `sky-tracker/sky_flags.h`, rasterised from the 4:3 SVGs with
  cairosvg and downsampled (Lanczos) to ARGB8888 at 16×12 and 12×9
- **Use:** driver nationality flags and circuit country flags (§6.6)
- **Additions needed here:** `MCO` (measured as the only gap for the 2026
  season), plus `BHR` and `SAU` for calendar safety

## Fonts
- **Roboto Mono** — Apache 2.0, via ESPHome's `gfonts://`
- **Material Design Icons** — Apache 2.0 (`@mdi/font`), via a `type: web` URL

## Data
- **Jolpica** — https://jolpi.ca/ — calendar, circuits, drivers, qualifying,
  results, standings. Ergast-compatible. **Terms not yet reviewed** (open
  question 1).
- **OpenF1** — https://openf1.org/ — live session data. **Terms not yet
  reviewed** (open question 1).
- Attribution is displayed on the device and linked in the web UI regardless of
  what the terms require (decision 43).

## Code copied from sibling projects
Each copied header must record its source version at the top, e.g.
`// from sky-tracker 4.5.34`, so a later divergence can be diffed rather than
guessed at (decision 45).

| Copied | From |
|---|---|
| `f1_flags.h` | `sky_flags.h` |
| solar maths in `f1_sun.h` | `sky_math.h` (`sun`, `horizontal`, `next_events`) |
| `f1_web.h` | `sky_web.h` |
| `f1_diag.h` | `sky_diag.h` — **re-target the persistence**, it writes to the `skydata` partition this project declines (decision 46) |
| JPEG decode for headshots | `sky_jpg.h`, `sky_photos.h` |

## Trademarks
`F1`, `FORMULA 1`, `FIA`, `GRAND PRIX` and the team names are trademarks of
their respective owners. This project is personal and non-commercial, uses no
logos or wordmarks, and is not affiliated with or endorsed by any of them
(decision 44).
