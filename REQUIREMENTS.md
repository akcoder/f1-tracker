# F1 Tracker — Requirements

**Target hardware:** Guition ESP32-4848S040 (ESP32-S3, 4.0" 480×480 IPS)
**Framework:** ESPHome (ESP-IDF)
**Status:** Draft rev 26 — living document, updated as decisions are made
**Last updated:** 2026-10-08 (rev 26: firmware updates and OTA modelled on
`sky-tracker`'s UI-68 — the prompt, the notes, the icon, the install card)

---

## 1. Purpose

A wall/desk-mounted 480×480 touchscreen that renders the Formula 1 season.

On a **race day** it shows the circuit map for that weekend's track and the
drivers in starting order, each with the flag of their nationality, under a
header carrying the circuit's country flag. On a **non-race day** it rotates
through **three kinds of card** — the circuit map of every track on the
calendar, a profile of every current driver, and a profile of an F1 legend —
15–120 s each, with facts, photographs and career records (§6.4).

**One driver is watched.** The device ships watching **Max Verstappen** and
raises an alert when he is racing, when his grid slot is known, when his result
lands, and when he passes a career milestone (§6.14). The watched driver is a
setting, so it is a mechanism rather than a hard-coded name.

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
- [ ] **BUILD-2: there is no board to flash yet** (decision 84). Everything up
      to and including `esphome config` and `esphome compile` runs on this
      host; nothing is installed. The milestones are ordered around that
      (§14), and the hardware-gated items are collected in §14.1 so they are
      not mistaken for forgotten work.
- [ ] Serial port, when a board arrives: `plane-tracker` uses
      `/dev/cu.usbserial-21340` on this host, and only one board can hold it at
      a time.

### 2.3 Reference configs
Two mature projects by the same author drive this board, and both are on disk:

| Project | Path | Role here |
|---|---|---|
| `sky-tracker` | `<sky-tracker>/sky-tracker.yaml` + `sky-tracker/*.h` (paths in `REQUIREMENTS.private.md`) | **UI cues, flags, platform config, settings page, web UI.** Installed and working (rev 4, fw 4.5.34) |
| `plane-tracker` | `<plane-tracker>/` (path in `REQUIREMENTS.private.md`) | **Document style, requirement IDs, and 65 hard-won decisions** already distilled from `sky-tracker` |

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
| `CONFIG_LWIP_TCP_WND_DEFAULT: "65535"` | **Raised from `sky-tracker`'s 32768** so every response fits one window — §2.4.8 |
| `CONFIG_LWIP_TCP_RECVMBOX_SIZE: "64"` | Must scale with the window: ≥ window/MSS = 65535/1440 = 46 |
| `CONFIG_LWIP_TCP_SACK_OUT: y` | Selective ACK — recovers from a single lost segment without re-sending the window behind it (§2.4.8) |
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
- [x] **UI-68: internet updates, modelled on `sky-tracker` 4.6.27** (decision 183).
      `f1_updlogic.h` (pure, host-tested) and `f1_update.h` (the card):
      - the manifest is the GitHub release's `manifest.json`; ESPHome's
        `http_request` update entity reads it **3 min after boot, then hourly**,
        and on demand; the entity's own polling is off so every check is logged
        (NET-13a: `firmware: GET <url>` at DEBUG, the answer at INFO);
      - **only a NEWER version is an update** (numeric compare: `0.10.0` >
        `0.9.0`). ESPHome calls any difference "available", so without this an
        *older* release is offered and installed over a newer one - which
        `sky-tracker` 4.6.5 did;
      - **Settings > Check for updates** opens a card on the top layer:
        *Checking for updates* → *Up to date* / *Update available* (Not now,
        Update) / *Couldn't check*;
      - **the hourly check never opens anything by itself** (UI-68a): an amber
        download icon appears beside the gear, **on the pages that have a gear**,
        and tapping it shows the prompt;
      - **UI-68c: the prompt shows the release notes** - the manifest's
        `ota.summary`, Markdown marks dropped, blank runs squeezed, typographic
        punctuation folded to the fonts, at most ~1.5 KB, in a box that scrolls.
        `tools/make_release.py` writes it, from `--notes FILE` or the commit
        subjects since the previous tag;
      - **UI-68b: the install** is one blocking call on the main loop, so the
        card is redrawn from the OTA component's own hooks: *Updating firmware*,
        *Downloading*, **Keep the power on**, a bar, then *Installed, restarting*
        or *Update failed*. If the entity is still "available" two minutes in,
        the install did not happen and the prompt returns;
      - the **Upgrade Check** button on the web UI and in HA (System group,
        config category) runs the same check quietly - the Firmware row and the
        icon show the result;
      - all **three OTA sources now show on the panel and pause the data task**:
        the IDE upload and the web page's upload on the existing OTA panel
        (the web platform had no hooks at all, so a web upload froze the screen
        on whatever was last drawn), the internet install on the update card;
      - `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC: y` - the check is a **third TLS
        session** beside the two the data task keeps open (NET-12), and from
        internal RAM it ran out of memory on `sky-tracker` (`-0x7F00`).
        **Unmeasured here** (§14.1): it moves buffers to PSRAM, which is also what
        the panel refills from.
- [ ] Wi-Fi uses **ESPHome defaults** — `plane-tracker` decision 42 reversed
      `sky-tracker`'s `reboot_timeout: 0s` / `ap_timeout: 30s` overrides.
      Follow the newer decision.

#### 2.4.7 Backlight PWM frequency — 30 kHz, not 1 kHz
`sky-tracker` runs `ledc` at 1000 Hz. `plane-tracker` decision 62 raised it to
**30 kHz** because 1 kHz is audible: the backlight converter's inductor whines
loudest at low duty, which is exactly where dimming parks it.

- [ ] Use **30 kHz**. This device will sit dimmed overnight for most of the
      year at the deployment latitude (§6.8; value in `REQUIREMENTS.private.md`), so it lands squarely in the whine band.

#### 2.4.8 Download buffers — sized so every response fits one window (NET-15)
**Decision 99: raise the TCP receive window from 32 KB to 64 KB, and the HTTP
client's receive buffer from its 512 B default to 4 KB.**

Throughput over a long link is capped by **window ÷ round-trip time**, and from
the deployment site (a high-latitude, remote location) these hosts are **~100 ms away**. `sky-tracker` measured exactly this:
the IDF default gave 57 KB/s and a 32 KB window gave ~320 KB/s — which matches
the arithmetic to within rounding, so the model is trustworthy enough to size
against.

| Window | Ceiling @100 ms | 48 KB response | Windows needed | Note |
|---|---|---|---|---|
| 5,760 (IDF default) | 56 KB/s | 1,653 ms | 9 | ESPHome out of the box |
| 32,768 (`sky-tracker`) | 320 KB/s | 250 ms | **2** | one mid-transfer stall |
| **65,535 (chosen)** | **640 KB/s** | **75 ms** | **1** | **no stall** |
| 131,072 | 1,280 KB/s | 38 ms | 1 | needs window scaling; 37 ms for a rare fetch |

**The argument is not raw throughput, it is the stall.** At 32 KB our largest
response needs **two** window-fulls, so the transfer pauses for a full
round-trip in the middle waiting for an ACK. At 64 KB **every response this
device will ever make fits in a single window** — checked against all ten
runtime responses, largest 47.0 KB (§3.0):

| Response | Size | At 64 KB |
|---|---|---|
| OpenF1 `sessions?year=X` | 47.0 KB | one window |
| OpenF1 `position` (whole session) | 35.1 KB | one window |
| OpenF1 `weather` | 35.3 KB | one window |
| OpenF1 `race_control` flags | 19.9 KB | one window |
| Jolpica `races` | 14.0 KB | one window |
| …and every other, down to 0.8 KB | | one window |

- [ ] **65,535 is the largest unscaled window** (the TCP header field is 16
      bits). Staying at or below it means **window scaling is not required**,
      which is one less option to get wrong. Going further needs
      `CONFIG_LWIP_WND_SCALE` and buys 37 ms on a response we fetch rarely —
      **not worth it**, and recorded so it is not re-proposed.
- [ ] **`CONFIG_LWIP_TCP_RECVMBOX_SIZE` must scale with the window** — at least
      window/MSS = 65535/1440 ≈ 46, so **64**. Leaving it at 32 would throttle
      the larger window back to roughly what it replaced, which is the quiet
      way this change fails to do anything.
- [ ] **Raise the `esp_http_client` receive buffer to 4096.** Its default is
      **512 bytes**, so a 47 KB response is ~94 read calls, each resuming the
      selective parser (§4.2). This is cheaper than the window change and
      easier to overlook. We construct `esp_http_client_config_t` ourselves in
      the C++ data task (ARCH-3), so it is one field: `.buffer_size = 4096`.
- [ ] **`CONFIG_LWIP_TCP_SACK_OUT: y`** so a single lost segment does not force
      a retransmit of everything behind it. On a 64 KB window over WiFi that is
      a much bigger penalty than it was at 32 KB.

**Cost, stated honestly:**
- [ ] **+32 KB per socket** over the previous setting. With
      `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP: y` (§2.4.2) these buffers come
      from **PSRAM**, so the ~70 KB of free internal RAM is unaffected — which
      is the budget that actually binds (§9).
- [ ] **PSRAM bandwidth is the caveat, not PSRAM capacity.** 64 KB against
      8 MB is nothing, but `plane-tracker` decision 65 measured that network
      and crypto work on the PSRAM bus is what produced `lvgl took a long time
      (1231 ms)` warnings, because the RGB panel refills from the same bus. A
      larger window moves **more** bytes through it per fetch — in a shorter
      burst. **Watch for the warning after this change**, and if it appears,
      the window is the first thing to walk back.
- [ ] Two hosts means potentially **two sockets**, so budget **128 KB** of
      PSRAM for receive windows, not 64.

**What this does not fix, so the expectation is right:**
- [ ] **The TLS handshake dominates everything here.** `plane-tracker` measured
      ~**1.2 s** of crypto for a fresh handshake — an order of magnitude more
      than the 175 ms this change saves on our largest response. **NET-12
      (persistent TLS sessions, decision 65) is by far the larger lever**, and
      this change is a refinement on top of it, not a substitute.
- [ ] **Measured latency is mostly not transfer.** Jolpica responded in
      0.33–0.85 s (§3.0) for payloads of 0.8–14 KB, which at any of these
      window sizes transfer in tens of milliseconds. Most of that time is
      server processing and round-trips. **Do not expect this change to make
      the device feel faster** — it removes a stall on the few large fetches
      and makes the worst case predictable.
- [ ] **Our poll rate is 18 requests/hour** (§3.6.2). This is a tail-latency
      improvement on a rare event, which is a fine reason to make a one-line
      change and a poor reason to expect much from it.
- [ ] **Re-measure after the first flash** and record real figures here,
      replacing this arithmetic. The model is corroborated by `sky-tracker`'s
      two data points but it is still a model.

#### 2.4.9 `logger: hardware_uart: UART0`
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
| `Driver.driverId` | **the only stable driver key** — see §3.4 |
| `Driver.code`, `givenName`, `familyName` | driver list; `code` is the 3-letter acronym |
| `Driver.permanentNumber` | car number. **Reassigned between seasons** — never a key (§3.4) |
| `Driver.dateOfBirth` | age on the profile card (§5.6) |
| `Driver.url` | Wikipedia page — the **photo lookup key** at build time (§5.6.3) |
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
- [ ] **DATA-6: car numbers are reassigned between seasons, so they are not an
      identity.** Measured in the 2026 data: **Lando Norris carries `1`**
      (reigning champion) and **Max Verstappen carries `3`** — not the `1` he
      held as champion, nor his permanent `33`. A watched-driver feature keyed
      on car number (§6.14) would silently follow whoever holds that number
      into the next season. **Key on `driverId`** (`"max_verstappen"`), which
      is stable for the life of the dataset, and resolve the per-session OpenF1
      `driver_number` through the 3-letter acronym when a live row must be
      matched.
- [ ] **DATA-7: `permanentNumber` and `code` are absent for historical
      drivers.** Measured: Ayrton Senna's row carries only `driverId`, `url`,
      `givenName`, `familyName`, `dateOfBirth` and `nationality` — no number
      and no acronym. Legend cards (§5.6) must render without either.
- [ ] **DATA-8: pole counts from the API are wrong for anyone who raced before
      1994**, because Ergast-lineage qualifying data starts there. Measured
      against known career totals:

  | Driver | API poles | Actual | Verdict |
  |---|---|---|---|
  | Verstappen | 65 | 65 | correct (post-1994 career) |
  | Hamilton | 118 | 118 | correct |
  | M. Schumacher | 36 | 68 | **partial** — 1994 onward only |
  | Senna | **3** | 65 | **badly wrong** |
  | Prost | **0** | 33 | **badly wrong** |
  | Clark | **0** | 33 | **badly wrong** |
  | Fangio | **0** | 29 | **badly wrong** |

      A legends card reading *"Alain Prost — 0 poles"* is the single most
      embarrassing thing this device could display. **Hand-curate poles for
      every driver whose career began before 1994, or omit the line entirely
      for them** (§5.6.2).
- [ ] **Wins and starts are correct across every era** — verified against known
      totals for Fangio (24/51), Clark (25/72), Senna (41/162), Prost (51/202),
      Schumacher (91/308), Hamilton (106/395) and Verstappen (71/248). Results
      data runs from 1950, so only qualifying is affected by DATA-8.
- [ ] **DATA-12: OpenF1's track temperature drops to exactly `0.0`.** Measured:
      2 of 168 weather rows in session 11377, with the air at 26 °C. Ignore a
      zero track reading beside warm air, or the summary says "Track 0–48".
- [ ] **OpenF1 returns HTTP 404 for a filter with no matches** — `flag=RED` on
      a race with no red flag gave `{"detail":"No results found."}`. That is an
      empty result, not an error (decision 176).
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
interval in `POST_SESSION`, i.e. **6 requests/hour** against a 500/hour ceiling —
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

### 3.8 Licensing posture — accepted by the owner 2026-10-01
**The project owner accepts the licensing terms of every source used here.**
This is a personal, non-commercial, single-device project, which is the use all
three licences contemplate.

| Source | Licence | What acceptance commits us to |
|---|---|---|
| Jolpica | CC BY-NC-SA 4.0 | attribute; non-commercial; SA on the generated data (§3.7.3) |
| OpenF1 | CC BY-NC-SA 4.0 | attribute; non-commercial |
| `f1-circuits` | MIT | retain the notice |
| flag-icons | MIT | retain the notice |
| Commons photographs | mostly CC BY-SA / CC BY | **credit the photographer on the card** (§5.6.3) |

- [ ] Acceptance is **recorded, not assumed**: every obligation above is a
      checkbox elsewhere in this document, and the build fails on an
      unattributable photograph (§5.6.3).
- [ ] **One thing acceptance cannot do**, stated plainly so it is not
      rediscovered: F1's own media CDN (`media.formula1.com`, OpenF1's
      `headshot_url`) carries **no public licence to accept**. That is why
      §5.6.3 sources portraits from Commons instead — not caution, but the
      availability of actual terms to comply with.
- [ ] If this project is ever published, shared as a kit, or sold, **NC makes
      that a different question** and both data sources must be contacted.
      Recorded in §3.7.3 as well, because it is the obligation most easily
      forgotten years later.

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
                 └───── POST_SESSION <──┴── RACE_LIVE <──────────┘
```

| State | Entered when | Primary page |
|---|---|---|
| `OFF_SEASON` | no future round in the calendar | Circuit Carousel (§6.4) |
| `IDLE` | next session > 24 h away | Circuit Carousel |
| `RACE_WEEK` | any session of the next round < 7 days away | Carousel, **next round pinned first** |
| `SESSION_SOON` | a session starts in < 2 h | Race Day page with countdown (§6.2) |
| `SESSION_LIVE` | now ∈ [start, end] of a non-race session | Race Day page, session order |
| `RACE_LIVE` | now ∈ [start, start + 3 h] of the Race | Race Day page, running order |
| `POST_SESSION` | OpenF1's window closed (session end + 30 min) < 12 h ago — decision 60 | Race Day page, final classification |

- [ ] **"Race day" is a state, not a date comparison.** The brief says "on race
      day"; the device is far from the races, which are in Melbourne, Suzuka and
      Las Vegas. A UTC race start of `04:00Z` is the previous *evening*
      locally. Driving the UI off the device's local calendar date would show
      the race-day screen on the wrong day for roughly half the calendar.
      **Derive state from the session timestamps, in UTC, never from a local
      date.** This is the single most likely bug in this project.
- [ ] **RACE-14: a Sprint is a first-class race day** (decision 90). It has its
      own grid, its own result and its own classification, so it drives the
      same `RACE_LIVE` → `POST_SESSION` path as the Grand Prix and gets the
      full race page. The header says **`SPRINT`** so the two are never
      confused, and a sprint weekend therefore has **two** race days.
- [ ] Sprint sessions appear as `Sprint` and `SprintQualifying` in the Jolpica
      round, and **only on sprint weekends** (§3.4) — `ThirdPractice` is absent
      on those. Drive it off whichever session objects exist.
- [ ] The watched-driver alerts (§6.14) fire for the Sprint too, which is the
      main practical consequence: twice the alerts on a sprint weekend. The
      `Show sprint sessions` setting (§7) gates it for anyone who disagrees.
- [ ] **Milestone counting must not double-count.** A sprint win is not a Grand
      Prix win in the career totals, and Jolpica keeps them in separate
      endpoints (`/sprint/` vs `/results/`). The generated win total (§5.6.2)
      comes from `/results/` only, so a sprint victory must not fire a
      "nth win" milestone.
- [ ] `RACE_LIVE` has no reliable end time in the Jolpica feed (no duration).
      Bound it with **3 h** from the start, and leave it early if the
      classification is final.
- [ ] OpenF1's `date_end` for the Race session is a better bound when it is
      known — it is in the `sessions` list, which is fetched outside the live
      window (NET-14). Prefer it; fall back to the 3 h window.
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

### 4.2.1 The season is resolved at runtime, never fixed (RACE-13)
**Decision 89: the device tracks the _current_ season, resolved from the API at
runtime. No season year appears anywhere in the firmware, the YAML or the
generated headers as a target.**

This is a device meant to sit on a wall for years. A fixed season would mean a
firmware rebuild every winter, and a device that silently shows a stale year if
nobody does one. Following the rollover is the whole difference between an
appliance and a project.

**Measured 2026-10-01 — the endpoints that make this cheap:**

| Query | Result | Role |
|---|---|---|
| `/ergast/f1/current/?limit=1` | `season = 2026`, `total = 23` | **the season resolver** — tiny, and states the season explicitly |
| `/ergast/f1/current/next/` | season 2026, round 16, 2026-10-04 | next session; **goes empty when the season is over** |
| `/ergast/f1/current/last/` | season 2026, round 15 | most recent completed round |
| `/ergast/f1/2027/races/` | `total = 0` | a future season simply does not exist until it is published |

- [ ] **RACE-13a: resolve the season from `/current/`**, which names it in a
      response of a few hundred bytes. Re-resolve on the ordinary `IDLE` /
      `OFF_SEASON` cadence (§3.6) — a season changes once a year, so this costs
      nothing.
- [ ] **RACE-13b: `/current/next/` returning empty is the end-of-season
      signal**, not an error. Measured shape: an empty `Races` array. When it
      is empty and the last round has run, the device is in `OFF_SEASON` and
      waits for `/current/` to name a new season.
- [ ] **RACE-13c: the gap between seasons is months, and it is normal.** From
      the last race until the next calendar is published, there is no future
      session anywhere. Measured today: no 2027 calendar and no 2027 entry list
      exist. **This must not look like a fault** — it is not a "no data" state
      (§6.12), it is `OFF_SEASON`, and the carousel runs the whole time on
      compiled-in data (§4.3).
- [ ] **`OFF_SEASON` is therefore a first-class screen**, not a placeholder.
      It is where the device spends a good part of every year and it is the
      state it will be in when first switched on. Show the completed season's
      champion and final standings, and the carousel; say *"2026 season
      complete — 2027 calendar not yet published"* rather than anything that
      reads as broken.
- [ ] **Compile in whatever is newest at build time and record the season in
      the generated header** (§4.3). A fetched season always supersedes it. A
      device flashed in one year and never rebuilt follows the calendar
      forward on its own.
- [ ] **A season rollover must need no firmware update.** Make this an explicit
      host test: feed the state machine a `current` that advances a year
      mid-run and assert the calendar, the entry list, the driver carousel and
      the watched-driver resolution all follow.

#### 4.2.1.1 What rolling over costs — two consequences worth designing for
Tracking the current season at runtime collides with two things that are
generated at build time (§5.6), and both need handling.

- [ ] **RACE-13d: resolve the legend/current-driver overlap at _runtime_, not
      at build time.** This **corrects decision 86.** If the overlap were baked
      and the season advanced without a rebuild, a driver who retired would be
      suppressed from the legend rotation (because the build marked him
      "current") *and* absent from the driver rotation (because he is no longer
      in the entry list) — he would **vanish entirely**. Ship the legend record
      for every legend, including those currently racing, and decide **per
      card, at runtime**, against the live entry list:
      in the entry list → driver card with a `LEGEND` badge; not in it →
      legend card. Hamilton retiring then moves him into the legend rotation on
      the next poll, with no rebuild.
- [ ] **RACE-13e: a driver with no compiled profile still gets a card.** A
      rookie who joins after the firmware was built has no portrait and no
      career record. Render a **text-only card** from the live entry list —
      name, flag, number, team — and omit the portrait and the stats rather
      than skipping the driver. The card degrades to exactly what is known,
      which for a rookie is very nearly everything anyway.
- [ ] Show the **profile data's build date** on the debug page beside the
      calendar's source and age (§4.3), so a device running two-year-old
      portraits is visible rather than mysterious.
- [ ] **Portraits are the one thing a rebuild is genuinely needed for.** Say so
      in the README rather than pretending otherwise: the device follows the
      season on its own, and an occasional reflash refreshes the faces.
- [ ] Keep the **2026 fixtures as test data** (`reference/samples/`). They are
      a complete real season's shapes, they exercise every data-quality case in
      §3.4, and they are now also the **rollover fixture** — a season that ends
      while a newer one does not yet exist is precisely the edge case above.

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
- [x] **Fail the build on an unmatched calendar circuit**, listing it. A
      silently missing map is the failure mode to prevent, and this is the only
      place it can be caught. Built at M1; all 23 rounds match.
- [x] **MAP-2a: a distance threshold alone cannot separate a true match from a
      false one, so known-distinct neighbours are named explicitly.** Found at
      M1: `zeltweg` matched `at-1969` at **2.4 km** — Zeltweg Airfield hosted
      the 1964 Austrian GP and the Red Bull Ring was built beside it in 1969;
      they are different circuits. Kyalami is a **true** match at 1.3 km, so no
      threshold separates the two cases. The generator carries a small
      `EXCLUDE` set with the reason recorded per row. Worst match is now
      **1.3 km**.
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
- [x] **Draw a small north arrow** whenever the baked rotation is non-zero.
      **Measured at M1: no circuit is north-up.** The closest is Istanbul at
      **4.75°**, and the spread runs to **168.5°** (Monaco). So the arrow is
      **permanent furniture, not an occasional cue** — design it as part of the
      card rather than as an exception. A 2° snap tolerance is applied anyway so
      a future trace that genuinely wants no rotation suppresses the arrow
      instead of pointing almost-straight-up.
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

## 5.6 Driver and legend profiles (DATA-9)

The carousel shows a profile card for every current driver and for a curated
set of F1 legends (§6.4). Everything on those cards is **generated offline and
compiled in** — the device fetches no profile data and no photographs at
runtime.

### 5.6.1 Why this is a build-time job
- The data is **static for a season** (a career record changes 23 times a year),
  so fetching it is waste.
- The counts are **expensive to derive but cheap to store**: a career win total
  needs a query per driver, and arrives as a 3-byte number.
- It keeps the carousel working with **no network at all** (§4.3), which is the
  state the device spends most of the year in.
- Photographs cannot be fetched per card anyway — see §5.6.3.

### 5.6.2 Career statistics — generated, with one hand-curated field
**Measured: `MRData.total` with `limit=1` returns a career count in a tiny
response.** This is the whole technique:

| Statistic | Query | Verified |
|---|---|---|
| Career wins | `/drivers/{id}/results/1/?limit=1` → `total` | ✅ all eras |
| Race starts | `/drivers/{id}/races/?limit=1` → `total` | ✅ all eras |
| Poles | `/drivers/{id}/qualifying/1/?limit=1` → `total` | ❌ **1994 onward only** (DATA-8) |
| Podiums | three queries, positions 1–3 | ✅ all eras |
| Championships | iterate `/{season}/driverStandings/1/` for 1950–2026 and collect the winner | ✅ ~77 small requests, once |
| Seasons, teams, first/last race | `/drivers/{id}/results/?limit=1` + `offset` | ✅ |

- [ ] `tools/gen_drivers.py` emits `f1-tracker/f1_drivers.h` and
      `f1_legends.h`. **Rate-limit the generator to well inside 4 req/s and
      500 req/hour** (§3.7.1) and **cache every response on disk**, so a rerun
      costs nothing. A generator that hammers a volunteer-run service is the
      easiest way to get this project's IP blocked.
- [ ] **Poles are hand-curated for every pre-1994 driver** (DATA-8), entered
      with a `// source:` comment per value as §5.5 does for circuit facts.
      The generator must **refuse to emit an API pole count for a driver whose
      first season precedes 1994** — a build-time guard, not a convention,
      because this is the one field that will otherwise be silently wrong.
- [ ] Assert at build time that every curated driver has **non-zero wins or a
      recorded reason for zero**. A legend with 0 wins is possible (Chris Amon)
      but should be deliberate, not a failed lookup.
- [ ] Legend cards also carry a **short curated line on why they matter** — one
      or two sentences, in the shape of `sky_lore.h`'s constellation cards,
      which is the right precedent in this family for exactly this kind of
      prose.

### 5.6.3 Photographs
- [ ] **Source: Wikimedia Commons, baked into the firmware at build time.**
      `sky_photos.h` already does precisely this for the planet images —
      fetched once, scaled to display size, stored as baseline JPEG, with
      per-image credits in the header comment. Follow that pattern exactly.
- [ ] **Coverage checked 2026-10-01: every current driver and every proposed
      legend has a page image on their English Wikipedia article.** The
      per-image licence was not resolved during this survey — a batched query
      returned **HTTP 429**, and per §3.7.4 a 4xx is not retried tightly. The
      generator resolves it properly (next item).
- [ ] **The generator records the licence and the photographer for every image**
      via the Commons `imageinfo` API (`extmetadata.LicenseShortName`,
      `extmetadata.Artist`), writes both into the generated header, and
      **fails the build on an image it cannot attribute.** Most Commons F1
      photography is CC BY-SA or CC BY, both of which require credit.
- [ ] **Display the photographer's credit on the card**, small. This is the
      same requirement `plane-tracker` records for aircraft photos (its §5.12),
      and here it is mandatory rather than courteous.
- [ ] **Batch and rate-limit the Commons queries**, one request for many
      titles. The 429 above was earned by a per-driver loop; the API accepts
      `titles=A|B|C` and should be used that way.
- [ ] **Do not use OpenF1's `headshot_url`.** Measured, it points at
      `media.formula1.com/.../d_driver_fallback_image.png/...` — F1's own media
      CDN, carrying no public licence, and the path is a fallback transform
      that may not even resolve to a real portrait. Commons has an actual
      licence we can comply with; this does not.
- [ ] **Decision 91: portraits are 240×320**, stored as baseline JPEG and
      decoded with the copied `sky_jpg.h` on display. The brief asked to *show
      picture and profile data* — the picture is the feature, and 150×200 is
      31 % of a 480 px width, which reads as a thumbnail beside the text rather
      than a portrait. 240×320 is half the screen width and carries the card.
- [ ] A decode is one JPEG per card, i.e. once per 15–120 s, so the larger size
      costs nothing at runtime. A 240×320 RGB565 decode buffer is **150 KB**,
      which comes from PSRAM and is **reused**, never reallocated per card
      (§9).
- [ ] **Budget — and the constraint is the app partition, not total flash.**
      54 portraits at 240×320, baseline JPEG q≈0.9, run **25–40 KB each →
      1.4–2.2 MB**. The earlier "trivial against 16 MB" framing was wrong: with
      the default partition table (§2.4.5) the 16 MB is split into **two OTA
      app slots of roughly 6.5–7.8 MB**, and the firmware must fit in **one**.
      Against a slot, 2 MB of portraits is **25–30 %** — fine, but not
      negligible, and not a number to let drift.
- [ ] **The generator reports the total and fails above 3 MB.** If it is ever
      approached, the escape hatches in order: drop legend portraits to
      180×240 while keeping current drivers at 240×320; then lower JPEG
      quality; then trim the legends list (§5.6.4), which is one table.
- [ ] **Measure the real figure at M2** and replace the estimate here. The
      range above is from JPEG rules of thumb, not from these images.

### 5.6.4 Who counts as a legend — settled
**Decision 75 is settled: the list below ships.** Delegated to me by the owner,
so the selection criteria matter more than my taste: **every multiple World
Champion**, plus **single champions of lasting significance**, plus **great
drivers who never won a title**. Thirty-two rows.

| Group | `driverId`s |
|---|---|
| Multiple champions (17) | `fangio` · `ascari` · `brabham` · `clark` · `stewart` · `lauda` · `prost` · `senna` · `michael_schumacher` · `vettel` · `fittipaldi` · `piquet` · `hakkinen` · `graham_hill` · `alonso`\* · `hamilton`\* · `max_verstappen`\* |
| Single champions of note (10) | `hunt` · `mansell` · `rindt` · `jacques_villeneuve` · `damon_hill` · `hawthorn` · `surtees` · `rosberg` (Nico) · `raikkonen` · `button` |
| Never champion (5) | `moss` · `gilles_villeneuve` · `amon` · `ickx` · `peterson` |

\* currently racing — the runtime overlap rule (RACE-13d) shows these as driver
cards today and as legend cards once they retire. **32 rows.**

Two corrections to the draft list, recorded because both were real errors:
- **Jack Brabham appeared twice**, as `Brabham` and `Jack Brabham`. One row.
- **Räikkönen and Button were missing** from the single champions. Both are
  modern champions with long careers and large start counts, and leaving them
  out while including Hawthorn would have been indefensible.
- `barrichello` is dropped from "never champion": a fine career and the
  start-count record for years, but §5.6.4's bar is *great drivers who never
  won a title*, and he is a tier below the other five.

#### Overlap with current drivers — measured
Checked against the 2026 entry list: **three listed legends are still
racing** — `alonso`, `hamilton` and `max_verstappen`. That set changes on its
own as drivers retire, which is why the rule is evaluated at runtime.

- [ ] **Show each driver once.** A driver in both sets gets their
      **current-driver card**, not a second legend card, and that card carries
      a **`LEGEND` badge** beside the `DRIVER` type badge (§6.4, UI-20b) plus
      the full career record. One card, both facts.
- [ ] **Resolve the overlap at _runtime_ against the live entry list**
      (RACE-13d) — **not** at build time, which was decision 86's original
      wording and is wrong for a device that follows the season (§4.2.1.1).
      The legend record is **always compiled in**, including for drivers
      currently racing; which card renders is decided per card, per poll.
- [ ] **`max_verstappen` is therefore listed in the legends table too**, not
      omitted. He is a four-time champion on record; the runtime rule shows him
      as a driver card while he is racing and moves him to the legend rotation
      the day he stops, with no rebuild and no edit.
- [ ] **This cannot be left to drift**, and runtime resolution is what prevents
      it: there is no build step that can go stale, and a retirement is picked
      up on the next entry-list poll.
- [ ] Keep the table **editable, with the group recorded per row**, so the
      carousel can weight or filter by group later.
- [ ] **DATA-7 applies to most of this list** — no `code`, no
      `permanentNumber` for the pre-1980s drivers. **DATA-8 applies to all but
      five**: everyone except Hamilton, Vettel, Räikkönen, Button and
      Jacques Villeneuve began before 1994, so their pole counts are curated or
      omitted.

---

## 6. Display

### 6.1 Pages
The page model is `sky-tracker`'s (its UI-2): **a tap anywhere flips to the next
page**, pages that are not in the rotation carry `skip: true`, and the gear
takes its own taps via `on_short_click`.

| Page | In rotation | Shown when |
|---|---|---|
| `wifi_page` | no (`skip`) | first page, so it is what boots; returns after 10 s offline — **but never over the settings page** |
| `race_page` | yes | the primary page in `SESSION_*`, `RACE_LIVE`, `POST_SESSION` |
| `order_page` | yes | the full 22-driver order (§6.3) |
| `circuit_page` | yes | the primary page in `IDLE`, `RACE_WEEK`, `OFF_SEASON` (§6.4) |
| `standings_page` | yes | championship (§8.1) |
| `settings_page` | no (`skip`) | gear, lower right |
| `about_page` | no (`skip`) | **About** button, lower right of the settings page (UI-67) |
| `debug_page` | no (`skip`) | **long-press** the gear |

- [ ] **UI-2a: the rotation order changes with state.** The *first* page after
      a flip from `wifi_page` is whichever page the state machine says is
      primary. The pages themselves do not move; only which one the device
      rests on does.
- [x] **NET-2b / BOOT-2: the boot page** (decision 178), ported from
      `sky-tracker` 4.6.27 (its NET-2b, NET-2c and BOOT-2) and driven by
      `f1_boot.h`, which is pure and host-tested:
      - a centred 380 px column: the mark (200 px), **"F1 Tracker"**
        (`setup_title`), a status line and a body line (`setup_body`), firmware
        version lower right;
      - **"Loading"** from the first frame until start-up ends, then
        **"Connecting to Wi-Fi"** over the network being tried (NET-2c: the
        entry ESPHome selected — saved to flash by Improv — and
        `Waiting for Wi-Fi details` when there is none);
      - **fallback AP up:** `Set up Wi-Fi` over `Join 'F1 Tracker - XXXX'` /
        `from your phone`;
      - **BOOT-2:** the page is drawn with `lv_refr_now()` and the backlight is
        set at the start of the start-up lambda, because LVGL draws nothing until
        setup ends and the light writes its output only from `loop()` — without
        it the panel stays dark through the whole of start-up;
      - checked every 2 s: **on connect it fades to the primary page** the state
        machine names (race page around a session, otherwise the carousel —
        UI-2a), and **after 10 s without Wi-Fi it comes back, but never over the
        settings page**.
      Not ported: `sky-tracker`'s 30 s `ap_timeout` (decision 42 keeps ESPHome's
      default), its GPS/compass lines (no such hardware here), the launch
      animation over the boot screen, and UI-72's 60 s idle close of other pages,
      which is a settings behaviour rather than a boot one.
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
- [ ] **UI-10b: 22 rows fit at `mono12` with the TEAM column.** Measured, not
      estimated — see §6.3.1. The fallback of dropping TEAM is **not needed**.
