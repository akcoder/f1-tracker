// Host tests for the weekend state machine (section 4.1) and the OpenF1
// live-window arithmetic (NET-14). Section 10 names these the high-value
// targets, and the 04:00Z-from-Alaska case is the bug 4.1 calls most likely.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

#include "../f1-tracker/f1_state.h"

static int checks = 0, failures = 0;
static void okf(bool cond, const char *fmt, ...) {
  checks++;
  if (!cond) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

using namespace f1;
using namespace f1::state;
using calendar::Round;

static uint32_t utc(int y, int mo, int d, int h, int mi) {
  std::tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
  t.tm_hour = h; t.tm_min = mi;
#if defined(__APPLE__) || defined(__linux__)
  return (uint32_t) timegm(&t);
#else
  return (uint32_t) mktime(&t);
#endif
}

// A synthetic two-round calendar so the tests do not depend on whatever season
// the generator last fetched.
static Round make_round(uint8_t n, const char *cid, uint32_t fp1, uint32_t quali,
                        uint32_t race, uint32_t sprint = 0) {
  Round r{};
  r.round = n; r.name = "Test Grand Prix"; r.circuit_id = cid;
  r.country = "Testland"; r.iso3 = "GBR"; r.locality = "Test";
  r.start[calendar::FP1] = fp1;
  r.start[calendar::QUALI] = quali;
  r.start[calendar::RACE] = race;
  r.start[calendar::SPRINT] = sprint;
  return r;
}

static void test_alaska() {
  std::printf("the 04:00Z race read from Alaska (section 4.1)\n");
  // Melbourne, 2026-03-08 04:00 UTC. In America/Anchorage (UTC-9 in March,
  // AKDT) that is 2026-03-07 at 19:00 - the PREVIOUS EVENING. A state machine
  // driven by the device's local calendar date would show the race-day screen
  // on 2026-03-08 local, a full day late.
  const uint32_t race = utc(2026, 3, 8, 4, 0);
  const uint32_t quali = utc(2026, 3, 7, 5, 0);
  std::vector<Round> rs{make_round(1, "albert_park", utc(2026, 3, 6, 1, 30), quali, race)};
  Calendar cal{rs.data(), (int) rs.size(), 2026, true};

  // 19:30 Alaska on 7 March == 04:30 UTC on 8 March: the race IS running.
  const uint32_t during = race + 30 * 60;
  Status s = evaluate(cal, during, true);
  okf(s.weekend == RACE_LIVE, "mid-race: got %s, expected RACE_LIVE",
      weekend_name(s.weekend));

  // Local midnight in Alaska on 8 March is 09:00 UTC - the race is over, but a
  // naive "is today the race date" test would only now call it race day.
  const uint32_t local_midnight_ak = utc(2026, 3, 8, 9, 0);
  s = evaluate(cal, local_midnight_ak, true);
  okf(s.weekend != RACE_LIVE,
      "Alaska-local race day: got %s, must NOT still be RACE_LIVE",
      weekend_name(s.weekend));

  // And two hours before the start it is SESSION_SOON regardless of date.
  s = evaluate(cal, race - 3600, true);
  okf(s.weekend == SESSION_SOON, "1 h before lights out: got %s",
      weekend_name(s.weekend));
}

static void test_transitions() {
  std::printf("state transitions\n");
  const uint32_t fp1 = utc(2026, 5, 1, 11, 30);
  const uint32_t quali = utc(2026, 5, 2, 14, 0);
  const uint32_t race = utc(2026, 5, 3, 13, 0);
  std::vector<Round> rs{make_round(1, "catalunya", fp1, quali, race)};
  Calendar cal{rs.data(), (int) rs.size(), 2026, true};

  struct Case { uint32_t at; Weekend want; const char *why; };
  const Case cases[] = {
      {fp1 - 20 * 24 * 3600, IDLE,         "three weeks out"},
      {fp1 - 5 * 24 * 3600,  RACE_WEEK,    "five days out"},
      {fp1 - 3600,           SESSION_SOON, "an hour before FP1"},
      {fp1 + 600,            SESSION_LIVE, "inside FP1"},
      {race + 600,           RACE_LIVE,    "inside the race"},
      {race + RACE_LEN + 600, RACE_LIVE,   "race ended, still inside the window"},
      {race + RACE_LEN + LIVE_PAD + 60, POST_SESSION, "window closed"},
      {race + RACE_LEN + 20 * 3600, OFF_SEASON, "a day later, nothing ahead"},
  };
  for (const auto &c : cases) {
    Status s = evaluate(cal, c.at, true);
    okf(s.weekend == c.want, "%s: got %s, expected %s", c.why,
        weekend_name(s.weekend), weekend_name(c.want));
  }

  // decision 60: POST_SESSION begins when the WINDOW closes, not when the
  // session ends. One second either side of that boundary.
  Session r; r.round_idx = 0; r.session = calendar::RACE; r.start = race;
  okf(evaluate(cal, r.window_closes() - 1, true).weekend != POST_SESSION,
      "one second before window close must NOT be POST_SESSION");
  okf(evaluate(cal, r.window_closes(), true).weekend == POST_SESSION,
      "at window close must be POST_SESSION");
}

static void test_order_mode() {
  std::printf("order mode (RACE-12)\n");
  const uint32_t quali = utc(2026, 5, 2, 14, 0);
  const uint32_t race = utc(2026, 5, 3, 13, 0);
  std::vector<Round> rs{make_round(1, "catalunya", utc(2026, 5, 1, 11, 30), quali, race)};
  Calendar cal{rs.data(), (int) rs.size(), 2026, true};

  okf(evaluate(cal, quali - 3600, true).order == ENTRY_LIST,
      "before qualifying: expected ENTRY LIST");
  okf(evaluate(cal, quali + SESSION_LEN + 60, true).order == GRID_PROVISIONAL,
      "after qualifying: expected GRID (PROVISIONAL)");
  okf(evaluate(cal, race + 600, true).order == GRID_PROVISIONAL,
      "during the race the grid is still provisional - no live timing");
  Session r; r.round_idx = 0; r.session = calendar::RACE; r.start = race;
  okf(evaluate(cal, r.window_closes(), true).order == FINAL,
      "once the window closes: expected FINAL");
  // decision 61: there is no RUNNING mode at all.
  for (uint32_t t = race; t < race + RACE_LEN; t += 600)
    okf(evaluate(cal, t, true).order != GRID, "no confirmed GRID mid-race");
}

static void test_windows() {
  std::printf("OpenF1 live window (NET-14)\n");
  const uint32_t race = utc(2026, 5, 3, 13, 0);
  std::vector<Round> rs{make_round(1, "catalunya", 0, 0, race)};
  Calendar cal{rs.data(), (int) rs.size(), 2026, true};
  Session s; s.round_idx = 0; s.session = calendar::RACE; s.start = race;

  okf(s.window_opens() == race - 30 * 60, "window opens 30 min before the start");
  okf(s.window_closes() == race + RACE_LEN + 30 * 60, "window closes 30 min after the end");
  okf(openf1_readable(s, race - 31 * 60), "readable 31 min before");
  okf(!openf1_readable(s, race - 29 * 60), "NOT readable 29 min before");
  okf(!openf1_readable(s, race + 600), "NOT readable mid-session");
  okf(!openf1_readable(s, s.window_closes() - 1), "NOT readable one second early");
  okf(openf1_readable(s, s.window_closes()), "readable at window close");

  okf(!any_window_open(cal, race - 3600), "no window an hour before");
  okf(any_window_open(cal, race - 600), "window open 10 min before");
  okf(!any_window_open(cal, s.window_closes() + 1), "window shut after close");

  // Every state in which OpenF1 is polled at all must be outside every window.
  for (int w = 0; w < N_WEEKEND; w++)
    if (intervals_for((Weekend) w).openf1_s > 0)
      okf((Weekend) w == POST_SESSION,
          "%s polls OpenF1 but is not POST_SESSION", weekend_name((Weekend) w));
}

static void test_sprint_and_edges() {
  std::printf("sprints (decision 90) and edge cases\n");
  const uint32_t sprint = utc(2026, 4, 11, 14, 0);
  const uint32_t race = utc(2026, 4, 12, 13, 0);
  std::vector<Round> rs{make_round(1, "suzuka", 0, 0, race, sprint)};
  Calendar cal{rs.data(), (int) rs.size(), 2026, true};
  okf(evaluate(cal, sprint + 600, true).weekend == RACE_LIVE,
      "a sprint is a first-class race day");
  okf(is_race(calendar::SPRINT) && is_race(calendar::RACE), "both count as races");
  okf(!is_race(calendar::QUALI), "qualifying is not a race");

  // 3.4: start == 0 means UNKNOWN, not midnight. A round with no times must
  // never be treated as happening at the epoch.
  std::vector<Round> none{make_round(1, "unknown", 0, 0, 0)};
  Calendar empty{none.data(), 1, 2026, true};
  Status s = evaluate(empty, utc(2026, 6, 1, 12, 0), true);
  okf(s.weekend == OFF_SEASON, "a round with no times is OFF_SEASON, got %s",
      weekend_name(s.weekend));
  okf(!s.current.valid() && !s.next.valid(), "zero starts must not become sessions");

  // 6.7: an unsynced clock suppresses the state machine rather than guessing.
  Status u = evaluate(cal, race + 600, false);
  okf(u.weekend == OFF_SEASON, "unsynced clock must not report RACE_LIVE");
  okf(!u.clock_valid, "clock_valid must be reported");

  // An empty calendar - the months-long gap of RACE-13c - is not an error.
  Calendar nocal{nullptr, 0, 0, false};
  okf(evaluate(nocal, utc(2027, 1, 15, 12, 0), true).weekend == OFF_SEASON,
      "no calendar yet is OFF_SEASON, not a fault");
}

static void test_real_calendar() {
  std::printf("against the compiled calendar\n");
  Calendar cal;   // defaults to the generated one
  okf(cal.n > 0, "compiled calendar is present");
  // Walk the whole season in one-hour steps: the machine must never crash,
  // never report a session that has not started, and always agree with itself.
  const uint32_t first = cal.rounds[0].start[calendar::FP1]
                             ? cal.rounds[0].start[calendar::FP1]
                             : cal.rounds[0].start[calendar::RACE];
  const uint32_t last = cal.rounds[cal.n - 1].start[calendar::RACE];
  int seen[N_WEEKEND] = {0};
  for (uint32_t t = first - 10 * 24 * 3600; t < last + 5 * 24 * 3600; t += 3600) {
    Status s = evaluate(cal, t, true);
    seen[s.weekend]++;
    if (s.current.valid()) okf(s.current.start <= t, "current session is in the future");
    if (s.next.valid()) okf(s.next.start > t, "next session is in the past");
    if (s.weekend == RACE_LIVE || s.weekend == SESSION_LIVE)
      okf(!any_window_open(cal, t) || intervals_for(s.weekend).openf1_s == 0,
          "polling OpenF1 inside a live window");
  }
  std::printf("    ");
  for (int w = 0; w < N_WEEKEND; w++)
    if (seen[w]) std::printf("%s=%d  ", weekend_name((Weekend) w), seen[w]);
  std::printf("\n");
  okf(seen[RACE_LIVE] > 0, "the season contains a race");
  okf(seen[POST_SESSION] > 0, "the season contains a readable aftermath");
  okf(seen[IDLE] > 0, "the season contains quiet weeks");
}

// Sentence case (decision 153). The order list and the standings upper-case
// surnames on purpose; nothing else should shout.
static void test_case_style() {
  std::printf("sentence case\n");
  for (int m = 0; m < 4; m++) {
    const char *s = order_name((OrderMode) m);
    int upper = 0, lower = 0;
    for (const char *q = s; *q; q++) {
      if (*q >= 'A' && *q <= 'Z') upper++;
      if (*q >= 'a' && *q <= 'z') lower++;
    }
    okf(upper <= 1, "order mode '%s' has %d capitals - sentence case takes one", s, upper);
    okf(lower > 0, "order mode '%s' has no lower case at all", s);
  }
  for (int i = 0; i < calendar::N_SESSIONS; i++) {
    const char *s = session_label(i);
    const bool abbrev = (std::strlen(s) <= 3);   // FP1/FP2/FP3 are abbreviations
    int upper = 0;
    for (const char *q = s; *q; q++) if (*q >= 'A' && *q <= 'Z') upper++;
    okf(abbrev || upper <= 1, "session label '%s' is shouting", s);
  }
}

int main() {
  std::printf("\nF1 Tracker - state machine tests\n\n");
  test_alaska();
  test_transitions();
  test_order_mode();
  test_windows();
  test_sprint_and_edges();
  test_real_calendar();
  test_case_style();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
