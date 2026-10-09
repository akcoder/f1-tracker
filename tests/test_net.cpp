// What the data task asks for, and when (3.6, 3.7.4). The decision is pure -
// plan_for() and pick() - so the schedule can be simulated on the host over
// hours and days of virtual time.
//
// This file exists because the first version of the schedule had two faults
// that nothing could see without a board: one shared timer meant that once the
// calendar succeeded NOTHING else was due (qualifying, standings, constructors
// and the summary were never fetched), and POST_SESSION's results had no
// interval at all and would have been requested every ~2 s - 1,800 an hour
// against Jolpica's 500. Both look fine in a code read.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../f1-tracker/f1_net.h"

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

using namespace f1;

static store::Store g_store;
static state::Calendar cal() {
  state::Calendar c;
  c.rounds = g_store.rounds; c.n = g_store.n_rounds; c.season = g_store.season; c.fetched = true;
  return c;
}

struct Sim {
  net::Clock clk;
  int count[net::RES_N] = {0};
  int total = 0;
  bool pending = false;          // are post-session extras still missing?
  net::Res fail = net::RES_NONE; // a resource that always fails
  int fail_count = 0;

  // Mirrors task_body's loop: one request per iteration, 2 s after a request
  // and 5 s after an idle one.
  void run(uint32_t utc0, uint32_t seconds) {
    uint32_t now_ms = 0;
    while (now_ms < seconds * 1000u) {
      const uint32_t utc = utc0 + now_ms / 1000u;
      const auto st = state::evaluate(cal(), utc, true);
      const auto p = net::plan_for(cal(), st, utc, clk, now_ms, pending);
      const net::Res r = net::pick(p);
      if (r == net::RES_NONE) { now_ms += 5000; continue; }
      count[r]++; total++;
      if (r == fail) {
        fail_count++;
        clk.defer(r, now_ms, net::RETRY_AFTER_FAIL_S);
      } else {
        clk.stamp(r, now_ms);
        if (r == net::RES_EXTRAS) pending = false;
      }
      now_ms += 2000;
    }
  }
};

static uint32_t first_post_session() {
  const uint32_t race = g_store.rounds[0].start[calendar::RACE];
  for (uint32_t t = race; t < race + 12 * 3600; t += 60)
    if (state::evaluate(cal(), t, true).weekend == state::POST_SESSION) return t;
  return 0;
}

static void test_clock() {
  std::printf("the clock\n");
  net::Clock c;
  okf(c.due(net::RES_CALENDAR, 600, 0), "never fetched: due at once");
  c.stamp(net::RES_CALENDAR, 1000);
  okf(!c.due(net::RES_CALENDAR, 600, 1000 + 599 * 1000u), "not due inside the interval");
  okf(c.due(net::RES_CALENDAR, 600, 1000 + 600 * 1000u), "due on the interval");
  okf(c.due(net::RES_ROSTER, 600, 5), "each resource has its OWN timer: the calendar's stamp did not touch it");
  c.defer(net::RES_ROSTER, 5000, 60);
  okf(!c.due(net::RES_ROSTER, 600, 5000 + 59 * 1000u), "3.7.4: a failure is not retried tightly");
  okf(c.due(net::RES_ROSTER, 600, 5000 + 60 * 1000u), "...but is retried");
  // millis() wraps at ~49.7 days; a device on a wall will cross it.
  net::Clock w;
  w.stamp(net::RES_CALENDAR, 0xFFFFF000u);
  okf(!w.due(net::RES_CALENDAR, 600, 0x00000100u), "a wrapped counter does not make it instantly due");
  okf(w.due(net::RES_CALENDAR, 600, 0x00000100u + 600 * 1000u), "...and it still comes due");
}

