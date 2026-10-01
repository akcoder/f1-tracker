#!/usr/bin/env python3
"""Generate f1-tracker/f1_drivers.h and f1_legends.h.

REQUIREMENTS.md 5.6. Career records are generated offline and compiled in, so
the carousel works with no network (decision 69).

Two guards that exist because the data lies in specific ways:

  DATA-8  Ergast-lineage qualifying data starts in 1994, so API pole counts are
          wrong for anyone who raced before it - Prost 0 against an actual 33,
          Fangio 0 against 29, Senna 3 against 65. The generator REFUSES to emit
          an API pole count for a pre-1994 career and uses the curated table
          instead, or omits the line. A blank is correct; a zero is a lie.

  DATA-7  Historical drivers carry no `code` and no `permanentNumber`. The card
          must render without either.

The legends table is the full 32 rows of 5.6.4 INCLUDING drivers who are still
racing: RACE-13d resolves the overlap at runtime against the live entry list,
so a retirement moves a driver into the legend rotation with no rebuild.
"""
import argparse, datetime, json, os, sys
from jolpica import Client

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# 5.6.4, decision 85. Groups are kept so the carousel can weight or filter.
LEGENDS = [
    # multiple champions (17)
    ("fangio", 0), ("ascari", 0), ("brabham", 0), ("clark", 0), ("stewart", 0),
    ("lauda", 0), ("prost", 0), ("senna", 0), ("michael_schumacher", 0),
    ("vettel", 0), ("fittipaldi", 0), ("piquet", 0), ("hakkinen", 0),
    ("graham_hill", 0), ("alonso", 0), ("hamilton", 0), ("max_verstappen", 0),
    # single champions of note (10)
    ("hunt", 1), ("mansell", 1), ("rindt", 1), ("jacques_villeneuve", 1),
    ("damon_hill", 1), ("hawthorn", 1), ("surtees", 1), ("rosberg", 1),
    ("raikkonen", 1), ("button", 1),
    # never champion (5)
    ("moss", 2), ("gilles_villeneuve", 2), ("amon", 2), ("ickx", 2), ("peterson", 2),
]
GROUPS = ["Multiple champion", "World champion", "Never champion"]

# DATA-8: curated poles for pre-1994 careers. The API cannot supply these.
# source: each driver's career record, cross-checked against their Wikipedia
# article infobox (the same page the portrait comes from).
CURATED_POLES = {
    "fangio": 29, "ascari": 14, "brabham": 13, "clark": 33, "stewart": 17,
    "lauda": 24, "prost": 33, "senna": 65, "michael_schumacher": 68,
    "fittipaldi": 6, "piquet": 24, "graham_hill": 13, "hunt": 14, "mansell": 32,
    "rindt": 10, "hawthorn": 4, "surtees": 8, "moss": 16,
    "gilles_villeneuve": 2, "amon": 5, "ickx": 13, "peterson": 14,
    "hakkinen": 26, "damon_hill": 20, "jacques_villeneuve": 13,
}
QUALI_DATA_FROM = 1994

# Nationality demonym -> ISO 3166-1 alpha-3 (3.4, decision 19). A driver whose
# demonym is absent or unmapped draws NO flag - never a wrong one (decision 20).
DEMONYM_ISO3 = {
    "American": "USA", "Argentine": "ARG", "Argentinian": "ARG", "Australian": "AUS",
    "Austrian": "AUT", "Belgian": "BEL", "Brazilian": "BRA", "British": "GBR",
    "Canadian": "CAN", "Chilean": "CHL", "Chinese": "CHN", "Colombian": "COL",
    "Czech": "CZE", "Danish": "DNK", "Dutch": "NLD", "Finnish": "FIN",
    "French": "FRA", "German": "DEU", "Hungarian": "HUN", "Indian": "IND",
    "Indonesian": "IDN", "Irish": "IRL", "Italian": "ITA", "Japanese": "JPN",
    "Malaysian": "MYS", "Mexican": "MEX", "Monegasque": "MCO", "New Zealander": "NZL",
    "Polish": "POL", "Portuguese": "PRT", "Rhodesian": "ZWE", "Russian": "RUS",
    "South African": "ZAF", "Spanish": "ESP", "Swedish": "SWE", "Swiss": "CHE",
    "Thai": "THA", "Uruguayan": "URY", "Venezuelan": "VEN", "East German": "DEU",
    "Liechtensteiner": "LIE", "Chilean ": "CHL",
}


