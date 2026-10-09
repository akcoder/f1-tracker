// Decision 98: a season rollover must need no firmware update.
//
// The device is meant to sit on a wall for years. This feeds the data layer a
// `current` that advances a year MID-RUN and asserts the four things the
// decision names - the calendar, the entry list, the driver carousel and the
// watched driver - all follow, with nothing rebuilt in between.
//
// 2027 does not exist yet (open question 14), so it is DERIVED from the real
// 2026 fixtures by the smallest edit that makes the point: dates move a year,
// one driver leaves, one rookie arrives. That is exactly the shape of a real
// winter, and every other byte is real.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

#include "../f1-tracker/f1_carousel.h"
#include "../f1-tracker/f1_json.h"
#include "../f1-tracker/f1_legends.h"
#include "../f1-tracker/f1_roster.h"
#include "../f1-tracker/f1_state.h"
#include "../f1-tracker/f1_store.h"
#include "../f1-tracker/f1_watch.h"

static int checks = 0, failures = 0;
static void okf(bool cond, const char *fmt, ...) {
  checks++;
  if (!cond) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

static std::string slurp(const char *rel) {
  std::string p = std::string("../reference/samples/") + rel;
  FILE *f = std::fopen(p.c_str(), "rb");
  if (!f) { std::printf("  FAIL  cannot open %s\n", p.c_str()); failures++; return {}; }
  std::string out; char buf[8192]; size_t r;
  while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, r);
  std::fclose(f);
  return out;
}

static std::string replace_all(std::string s, const std::string &a, const std::string &b) {
  for (size_t i = 0; (i = s.find(a, i)) != std::string::npos; i += b.size()) s.replace(i, a.size(), b);
  return s;
}

using namespace f1;

static uint32_t utc(int y, int mo, int d, int h, int mi) {
  char dd[16], tt[16];
  std::snprintf(dd, sizeof(dd), "%04d-%02d-%02d", y, mo, d);
  std::snprintf(tt, sizeof(tt), "%02d:%02d:00", h, mi);
  return store::to_epoch(dd, tt);
}

static state::Calendar cal_of(const store::Store &s) {
  state::Calendar c;
  c.rounds = s.rounds; c.n = s.n_rounds; c.season = s.season; c.fetched = true;
  return c;
}

static int driver_index(const char *id) {
  for (int i = 0; i < drivers::N; i++) if (std::strcmp(drivers::P[i].driver_id, id) == 0) return i;
  return -1;
}

static void test_calendar() {
  std::printf("the calendar follows\n");
  const std::string j26 = slurp("jolpica-2026-races.json");
  const std::string j27 = replace_all(j26, "2026", "2027");

  store::Store st;
  okf(store::parse_calendar(j26.data(), j26.size(), st) && st.season == 2026, "2026 parses");
  const int n26 = st.n_rounds;

  // The winter: the last 2026 round has run and nothing is published beyond it.
  const uint32_t winter = utc(2026, 12, 20, 12, 0);
  state::Status before = state::evaluate(cal_of(st), winter, true);
  okf(before.weekend == state::OFF_SEASON, "after the last round: OFF_SEASON (got %s)",
      state::weekend_name(before.weekend));
  okf(!before.next.valid(), "no future session anywhere (RACE-13c)");

  // `current` advances. The very same clock reading, a new calendar.
  okf(store::parse_calendar(j27.data(), j27.size(), st), "2027 parses");
  okf(st.season == 2027, "the store follows the season, got %u", (unsigned) st.season);
  okf(st.n_rounds == n26, "every round replaced, not appended (%d vs %d)", st.n_rounds, n26);
  state::Status after = state::evaluate(cal_of(st), winter, true);
  okf(after.weekend == state::IDLE, "same instant, new calendar: IDLE, got %s",
      state::weekend_name(after.weekend));
  okf(after.next.valid() && after.next.round_idx == 0,
      "the next session is round 1 of 2027");

  // ...and it carries through to race week with no rebuild.
  const uint32_t r1 = st.rounds[0].start[calendar::RACE];
  state::Status week = state::evaluate(cal_of(st), r1 - 3 * 86400u, true);
  okf(week.weekend == state::RACE_WEEK, "three days before 2027 round 1: RACE_WEEK, got %s",
      state::weekend_name(week.weekend));
  okf(r1 > utc(2027, 1, 1, 0, 0), "and it really is a 2027 date");

  // A calendar that arrives EMPTY (the gap between seasons) must not wipe it.
  const char *empty = R"({"MRData":{"RaceTable":{"season":"2028","Races":[]}}})";
  const uint32_t gen = st.generation;
  okf(!store::parse_calendar(empty, std::strlen(empty), st), "an empty season is not a calendar");
  okf(st.season == 2027 || st.season == 2028, "(season field may move, rounds must not)");
  okf(st.n_rounds == n26 && st.generation == gen, "...and the 2027 rounds survive it");
}