- [ ] **UI-10c: the order page is the single source of truth for position.**
      `plane-tracker` decision 47 and 57 both came from two places counting the
      same thing and disagreeing. The race page's state line, the top-5 strip
      and this page must all read **one** ordered array, never the raw feed.
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
- [ ] ~~Highlight position changes with a green/red tint per poll~~ —
      **superseded** (decision 58): there is no live poll for it to tint. The
      grid → `FINAL` transition above carries the same information once, as
      places gained and lost against the grid (§8.1).
- [ ] A **retirement** keeps its row, greyed, with the status word (`DNF`,
      `ACCIDENT`, `+1 LAP`) rather than vanishing.

#### 6.3.1 The layout is measured, not estimated (answers open question 3)
`tools/mock_order_page.py` renders this page at 480×480 using **real Roboto
Mono metrics, the real 2026 entry list and the real OpenF1 team colours**, and
reports the fit. Output is committed under `docs/renders/`. Rerun with:

```
python3 tools/mock_order_page.py --font /path/to/RobotoMono.ttf
```

**Measured 2026-10-01:**

| | |
|---|---|
| Roboto Mono advance @ 12 px | **7.0 px/char** (monospace, so width is exactly `7 × chars`) |
| 12 px ascent / descent | 13 / 4 → **natural line height 17 px** |
| Longest surname | `VERSTAPPEN` — 10 ch, **70 px** |
| Longest team | `Red Bull Racing` — 15 ch, **105 px** |
| Widest row, all columns + bar + flag | **~322 px** of 480 — width was never the constraint |

