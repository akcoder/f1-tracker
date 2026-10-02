#pragma once
// The parsed data store: what the fetch task produces and the UI thread reads.
//
// Free of LVGL and of ESP-IDF so tests/ can drive the parsers against the
// committed fixtures - the parse is where the data-quality rules in section 3.4
// actually live, so it is the part most worth testing.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "f1_json.h"
#include "f1_state.h"

namespace f1 {
namespace store {

// The order-page row, kept HERE rather than in f1_order.h so the parsers and
// their tests build on the host: f1_order.h needs LVGL, this does not.
struct Entry {
  int pos = 0;                 // 0 = no position yet (ENTRY LIST)
  int number = 0;
  char code[8] = {0};
  char name[20] = {0};         // surname, upper case
  char team[20] = {0};
  char iso3[4] = {0};          // "" = draw NO flag (decision 20)
  char gap[12] = {0};
  uint32_t colour = 0x888888;  // team_colour, cached (6.10)
  bool out = false;            // retired: keeps its row, greyed (6.3)
  bool watched = false;
};

inline constexpr int MAX_ROUNDS = 26;
inline constexpr int MAX_ENTRIES = 24;
inline constexpr int MAX_STANDINGS = 24;

struct Standing {
  int pos = 0;
  int points = 0;
  int wins = 0;
  char code[8] = {0};
  char name[20] = {0};
  char team[20] = {0};
  char iso3[4] = {0};
  char driver_id[28] = {0};
};

// 8.1: the post-session summary. Found at implementation time that Jolpica
// carries BOTH the fastest lap and the pit stops, so this page needs no OpenF1
// at all - which means no live window to wait out and nothing gated. Only tyre
// compounds, flags and weather would need OpenF1, and those are optional.
struct Constructor {
  int pos = 0;
  int points = 0;
  int wins = 0;
  char name[20] = {0};
  char iso3[4] = {0};
};

struct Summary {
  char fl_driver[20] = {0};
  char fl_time[12] = {0};
  int fl_lap = 0;
  char event[48] = {0};
  uint16_t season = 0;
  int n_stops = 0;
  char best_stop_driver[20] = {0};
  char best_stop[10] = {0};
  bool have_fastest = false;
  bool have_stops = false;
};

struct Store {
  // A fetched calendar supersedes the compiled floor (DATA-3). Rounds are kept
  // in the same shape the state machine already reads.
  calendar::Round rounds[MAX_ROUNDS];
  int n_rounds = 0;
  uint16_t season = 0;
  bool have_calendar = false;

  Entry entries[MAX_ENTRIES];
  int n_entries = 0;
  state::OrderMode entries_mode = state::ENTRY_LIST;

  Standing standings[MAX_STANDINGS];
  int n_standings = 0;