static void test_entry_list() {
  std::printf("the entry list follows\n");
  store::Store st;
  const std::string q26 = slurp("jolpica-2025-last-qualifying.json");
  okf(store::parse_qualifying(q26.data(), q26.size(), st), "a season's grid parses");
  const int n_old = st.n_entries;
  okf(n_old >= 20, "a full grid (%d)", n_old);

  // The next season's first grid: smaller, different, and it REPLACES.
  const char *q27 =
      R"({"MRData":{"RaceTable":{"season":"2027","Races":[{"QualifyingResults":[)"
      R"({"number":"3","position":"1","Driver":{"code":"VER","familyName":"Verstappen"},"Constructor":{"name":"Red Bull"}},)"
      R"({"number":"99","position":"2","Driver":{"code":"KID","familyName":"Rookie"},"Constructor":{"name":"Cadillac"}},)"
      R"({"number":"1","position":"3","Driver":{"code":"NOR","familyName":"Norris"},"Constructor":{"name":"McLaren"}}]}]}}})";
  okf(store::parse_qualifying(q27, std::strlen(q27), st), "2027 grid parses");
  okf(st.n_entries == 3, "the old grid is gone, not merged: %d entries (was %d)", st.n_entries, n_old);
  okf(std::strcmp(st.entries[1].code, "KID") == 0 && st.entries[1].number == 99,
      "the rookie is on the grid");
  okf(st.entries_mode == state::GRID_PROVISIONAL, "provisional until results confirm it");
}

static roster::View roster_of(const std::string &json) {
  store::Store st;
  roster::View v;
  if (store::parse_roster(json.data(), json.size(), st)) roster::build(st, v);
  return v;
}

static void test_roster_and_carousel() {
  std::printf("the driver carousel follows (RACE-13d/13e)\n");
  const std::string d26 = slurp("jolpica-2026-drivers.json");

  // 2027: Alonso retires, a rookie arrives. Names and codes edited in place.
  std::string d27 = replace_all(d26, "\"season\":\"2026\"", "\"season\":\"2027\"");
  d27 = replace_all(d27, "\"driverId\":\"alonso\"", "\"driverId\":\"rookie_kid\"");
  d27 = replace_all(d27, "\"code\":\"ALO\"", "\"code\":\"KID\"");
  d27 = replace_all(d27, "\"givenName\":\"Fernando\"", "\"givenName\":\"Kid\"");
  d27 = replace_all(d27, "\"familyName\":\"Alonso\"", "\"familyName\":\"Rookie\"");

  const roster::View cold;                       // no network yet
  const roster::View v26 = roster_of(d26);
  const roster::View v27 = roster_of(d27);

  okf(v26.n == 23, "DATA-10: 23 racing drivers of the 32 rows, got %d", v26.n);
  okf(v27.season == 2027 && v26.season == 2026, "the roster knows its season");

  // The floor: before any fetch the compiled table answers.
  okf(roster::racing(cold, "alonso") && roster::racing(cold, "max_verstappen"),
      "cold boot: the compiled table is the floor");
  okf(!roster::racing(cold, "senna"), "...and a legend is not racing");

  okf(roster::racing(v26, "alonso"), "2026: Alonso is racing");
  okf(!roster::racing(v27, "alonso"), "2027: Alonso is NOT racing - the compiled table says he is");
  okf(roster::racing(v27, "max_verstappen") && roster::racing(v27, "hamilton"),
      "everyone else still is");

  // The overlap rule, per card: a legend's card appears exactly when they stop.
  const int alo = driver_index("alonso");
  okf(alo >= 0, "Alonso is in the compiled table");
  okf(roster::slot_kind(v26, alo) == roster::SLOT_COMPILED, "2026: a driver card");
  okf(roster::slot_kind(v27, alo) == roster::SLOT_RETIRED,
      "2027: NO driver card - a stale card for someone not racing is the bug");
  bool alo_legend = false;
  for (int i = 0; i < legends::N; i++) if (!std::strcmp(legends::P[i].driver_id, "alonso")) alo_legend = true;
  okf(alo_legend, "Alonso is on the legends list");
  okf(!roster::racing(v26, "alonso") == false && !roster::racing(v27, "alonso"),
      "so his LEGEND card (shown when !racing) appears in 2027 and not in 2026");

  // RACE-13e: a rookie with no compiled profile still gets a card.
  okf(roster::live_only_count(v26) == 0, "2026: nobody is missing from the compiled table");
  okf(roster::live_only_count(v27) == 1, "2027: one rookie, got %d", roster::live_only_count(v27));
  const store::RosterDriver *kid = roster::live_only(v27, 0);
  okf(kid && !std::strcmp(kid->id, "rookie_kid") && kid->number == 14 &&
          !std::strcmp(kid->code, "KID") && !std::strcmp(kid->family, "Rookie"),
      "the rookie's card is built from what the roster knows");
  okf(roster::driver_slots(v27) == drivers::N + 1, "one extra slot in the rotation");
  okf(roster::slot_kind(v27, drivers::N) == roster::SLOT_ROOKIE, "...and it is a rookie slot");
  okf(roster::slot_kind(v27, drivers::N + 1) == roster::SLOT_NONE, "...and no further");

  // Walk the real rotation and see which driver cards a viewer would get.
  auto shown = [&](const roster::View &v) {
    carousel::Rotation rot;
    rot.configure(41, roster::driver_slots(v), legends::N, carousel::DRIVERS_ONLY);
    std::set<std::string> ids;
    carousel::Card c;
    for (int i = 0; i < 200 && rot.next(c); i++) {
      if (c.type != carousel::DRIVER) continue;
      const auto k = roster::slot_kind(v, c.index);
      if (k == roster::SLOT_COMPILED) ids.insert(drivers::P[c.index].driver_id);
      else if (k == roster::SLOT_ROOKIE) ids.insert(roster::live_only(v, c.index - drivers::N)->id);
    }
    return ids;
  };
  const auto s26 = shown(v26), s27 = shown(v27);
  okf((int) s26.size() == 23 && s26.count("alonso"), "2026: all 23 drivers are shown");
  okf((int) s27.size() == 23, "2027: still 23 cards, got %d", (int) s27.size());
  okf(!s27.count("alonso") && s27.count("rookie_kid"), "2027: Alonso is gone, the rookie is in");

  // Same roster, no churn: the staged card must not be reset every tick.
  okf(roster::same(v26, roster_of(d26)), "an identical roster compares equal");
  okf(!roster::same(v26, v27), "a changed roster compares different");

  // The guard: a feed that briefly returns three drivers must not retire twenty.
  const char *tiny = R"({"MRData":{"DriverTable":{"season":"2027","Drivers":[)"
      R"({"driverId":"a","code":"AAA","permanentNumber":"1"},{"driverId":"b","code":"BBB","permanentNumber":"2"},)"
      R"({"driverId":"c","code":"CCC","permanentNumber":"3"}]}}})";
  store::Store st;
  store::parse_roster(d26.data(), d26.size(), st);
  const int before = st.n_roster;
  okf(!store::parse_roster(tiny, std::strlen(tiny), st), "a three-driver roster is refused");
  okf(st.n_roster == before, "...and the real roster is kept");

  // DATA-10: reserves with no code and no number are not racing.
  okf(!roster::racing(v26, "paul_aron"), "a reserve with no number is not on the grid");
}