| Variant | Row height | TEAM column | Last row ends | Verdict |
|---|---|---|---|---|
| a | 18 px | yes | y=456 | **fits**, 22 px slack |
| b | 19 px | yes | y=478 | fits, but flush to the edge |
| c | 18 px | **no** | y=456 | fits — the fallback is unnecessary |
| d | 17 px | yes | y=434 | fits; 17 px is the natural line, so no padding at all |
| **RECOMMENDED** | **18 px** | **yes** | y=472 | **fits**, and spends the slack on the state line |

- [x] **Decision 88: 22 rows at `mono12`, 18 px per row, TEAM column kept.**
      18 px gives 1 px of padding over the 17 px natural line, which the render
      shows is enough. 19 px puts P22 flush against the bottom edge and 17 px
      removes padding entirely.
- [ ] **Spend the 22 px of slack on the state line** (UI-3), which §6.2 calls
      the page's most important element. The recommended render promotes it to
      `mono15` on its own line with the countdown in `mono12` beneath, and
      still ends at y=472.
- [ ] **UI-10d: the GAP column is never empty except in `ENTRY LIST`** — a
      finding from the render, which initially showed race gaps against a
      provisional grid. In `GRID (PROVISIONAL)` and `GRID` it carries the
      **gap to pole**, computed from Jolpica's Q1/Q2/Q3 times, which are free
      and have no live window. In `FINAL` it carries the race gap. This is
      strictly better than the blank column decision 31 settled for, because
      the data was there all along; decision 31 was about *live* intervals.
- [ ] **The render confirms decision 35 was necessary**: `HÜLKENBERG` appears
      in the real entry list. `sky-tracker`'s ASCII glyph set would have drawn
      a box in the middle of a driver's name.
- [ ] **Still verify on hardware** (§14.1). LVGL's text metrics are not PIL's,
      and 1 px of padding is not much margin. But the layout question — *does
      it fit with TEAM, or must a column go* — is now answered, and the answer
      is that it fits.

### 6.4 The Carousel — the non-race-day page (UI-20)
The default screen for most of the year. **Three card types, one rotation:**
circuits, current drivers and legends.

- [ ] Rotate **one card at a time**, advancing on a timer. **Interval is a
      setting: 15–120 s, step 15, default 45 s** — the brief asked for 30 s to
      1 minute, and the range brackets it.
- [ ] **UI-20a: card types are interleaved deterministically, not shuffled.**
      With 40 circuits, 22 drivers and ~32 legends, a random draw clusters —
      three legends in a row, then eleven circuits. Walk the three lists in
      parallel on a fixed pattern (**circuit → driver → circuit → legend**,
      repeating), so each type appears at a predictable rhythm and every card
      is reached. Each list advances its own cursor, so the lists need not be
      the same length.
- [ ] **Setting: `Carousel content`** — `all` (default) / `circuits only` /
      `drivers only` / `legends only` / `circuits + drivers`. The brief asked
      for circuits *and* drivers *and* legends, so `all` ships as the default;
      the filters exist because a season tracker someone uses daily should be
      tunable.
- [ ] **Circuit card:** the **map** (large, §6.5), the **circuit name**, the
      **country flag** and country, the locality, and **the facts** (§5.5).
- [ ] **Driver card:** the **portrait** (§5.6.3), full name, **nationality
      flag**, car number, team, age from `dateOfBirth`, and the career record —
      starts, wins, poles, podiums, championships (§5.6.2). Plus the
      **current season**: championship position and points, which is the only
      part a live fetch improves (free, no window — Jolpica standings).
- [ ] **Legend card:** the **portrait**, full name, **nationality flag**, the
      **era** (`1984–1994`), the career record, championships with the years,
      and the **curated line on why they matter** (§5.6.2).
- [ ] **Legend cards must render with no car number and no acronym** — DATA-7,
      measured absent for historical drivers.
- [ ] **Omit the poles line entirely for pre-1994 drivers** unless a curated
      value exists (DATA-8). A blank is correct; a zero is a lie.
- [ ] **Photographer credit on every card carrying a photograph** (§5.6.3),
      small, bottom of the card.
- [ ] **UI-20b: the three card types must be instantly distinguishable** — a
      glance should never leave a reader unsure whether they are looking at a
      circuit or a person. Use a consistent **type badge** in the same corner
      on all three (`CIRCUIT` / `DRIVER` / `LEGEND`) rather than relying on
      layout alone, because a portrait and a map already differ so much that
      the *layout* carries no signal when the card changes.
- [ ] **Circuit order is calendar order**, and in `RACE_WEEK` the cycle
      **starts at the next round** so the upcoming circuit is the first thing
      seen. Driver order is championship order; legend order is the curated
      table's order.
- [ ] **UI-20c: in `RACE_WEEK`, bias the driver cards toward this weekend's
      entry list** and the circuit cards toward the next round. The carousel
      should feel like it knows what is coming up.
- [ ] **UI-20d: cross-link the watched driver into the circuit cards.** The
      facts table is generated offline (§5.5) and already aggregates per-circuit
      winners, so *"Verstappen has won here 3 times"* costs nothing and ties
      the two features together. Omit the line where he has never won there
      rather than printing a zero.
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
- [ ] Timezone: the owner's IANA zone (see `REQUIREMENTS.private.md`), handling DST automatically. ESPHome's
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
- [x] The Sun's elevation is **computed on-device from the latitude and longitude
      entities** and the clock - no network call, no extra configuration.
      `f1_sun.h` is `sky-tracker`'s `astro::sun` / `gmst_deg` / `horizontal`
      (Astronomical Almanac low-precision formulae, ~0.01 deg, equation of time
      included), stripped to the elevation alone (decision 179). The position is
      read from the entities on **every** pass, never cached (decision 180), and
      `Sun Elevation` is a diagnostic entity so a wrong position is a number, not
      a mysteriously dim screen.
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
- [ ] **This matters far more at high latitude than at mid-latitudes.** Day length at
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
      correct in UTC while the clock stays on the home time zone, so the display will
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
- [ ] **Night Mode is declined** (§6.13, decision 83). Auto-dim is the whole
      of this project's dusk behaviour: the panel gets dimmer, and it stays in
      colour.

