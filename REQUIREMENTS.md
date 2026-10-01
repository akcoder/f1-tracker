# F1 Tracker — Requirements

**Target hardware:** Guition ESP32-4848S040 (ESP32-S3, 4.0" 480×480 IPS)
**Framework:** ESPHome (ESP-IDF)
**Status:** Draft rev 2 — living document, updated as decisions are made
**Last updated:** 2026-10-01 (rev 3: **free data tiers only** — no paid
subscription, §3.6.1. Live timing is out of scope; the brief is unaffected)

---

## 1. Purpose

A wall/desk-mounted 480×480 touchscreen that renders the Formula 1 season.

On a **race day** it shows the circuit map for that weekend's track and the
drivers in starting order, each with the flag of their nationality, under a
header carrying the circuit's country flag. On a **non-race day** it rotates
through the circuit map of every track on the calendar, 15–120 s each, with
facts about the circuit on screen.

This is the **third board** in the same family, after `sky-tracker` (installed,
rev 4) and `plane-tracker` (in bring-up). It uses the **same hardware and the
same UI cues** as `sky-tracker`: the same panel and touch controller, the same
Roboto Mono 4 bpp font stack, the same orange section titles on black, the same
tap-to-flip page model, the same gear-in-the-lower-right settings page, the
same long-press debug page, and the same web UI + Home Assistant entity model.

**Latitude and longitude are captured for one purpose only:** computing
sunrise and sunset so the display can dim itself automatically, which is a
checkbox setting (§6.8). Nothing on screen is plotted from the observer
position. This is a deliberate narrowing — see decision 7.

---

## 2. Hardware

### 2.1 Confirmed
Identical to `sky-tracker`, which drives this exact board in production, and to
`plane-tracker`. No new hardware is introduced.

| Item | Value |
|---|---|
| Board | Guition ESP32-4848S040 |
| MCU | ESP32-S3 (dual-core Xtensa LX7, 240 MHz) |
| Flash / PSRAM | 16 MB flash / 8 MB octal PSRAM @ 80 MHz |
| Display | 4.0" 480×480 IPS, ST7701S controller |
| Display platform | `mipi_rgb`, `model: GUITION-4848S040` — pins, timings and 12 MHz pclk come from the model |
| Panel orientation | `mirror_x: false`, `mirror_y: false` (the model default is true/true, so this is the 180° rotation) |
| Panel init SPI | CLK `GPIO48`, MOSI `GPIO47`, CS `GPIO39` |
| Touch | GT911 capacitive, `i2c` bus A — SDA `GPIO19`, SCL `GPIO45`, no transform |
| Backlight | `ledc` on `GPIO38` — PWM confirmed |
| Spare I²C (relay pins) | SDA `GPIO1`, SCL `GPIO2` — **unused here** |
| Spare UART RX | `GPIO42` — **unused here** |
| Board / flash | `esp32-s3-devkitc-1`, 16 MB |

- [ ] **HW-7: a soft panel reset is required _before_ the SPI bus comes up** —
      `on_boot` priority **1100** (SPI is 1000). `sky-tracker` calls
      `sat::panel_soft_reset(39, 48, 47)` from a header we do not have yet.
      `plane-tracker` omitted it and left it as the first thing to look at if
      the panel fails to initialise. **Port it properly here** rather than
      inheriting that open question twice.
- [ ] **No ambient light sensor is fitted** (confirmed on `sky-tracker`), so
      auto-dim is sun-scheduled, not sensed (§6.8).
- [ ] No GPS and no compass. Both sibling projects support them optionally; this
      project has no use for either, and the lat/lon it does hold is incidental
      (§1). Leave the pins free and the code out.
- [ ] Confirm the board **revision** against the silkscreen before trusting the
      model defaults.

### 2.2 Host toolchain
- [ ] ESPHome in a **dedicated venv**, as `plane-tracker` did — not the
      `radioconda` environment, to avoid dependency conflicts.
- [ ] Serial port for the first flash: confirm before use; `plane-tracker` uses
      `/dev/cu.usbserial-21340` on this host, and only one board can hold it at
      a time.

### 2.3 Reference configs
Two mature projects by the same author drive this board, and both are on disk:

| Project | Path | Role here |
|---|---|---|
| `sky-tracker` | `/Volumes/config/esphome/sky-tracker.yaml` + `sky-tracker/*.h` | **UI cues, flags, platform config, settings page, web UI.** Installed and working (rev 4, fw 4.5.34) |
| `plane-tracker` | `/Users/dan.m/Projects/Personal/plane-tracker/` | **Document style, requirement IDs, and 65 hard-won decisions** already distilled from `sky-tracker` |

- [ ] Treat `sky-tracker`'s pin map, `sdkconfig_options` and LVGL tuning as
      **authoritative** over any community config or default.
- [ ] Read `plane-tracker`'s decision log (its §10, 65 rows) before
      implementing anything in §6 or §7. Most of what this project needs was
      already discovered and paid for there; several rows are bugs that took a
      device to find.

#### 2.3.1 Requirement-ID convention
Both sibling projects tag every non-obvious line with an ID — `HW-6`, `UI-16`,
`PERF-8`, `NET-2`, `DATA-10`, `FAIL-10`, `ARCH-3`, `BUILD-1` — and the code
comments cite them.

- [ ] **Use the same prefixes**, so all three projects read alike. Two
      project-specific prefixes are added, in the same spirit as
      `sky-tracker`'s `MOTION-`:
      **`RACE-`** for the weekend state machine and session logic (§4.1), and
      **`MAP-`** for circuit-trace generation and drawing (§5, §6.5).
- [ ] Carry the `substitutions:` + `fw_version` pattern across, bump it on
      every install, and surface it in HA, on the settings page and on the
      Wi-Fi status page.

### 2.4 Proven platform configuration to inherit (PERF)
These are hard-won fixes from a working build on this board, with the symptom
each one cures. Start from them rather than rediscovering them.

#### 2.4.1 `-DLV_INV_BUF_SIZE=128`
```
build_flags:
  - "-DLV_INV_BUF_SIZE=128"
```
LVGL tracks **32 dirty areas** by default and redraws the **whole screen** when
they overflow. On `sky-tracker`, ~16 moving markers dirtied two areas each,
overflowed the budget every tick, and the panel visibly jumped.

- [ ] **Our invalidation profile is different from both siblings' and should be
      stated, not assumed.** Nothing here moves per-frame: the circuit trace is
      static for 15–120 s at a time, and the driver list changes only when
      positions change. Our risk is the opposite one — **a single large dirty
      area**, because an `lv_line` spanning the map box invalidates that whole
      box on every change (§6.5).
- [ ] Set the flag anyway. It costs ~1.5 KB of RAM, and a 22-row live order
      page plus a lap counter plus a clock plus flags is well past 32 areas
      during a race.

#### 2.4.2 `sdkconfig_options` (ESP-IDF)
| Option | Why |
|---|---|
| `CONFIG_LCD_RGB_RESTART_IN_VSYNC: y` | Re-syncs panel DMA each vblank, so a stall cannot leave the picture permanently shifted |
| `CONFIG_SPIRAM_XIP_FROM_PSRAM: y` | Runs code and rodata from PSRAM. Flash and PSRAM share one bus on the S3: flash fetches starved the panel's refills (jitter) and flash writes stalled the cache entirely (picture shift during uploads) |
| `CONFIG_ESP32S3_DATA_CACHE_LINE_64B: y` | Fewer, larger PSRAM bursts for bounce-buffer copies |
| `CONFIG_LWIP_TCP_WND_DEFAULT: "32768"` | Default window is 5.7 KB. **From Alaska the data hosts are ~100 ms away** and throughput is capped at window/RTT — measured 57 KB/s → ~320 KB/s |
| `CONFIG_LWIP_TCP_RECVMBOX_SIZE: "32"` | Matches the larger window |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP: y` | Keeps lwIP/Wi-Fi buffers out of the ~70 KB of free internal RAM |

- [ ] **Do NOT add `CONFIG_LCD_RGB_ISR_IRAM_SAFE`.** `sky-tracker` explicitly
      forbids it (its PERF-3); `SPIRAM_XIP_FROM_PSRAM` replaces the need for it.
- [ ] The TCP window fix matters **more** here than in either sibling: our
      largest useful response is 48 KB (§3.0) against `plane-tracker`'s 8.5 KB.

#### 2.4.3 LVGL buffer strategy
```
lvgl:
  buffer_size: 12.5%     # ~57.6 KB, requested from internal RAM first
```
LVGL blends in fast internal SRAM and only finished pixels cross to the PSRAM
framebuffer, leaving PSRAM bandwidth for the panel's bounce-buffer refills.
**Starving those refills is what produces the vsync shift and garbage lines.**

- [ ] Keep `12.5%` unless measurement says otherwise, and log at boot whether
      the buffer actually landed in internal RAM or fell back to PSRAM.

#### 2.4.4 Boot ordering
| `on_boot` priority | Action |
|---|---|
| **1100** | Panel soft reset — **before** the SPI bus (1000) and panel init |
| 600 | Install web/diagnostic hooks — before `web_server` setup (249) |
| −100 | Main application setup (widgets, config, bindings) |

- [ ] The fallback-AP SSID must be set **before** Wi-Fi starts (setup priority
      250), because ESPHome's `ssid:` must be a literal. Name it
      `"F1 Tracker - XXXX"` with the last MAC bytes, as both siblings do.

#### 2.4.5 Flash partitioning — use the default table
**Decision: default ESPHome partition layout, no custom data partition.**

`sky-tracker` carves a 4 MB `skydata` partition to cache orbital element sets;
`plane-tracker` deliberately declined to carry it over. We decline it too, but
for a different reason worth recording: our static assets — circuit traces
(19.2 KB, §5.2), flags (~150 KB, §6.6) and the compiled-in season calendar
(§4.3) — belong in **program flash**, and our network data is small and
refreshable.

- [ ] Keeping the default table leaves **larger OTA app slots**, which matters:
      the firmware carries LVGL, TLS, ArduinoJson, 4 bpp fonts, flags, circuit
      traces and a curated facts table.
- [ ] Settle this **before first flash** — OTA cannot move partitions, so
      reversing it later means a USB cable trip.
- [ ] Consequence: no durable cache for *fetched* data. Mitigated, not ignored
      — the compiled-in calendar (§4.3) means a cold boot with no network still
      knows the season and can run the circuit carousel. That is a materially
      better offline story than either sibling has, and it comes free.

#### 2.4.6 API / OTA settings that prevent lockouts
| Setting | Why |
|---|---|
| `api: reboot_timeout: 0s` | The default 15-minute "no HA client" reboot **kills the fallback AP** mid-configuration |
| `api: max_connections: 8` | At the default 5, an extra log viewer was dropped ("dropped immediately after encrypted hello") |
| `ota: encryption:` (no key) | Reuses the API encryption key |

- [ ] **FAIL-12: show OTA progress on the display.** Port `sky-tracker`'s
      `ota_panel` on the LVGL **top layer** so it covers whichever page is
      showing without disturbing it, wired to `on_begin`/`on_progress`/
      `on_end`/`on_error` on **both** OTA platforms (ESPHome and `web_server`),
      redrawing every 5 % and calling `lv_refr_now()` each step. The OTA write
      blocks the loop; without the explicit refresh the screen freezes on
      whatever was last drawn and the upgrade reads as a crash.
- [ ] The data task must be **paused for the duration** and resumed on error,
      as `plane-tracker` does.
- [ ] Wi-Fi uses **ESPHome defaults** — `plane-tracker` decision 42 reversed
      `sky-tracker`'s `reboot_timeout: 0s` / `ap_timeout: 30s` overrides.
      Follow the newer decision.

#### 2.4.7 Backlight PWM frequency — 30 kHz, not 1 kHz
`sky-tracker` runs `ledc` at 1000 Hz. `plane-tracker` decision 62 raised it to
**30 kHz** because 1 kHz is audible: the backlight converter's inductor whines
loudest at low duty, which is exactly where dimming parks it.

- [ ] Use **30 kHz**. This device will sit dimmed overnight for most of the
      year at 61.58 N (§6.8), so it lands squarely in the whine band.

#### 2.4.8 `logger: hardware_uart: UART0`
The S3's default console is USB Serial/JTAG on GPIO19/20, and **GPIO19 is the
GT911's SDA**. Leaving the default collides with touch.

- [ ] Set `hardware_uart: UART0` explicitly (`plane-tracker` decision 41).

---

## 3. Data sources

### 3.0 Measured baseline — 2026-10-01, this host
Every figure below was measured, not estimated. Captured responses are
committed under `reference/samples/` as offline fixtures.

**Ergast is dead.** `https://ergast.com/api/f1/2026.json` returns **HTTP 404**
with an HTML error page. It is not a candidate.

#### Jolpica (Ergast-compatible successor) — `api.jolpi.ca`
| Endpoint | HTTP | Size | Latency | Notes |
|---|---|---|---|---|
| `/ergast/f1/current/next/?format=json` | 200 | **821 B** | 0.3 s | one race, full session times |
| `/ergast/f1/2026/races/?format=json&limit=40` | 200 | 14.3 KB | 0.33 s | **23 rounds** |
| `/ergast/f1/2026/circuits/?format=json&limit=100` | 200 | 5.3 KB | 0.70 s | **24 circuits**, with lat/lon + country |
| `/ergast/f1/2026/drivers/?format=json&limit=100` | 200 | 5.6 KB | 0.67 s | **32 drivers** |
| `/ergast/f1/2025/last/qualifying/?format=json` | 200 | 9.1 KB | 0.84 s | 20 results |
| `/ergast/f1/current/last/results/?format=json` | 200 | 13.0 KB | — | finishing order |
| `/ergast/f1/current/driverStandings/?format=json` | 200 | 10.1 KB | — | championship |
| `/ergast/f1/current/constructorStandings/?format=json` | 200 | 2.5 KB | — | championship |
| `/ergast/f1/2025/1/fastest/1/results/?format=json` | 200 | 1.2 KB | — | fastest lap of a race |
| `/ergast/f1/2025/sprint/?format=json&limit=100` | 200 | 58.7 KB | — | whole season's sprints |

- Served behind **Cloudflare** with `cache-control: max-age=600`, so a
  10-minute poll costs the origin nothing and our own cadence can be slower
  still (§3.6).
- **No rate-limit headers are returned.** Absence of a header is not a licence;
  see §3.7.

#### OpenF1 — `api.openf1.org`
| Endpoint | HTTP | Size | Notes |
|---|---|---|---|
| `/v1/sessions?year=2026` | 200 | 48.1 KB | every session of the year |
| `/v1/sessions?year=2026&session_type=Race` | 200 | **11.2 KB** | races only — the useful form |
| `/v1/drivers?session_key=11377` | 200 | 9.2 KB | static per session; team colour, acronym, headshot |
| `/v1/position?session_key=11377` | 200 | **35.9 KB / 311 rows** | **event-driven** — a row only on a change |
| `/v1/stints?session_key=11377` | 200 | 8.4 KB | tyre compound + age |
| `/v1/race_control?session_key=11377&category=Flag` | 200 | 20.3 KB | flags, SC, VSC for the session |
| `/v1/laps?session_key=11377` | 200 | 487 KB | whole session — too big |
| `/v1/laps?session_key=11377&lap_number=50` | 200 | 7.9 KB | one lap — usable |
| `/v1/weather?session_key=11377` | 200 | 36.2 KB | track/air temp, rain |
| `/v1/intervals?session_key=11377` | 200 | **3.53 MB** | ~4 Hz per driver — **unusable raw** |
| `/v1/intervals?...&driver_number=63` | 200 | 7.3 KB | one driver's whole session |
| `/v1/starting_grid?session_key=latest` | **404** | — | **this endpoint does not exist** |
| `/v1/meetings?session_key=latest` | 404 | — | `meetings` does not accept `session_key` |
| `/v1/car_data?session_key=latest` | **422** | — | requires a driver filter |

- `session_key=latest` works and resolves to the most recent session.
- **Filters do work.** `intervals?session_key=11377&date>=2026-09-26T11:50:00`
  returned 11,271 rows whose first timestamp is `11:50:00.229` — the filter
  narrowed correctly; the response is 1.6 MB because the *data* is that dense,
  not because the filter was ignored.
- `/v1/` responses carry **no cache-control**, so assume uncached origin hits.

### 3.1 Decision: Jolpica for the calendar, OpenF1 for the live session
Neither source alone covers this project.

```
                   schedule, circuits, drivers,
                   qualifying, results, standings
api.jolpi.ca ────────────────────────────┐
                                         ├─HTTPS─> ESP32 (state machine,
api.openf1.org ──────────────────────────┘           render)
                   live session state, actual grid,
                   running order, tyres, flags
```

**DATA-1: Jolpica is the system of record for the calendar.** It is the only
source with full session date/times per round in one small response, and
`current/next` is 821 B — small enough to poll often and cheap enough to parse
with a selective parser.

**DATA-2: OpenF1 is the system of record for what is happening right now.**
Jolpica publishes results after the fact; OpenF1 publishes positions as they
change. Nothing else is needed from it when no session is live.

- [ ] Keep the two behind **one narrow interface** so either can be swapped,
      and record **which source last updated** each field.
- [ ] Handle them failing **independently** — the circuit carousel must keep
      running on a compiled-in calendar even if both are unreachable (§4.3).
- [ ] `http_request:` is included **even though the C++ task issues the
      requests** (ARCH-3), purely to pull in `esp_http_client`, `esp-tls`, the
      certificate bundle and ArduinoJson. Both siblings do this.
- [ ] **NET-12: keep the TLS session open between fetches.** `plane-tracker`
      decision 65 measured a fresh handshake at ~1.2 s of crypto, and with
      `CONFIG_SPIRAM_XIP_FROM_PSRAM` that crypto runs on the same bus the RGB
      panel needs — the measured cause of `lvgl took a long time (1231 ms)`
      warnings. Drop the handle on error or on a body not read to completion.
      **Two hosts means two session handles**, one per origin.

### 3.2 Jolpica fields used
| Field | Use |
|---|---|
| `season`, `round` | identity of the weekend; carousel highlight |
| `raceName` | header title. **Do not derive a country from it** — see §3.4 |
| `date`, `time` | race start, **UTC** — drives the state machine (§4.1) |
| `FirstPractice` / `SecondPractice` / `ThirdPractice` / `Qualifying` / `Sprint` / `SprintQualifying` | session times, each `{date, time}` in UTC. **Presence varies by round** |
| `Circuit.circuitId` | **key** for the circuit trace and facts table (§5) |
| `Circuit.circuitName` | map caption |
| `Circuit.Location.country` | **authoritative** for the circuit flag (§6.6) |
| `Circuit.Location.locality` | facts line |
| `Circuit.Location.lat` / `long` | centroid match to the vendored trace at build time only — **not used at runtime** |
| `Driver.driverId`, `code`, `givenName`, `familyName` | driver list; `code` is the 3-letter acronym |
| `Driver.permanentNumber` | car number |
| `Driver.nationality` | demonym → ISO3 → driver flag (§6.6). **Often absent** — see §3.4 |
| `QualifyingResults[].position`, `Driver`, `Constructor`, `Q1`/`Q2`/`Q3` | provisional starting order (§3.5) |
| `Results[].position`, `grid`, `status`, `Time` | finishing order and actual grid, post-race |
| `DriverStandings[].position`, `points`, `wins` | championship page |

### 3.3 OpenF1 fields used
| Field | Use |
|---|---|
| `session_key`, `meeting_key` | identity; `latest` resolves the current one |
| `session_type`, `session_name` | Practice / Qualifying / Race / Sprint |
| `date_start`, `date_end` | live-session detection, cross-checked against Jolpica |
| `circuit_short_name`, `country_code`, `country_name` | **secondary** circuit identification; `country_code` here is ISO3 and *is* populated |
| `gmt_offset` | **track local time** — a free, genuinely interesting header field (§6.7) |
| `is_cancelled` | suppress a session that will not happen |
| `driver_number` | **key** linking every live row to a driver |
| `name_acronym`, `full_name`, `team_name` | list labels |
| `team_colour` | 6-hex RGB — the team-colour bar (§6.10) |
| `headshot_url` | driver photo on the detail card, best-effort (§6.11) |
| `country_code` (drivers) | **null throughout the 2026 data** — see §3.4 |
| `position`, `date` | starting grid and running order (§3.5) |
| `compound`, `tyre_age_at_start`, `stint_number` | tyre column, optional |
| `flag`, `category`, `message`, `scope` | track status: yellow / red / SC / VSC (§6.2) |

### 3.4 Data-quality handling (required)
Each of these was **observed in the live data on 2026-10-01** and will occur.

- [ ] **`Driver.nationality` is absent for 9 of the 32 drivers** in the 2026
      list (`paul_aron`, `dino_beganovic`, `luke_browning`, `jak_crawford`,
      `leonardo_fornaroli`, `colton_herta`, `ryo_hirakawa`, `ayumu_iwasa`,
      `frederik_vesti` — the newly added entries). The field is **missing
      entirely**, not null. Every read must tolerate it, and a driver with no
      resolvable nationality **draws no flag rather than a wrong one**.
- [ ] **OpenF1's driver `country_code` is `null` for every 2026 row.** It is
      not a usable nationality source. Driver flags come from Jolpica's
      demonym, full stop.
- [ ] **Nationality is a demonym, not a code** — `"British"`, `"Dutch"`,
      `"Monegasque"`, `"Thai"`. A **demonym → ISO 3166-1 alpha-3 table** is
      required (§6.6). Keep it in one editable table and expect to add rows.
- [ ] **The race name and the circuit can disagree on country.** Observed:
      2026 round 16 is served as `raceName: "Bahrain Grand Prix in Malaysia"`
      with `circuitId: "sepang"`, `Location.country: "Malaysia"`. The circuit
      flag must come from **`Circuit.Location.country`**, never from the race
      name. A name-derived flag would have drawn Bahrain over a Malaysian
      circuit.
- [ ] **The calendar is provisional and can be internally odd.** The 2026
      `races` endpoint returns **23 rounds** while `circuits` returns **24**,
      and the round list does not match the published calendar in every slot.
      Treat the calendar as data to be refreshed, never as a constant, and do
      not assert "round N is X" anywhere in the UI beyond what the feed says.
- [ ] **Session keys vary by round.** `Sprint` and `SprintQualifying` are
      present only on sprint weekends; `ThirdPractice` is absent on them. Drive
      the state machine off whichever session objects exist, not a fixed five.
- [ ] **Times are UTC with a trailing `Z`** and arrive as a separate `date` and
      `time` pair that must be joined. A round can also carry a `date` with no
      `time` — treat the start as unknown rather than midnight.
- [ ] **`alt`-style surprises:** `Results[].grid` is `"0"` for a pit-lane
      start. Do not render `P0`.
- [ ] **OpenF1 `position` is event-driven**, so a driver who never changes
      position after the start has exactly one row. Absence of recent rows
      means "unchanged", **not** "missing" (§3.5).
- [ ] Treat an empty or unparseable body as a **failed poll**: keep the
      existing state, do not clear it, retry on the next cycle.

### 3.5 Starting order — where it actually comes from
The user-visible requirement is "drivers in starting order". That turns out to
have three distinct answers depending on when you ask, and conflating them
would put a wrong grid on screen.

**RACE-10: OpenF1 has no starting-grid endpoint.** `/v1/starting_grid` returns
HTTP 404 — it does not exist. Measured, not assumed.

**RACE-11: the actual grid is recoverable from the earliest `position` rows.**
Measured against `reference/samples/openf1-position-11377.json` (session 11377,
2026-09-26): the 311 rows contain, at one identical timestamp
`10:07:06.143`, a **complete P1–P22 ordering with every driver present**. The
earliest `position` row per driver, sorted by position, *is* the grid — penalties
and all — and it costs one 36 KB fetch.

| When | Source of "starting order" | Confidence |
|---|---|---|
| Before qualifying | **nothing** — show the entry list in car-number order, labelled as such | n/a |
| After qualifying, until 30 min after the race | Jolpica `/{season}/{round}/qualifying/` classification | **provisional** — penalties not applied |
| 30 min after the race, whichever lands first | Jolpica `Results[].grid` **or** OpenF1 earliest-row-per-driver (RACE-11) | **actual grid** |

**RACE-11a: under free tiers, Jolpica's `Results[].grid` is the simpler path
and RACE-11 becomes the race for second place.** The live window (§3.6.1) means
OpenF1's `position` is unreadable until 30 minutes after the race, by which
point Jolpica's results — one 13 KB request, no window, `grid` and `status` and
gaps together — give everything RACE-11 gives and more.

- [ ] **Poll both and take whichever arrives first.** Jolpica is volunteer-run
      with no publishing-time guarantee (§3.7.1); OpenF1's data becomes free on
      a fixed schedule. Neither is reliably faster, the two agree on the grid,
      and merging whichever lands first is a few lines.
- [ ] **Keep RACE-11 and its test.** The mechanism is measured, the fixture is
      committed, and it is the only independent cross-check that Jolpica's
      `grid` field is right. A silent disagreement between the two sources is
      worth a log line.

- [ ] **Label the provisional case on screen.** A qualifying classification
      presented as a grid is exactly the kind of error that looks like correct
      data. Show `GRID (PROVISIONAL)` until RACE-11 or the results confirm it.
- [ ] Once the race is running, the same `position` feed gives the **running
      order**, so the page has two modes (§6.3) off one data path.
- [ ] A pit-lane start is `grid: "0"` in Jolpica and simply the back of the
      earliest-position ordering in OpenF1. Render it as `PIT`, not `P0`.

### 3.6 Poll intervals by state
Polling is **driven by the state machine** (§4.1), not by a fixed timer. This
is the direct analogue of `plane-tracker`'s radius→interval table (its decision
20): keep the mapping in **one table**, not inline logic.