static void test_post_session() {
  std::printf("POST_SESSION: a bounded, complete schedule\n");
  const uint32_t t0 = first_post_session();
  okf(t0 != 0, "found the moment the window closes");
  Sim s;
  s.pending = true;
  s.run(t0, 3600);
  okf(s.count[net::RES_CALENDAR] >= 1 && s.count[net::RES_ROSTER] >= 1 &&
          s.count[net::RES_RESULTS] >= 1 && s.count[net::RES_STANDINGS] >= 1 &&
          s.count[net::RES_CONSTRUCTORS] >= 1 && s.count[net::RES_SUMMARY] >= 1,
      "every resource is fetched in the first hour");
  okf(s.count[net::RES_EXTRAS] == 1, "extras fetched while pending, then never again (%d)",
      s.count[net::RES_EXTRAS]);
  okf(s.count[net::RES_RESULTS] <= 7,
      "results on a 10 min interval, not every loop: %d in an hour (was ~1800)",
      s.count[net::RES_RESULTS]);
  okf(s.total <= 60, "an hour is cheap: %d requests (Jolpica allows 500)", s.total);
  std::printf("    (%d requests in the first hour)\n", s.total);

  // A whole day at 5 min / 10 min stays far under the budget.
  Sim d;
  d.pending = true;
  d.run(t0, 12 * 3600);
  okf(d.total / 12 <= 60, "12 h of POST_SESSION averages %d requests/hour", d.total / 12);
}

static void test_race_week_regression() {
  std::printf("RACE_WEEK: the calendar's success must not starve the rest\n");
  const uint32_t race = g_store.rounds[0].start[calendar::RACE];
  Sim s;
  s.run(race - 3 * 86400u, 1800);        // three days out, half an hour
  okf(state::evaluate(cal(), race - 3 * 86400u, true).weekend == state::RACE_WEEK, "it is race week");
  okf(s.count[net::RES_CALENDAR] >= 1, "calendar fetched");
  okf(s.count[net::RES_STANDINGS] >= 1 && s.count[net::RES_CONSTRUCTORS] >= 1,
      "standings and constructors fetched AFTER the calendar succeeded");
  okf(s.count[net::RES_ROSTER] >= 1, "roster fetched");
  // Once qualifying exists the order is provisional, and it must be fetched -
  // even though the calendar just succeeded and has its own timer.
  const uint32_t t = race - 4 * 86400u;
  const auto st = state::evaluate(cal(), t, true);
  okf(st.weekend == state::RACE_WEEK, "four days out is still race week");
  net::Clock clk;
  clk.stamp(net::RES_CALENDAR, 0);
  state::Status prov = st;
  prov.order = state::GRID_PROVISIONAL;
  okf(net::plan_for(cal(), prov, t, clk, 1000, false).want_qualifying,
      "a provisional grid: qualifying is wanted though the calendar just succeeded");
  state::Status entry = st;
  entry.order = state::ENTRY_LIST;
  okf(!net::plan_for(cal(), entry, t, clk, 1000, false).want_qualifying,
      "no qualifying fetch while the order is only the entry list");
}

static void test_post_qualifying() {
  std::printf("after qualifying: the grid, not last race's result\n");
  const auto &r = g_store.rounds[0];
  const uint32_t quali = r.start[calendar::QUALI];
  okf(quali != 0, "round 1 has a qualifying session");
  const uint32_t t = quali + 3 * 3600;      // well past the window, before the race
  const auto st = state::evaluate(cal(), t, true);
  okf(st.weekend == state::POST_SESSION, "it is POST_SESSION (got %s)", state::weekend_name(st.weekend));
  okf(st.current.session == calendar::QUALI, "...of the QUALIFYING session");
  net::Clock clk;
  state::Status prov = st;
  prov.order = state::GRID_PROVISIONAL;
  const auto p = net::plan_for(cal(), prov, t, clk, 0, true);
  okf(p.want_qualifying, "the qualifying classification is wanted");
  okf(!p.want_results, "'last/results' is the PREVIOUS race's: not requested after qualifying");
  okf(!p.want_summary && !p.want_extras, "no summary and no OpenF1 extras after qualifying");
  okf(p.want_standings, "standings still are");
}

