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
  bool favourite = false;      // 8.2: a row of the favourite team
};

inline constexpr int MAX_ROUNDS = 26;
inline constexpr int MAX_ENTRIES = 24;
inline constexpr int MAX_STANDINGS = 24;
inline constexpr int MAX_CONSTRUCTORS = 12;

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
  uint32_t race_day = 0;          // epoch of the race's UTC date (Jolpica `date`)
  bool have_fastest = false;
  bool have_stops = false;
};

// 8.1 / decision 134: the parts of the post-session summary that DO need
// OpenF1 - tyre strategy, weather, and the safety cars and red flags that
// occurred. All four endpoints are free once the live window has closed
// (NET-14), and all are fetched ONCE per race: the 5 min interval is for
// retrying until it appears, never for refreshing (3.6.2).
inline constexpr int MAX_STINTS = 6;
inline constexpr int MAX_STINT_DRIVERS = 24;
inline constexpr int MAX_INCIDENTS = 8;

struct DriverStints {
  int number = 0;                       // OpenF1 driver_number
  int n = 0;
  char compound[MAX_STINTS] = {0};      // S M H I W, or '?' when unrecognised
  uint8_t laps[MAX_STINTS] = {0};
};

struct Incident {
  uint8_t lap = 0;
  char kind = 0;                        // 'S' safety car, 'V' virtual, 'R' red flag
};

struct Extras {
  uint32_t session_key = 0;             // the Race session these belong to
  uint32_t race_day = 0;                // epoch of its UTC date, for the match
  bool have_session = false;
  bool have_stints = false;
  bool have_weather = false;
  bool have_safety = false;             // "none occurred" is a valid answer
  bool have_red = false;

  DriverStints stints[MAX_STINT_DRIVERS];
  int n_stints = 0;
  float air_min = 0, air_max = 0, track_min = 0, track_max = 0;
  bool rain = false;
  Incident incidents[MAX_INCIDENTS];
  int n_incidents = 0;

  bool complete() const {
    return have_session && have_stints && have_weather && have_safety && have_red;
  }
};

// RACE-13d/13e: who is racing THIS season, from Jolpica's season driver list
// at runtime. It replaces the compiled table as the answer to "is this driver
// current?", so a retirement or a rookie needs no rebuild (decision 98).
inline constexpr int MAX_ROSTER = 32;

struct RosterDriver {
  char id[28] = {0};           // driverId - the only stable key (DATA-6)
  char code[8] = {0};
  char given[20] = {0};
  char family[24] = {0};
  char iso3[4] = {0};          // "" = draw NO flag (decision 20)
  int number = 0;
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