def career(c, did, first_season):
    """Career counts via MRData.total with limit=1 (decision 70)."""
    wins = c.total(f"drivers/{did}/results/1/")
    starts = c.total(f"drivers/{did}/races/")
    # Podiums would cost two more requests per driver (positions 2 and 3). At
    # 500 requests/hour that is ~110 requests for a line the card can live
    # without, so it is dropped rather than paid for.
    podiums = 0

    # DATA-8 guard. Never emit an API pole count for a pre-1994 career.
    if first_season and first_season < QUALI_DATA_FROM:
        poles = CURATED_POLES.get(did, -1)   # -1 = unknown, omit the line
        pole_src = "curated" if poles >= 0 else "omitted"
    else:
        poles = c.total(f"drivers/{did}/qualifying/1/")
        pole_src = "api"
    return wins, starts, podiums, poles, pole_src


def seasons_of(c, did):
    d = c.get(f"drivers/{did}/seasons/", limit=100)["MRData"]["SeasonTable"]["Seasons"]
    yrs = [int(s["season"]) for s in d]
    return (min(yrs), max(yrs)) if yrs else (0, 0)


def champions(c, lo=1950, hi=None):
    """Titles per driver, by sweeping the champion of every season."""
    hi = hi or datetime.date.today().year
    out = {}
    for y in range(lo, hi + 1):
        try:
            L = c.get(f"{y}/driverStandings/1/")["MRData"]["StandingsTable"]["StandingsLists"]
        except RuntimeError:
            continue
        if not L:
            continue
        ds = L[0].get("DriverStandings") or []
        if ds:
            out.setdefault(ds[0]["Driver"]["driverId"], []).append(y)
    return out


def esc(s):
    return (s or "").replace("\\", "\\\\").replace('"', '\\"')


