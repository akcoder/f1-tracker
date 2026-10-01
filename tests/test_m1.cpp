// Host tests for M1: circuit geometry, the circuitId map, and the carousel
// rotation. REQUIREMENTS.md section 10.
//
// These build on the host because f1_map.h and f1_carousel.h are free of LVGL
// and ESPHome - which is the whole reason they are separate from the drawing
// code.  make -C tests
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "../f1-tracker/f1_calendar.h"
#include "../f1-tracker/f1_carousel.h"
#include "../f1-tracker/f1_map.h"

static int checks = 0, failures = 0;

static void ok(bool cond, const char *what) {
  checks++;
  if (!cond) { failures++; std::printf("  FAIL  %s\n", what); }
}
static void okf(bool cond, const char *fmt, ...) {
  checks++;
  if (!cond) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n");
    va_end(ap);
  }
}

using namespace f1;

// ---------------------------------------------------------------- geometry
static void test_geometry() {
  std::printf("circuit geometry (MAP-2, MAP-3)\n");
  ok(circuits::N == 40, "40 circuits shipped (decision 22)");

  for (int i = 0; i < circuits::N; i++) {
    const auto &c = circuits::C[i];
    okf(c.n_pts >= 60, "%s: %u points, expected >= 60", c.id, c.n_pts);
    okf(c.half_w > 0 && c.half_h > 0, "%s: zero extent", c.id);
    // normalise() scales the LONGER axis to COORD_MAX, so exactly one of the
    // two half-extents must reach it.
    const int16_t longer = c.half_w > c.half_h ? c.half_w : c.half_h;
    okf(std::abs(longer - circuits::COORD_MAX) <= 1,
        "%s: longer axis %d, expected %d", c.id, longer, circuits::COORD_MAX);
    okf(c.length_m > 2000 && c.length_m < 9000,
        "%s: length %u m is implausible", c.id, c.length_m);
  }

  // MAP-3: every trace must land inside its box at any box shape, with aspect
  // preserved. A circuit squeezed to fill a square is unrecognisable.
  const int boxes[][2] = {{300, 300}, {400, 320}, {200, 460}, {460, 200}};
  for (int b = 0; b < 4; b++) {
    for (int i = 0; i < circuits::N; i++) {
      const auto &c = circuits::C[i];
      const auto f = map::fit_box(c, boxes[b][0], boxes[b][1]);
      float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
      for (int p = 0; p < c.n_pts; p++) {
        float x, y; map::point_at(c, p, f, x, y);
        minx = std::fmin(minx, x); maxx = std::fmax(maxx, x);
        miny = std::fmin(miny, y); maxy = std::fmax(maxy, y);
      }
      okf(minx >= -0.5f && miny >= -0.5f &&
          maxx <= boxes[b][0] + 0.5f && maxy <= boxes[b][1] + 0.5f,
          "%s: escapes %dx%d box (%.1f,%.1f)-(%.1f,%.1f)",
          c.id, boxes[b][0], boxes[b][1], minx, miny, maxx, maxy);
      // and it must actually FILL one axis, or the rotation did nothing useful
      const float fw = (maxx - minx) / (boxes[b][0] - 6.0f);
      const float fh = (maxy - miny) / (boxes[b][1] - 6.0f);
      okf(std::fmax(fw, fh) > 0.98f, "%s: fills only %.0f%% of %dx%d",
          c.id, 100.0f * std::fmax(fw, fh), boxes[b][0], boxes[b][1]);
    }
  }

  // The fill rotation must beat north-up on the long thin street circuits -
  // that is the entire justification for decision 24.
  for (const char *id : {"be-1925", "sa-2021", "az-2016", "us-2023"}) {
    const auto *c = map::by_trace_id(id);
    okf(c != nullptr, "trace %s present", id);
    if (c) okf(c->rot_cdeg != 0, "%s: expected a non-zero fill rotation", id);
  }

  // MAP-3: north_deg is the negative of the applied rotation, normalised.
  for (int i = 0; i < circuits::N; i++) {
    const auto &c = circuits::C[i];
    const float n = map::north_deg(c);
    okf(n >= 0.0f && n < 360.0f, "%s: north_deg %.1f out of range", c.id, n);
    if (c.rot_cdeg == 0) okf(map::is_north_up(c), "%s: should be north-up", c.id);
  }
}