static void test_idle() {
  std::printf("IDLE: long gaps, few requests\n");
  const uint32_t t = g_store.rounds[0].start[calendar::RACE] - 20 * 86400u;
  okf(state::evaluate(cal(), t, true).weekend == state::IDLE, "it is IDLE");
  Sim s;
  s.run(t, 24 * 3600);
  okf(s.count[net::RES_CALENDAR] >= 4 && s.count[net::RES_CALENDAR] <= 5,
      "calendar every 6 h: %d in a day", s.count[net::RES_CALENDAR]);
  okf(s.count[net::RES_STANDINGS] >= 1,
      "standings are fetched in IDLE, so the page is not empty after a reboot");
  okf(s.count[net::RES_RESULTS] == 0 && s.count[net::RES_EXTRAS] == 0, "no results or OpenF1 in IDLE");
  okf(s.total <= 30, "a day of IDLE is %d requests", s.total);
}

static void test_failures() {
  std::printf("failures are retried, never hammered (3.7.4)\n");
  const uint32_t t = first_post_session();
  Sim s;
  s.fail = net::RES_RESULTS;
  s.run(t, 600);
  okf(s.fail_count >= 1 && s.fail_count <= 11,
      "a resource that always fails is attempted %d times in 10 min (60 s gate)", s.fail_count);
  okf(s.count[net::RES_STANDINGS] >= 1, "and the others carry on regardless");
}

static void test_extras_gating() {
  std::printf("OpenF1 is gated (NET-14)\n");
  const uint32_t race = g_store.rounds[0].start[calendar::RACE];
  net::Clock clk;
  // Inside the live window: nothing for OpenF1, whatever is pending.
  const auto live = state::evaluate(cal(), race + 600, true);
  const auto pl = net::plan_for(cal(), live, race + 600, clk, 0, true);
  okf(pl.openf1_blocked && !pl.want_extras, "inside the window: blocked, no extras");
  // After the window: wanted if pending, not if complete.
  const uint32_t t = first_post_session();
  const auto post = state::evaluate(cal(), t, true);
  okf(net::plan_for(cal(), post, t, clk, 0, true).want_extras, "window closed and pending: wanted");
  okf(!net::plan_for(cal(), post, t, clk, 0, false).want_extras, "complete: never wanted again");
  // A failed OpenF1 step waits out the state's own interval.
  net::Clock failed;
  failed.defer(net::RES_EXTRAS, 0, 300);
  okf(!net::plan_for(cal(), post, t, failed, 299 * 1000u, true).want_extras, "retry waits 5 min");
  okf(net::plan_for(cal(), post, t, failed, 300 * 1000u, true).want_extras, "...then retries");
}

static void test_pending() {
  std::printf("extras_pending\n");
  store::Store s;
  okf(store::extras_pending(s), "nothing gathered: pending");
  s.extras.have_session = s.extras.have_stints = s.extras.have_weather =
      s.extras.have_safety = s.extras.have_red = true;
  okf(!store::extras_pending(s), "complete, no summary yet: done");
  s.summary.race_day = 1000000u * 86400u / 1000000u * 1000u;   // some date
  s.extras.race_day = s.summary.race_day;
  okf(!store::extras_pending(s), "complete and matching: done - a finished race costs nothing more");
  s.summary.race_day += 14 * 86400u;                            // a newer race published
  okf(store::extras_pending(s), "a NEWER race on the summary: pending again");
}

int main() {
  std::printf("\nF1 Tracker - fetch schedule\n\n");
  const std::string j = slurp("jolpica-2026-races.json");
  if (!store::parse_calendar(j.data(), j.size(), g_store)) { std::printf("  FAIL  calendar\n"); return 1; }
  test_clock();
  test_post_session();
  test_race_week_regression();
  test_post_qualifying();
  test_idle();
  test_failures();
  test_extras_gating();
  test_pending();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
