// f1_animlogic.h: the start-lights timing and the car's path round the logo.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "../f1-tracker/f1_animlogic.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
  using namespace f1::animlogic;
  // lights: none, then one per step, five held, then out
  CHECK(lit(-1) == 0 && lit(0) == 0 && lit(STEP_MS - 1) == 0, "nothing before the first step");
  CHECK(lit(STEP_MS) == 1 && lit(2 * STEP_MS) == 2 && lit(5 * STEP_MS) == 5, "one more each step");
  CHECK(lit(5 * STEP_MS + HOLD_MS - 1) == 5, "five are held");
  CHECK(lit(OUT_MS) == 0 && lit(OUT_MS + 100000) == 0, "lights out, and they stay out");
  int prev = 0; bool mono = true;
  for (int t = 0; t < OUT_MS; t += 10) { const int k = lit(t); if (k < prev) mono = false; prev = k; }
  CHECK(mono, "the count never falls before lights out");
  CHECK(gantry_visible(0) && gantry_visible(OUT_MS) && !gantry_visible(GANTRY_END_MS) && !gantry_visible(-1),
        "the gantry shows for the sequence and a moment after");
  CHECK(about_phase(-5) == -1 && about_phase(0) == 0 && about_phase(ABOUT_PERIOD_MS + 7) == 7,
        "About repeats the sequence");
  CHECK(ABOUT_PERIOD_MS > GANTRY_END_MS, "there is a rest between About sequences");

  // the track: Monza, as the 200 px and 112 px logos lay it out
  const auto *c = find_circuit("it-1922");
  CHECK(c != nullptr, "the logo's circuit exists");
  if (!c) return 1;
  for (int size : {200, 112}) {
    Track t;
    CHECK(build(t, *c, size), "built at %d", size);
    CHECK(t.n == c->n_pts + 1, "closed: %d points + the first again (%d)", c->n_pts, t.n);
    float minx = 1e9, maxx = -1e9, miny = 1e9, maxy = -1e9;
    for (int i = 0; i < t.n; i++) {
      minx = std::fmin(minx, t.x[i]); maxx = std::fmax(maxx, t.x[i]);
      miny = std::fmin(miny, t.y[i]); maxy = std::fmax(maxy, t.y[i]);
    }
    const float m = 0.16f * size;
    CHECK(minx >= m - 1 && maxx <= size - m + 1 && miny >= m - 1 && maxy <= size - m + 1,
          "inside the logo's 16%% margin at %d: x %.1f-%.1f y %.1f-%.1f", size, minx, maxx, miny, maxy);
    CHECK(std::fmax(maxx - minx, maxy - miny) > 0.9f * (size - 2 * m), "fills the logo");
    float x0, y0, x1, y1;
    at(t, 0.0f, x0, y0);
    CHECK(std::fabs(x0 - t.x[0]) < 0.01f && std::fabs(y0 - t.y[0]) < 0.01f, "frac 0 is the start line");
    at(t, 1.0f, x1, y1);
    CHECK(std::fabs(x1 - x0) < 0.01f && std::fabs(y1 - y0) < 0.01f, "a lap closes");
    at(t, 1.25f, x1, y1); at(t, 0.25f, x0, y0);
    CHECK(std::fabs(x1 - x0) < 0.01f && std::fabs(y1 - y0) < 0.01f, "it wraps");
    // a smooth path: successive samples never jump more than a few px
    float px, py; at(t, 0.0f, px, py); float worst = 0;
    for (int i = 1; i <= 1000; i++) {
      float x, y; at(t, i / 1000.0f, x, y);
      worst = std::fmax(worst, std::hypot(x - px, y - py)); px = x; py = y;
    }
    CHECK(worst < t.total / 200.0f, "no jumps along the lap (%.2f px worst of %.0f)", worst, t.total);
    // the car stays on the drawn line: every sample lies on a segment of the polyline
    float off = 0;
    for (int i = 0; i < 400; i++) {
      float x, y; at(t, i / 400.0f, x, y);
      float best = 1e9;
      for (int k = 1; k < t.n; k++) {
        const float ax = t.x[k - 1], ay = t.y[k - 1], bx = t.x[k], by = t.y[k];
        const float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        float u = l2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / l2 : 0;
        u = std::fmax(0, std::fmin(1, u));
        best = std::fmin(best, std::hypot(x - (ax + u * dx), y - (ay + u * dy)));
      }
      off = std::fmax(off, best);
    }
    CHECK(off < 0.01f, "the car is on the line (%.4f px off)", off);
  }
  CHECK(lap_frac(-3) == 0 && lap_frac(0) == 0 && std::fabs(lap_frac(LAP_MS / 2) - 0.5f) < 1e-6f &&
        std::fabs(lap_frac(LAP_MS + 1250) - 0.25f) < 1e-6f, "lap fraction");
  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
