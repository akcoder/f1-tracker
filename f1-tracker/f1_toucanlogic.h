#pragma once
// The toucan, the part that needs no screen: a random flight (which way, how high up, how fast, where
// it perches, what it says), where the bird is and what it is doing at a moment, and when the next
// visit is. tools/toucan_art.py is its Python twin (the render tool and the tests agree).
//
// A visit: the toucan flies in off screen, swooping up and down, settles on a branch somewhere in the
// middle, clacks its beak and says something in a speech bubble for 2-3 s, then flies on and leaves.
// Its speed is never steady: base * (1 + amp cos(2 pi t / period + phase)), always forward; its
// wings beat by distance, so they hurry and dawdle with it.
#include <cmath>
#include <cstdint>

#include "f1_critterlogic.h"

namespace f1 {
namespace toucan {

inline constexpr int SW = 180, SH = 140;            // the sprite
inline constexpr int SCREEN_W = 480, SCREEN_H = 480;
inline constexpr int LANE_MIN = 70, LANE_MAX = 320;     // the sprite's top edge before the swoops (they add +-11)
inline constexpr int STOP_MIN = 150, STOP_MAX = 300;    // where it perches (its left edge, flying right): the whole bird stays on screen either way
inline constexpr double BASE_SPEED = 110.0;
inline constexpr double STEP_PX = 6.0;       // one of the four wing poses per this much distance
inline constexpr double WAVE_PX = 150.0, WAVE_AMP = 11.0, WAVE_CALM = 70.0;   // the swoops, and how near the perch they die away
inline constexpr uint32_t BEAK_DELAY_MS = 300, BEAK_STEP_MS = 220;
inline constexpr uint32_t GAP_MIN_MS = critter::GAP_MIN_MS, GAP_MAX_MS = critter::GAP_MAX_MS;   // between visits

enum Pose : uint8_t { FLY0 = 0, FLY1, FLY2, FLY3, PERCH, BEAK_HALF, BEAK_OPEN, N_POSES };

inline constexpr int N_WORDS = 8;
inline const char *const WORDS[N_WORDS] = {"Just follow your nose!", "Squawk!", "Toucan do it!", "Got any fruit?",
                                           "Nice beak, huh?", "Rrrk! Rrrk!", "Just follow your nose!",
                                           "Fruit salad time!"};

using critter::Rng;

struct Flight {
  bool flip = false;          // true: right to left
  int lane = 0;               // the sprite's top edge, before the swoops
  double speed = BASE_SPEED;  // px/s
  int stop_x = 0;             // its left edge at the perch (flying right)
  uint32_t pause_ms = 2200;
  int word = 0;
  double amp = 0.3, period = 2000.0, phase = 0.0;   // how its speed varies: depth, ms, where in the cycle
  uint32_t t_stop = 0, t_end = 0;

  // pixels flown after tw_ms of flying (the perch does not count)
  double dist(uint32_t tw_ms) const {
    const double w = 2 * M_PI / period;
    return speed * (tw_ms / 1000.0 + amp / w * (std::sin(w * tw_ms + phase) - std::sin(phase)) / 1000.0);
  }
  uint32_t time_for(double px) const {
    uint32_t t = 0;
    while (dist(t) < px) t++;
    return t;
  }
  // vertical offset at distance d: a sine that settles to nothing at the perch
  double swoop(double d) const {
    const double near = std::fabs(d - (stop_x + SW)) / WAVE_CALM;
    const double k = near >= 1 ? 1.0 : near * near * (3 - 2 * near);
    return WAVE_AMP * std::sin(2 * M_PI * d / WAVE_PX) * k;
  }
};

inline Flight make_flight(uint32_t seed) {
  Rng r(seed);
  Flight w;
  w.flip = r.next() < 0.5;
  w.lane = LANE_MIN + (int) (r.next() * (LANE_MAX - LANE_MIN + 1));
  w.speed = BASE_SPEED * (0.7 + 0.6 * r.next());
  w.stop_x = STOP_MIN + (int) (r.next() * (STOP_MAX - STOP_MIN + 1));
  w.pause_ms = 2200 + (uint32_t) (r.next() * 1000);
  w.word = (int) (r.next() * N_WORDS) % N_WORDS;
  w.amp = 0.25 + 0.25 * r.next();
  w.period = 1200.0 + 1800.0 * r.next();
  w.phase = 2 * M_PI * r.next();
  w.t_stop = w.time_for(w.stop_x + SW);
  w.t_end = w.t_stop + w.pause_ms + (w.time_for(SCREEN_W + SW) - w.t_stop);
  return w;
}

struct Frame {
  int x = 0, y = 0;           // top left of the sprite on screen
  Pose pose = FLY0;
  bool bubble = false;
};

inline Frame at(const Flight &w, uint32_t t_ms) {
  Frame f;
  uint32_t tw;
  bool perched = false;
  if (t_ms < w.t_stop) {
    tw = t_ms;
  } else if (t_ms < w.t_stop + w.pause_ms) {
    tw = w.t_stop;
    const uint32_t u = t_ms - w.t_stop;
    f.bubble = u >= BEAK_DELAY_MS;
    static const Pose BEAK[4] = {PERCH, BEAK_HALF, BEAK_OPEN, BEAK_HALF};
    f.pose = f.bubble ? BEAK[((u - BEAK_DELAY_MS) / BEAK_STEP_MS) % 4] : PERCH;
    perched = true;
  } else {
    tw = t_ms - w.pause_ms;
  }
  const double d = w.dist(tw);
  if (!perched) f.pose = (Pose) ((long) (d / STEP_PX) % 4);
  double x = -SW + d;
  if (w.flip) x = SCREEN_W - x - SW;
  f.x = (int) std::floor(x + 0.5);
  f.y = w.lane + (int) std::floor(w.swoop(d) + 0.5);
  return f;
}

inline bool finished(const Flight &w, uint32_t t_ms) { return t_ms >= w.t_end; }

inline uint32_t next_gap_ms(uint32_t seed) { return critter::next_gap_ms(seed); }

}  // namespace toucan
}  // namespace f1
