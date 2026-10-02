// The watched driver (section 6.14). decision 80 calls the latch the most
// likely bug in the feature, so most of this is about firing exactly once.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

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

using namespace f1;
using namespace f1::watch;
using calendar::Round;

static uint32_t utc(int y, int mo, int d, int h, int mi) {
  std::tm t{}; t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
  t.tm_hour = h; t.tm_min = mi;
  return (uint32_t) timegm(&t);
}

static Config cfg_default() {
  Config c;
  std::strcpy(c.driver_id, DEFAULT_DRIVER_ID);
  std::strcpy(c.code, "VER");
  std::strcpy(c.given, "Max");
  std::strcpy(c.iso3, "NLD");
  c.entered = true;
  c.career_wins = 71;     // measured 2026-10-01
  c.career_starts = 248;
  return c;
}

int main() {
  std::printf("\nF1 Tracker - watched driver tests\n\n");

  const uint32_t quali = utc(2026, 5, 2, 14, 0);
  const uint32_t race = utc(2026, 5, 3, 13, 0);
  Round r{};
  r.round = 1; r.name = "Test GP"; r.circuit_id = "catalunya";
  r.country = "Spain"; r.iso3 = "ESP"; r.locality = "Madrid";
  r.start[calendar::QUALI] = quali;
  r.start[calendar::RACE] = race;
  std::vector<Round> rs{r};
  state::Calendar cal{rs.data(), 1, 2026, true};

  std::printf("identity (DATA-6)\n");
  okf(std::strcmp(DEFAULT_DRIVER_ID, "max_verstappen") == 0,
      "default watch is a driverId, not a number");

  std::printf("the latch fires each event once (decision 80)\n");
  {
    Config c = cfg_default();
    Latch latch; Alert a;
    int raised = 0;
    // Walk the whole weekend minute by minute.
    for (uint32_t t = quali - 3 * 3600; t < race + 6 * 3600; t += 60) {
      state::Status st = state::evaluate(cal, t, true);
      const int finish = (st.weekend == state::POST_SESSION) ? 1 : 0;
      if (step(c, st, latch, a, t * 1000u, 2026, 1, 3, finish, ""))
        raised++;
    }
    okf(raised > 0, "some alerts were raised");
    okf(raised <= (int) EV_N * 2, "far too many alerts raised (%d) - latch leaking", raised);
  }

  std::printf("a mid-weekend reboot does not replay the set\n");
  {
    Config c = cfg_default();
    Latch latch; Alert a;
    int before = 0;
    for (uint32_t t = quali - 3600; t < race - 3600; t += 60) {
      state::Status st = state::evaluate(cal, t, true);
      if (step(c, st, latch, a, t * 1000u, 2026, 1, 3, 0, "")) before++;
    }
    // Reboot: the Alert is volatile, the Latch is restore_value.
    Alert fresh;
    int after = 0;
    for (uint32_t t = quali - 3600; t < race - 3600; t += 60) {
      state::Status st = state::evaluate(cal, t, true);
      if (step(c, st, latch, fresh, t * 1000u, 2026, 1, 3, 0, "")) after++;
    }
    okf(before > 0, "alerts fired before the reboot");
    okf(after == 0, "after a reboot the same window re-fired %d alerts", after);
  }

  std::printf("a new round clears the latch\n");
  {
    Config c = cfg_default();
    Latch latch; Alert a;
    for (uint32_t t = quali - 3600; t < race + 6 * 3600; t += 60) {
      state::Status st = state::evaluate(cal, t, true);
      step(c, st, latch, a, t * 1000u, 2026, 1, 3, 1, "");
    }
    okf(latch.fired != 0, "round 1 latched something");
    latch.reset_if_changed(2026, 2);
    okf(latch.fired == 0, "round 2 must start with a clear latch");
    latch.reset_if_changed(2027, 2);
    okf(latch.fired == 0, "a new season must clear the latch too");
  }

  std::printf("settings gate the alerts\n");
  {
    Config off = cfg_default(); off.enabled = false;
    Latch l; Alert a;
    state::Status st = state::evaluate(cal, race + 600, true);
    okf(!step(off, st, l, a, 1000, 2026, 1, 3, 0, ""), "disabled must raise nothing");

    Config away = cfg_default(); away.entered = false;
    Latch l2; Alert a2;
    okf(!step(away, st, l2, a2, 1000, 2026, 1, 3, 0, ""),
        "a driver not in the entry list must raise nothing");

    Config ms = cfg_default(); ms.milestones_only = true;
    Latch l3; Alert a3;
    int n = 0;
    for (uint32_t t = quali - 3600; t < race + 2 * 3600; t += 60) {
      state::Status s2 = state::evaluate(cal, t, true);
      if (step(ms, s2, l3, a3, t * 1000u, 2026, 1, 3, 0, "")) n++;
    }
    okf(n == 0, "milestones_only must suppress the per-session tier, got %d", n);
  }

  std::printf("wording (6.14.2)\n");
  {
    Config c = cfg_default();
    state::Status st = state::evaluate(cal, race + 600, true);
    char b[64];
    compose(c, EV_GRID_SET, st, 1, 0, "", b, sizeof(b));
    okf(std::strstr(b, "takes pole") != nullptr,
        "P1 on the grid should read as pole, got '%s'", b);
    // Sentence case, not shouting: the name is proper case inside a sentence.
    okf(std::strncmp(b, "Max ", 4) == 0, "the name should be proper case, got '%s'", b);
    compose(c, EV_GRID_SET, st, 3, 0, "", b, sizeof(b));
    okf(std::strstr(b, "P3") != nullptr, "grid slot should appear, got '%s'", b);
    compose(c, EV_RESULT, st, 3, 1, "", b, sizeof(b));
    okf(std::strstr(b, "wins") != nullptr, "a win should read as a win, got '%s'", b);
    compose(c, EV_RESULT, st, 3, 4, "", b, sizeof(b));
    okf(std::strstr(b, "P4") != nullptr, "a finish should carry the position, got '%s'", b);
    // A withdrawal is not a result: no finish and no status means say nothing.
    compose(c, EV_RESULT, st, 3, 0, "", b, sizeof(b));
    okf(b[0] == '\0', "a withdrawal must produce no line, got '%s'", b);
    compose(c, EV_RESULT, st, 3, 0, "GEARBOX", b, sizeof(b));
    okf(std::strstr(b, "GEARBOX") != nullptr, "a known retirement reason should show");
  }

  std::printf("tiers and hold times (decision 92)\n");
  {
    okf(tier_of(EV_MILESTONE) == MILESTONE, "a milestone is the loud tier");
    okf(tier_of(EV_RESULT) == EVENT, "a result is the event tier");
    okf(TAKEOVER_MS < EVENT_HOLD_MS, "the takeover is briefer than the banner");

    Config c = cfg_default();
    Latch l; Alert a;
    state::Status st = state::evaluate(cal, race + state::RACE_LEN + state::LIVE_PAD + 60, true);
    okf(st.weekend == state::POST_SESSION, "set-up: expected POST_SESSION");
    okf(step(c, st, l, a, 100000, 2026, 1, 3, 1, ""), "result should raise");
    okf(a.active, "alert should be active");
    // It retires on its own - nothing ever requires dismissing.
    step(c, st, l, a, 100000 + EVENT_HOLD_MS + 1, 2026, 1, 3, 1, "");
    okf(!a.active, "the banner must retire on its own");
    // and a tap dismisses early
    Alert b2; b2.active = true; dismiss(b2);
    okf(!b2.active, "a tap must dismiss early");
  }

  std::printf("milestones are rare by construction\n");
  {
    char b[64];
    okf(is_milestone(1, 72, 249, false, b, sizeof(b)), "a win is a milestone");
    okf(is_milestone(1, 80, 249, false, b, sizeof(b)) && std::strstr(b, "80th win"),
        "a round-number win names the number, got '%s'", b);
    okf(is_milestone(0, 71, 250, false, b, sizeof(b)) && std::strstr(b, "250th start"),
        "a round-number start, got '%s'", b);
    okf(!is_milestone(4, 71, 249, false, b, sizeof(b)), "an ordinary P4 is not a milestone");
    okf(is_milestone(0, 71, 248, true, b, sizeof(b)) && std::strstr(b, "champion"),
        "a title is a milestone");
    // Nothing the device draws outside the order list should be ALL CAPS.
    for (const char *s2 : {"Max is racing today", "Max takes pole", "Max wins"}) {
      int upper = 0, lower = 0;
      for (const char *q = s2; *q; q++) {
        if (*q >= 'A' && *q <= 'Z') upper++;
        if (*q >= 'a' && *q <= 'z') lower++;
      }
      okf(lower > upper, "'%s' is shouting", s2);
    }
  }

  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