static void test_watched() {
  std::printf("the watched driver follows (DATA-6)\n");
  const std::string d26 = slurp("jolpica-2026-drivers.json");
  const std::string d27 = replace_all(
      replace_all(d26, "\"season\":\"2026\"", "\"season\":\"2027\""),
      "\"driverId\":\"max_verstappen\"", "\"driverId\":\"someone_else\"");
  const roster::View v26 = roster_of(d26), v27 = roster_of(d27), cold;

  const char *id = watch::DEFAULT_DRIVER_ID;
  okf(roster::watched_entered(v26, id, false), "2026: he is entered, whatever the previous value");
  okf(!roster::watched_entered(v27, id, true),
      "2027, he has left the grid: no longer entered, even though it was true before");
  okf(roster::watched_entered(cold, id, true), "no roster yet: the previous answer stands");
  okf(!roster::watched_entered(cold, id, false), "...in both directions");

  // And the consequence: an un-entered watched driver raises nothing.
  const std::string rj = slurp("jolpica-2026-races.json");
  store::Store cs;
  store::parse_calendar(rj.data(), rj.size(), cs);
  watch::Config cfg;
  std::snprintf(cfg.driver_id, sizeof(cfg.driver_id), "%s", id);
  std::snprintf(cfg.code, sizeof(cfg.code), "VER");
  cfg.entered = true;
  const uint32_t race = cs.rounds[0].start[calendar::RACE];
  const state::Status live = state::evaluate(cal_of(cs), race + 600, true);
  watch::Latch latch;
  int ev = -1;
  const watch::Event on = watch::due(cfg, live, latch, 0, ev);
  okf(on != watch::EV_NONE, "entered: an alert is due during the race");
  cfg.entered = roster::watched_entered(v27, id, cfg.entered);
  okf(watch::due(cfg, live, latch, 0, ev) == watch::EV_NONE,
      "after the rollover he is not entered: the same moment raises nothing");
}

int main() {
  std::printf("\nF1 Tracker - season rollover (decision 98)\n\n");
  test_calendar();
  test_entry_list();
  test_roster_and_carousel();
  test_watched();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
