#pragma once
// The animated logos: on the boot page the start lights and a pack of cars lapping the
// logo's own circuit, on the About page the cars alone. Pure functions and geometry, free
// of LVGL, so the timing and the path can be tested on the host; f1_anim.h draws them.
//
// The cars follow f1_logomark.h, the very points tools/gen_logo.py draws the logo's outline
// from (the hand-smoothed Monza of reference/monza-smoothed.svg, laid out with a 16 % margin).
#include <cmath>
#include <cstdint>
#include <cstring>

#include "f1_logomark.h"

namespace f1 {
namespace animlogic {

// ---- the start lights (boot page only) -------------------------------------
// One light per STEP_MS, all five held for HOLD_MS, then OUT: lights out and away.
inline constexpr int N_LIGHTS = 5;
inline constexpr int32_t STEP_MS = 600, HOLD_MS = 1000;
inline constexpr int32_t OUT_MS = N_LIGHTS * STEP_MS + HOLD_MS;      // 4000: the lights go out
inline constexpr int32_t GANTRY_END_MS = OUT_MS + 500;               // the gantry then fades away

// Lights lit `t` ms into the sequence (0 before the first, and again from OUT).
inline int lit(int32_t t) {
  if (t < STEP_MS || t >= OUT_MS) return 0;
  const int k = (int) (t / STEP_MS);
  return k > N_LIGHTS ? N_LIGHTS : k;
}
// Is the gantry (the five unlit discs) on screen at `t`?
inline bool gantry_visible(int32_t t) { return t >= 0 && t < GANTRY_END_MS; }

// ---- the cars ---------------------------------------------------------------
inline constexpr int MAX_PTS = 160;
inline constexpr int32_t LAP_MS = 5000;
inline constexpr int N_CARS = 3;                 // a small pack, nose to tail
inline constexpr float CAR_GAP = 0.075f;         // of a lap, between cars
inline constexpr int HEADINGS = 16;              // sprites per car (f1_cars.h), 0 = nose up, clockwise

struct Track {
  float x[MAX_PTS + 1], y[MAX_PTS + 1], cum[MAX_PTS + 1];
  int n = 0;                                     // points, the last one repeating the first
  float total = 0;
};

// The outline as the logo of `size` px draws it (margin 16 %), the lap closed.
inline bool build(Track &t, int size) {
  t.n = 0;
  t.total = 0;
  constexpr int n = logomark::N;
  static_assert(n >= 3 && n <= MAX_PTS, "the logo outline must fit the track");
  const float pad = 0.16f * (float) size;
  const float hw = logomark::HALF_W > 0 ? (float) logomark::HALF_W : 1.0f;
  const float hh = logomark::HALF_H > 0 ? (float) logomark::HALF_H : 1.0f;
  const float sx = ((float) size - 2.0f * pad) / (2.0f * hw), sy = ((float) size - 2.0f * pad) / (2.0f * hh);
  const float k = sx < sy ? sx : sy;
  for (int i = 0; i < n; i++) {
    t.x[i] = (float) size * 0.5f + (float) logomark::PTS[i][0] * k;
    t.y[i] = (float) size * 0.5f - (float) logomark::PTS[i][1] * k;
  }
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

// Which sprite points the way the car is going at `frac` of the lap: the direction of the
// track there, as a step of 360/HEADINGS degrees clockwise from straight up.
inline int heading(const Track &t, float frac) {
  float x0, y0, x1, y1;
  at(t, frac - 0.004f, x0, y0);
  at(t, frac + 0.004f, x1, y1);
  const float deg = std::atan2(x1 - x0, -(y1 - y0)) * 57.29578f;      // 0 = up (negative y), clockwise
  int k = (int) std::lround(deg / (360.0f / HEADINGS));
  return ((k % HEADINGS) + HEADINGS) % HEADINGS;
}

// ---- the pack's speeds -----------------------------------------------------
// Every car averages one lap per LAP_MS but speeds up and slows down on the way: its
// position (in laps) is   u - k*CAR_GAP + w_k(u) - w_k(0),   u = t / LAP_MS,   with
//   w_k(u) = a_k * sin(2 pi (n_k u + phase_k)) / (2 pi n_k),
// so its speed is 1 + a_k cos(...), always positive (a_k < 1). n_k is a whole number of
// swings per lap, so after one lap each car is exactly one lap further on (a looping render
// repeats cleanly) and at u = 0 the cars stand on the grid, CAR_GAP apart. The swings differ
// per car, so one gains on the one ahead, draws alongside and goes past. The numbers come
// from a seed (the device picks one each time the page opens; the renders use DEMO_SEED).
inline constexpr uint32_t DEMO_SEED = 7;
// a_k: with the mean pace 1, a car's speed swings between 1 - a and 1 + a. On the first board
// 0.62-0.93 read as stalling ("that's not how F1 drivers race"), so no car drops under ~0.6.
inline constexpr float SWING_MIN = 0.22f, SWING_MAX = 0.40f;
inline constexpr float SWING_CAP = 0.55f;                       // the most a hand-over may ask for (next_pack)
inline constexpr float SIDE_PX_PER_100 = 4.0f;                  // the passer's sideways step, px per 100 px of logo
inline constexpr float PASS_ZONE = 0.045f;                      // laps: closer than this, the faster car pulls out

struct Pack {
  float a[N_CARS] = {0}, phase[N_CARS] = {0};
  int n[N_CARS] = {1, 1, 1};
  // MOTION-3a: the lap this set is for, where each car stands at its start (laps, relative to
  // the leader's line crossing) and the net laps it gains over the lap to reach its next slot.
  // Lap 0 is the grid with no gain, so a one-lap render loops cleanly.
  int lap = 0;
  float off[N_CARS] = {0.0f, -CAR_GAP, -2.0f * CAR_GAP};
  float gain[N_CARS] = {0};
};

inline uint32_t xorshift(uint32_t &x) {
  x ^= x << 13; x ^= x >> 17; x ^= x << 5;
  return x;
}

inline Pack make_pack(uint32_t seed) {
  Pack p;
  uint32_t x = seed * 2654435761u + 0x9E3779B9u;
  if (x == 0) x = 1;
  for (int i = 0; i < 4; i++) xorshift(x);                       // churn a small seed
  for (int k = 0; k < N_CARS; k++) {
    p.n[k] = 1 + (int) (xorshift(x) % 2);                         // 1 or 2 swings a lap
    p.a[k] = SWING_MIN + (SWING_MAX - SWING_MIN) * (float) (xorshift(x) % 1000) / 999.0f;
    p.phase[k] = (float) (xorshift(x) % 1000) / 1000.0f;
  }
  return p;
}

// MOTION-3a: the next lap's set, so no lap repeats the one before. The order at the line is
// re-dealt (any of the N! orders, by the seed; on the first board the cars always crossed in
// the same order) and each car's net pace over the lap carries it to its new slot, so the
// pack is nose to tail at every line. The hand-over keeps each car's position (the lap ends
// where the next set starts) and its speed: the new phase is chosen so gain + a cos(2 pi phase)
// is the pace it crossed the line with, the swing widened up to SWING_CAP if it must be.
inline Pack next_pack(const Pack &prev, uint32_t seed) {
  Pack p = make_pack(seed);
  p.lap = prev.lap + 1;
  uint32_t x = seed * 2246822519u + 0x3C6EF372u;
  if (x == 0) x = 1;
  for (int i = 0; i < 4; i++) xorshift(x);
  int slot[N_CARS];                                                // slot[k]: car k's place at the next line
  for (int k = 0; k < N_CARS; k++) slot[k] = k;
  for (int i = N_CARS - 1; i > 0; i--) {                           // Fisher-Yates: every order equally likely
    const int j = (int) (xorshift(x) % (uint32_t) (i + 1));
    const int tmp = slot[i]; slot[i] = slot[j]; slot[j] = tmp;
  }
  for (int k = 0; k < N_CARS; k++) {
    p.off[k] = prev.off[k] + prev.gain[k];
    p.gain[k] = -(float) slot[k] * CAR_GAP - p.off[k];
    const float v = prev.gain[k] + prev.a[k] * std::cos(6.28318531f * prev.phase[k]);   // pace - 1 at the line
    const float need = v - p.gain[k];
    const float an = std::fabs(need);
    if (p.a[k] < an) p.a[k] = an < SWING_CAP ? an : SWING_CAP;
    float c = need / p.a[k];
    c = c < -1.0f ? -1.0f : (c > 1.0f ? 1.0f : c);
    const float ph = std::acos(c) / 6.28318531f;                           // 0 .. 0.5
    p.phase[k] = p.phase[k] < 0.5f ? ph : 1.0f - ph;                       // either branch, by the seed
  }
  return p;
}

// The pack for lap `lap` of an opening seeded `seed`: lap 0 is make_pack(seed), each later lap
// follows from the one before through a xorshift chain. The device walks the chain a lap at a
// time; the renders and the tests ask for a lap outright.
inline Pack lap_pack(uint32_t seed, int lap) {
  Pack p = make_pack(seed);
  uint32_t x = seed ? seed : 1;
  for (int l = 1; l <= lap; l++) p = next_pack(p, xorshift(x));
  return p;
}

inline float swing(const Pack &p, int k, float u) {
  const float tau = 6.28318531f * (float) p.n[k];
  return p.a[k] * std::sin(6.28318531f * ((float) p.n[k] * u + p.phase[k])) / tau;
}

// Car k's position round the lap, in laps, `u` laps after the page opened (u = t / LAP_MS),
// with the set for the lap `u` is in.
inline float car_pos(const Pack &p, int k, float u) {
  const float f = u - (float) p.lap;                                       // into this lap
  return u + p.off[k] + p.gain[k] * f + swing(p, k, f) - swing(p, k, 0.0f);
}

// Its speed, in laps per lap-time (the pack's mean is 1).
inline float car_speed(const Pack &p, int k, float u) {
  const float f = u - (float) p.lap;
  return 1.0f + p.gain[k] + p.a[k] * std::cos(6.28318531f * ((float) p.n[k] * f + p.phase[k]));
}

// Laps since the page opened, for a time `t` ms after it (0 before).
inline float lap_u(int32_t t) { return t <= 0 ? 0.0f : (float) t / (float) LAP_MS; }

// How far car k has pulled out to pass, 0..1: while it is the faster of two cars within
// PASS_ZONE of each other, 1 alongside, 0 at the edge of the zone. The step is to the left
// of its direction of travel.
inline float pulling_out(const Pack &p, int k, float u) {
  float w = 0;
  for (int j = 0; j < N_CARS; j++) {
    if (j == k) continue;
    float d = car_pos(p, k, u) - car_pos(p, j, u);
    d -= std::floor(d + 0.5f);                                    // the shortest way round the lap
    const float ad = std::fabs(d);
    if (ad < PASS_ZONE && car_speed(p, k, u) > car_speed(p, j, u))
      w = std::fmax(w, 1.0f - ad / PASS_ZONE);
  }
  return w;
}

}  // namespace animlogic
}  // namespace f1