| State | Jolpica | OpenF1 | Rationale |
|---|---|---|---|
| `OFF_SEASON` | 24 h | — | the calendar changes on the order of weeks |
| `IDLE` (no session within 24 h) | 6 h | — | carousel needs no network at all (§4.3) |
| `RACE_WEEK` | 1 h | — | session times can move |
| `SESSION_SOON` (< 2 h) | 15 min | — | nothing to fetch yet; the countdown is local |
| `SESSION_LIVE` (not the race) | 30 min | **none** | inside OpenF1's live window (§3.6.1) |
| `RACE_LIVE` | 30 min | **none** | inside OpenF1's live window (§3.6.1) |
| `POST_SESSION` (window closed, < 12 h) | 10 min | **5 min** | the session's data is now historical and free |

#### 3.6.1 Free tiers only — never request inside OpenF1's live window (NET-14)
**Decision: this device uses only the free tiers of both services.** No
subscription is taken (decision 58). The consequence is a requirement, not a
degradation to be detected at runtime:

- [ ] **Compute OpenF1's live window from the calendar and do not request
      inside it.** The window is *session start − 30 min* to *session end +
      30 min* (§3.7.2), and the device already knows every session time to the
      second (§3.2). Skipping the request is strictly better than making one
      that will not answer: it cannot be rate-limited, cannot be mistaken for a
      fault, and cannot return a stale snapshot that looks like success.