### 6.9 Typography (UI-40)
- [ ] **UI-40d: sentence case** (decision 153). Capitals are reserved for driver
      surnames in the order list and standings, and for abbreviations.
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
- [x] **UI-40b: accented characters are guaranteed here**, unlike in either
      sibling. **Settled at M0: use `glyphsets: [GF_Latin_Core]`**, ESPHome's
      own mechanism (via `esphome-glyphsets`), rather than a hand-built literal
      string. 319 codepoints, declared once as a YAML anchor and reused across
      all eight faces.
      **Verified, not assumed:** every character in all **715 real strings**
      from `reference/samples/` and the 40 circuit traces resolves — **0
      missing**. The data really does contain `á ã é í ó ü`
      (`Hülkenberg`, `Autódromo José Carlos Pace`, `Nürburgring`,
      `Hermanos Rodríguez`), so `sky-tracker`'s ASCII set would have drawn a box
      mid-name.
- [ ] **UI-40c: Roboto Mono is missing 12 of `GF_Latin_Core`'s codepoints**, and
      the build warns about it. All twelve are **combining diacritical marks**
      (U+0302 circumflex, U+0304 macron, U+0306 breve, …), which no monospace
      face ships as standalone glyphs. **Harmless for our data**, which uses
      precomposed forms — but it names a real latent bug: if a source ever
      returns **decomposed (NFD)** text, `u` + combining diaeresis renders as a
      letter followed by a box instead of `ü`.
      **Normalise incoming strings to precomposed form at parse time.** Full
      Unicode NFC is far too heavy for this device; a small fold table covering
      the Latin-1 base+mark pairs is enough, since that is the whole range our
      content occupies. Assert it in the host tests with an NFD fixture.
- [x] **`tools/check_glyphs.py` is built and gates `deploy.sh`** (decision 140).
      It resolves each label's *effective* font — the gear is measured against
      `montserrat_28`, not against Roboto Mono — and covers the **generated**
      tables as well as the YAML and the C++ headers. 1,901 strings, 99 distinct
      characters, 0 missing.
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
- [ ] **Team colours survive intact**, because Night Mode is declined (§6.13).
      This is part of why it was declined: `plane-tracker` decision 56 found
      that multiplying red over a blue gives black, so a red palette would make
      half the grid's colour bars vanish. On a device whose job includes
      telling teams apart, that is a real loss rather than an acceptable
      trade.

### 6.11 Detail cards (UI-24)
- [ ] Tapping a driver row opens a card: full name, number, acronym, team,
      nationality + flag, grid position, finishing position and gap once the
      result exists, and status. **No live position and no tyre compound** —
      both are inside OpenF1's live window (decision 58); a post-session tyre
      strip is §8.1's job. Tapping the circuit map opens a card with the full
      facts set (§5.5) and the session times in all three zones (§6.7).
- [ ] Touch targets are **finger-sized** — a ~40×40 px minimum hit box, and
      **the whole row is the target**, not just the text. `plane-tracker`
      decision 53: to a finger the label is part of the thing, and it is the
      larger of the two.
- [ ] Clear dismissal: a tap anywhere closes the card (§6.1).
- [ ] **The portrait is the compiled-in Commons image** (§5.6.3), with its
      credit. **No runtime photo fetch, and never OpenF1's `headshot_url`**
      (decision 73). **Clear the image the instant the selection changes** so
      a previous driver's photo is never shown against new data
      (`plane-tracker` decision 45).
- [ ] The card must be fully useful with no photo at all (RACE-13e).
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

### 6.13 Night mode — declined (UI-45)
**Decision 83: the red night-vision palette is not ported.** Both siblings have
it; this project does not get it.

The reasoning, recorded because it is the kind of thing that gets re-proposed:
`sky-tracker` and `plane-tracker` are **instruments watched in the dark**, where
preserving night vision is the point. This is a **living-room object showing a
sport**, and at the deployment latitude a dusk-triggered red palette would hold the screen red
for up to **19 hours a day in December** (§6.8) — the device would be red far
more often than not, which is an aesthetic decision disguised as a feature.

- [ ] **Remove it from the settings page and the entity list.** Not shipped
      disabled — absent. A switch for a feature nobody will turn on is
      clutter, and §7's page is already long.
- [ ] **Auto-dim stays** (§6.8) and is the right answer to the same problem:
      the display gets dimmer after sunset, it just stays in colour. Team
      colours (§6.10) and both sets of flags (§6.6) are load-bearing on this
      device in a way they are not on a radar scope, and a red palette
      destroys all three.
- [ ] One consequence worth keeping: `plane-tracker` decision 61 found that a
      palette setter must apply **unconditionally**, because an early-out on an
      unchanged palette left widgets showing the colours written literally into
      the YAML. **That applies to any theming this project does**, dimming
      included — so the lesson is inherited even though the feature is not.
- [ ] Also inherited: team colours must **not** be the only signal (§6.10).
      That requirement stands on contrast grounds alone, with no palette
      switching involved.

---

### 6.14 The watched driver (UI-50)
The device watches one driver and says so. It ships watching **Max
Verstappen**, and the mechanism is generic so the name is a setting rather than
a constant.

#### 6.14.1 Identity — key on `driverId`, never the car number
**Measured 2026-10-01, and this is the trap:** Verstappen's 2026 row is
`driverId: "max_verstappen"`, `code: "VER"`, `permanentNumber: "3"`. He does
**not** hold `1` (Norris does, as reigning champion) and he does not hold his
permanent `33`.

- [ ] **The watched driver is stored as a `driverId`.** A number-keyed watch
      would have followed Norris in 2026 and someone else in 2027 — a bug that
      produces plausible, confidently wrong output (DATA-6).
- [ ] Resolve to a live `driver_number` only where an OpenF1 row must be
      matched, and do it through the acronym for that session.
- [ ] Default `max_verstappen`, settable from a `select`. **ESPHome `select`
      options are compile-time** (decision 109), so the list is generated from
      the **compiled driver table**, not the live entry list; a driver who
      joins after the build is selectable only after the next one.
- [ ] **DATA-10: the season driver list is not the race entry list.** Measured:
      Jolpica's 2026 `drivers` endpoint returns **32 rows**, of which only
      **23 carry a `code` and a `permanentNumber`** — the other 9 are reserve
      and rookie entries with no number, no acronym and no nationality
      (DATA-7). A `select` built from the season pool would offer nine drivers
      who are not racing this weekend.
      **Build the entry list from the round's own data** — qualifying
      classification, or OpenF1's per-session `drivers` once the window has
      closed — and fall back to "season rows that carry a `code`" before the
      first session of a year. The same rule applies to the compiled table
      the `select` is generated from.

#### 6.14.2 What "driving" means on a free tier
The device cannot see cars on track (§3.6.1). It **can** know, to the second,
that a session is under way and that the watched driver is entered in it —
both from the calendar and the entry list, neither of which has a live window.
That is enough for a truthful alert, and it is the whole reason this feature
works at all under decision 58.

| Trigger | Alert | Source |
|---|---|---|
| Race day, he is entered | `MAX IS RACING TODAY` | calendar + entry list |
| Any session window open | `MAX IS ON TRACK — QUALIFYING` | calendar |
| Grid known | `MAX STARTS P3` | Jolpica qualifying |
| Window closed, result in | `MAX WINS` / `MAX FINISHES P4` / `MAX RETIRES — GEARBOX` | Jolpica results |
| Career milestone | `MAX — 72ND WIN` / `250TH START` | generated totals + this result |
| Championship | `MAX IS WORLD CHAMPION` | standings |

- [ ] **UI-50a: never say he is on track when only the clock says so and the
      session was cancelled.** `is_cancelled` is in the OpenF1 session data
      (§3.3) and a cancelled session must suppress the alert.
- [ ] **A withdrawal is not a result.** If he is in the entry list but absent
      from the classification, say nothing rather than inventing a DNF.

#### 6.14.3 Alert design — the rules matter more than the list
`plane-tracker`'s §5.13 established the house rules for anything that
interrupts, and they apply directly:

- [ ] **Never cover the instrument.** The alert is a **strip**, not a modal,
      and **nothing ever needs dismissing**. The circuit map, the order list and
      the carousel keep working underneath.
- [ ] **Rare beats frequent.** An alert on every poll becomes wallpaper within
      a week. Three tiers, with hard rules:

  | Tier | Fires | Behaviour |
  |---|---|---|
  | **Ambient** | whenever he is in the current context | a persistent marker — his flag and `VER` in the header, his row highlighted on the order page. **Not an alert at all** |
  | **Event** | session start, grid set, result | a **full-width banner** across the upper third, in his team's colour, with his flag at card size and the message in `mono24`. Holds **~30 s**, then collapses into the ambient marker |
  | **Milestone** | a win, a round-number win/start/pole, a title | a **full-screen takeover** for ~8 s — flag, portrait, the number — then it collapses to the banner, which is **held until the next session** |

- [ ] **UI-50c: loud is safe on this device, and that is not a general
      licence.** `plane-tracker`'s rule is never to cover the instrument,
      because on a radar scope the aircraft are live and covering them loses
      information. **Here nothing underneath is changing** — the grid is
      static between sessions and there is no live timing at all (decision 58)
      — so a banner or a brief takeover costs the reader nothing. The tier
      above is deliberately louder than the sibling projects would allow, and
      the reason it is allowed is specific to this device.
- [ ] **Nothing ever requires dismissing**, loud or not. Every tier
      self-retires on a timer, and **a tap dismisses early** rather than being
      the only way out. A banner that needs acknowledging is a different and
      much worse thing than a banner that is simply large.
- [ ] **The takeover is milestone-only**, and milestones are rare by
      construction — a win, a round number, a title. If it ever fires twice in
      a weekend, the latch (below) is broken.

- [ ] **Each event fires once.** Latch it against a `(round, session, event)`
      key held in a `restore_value` global so a reboot mid-weekend does not
      replay the whole set. This is the most likely bug in the feature.
- [ ] **Keep the alert text in a data file**, not scattered through the code, so
      lines can be added over seasons and pushed by OTA. Same rule
      `plane-tracker` records for its own quiet lines.
- [ ] **UI-50b: the ambient marker is the feature most of the time**, and it is
      the one worth polishing. His row highlighted on the order page and his
      flag in the header is what a glance actually wants; the strip is for the
      handful of moments that deserve one.
- [ ] **Promoted from §8.2.** This was listed as the optional "favourite
      driver" idea. It is now a requirement, and the §8.2 entry is retired.
- [ ] A **`Watched driver alerts` checkbox**, default on, and a separate
      **`Milestone alerts only`** option for someone who wants the rare tier
      without the per-session one. Offering the choice costs one switch.
- [ ] **`Alert style`: `loud` (default) / `quiet`.** `quiet` demotes the event
      banner to a one-line strip and the milestone takeover to a banner. The
      loud design ships as the default because it was asked for; the quiet
      path exists because a device in a living room may be watched by someone
      who did not ask for it.

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
| Auto off overnight | checkbox | blank the panel between two hours (UI-44c) |
| Auto off from / until | entry | local hours, default 23 → 07 |
| Brightness | slider | 1–100 % (the **daytime** level while auto is on) |
| Clock format | dropdown | 24 h local / 12 h local / UTC-Zulu |
| Carousel interval | slider | 15–120 s, step 15, default **45** |
| Carousel order | dropdown | calendar / random / current season only |
| Carousel content | dropdown | all (default) / circuits / drivers / legends / circuits + drivers (§6.4) |
| Watched driver | dropdown | from the compiled driver table (decision 109), default **Max Verstappen** (§6.14) |
| Watched driver alerts | checkbox | default **on** |
| Milestone alerts only | checkbox | default off — the rare tier without the per-session one |
| Alert style | dropdown | **loud** (default) / quiet (§6.14.3) |
| Show practice sessions | checkbox | treat FP as a session worth a page |
| Show sprint sessions | checkbox | — |
| Order columns | dropdown | with team / with gap / with tyre |
| Force page | dropdown | Auto / Race Day / Carousel (**not persisted**, §4.1) |
| About | button | lower right of the settings page; opens the About page (UI-67, decision 181) |

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
- [x] Group the page into **tabs** as `sky-tracker` does (its UI-16a) — three
      panels, one shown at a time: **Display**, **Race**, **Location**.
      Built (decision 170). Only the settings that were already on the panel
      plus latitude, longitude and the Auto Off hours are there; the dropdown
      settings (clock format, carousel order and content, watched driver,
      alert style, order columns, favourite team, force page) remain on the
      web UI and Home Assistant.

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
- [x] Give every entity an `icon:` — the five diagnostics that lacked one (IP
      Address, Connected SSID, ESPHome Version, WiFi Signal, Uptime) now have it
      (decision 182).
- [ ] **Set web UI authentication.** The page exposes control of every setting.
- [ ] Prefer the HA **native API** over MQTT.

#### 7.1.1 Entity mapping
| Setting | ESPHome entity |
|---|---|
| Latitude, Longitude | two `number` (box mode, step 0.001) |
| Auto-dim display | `switch` |
| Auto off overnight | `switch` (UI-44c) |
| Auto off from / until | two `number` (local hours) |
| Brightness | `number` (1–100 %, slider) |
| Clock format | `select` (24 h / 12 h / Zulu) |
| Carousel interval | `number` (15–120, step 15, slider) |
| Carousel order | `select` |
| Carousel content | `select` |
| Watched driver | `select` (options generated from the compiled driver table — decision 109) |
| Watched driver alerts | `switch` |
| Milestone alerts only | `switch` |
| Alert style | `select` |
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
- [ ] **Watched driver** — resolved `driverId`, whether he is in the current
      entry list, and which alerts have latched this weekend (§6.14.3). The
      latch set is the first thing to look at when an alert does or does not
      fire.