  Constructor constructors[MAX_CONSTRUCTORS];
  int n_constructors = 0;
  uint8_t standings_round = 0;    // which round the table is up to date through
  Summary summary;
  Extras extras;
  RosterDriver roster[MAX_ROSTER];
  int n_roster = 0;
  uint16_t roster_season = 0;
  uint32_t parse_now = 0;       // UTC, set by the caller before parse_sessions
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
  char day[16];
  if (json::get_str(s, r0, n, "date", day, sizeof(day)))
    out.summary.race_day = to_epoch(day, "00:00:00");
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

// ---- OpenF1, post-session (8.1, decision 134) ----------------------------

// OpenF1 answers a filter that matches nothing with HTTP 404 and
// {"detail":"No results found."} - measured with flag=RED on a race that had no
// red flag. That is an EMPTY RESULT, not a fault: treating it as an API error
// would back the whole task off for five minutes over a clean race, and a clean
// race is the common case. Pure so the host can test it; the fetch calls it.
inline bool openf1_no_results(int status, const char *body, size_t n) {
  if (status != 404 || body == nullptr) return false;
  for (size_t i = 0; i + 10 <= n; i++)
    if (std::strncmp(body + i, "No results", 10) == 0) return true;
  return false;
}

// The '[' of a top-level array, or NPOS.
inline size_t root_array(const char *s, size_t n) {
  size_t i = 0;
  while (i < n && s[i] != '[' && s[i] != '{') i++;
  return (i < n && s[i] == '[') ? i : json::NPOS;
}

// "2026-03-08T06:00:00+00:00" -> UTC epoch. OpenF1 always sends +00:00 here.
inline uint32_t iso_to_epoch(const char *iso) {
  if (!iso || std::strlen(iso) < 19 || iso[10] != 'T') return 0;
  char d[11];
  std::memcpy(d, iso, 10); d[10] = '\0';
  return to_epoch(d, iso + 11);
}

// Finds the most recent GRAND PRIX whose live window has closed. `session_type`
// is "Race" for sprints too - measured, 2026 round 2 carries a Sprint under
// that type - so the name is what separates them. Needs `parse_now`.
inline bool parse_sessions(const char *s, size_t n, Store &out) {
  const size_t arr = root_array(s, n);
  if (arr == json::NPOS || out.parse_now == 0) return false;
  const int nr = json::array_len(s, arr, n);
  uint32_t best_end = 0, best_key = 0, best_day = 0;
  for (int i = 0; i < nr; i++) {
    const size_t o = json::array_at(s, arr, n, i);
    char name[16], end[32], start[32];
    if (!json::get_str(s, o, n, "session_name", name, sizeof(name)) ||
        std::strcmp(name, "Race") != 0) continue;
    const size_t c = json::find_key(s, o, n, "is_cancelled");
    if (c != json::NPOS && s[c] == 't') continue;
    if (!json::get_str(s, o, n, "date_end", end, sizeof(end))) continue;
    const uint32_t e = iso_to_epoch(end);
    // NET-14: only a session whose window has CLOSED is readable on this tier.
    if (e == 0 || e + 30 * 60 > out.parse_now) continue;
    long key = 0;
    if (!json::get_int(s, o, n, "session_key", key) || key <= 0) continue;
    if (e > best_end) {
      best_end = e; best_key = (uint32_t) key;
      best_day = json::get_str(s, o, n, "date_start", start, sizeof(start))
                     ? iso_to_epoch(start) / 86400u * 86400u : 0;
    }
  }
  if (best_key == 0) return false;
  if (best_key != out.extras.session_key) out.extras = Extras();   // a new race
  out.extras.session_key = best_key;
  out.extras.race_day = best_day;
  out.extras.have_session = true;
  out.generation++;
  return true;
}

inline char compound_letter(const char *c) {
  if (!std::strcmp(c, "SOFT")) return 'S';
  if (!std::strcmp(c, "MEDIUM")) return 'M';
  if (!std::strcmp(c, "HARD")) return 'H';
  if (!std::strcmp(c, "INTERMEDIATE")) return 'I';
  if (!std::strcmp(c, "WET")) return 'W';
  return '?';
}

inline bool parse_stints(const char *s, size_t n, Store &out) {
  const size_t arr = root_array(s, n);
  if (arr == json::NPOS) return false;
  const int nr = json::array_len(s, arr, n);
  if (nr <= 0) return false;
  Extras &x = out.extras;
  x.n_stints = 0;
  for (int i = 0; i < MAX_STINT_DRIVERS; i++) x.stints[i] = DriverStints();
  for (int i = 0; i < nr; i++) {
    const size_t o = json::array_at(s, arr, n, i);
    long dn = 0, sn = 0, l0 = 0, l1 = 0;
    char comp[16];
    if (!json::get_int(s, o, n, "driver_number", dn) ||
        !json::get_int(s, o, n, "stint_number", sn) || sn < 1 || sn > MAX_STINTS) continue;
    DriverStints *d = nullptr;
    for (int k = 0; k < x.n_stints; k++) if (x.stints[k].number == dn) d = &x.stints[k];
    if (!d) {
      if (x.n_stints >= MAX_STINT_DRIVERS) continue;
      d = &x.stints[x.n_stints++];
      d->number = (int) dn;
    }
    const int idx = (int) sn - 1;
    d->compound[idx] = json::get_str(s, o, n, "compound", comp, sizeof(comp))
                           ? compound_letter(comp) : '?';
    // lap_end is null for a stint still running; the session is over here, but
    // a missing value is shown as an unknown length (0), never a guessed one.
    int laps = 0;
    if (json::get_int(s, o, n, "lap_start", l0) && json::get_int(s, o, n, "lap_end", l1) &&
        l1 >= l0)
      laps = (int) (l1 - l0 + 1);
    d->laps[idx] = (uint8_t) (laps > 255 ? 255 : laps);
    if (idx + 1 > d->n) d->n = idx + 1;
  }
  if (x.n_stints == 0) return false;
  x.have_stints = true;
  out.generation++;
  return true;
}

inline bool parse_weather(const char *s, size_t n, Store &out) {
  const size_t arr = root_array(s, n);
  if (arr == json::NPOS) return false;
  const int nr = json::array_len(s, arr, n);
  if (nr <= 0) return false;
  Extras &x = out.extras;
  bool any = false;
  double amin = 1e9, amax = -1e9, tmin = 1e9, tmax = -1e9;
  x.rain = false;
  for (int i = 0; i < nr; i++) {
    const size_t o = json::array_at(s, arr, n, i);
    double a = 0, t, r;
    const bool have_air = json::get_double(s, o, n, "air_temperature", a);
    if (have_air) {
      any = true; if (a < amin) amin = a; if (a > amax) amax = a;
    }
    // DATA-12: the track sensor drops out to exactly 0.0 - measured, 2 of 168
    // rows in session 11377, with the air at 26 C. Left in, the summary would
    // say "Track 0-48" and read as correct data. A zero beside warm air is a
    // dropout, not a reading.
    if (json::get_double(s, o, n, "track_temperature", t) && !(t <= 0 && have_air && a > 5)) {
      if (t < tmin) tmin = t; if (t > tmax) tmax = t;
    }
    if (json::get_double(s, o, n, "rainfall", r) && r > 0) x.rain = true;
  }
  if (!any || tmin > tmax) return false;
  x.air_min = (float) amin; x.air_max = (float) amax;
  x.track_min = (float) tmin; x.track_max = (float) tmax;
  x.have_weather = true;
  out.generation++;
  return true;
}

// Safety cars and red flags share one list, in lap order as they arrive.
inline void add_incident(Extras &x, int lap, char kind) {
  if (x.n_incidents >= MAX_INCIDENTS) return;
  x.incidents[x.n_incidents].lap = (uint8_t) (lap < 0 ? 0 : lap > 255 ? 255 : lap);
  x.incidents[x.n_incidents].kind = kind;
  x.n_incidents++;
}

// An empty array is a VALID answer here - a clean race - unlike every other
// parser in this file, where empty means "failed poll".
inline bool parse_safety(const char *s, size_t n, Store &out) {
  const size_t arr = root_array(s, n);
  if (arr == json::NPOS) return false;
  const int nr = json::array_len(s, arr, n);
  Extras &x = out.extras;
  for (int i = 0; i < nr; i++) {
    const size_t o = json::array_at(s, arr, n, i);
    char msg[48];
    if (!json::get_str(s, o, n, "message", msg, sizeof(msg))) continue;
    if (!std::strstr(msg, "DEPLOYED")) continue;      // count deployments, not "in this lap"
    long lap = 0;
    json::get_int(s, o, n, "lap_number", lap);
    add_incident(x, (int) lap, std::strstr(msg, "VIRTUAL") ? 'V' : 'S');
  }
  x.have_safety = true;
  out.generation++;
  return true;
}

inline bool parse_red(const char *s, size_t n, Store &out) {
  const size_t arr = root_array(s, n);
  if (arr == json::NPOS) return false;
  const int nr = json::array_len(s, arr, n);
  Extras &x = out.extras;
  for (int i = 0; i < nr; i++) {
    const size_t o = json::array_at(s, arr, n, i);
    char scope[16];
    // A red flag is a TRACK-wide event; sector or driver-scoped rows are not.
    if (json::get_str(s, o, n, "scope", scope, sizeof(scope)) && std::strcmp(scope, "Track") != 0)
      continue;
    long lap = 0;
    json::get_int(s, o, n, "lap_number", lap);
    add_incident(x, (int) lap, 'R');
  }
  x.have_red = true;
  out.generation++;
  return true;
}

// The extras describe the race OpenF1 found; the summary describes the race
// Jolpica last published. Around a race's end they can disagree for a while,
// and showing one race's tyres under another race's fastest lap is the kind of
// error that looks like correct data. Show the extras only when they match.
inline bool extras_match_summary(const Store &s) {
  if (!s.extras.have_session || s.summary.race_day == 0 || s.extras.race_day == 0) return false;
  const uint32_t a = s.summary.race_day, b = s.extras.race_day;
  return (a > b ? a - b : b - a) <= 86400u;
}

// Appends the OpenF1 half of the summary to `b`. Pure, so the host tests pin the
// exact text. Draws NOTHING unless the extras belong to the race on the page
// (extras_match_summary) - a blank is correct, another race's tyres are not.
// `podium` carries the first three finishers' OpenF1 driver numbers and codes.
struct PodiumRef { int number = 0; char code[8] = {0}; };

inline int format_extras(const Store &st, const PodiumRef podium[3], char *b, size_t n) {
  if (n == 0) return 0;
  b[0] = '\0';
  if (!extras_match_summary(st)) return 0;
  const Extras &x = st.extras;
  int k = 0;
  auto put = [&](const char *label, const char *text) {
    k += std::snprintf(b + k, n - (size_t) k, "%-16s %s\n", label, text);
  };
  char t[64];

  if (x.have_stints) {
    bool first = true;
    for (int i = 0; i < 3; i++) {
      if (podium[i].number == 0) continue;
      const DriverStints *d = nullptr;
      for (int j = 0; j < x.n_stints; j++) if (x.stints[j].number == podium[i].number) d = &x.stints[j];
      if (!d || d->n == 0) continue;
      int m = std::snprintf(t, sizeof(t), "%s", podium[i].code[0] ? podium[i].code : "?");
      for (int j = 0; j < d->n && m < (int) sizeof(t) - 6; j++) {
        if (!d->compound[j]) continue;                 // a stint with no data
        if (d->laps[j]) m += std::snprintf(t + m, sizeof(t) - (size_t) m, " %c%d", d->compound[j], d->laps[j]);
        else m += std::snprintf(t + m, sizeof(t) - (size_t) m, " %c", d->compound[j]);
      }
      put(first ? "Tyres" : "", t);
      first = false;
    }
  }
  if (x.have_weather) {
    std::snprintf(t, sizeof(t), "Track %.0f-%.0f\xC2\xB0" "C, air %.0f-%.0f\xC2\xB0" "C, %s",
                  (double) x.track_min, (double) x.track_max, (double) x.air_min,
                  (double) x.air_max, x.rain ? "rain" : "dry");
    put("Weather", t);
  }
  if (x.have_safety && x.have_red) {
    if (x.n_incidents == 0) {
      put("Incidents", "No safety car or red flag");
    } else {
      Incident sorted[MAX_INCIDENTS];
      for (int i = 0; i < x.n_incidents; i++) sorted[i] = x.incidents[i];
      for (int i = 1; i < x.n_incidents; i++)          // insertion sort by lap
        for (int j = i; j > 0 && sorted[j].lap < sorted[j - 1].lap; j--) {
          const Incident tmp = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = tmp;
        }
      for (int i = 0; i < x.n_incidents && i < 4; i++) {
        const char *what = sorted[i].kind == 'R' ? "Red flag" : sorted[i].kind == 'V' ? "Virtual safety car" : "Safety car";
        if (sorted[i].lap) std::snprintf(t, sizeof(t), "%s, lap %d", what, sorted[i].lap);
        else std::snprintf(t, sizeof(t), "%s", what);
        put(i == 0 ? "Incidents" : "", t);
      }
    }
  }
  return k;
}

// The season's driver list -> the roster (RACE-13d/e).
//
// DATA-10: the endpoint returns 32 rows for 2026 but only 23 are racing; the
// rest are reserves with no code and no number. Only rows carrying BOTH count.
// And a short list is refused outright: a feed that briefly returned three
// drivers would otherwise mark the other twenty as retired, and the carousel
// would quietly drop them - a plausible, confidently wrong result.
inline constexpr int MIN_PLAUSIBLE_ROSTER = 10;

inline bool parse_roster(const char *s, size_t n, Store &out) {
  const size_t tbl = json::path(s, n, "MRData.DriverTable");
  if (tbl == json::NPOS) return false;
  const size_t arr = json::find_key(s, tbl, n, "Drivers");
  if (arr == json::NPOS) return false;
  const int nd = json::array_len(s, arr, n);
  RosterDriver tmp[MAX_ROSTER];
  int kept = 0;
  for (int i = 0; i < nd && kept < MAX_ROSTER; i++) {
    const size_t o = json::array_at(s, arr, n, i);
    RosterDriver &r = tmp[kept];
    long num = 0;
    if (!json::get_str(s, o, n, "driverId", r.id, sizeof(r.id)) || !r.id[0]) continue;
    if (!json::get_str(s, o, n, "code", r.code, sizeof(r.code)) || !r.code[0]) continue;
    if (!json::get_int(s, o, n, "permanentNumber", num) || num <= 0) continue;
    r.number = (int) num;
    json::get_str(s, o, n, "givenName", r.given, sizeof(r.given));
    json::get_str(s, o, n, "familyName", r.family, sizeof(r.family));
    char nat[24];
    if (json::get_str(s, o, n, "nationality", nat, sizeof(nat)))   // often absent (3.4)
      std::snprintf(r.iso3, sizeof(r.iso3), "%s", team_iso3(nat));
    kept++;
  }
  if (kept < MIN_PLAUSIBLE_ROSTER) return false;
  for (int i = 0; i < kept; i++) out.roster[i] = tmp[i];
  for (int i = kept; i < MAX_ROSTER; i++) out.roster[i] = RosterDriver();
  out.n_roster = kept;
  char season[8];
  out.roster_season = json::get_str(s, tbl, n, "season", season, sizeof(season))
                          ? (uint16_t) atoi(season) : 0;
  out.generation++;
  return true;
}

// Does the store still lack a piece of the post-session extras for the race on
// the summary? False once complete AND matching, so a finished race costs no
// further OpenF1 requests - the 5 min interval is for retrying, not refreshing.
inline bool extras_pending(const Store &s) {
  if (!s.extras.complete()) return true;
  return s.summary.race_day != 0 && !extras_match_summary(s);
}

}  // namespace store
}  // namespace f1