- [ ] This **retires most of open question 8.** The gated response shape still
      matters as a defensive check — if a request ever lands inside a window
      because a session time moved, anything unexpected is treated as "no
      data", never as data — but it is no longer on the critical path.
- [ ] `POST_SESSION` replaces `POST_RACE` and is entered when the **window
      closes**, not when the session ends: *end + 30 min*. This is the moment
      the device can finally read the session, and it is the one transition
      worth getting exactly right.
- [ ] **Everything the brief asks for is on the free tier.** The circuit map is
      compiled in (§5), and the starting order comes from Jolpica qualifying,
      which has no live window at all. What the free tier costs us is the live
      running order — a feature this document proposed, not one that was asked
      for (§3.6.2).

#### 3.6.2 What the free tier costs, stated plainly
| Capability | Free tier |
|---|---|
| Circuit map, every circuit, with facts | **yes** — compiled in, no network (§5) |
| Drivers in starting order on race day | **yes** — Jolpica qualifying (§3.5) |
| Driver and circuit country flags | **yes** — compiled in (§6.6) |
| Championship standings | **yes** — Jolpica, no window (§8.1) |
| Session countdowns, all time zones | **yes** — calendar + local clock (§6.7) |
| Actual grid, with penalties applied | **yes**, 30 min after the race (§3.5) |
| Final classification, status, gaps | **yes**, 30 min after the race |
| Fastest lap, pit stops, flags that occurred | **yes**, 30 min after the session (§8.1) |
| **Live running order during a session** | **no** |
| **Live track status, tyres, weather** | **no** |

- [ ] The honest summary: **the device is a season tracker and a race-weekend
      companion, not a live timing screen.** Design the race page to be
      excellent at what it is rather than apologetic about what it is not
      (§6.2, §6.3).

- [ ] **Peak request rate is now trivial.** The old `RACE_LIVE` 5 s poll is
      gone, so the busiest state is `POST_SESSION` at 5 min OpenF1 + 10 min
      Jolpica = **18 requests/hour** combined, against Jolpica's 500/hour and
      OpenF1's 30/min. There is no demanding case left in this design.
- [ ] `position` is event-driven and 36 KB for a whole session. In
      `POST_SESSION` fetch it **once**, not every 5 minutes — the 5 min
      interval is for *retrying until it appears*, and the poll must stop once
      a complete session has been read. A repeated full-session fetch is the
      easiest way to waste the budget this design otherwise has in abundance.
- [ ] **Re-check this table whenever either service's limits change** —
      Jolpica has said theirs will decrease (DATA-4).
- [ ] Changing state must **retime the timers immediately**, not wait out the
      old interval.
- [ ] `plane-tracker` decision 60: a restored `number` is **range-checked at
      boot**, not trusted. ESPHome restores stored bytes without clamping to
      `min_value`/`max_value`. The carousel interval and any poll-affecting
      setting must be clamped both at boot and at the point of use.

### 3.7 Terms of use and attribution — read and approved 2026-10-01
Both services' terms were read in full and **approved by the project owner**.
This section is the record of what was approved; open question 1 is closed.

#### 3.7.1 Jolpica
Source: `github.com/jolpica/jolpica-f1/blob/main/TERMS.md` (last updated
**2025-08-27**) and `docs/rate_limits.md`.

| Term | Value |
|---|---|
| Data licence | **CC BY-NC-SA 4.0** |
| Use | Free for **non-commercial** use; commercial via `admin@jolpi.ca` |
| **Burst limit** | **4 requests / second** |
| **Sustained limit** | **500 requests / hour** |
| Over limit | `HTTP 429 Too Many Requests` — *"Request was throttled"* |
| Enforcement | *"Abuse or excessive use may result in temporary or permanent blocking… may block without notice"* |
| Guarantees | **None.** *"volunteer-run, donation-supported… we do not guarantee uptime, availability, or correctness"* |
| Terms stability | *"We reserve the right to change these terms"* |

**Our usage against the limits.** The §3.6 table peaks at a 10-minute Jolpica
interval in `POST_RACE`, i.e. **6 requests/hour** against a 500/hour ceiling —
0.2 % of the sustained budget. Nothing in this design comes near either limit.

- [ ] **DATA-4: their published limits "will decrease in the future"** as token
      access and a non-Ergast replacement API roll out. Treat the current
      headroom as temporary and keep the poll table (§3.6) as the single place
      to retune. Do not spend the headroom on convenience polling.
- [ ] Their rate-limit guide asks callers to **implement a cache** and **use
      efficient queries with filters and offsets**. Decision 12 (the
      compiled-in calendar) and §3.6's state-driven intervals already satisfy
      this. Record the alignment so it is not accidentally undone.
- [ ] **No uptime or correctness guarantee** makes §4.3 load-bearing rather
      than a nicety: the device must be fully useful with Jolpica down.

#### 3.7.2 OpenF1
Source: `openf1.org` (Access & Support, FAQ, footer) and `openf1.org/docs`,
read 2026-10-01.

| Term | Value |
|---|---|
| Data licence | **CC BY-NC-SA 4.0** |
| Use | *"educational purposes, personal learning projects, research, and non-commercial fan engagement"* |
| **Free tier (Community)** | all 18 endpoints, **historical only**, no auth, **3 req/s and 30 req/min** |
| **Sponsor tier** | **€9.90 / month** — adds **live data during sessions** (REST, MQTT, WebSocket), 6 req/s, 60 req/min, up to 10 concurrent MQTT/WS connections |
| Historical coverage | **2023 onwards** |
| Live latency | ~**3 s** after the live event |
| Attribution | *"credits are not required… a link back to openf1.org helps"* |

**NET-13: the free tier does not cover the live window, and this is the single
most consequential term in this document.** Quoted exactly:

> *"Data is considered live from **30 minutes before a session starts until 30
> minutes after it ends**. Outside of this window, data is classified as
> historical and is free to access."*

And from the API reference:

> *"Historical data (from 2023 onwards) is free and accessible without
> authentication. **Real-time data requires a paid subscription.**"*

So every live feature this document had proposed — RUNNING order,
position-change flashes, track status, tyre compound, live weather — sits
**inside the paid window**.

**Decision 58: the sponsorship is declined. This device uses the free tiers
only.** The design response is NET-14 in §3.6.1 — compute the window and never
request inside it — and §3.6.2 states plainly what that costs. Everything the
original brief asked for is unaffected.

- [ ] **Measured caveat:** every OpenF1 figure in §3.0 was measured on
      **historical** data (session 11377, five days old). The behaviour of an
      unauthenticated request *inside* a live window is **unmeasured**, which
      is now a defensive concern rather than a design one (§3.6.1).
- [ ] **No token, no auth, nothing in `secrets.yaml` for either source.** Both
      free tiers are unauthenticated, which removes a whole class of secret
      handling from this project.
- [ ] Free-tier limits are **3 req/s and 30 req/min**. Our busiest state is 18
      requests per *hour* (§3.6.2). The rate limit is not a constraint on this
      design; the live window is the only one that was, and NET-14 settles it.

#### 3.7.3 Attribution, NonCommercial and ShareAlike (DATA-5)
Both datasets are **CC BY-NC-SA 4.0**, which has three live terms for us.

- [ ] **BY — attribution is mandatory.** OpenF1's FAQ says credits are not
      required while its footer licenses the data CC BY-NC-SA 4.0; those
      conflict, and the cost of attributing anyway is a line of text.
      **Attribute both**, on screen in a small footer —
      `Data: jolpi.ca · openf1.org` — and with **clickable links to both** in
      the web UI. `plane-tracker` decision 32 learned that a device-only credit
      does not satisfy an "include a link" clause.
- [ ] **NC — non-commercial.** This is a personal device. Record it so the
      constraint is not forgotten if the project is ever published or sold as a
      kit, which the licence would forbid without permission from both.
- [ ] **SA — ShareAlike, and this one is easy to miss.** Decision 21 noted that
      the MIT circuit geometry imposes no licence on the firmware. **That does
      not extend to the Jolpica-derived data we compile in.** The generated
      calendar (§4.3) and the lap records, most-wins and race counts in the
      facts table (§5.5) are adaptations of CC BY-NC-SA 4.0 data, distributed
      inside the firmware. Immaterial for a personal device; if this is ever
      published, **those generated data files carry CC BY-NC-SA 4.0** while the
      code does not.
- [ ] **Keep the licence boundary visible in the tree**: generated headers
      sourced from Jolpica get a CC BY-NC-SA 4.0 notice in their header
      comment, emitted by the generator, not added by hand.

#### 3.7.4 Failure handling required by the terms
- [ ] **Never retry a 4xx tightly.** Treat 4xx as *fatal for that request
      shape* — log it, surface it, back off exponentially to minutes. Only 5xx
      and network errors get brisk retries. Jolpica blocks for abuse **without
      notice**, and a bad-request loop would look exactly like the service
      being down.
- [ ] **Distinguish 429 from a network failure in the UI** ("rate limited" vs
      "no data"); they need opposite responses. Jolpica returns 429 with
      *"Request was throttled"*.
- [ ] **Distinguish "live data not available on this tier" from both** (§6.3,
      RACE-12). Three failure states that look similar and mean different
      things is exactly the trap §6.12 exists to avoid.

#### 3.7.5 Trademarks
- [ ] `F1`, `FORMULA 1`, `FORMULA ONE`, `FIA FORMULA ONE WORLD CHAMPIONSHIP`,
      `GRAND PRIX` and the team names are trademarks of Formula One Licensing
      B.V. and others. Both data sources carry an explicit non-affiliation
      disclaimer; **carry the same one** in `README.md` and on the debug page.
- [ ] **Do not use the F1 logo or wordmark anywhere in the UI**, and name the
      device plainly.

---

## 4. Architecture

### 4.1 The weekend state machine (RACE-1)
Everything the device shows is a function of **one state**, derived from the
calendar and the clock. This is the core abstraction; it replaces the ad-hoc
"is it race day" test the brief implies.

```
OFF_SEASON ─> IDLE ─> RACE_WEEK ─> SESSION_SOON ─> SESSION_LIVE ─┐
                 ^                      ^                        │
                 └──────── POST_RACE <──┴── RACE_LIVE <──────────┘
```

| State | Entered when | Primary page |
|---|---|---|
| `OFF_SEASON` | no future round in the calendar | Circuit Carousel (§6.4) |
| `IDLE` | next session > 24 h away | Circuit Carousel |
| `RACE_WEEK` | any session of the next round < 7 days away | Carousel, **next round pinned first** |
| `SESSION_SOON` | a session starts in < 2 h | Race Day page with countdown (§6.2) |
| `SESSION_LIVE` | now ∈ [start, end] of a non-race session | Race Day page, session order |
| `RACE_LIVE` | now ∈ [start, start + 3 h] of the Race | Race Day page, running order |
| `POST_RACE` | race ended < 12 h ago | Race Day page, final classification |

- [ ] **"Race day" is a state, not a date comparison.** The brief says "on race
      day"; the device is in Alaska and the races are in Melbourne, Suzuka and
      Las Vegas. A UTC race start of `04:00Z` is the previous *evening*
      locally. Driving the UI off the device's local calendar date would show
      the race-day screen on the wrong day for roughly half the calendar.
      **Derive state from the session timestamps, in UTC, never from a local
      date.** This is the single most likely bug in this project.
- [ ] `RACE_LIVE` has no reliable end time in the Jolpica feed (no duration).
      Bound it with **3 h** from the start, and leave it early if OpenF1
      reports the session ended or the classification is final.
- [ ] OpenF1's `date_end` for the Race session is a better bound when the
      session exists. Prefer it; fall back to the 3 h window.
- [ ] Expose the current state as a **diagnostic text sensor** — it explains
      everything else the device is doing, and it is the first thing to look at
      when the wrong page is up.
- [ ] A **manual override** in settings (`Force page: Auto / Race Day /
      Carousel`) so the race-day screen can be seen in February. Reverts to
      `Auto` on reboot, deliberately — a forced page is a debugging tool, not a
      mode.

### 4.2 Direct-to-API
The device queries both services directly over HTTPS. There is no companion
host. Responses are small (§3.0), the parsing is shallow, and nothing needs a
database.

- [ ] Favour a **streaming/selective parser** over a full DOM. The 48 KB
      `sessions` response carries ~20 fields per row and we use 8.
- [ ] Size buffers for the **48 KB worst case**, not the 821 B best one.
- [ ] Mirror the siblings' split: **YAML shell + C++ headers.**
      `f1_net.h` (fetch, parse, both sources, merge), `f1_state.h` (the state
      machine + poll scheduling), `f1_map.h` (projection + trace drawing),
      `f1_circuits.h` + `f1_facts.h` (generated), `f1_flags.h` (copied from
      `sky_flags.h`), `f1_diag.h` / `f1_web.h` (copied).
- [ ] Hand widget pointers and a config struct into C++ from an `on_boot`
      lambda at priority −100, as both siblings do.