- [ ] **Carousel** — current card type and index, and the counts of each type.
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
- [ ] ~~**Favourite driver / team.**~~ **Promoted to a requirement** — §6.14,
      the watched driver, shipping as Max Verstappen. A **favourite *team***
      remains open as a smaller version of the same idea: tint the two rows and
      show the constructors' position in the header.
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
- [x] **Flash budget — now measured, not estimated.** M0 compiled on
      2026-10-01 (ESPHome 2026.9.1), which settles the numbers this section
      previously guessed at:

  | | Measured |
  |---|---|
  | **App slot (default table)** | **8,126,464 B — 7.75 MB** (the top of the 6.5–7.8 MB estimate) |
  | **M0 firmware image** | **1,589,643 B — 1.52 MB, 19.6 % of a slot** |
  | **Fonts + glyph data** | **212.5 KB** — see below |
  | `web_server` bundled UI (`local: true`) | 25.5 KB |

  **The font estimate was wrong and is corrected.** This section said ~80 KB;
  the real figure for eight faces is **212.5 KB**, because the earlier number
  counted only the five `mono` sizes and ignored `setup_title` (34 px),
  `setup_body` (22 px) and the MDI icon face — the two large Roboto faces alone
  are 85 KB of it. Still immaterial against a 7.75 MB slot, but recorded
  because an estimate that is 2.6× low is worth knowing about.

  Projected total with everything from §5–§6 added:

  | Asset | Size |
  |---|---|
  | M0 image (firmware, LVGL, TLS, fonts, web UI) | **1.52 MB (measured)** |
  | Circuit traces, all 40 | 19.2 KB (measured) |
  | Flags, ~82 countries at two sizes | ~150 KB |
  | Compiled-in calendar + circuit facts + profiles | ~58 KB |
  | **Portraits, 54 at 240×320** | **1.4–2.2 MB** (§5.6.3) |
  | **Projected total** | **~3.2 MB — 41 % of one 7.75 MB slot** |

- [ ] **The portraits remain the dominant *variable* asset** at 21.8 % of a
      slot. 41 % projected leaves comfortable headroom, so decision 74's 3 MB
      generator cap stands with room to spare.
- [x] **RAM at M0: 119,475 of 341,760 B — 35.0 %**, with no data layer, no
      track store and no TLS sessions yet. That is the figure to watch as M3
      and M4 land; §2.4.3's note about logging where the LVGL buffer landed
      matters more now that a third of the budget is already spoken for.
- [ ] A portrait decode is one JPEG per card, once per 15–120 s. Free in time.
      But **decode into a single reused PSRAM buffer** (240×320 RGB565 =
      150 KB), never a fresh allocation per card — 54 cards cycling for weeks
      is exactly where a slow leak shows up, and this device is expected to run
      for a season.
- [ ] **RAM is the real budget**, as in both siblings: 460 KB framebuffer +
      57.6 KB LVGL buffer + TLS buffers for **two** hosts + `web_server`.
      Measure free internal RAM and PSRAM with everything enabled, and log it
      at boot.
- [ ] **JSON parsing is the main unknown** — a 48 KB response with ~20 fields
      per row. Measure early, prefer selective parsing, and size the buffer for
      48 KB rather than the common case.
