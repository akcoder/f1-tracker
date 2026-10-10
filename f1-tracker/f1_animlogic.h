#pragma once
// The animated logos (boot and About): the start-lights sequence and a car that laps the
// logo's own circuit. Pure functions and geometry, free of LVGL, so the timing and the
// path can be tested on the host; f1_anim.h draws them.
//
// The logo IS the Monza trace from f1_circuits.h (tools/gen_logo.py lays it out with a
// 16 % margin), so the car follows the very points the logo was drawn from.
#include <cmath>
#include <cstdint>
#include <cstring>

#include "f1_circuits.h"
#include "f1_map.h"

namespace f1 {
namespace animlogic {

// ---- the start lights ------------------------------------------------------
// One light per STEP_MS, all five held for HOLD_MS, then OUT: lights out and away.
inline constexpr int N_LIGHTS = 5;
inline constexpr int32_t STEP_MS = 600, HOLD_MS = 1000;
inline constexpr int32_t OUT_MS = N_LIGHTS * STEP_MS + HOLD_MS;      // 4000: the lights go out
inline constexpr int32_t GANTRY_END_MS = OUT_MS + 500;               // the gantry then fades away
inline constexpr int32_t ABOUT_PERIOD_MS = 9000;                     // About: the sequence repeats

// Lights lit `t` ms into the sequence (0 before the first, and again from OUT).
inline int lit(int32_t t) {
  if (t < STEP_MS || t >= OUT_MS) return 0;
  const int k = (int) (t / STEP_MS);
  return k > N_LIGHTS ? N_LIGHTS : k;
}
// Is the gantry (the five unlit discs) on screen at `t`?
inline bool gantry_visible(int32_t t) { return t >= 0 && t < GANTRY_END_MS; }
// The About page repeats the sequence from where it opened.
inline int32_t about_phase(int32_t t) { return t < 0 ? -1 : t % ABOUT_PERIOD_MS; }

// ---- the car ---------------------------------------------------------------
inline constexpr int MAX_PTS = 160;
inline constexpr int32_t LAP_MS = 5000;
inline constexpr int N_TRAIL = 3;                // the head and three fading dots behind it
inline constexpr float TRAIL_LAG = 0.02f;       // of a lap, between dots

struct Track {
  float x[MAX_PTS + 1], y[MAX_PTS + 1], cum[MAX_PTS + 1];
  int n = 0;                                     // points, the last one repeating the first
  float total = 0;
};

inline const circuits::Circuit *find_circuit(const char *id) {
  for (int i = 0; i < circuits::N; i++)
    if (std::strcmp(circuits::C[i].id, id) == 0) return &circuits::C[i];
  return nullptr;
}

// The circuit as the logo of `size` px draws it (margin 16 %), the lap closed.
inline bool build(Track &t, const circuits::Circuit &c, int size) {
  t.n = 0;
  t.total = 0;
  const int n = c.n_pts < MAX_PTS ? c.n_pts : MAX_PTS;
  if (n < 3) return false;
  const auto fit = map::fit_box(c, size, size, 0.16f * (float) size);
  for (int i = 0; i < n; i++) map::point_at(c, i, fit, t.x[i], t.y[i]);
  t.x[n] = t.x[0];
  t.y[n] = t.y[0];
  t.n = n + 1;
  t.cum[0] = 0;
  for (int i = 1; i < t.n; i++) {
    const float dx = t.x[i] - t.x[i - 1], dy = t.y[i] - t.y[i - 1];
    t.cum[i] = t.cum[i - 1] + std::sqrt(dx * dx + dy * dy);
  }
  t.total = t.cum[t.n - 1];
  return t.total > 0;
}

// The point a fraction of the way round the lap (0 = the start/finish line). Wraps.
inline void at(const Track &t, float frac, float &x, float &y) {
  frac -= std::floor(frac);
  const float s = frac * t.total;
  int i = 1;
  while (i < t.n - 1 && t.cum[i] < s) i++;
  const float seg = t.cum[i] - t.cum[i - 1];
  const float u = seg > 0 ? (s - t.cum[i - 1]) / seg : 0.0f;
  x = t.x[i - 1] + (t.x[i] - t.x[i - 1]) * u;
  y = t.y[i - 1] + (t.y[i] - t.y[i - 1]) * u;
}

// Where the lap is `t` ms after the page opened.
inline float lap_frac(int32_t t) {
  return t <= 0 ? 0.0f : (float) (t % LAP_MS) / (float) LAP_MS;
}

}  // namespace animlogic
}  // namespace f1