// ---------------------------------------------------------------- id map
static void test_idmap() {
  std::printf("circuitId -> trace (decision 23)\n");
  // sorted, for the binary search to be valid
  for (int i = 1; i < circuits::N_IDMAP; i++)
    okf(std::strcmp(circuits::BY_CIRCUIT_ID[i - 1].circuit_id,
                    circuits::BY_CIRCUIT_ID[i].circuit_id) < 0,
        "BY_CIRCUIT_ID not sorted at %d", i);

  for (int i = 0; i < circuits::N_IDMAP; i++) {
    const char *cid = circuits::BY_CIRCUIT_ID[i].circuit_id;
    const auto *c = map::by_circuit_id(cid);
    okf(c != nullptr, "lookup failed for %s", cid);
  }
  ok(map::by_circuit_id("no_such_circuit") == nullptr, "unknown id returns nullptr");
  ok(map::by_circuit_id("") == nullptr, "empty id returns nullptr");
  ok(map::by_circuit_id(nullptr) == nullptr, "null id returns nullptr");

  // decision 23: EVERY circuit on the compiled calendar must resolve, or the
  // device shows a race weekend with no map.
  for (int i = 0; i < calendar::N_ROUNDS; i++) {
    const auto &r = calendar::R[i];
    okf(map::by_circuit_id(r.circuit_id) != nullptr,
        "round %u (%s) has no trace for circuitId '%s'", r.round, r.name, r.circuit_id);
  }

  // The generator's exclusion list: Zeltweg must NOT resolve to the Red Bull
  // Ring, which sits 2.4 km away and is a different circuit.
  const auto *z = map::by_circuit_id("zeltweg");
  ok(z == nullptr || std::strcmp(z->id, "at-1969") != 0,
     "zeltweg must not map to the Red Bull Ring");
}

// ---------------------------------------------------------------- calendar
static void test_calendar() {
  std::printf("compiled calendar (DATA-3, RACE-13)\n");
  ok(calendar::N_ROUNDS > 0, "calendar has rounds");
  ok(calendar::SEASON >= 2024 && calendar::SEASON < 2100, "season looks sane");

  uint32_t prev_race = 0;
  for (int i = 0; i < calendar::N_ROUNDS; i++) {
    const auto &r = calendar::R[i];
    okf(r.round == i + 1, "round %d out of order (got %u)", i + 1, r.round);
    okf(r.iso3 != nullptr && std::strlen(r.iso3) == 3,
        "round %u: bad iso3", r.round);
    // decision 18: the flag keys on the CIRCUIT country, so country/iso3 must
    // be populated even when the race NAME disagrees with it.
    okf(r.country != nullptr && *r.country, "round %u: empty country", r.round);
    const uint32_t race = r.start[calendar::RACE];
    if (race && prev_race) okf(race > prev_race, "round %u: race time goes backwards", r.round);
    if (race) prev_race = race;
    // sessions must precede the race when both are known
    for (int s = 0; s < calendar::RACE; s++)
      if (r.start[s] && race)
        okf(r.start[s] < race, "round %u: session %d is after the race", r.round, s);
  }

  // decision 90: sprint weekends carry SPRINT and SQ, and must have no FP3.
  int sprints = 0;
  for (int i = 0; i < calendar::N_ROUNDS; i++) {
    const auto &r = calendar::R[i];
    if (r.start[calendar::SPRINT]) {
      sprints++;
      okf(r.start[calendar::FP3] == 0,
          "round %u has both a sprint and FP3", r.round);
    }
  }
  std::printf("  (%d sprint weekends in %u)\n", sprints, calendar::SEASON);
}