  Constructor constructors[12];
  int n_constructors = 0;
  uint8_t standings_round = 0;    // which round the table is up to date through
  Summary summary;
  uint32_t generation = 0;      // bumped on every successful parse, so the UI
                                // thread can tell "unchanged" from "stale"
};

// ---- helpers -------------------------------------------------------------
// Surnames are upper-cased for the order list and the standings, and ONLY
// there. That is the timing-screen convention, and in a monospace column it
// genuinely helps the eye find a name. Everywhere else - cards, banners, the
// state line - text is sentence case, because shouting everything emphasises
// nothing and is slower to read.
inline void upper_copy(char *dst, size_t n, const char *src) {
  size_t i = 0;
  for (; src && src[i] && i + 1 < n; i++)
    dst[i] = (src[i] >= 'a' && src[i] <= 'z') ? (char) (src[i] - 32) : src[i];
  dst[i] = '\0';
}

// Jolpica serves date and time separately, both UTC. A round with a date but
// NO time has an UNKNOWN start, which must not become midnight (section 3.4).
inline uint32_t to_epoch(const char *date, const char *time_s) {
  if (!date || !*date || !time_s || !*time_s) return 0;
  int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
  if (std::sscanf(date, "%d-%d-%d", &y, &mo, &d) != 3) return 0;
  if (std::sscanf(time_s, "%d:%d:%d", &h, &mi, &se) < 2) return 0;
  // days since epoch, civil-from-days (Howard Hinnant's algorithm)
  y -= mo <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned) (y - era * 400);
  const unsigned doy = (unsigned) ((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  const long days = (long) era * 146097 + (long) doe - 719468;
  return (uint32_t) (days * 86400L + h * 3600 + mi * 60 + se);
}

// ---- parsers -------------------------------------------------------------
// Each returns false on anything it cannot trust. 3.4: a failed parse keeps the
// existing state rather than clearing it, so the caller simply does not commit.

inline bool parse_calendar(const char *s, size_t n, Store &out) {
  const size_t tbl = json::path(s, n, "MRData.RaceTable");
  if (tbl == json::NPOS) return false;
  char season[8];
  if (json::get_str(s, tbl, n, "season", season, sizeof(season)))
    out.season = (uint16_t) atoi(season);
  const size_t races = json::find_key(s, tbl, n, "Races");
  if (races == json::NPOS) return false;
  const int nr = json::array_len(s, races, n);
  if (nr <= 0) return false;                 // an empty season is not a calendar

  static const char *KEYS[] = {"FirstPractice", "SecondPractice", "ThirdPractice",
                               "SprintQualifying", "Sprint", "Qualifying"};
  int kept = 0;
  for (int i = 0; i < nr && kept < MAX_ROUNDS; i++) {
    const size_t r = json::array_at(s, races, n, i);
    if (r == json::NPOS) continue;
    calendar::Round &R = out.rounds[kept];
    static char names[MAX_ROUNDS][48], cids[MAX_ROUNDS][28],
                countries[MAX_ROUNDS][28], locals[MAX_ROUNDS][28];
    long round = 0;
    json::get_int(s, r, n, "round", round);
    R.round = (uint8_t) round;
    json::get_str(s, r, n, "raceName", names[kept], sizeof(names[kept]));
    R.name = names[kept];

    const size_t ci = json::find_key(s, r, n, "Circuit");
    if (ci == json::NPOS) continue;
    json::get_str(s, ci, n, "circuitId", cids[kept], sizeof(cids[kept]));
    R.circuit_id = cids[kept];
    const size_t loc = json::find_key(s, ci, n, "Location");
    // decision 18: the flag keys on the CIRCUIT's country, never the race name.
    json::get_str(s, loc, n, "country", countries[kept], sizeof(countries[kept]));
    json::get_str(s, loc, n, "locality", locals[kept], sizeof(locals[kept]));
    R.country = countries[kept];
    R.locality = locals[kept];
    R.iso3 = "";          // resolved against the compiled table by the caller

    for (int k = 0; k < calendar::N_SESSIONS; k++) R.start[k] = 0;
    char d[16], t[16];
    for (int k = 0; k < 6; k++) {
      const size_t sj = json::find_key(s, r, n, KEYS[k]);
      if (sj == json::NPOS) continue;
      if (json::get_str(s, sj, n, "date", d, sizeof(d)) &&
          json::get_str(s, sj, n, "time", t, sizeof(t)))
        R.start[k] = to_epoch(d, t);
    }
    if (json::get_str(s, r, n, "date", d, sizeof(d)) &&
        json::get_str(s, r, n, "time", t, sizeof(t)))
      R.start[calendar::RACE] = to_epoch(d, t);
    kept++;
  }
  if (kept == 0) return false;
  out.n_rounds = kept;
  out.have_calendar = true;
  out.generation++;
  return true;
}

// Qualifying classification -> GRID (PROVISIONAL). Penalties are not applied to
// it, which is why the mode says so (3.5).
inline bool parse_qualifying(const char *s, size_t n, Store &out) {
  const size_t races = json::path(s, n, "MRData.RaceTable.Races");
  if (races == json::NPOS || json::array_len(s, races, n) == 0) return false;
  const size_t r0 = json::array_at(s, races, n, 0);
  const size_t res = json::find_key(s, r0, n, "QualifyingResults");
  if (res == json::NPOS) return false;
  const int nq = json::array_len(s, res, n);
  if (nq <= 0) return false;

  int kept = 0;
  for (int i = 0; i < nq && kept < MAX_ENTRIES; i++) {
    const size_t q = json::array_at(s, res, n, i);
    Entry &e = out.entries[kept];
    e = Entry();
    long v = 0;
    json::get_int(s, q, n, "position", v); e.pos = (int) v;
    json::get_int(s, q, n, "number", v);   e.number = (int) v;
    const size_t dr = json::find_key(s, q, n, "Driver");
    char fam[24];
    json::get_str(s, dr, n, "code", e.code, sizeof(e.code));
    json::get_str(s, dr, n, "familyName", fam, sizeof(fam));
    upper_copy(e.name, sizeof(e.name), fam);
    const size_t co = json::find_key(s, q, n, "Constructor");
    json::get_str(s, co, n, "name", e.team, sizeof(e.team));
    kept++;
  }
  if (kept == 0) return false;
  out.n_entries = kept;
  out.entries_mode = state::GRID_PROVISIONAL;
  out.generation++;
  return true;
}

// Race results -> GRID (actual, penalties applied) and FINAL.
inline bool parse_results(const char *s, size_t n, Store &out) {
  const size_t races = json::path(s, n, "MRData.RaceTable.Races");
  if (races == json::NPOS || json::array_len(s, races, n) == 0) return false;
  const size_t r0 = json::array_at(s, races, n, 0);
  const size_t res = json::find_key(s, r0, n, "Results");
  if (res == json::NPOS) return false;
  const int nr = json::array_len(s, res, n);
  if (nr <= 0) return false;

  int kept = 0;
  for (int i = 0; i < nr && kept < MAX_ENTRIES; i++) {
    const size_t q = json::array_at(s, res, n, i);
    Entry &e = out.entries[kept];
    e = Entry();
    long v = 0;
    json::get_int(s, q, n, "position", v); e.pos = (int) v;
    json::get_int(s, q, n, "number", v);   e.number = (int) v;
    const size_t dr = json::find_key(s, q, n, "Driver");
    char fam[24];
    json::get_str(s, dr, n, "code", e.code, sizeof(e.code));
    json::get_str(s, dr, n, "familyName", fam, sizeof(fam));
    upper_copy(e.name, sizeof(e.name), fam);
    const size_t co = json::find_key(s, q, n, "Constructor");
    json::get_str(s, co, n, "name", e.team, sizeof(e.team));

    char status[24] = {0};
    json::get_str(s, q, n, "status", status, sizeof(status));
    // "Finished" and "+1 Lap" are results; anything else is a retirement, and
    // the row is kept and greyed rather than removed (6.3).
    e.out = status[0] && std::strcmp(status, "Finished") != 0 &&
            std::strncmp(status, "+", 1) != 0;
    const size_t tm = json::find_key(s, q, n, "Time");
    if (tm != json::NPOS)
      json::get_str(s, tm, n, "time", e.gap, sizeof(e.gap));
    else if (status[0])
      // The gap column is 12 characters wide, and a status can be longer
      // ("Collision damage"). Truncate EXPLICITLY with a precision specifier
      // rather than letting snprintf do it silently - this is the exact
      // -Wformat-truncation that decision 63's build gate exists to catch.
      std::snprintf(e.gap, sizeof(e.gap), "%.*s", (int) sizeof(e.gap) - 1, status);
    kept++;
  }
  if (kept == 0) return false;
  out.n_entries = kept;
  out.entries_mode = state::FINAL;
  out.generation++;
  return true;
}

inline bool parse_standings(const char *s, size_t n, Store &out) {
  const size_t lists = json::path(s, n, "MRData.StandingsTable.StandingsLists");
  if (lists == json::NPOS || json::array_len(s, lists, n) == 0) return false;
  const size_t l0 = json::array_at(s, lists, n, 0);
  const size_t ds = json::find_key(s, l0, n, "DriverStandings");
  if (ds == json::NPOS) return false;
  const int nd = json::array_len(s, ds, n);
  if (nd <= 0) return false;

  int kept = 0;
  for (int i = 0; i < nd && kept < MAX_STANDINGS; i++) {
    const size_t q = json::array_at(s, ds, n, i);
    Standing &st = out.standings[kept];
    st = Standing();
    long v = 0;
    json::get_int(s, q, n, "position", v); st.pos = (int) v;
    json::get_int(s, q, n, "points", v);   st.points = (int) v;
    json::get_int(s, q, n, "wins", v);     st.wins = (int) v;
    const size_t dr = json::find_key(s, q, n, "Driver");
    char fam[24];
    json::get_str(s, dr, n, "driverId", st.driver_id, sizeof(st.driver_id));
    json::get_str(s, dr, n, "code", st.code, sizeof(st.code));
    json::get_str(s, dr, n, "familyName", fam, sizeof(fam));
    upper_copy(st.name, sizeof(st.name), fam);
    const size_t cs = json::find_key(s, q, n, "Constructors");
    if (cs != json::NPOS) {
      const size_t c0 = json::array_at(s, cs, n, 0);
      if (c0 != json::NPOS) json::get_str(s, c0, n, "name", st.team, sizeof(st.team));
    }
    kept++;
  }
  if (kept == 0) return false;
  out.n_standings = kept;
  out.generation++;
  return true;
}

// Constructor nationality is a demonym, like a driver's. The table is small
// enough to live here rather than in a generator.
inline const char *team_iso3(const char *nat) {
  struct Row { const char *demonym, *iso3; };
  static const Row R[] = {
      {"British", "GBR"}, {"Italian", "ITA"}, {"Austrian", "AUT"}, {"German", "DEU"},
      {"French", "FRA"}, {"Swiss", "CHE"}, {"American", "USA"}, {"Indian", "IND"},
      {"Irish", "IRL"}, {"Japanese", "JPN"}, {"Dutch", "NLD"}, {"Spanish", "ESP"},
      {"Malaysian", "MYS"}, {"Russian", "RUS"}, {"Canadian", "CAN"},
      {"New Zealander", "NZL"}, {"Belgian", "BEL"}, {"Swedish", "SWE"},
      {"South African", "ZAF"}, {"Mexican", "MEX"}, {"Brazilian", "BRA"},
      {"Australian", "AUS"}, {"Hong Kong", "HKG"}, {"Rhodesian", "ZWE"},
      {"East German", "DEU"},
  };
  if (nat == nullptr || !*nat) return "";
  for (const auto &r : R)
    if (std::strcmp(nat, r.demonym) == 0) return r.iso3;
  return "";   // decision 20: no flag beats a wrong flag
}

inline bool parse_constructors(const char *s, size_t n, Store &out) {
  const size_t lists = json::path(s, n, "MRData.StandingsTable.StandingsLists");
  if (lists == json::NPOS || json::array_len(s, lists, n) == 0) return false;
  const size_t l0 = json::array_at(s, lists, n, 0);
  const size_t cs = json::find_key(s, l0, n, "ConstructorStandings");
  if (cs == json::NPOS) return false;
  const int nc = json::array_len(s, cs, n);
  if (nc <= 0) return false;

  char rnd[8];
  if (json::get_str(s, l0, n, "round", rnd, sizeof(rnd)))
    out.standings_round = (uint8_t) atoi(rnd);

  int kept = 0;
  const int cap = (int) (sizeof(out.constructors) / sizeof(out.constructors[0]));
  for (int i = 0; i < nc && kept < cap; i++) {
    const size_t q = json::array_at(s, cs, n, i);
    Constructor &c = out.constructors[kept];
    c = Constructor();
    long v = 0;
    json::get_int(s, q, n, "position", v); c.pos = (int) v;
    json::get_int(s, q, n, "points", v);   c.points = (int) v;
    json::get_int(s, q, n, "wins", v);     c.wins = (int) v;
    const size_t ct = json::find_key(s, q, n, "Constructor");
    json::get_str(s, ct, n, "name", c.name, sizeof(c.name));
    char nat[24];
    if (json::get_str(s, ct, n, "nationality", nat, sizeof(nat)))
      std::snprintf(c.iso3, sizeof(c.iso3), "%s", team_iso3(nat));
    kept++;
  }
  if (kept == 0) return false;
  out.n_constructors = kept;
  out.generation++;
  return true;
}

inline bool parse_fastest(const char *s, size_t n, Store &out) {
  const size_t races = json::path(s, n, "MRData.RaceTable.Races");
  if (races == json::NPOS || json::array_len(s, races, n) == 0) return false;
  const size_t r0 = json::array_at(s, races, n, 0);
  const size_t res = json::find_key(s, r0, n, "Results");
  if (res == json::NPOS || json::array_len(s, res, n) == 0) return false;
  const size_t q = json::array_at(s, res, n, 0);
  const size_t fl = json::find_key(s, q, n, "FastestLap");
  if (fl == json::NPOS) return false;

  char season[8];
  if (json::get_str(s, r0, n, "season", season, sizeof(season)))
    out.summary.season = (uint16_t) atoi(season);
  json::get_str(s, r0, n, "raceName", out.summary.event, sizeof(out.summary.event));
  const size_t dr = json::find_key(s, q, n, "Driver");
  char fam[24];
  json::get_str(s, dr, n, "familyName", fam, sizeof(fam));
  upper_copy(out.summary.fl_driver, sizeof(out.summary.fl_driver), fam);
  long lap = 0;
  json::get_int(s, fl, n, "lap", lap);
  out.summary.fl_lap = (int) lap;
  const size_t tm = json::find_key(s, fl, n, "Time");
  if (tm != json::NPOS)
    json::get_str(s, tm, n, "time", out.summary.fl_time, sizeof(out.summary.fl_time));
  if (!out.summary.fl_time[0]) return false;
  out.summary.have_fastest = true;
  out.generation++;
  return true;
}

inline bool parse_pitstops(const char *s, size_t n, Store &out) {
  const size_t races = json::path(s, n, "MRData.RaceTable.Races");
  if (races == json::NPOS || json::array_len(s, races, n) == 0) return false;
  const size_t r0 = json::array_at(s, races, n, 0);
  const size_t ps = json::find_key(s, r0, n, "PitStops");
  if (ps == json::NPOS) return false;
  const int np = json::array_len(s, ps, n);
  if (np <= 0) return false;

  double best = 1e9;
  char best_id[28] = {0};
  for (int i = 0; i < np; i++) {
    const size_t q = json::array_at(s, ps, n, i);
    double dur = 0;
    if (!json::get_double(s, q, n, "duration", dur)) continue;
    if (dur > 0 && dur < best) {
      best = dur;
      json::get_str(s, q, n, "driverId", best_id, sizeof(best_id));
    }
  }
  out.summary.n_stops = np;
  if (best < 1e9) {
    std::snprintf(out.summary.best_stop, sizeof(out.summary.best_stop), "%.3fs", best);
    // driverId -> surname, upper case, via the first dot-free segment
    const char *p = best_id;
    const char *us = std::strrchr(best_id, '_');
    if (us) p = us + 1;
    upper_copy(out.summary.best_stop_driver, sizeof(out.summary.best_stop_driver), p);
  }
  out.summary.have_stops = true;
  out.generation++;
  return true;
}

}  // namespace store
}  // namespace f1