- [ ] Record the **source version at the top of each copied file** (e.g. "from
      sky-tracker 4.5.34"), so a later divergence can be diffed rather than
      guessed at. Copy, do not factor a shared library — `plane-tracker`
      decision 31, and the same reasoning applies a third time.

### 4.3 The season calendar is compiled in, then refreshed (DATA-3)
A generated header carries the calendar as built, and the network refreshes it.

- [ ] `tools/gen_calendar.py` → `f1-tracker/f1_calendar.h` — rounds, session
      times, `circuitId`, country, all from Jolpica at build time.
- [ ] At boot the device **has a usable season immediately**, before Wi-Fi.
      A fetched calendar supersedes it in RAM; the compiled one is the floor.
- [ ] This is what makes §2.4.5 (no data partition) cost nothing: the thing
      worth caching is baked in instead.
- [ ] Show **which calendar is in use** on the debug page (`compiled` /
      `fetched, age N h`). A device quietly running a stale compiled calendar
      in August is otherwise invisible.
- [ ] Bump the generated calendar on every firmware build so an OTA also
      refreshes the floor.

---

## 5. Circuit data

### 5.1 Decision: the `f1-circuits` GeoJSON set (MAP-1)
**Source:** `github.com/bacinger/f1-circuits`, **MIT licensed** (confirmed via
the GitHub API, 2026-10-01). A repository of F1 circuits as GeoJSON
`LineString` traces in lon/lat, with per-circuit properties.

Vendored under `reference/f1-circuits/` — 40 circuit files plus
`f1-locations.geojson`.

```json
{ "id": "mc-1929", "Location": "Monaco", "Name": "Circuit de Monaco",
  "opened": 1929, "firstgp": 1929, "length": 3337, "altitude": 47 }
```

Why this rather than bitmaps or SVG diagrams:
- It is **geometry, not artwork** — it scales to any box, rotates, and can be
  drawn as a polyline with no rasteriser.
- MIT is permissive, so unlike `plane-tracker`'s tar1090 artwork (its decision
  39, which made that firmware GPL-2.0-or-later) **this choice imposes no
  licence on our firmware.**
- It is tiny (§5.2).

### 5.2 Measured: coverage and size
**Coverage of the 2026 calendar is complete.** Every one of the 23 rounds
Jolpica serves matched a vendored trace by centroid, worst case **1.1 km** off:

| | |
|---|---|
| Rounds in the 2026 Jolpica calendar | 23 |
| Matched to a vendored trace (< 15 km centroid) | **23 / 23** |
| Worst centroid offset | 1.1 km (Baku) |
| Unmatched | **none** |

The set even carries **`es-2026` (Circuito de Madring)**, the new 2026 venue.

**Size is a non-issue:**

| | |
|---|---|
| Circuit files | 40 |
| Total trace points | **4,794** |
| Points per circuit, min / median / max | 81 / 116 / **203** (Paul Ricard) |
| All 40 as `int16` x,y pairs | **19,176 B (19.2 KB)** |

- [ ] Store as `int16` x,y in a **pre-projected, pre-fitted local frame**
      (§5.4), not as floats and not as lon/lat. 19.2 KB of flash for every
      circuit F1 has raced on is not worth optimising further.
- [ ] **Ship all 40**, not just the current calendar. The carousel is better
      for having Kyalami, Watkins Glen and Estoril in it, the cost is 8 KB, and
      a calendar change cannot then strand the device without a map.

### 5.3 Offline generation (MAP-2)
- [ ] `tools/gen_circuits.py` reads the vendored GeoJSON and emits
      `f1-tracker/f1_circuits.h`: per circuit, the `id`, a `circuitId` cross-
      reference for Jolpica, the point count, the fitted rotation (§5.4) and
      the `int16` point array.
- [ ] **Map `circuitId` → trace `id` at build time**, by centroid against
      Jolpica's `Circuit.Location`, and **emit the table**. Doing this at
      runtime would mean carrying lat/lon for 40 circuits and a nearest-
      neighbour search for no benefit. The build-time match is already
      measured at ≤1.1 km (§5.2).
- [ ] **Fail the build on an unmatched calendar circuit**, listing it. A
      silently missing map is the failure mode to prevent, and this is the only
      place it can be caught.
- [ ] Assert **non-degenerate geometry** per circuit — at least 60 points, a
      bounding box over 200 m on both axes, and a closed loop (first and last
      point within ~100 m) for everything except the one-off street layouts.
      `plane-tracker` learned this the hard way: a silently blank icon is
      invisible rather than obviously broken (its §5.11.3).

### 5.4 Projection and fit (MAP-3)
- [ ] Project each trace to a local plane about its own centroid:
      `x = (lon − lon₀)·cos(lat₀)`, `y = (lat − lat₀)`. **The `cos(lat₀)` term
      is mandatory** — at Silverstone (52 N) omitting it stretches the circuit
      east-west by 1.6×, and at Montréal (45.5 N) by 1.4×. `plane-tracker`
      sidestepped projection entirely by plotting from `dst`/`dir`; we cannot,
      so this is the one place that distortion has to be got right.
- [ ] Compute the **minimum-area bounding rectangle** offline and bake a
      per-circuit **rotation** so each map fills its box. North-up wastes most
      of the box on long thin layouts (Spa, Jeddah, Baku, Las Vegas).
- [ ] **Draw a small north arrow** whenever the baked rotation is non-zero. A
      rotated map with no orientation cue is a quietly wrong map.
- [ ] Normalise to a fixed `int16` range at generation time so the device only
      scales and translates — no per-circuit float work at draw time.
- [ ] Preserve **aspect ratio**. A circuit squeezed to fill a square box is
      unrecognisable, which defeats the whole point.

### 5.5 Curated circuit facts (MAP-4)
The brief asks for facts about each circuit. The GeoJSON gives four, the API
gives three, and the interesting ones are in neither.

**From the GeoJSON** — free, authoritative, already vendored:
`length` (m), `altitude` (m), `opened`, `firstgp`, `Name`, `Location`.

**From Jolpica** — fetched or compiled in:
country, locality, and the round's date.

**Computable offline from Jolpica, baked into the facts table:**
- [ ] **All-time circuit lap record** — `/{season}/{round}/fastest/1/results/`
      across every season the circuit has hosted. Measured at 1.2 KB per
      round, so this is a few hundred cheap requests **once, at build time**,
      and never on the device.
- [ ] **Most wins at this circuit**, and **most poles** — same method.
- [ ] **Number of races held**, and the **most recent winner**.

**Hand-curated, with a source recorded per row:**
- [ ] **Number of turns**, **DRS zones**, **race laps** and **race distance**.
      None of these is in any feed. **Do not compute turns from the trace** —
      curvature-extrema counting on a 116-point median trace disagrees with the
      published count often enough to be worse than useless, and a wrong turn
      count is the sort of thing a reader will spot instantly.
- [ ] Keep the facts in **one editable table** in `f1_facts.h`, generated with
      a `// source:` comment per hand-entered value, as `sky_lore.h` does for
      its constellation cards.
- [ ] Facts render as **two to four short lines**, rotating if there are more
      than fit. `sky_lore.h`'s shape — a small struct of `const char *` fields —
      is the right model to copy.

---

## 6. Display

### 6.1 Pages
The page model is `sky-tracker`'s (its UI-2): **a tap anywhere flips to the next
page**, pages that are not in the rotation carry `skip: true`, and the gear
takes its own taps via `on_short_click`.

| Page | In rotation | Shown when |
|---|---|---|
| `wifi_page` | no (`skip`) | first page, so it is what boots; returns after 10 s offline — **but never over the settings page** |
| `race_page` | yes | the primary page in `SESSION_*`, `RACE_LIVE`, `POST_RACE` |
| `order_page` | yes | the full 22-driver order (§6.3) |
| `circuit_page` | yes | the primary page in `IDLE`, `RACE_WEEK`, `OFF_SEASON` (§6.4) |
| `standings_page` | yes | championship (§8.1) |
| `settings_page` | no (`skip`) | gear, lower right |
| `debug_page` | no (`skip`) | **long-press** the gear |

- [ ] **UI-2a: the rotation order changes with state.** The *first* page after
      a flip from `wifi_page` is whichever page the state machine says is
      primary. The pages themselves do not move; only which one the device
      rests on does.
- [ ] With a detail card open, a tap anywhere — **including the gear** —
      closes the card rather than acting. Both siblings do this.
- [ ] **Corner furniture is screen-fixed.** The clock and gear must not move
      with a rotated map (§5.4).

### 6.2 Race Day page (UI-1)
480×480. The brief's two requirements — circuit map and drivers in starting
order — do not fit together on one screen at a readable size, so they are split
across two pages (§6.3) with the map page carrying a top-5 strip.
**Approved by the project owner 2026-10-01** (decision 28).

```
┌──────────────────────────────────────────────┐
│ R14 SPANISH GRAND PRIX          [flag] 14:35 │  title orange, mono16; circuit
│ Circuito de Madring · Madrid                 │  flag from Location.country
│ RACE UNDER WAY · RESULTS IN ~00:18           │  state line, clock-driven
├──────────────────────────────────────────────┤
│                                              │
│            ╭─────────────╮                   │
│           ╱               ╲      ▲N          │  circuit trace, lv_line
│          │   circuit map   │                 │  start/finish marked
│           ╲               ╱                  │
│            ╰─────────────╯                   │
│                                              │
├──────────────────────────────────────────────┤
│ P1 [NL] VER  Red Bull      │ 5.471 km        │  top 5, team-colour bar
│ P2 [GB] NOR  McLaren       │ 57 laps         │  facts to the right
│ P3 [MC] LEC  Ferrari       │ 20 turns        │
│ P4 [AU] PIA  McLaren       │ 1:12.272 (2026) │
│ P5 [GB] RUS  Mercedes      │ Data: jolpi.ca  │
└──────────────────────────────────────────────┘ ⚙
```

- [ ] **UI-3: the header's state line is the page's most important element.**
      It tells the reader whether the order below is an entry list, a
      prediction or a result. Under free tiers it is driven by the **clock and
      the calendar**, never by lap data:
      `LIGHTS OUT IN 02:14:30` / `QUALIFYING IN 00:42:10` /
      `RACE UNDER WAY · 01:12 ELAPSED` / `RESULTS IN ~00:18` /
      `GRID (PROVISIONAL)` / `FINAL` / `NEXT: R15 BAKU, 6 DAYS`.
- [ ] **UI-3a: `RESULTS IN ~mm:ss` is the state that replaces live timing.**
      Once the race has started, the device knows exactly when the data becomes
      readable: session end + 30 min (§3.6.1). Counting down to the result is
      truthful, useful, and the only thing on this page that moves during a
      race. It turns the free tier's one limitation into a legible behaviour
      rather than a dead screen.
- [ ] **Never show `LAP n/m`.** Lap data is inside the live window and the
      device does not have it. A lap counter that is wrong or frozen is the
      worst possible element on this page.
- [ ] **UI-4: live track status is out of scope** under free tiers —
      `race_control` is inside the live window. It returns **after** the
      session, so the flags, safety cars and red flags that *occurred* belong
      on the post-session summary (§8.1) rather than the header. If it is ever
      shown live, it must be a coloured dot **plus a word**, never colour
      alone.
- [ ] **UI-5: a countdown in `SESSION_SOON`**, to the next session, labelled
      with which session it is. This is the state the device will sit in most
      often during a race week and it should be the best-looking screen on it.
- [ ] Both flags appear on this page: the **circuit country flag** in the
      header, and a **driver nationality flag** per row (§6.6).
- [ ] The top-5 strip is a **summary, not a list** — it must not try to be
      §6.3. Five rows, nothing scrollable.
- [ ] Facts shown here are the short ones. The full set lives on the circuit
      page (§6.4).

### 6.3 Order page (UI-10)
The full field, 22 drivers in 2026, in whichever order the state machine says is
current.

```
 POS  #   NAT  DRIVER   TEAM          GAP
  1   1   [NL] VER      Red Bull      —
  2   4   [GB] NOR      McLaren       +3.104
  …
 22  18   [BR] DRU      Cadillac      +1 LAP
```

- [ ] **UI-10a: build rows as lightweight containers in C++, not an LVGL
      `table`.** `sky-tracker` uses a `table` for its satellite list, and it is
      the right choice there because every cell is text. Ours is not: each row
      needs a **flag image** and a **team-colour bar**, neither of which a
      table cell holds. 22 rows × 4 children is 88 static objects, which is
      affordable precisely because nothing here moves per-frame (§2.4.1).
- [ ] **UI-10b: 22 rows in 480 px leaves ~19 px per row** after a header.
      `mono12` with `pad_all: 2` and a `BOTTOM` border, as `sky-tracker`'s list
      does at `pad_all: 5`. Measure it on hardware before committing the
      layout — if it does not fit, drop the TEAM column before shrinking the
      font.
- [ ] **UI-10c: the order page is the single source of truth for position.**
      `plane-tracker` decision 47 and 57 both came from two places counting the
      same thing and disagreeing. The header's `LAP n/m`, the top-5 strip and
      this page must all read **one** ordered array, never the raw feed.
- [ ] **RACE-12: four order modes, one data path** (§3.5). The mode is chosen
      by state, never by the user, and **the mode's name is always on screen**
      so the order is never ambiguous:

  | Mode | Shown when | Source |
  |---|---|---|
  | `ENTRY LIST` | before qualifying | Jolpica drivers, car-number order |
  | `GRID (PROVISIONAL)` | after qualifying, until the window closes | Jolpica qualifying |
  | `GRID` | 30 min after the race | Jolpica results / RACE-11 |
  | `FINAL` | 30 min after the race | Jolpica results — carries `status` (`DNF`, `+1 LAP`, `ACCIDENT`) and gaps |