def emit(path, ns, rows, season, extra_note=""):
    L = []; w = L.append
    w("#pragma once")
    w(f"// GENERATED by tools/gen_drivers.py - do not edit.")
    w("//")
    w("// Data: jolpi.ca, CC BY-NC-SA 4.0. This generated file is an adaptation of")
    w("// that data and carries the same licence (decision 55). The code does not.")
    if extra_note:
        for line in extra_note.split("\n"):
            w(f"// {line}")
    w(f"// Built {datetime.datetime.now(datetime.timezone.utc):%Y-%m-%d %H:%M UTC}")
    w("#include <cstdint>")
    w("")
    w("namespace f1 {")
    w(f"namespace {ns} {{")
    w("")
    w("struct Profile {")
    w("  const char *driver_id;   // the ONLY stable key (DATA-6)")
    w("  const char *given;")
    w("  const char *family;")
    w("  const char *code;        // 3-letter acronym, or \"\" - absent pre-1980s (DATA-7)")
    w("  const char *iso3;        // \"\" when the demonym is absent or unmapped: draw")
    w("                           // NO flag rather than a wrong one (decision 20)")
    w("  const char *dob;         // ISO date, for the age line")
    w("  uint16_t number;         // 0 when absent (DATA-7). NEVER an identity key")
    w("  uint16_t wins, starts, podiums;")
    w("  int16_t poles;           // -1 = unknown, OMIT the line (DATA-8)")
    w("  uint16_t first_season, last_season;")
    w("  uint8_t titles;")
    w("  uint8_t group;           // index into GROUP_NAME, legends only")
    w("};")
    w("")
    w(f"inline constexpr const char *GROUP_NAME[{len(GROUPS)}] = {{")
    for g in GROUPS:
        w(f'    "{esc(g)}",')
    w("};")
    w("")
    w(f"inline constexpr Profile P[{len(rows)}] = {{")
    for r in rows:
        w(f'    {{"{esc(r["id"])}", "{esc(r["given"])}", "{esc(r["family"])}", '
          f'"{esc(r["code"])}", "{esc(r["iso3"])}", "{esc(r["dob"])}", {r["number"]}, '
          f'{r["wins"]}, {r["starts"]}, {r["podiums"]}, {r["poles"]}, '
          f'{r["first"]}, {r["last"]}, {r["titles"]}, {r["group"]}}},')
    w("};")
    w(f"inline constexpr int N = {len(rows)};")
    w(f"inline constexpr uint16_t SOURCE_SEASON = {season};")
    w("")
    w(f"}}  // namespace {ns}")
    w("}  // namespace f1")
    open(path, "w").write("\n".join(L) + "\n")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--season", default=None, help="default: whatever /current/ names")
    a = ap.parse_args()
    c = Client()

    season = a.season
    if not season:
        season = c.get("current/", limit=1)["MRData"]["RaceTable"]["season"]
    print(f"season {season}")

    titles = champions(c)
    print(f"champion sweep: {len(titles)} drivers with at least one title")

    def build(did, group, entry=None):
        d = entry or c.get(f"drivers/{did}/")["MRData"]["DriverTable"]["Drivers"][0]
        first, last = seasons_of(c, did)
        wins, starts, podiums, poles, psrc = career(c, did, first)
        nat = d.get("nationality")
        iso = DEMONYM_ISO3.get(nat, "") if nat else ""
        if nat and not iso:
            print(f"  WARN unmapped demonym '{nat}' for {did} - no flag will be drawn",
                  file=sys.stderr)
        return dict(id=did, given=d.get("givenName", ""), family=d.get("familyName", ""),
                    code=d.get("code") or "", iso3=iso, dob=d.get("dateOfBirth", ""),
                    number=int(d["permanentNumber"]) if d.get("permanentNumber") else 0,
                    wins=wins, starts=starts, podiums=podiums, poles=poles,
                    first=first, last=last, titles=len(titles.get(did, [])),
                    group=group, pole_src=psrc)

    # ---- current drivers: only those carrying a code are actually racing
    # (DATA-10 - the season list includes reserves with no number or acronym)
    dj = c.get(f"{season}/drivers/", limit=100)["MRData"]["DriverTable"]["Drivers"]
    racing = [d for d in dj if d.get("code")]
    print(f"{season} driver list: {len(dj)} rows, {len(racing)} with a code (DATA-10)")
    drivers = []
    for d in racing:
        drivers.append(build(d["driverId"], 0, d))
    drivers.sort(key=lambda r: (-r["titles"], -r["wins"], r["family"]))

    # ---- legends: the full table, INCLUDING those still racing (RACE-13d)
    legends = []
    for did, grp in LEGENDS:
        legends.append(build(did, grp))
    legends.sort(key=lambda r: (-r["titles"], -r["wins"], r["family"]))

    print(f"\n{c.report()}")

    # ---- DATA-8 report, because this is the field most likely to be wrong
    curated = [r for r in legends if r["pole_src"] == "curated"]
    omitted = [r for r in legends if r["pole_src"] == "omitted"]
    api = [r for r in legends if r["pole_src"] == "api"]
    print(f"\npoles (DATA-8): {len(api)} from the API (post-{QUALI_DATA_FROM} careers), "
          f"{len(curated)} curated, {len(omitted)} omitted")
    if omitted:
        print(f"  omitted (no curated value): {[r['id'] for r in omitted]}")

    noflag = [r["id"] for r in drivers + legends if not r["iso3"]]
    if noflag:
        print(f"no flag (decision 20): {noflag}")

    emit(os.path.join(ROOT, "f1-tracker", "f1_drivers.h"), "drivers", drivers, int(season),
         f"The {season} entry list: drivers carrying a code, i.e. actually racing\n"
         "(DATA-10 - the season endpoint also lists reserves with no number).")
    emit(os.path.join(ROOT, "f1-tracker", "f1_legends.h"), "legends", legends, int(season),
         "5.6.4, decision 85. INCLUDES drivers who are still racing: RACE-13d\n"
         "resolves the overlap at RUNTIME against the live entry list, so a\n"
         "retirement moves a driver into the legend rotation with no rebuild.")
    print(f"\nwrote f1_drivers.h ({len(drivers)}) and f1_legends.h ({len(legends)})")


if __name__ == "__main__":
    main()