- [ ] **Network receive buffers are PSRAM, not internal RAM** (§2.4.8): a
      64 KB window per socket, so budget **128 KB** across the two hosts. The
      capacity is nothing against 8 MB; the **bandwidth** shares a bus with the
      panel, which is the thing to watch.
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
- [x] **NET-10: the web page's tab icon and header badge** are the project mark
      (decision 149), ported from `sky-tracker`'s `sky_web.h`.
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
        including a race at `04:00Z` read from a far-away time zone — the bug §4.1 warns about;
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
| 14 | Poll intervals are **a table keyed on state**, not a fixed timer; ~~only `RACE_LIVE` (5 s) is demanding~~ | 2026-10-01 | active; **the 5 s `RACE_LIVE` poll is superseded by 58/59** — see 66 |
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
| 67 | **All licensing terms accepted by the owner** (§3.8) — personal, non-commercial, single device. Every obligation is a checkbox elsewhere in this document rather than a general assurance | 2026-10-01 | **decided by owner** |
| 68 | **The carousel carries three card types**: circuits, current drivers and legends, interleaved deterministically (circuit → driver → circuit → legend), filterable by a `Carousel content` setting, default `all` | 2026-10-01 | active |
| 69 | **Driver and legend profiles are generated offline and compiled in** — career records, portraits and prose. The device fetches no profile data at runtime, so the carousel works with no network | 2026-10-01 | active |
| 70 | **Career counts come from `MRData.total` with `limit=1`** — a career win total in a tiny response. The technique that makes §5.6.2 cheap | 2026-10-01 | active |
| 71 | **DATA-8: API pole counts are hand-curated for pre-1994 drivers, or omitted.** Measured: Prost 0, Fangio 0, Clark 0, Senna 3 against actuals of 33/29/33/65. The generator **refuses** to emit an API pole count for a pre-1994 career — a build guard, because a blank is correct and a zero is a lie | 2026-10-01 | active |
| 72 | **Portraits come from Wikimedia Commons, baked in, with the photographer credited on the card.** The generator records licence and artist and **fails the build on an unattributable image** | 2026-10-01 | active |
| 73 | **OpenF1's `headshot_url` is not used.** It points at F1's own media CDN with no public licence, via a fallback transform that may not resolve. Commons has terms we can actually comply with | 2026-10-01 | active |
| 74 | **Portraits are the dominant flash asset** (0.8–1.4 MB vs ~310 KB for everything else). The generator reports the total and fails above ~~2 MB~~ **3 MB** (`CAP_BYTES` in `gen_portraits.py`; §5.6.3) | 2026-10-01 | active, cap raised with 91 |
| 75 | **The legends list is taste, not data** — proposed in §5.6.4 for the owner to edit, with the selection criteria recorded so additions stay consistent | 2026-10-01 | **needs owner input** |
| 76 | **One driver is watched; the device ships watching Max Verstappen**, and the watched driver is a setting so it is a mechanism rather than a constant | 2026-10-01 | **decided by owner** |
| 77 | **DATA-6: the watched driver is keyed on `driverId`, never the car number.** Measured: Norris holds `1` in 2026 and Verstappen holds `3`, not `1` and not his permanent `33`. A number-keyed watch would silently follow whoever holds the number next season | 2026-10-01 | active |
| 78 | **"Driving" is derived from the calendar and the entry list**, not from live track data — both are free of OpenF1's live window, which is what makes the alert work at all under 58 | 2026-10-01 | active |
| 79 | **Alerts are a strip, never a modal, and nothing ever needs dismissing.** Three tiers: ambient marker (always), event (~20 s, then collapses), milestone (held, distinct colour). `plane-tracker`'s §5.13 rules | 2026-10-01 | **timings superseded by 92**: event banner ~30 s, milestone takeover ~8 s (§6.14.3, `f1_watch.h`) |
| 80 | **Each alert event latches once** against a `(round, session, event)` key in a `restore_value` global, so a mid-weekend reboot does not replay the set. The most likely bug in the feature | 2026-10-01 | active |
| 81 | **The ambient marker is the feature most of the time** — his flag in the header and his row highlighted — and is the part worth polishing. The strip is for the few moments that deserve one | 2026-10-01 | active |
| 82 | A **cancelled session suppresses the "on track" alert** (`is_cancelled`), and a **withdrawal is never reported as a DNF** | 2026-10-01 | active |
| 83 | **Red night mode is declined** — not ported, not shipped disabled, absent. Both siblings are instruments watched in the dark; this is a living-room object showing a sport, and at the deployment latitude a dusk trigger would hold the screen red up to 19 h a day in December. Auto-dim (§6.8) answers the same problem without destroying team colours and flags | 2026-10-01 | **decided by owner**, closes open question 2 |
| 84 | **No hardware yet.** Work stops at `esphome compile`; §14.1 collects every hardware-gated item. The generators, data layer, state machine and test suite are all host work and carry most of the project's risk, so this costs little | 2026-10-01 | **decided by owner** |
| 85 | **The legends list is settled at 31 rows** — 32 after 96 adds `max_verstappen` (§5.6.4), delegated by the owner. Two errors in the draft fixed: Jack Brabham was duplicated, and Räikkönen and Button were missing. Barrichello dropped on the stated bar | 2026-10-01 | settles 75 |
| 86 | **A driver in both sets gets one card** — their current-driver card with a `LEGEND` badge. Measured overlap: Alonso, Hamilton, Verstappen | 2026-10-01 | **corrected by 96** — the resolution is at runtime, not build time |
| 87 | **DATA-10: the season driver list is not the race entry list.** Measured: 32 rows for 2026, only 23 with a `code` and number; the rest are reserves. Build the watched-driver `select` from the round's own data, not the season pool | 2026-10-01 | active |
| 88 | **22 order rows fit at `mono12`, 18 px per row, TEAM column kept** — measured by rendering the real layout with real font metrics and the real entry list (§6.3.1). The fallback of dropping TEAM is unnecessary. 19 px puts P22 flush to the edge; 17 px removes padding entirely | 2026-10-01 | **answers open question 3** |
| 89 | ~~Target season is 2027~~ | 2026-10-01 | **superseded by 94** |
| 90 | **A Sprint is a first-class race day** (RACE-14) — its own grid, result and race page, labelled `SPRINT`. A sprint weekend has two race days. Sprint wins must **not** count toward career win milestones; Jolpica keeps them in a separate endpoint | 2026-10-01 | **decided by owner**, closes open question 6 |
| 91 | **Portraits are 240×320**, not 150×200. The brief asked to show the picture; at 480 px wide, 150×200 reads as a thumbnail. Budget restated against the **app slot** (~6.5–7.8 MB), not total flash: portraits are 25–30 % of one slot | 2026-10-01 | **delegated**, answers open question 13 |
| 92 | **Alerts are loud** (§6.14.3): full-width banner for events, brief full-screen takeover for milestones. Safe here specifically because **nothing underneath is changing** — no live timing (58) — which is not a general licence. Nothing ever requires dismissing; a tap only dismisses early. An `Alert style: loud/quiet` setting ships with loud as the default | 2026-10-01 | **decided by owner**, closes open question 12 |
| 93 | **UI-10d: the GAP column carries the gap to pole in grid modes**, from Jolpica's Q1/Q2/Q3 times — free, no live window. Found by rendering §6.3.1, which showed race gaps against a provisional grid. Decision 31's blank column was about *live* intervals only | 2026-10-01 | refines 31 |
| 94 | **The season is resolved at runtime from `/current/`, never fixed.** No season year appears in the firmware, YAML or generated headers as a target. This is a device meant to sit on a wall for years; a fixed season means a rebuild every winter and a stale year if nobody does one | 2026-10-01 | **decided by owner**, supersedes 89 |
| 95 | **An empty `/current/next/` is the end-of-season signal, not an error**, and the months-long gap until the next calendar is published is `OFF_SEASON` — a first-class screen showing the champion, the final standings and the carousel, never a "no data" state | 2026-10-01 | active |
| 96 | **RACE-13d: the legend/current-driver overlap is resolved at _runtime_**, against the live entry list. **Corrects decision 86.** Baked overlap + a runtime season rollover would make a newly retired driver vanish from both rotations. `max_verstappen` is therefore listed in the legends table (32 rows), not omitted | 2026-10-01 | **corrects 86** |
| 97 | **RACE-13e: a driver with no compiled profile still gets a text-only card** from the live entry list. A rookie who joins after the build has no portrait and no career record; render what is known rather than skipping them. **Portraits are the one thing a rebuild is genuinely needed for**, and the README says so | 2026-10-01 | active |
| 98 | **A season rollover must need no firmware update**, and that is a host test: advance `current` by a year mid-run and assert the calendar, entry list, driver carousel and watched-driver resolution all follow | 2026-10-01 | active |
| 99 | **NET-15: TCP receive window raised to 65,535 and the `esp_http_client` buffer to 4 KB.** Sized so **every** runtime response (largest 47.0 KB) fits **one window** — at `sky-tracker`'s 32 KB the biggest needs two, stalling a full round-trip mid-transfer. 48 KB worst case: 250 ms → 75 ms | 2026-10-01 | active |
| 100 | **65,535 is deliberately the ceiling** — the largest unscaled TCP window, so `CONFIG_LWIP_WND_SCALE` is not needed. 128 KB would save a further 37 ms on a rarely-fetched response and add an option to get wrong | 2026-10-01 | active |
| 101 | **`RECVMBOX_SIZE` must scale with the window** (≥ window/MSS ≈ 46, so 64). Leaving it at 32 would throttle the larger window back to roughly what it replaced — the quiet way this change does nothing | 2026-10-01 | active |
| 102 | **The `esp_http_client` receive buffer default is 512 B**, so a 47 KB response is ~94 read calls each resuming the parser. Raised to 4 KB — cheaper than the window change and easier to overlook | 2026-10-01 | active |
| 103 | **TLS session reuse (65) dwarfs this.** A fresh handshake is ~1.2 s against the ~175 ms NET-15 saves. Recorded so the buffer change is understood as a tail-latency refinement on a rare large fetch, not something that makes the device feel faster | 2026-10-01 | refines 65 |
| 104 | **M0 is built.** ESPHome 2026.9.1 in `.venv`, config validates, firmware compiles clean. App slot **7.75 MB**, image **1.52 MB (19.6 %)**, RAM **35.0 %** | 2026-10-01 | **done** |
| 105 | **`glyphsets: [GF_Latin_Core]`**, ESPHome's own mechanism, not a hand-built literal string. Verified against all **715 real strings** in the fixtures and traces: **0 missing** | 2026-10-01 | settles 35 |
| 106 | **UI-40c: normalise incoming text to precomposed form at parse time.** Roboto Mono lacks all 12 of `GF_Latin_Core`'s combining marks, so decomposed (NFD) input would render `u` + a box instead of `ü`. A small Latin-1 fold table, not full NFC | 2026-10-01 | active |
| 107 | **The §9 font estimate was 2.6× low** — ~80 KB against a measured **212.5 KB** — because it counted only the five `mono` sizes and ignored the two large Roboto faces and the MDI icon face. Immaterial against the slot; recorded because the error is worth knowing | 2026-10-01 | corrects 9 |
| 108 | **`deploy.sh` ships at M0, not M7.** `plane-tracker` decision 63's gate caught three `-Wformat` warnings in our own lambda on the **first** build — exactly the class of thing it exists for, found immediately | 2026-10-01 | implements `plane-tracker` 63 (§10) |
| 109 | **ESPHome `select` options are compile-time**, so the watched-driver list (§6.14.1) **cannot** be populated from a live entry list. It is generated from the **compiled driver table** at M2 instead, which means a driver who joins after the build is not selectable until the next one — consistent with RACE-13e | 2026-10-01 | constrains 87 |
| 110 | **`web_server` OTA is declared explicitly** rather than left implicit, so its plaintext `/update` endpoint is a deliberate choice; `auth: type: digest` gates it, ahead of ESPHome's 2027.1.0 default flip | 2026-10-01 | active |
| 111 | **The GPIO19/20 USB-Serial-JTAG build warning is expected** and is direct confirmation of decision 5: the GT911 owns GPIO19, so the S3's default console would fight it. GPIO45 is a strapping pin, known-good because `sky-tracker` drives this panel on these exact pins in production | 2026-10-01 | confirms 5 |
| 112 | **M1 is built.** Both generators, the carousel, the LVGL trace drawing, and **1823 host tests, 0 failures**. Image 1.52 → **1.55 MB (20.0 %)**, RAM 35.0 → **35.6 %**. The 40 traces cost exactly the predicted **19,176 B** | 2026-10-01 | **done** |
| 113 | **MAP-2a: known-distinct neighbouring venues are excluded by name, not by threshold.** `zeltweg` false-matched the Red Bull Ring at 2.4 km while `kyalami` is a true match at 1.3 km, so no distance cut separates them. The generator carries an `EXCLUDE` set with a reason per row | 2026-10-01 | refines 23 |
| 114 | **No circuit is north-up.** Closest is Istanbul at 4.75°, spread to 168.5° (Monaco), so MAP-3's north arrow is **permanent furniture**, not an occasional cue, and should be designed into the card. A 2° snap is applied defensively | 2026-10-01 | refines 24 |
| 115 | **The GeoJSON traces do not repeat the first point**, so the device must close the lap explicitly or every circuit shows a visible gap at the start/finish line. `closed` is generated per trace; all 40 are closed laps | 2026-10-01 | active |
| 116 | **LVGL's line widget is compiled out unless a `line:` appears in the YAML**, so `lv_line_create` does not link. The trace, tick and north arrow are **declared in YAML** and C++ only swaps their point arrays — better practice anyway: LVGL owns parentage and styling, we own geometry | 2026-10-01 | active |
| 117 | **`tools/preview_circuits.py` renders the GENERATED header**, not the source GeoJSON, through the same `fit_box()` arithmetic the device uses. It verifies MAP-3/MAP-5 without hardware: a wrong rotation, a squashed aspect or a trace escaping its box is visible immediately. All 40 confirmed | 2026-10-01 | active |
| 118 | **M2 is built.** 23 driver and 32 legend profiles, 48 portraits (0.98 MB), 48 flags (28.1 kB), 41 circuits of facts. Image **2.71 MB — 33.4 % of the slot**, RAM 36.3 % | 2026-10-01 | **done** |
| 119 | **Cards are prepare-then-commit.** The next card is fully resolved into a staging buffer during the current card's dwell — profile row, flag, portrait, composed text — and the switch is only widget updates. If it is not ready the current card is **held**: a card that is late beats a card that is empty. The portrait and its credit are set together so a photo can never appear uncredited | 2026-10-01 | **asked for by owner** |
| 120 | **DATA-11: fastest-lap data begins in 2004**, the same shape of gap as DATA-8's 1994 for qualifying. Monza has raced since 1950 but returns 25 fastest laps. The card therefore says **"fastest lap … (since 2004)"**, never "lap record", which would be wrong for every circuit older than that | 2026-10-01 | active |
| 121 | **Circuit cards carry fastest lap, most recent winner with the event name, most wins and races held** — all computed offline from Jolpica and baked in (decision 27). 35 of 41 circuits have a fastest lap | 2026-10-01 | **asked for by owner** |
| 122 | **Ergast driverIds are not `firstname_surname` by rule.** The bare surname belongs to whoever the dataset assigned it to, and it is **not** the earlier driver: Graham Hill is `hill` while Damon is `damon_hill`; Jacques Villeneuve is `villeneuve` while his father Gilles, racing 18 years earlier, is `gilles_villeneuve`. The generator now fails with the real lookup URL rather than an IndexError | 2026-10-01 | active |
| 123 | **Wikimedia serves a per-file list of thumbnail widths.** Constructing a `/thumb/` URL by hand earns HTTP 400 — 640, 320, 800 and 1024 were all refused for a file that served 250. Ask the API for a `thumburl` via `iiurlwidth`, which is guaranteed servable. Fetching full-resolution originals earns 429, and the 429 body names this as the fix | 2026-10-01 | active |
| 124 | **Commons normalises `File:A_B.jpg` to spaces**, so a key taken from the image URL never matches one taken from the API response. This silently discarded 47 of 48 portraits as unattributable — a lookup that fails closed looks exactly like a licence problem | 2026-10-01 | active |
| 125 | **OGL 3 is a free, attribution-requiring licence** and belongs in the accepted set. Rejecting it was the regex being wrong, not the image being unusable | 2026-10-01 | refines 72 |
| 126 | **`3.7.4` applies to every service, not only ours.** Both the Jolpica client and the portrait fetcher back off on 429 and treat other 4xx as fatal for that request shape. My first Jolpica client treated all 4xx as fatal including 429, which is precisely the distinction 3.7.4 exists to draw | 2026-10-01 | implements §3.7.4, 57 |
| 127 | **The latch word must be 64 bits.** 7 sessions × 6 events reaches index 42, and a 32-bit word silently dropped everything from 32 up — the whole RACE session — so race alerts never latched and fired every tick. A `static_assert` now fails the build if a session type is added | 2026-10-01 | implements 80 |
| 128 | **The fetch task is double-buffered and pinned to core 1.** It parses into a back store and swaps under a mutex, so the UI thread never reads a torn parse and the lock is held for a `memcpy`, never for drawing | 2026-10-01 | implements 9 |
| 129 | **`Entry` lives in `f1_store.h`, not `f1_order.h`.** The parsers and their tests must build on the host, and `f1_order.h` needs LVGL. Pure data belongs on the testable side of that line | 2026-10-01 | active |
| 130 | **`-Wformat-truncation` caught a real truncation**: a 24-byte race status written into the 12-byte gap field. Now truncated **explicitly** with a precision specifier, so the cut is intentional rather than silent. The third time `plane-tracker` decision 63's gate has paid for itself | 2026-10-01 | implements `plane-tracker` 63 (§10) |
| 131 | **`#` is not a comment inside a C++ lambda in YAML**, and `time` is ambiguous against ESPHome's `time::` namespace — `::time(nullptr)` is required | 2026-10-01 | active |
| 132 | **Legend prose states facts, not adjectives** (5.6.2): records and circumstances, because the numbers are already on the card and a superlative adds nothing a reader cannot see. A driver who is also a legend carries the line on their driver card — one card, both facts | 2026-10-01 | implements 86 |
| 133 | **The post-session summary needs no OpenF1 at all.** Found at implementation: Jolpica carries **both** the fastest lap (`/last/fastest/1/results/`, 1.2 kB) and the pit stops (`/last/pitstops/`), neither of which has a live window. So the page appears as soon as results publish rather than waiting out OpenF1's +30 min, and §8.1's strongest free-tier feature turns out to be cheaper than planned | 2026-10-01 | refines 64 |
| 134 | **Only tyre compounds, flags and weather would still need OpenF1**, and all three are optional garnish on a summary that already carries the fastest lap, the pit stops and the podium | 2026-10-01 | active |
| 135 | **The detail card closes on a tap anywhere, including the gear** (§6.1). Every page's `on_click` and the gear's `on_short_click` consult one `tap_consumed()` helper, so there is a single place that decides, rather than the rule being re-implemented per page | 2026-10-01 | implements 6.1 |
| 136 | **Row taps are wired by a function pointer supplied by the app**, not by `f1_order.h` reaching upward. The order page stays free of everything above it, which is what keeps it host-compilable alongside the store | 2026-10-01 | active |
| 137 | **`tools/render_pages.py` renders every page from the GENERATED headers**, not from mock text — the same bytes the firmware carries, with the same geometry and the same Roboto Mono metrics. It is a check on the design, not a drawing of it | 2026-10-01 | active |
| 138 | **The renders caught two real gaps on the race page**: the circuit country flag and the top-5 strip (decision 49) were both specified and neither was built. The flag also collided with the clock in the top-right corner. All three fixed. This is the second time rendering the real layout has found something a reading of the document did not | 2026-10-01 | active |
| 139 | **The OTA panel names the phase it is in**: `UPLOADING` while the bytes arrive, `UPGRADING` once they are being applied, `UPLOAD FAILED` on error. Every phase previously said `UPGRADING`, which is wrong for the part that takes longest and is the part a watcher is waiting on | 2026-10-01 | **asked for by owner** |
| 140 | **`tools/check_glyphs.py` is built and is a `deploy.sh` gate** (`plane-tracker` decision 52). 1,901 strings from the YAML, the C++ headers and the **generated tables** — the last being where the risk lives, because nobody reads those files. A font's effective set is what it requests **intersected with what the typeface provides** | 2026-10-01 | implements 36 |
| 141 | **The checker's first version made the very mistake `plane-tracker` decision 52 warns about**, in the opposite direction: it measured every string against one shared set and flagged the gear symbol, which `montserrat_28` draws perfectly well. It now resolves each label's **effective** font | 2026-10-01 | active |
| 142 | **On a driver's birthday their card is badged in gold and injected every 10th card.** The priority slot does not consume a cursor, so nothing is starved and the rotation resumes where it was; a birthday never overrides a content filter that excludes drivers | 2026-10-01 | **asked for by owner**, rate revised by 145 |
| 143 | **Birthdays are scoped to CURRENT DRIVERS.** Legends carry a date of birth but **no date of death**, so the device cannot tell a living driver's birthday from the anniversary of someone long dead — `BIRTHDAY` over Ayrton Senna would be the worst thing it could display. `LEGENDS_INCLUDED` is named so the reasoning is in the code, not only here | 2026-10-01 | active |
| 144 | **A 29 February birthday falls back to the 28th** in a non-leap year, rather than being skipped three years in four | 2026-10-01 | active |
| 145 | **The birthday rate was wrong twice, in both directions, and settled at every 10th card.** The rotation is circuit→driver→circuit→legend, so with 23 drivers any one appears naturally every 92 cards — about hourly. Every 4th was **23×** natural (480/day): wallpaper, which `plane-tracker`'s §5.13 explicitly warns against. Every 20th was **4.6×** (96/day): too rare for something that lasts one day, and easy to miss entirely on a device glanced at occasionally. **Every 10th** is **9.2×**, one per eight minutes, 192/day — met on most visits without dominating any. The arithmetic lives in the header because the intuition is bad in both directions | 2026-10-01 | **corrected twice by owner** |
| 146 | **The project mark is the Monza trace from `f1_circuits.h`** — the same `int16` geometry the device draws — on a dark disc, with the orange start/finish tick the circuit cards use. Generated by `tools/gen_logo.py` rather than drawn, so it is a picture of what the device does, uses **no F1 or team trademark** (decision 44), and stays in step if the traces are regenerated | 2026-10-01 | **asked for by owner** |
| 147 | **Monza because it survives icon size.** Two long straights and a distinctive kink stay legible at 32 px where most of the 40 traces become a blob. Checked at 16/32/48/64 before choosing | 2026-10-01 | active |
| 148 | **The mark's canvas is pure black**, so the square is invisible against the device's black page and the disc is the whole mark. An alpha variant exists for any other ground | 2026-10-01 | active |
| 149 | **NET-10: `f1_web.h` replaces the web page's tab icon and header badge**, ported from `sky-tracker` 4.5.34. ESPHome's bundled v3 page ships an empty icon and offers no option to change it, so `/` is answered first: `INDEX_GZ` is inflated into PSRAM, both marks are swapped in, and the result is served uncompressed. The page is otherwise untouched, so an ESPHome update brings its new UI along, and **anything unexpected leaves ESPHome's own handler to serve as before** — a missing tab icon is not worth a failed boot | 2026-10-01 | active |
| 150 | **The web marks are SVG, the boot mark is RGB565.** Vector for the browser, where it scales to any tab or bookmark size; a 200×200 bitmap for the panel, which costs 80 kB of flash and needs no rasteriser | 2026-10-01 | active |
| 151 | **No panel behind the circuit trace.** The map container is transparent on both the race page and the circuit card, so the trace sits on the page's own black. The filled panel added a visible edge and no information — it was inherited from `sky-tracker`'s sky disc, where the fill *is* the subject | 2026-10-01 | **corrected by owner** |
| 152 | **The photo credit and the attribution are STACKED, not sharing a line.** Both belong bottom-left and the gear owns bottom-right, so one line cannot hold both: they overlapped on every profile card. Credit above, attribution on the bottom line | 2026-10-01 | **corrected by owner** |
| 153 | **Sentence case, not capitals.** Shouting every label was a habit rather than a design: when everything is capitalised nothing is emphasised, and long runs of capitals read measurably slower because the word-shape cue disappears. Capitals are kept in exactly two places that earn them — **driver surnames** in the order list and standings, which is the timing-screen convention and aids scanning a monospace column, and **abbreviations** like FP1. Asserted in the host tests so it cannot drift back | 2026-10-01 | **corrected by owner** |
| 154 | **The watched-driver banner REPLACES the top-5 strip** rather than sitting over it, and the strip hides while it shows. At 316 it cleared the trace by 6 px, which reads as touching; at 330 there are 20 px of clear black and nothing half-hidden behind it | 2026-10-01 | **corrected by owner** |
| 155 | **`render_pages.py` reads widget geometry from the YAML** and asserts three collisions before drawing: map into strip, banner over trace, banner over gear. It had carried its own copy of the geometry and drifted twice — once hiding a real 52 px collision, because it drew the map shorter than the firmware does. A render is only a check while it is derived from what the firmware actually does | 2026-10-01 | active |
| 156 | **The renderer fails hard if it draws a widget the YAML does not define.** Each page declares the widget ids it draws and `require()` checks them, so inventing one is an error rather than a nicer-looking picture. This is the structural fix for three separate drifts — a footer fix that landed in the YAML and not here, a map drawn 240 tall against the firmware's 300 (hiding a 52 px collision), and a flag drawn in the alert banner that the firmware never had | 2026-10-01 | **corrected by owner** |
| 157 | **Both render checks run in `deploy.sh`.** Geometry collisions and invented widgets now fail the build, alongside the glyph check and the warnings gate. A render that is not derived from the firmware is decoration, not verification | 2026-10-01 | active |
| 158 | **The alert banner's flag is padded**: 24 px from the edge, 24 px of clear space before the text, and the text beside it rather than beneath. It had been drawn directly above the words and the two collided | 2026-10-01 | **corrected by owner** |
| 159 | **`72th` is not a word.** The milestone ordinal special-cases 11/12/13 before looking at the last digit | 2026-10-01 | active |
| 160 | **UI-44c: Auto Off blanks the panel overnight**, as a checkbox with a from/until hour. Deliberately **separate from auto-dim**: dim is about the ambient light, off is about the household being asleep, and someone may want either, both or neither. At the deployment latitude dimming alone still leaves the panel lit through nineteen hours of December darkness | 2026-10-01 | **asked for by owner** |
| 161 | **The off window wraps midnight**, because 23:00–07:00 is the normal case and is not a range on a number line. A zero-length window never fires, and a touch wakes the panel for 60 s so somebody up at 3 a.m. need not change a setting. Leaving the window clears the override, so a touch cannot keep it awake into the next night | 2026-10-01 | active |
| 162 | **Every setting carries an icon**, the same glyph on the device, the web UI and Home Assistant. The codepoints are **generated from MDI's own metadata by name** (`tools/gen_icons.py`) and the generator fails on a name that does not exist — guessing a codepoint gives a blank glyph that compiles, flashes, and only shows up on a screen, which is the same silent class as a missing font glyph | 2026-10-01 | **asked for by owner** |
| 163 | **Constructor standings** ship beside the drivers' table. 2.5 kB, no live window, and team nationality resolves to a flag through the same demonym rule drivers use — an unmapped one draws none | 2026-10-01 | implements 8.1 |
| 164 | **Championship permutations** are pure arithmetic over standings already fetched: decided, sealable this weekend, or the gap. **A tie on points is NOT decided** — it falls to countback — so the reachability test is `>=`, not `>`. Exactly-reachable reads as open; one point more reads as clinched | 2026-10-01 | implements 8.1 |
| 165 | **The permutation line stays blank for most of a season on purpose.** It appears only once the field is genuinely down to two, or the title is settled. A sentence about arithmetic in April is noise, not news — and against the real fixture (8 contenders, 224 available) it correctly says nothing | 2026-10-01 | active |
| 166 | **UI-20e: race-week focus.** During a race weekend the carousel narrows to this weekend's circuit and the entered drivers, rather than wandering off to Kyalami and Fangio. The device should feel like it knows what is coming up | 2026-10-01 | implements 8.1 |
| 167 | **Home Assistant triggers**: `Session Live` and `Race Day` as binary sensors, `Next Session In` as a number. The device knows the schedule to the second, so HA can act with no code here | 2026-10-01 | implements 8.2 |
| 168 | **A teammate retirement is not a loss on merit.** The head-to-head is withheld and reported as "no result" rather than handed to the car that happened to keep running. Counting retirements is possible but must be asked for | 2026-10-01 | implements 8.2 |
| 169 | **The favourite team's rows get a quieter tint than the watched driver's**, so both can be on screen and still be told apart | 2026-10-01 | implements 8.2 |
| 170 | **The settings page is three tabs** (Display / Race / Location) with a numeric keyboard, and latitude, longitude and the Auto Off hours are entered on the panel. **Save validates first and writes nothing until every field is valid**: a clamped value is shown back in the field with a one-line message and the page stays open (§6.8). Tabs are three hidden-flag panels rather than an LVGL `tabview`, so a swipe cannot change tab under a finger | 2026-10-05 | **asked for by owner**, implements 7 |
| 171 | **The post-session summary carries OpenF1's half**: tyre strategy for the podium, weather, and the safety cars, virtual safety cars and red flags with their laps. Fetched **once per race**, after the window closes, one piece per loop; shown **only when it describes the same race as the Jolpica half** (a UTC-date match), because another race's tyres under this race's fastest lap looks like correct data | 2026-10-05 | **asked for by owner**, implements 8.1 / 134 |
| 172 | **"Is this driver racing?" is answered by the live roster** (`/current/drivers/`, rows with a code **and** a number — DATA-10), with the compiled table only as the floor before a fetch. Found by the rollover test: the carousel's overlap rule (RACE-13d) had been reading the **compiled** table, so a driver who retired kept a stale driver card and lost their legend card. A roster under 10 drivers is refused, because a briefly short feed would otherwise retire the rest of the grid. A rookie with no profile gets a text-only card (RACE-13e). The watched driver's `entered` follows the roster too | 2026-10-05 | corrects the implementation of 96/97 |
| 173 | **Every fetched resource has its own timer, and the schedule is pure and host-tested.** The first version shared the calendar's timer, so once the calendar succeeded **nothing else was due** — qualifying, standings, constructors and the summary were never fetched — while POST_SESSION's results had no interval at all (~1,800 requests/hour against Jolpica's 500). `last/results` is also the **previous** race's, so the race-only requests wait for the Grand Prix itself; after qualifying the grid is what is fetched. A failed resource is retried no tighter than 60 s (3.7.4). Neither fault was visible without a board | 2026-10-05 | corrects 14, implements 3.7.4 |
| 174 | **The rollover test derives 2027 from the real 2026 fixtures** — dates move a year, one driver leaves, one rookie arrives — because no 2027 exists to capture. It was mutation-checked: ignoring the roster makes it fail seven ways | 2026-10-05 | implements 98 |
| 175 | **DATA-12: OpenF1's track-temperature sensor drops out to exactly 0.0** (2 of 168 rows in session 11377, with the air at 26 °C). Left in, the weather line would read "Track 0–48". A zero beside warm air is ignored | 2026-10-05 | active |
| 176 | **OpenF1 answers a filter that matches nothing with HTTP 404** and `{"detail":"No results found."}` — measured with `flag=RED` on a race that had none. That is an empty result, not a fault; treated as an error it would back the data task off for five minutes over a clean race, the common case. `session_type=Race` also includes **Sprints** (name `Sprint`), so the session is chosen by `session_name` | 2026-10-05 | active |
| 177 | **`setup()` configured the carousel with zero drivers and zero legends** ("M2 fills them in") and `set_focus()` returns early when nothing changed, so in IDLE — most of the year — the carousel showed circuits only until someone touched Carousel Content. The counts are real from the first frame | 2026-10-05 | corrects 68 |
| 178 | **The boot / Wi-Fi page is ported from `sky-tracker` — and nothing ever left it.** This project had the page but no code that moved off it, so a device that connected would have sat on "Connecting to Wi-Fi" for ever. A 2 s check now fades to the primary page on connect and returns after 10 s offline (never over settings). Porting `sky-tracker`'s outage clock exactly would have carried a bug with it: it stores `now \| 1` and later computes `now - lost_since`, which for an **even** `now` is one tick in the future and wraps to ~4 billion, so the status page would return **at once** on half of all outages rather than after 10 s. The clock now stores `now`, or 1 when `now` is 0 | 2026-10-08 | **asked for by owner**, implements 6.1 |
| 179 | **Auto-dim reads the Sun from `sky-tracker`'s solar position**, not a cheaper formula. The first version used declination from the day of year and no equation of time - up to ~4 deg out around the equinoxes, harmless for a 13 deg ramp but no reason to be less accurate than the sibling. `tests/test_sun.cpp` pins it to known geometry (solstice noon elevations 51.9 / 5.0 deg, solar noon at Greenwich 11:44 UTC on 3 Nov and 12:14 on 11 Feb, east-positive longitude, both hemispheres) and shows midsummer at the deployment latitude dips only to ~42 % while midwinter reaches the 25 % floor | 2026-10-08 | **asked for by owner**, implements 6.8 |
| 180 | **The position is an input, read live.** The app had copied latitude and longitude into its own state once at boot, so a change from the new settings fields, the web UI or Home Assistant never reached the dimmer until a reboot - and at boot the restored value may not yet have been loaded. `auto_factor(time, lat, lon)` takes them as arguments and the 30 s pass reads the entities each time | 2026-10-08 | corrects 7 |
| 181 | **About page, modelled on `sky-tracker`'s UI-67**: logo (112 px, resized at build time because LVGL's own scaling garbles it), name, version, "by Dan Morphis", ESPHome version and build date, THIS DEVICE (name, IP, Wi-Fi and signal, MAC, uptime, refreshed each second while shown), DATA FROM with licences, and the trademark non-affiliation notice (3.7.5). Reached from a button where the settings page's version used to be. **Close returns to Settings without re-staging it**: on_load would otherwise discard unsaved edits and re-capture a live-changed brightness as Cancel's baseline | 2026-10-08 | **asked for by owner** |
| 182 | **Icons everywhere a control is**: Cancel, Save, About, Close and the three settings tabs on the panel, and the five diagnostic entities that had none on the web UI and in Home Assistant. Codepoints come from `tools/gen_icons.py` by name and the build fails on one that does not exist. The renders draw them from the same MDI font | 2026-10-08 | **asked for by owner**, implements 7.1 |
| 183 | **Firmware updates modelled on `sky-tracker`'s UI-68**: a prompt card on the top layer (Checking / Up to date / Update available with notes / Couldn't check), an icon beside the gear for an update the hourly check found, an install card with a bar, and an Upgrade Check button for the web UI and HA. First check 3 min after boot, then hourly. **Only a newer version counts**, because ESPHome offers an older one as readily - the bug that replaced `sky-tracker`'s working build with 4.6.3. The logic is pure and has `tests/test_update.cpp` (46 checks) | 2026-10-08 | **asked for by owner**, implements 2.4.6 |
| 184 | **The release notes are the manifest's `ota.summary`**, written by `make_release.py` from `--notes FILE` or the commit subjects since the previous tag, so the card says what changed without anyone remembering to write it. The device shows ~1.5 KB and the script warns past that | 2026-10-08 | implements 183 |
| 185 | **The web page's OTA upload had no hooks**, so an upload from it ran the whole write with the data task live and the screen frozen on its last frame - the exact failure FAIL-12 exists to prevent, on the one platform it was not wired to. It now shares the IDE upload's hooks through a YAML anchor | 2026-10-08 | corrects FAIL-12 |
| 186 | **`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC: y`**, taken from `sky-tracker` where the update check's TLS session failed with out-of-memory until it was set (plain `malloc` was not enough). Adopted on the same evidence rather than on a measurement of this device, and listed in 14.1 so it is measured; if handshakes on the PSRAM bus show as `lvgl took a long time`, it is the first thing to revisit | 2026-10-08 | active, **unmeasured** |
| 187 | **Settings chrome follows `sky-tracker` UI-16a/UI-68/UI-71/UI-72.** The header names the open tab (`DISPLAY`, `RACE`, `LOCATION`, orange mono16); the tabs are icon-only, 152x44 at y 52; the panels start at y 104; `Upgrade Check` (206,440, 150x34) and `About` (362,440, 112x34) are text-only buttons in a bottom row, the error line (x 16, y 440, 184 wide) beside them; the update card's progress track is `0x1A2547` (it was `0x22305A`); 60 s untouched closes Settings as Cancel (brightness restored), About and Debug back to the map, and an update offer or result as Not now, never an install |  2026-10-10 | active |
| 188 | **The detail card shows the driver's portrait, decoded top-down** (`f1_jpg.h`, copied from `sky-tracker`'s UI-59f): half size (120x160, a 2x2 average of the 240x320 JPEG) at (304,48), the photographer under it, the text narrowed to 280 px beside it. A short-lived task on core 0 decodes into one reused PSRAM buffer and reports a band at a time; a 40 ms timer on the display loop redraws it; rows not yet done are black. A card closed mid-decode cancels it. The carousel's driver card still has NO decoded portrait (its image widget is a placeholder) - found while doing this. The championship page shows a flag per driver (by driverId) and per constructor (the team's nationality); the Team column of the driver table goes to make room | 2026-10-10 | active |
| 189 | **The carousel's driver and legend cards decode their portrait too** (`f1_photo.h`, full size 240x320 at (24,56), top-down, cancelled when the next card commits). Before this the picture widget was a placeholder and the card text (x 12, y 300) lay across a picture that would have started at y 86: the portrait now sits at y 56 and a person's text beside it (x 282, y 60, 190 wide); a person with no portrait gets the text full width; a circuit keeps the old layout. The decoder's work area is on the task's stack so two decodes (detail card, carousel) can overlap | 2026-10-10 | active |
| 190 | **Animated logos, modelled on `sky-tracker`'s launch over its boot screen (UI-69k) and About shooting star (UI-67a).** Start lights (five, 600 ms apart, held 1 s, then out) and a car - an orange head and a fading trail - lapping the logo's own Monza trace (the logo is made from `f1_circuits.h`, so the car follows the very points; 5 s a lap). Boot page: once, from the first frame after setup (LVGL draws nothing while setup blocks it, so nothing can play earlier), the lights below the status text; About: the sequence repeats every 9 s with a rest, the lights over the logo's lower part. Pure timing and path in `f1_animlogic.h` (host test, 28 checks); objects in `f1_anim.h`: 9 small opaque discs per page, set_pos at 25 Hz only while that page is the active screen. No resize or blend per frame (sky-tracker PERF-9). The web page's header badge is now a link to the project page on GitHub: the SVG string the page is rewritten with (`f1_web.h`) is wrapped in an anchor, `tools/gen_logo.py` - sky-tracker does not link its badge, only swaps the SVG the same way. Unverified in a browser | 2026-10-10 | active |

