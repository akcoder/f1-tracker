# Captured API responses — offline fixtures

Captured **2026-10-01** on the development host. Every size and shape quoted in
REQUIREMENTS.md §3.0 was measured from these or from the probes that produced
them. They are committed so the parser and the host tests can be developed and
run with no network.

| File | Endpoint | Size |
|---|---|---|
| `jolpica-current-next.json` | `/ergast/f1/current/next/` | 821 B |
| `jolpica-2026-races.json` | `/ergast/f1/2026/races/?limit=40` | 14.3 KB |
| `jolpica-2026-circuits.json` | `/ergast/f1/2026/circuits/?limit=100` | 5.3 KB |
| `jolpica-2026-drivers.json` | `/ergast/f1/2026/drivers/?limit=100` | 5.6 KB |
| `jolpica-2025-last-qualifying.json` | `/ergast/f1/2025/last/qualifying/` | 9.1 KB |
| `jolpica-driver-standings.json` | `/ergast/f1/current/driverStandings/` | 10.1 KB |
| `openf1-2026-race-sessions.json` | `/v1/sessions?year=2026&session_type=Race` | 11.2 KB |
| `openf1-drivers-11377.json` | `/v1/drivers?session_key=11377` | 9.2 KB |
| `openf1-position-11377.json` | `/v1/position?session_key=11377` | 35.9 KB |
| `openf1-racecontrol-flags-11377.json` | `/v1/race_control?session_key=11377&category=Flag` | 20.3 KB |

## What each fixture pins down

- **`jolpica-2026-drivers.json`** — 32 drivers, **9 of them with no
  `nationality` field at all**. The demonym→ISO3 test asserts every present
  nationality resolves and every absent one draws no flag (§3.4, decisions
  19–20).
- **`openf1-position-11377.json`** — 311 rows for a whole race session. The
  earliest row per driver, sorted by position, is a complete **P1–P22** set at
  one identical timestamp (`10:07:06.143`). This is the grid-extraction fixture
  for RACE-11 (§3.5, decision 16).
- **`jolpica-2026-races.json`** — 23 rounds, including round 16 served as
  `raceName: "Bahrain Grand Prix in Malaysia"` with `circuitId: "sepang"`. The
  fixture that proves the circuit flag must come from
  `Circuit.Location.country`, not the race name (§3.4, decision 18).
- **`jolpica-2026-circuits.json`** — 24 circuits against the 23 rounds above.
  The calendar is provisional; open question 5.
- **`openf1-drivers-11377.json`** — `team_colour` present for every driver,
  `country_code` **null** for every driver (§3.4, decision 19).
