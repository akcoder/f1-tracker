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

## Driver and legend portraits — Wikimedia Commons
- **Source:** Wikimedia Commons, via each driver's English Wikipedia page image
- **Licences:** per image — mostly CC BY-SA or CC BY, both of which **require
  crediting the photographer**
- **Coverage checked 2026-10-01:** every current driver and every proposed
  legend has a page image. Per-image licences are resolved by the generator,
  not by hand.
- **`tools/gen_portraits.py` records the licence and the photographer for every
  image and fails the build on one it cannot attribute.** The credit is
  displayed on the card (REQUIREMENTS.md §5.6.3).
- Queries are **batched and rate-limited** — a per-driver loop earned an
  HTTP 429 during the initial survey.
- **OpenF1's `headshot_url` is deliberately not used.** It points at
  `media.formula1.com`, which carries no public licence, through a fallback
  transform that may not resolve to a real portrait (decision 73).

## Licensing posture
**All licensing terms are accepted by the project owner.** This is a personal,
non-commercial, single-device project — the use every licence here
contemplates. The obligations that acceptance creates are implemented, not
merely acknowledged: attribution on the device and in the web UI, photographer
credits on cards, a build that fails on an unattributable image, and
CC BY-NC-SA 4.0 notices emitted into the Jolpica-derived generated files.

If the project is ever published, shared as a kit or sold, the **NonCommercial**
term makes that a different question and both data sources must be contacted.

## Fonts
- **Roboto Mono** — Apache 2.0, via ESPHome's `gfonts://`
- **Material Design Icons** — Apache 2.0 (`@mdi/font`), via a `type: web` URL

## Data — terms read and approved 2026-10-01

Both datasets are licensed **CC BY-NC-SA 4.0**. All three terms bind us:
**BY** (attribute), **NC** (non-commercial only), **SA** (adaptations carry the
same licence). See REQUIREMENTS.md §3.7 for the full record.

### Jolpica — calendar, circuits, drivers, qualifying, results, standings
- **Site:** https://jolpi.ca/ · **API:** https://api.jolpi.ca/
- **Terms:** https://github.com/jolpica/jolpica-f1/blob/main/TERMS.md
  (last updated 2025-08-27)
- **Data licence:** CC BY-NC-SA 4.0
- **Rate limits:** 4 requests/second burst, **500 requests/hour sustained**.
  Published limits **will decrease** as token access rolls out.
- **No uptime or correctness guarantee** — volunteer-run, donation-supported.
- Commercial use requires contacting `admin@jolpi.ca`.

### OpenF1 — live session state, actual grid, running order, tyres, flags
- **Site:** https://openf1.org/ · **API:** https://api.openf1.org/v1/
- **Data licence:** CC BY-NC-SA 4.0 (site footer)
- **Free tier:** all 18 endpoints, **historical only** (2023 onwards), no
  authentication, 3 req/s and 30 req/min.
- **Live data is a paid tier** — €9.90/month. "Live" is defined as **30 minutes
  before a session starts until 30 minutes after it ends**; outside that window
  the data is historical and free.
- **We use the free tier only** (REQUIREMENTS.md decision 58). The device
  computes the live window from the session calendar and **does not issue
  requests inside it** (NET-14), so it never relies on data it is not entitled
  to and never makes a request that could be throttled or misread as a fault.
- **No authentication is used for either source**, so neither places anything
  in `secrets.yaml`.
- OpenF1's FAQ states credits are **not** required; its footer licenses the data
  CC BY-NC-SA 4.0, which requires them. **We attribute anyway.**

### How we attribute
- On the device: a small footer, `Data: jolpi.ca · openf1.org`.
- In the web UI: **clickable links** to both. A device-only credit does not
  satisfy an "include a link" clause (`plane-tracker` decision 32).

### ShareAlike reaches the generated data
The MIT circuit geometry imposes no licence on this firmware. **The
Jolpica-derived data compiled into it does.** `f1_calendar.h` and the lap
records, most-wins and race counts in `f1_facts.h` are adaptations of
CC BY-NC-SA 4.0 data, so the **generators emit that notice into those files'
header comments**. The code is unaffected; the generated data files are not.

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