---

## 13. Open questions

1. ~~**Terms of use and rate limits for Jolpica and OpenF1.**~~
   **CLOSED 2026-10-01** — both read in full and approved by the owner; the
   record is §3.7. It surfaced one material constraint, now decision 51:
   OpenF1's live window is a paid tier, which moved the whole live-timing
   design into RACE-12 (§6.3).
2. ~~**Does Night Mode default on?**~~ **CLOSED 2026-10-01 — declined
   entirely** (decision 83). Not a default question in the end: the feature is
   absent, and auto-dim (§6.8) covers the need.
3. ~~**Do 22 rows fit at `mono12`?**~~ **CLOSED 2026-10-01 — measured, yes**
   (decision 88, §6.3.1). Rendered at 480×480 with real Roboto Mono metrics
   and the real entry list: 18 px rows, TEAM column kept, 22 px of slack spent
   on the state line. `tools/mock_order_page.py` reproduces it. Hardware
   confirmation remains in §14.1, but the layout question is answered.
4. ~~**Driver headshot licence and hotlinking terms.**~~ **CLOSED 2026-10-01**
   — all licensing terms accepted (decision 67), and the design moved to
   Wikimedia Commons with the photographer credited and a build that fails on
   an unattributable image (§5.6.3, decisions 72–73). OpenF1's `headshot_url`
   is not used.