// ---------------------------------------------------------------- carousel
static void test_carousel() {
  std::printf("carousel rotation (UI-20a, decision 68)\n");
  using namespace carousel;

  // Every card is reached, and no type starves.
  {
    Rotation r;
    r.configure(40, 22, 32, ALL);
    std::set<int> seen[N_TYPES];
    Card c;
    for (int i = 0; i < 40 * 4 * 3; i++) { ok(r.next(c), "next() ran dry"); seen[c.type].insert(c.index); }
    okf((int) seen[CIRCUIT].size() == 40, "circuits reached %zu/40", seen[CIRCUIT].size());
    okf((int) seen[DRIVER].size() == 22, "drivers reached %zu/22", seen[DRIVER].size());
    okf((int) seen[LEGEND].size() == 32, "legends reached %zu/32", seen[LEGEND].size());
  }

  // Deterministic interleave, not a shuffle: no type three times running.
  {
    Rotation r;
    r.configure(40, 22, 32, ALL);
    Card c; int run = 1; CardType last = N_TYPES;
    for (int i = 0; i < 200; i++) {
      r.next(c);
      run = (c.type == last) ? run + 1 : 1;
      okf(run <= 2, "three %d cards in a row at %d", (int) c.type, i);
      last = c.type;
    }
  }

  // Content filters.
  {
    struct { Content c; CardType only; } cases[] = {
        {CIRCUITS_ONLY, CIRCUIT}, {DRIVERS_ONLY, DRIVER}, {LEGENDS_ONLY, LEGEND}};
    for (auto &k : cases) {
      Rotation r; r.configure(40, 22, 32, k.c);
      Card c;
      for (int i = 0; i < 50; i++) { r.next(c); okf(c.type == k.only, "filter leaked type %d", (int) c.type); }
    }
    Rotation r; r.configure(40, 22, 32, CIRCUITS_AND_DRIVERS);
    Card c;
    for (int i = 0; i < 50; i++) { r.next(c); ok(c.type != LEGEND, "legend leaked into circuits+drivers"); }
  }

  // Empty lists must not hang or starve. A fresh build has no driver or legend
  // tables yet (they land at M2), so this is the state the device ships in now.
  {
    Rotation r; r.configure(40, 0, 0, ALL);
    Card c;
    for (int i = 0; i < 50; i++) { ok(r.next(c), "circuits-only rotation stalled"); ok(c.type == CIRCUIT, "non-circuit with empty lists"); }
    Rotation e; e.configure(0, 0, 0, ALL);
    ok(!e.any(), "empty rotation reports any()==false");
    ok(!e.next(c), "empty rotation must return false, not hang");
    Rotation f; f.configure(40, 22, 32, LEGENDS_ONLY);
    ok(f.any(), "legends-only with legends present");
    Rotation g; g.configure(40, 0, 0, LEGENDS_ONLY);
    ok(!g.any(), "legends-only with no legends");
    ok(!g.next(c), "legends-only with no legends must not hang");
  }

  // UI-20c: the circuit cursor can be pointed at the next round.
  {
    Rotation r; r.configure(40, 22, 32, ALL);
    r.set_circuit_cursor(13);
    Card c; r.next(c);
    ok(c.type == CIRCUIT && c.index == 13, "set_circuit_cursor ignored");
    r.set_circuit_cursor(999);
    ok(r.cursor(CIRCUIT) != 999, "out-of-range cursor accepted");
  }

  // plane-tracker decision 60: a restored number is range-checked, not trusted.
  ok(clamp_interval_s(45) == 45, "interval 45 passes");
  ok(clamp_interval_s(0) == 45, "interval 0 -> default");
  ok(clamp_interval_s(-5) == 45, "negative interval -> default");
  ok(clamp_interval_s(1e9f) == 120, "huge interval clamps");
  ok(clamp_interval_s(NAN) == 45, "NaN interval -> default");
}

int main() {
  std::printf("\nF1 Tracker - M1 host tests\n\n");
  test_geometry();
  test_idmap();
  test_calendar();
  test_carousel();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