- [ ] **There is no `RUNNING` mode.** Live order needs OpenF1's paid tier,
      which is declined (decision 58). The order page is honest about this
      rather than holding stale data behind a live-looking label: during a
      session it keeps showing `GRID (PROVISIONAL)` — which is *correct and
      current information*, not a degraded substitute — and the race page
      header carries the session's progress by clock, not by lap (§6.2).
- [ ] **Do not build a "live timing unavailable" apology.** An earlier draft
      had a `LIVE TIMING UNAVAILABLE` banner and a `Live timing` switch; both
      are removed. A device that never attempts live timing has nothing to
      report as missing, and a permanent banner about an absent feature is
      worse than no banner. The settings page and the diagnostics lose their
      live-timing entries entirely.
- [ ] **The transition at window-close is the page's one real moment.** Going
      from `GRID (PROVISIONAL)` to `GRID` + `FINAL` is when the page changes
      from a prediction to a result. Make it feel deliberate — a fade, the mode
      label changing, and the finishing order animating in from the grid order
      if it is cheap. This is the closest the free tier gets to drama and it
      costs nothing.
- [ ] **Never present held data as current.** `GRID (PROVISIONAL)` is current
      until the race result exists. But if the newest Jolpica parse is older
      than several poll intervals, say so in the status line (§6.12) —
      `plane-tracker` decision 50: an HTTP 200 carrying nothing is not fresh
      data.
- [ ] **Gap column**: `interval` from OpenF1 is 3.5 MB per session (§3.0), so
      it is **not** fetched wholesale. Either narrow it hard to the newest rows
      or leave the column blank. **Blank is acceptable**; a wrong gap is not.
- [ ] Highlight **position changes** briefly — a short green/red tint on a row
      that gained or lost places since the last poll. This is the cheapest way
      to make a 5 s poll feel live without any animation.
- [ ] A **retirement** keeps its row, greyed, with the status word (`DNF`,
      `ACCIDENT`, `+1 LAP`) rather than vanishing.

### 6.4 Circuit Carousel — the non-race-day page (UI-20)
The default screen for most of the year.

- [ ] Rotate through the circuits, **one at a time**, advancing on a timer.
      **Interval is a setting: 15–120 s, step 15, default 45 s** — the brief
      asked for 30 s to 1 minute, and the range brackets it.
- [ ] Each card shows: the **map** (large, §6.5), the **circuit name**, the
      **country flag** and country, the locality, and **the facts** (§5.5).
- [ ] **Order is calendar order**, and in `RACE_WEEK` the cycle **starts at the
      next round** so the upcoming circuit is the first thing seen.
- [ ] **Mark the next round** on its card (`NEXT · R14 · Mar 8`) and mark
      circuits not on the current calendar (`not on the 2026 calendar`). Both
      matter: the set ships all 40 (§5.2), so without the second label the
      device looks like it has invented races.
- [ ] A tap flips pages as everywhere else. **Hold** to pause the carousel on
      the current circuit — and show that it is paused.
- [ ] **Transition is a fade, not a slide.** A slide drags a full-screen dirty
      area across the panel at 12 MHz pclk; a cross-fade on a static image is
      cheap and reads as deliberate. `sky-tracker` uses `FADE_IN` at 200 ms for
      every page change; match it.
- [ ] Setting: **`Carousel: calendar order / random / current season only`**.
- [ ] The carousel must run **with no network at all** (§4.3). This is the
      state the device spends most of the year in and it should be the most
      robust thing it does.

### 6.5 Drawing the circuit map (MAP-5)
- [ ] One **`lv_line`** per circuit, built from the generated `int16` points
      scaled into the map box. Line width ~3 px, rounded caps, anti-aliased by
      LVGL's draw unit. A single widget for a 203-point trace.
- [ ] **Split the trace into three `lv_line`s for sector colours** only if
      sector boundaries are available. They are not in the GeoJSON, so this is
      deferred — not designed around.
- [ ] Mark **start/finish** with a short perpendicular tick at point 0, and the
      **racing direction** with a small chevron. Both come free from the trace.
- [ ] **MAP-5a: an `lv_line` invalidates its whole bounding box.** At ~300×300
      that is 39 % of the screen, or ~175 KB of PSRAM framebuffer traffic per
      redraw. This is fine at one redraw per 45 s and **not** fine if anything
      makes it redraw per-frame. Keep the map in its own container and never
      put a moving widget inside its bounds.
- [ ] **Pit lane is not in the data** — the GeoJSON is a single `LineString`.
      Out of scope (§11); do not fake it.
- [ ] Budget the map box at ~**300×300 px** on the race page and up to
      **400×320** on the carousel card, where it is the subject rather than one
      element among several.

### 6.6 Flags (UI-30)
The brief requires a flag for the driver and a flag for the course.

**Copy `sky_flags.h` wholesale.** It is generated from **flag-icons 7.5.0
(MIT)**, rasterised from 4:3 SVGs, and already contains exactly the two sizes
this project needs:

| | |
|---|---|
| Card flags | **16×12**, ARGB8888, keyed by ISO 3166-1 alpha-3 |
| Map flags | **12×9**, ARGB8888 |
| Countries present | **79** |

**Measured gap: exactly one flag is missing.** Checked every 2026 driver
nationality and every 2026 circuit country against the 79 available:

| Needed | Status |
|---|---|
| 16 driver nationalities (23 drivers with the field) | **15 present**, `MCO` missing |
| 20 distinct circuit countries | **19 present**, `MCO` missing |

- [ ] **Add `MCO`** (Monaco) — needed twice over, for Leclerc and for the
      Monaco Grand Prix. Regenerate from the same flag-icons 7.5.0 source at
      the same two sizes so the set stays homogeneous.
- [ ] Add `BHR` and `SAU` at the same time. They are absent from the 23-round
      2026 feed but the calendar is provisional (§3.4) and both are long-
      standing venues; a missing flag on the Bahrain GP would be a silly way to
      discover this.
- [ ] Build the **demonym → ISO3 table** required by §3.4 as a generated,
      sorted table with binary search, as `plane-tracker` does for its 375-row
      designator map. Seed it from the observed 2026 values and assert in tests
      that **every nationality in the captured driver fixture resolves**.
- [ ] A driver with no nationality, or a demonym with no mapping, **draws no
      flag** — never a placeholder that could be mistaken for a country.
      Log it once so the table can be extended.
- [ ] Record the flag-icons licence and version in `reference/CREDITS.md`, as
      `plane-tracker` does for its icon artwork.

### 6.7 Clock and session times (UI-8)
- [ ] **Current local time in the upper-right corner**, `mono24`, showing
      `--:--` before time sync. Port `sky-tracker`'s widget directly, including
      the `y` offset that aligns the digit tops with the title's capitals.
- [ ] Format is a **three-state dropdown**, as `plane-tracker` settled (its
      decision 21): **24 h local** (default) / 12 h local / **UTC-Zulu**
      (`2235Z`, no colon, trailing `Z`).
- [ ] Monospace keeps the glyph count stable so the clock does not jitter.
      Roboto Mono is monospaced by construction.
- [ ] Omit seconds. They force a 1 Hz redraw of the corner for no value.
- [ ] **Session times are the interesting case here, and they are not the
      clock.** Every session time arrives in **UTC** (§3.2) and must be shown in
      the reader's local time, or the countdown is meaningless. Convert once,
      centrally.
- [ ] **UI-8a: offer track local time too.** OpenF1 `sessions` carries
      `gmt_offset` (measured, e.g. `"03:00:00"`), so showing "14:00 local ·
      03:00 your time · 11:00 at the track" costs nothing and is genuinely
      useful for a global calendar. A good candidate for the detail card rather
      than the header.