5. ~~**Is the 2026 calendar Jolpica serves correct?**~~ **MOOT 2026-10-01** —
   the device tracks **whatever season is current** (decision 94), so a given
   year's oddities are data to tolerate (§3.4), not a target to validate.
   2026's quirks become test fixtures.
6. ~~**What is "race day" for a sprint weekend?**~~ **CLOSED 2026-10-01 — yes,
   first-class** (decision 90). Own grid, own result, own race page, labelled
   `SPRINT`; a sprint weekend has two race days.
7. ~~**Serial port for the first flash.**~~ **MOOT 2026-10-01** — no hardware
   yet (decision 84). Recorded in §2.2 for when a board arrives; the port
   contention with `plane-tracker` still applies then.
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
10. ~~**Which legends?**~~ **CLOSED 2026-10-01 — delegated and settled** at 32
   rows (§5.6.4, decisions 85 and 96). Still the easiest thing in the project to
   change: it is one table, and nothing depends on its contents.
11. **Is the `POST_SESSION` window-close moment exactly right?** The +30 min
   boundary is OpenF1's published definition, but whether their data is
   actually readable at +30:00 or a little later is unmeasured. Decision 60
   makes this transition the race page's one real moment, so **retry rather
   than trusting the boundary** — and measure the true lag after the first
   race the device sees.
12. ~~**How loud should the watched-driver alerts be?**~~ **CLOSED 2026-10-01
   — louder** (decision 92). Full-width team-coloured banner for events,
   ~8 s full-screen takeover for milestones. An `Alert style` setting keeps a
   quiet path for anyone else in the room.
13. ~~**Portrait size and crop.**~~ **CLOSED 2026-10-01 — 240×320**
   (decision 91), delegated. The generator takes the size as a parameter, so
   it is a rerun if it ever looks wrong on hardware.

### 13.1 What is actually outstanding
| # | Question | Owner | Blocked on |
|---|---|---|---|
| 8 | Gated OpenF1 response shape — defensive only since NET-14 | me | a live session happening |
| 11 | True lag at OpenF1's +30 min boundary | me | the first race the device sees |
| 14 | **When does the next calendar get published?** RACE-13c handles the gap, but the first real end-to-end rollover test needs one to actually happen | me | Jolpica, and the turn of a season |

**Every question that needed an owner decision is closed.** The three above are
measurements waiting on the world, not choices — and **none of them blocks M0,
M1, M2 or M3.**

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

**No hardware yet** (decision 84). The order below puts everything that can be
finished on this host first, and §14.1 collects what genuinely needs a board.
This is a good position to be in: the generators, the data layer, the state
machine and the whole test suite are host work, and they are most of the
project's real risk.

1. **M0 — Host setup and a config that compiles — DONE 2026-10-01**
   - [x] ESPHome **2026.9.1** in a dedicated `.venv`, kept out of `radioconda`
   - [x] `f1-tracker.yaml`: hardware, `sdkconfig_options` (including NET-15),
         `build_flags`, LVGL buffer strategy, boot ordering, 30 kHz backlight,
         UART0 logger
   - [x] `secrets.yaml` (git-ignored) with a generated API key and web password;
         `secrets.yaml.example` committed — **no data-source tokens**, both free
         tiers are unauthenticated (decision 65)
   - [x] **23 entities** on the web UI and HA from the start (§7.1): 4 `number`,
         5 `switch`, 7 `select`, diagnostics and sensors, sorted into the four
         groups
   - [x] **`panel_soft_reset()` ported properly** (§2.1, HW-7) — `f1_panel.h`
         bit-bangs SWRESET over the 9-bit init SPI at `on_boot` priority 1100,
         before the SPI bus claims the pins. `plane-tracker` left this as an
         open question; this project does not
   - [x] LVGL shells for all seven pages (§6.1), the gear with
         `on_short_click`/`on_long_press`, the OTA panel on the top layer, and
         the attribution footer (decision 54)
   - [x] `esphome config` validates and `esphome compile` succeeds
   - [x] **`deploy.sh`** with the warnings-are-errors gate (decision 108)
   - [x] **Stop there.** Flashing, and everything that depends on seeing the
         panel, is §14.1
2. **M1 — Circuit data and the carousel — DONE 2026-10-01**
   - [x] `tools/gen_circuits.py` → `f1_circuits.h`: all 40 traces, projected
         with `cos(lat₀)`, rotated for best fill, normalised to `int16`, with a
         41-row `circuitId` cross-reference. **19,176 B of point data — exactly
         the §5.2 prediction.** Fails the build on an unmatched calendar
         circuit or degenerate geometry
   - [x] `tools/gen_calendar.py` → `f1_calendar.h`: season from `/current/` at
         build time, **never pinned** (decision 94); session starts as UTC
         epochs; circuit country → ISO3; CC BY-NC-SA notice emitted (decision 55)
   - [x] `f1_map.h` and `f1_carousel.h` — free of LVGL, so they build on the host
   - [x] **Host tests: 1823 checks, 0 failures** (`make -C tests`) — fit inside
         four box shapes with aspect preserved and ≥98 % fill, sorted-and-
         complete id map, every calendar round resolving, the `zeltweg`
         exclusion, calendar ordering, sprint/FP3 exclusivity, carousel
         coverage, no-three-in-a-row, every content filter, empty lists, and the
         restored-interval clamp
   - [x] `circuit_page` drawing the trace, start/finish tick and north arrow;
         carousel timing, fade, content filter and hold-to-pause
   - [x] `tools/preview_circuits.py` — **all 40 traces verified by render**
         (decision 117)
   - [ ] Driver and legend cards fill in at M2; the rotation already handles
         empty lists, which is the state the device ships in today
   - *This milestone is deliberately first: it is the state the device spends
     most of the year in, it needs no live data, and it proves the map
     rendering that everything else depends on.*
3. **M2 — Facts, flags, profiles and portraits — DONE 2026-10-01**
   - [x] `tools/gen_facts.py` → `f1_facts.h`: fastest lap, most recent winner
         with the event name, most wins and races held, for 41 circuits.
         **35 have a fastest lap**; DATA-11 labels it "since 2004"
   - [x] `tools/gen_flags.py` → `f1_flags.h`: **48 flags, 28.1 kB** in RGB565,
         including `MCO`, `BHR` and `SAU`. Far under the ~150 kB estimate
   - [x] Demonym→ISO3 in the generator; a driver with no resolvable
         nationality draws **no flag** (decision 20)
   - [x] `glyphsets: [GF_Latin_Core]`, verified against 715 real strings
   - [x] `check_glyphs.py` as a build gate, over 1,901 strings
   - [x] `tools/gen_drivers.py` → **23 drivers, 32 legends**. The pole guard
         worked: **24 curated, 8 from the API, 0 wrong**
   - [x] `tools/gen_portraits.py` → **48 portraits, 0.98 MB**, each with a
         licence and a credit the card draws. 3 people have no usable image and
         get text-only cards (RACE-13e)
   - [x] Pre-1994 pole values curated with sources
   - [x] Curated legend prose for all 32 legends (5.6.2)
   - [x] Portraits at **240×320**; generator caps at 3 MB
4. **M3 — Data path and the state machine — mostly done 2026-10-01**
   - [x] `f1_net.h`: TLS to both hosts, persistent sessions (`keep_alive`),
         selective parse (`f1_json.h`), `.buffer_size = 4096` on the HTTP
         client (NET-15)
   - [x] `f1_state.h`: the state machine and the poll-interval table
         (`intervals_for()`), read by the fetch task each cycle
   - [x] Host tests for the state machine, **including a `04:00Z` race read
         from a far-away time zone** (`tests/test_state.cpp`)
   - [x] Calendar refresh superseding the compiled floor, with a
         `Calendar Source` diagnostic
   - [ ] The **profile build date** on the debug page (RACE-13e) — not built
   - [x] **A live roster** from `/current/drivers/` (`parse_roster`, `f1_roster.h`),
         so the carousel and the watched driver follow who is racing without a
         rebuild — decision 172
   - [x] **Per-resource fetch timers**, a pure `plan_for()`/`pick()`, and
         `tests/test_net.cpp` — decision 173
   - [x] **Season resolution from `/current/`** and the `OFF_SEASON` gap
         (RACE-13a–c); "no calendar yet is `OFF_SEASON`, not a fault" is tested
   - [x] **Rollover host test** (decision 98): `tests/test_rollover.cpp`, 51
         checks. The 2027 inputs are derived from the real 2026 fixtures by the
         smallest edit that makes the point (decision 174)
5. **M4 — Race day — mostly done 2026-10-01**
   - [x] `race_page`: header, state line, map, top-5 strip
   - [x] `order_page`: 22 rows, flags, team-colour bars
   - [x] Grid extraction (RACE-11) + host test against the position fixture
   - [ ] **Measure 22 rows at `mono12` on hardware** — §14.1
   - [x] **The four order modes** (RACE-12): `ENTRY LIST` →
         `GRID (PROVISIONAL)` → `GRID` + `FINAL`, host-tested, including the
         window-close moment (decision 60)
   - [x] **NET-14**: OpenF1's live window computed and skipped; host-tested
   - [x] `RESULTS IN ~mm:ss` countdown (UI-3a) — the only moving element
   - [x] Attribution footer on every page (§3.7.3)
   - [ ] **Clickable links to both sources in the web UI** (§3.7.3) — the
         footer text is on the panel, but the web page carries no links yet
   - [x] The distinct failure states (decision 57)
   - [x] **Watched driver** (§6.14): `driverId` resolution, the ambient marker,
         the three alert tiers, and the **once-only latch** with a host test
         that replays a reboot mid-weekend (decision 80)
6. **M5 — Settings and web/HA parity — mostly done 2026-10-01**
   - [x] Settings page with staged Save/Cancel (decision 38); brightness live
   - [x] **Tabs** (Display / Race / Location), the **numeric keyboard**, and
         latitude, longitude and the Auto Off hours as on-screen fields, with
         validation that clamps visibly and never saves a partial form
         (decision 170)
   - [x] Every entity on the web UI and HA, sorted into groups
   - [x] Auto-dim (§6.8) and the lat/lon-vs-timezone note
7. **M6 — Standings and detail cards — mostly done 2026-10-01**
   - [x] `standings_page`, rendered from the fetched championship table
   - [x] **Runtime legend/driver overlap** (RACE-13d) and the text-only card
         for a person with no portrait (RACE-13e)
   - [x] Portraits on the profile cards, with the mandatory credit
   - [x] Tap-a-row detail card for a driver, and tap-the-map for the circuit.
         A tap anywhere closes it, including the gear (§6.1)
   - [x] **`summary_page`** — fastest lap, pit-stop count, quickest stop and the
         podium. **No OpenF1 needed** (decision 133)
   - [x] Race page's **circuit flag and top-5 strip** (decision 49), both found
         missing by `render_pages.py` (decision 138)
   - [x] Tyre strategy, weather and the safety cars and red flags that
         occurred — from OpenF1, once the window closes (decision 171). Tyres are
         a text line per podium finisher (`NOR M30 S5`), not coloured blocks
8. **M7 — Polish — mostly done 2026-10-01**
   - [x] OTA progress panel on both platforms, with the data task paused for
         the duration and resumed on failure (§2.4.6)
   - [x] Distinct fault states: rate-limited vs no-data vs API error (decision 57)
   - [ ] Crash record — `sky_diag.h` needs re-targeting off the `skydata`
         partition this project declines (decision 46)
   - [x] ~~`deploy.sh` with warnings-are-errors~~ — **pulled forward to M0**
         (decision 108); it earned its place on the first build
   - [ ] Enclosure

### 14.1 Parked until there is a board
Not forgotten — **blocked**. Collected here so the backlog stays honest.

| Item | Why it needs hardware | Section |
|---|---|---|
| First flash over USB | — | M0 |
| Confirm panel, touch, backlight, PSRAM | — | §2.1 |
| Confirm the board revision against the silkscreen | the model defaults are only trusted once | §2.1 |
| **Prove `panel_soft_reset()` at priority 1100** | the symptom is a panel that fails to initialise | §2.1, HW-7 |
| `min_power` — the lowest still-visible duty | panel-specific; 0.10 is `sky-tracker`'s value | §2.4.7 |
| **Does 30 kHz actually stop the backlight whine?** | audible, not measurable in software | §2.4.7, decision 4 |
| Confirm the 18 px row height in LVGL | **answered by render** (decision 88); LVGL metrics are not PIL's, and 1 px of padding is thin | §6.3.1 |
| Confirm the circuit traces on the panel | **verified by render** (decision 117); this confirms LVGL's line rasteriser agrees with PIL's | §6.5 |
| Confirm 240×320 portraits read well on the panel | settled at 240×320 (decision 91); this is confirmation only | §5.6.3 |
| Where the LVGL buffer landed (internal RAM or PSRAM) | logged at boot | §2.4.3 |
| Free internal RAM / PSRAM with everything enabled | the real budget (§9) | §9 |
| **Does the 64 KB window trigger `lvgl took a long time`?** | more bytes through the PSRAM bus per fetch; the window is the first thing to walk back | §2.4.8, decision 99 |
| Real transfer times vs the §2.4.8 arithmetic | the model is corroborated but unverified on this board | §2.4.8 |
| Boot time, and whether icon/portrait work needs core 1 | `plane-tracker` decision 64 | §9 |
| `MAP-5a` — the `lv_line` redraw cost at ~300×300 | PSRAM bandwidth | §6.5 |
| **Does the update check's TLS session fit?** And does `MBEDTLS_EXTERNAL_MEM_ALLOC` slow handshakes enough to show as `lvgl took a long time`? | free internal RAM beside two kept sessions is the thing that failed on `sky-tracker` | §2.4.6, decision 183 |
| **A real update, end to end**: the card, the bar through a blocking download, the restart, and the rollback if it fails | the install blocks the loop; nothing redraws except from the OTA hooks | decision 183 |
| The release asset round trip: `/releases/latest/download/manifest.json` → the 302 → the `.ota.bin` path resolved against it | needs a published release with a manifest that carries `ota.summary` | decision 184 |

- [ ] **Everything in this table is a measurement, not a design decision.**
      Nothing in §1–§13 is blocked by it; the design is settled and the
      generators, the data layer and the tests can all be finished first.
- [ ] Keep writing the LVGL YAML regardless. `esphome config` catches most
      structural mistakes, and `check_glyphs.py` (§6.9) catches the font bugs
      that would otherwise need a screen.