- [ ] Time source: **SNTP** plus a second `time:` platform `homeassistant`, so
      the clock sets as soon as HA connects without waiting on SNTP. **Gate
      SNTP on Wi-Fi** (`sky-tracker`'s NET-8): hold at boot, kick from
      `on_connect`, hold on `on_disconnect`.
- [ ] Timezone: `America/Anchorage`, handling DST automatically. ESPHome's
      `timezone:` must be a **literal**, so it is compile-time — see the
      caveat in §6.8.
- [ ] Show a clear **unsynced** indication rather than a plausible-but-wrong
      time. The state machine depends on the clock, so an unsynced clock must
      also suppress the state machine rather than letting it guess.

### 6.8 Auto-dimming — the only use of latitude and longitude (UI-44)
This is why the device stores a position at all.

- [ ] **Checkbox: `Auto-dim display`, default on.** When off, the backlight
      stays at the manually set brightness.
- [ ] **No ambient light sensor is fitted** (§2.1), and the backlight **is**
      PWM-capable, so "auto" means **sun-scheduled** and it can genuinely *dim*
      rather than merely switch off.
- [ ] Sunrise and sunset are **computed on-device from the stored lat/lon** and
      the clock. No network call and no extra configuration. `sky_math.h`
      already has the solar position maths (`sun()`, `horizontal()`,
      `next_events()`); copy it and strip the satellite parts.
- [ ] **Port `sky-tracker`'s UI-44 implementation**, which already works:
      - a `monochromatic` light on the `ledc` output, marked `internal: true`,
        with a **1–100 % `number` entity** as the user-facing control, so HA and
        the web UI stay clean;
      - `min_power: 0.10` for the lowest still-visible duty, `zero_means_zero:
        true` so only an explicit OFF blanks the panel;
      - `restore_mode: RESTORE_AND_ON` — the display always boots on and keeps
        its brightness;
      - a **30 s interval** scaling a stored **daytime** brightness by a sun
        factor, transitioning over **3 s**;
      - `auto_bright_factor()`: 100 % in daylight falling to **25 %** once the
        Sun is 8° below the horizon;
      - the adjustment is **skipped while the settings page is open**, so it
        does not fight the user's slider;
      - the user's slider sets the **daytime** level while auto is on, not the
        current level — a subtle distinction that must be preserved.
- [ ] **This matters far more at 61.58 N than at mid-latitudes.** Day length at
      the default position runs from **~19.1 h** at the summer solstice to
      **~4.9 h** at the winter solstice. A fixed clock-time schedule would be
      wrong most of the year, which is the whole argument for deriving it from
      the sun.
- [ ] Ramp transitions over seconds. An abrupt backlight jump reads as a fault.
- [ ] **Touching the screen while dimmed wakes to full brightness** for a
      timeout, then returns to dim.
- [ ] **UI-44a: the lat/lon and the timezone are independent, and that is a
      trap.** The position is a runtime entity; the timezone is a compile-time
      literal. Move the position to Melbourne and the sun times will be
      correct in UTC while the clock stays on Alaska time, so the display will
      dim at the right absolute moment and the wrong apparent hour. **State
      this on the settings page** next to the coordinate fields — one line, so
      nobody has to discover it.
- [ ] **UI-44b: decline the DMS entry widget.** Both siblings carry a
      decimal⟷DMS toggle with hemisphere buttons and a three-field entry per
      axis. Here the coordinates are not the instrument — they feed one
      brightness curve — and the toggle is a page's worth of widgets and a
      round-tripping precision test for no benefit. **Decimal degrees only**,
      two fields, `accepted_chars` restricted to digits, `-` and `.`. The port
      remains available if it is ever wanted.
- [ ] Validation: lat ±90, lon ±180. Clamp with visible feedback, and
      **range-check the restored values at boot** (§3.6).
- [ ] **Consider porting Night Mode** (`sky-tracker`'s UI-45): a red-only
      palette after civil dusk, applied to the pixels LVGL hands the display
      just before each flush, so no widget needs a second colour scheme. See
      §6.13.

### 6.9 Typography (UI-40)
- [ ] **Render all text with 4 bpp fonts.** **ESPHome's font default is
      `bpp: 1`**; `sky-tracker` annotates every single font block with that
      fact, which suggests it was learned the hard way. Set it explicitly on
      every font.
- [ ] **Use `sky-tracker`'s exact font stack**: `Roboto Mono` from
      `gfonts://`, with the glyph string defined once as a YAML anchor
      (`&glyphs`) and reused (`*glyphs`) across sizes. It runs 12/14/15/16/18/24;
      we likely need 12, 14, 16, 18 and 24.
- [ ] **UI-40a: give every font the shared glyph set.** `plane-tracker`
      decision 51: a reduced set only pays off if the strings never change, and
      a clock-only `mono24` set drew `UPGRADING` as boxes. Our strings are
      driver names, team names and circuit names from a live feed — they
      **will** contain characters nobody predicted.
- [ ] **UI-40b: accented and non-Latin characters are guaranteed here**, unlike
      in either sibling. `Autódromo José Carlos Pace`, `Hülkenberg`,
      `Nürburgring`, `Pérez`, `Antonelli`. `sky-tracker`'s glyph set is ASCII
      plus `°` and **will draw boxes** for every one of these.
      **Extend the set with Latin-1 Supplement and Latin Extended-A**, or
      transliterate at generation time and state that choice. Extending is
      better: the names are the content.
- [ ] Adopt `plane-tracker`'s **`check_glyphs.py`** (its decision 52): it
      resolves each label's *effective* font and checks its text against that
      font, including strings set from lambdas via `lv_label_set_text`.
      Checking everything against one shared set is what let the `UPGRADING`
      bug through. Extend it to cover the **generated** circuit and driver
      name tables, which is where our risk actually is.
- [ ] For glyph iconography, load **Material Design Icons from a `type: web`
      URL** with an explicit codepoint list per size, as `sky-tracker` does.
      Flags remain bitmaps (§6.6).

### 6.10 Team colours (UI-32)
- [ ] OpenF1 `drivers` carries **`team_colour`** as a 6-hex RGB string,
      measured present (e.g. McLaren `F47600`, Red Bull `4781D7`). Use it for a
      **3–4 px colour bar** at the left of each order row.
- [ ] It is a live value, so it tracks livery changes with no firmware update.
      **Cache the last known colour per team** so the bar survives a failed
      poll, and fall back to a neutral grey — never to a wrong team's colour.
- [ ] **Do not colour the text** with it. Several team colours have poor
      contrast on black (dark blues especially) and the driver name must stay
      readable. A bar is decoration; the text is information.
- [ ] Under Night Mode (§6.13) team colours **must collapse to intensity, not
      hue**. `plane-tracker` decision 56 found that multiplying red over a blue
      made black and the contact vanished. Two teammates differ by the number
      and the name, not the bar, so losing hue costs nothing.

### 6.11 Detail cards (UI-24)
- [ ] Tapping a driver row opens a card: full name, number, acronym, team,
      nationality + flag, grid position, current position, gap, tyre compound
      and age, status. Tapping the circuit map opens a card with the full facts
      set (§5.5) and the session times in all three zones (§6.7).
- [ ] Touch targets are **finger-sized** — a ~40×40 px minimum hit box, and
      **the whole row is the target**, not just the text. `plane-tracker`
      decision 53: to a finger the label is part of the thing, and it is the
      larger of the two.
- [ ] Clear dismissal: a tap anywhere closes the card (§6.1).
- [ ] **Driver headshot is enrichment, not core.** OpenF1 gives a
      `headshot_url`, and `sky_jpg.h` + `sky_photos.h` already solve the decode
      side. Fetch on tap only, cache a few in PSRAM, never block the render
      loop, and **clear the image the instant the selection changes** so a
      previous driver's photo is never shown against new data
      (`plane-tracker` decision 45).
- [ ] Check the licence and hotlinking terms before shipping a photo fetch
      (§13). The card must be fully useful with no photo at all.
- [ ] **UI-24a: entry and card boxes carry explicit `pad_top`/`pad_bottom` and
      a 44 px height.** `plane-tracker` decision 46: relying on the LVGL
      theme's own padding clipped the bottom of every field.

### 6.12 Status line (UI-25)
- [ ] A small status line under the title carries **faults only** — no data, no
      Wi-Fi, rate limited, API error, clock unsynced, stale calendar. Each must
      be **distinguishable from the others**.
- [ ] `plane-tracker` decision 57: the status line must **not** carry a tally
      that something else also counts. Two disagreeing numbers are worse than
      one.
- [ ] **Distinguish "nothing happening" from "no data".** In `IDLE` the correct
      message is the next round and its countdown, not an error.
- [ ] **Data age** is stamped on every **successful parse**, not on an HTTP 200
      (`plane-tracker` decision 50 — a 200 carrying garbage is not fresh data).
      Show it on the debug page and in the status line when it exceeds the
      expected poll interval by a wide margin.

### 6.13 Night mode (UI-45)
- [ ] **Port `sky-tracker`'s Night Mode**, and consider making it the default
      as `plane-tracker` did (its decision 56): after civil dusk (Sun below
      −6°) everything is drawn in red only, at the pixel level in the display
      flush callback, so no widget needs a second colour scheme.
- [ ] `plane-tracker` found this must be a **red-only palette, not a
      translucent red overlay** — and that classes must separate by
      **intensity**, not hue (§6.10).
- [ ] It also found that `set_dark()` must apply the palette
      **unconditionally** (its decision 61): an early-out on an unchanged
      palette meant that at boot, where dark is already the default, widgets
      were never repainted and kept the daylight colours written literally into
      the YAML.
- [ ] **Whether it defaults on is an open question here** (§13) — this is a
      living-room object showing a sport, not a night-vision instrument, and a
      red screen for 19 h a day in December is a strong aesthetic choice to
      make by default.

---

## 7. Settings screen (UI-16)

Opened via the **gear icon in the lower-right** of the primary page: 46×46 at
`BOTTOM_RIGHT` offset −2/−2, transparent background, `LV_SYMBOL_SETTINGS` in
`montserrat_28`, on **`on_short_click`** so a long press does not also open it —
and `on_long_press` opens the debug page. Ported from `sky-tracker` exactly.

| Control | Type | Values |
|---|---|---|
| Latitude / Longitude | entry | decimal degrees only (§6.8) |
| Auto-dim display | checkbox | on/off — sunset-based dimming (§6.8) |
| Brightness | slider | 1–100 % (the **daytime** level while auto is on) |
| Night mode | checkbox | red palette after civil dusk (§6.13) |
| Clock format | dropdown | 24 h local / 12 h local / UTC-Zulu |
| Carousel interval | slider | 15–120 s, step 15, default **45** |
| Carousel order | dropdown | calendar / random / current season only |
| Show practice sessions | checkbox | treat FP as a session worth a page |
| Show sprint sessions | checkbox | — |
| Order columns | dropdown | with team / with gap / with tyre |
| Force page | dropdown | Auto / Race Day / Carousel (**not persisted**, §4.1) |

- [ ] All settings persist across reboot — ESPHome `globals` with
      `restore_value`, or the entity's own `restore_value`.
- [ ] **Adopt the staged Save/Cancel model.** On-screen edits are staged in
      LVGL widgets and only written to entities when **Save** is pressed;
      **Cancel** restores the values captured in `on_load`.
- [ ] **Brightness is the deliberate exception** — the slider drives the
      backlight live so the change is visible, and Cancel restores the opening
      value. Keep the guard: the `on_value` handler compares against the
      current value and returns early to ignore its own echo from the light's
      `on_state`.
- [ ] Reuse the `textarea` conventions: `one_line: true`, `max_length` per
      field, `accepted_chars` restricted so bad input cannot be typed.
- [ ] Reuse the LVGL `keyboard` in `mode: NUMBER`, hidden until a field is
      tapped.
- [ ] Group the page into **tabs** as `sky-tracker` does (its UI-16a) — three
      panels, one shown at a time: **Display**, **Race**, **Location**.

### 7.1 Every setting exposed to the web UI and Home Assistant
All settings must be controllable from **three** places, kept in sync: the
touchscreen, the device's own **web UI**, and **Home Assistant**.

- [ ] `web_server: version: 3, local: true` so assets are served from flash and
      the page works on the fallback AP with no internet. Firmware upload from
      the page stays on.
- [ ] `web_server: sorting_groups:` with `sorting_weight` to section the page
      (HA is unaffected). Groups: **Race**, **Display**, **Location**,
      **System**.
- [ ] Mark diagnostics `entity_category: diagnostic` so they land on HA's
      device page rather than cluttering the controls.
- [ ] Give every entity an `icon:`.
- [ ] **Set web UI authentication.** The page exposes control of every setting.
- [ ] Prefer the HA **native API** over MQTT.

#### 7.1.1 Entity mapping
| Setting | ESPHome entity |
|---|---|
| Latitude, Longitude | two `number` (box mode, step 0.001) |
| Auto-dim display | `switch` |
| Brightness | `number` (1–100 %, slider) |
| Night mode | `switch` |
| Clock format | `select` (24 h / 12 h / Zulu) |
| Carousel interval | `number` (15–120, step 15, slider) |
| Carousel order | `select` |
| Show practice / sprint | two `switch` |
| Order columns | `select` |
| Force page | `select` (not restored) |

#### 7.1.2 This is architectural, not a bolt-on
- [ ] **Settings live in ESPHome entities, not in LVGL widget state.** The
      entity (backed by a global with `restore_value`) is the single source of
      truth; the touchscreen widget is one *view* of it. A design where the
      widget owns the value cannot be driven from HA.
- [ ] **Bidirectional sync:** a touchscreen change publishes the entity state;
      an HA or web change updates the widget.
- [ ] **Guard against feedback loops.** Entity-set → widget-update →
      widget-callback → entity-set is an infinite loop. Suppress the callback
      while applying an external update, or make the setter idempotent and bail
      when the value is unchanged. This is the most likely bug in this area.
- [ ] Route every change through **one apply-settings handler**, so "carousel
      interval changed" retimes the timer and "auto-dim toggled" re-evaluates
      the backlight, regardless of which surface changed it.
- [ ] **Validation lives in the handler, not the widget** — HA and the web UI
      can submit out-of-range values the keypad would have prevented.

#### 7.1.3 Read-only diagnostics to expose
Cheap, and they make the device debuggable without a serial cable.

- [ ] **Weekend state** (§4.1) — the single most explanatory value.
- [ ] **Order mode** — `ENTRY LIST` / `GRID (PROVISIONAL)` / `GRID` / `FINAL`
      (§6.3), and **when the next OpenF1 window closes**, which is the next
      moment anything will change.
- [ ] Next round, next session, and the countdown to it.
- [ ] Calendar source and age (`compiled` / `fetched, 3 h`).
- [ ] Last successful Jolpica poll, last successful OpenF1 poll — separately.
- [ ] Current circuit on the carousel.
- [ ] Driver count and order mode.
- [ ] Wi-Fi RSSI, free internal RAM, free PSRAM, uptime, firmware version.
- [ ] **Crash record** (reset reason + last crash) — port `sky_diag.h`,
      installed early in `on_boot` (600) so the API can read it before ESPHome
      clears it. Note that `sky_diag.h` writes to a sector of the `skydata`
      partition, which we do not have (§2.4.5): **re-target it to NVS or drop
      the persistence and keep the reset reason.** Do not copy it unexamined.

---

## 8. Feature ideas

Ranked by how well each fits what the device is for. The brief asked for ideas;
these are offered as candidates, not commitments.

**Tier note:** the sponsorship is declined (decision 58), so anything needing
data inside OpenF1's live window is **unavailable** and has moved to §8.4.
Several of them survive in a **post-session** form, which is free — and that
turns out to be the most interesting consequence of the free-only decision.

### 8.1 Strong candidates — these earn their place
- [ ] **Championship standings page.** `driverStandings` is 10.1 KB and
      `constructorStandings` is 2.5 KB (measured). Both are already in the
      rotation's natural shape — a list with flags and colour bars — so the
      page costs almost nothing beyond what §6.3 builds. This is the most
      obvious missing page in a season tracker.
- [ ] **"Next session" countdown as the resting state.** The device spends most
      of a race week in `SESSION_SOON`/`RACE_WEEK`. A large, calm countdown
      with the circuit map behind it is the screen that will actually be looked
      at most, and it needs no new data.
- [ ] **Grid → result transition** (§6.3). The one moment the order page
      changes from a prediction to a fact. Animate the finishing order in from
      the grid order, so a driver who gained eight places visibly gains eight
      places. This is the free-tier replacement for position-change flashes,
      and arguably the better feature: it shows the whole race's story in one
      movement instead of a tint every 5 seconds.
- [ ] **Post-session summary page.** `race_control` (20 KB), `stints` (8.4 KB)
      and `laps` (one lap, 7.9 KB) are all **free once the window closes**, so
      the device can show what *happened*: fastest lap and who set it, pit-stop
      counts, and the safety cars and red flags that occurred, with the lap
      each fell on. This is the single strongest free-tier feature and it
      recovers most of what live timing would have given — a day later rather
      than live, on a device that is going to be looked at for the whole week
      anyway.
- [ ] **Tyre strategy on the post-session page** from `stints` (8.4 KB, free
      after the window). Not a live column but a per-driver strip — S/M/H
      blocks sized by stint length. Reads better than a single letter ever
      would, and it is the sort of thing worth studying after a race rather
      than glancing at during one.
- [ ] **Track local time** (§6.7) from `gmt_offset`. One line, free, and
      genuinely clarifying for a calendar that spans 20 time zones.
- [ ] **Circuit "on this day" line.** The facts table already carries
      `firstgp`, `opened`, race count and most-wins (§5.5). A rotating line —
      *"First raced 1950. Hamilton has won here 8 times."* — makes the carousel
      a thing to read rather than a slideshow.
- [ ] **Weather on the post-session page** — `weather` is 36 KB per session and
      free after the window. Track and air temperature and whether it rained
      are part of a race's story, and summarising the session (min/max,
      "rain from lap 31") is more useful than a live thermometer would be.

### 8.2 Worth considering
- [ ] **Sector-coloured trace** if sector boundaries can be sourced. Deferred
      in §6.5 because the GeoJSON has no sectors, not because it is a bad idea.
- [ ] **Fastest-lap highlight** on the `FINAL` order — mark who set it and the
      time. Free after the window, and Jolpica's `fastest` endpoint (1.2 KB)
      gives it directly without touching `laps` at all.
- [ ] **Favourite driver / team.** A setting that pins one row to the top of
      the order page and shows their gap in the header. This is the single
      feature most likely to make the device feel personal.
- [ ] **A Home Assistant race-start notification.** The device already knows
      the schedule precisely; exposing a binary sensor for "session live" lets
      HA do the rest (lights, a TV, an announcement) with no extra code here.
- [ ] **Lap-time sparkline** for the selected driver on the detail card, over
      the whole race rather than the last 20 laps — which is only possible
      *because* the data arrives all at once after the session. One `lv_line`,
      and pit stops show up as spikes without any extra work.
- [ ] **Season progress bar** — rounds completed against rounds remaining, on
      the carousel. One line, and it answers "how far through the year are we".
- [ ] **A quiet line on the empty screens**, in the spirit of
      `plane-tracker`'s §5.13: the off-season and the long `IDLE` stretches are
      the natural home for something other than a countdown. Keep the text in a
      **data file**, not scattered in code, and keep the list a **prime
      length** — `plane-tracker` decision 59 found that mixing day and hour with
      factors that divide 24 shows the same line at a given hour forever.
- [ ] **Pit-stop count** per driver from `stints`.
- [ ] **A "circuit of the day" at a fixed hour** rather than a carousel, as an
      alternative mode. Less motion, more of an object.

### 8.3 Unavailable on the free tier
Not declined on merit — these need data inside OpenF1's live window, which
decision 58 puts out of reach. Recorded so that if the sponsorship is ever
reconsidered, the list is already here and the reasoning is intact.

- **Live running order** during a session (the `RUNNING` mode removed from
  §6.3). The largest single loss, and the reason §6.2's `RESULTS IN ~mm:ss`
  exists.
- **Live gaps and intervals.** Already doubtful on cost grounds — `intervals`
  is 3.5 MB per session (decision 31) — so the window makes a decision we had
  effectively made anyway.
- **Live track status** — flags, safety car, VSC as they happen (§6.2, UI-4).
  Available after the session, which is where it now lives (§8.1).
- **Live tyre compound**, live weather, live fastest-lap. All three survive in
  post-session form (§8.1), which for this device is arguably the better fit.
- **Position-change flashes.** Replaced by the grid→result transition (§8.1),
  which shows more with less.

### 8.3.1 What free-only actually changed
Worth stating, because the shape of the project moved less than the list above
suggests: **the brief is untouched.** The circuit map, the carousel, the facts,
the starting order, both sets of flags and the auto-dim were never live
features. What changed is that the race page tells the time instead of the lap,
and the interesting data arrives half an hour after the race instead of during
it — which suits a device watched all week rather than only on Sunday.

### 8.4 Declined, with reasons
- **Live car telemetry / throttle & brake traces.** `car_data` requires a
  driver filter (HTTP 422 without one) and is enormous. A 480×480 panel showing
  one driver's throttle trace is a worse version of what a phone does well.
  Doubly out of reach now (§8.3).
- **A live track map with moving car positions.** `location` data is per-driver
  and high-rate; 20 drivers at a usable rate is the `intervals` problem (3.5 MB
  per session) multiplied. This is the one feature that would be genuinely
  impressive and it is the one the hardware and the APIs most clearly cannot
  support. Recording it as declined so it is not rediscovered as a good idea.
- **Radio messages / team audio.** No decode path, no speaker, and the text is
  not in the feeds.
- **Betting odds or predictions.** Out of character for the device and a
  licensing problem.
- **The F1 logo or team logos.** Trademarks (§3.7).

---

## 9. Performance budget / constraints

- [ ] 480×480 RGB565 framebuffer = **460 KB** → PSRAM framebuffer is
      mandatory. Expect PSRAM bandwidth, not CPU, to be the limit.
- [ ] **Our frame-rate problem is small and our boot-time problem is not.**
      Nothing moves per-frame (§2.4.1), so the siblings' render budget does not
      bind here. Instead: 22 order rows × 4 children, plus 40 generated traces,
      plus ~82 flag bitmaps, plus the facts table. **Measure boot time** and, if
      it is long, follow `plane-tracker` decision 64 — move pure pixel work to a
      **one-shot task on core 1**, refuse to draw until a `g_ready` flag, and
      set image sources on first use rather than at creation.
- [ ] **Flash budget**, estimated from measurements:
      traces 19.2 KB · flags ~150 KB · calendar ~8 KB · facts ~20 KB ·
      fonts (5 sizes, extended glyph set, 4 bpp) ~80 KB. Call it **~280 KB** of
      assets. Trivial against 16 MB, and the default partition table (§2.4.5)
      leaves two large app slots.
- [ ] **RAM is the real budget**, as in both siblings: 460 KB framebuffer +
      57.6 KB LVGL buffer + TLS buffers for **two** hosts + `web_server`.
      Measure free internal RAM and PSRAM with everything enabled, and log it
      at boot.
- [ ] **JSON parsing is the main unknown** — a 48 KB response with ~20 fields
      per row. Measure early, prefer selective parsing, and size the buffer for
      48 KB rather than the common case.
- [ ] Wi-Fi reconnect and API-unavailable states **must not block rendering**.
      The carousel in particular must keep turning with no network (§6.4).
- [ ] **Free-only removes this project's one timing pressure.** With no 5 s
      `RACE_LIVE` poll, there is no state in which a fetch, a TLS handshake and
      a redraw contend. The TLS-session reuse of NET-12 is still worth having —
      ~1.2 s of crypto on the panel's bus is worth avoiding at any cadence —
      but it is no longer load-bearing.

---

## 10. Non-functional

- [ ] OTA updates enabled, with the progress panel of §2.4.6.
- [ ] Graceful degradation: clear, **mutually distinguishable** "no Wi-Fi",
      "no data", "rate limited", "clock unsynced" and "stale calendar" states
      (§6.12).
- [ ] Recover from an outage without reboot; back off on repeated failures
      rather than hammering an endpoint (§3.7).
- [ ] **Port the Wi-Fi resilience** (`sky-tracker` NET-2/NET-2b): an open
      fallback AP named with the MAC suffix, `captive_portal` for setup, and a
      boot/status page shown until Wi-Fi connects that returns after 10 s
      offline — **but never over the settings page**. This is the difference
      between a recoverable device and a cable trip.
- [ ] Surface a **crash record** as a diagnostic entity (§7.1.3), with the
      partition caveat noted there.
- [ ] Bump `fw_version` on every install and show it in HA, on the settings
      page and on the status page.
- [ ] No secrets in the repo — Wi-Fi credentials and the API key in
      `secrets.yaml` (git-ignored). Commit `secrets.yaml.example`.
- [ ] Config reproducible from a clean checkout, including the generators.
- [ ] **`deploy.sh` fails on any compiler warning in our own code.**
      `plane-tracker` decision 63: grepping only for `error:` hid a
      `-Wformat-truncation` on a string buffer. Copy the script and the
      discipline.
- [ ] **Host tests, run with `make -C tests`**, as `plane-tracker` does (129
      checks, 0 failures). Keep the testable logic free of LVGL so it builds on
      the host. The high-value targets:
      - the **state machine** (§4.1) against a table of synthetic clocks,
        including a race at `04:00Z` read from Alaska — the bug §4.1 warns about;
      - **demonym → ISO3** resolution against the captured driver fixture,
        asserting every present nationality resolves and every absent one
        yields no flag;
      - **grid extraction** (RACE-11) against `openf1-position-11377.json`,
        asserting a complete P1–P22 ordering;
      - **circuit matching**, asserting all 23 calendar rounds resolve to a
        trace;
      - **projection and fit**, asserting aspect ratio is preserved and every
        trace lands inside its box at every baked rotation.

---

## 11. Out of scope (for now)

- Live car positions on the track map (§8.3).
- Car telemetry, team radio, tyre-degradation modelling.
- Pit-lane geometry — not in the circuit data (§6.5).
- Sector boundaries and sector timing (§8.2 if sourced).
- Historical browsing beyond the facts table — this is a *current season*
  device.
- Other series (F2, F3, FE, WEC). The data sources differ and the UI would
  need a series selector.
- Audio alerts.
- Multi-device sync.

---

## 12. Decision log

Rows are never deleted — a superseded decision records why the current one
exists.

| # | Decision | Date | Status |
|---|---|---|---|
| 1 | **Third board**, alongside `sky-tracker` and `plane-tracker` — separate name, hostname and HA identity | 2026-10-01 | active |
| 2 | Hardware is **identical** to `sky-tracker`; no new peripherals, and GPS/compass support is deliberately left out | 2026-10-01 | active |
| 3 | Inherit `sky-tracker`'s pin map, `sdkconfig_options`, LVGL tuning and boot ordering; inherit `plane-tracker`'s corrections to them | 2026-10-01 | active |
| 4 | Backlight PWM at **30 kHz**, not `sky-tracker`'s 1 kHz — 1 kHz whines at the low duty this device will sit at | 2026-10-01 | active |
| 5 | `logger: hardware_uart: UART0` — the S3 default console is on GPIO19, which is the GT911's SDA | 2026-10-01 | active |
| 6 | **Default partition table**; the calendar is compiled in instead of cached (decision 12) | 2026-10-01 | active |
| 7 | **Lat/lon exist only to compute sunrise/sunset for auto-dim.** Nothing is plotted from the observer position | 2026-10-01 | active |
| 8 | **Decimal degrees only** — the DMS entry widget is declined, unlike both siblings, because coordinates are not the instrument here | 2026-10-01 | active |
| 9 | **Ergast is dead** (HTTP 404) and is not a candidate | 2026-10-01 | active |
| 10 | **Jolpica** (`api.jolpi.ca`) is the system of record for calendar, circuits, drivers, qualifying, results and standings | 2026-10-01 | active |
| 11 | **OpenF1** (`api.openf1.org`) is the system of record for the live session: actual grid, running order, tyres, flags, weather | 2026-10-01 | active |
| 12 | The **season calendar is compiled in** and refreshed over the network; the compiled one is the floor, so the carousel runs with no network | 2026-10-01 | active |
| 13 | Everything on screen is a function of **one weekend state machine** (§4.1), derived from UTC session times — **never from the device's local date** | 2026-10-01 | active |
| 14 | Poll intervals are **a table keyed on state**, not a fixed timer; only `RACE_LIVE` (5 s) is demanding | 2026-10-01 | active |
| 15 | **OpenF1 has no `starting_grid` endpoint** (HTTP 404, measured) | 2026-10-01 | active |
| 16 | The **actual grid is the earliest `position` row per driver** — measured as a complete P1–P22 set at one timestamp | 2026-10-01 | active |
| 17 | Qualifying classification is shown as **`GRID (PROVISIONAL)`** until decision 16 or the results confirm it; penalties are not applied in it | 2026-10-01 | active |
| 18 | The circuit flag comes from **`Circuit.Location.country`**, never the race name — observed: "Bahrain Grand Prix in Malaysia" at Sepang | 2026-10-01 | active |
| 19 | Driver flags come from Jolpica's **demonym**, via a generated demonym→ISO3 table. OpenF1's driver `country_code` is **null throughout** | 2026-10-01 | active |
| 20 | A driver with no resolvable nationality **draws no flag** — 9 of 32 drivers have no `nationality` field at all | 2026-10-01 | active |
| 21 | Circuit maps come from **`bacinger/f1-circuits` GeoJSON, MIT** — geometry, not artwork, so **no licence is imposed on this firmware** (unlike `plane-tracker`, which is GPL-2.0-or-later) | 2026-10-01 | active |
| 22 | **Ship all 40 traces** (19.2 KB as `int16` pairs), not just the current calendar | 2026-10-01 | active |
| 23 | `circuitId` → trace matching happens **at build time** by centroid (measured ≤1.1 km, 23/23) and the build **fails** on an unmatched calendar circuit | 2026-10-01 | active |
| 24 | Traces are **pre-projected and pre-fitted** offline, with a baked per-circuit rotation to fill the box, and a **north arrow** whenever the rotation is non-zero | 2026-10-01 | active |
| 25 | `cos(lat₀)` in the projection is **mandatory** — 1.6× east-west distortion at Silverstone without it | 2026-10-01 | active |
| 26 | **Turn counts are hand-curated, never computed from the trace** | 2026-10-01 | active |
| 27 | Lap records, most-wins and race counts are **computed offline from Jolpica** and baked into the facts table; the device never fetches them | 2026-10-01 | active |
| 28 | The map and the full order **do not share a screen**: the race page carries the map plus a top-5 strip, and a second page carries all 22 | 2026-10-01 | active |
| 29 | Order rows are **C++-built containers, not an LVGL `table`** — each row needs a flag image and a team-colour bar, which a table cell cannot hold | 2026-10-01 | active |
| 30 | **One ordered array is the single source of truth** for position; the header, the top-5 strip and the order page all read it, never the raw feed | 2026-10-01 | active |
| 31 | The **gap column is left blank rather than wrong** — `intervals` is 3.5 MB per session and will not be fetched wholesale | 2026-10-01 | active |
| 32 | Carousel interval is a **15–120 s slider, default 45 s**; transitions are a **fade**, not a slide | 2026-10-01 | active |
| 33 | Circuits not on the current calendar are **labelled as such** on their card, since all 40 ship | 2026-10-01 | active |
| 34 | **Copy `sky_flags.h`** (flag-icons 7.5.0, MIT); add **`MCO`**, plus `BHR` and `SAU` for safety. Measured: `MCO` is the only gap for the 2026 season | 2026-10-01 | active |
| 35 | **Extend the font glyph set to Latin-1 Supplement + Latin Extended-A.** Driver and circuit names guarantee accents, unlike either sibling's data | 2026-10-01 | active |
| 36 | Every font gets the **shared glyph set**, and `check_glyphs.py` is extended to cover the **generated** name tables | 2026-10-01 | active |
| 37 | Team colours are a **bar, never the text colour** — several liveries have poor contrast on black | 2026-10-01 | active |
| 38 | Settings use **staged Save/Cancel**; brightness is the live exception | 2026-10-01 | active |
| 39 | **All settings exposed to the web UI and HA**; entities are the source of truth, widgets are views | 2026-10-01 | active |
| 40 | Clock is a **three-state dropdown** (24 h local / 12 h local / UTC-Zulu), default 24 h local | 2026-10-01 | active |
| 41 | Session times are converted from **UTC once, centrally**; track local time from `gmt_offset` is offered on the detail card | 2026-10-01 | active |
| 42 | **The lat/lon and the timezone are independent**, and the settings page says so in one line | 2026-10-01 | active |
| 43 | **Attribution is displayed regardless** of what the terms turn out to require: on-screen credit plus links in the web UI | 2026-10-01 | active |
| 44 | **No F1/FIA/team logos or wordmarks** anywhere in the UI | 2026-10-01 | active |
| 45 | **Copy** reusable headers into this project; no shared library across the three | 2026-10-01 | active |
| 46 | `sky_diag.h` is **re-targeted or trimmed**, not copied unexamined — it writes to the `skydata` partition we declined (decision 6) | 2026-10-01 | active |
| 47 | **Live car positions on the track map are declined** — the data rate the APIs expose cannot support 20 cars on this hardware | 2026-10-01 | active |
| 48 | A **manual `Force page` override** exists for testing and is **not persisted** across reboot | 2026-10-01 | active |
| 49 | **Race page layout approved**: circuit map plus a top-5 strip on `race_page`, the full 22-driver order on `order_page` | 2026-10-01 | **approved by owner**, confirms 28 |
| 50 | **Both services' terms read and approved.** Jolpica: CC BY-NC-SA 4.0, non-commercial, 4 req/s burst, 500 req/hour sustained. OpenF1: CC BY-NC-SA 4.0, non-commercial/educational | 2026-10-01 | **approved by owner**, closes open question 1 |
| 51 | **OpenF1's free tier is historical only.** Live data — 30 min before a session to 30 min after — needs the €9.90/month sponsor tier. Every live feature sits inside that window | 2026-10-01 | active |
| 52 | ~~The unsponsored path is the baseline and is built first; `RUNNING` mode layers on top~~ | 2026-10-01 | **superseded by 58, 61** — the free path is now the *only* path, so it is not a fallback and there is nothing to layer on top |
| 53 | ~~`Live timing` is a setting, default off~~ | 2026-10-01 | **superseded by 58** — no live polling at all, so no setting |
| 54 | **Attribution is mandatory** — both datasets are CC BY-NC-SA 4.0 (BY). OpenF1's FAQ waives credit while its footer requires it; attribute both anyway | 2026-10-01 | supersedes the "regardless" wording of 43 |
| 55 | **ShareAlike reaches the generated data, not the code.** The compiled-in calendar and the Jolpica-derived facts are CC BY-NC-SA 4.0 adaptations; generators emit that notice in the header comment. Decision 21's "no licence imposed" covers the MIT geometry only | 2026-10-01 | refines 21 |
| 56 | **Jolpica's limits will decrease** as token access rolls out; the poll table (§3.6) is the single place to retune and the headroom is not to be spent | 2026-10-01 | active |
| 57 | **Two failure states are distinct and must look it**: `429 rate limited` and `no data / network`. The third ("live timing unavailable") is gone with 58 — a request never made cannot fail | 2026-10-01 | refined by 58 |
| 58 | **Free tiers only. The €9.90/month OpenF1 sponsorship is declined** | 2026-10-01 | **decided by owner**, closes open question 9 |
| 59 | **NET-14: never request OpenF1 inside its live window.** The device knows every session time, so it computes the window (start −30 min to end +30 min) and skips. Strictly better than a request that cannot be rate-limited, mistaken for a fault, or return a stale snapshot that looks like success | 2026-10-01 | active |
| 60 | **`POST_SESSION` is entered when the window closes**, not when the session ends. That transition is the one moment the race page changes from a prediction to a result, and it is the one to get exactly right | 2026-10-01 | supersedes `POST_RACE` in 13 |
| 61 | **No `RUNNING` order mode, and no "live timing unavailable" apology.** A device that never attempts live timing has nothing to report as missing; a permanent banner about an absent feature is worse than no banner | 2026-10-01 | active |
| 62 | **The race page is clock-driven, never lap-driven.** `RESULTS IN ~mm:ss` replaces `LAP n/m`: truthful, derived from data the device already has, and the only thing that moves during a race. A frozen or wrong lap counter is the worst possible element on that page | 2026-10-01 | active |
| 63 | **Jolpica `Results[].grid` is the primary actual-grid source**; RACE-11 (earliest OpenF1 `position` rows) is kept as a cross-check and a race for whichever publishes first | 2026-10-01 | refines 16 |
| 64 | **Live features move to post-session form rather than being dropped**: track status, tyres, weather, fastest lap and lap traces are all free once the window closes, and a post-session summary page recovers most of what live timing would have given | 2026-10-01 | active |
| 65 | **No authentication anywhere.** Both free tiers are unauthenticated, so neither source puts anything in `secrets.yaml` | 2026-10-01 | active |
| 66 | **Peak request rate is 18/hour** (`POST_SESSION`), against Jolpica's 500/hour. There is no demanding case left in this design | 2026-10-01 | active |

---

## 13. Open questions

1. ~~**Terms of use and rate limits for Jolpica and OpenF1.**~~
   **CLOSED 2026-10-01** — both read in full and approved by the owner; the
   record is §3.7. It surfaced one material constraint, now decision 51:
   OpenF1's live window is a paid tier, which moved the whole live-timing
   design into RACE-12 (§6.3).
2. **Does Night Mode default on?** (§6.13) `plane-tracker` made red-at-1 % the
   default for a night-vision instrument. This is a living-room object showing
   a sport, and 19 h of red a day in December is a strong choice to make by
   default. Recommend **off by default, on by setting.**
3. **Do 22 rows fit at `mono12`?** (§6.3) ~19 px per row is tight. Needs a
   hardware measurement, not a calculation. Fallback is dropping the TEAM
   column, not shrinking the font.
4. **Driver headshot licence and hotlinking terms** (§6.11). The card must work
   without photos, so this gates an enrichment rather than the feature.
5. **Is the 2026 calendar Jolpica serves correct?** It returns 23 rounds
   against 24 circuits, with at least one oddly-named round (§3.4). Worth
   cross-checking against OpenF1's `sessions?year=2026` before building the
   compiled-in calendar from it.
6. **What is "race day" for a sprint weekend?** The Sprint is a race with its
   own grid and result. Recommend treating it as a first-class `RACE_LIVE`
   session, which the state machine already allows, and saying `SPRINT` in the
   header.
7. **Serial port for the first flash** — `plane-tracker` holds
   `/dev/cu.usbserial-21340` on this host and only one board can use it at a
   time.
8. **What does an unauthenticated OpenF1 request return inside the live
   window?** Still unmeasured, but **no longer on the critical path** — NET-14
   (decision 59) means the device never makes that request. It remains a
   **defensive** question: if a session time moves and a request lands inside a
   window anyway, the handler must treat anything unexpected as "no data". Low
   priority; measure it if the chance arises.
9. ~~**Take the €9.90/month OpenF1 sponsorship?**~~
   **CLOSED 2026-10-01 — declined.** Free tiers only (decision 58). The design
   response is NET-14 (§3.6.1), the cost is stated in §3.6.2, and the affected
   features are preserved in §8.3 in case it is ever reconsidered. **The
   original brief is unaffected.**
10. **Is the `POST_SESSION` window-close moment exactly right?** The +30 min
   boundary is OpenF1's published definition, but whether their data is
   actually readable at +30:00 or a little later is unmeasured. Decision 60
   makes this transition the race page's one real moment, so **retry rather
   than trusting the boundary** — and measure the true lag after the first
   race the device sees.

### Settled before this draft was written
| Question | Answer |
|---|---|
| Pin map / RGB timings | Supplied by the `mipi_rgb` model (§2.1) |
| Backlight PWM-capable? | Yes — `ledc`, GPIO38; use **30 kHz** (§2.4.7) |
| Ambient light sensor? | None fitted; auto-dim uses sun position (§6.8) |
| Custom data partition? | **No** — default table, calendar compiled in (§2.4.5, §4.3) |
| Which F1 data source? | **Jolpica + OpenF1**; Ergast is dead (§3.0, §3.1) |
| Where does the starting grid come from? | Earliest OpenF1 `position` rows; qualifying is provisional (§3.5) |
| Where do circuit maps come from? | `bacinger/f1-circuits` GeoJSON, MIT (§5.1) |
| Does it cover the 2026 calendar? | **23/23 rounds**, worst match 1.1 km (§5.2) |
| How big are the traces? | **19.2 KB** for all 40 (§5.2) |
| Are the flags we need available? | 79 present; **only `MCO` missing** (§6.6) |
| Does the firmware inherit a licence? | **No** — MIT geometry, unlike `plane-tracker`'s GPL artwork (§5.1) |
| Rendering approach | YAML shell + C++ headers, `lv_line` for traces (§4.2, §6.5) |
| Share code or copy? | **Copy** into this project (§4.2) |
| Terms of use for both sources | **Read and approved** — CC BY-NC-SA 4.0 both (§3.7) |
| Are our poll rates within the limits? | Yes, with large headroom (§3.7.1, §3.7.2) |
| Is attribution required? | **Yes** — BY on both; links in the web UI (§3.7.3) |
| Is live timing free? | **No** — OpenF1 paid tier, 30 min either side of a session (§3.7.2) |
| Do we pay for it? | **No** — free tiers only, declined (decision 58) |
| Does that affect the brief? | **No.** Map, carousel, facts, starting order and flags were never live features (§8.3.1) |
| What replaces live order? | A clock-driven race page and a post-session summary (§6.2 UI-3a, §8.1) |
| Does the race map share a page with the order? | **No** — map + top-5, then the full order (§6.2, decision 49) |

---

## 14. Milestones

1. **M0 — Bring-up**
   - [ ] ESPHome in a dedicated `.venv`, kept out of `radioconda`
   - [ ] `f1-tracker.yaml`: hardware block, `sdkconfig_options`, `build_flags`,
         LVGL buffer strategy, boot ordering, 30 kHz backlight, UART0 logger
   - [ ] `secrets.yaml` (git-ignored) with a generated API key;
         `secrets.yaml.example` committed
   - [ ] Entities live on the web UI and HA from the start (§7.1)
   - [ ] **Port `panel_soft_reset()` properly** (§2.1) rather than inheriting
         `plane-tracker`'s open question
   - [ ] Config validates, compiles, flashes over USB
   - [ ] Confirm panel, touch, backlight, PSRAM and the board revision
2. **M1 — Circuit data and the carousel**
   - [ ] `tools/gen_circuits.py` → `f1_circuits.h` (all 40, pre-projected,
         pre-fitted, with the `circuitId` cross-reference)
   - [ ] `tools/gen_calendar.py` → `f1_calendar.h`
   - [ ] Host tests: circuit matching, projection, fit, non-degenerate geometry
   - [ ] `circuit_page` drawing a trace with `lv_line`, north arrow,
         start/finish tick
   - [ ] Carousel timing, order and fade; **working with no network**
   - *This milestone is deliberately first: it is the state the device spends
     most of the year in, it needs no live data, and it proves the map
     rendering that everything else depends on.*
3. **M2 — Facts and flags**
   - [ ] `tools/gen_facts.py` — GeoJSON properties, offline Jolpica
         aggregation (lap records, most wins, race counts), curated rows with
         per-value sources
   - [ ] Copy and extend `sky_flags.h`: add `MCO`, `BHR`, `SAU`
   - [ ] Generated demonym→ISO3 table + host test against the driver fixture
   - [ ] Extended font glyph set; `check_glyphs.py` over the generated tables
4. **M3 — Data path and the state machine**
   - [ ] `f1_net.h`: TLS to both hosts, persistent sessions, selective parse
   - [ ] `f1_state.h`: the state machine, the poll-interval table, retiming
   - [ ] Host tests for the state machine, **including a `04:00Z` race read
         from Alaska**
   - [ ] Calendar refresh superseding the compiled floor; source + age
         diagnostic
5. **M4 — Race day**
   - [ ] `race_page`: header, state line, map, top-5 strip
   - [ ] `order_page`: 22 rows, flags, team-colour bars
   - [ ] Grid extraction (RACE-11) + host test against the position fixture
   - [ ] **Measure 22 rows at `mono12` on hardware** (open question 3)
   - [ ] **The four order modes** (RACE-12): `ENTRY LIST` →
         `GRID (PROVISIONAL)` → `GRID` + `FINAL`. Host-test all transitions,
         including the window-close moment (decision 60).
   - [ ] **NET-14**: compute OpenF1's live window and skip requests inside it;
         host-test the window arithmetic against the session fixtures
   - [ ] `RESULTS IN ~mm:ss` countdown (UI-3a) — the only moving element
   - [ ] Attribution footer and the web UI links (§3.7.3)
   - [ ] The two distinct failure states (decision 57)
6. **M5 — Settings and web/HA parity**
   - [ ] Settings page with tabs, staged Save/Cancel, numeric keyboard
   - [ ] Every entity on the web UI and HA, sorted into groups
   - [ ] Auto-dim (§6.8) and the lat/lon-vs-timezone note
7. **M6 — Detail cards, standings and the post-session summary**
   - [ ] Driver and circuit detail cards; headshot as best-effort enrichment
   - [ ] `standings_page`
   - [ ] **`summary_page`** (§8.1) — fastest lap, pit stops, tyre strategy
         strips, flags that occurred, weather. All free once the window closes,
         and the feature that recovers most of what live timing would have been
8. **M7 — Polish**
   - [ ] Error states, Night Mode decision, OTA panel, crash record
   - [ ] `deploy.sh` with warnings-are-errors
   - [ ] Enclosure
