// The walking dinosaur's timing (tracker_dinologic.h): the walk, the stop and the roar, the way out,
// and the numbers tools/dino_art.py (its Python twin) prints for seed 7.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../f1-tracker/f1_dinologic.h"
using namespace f1::dino;
static int checks = 0, failures = 0;
#define OK(c, m) do { checks++; if (!(c)) { failures++; std::printf("  FAIL  %s\n", m); } } while (0)
int main() {
  std::printf("\nF1 Tracker - dinosaur tests\n\n");
  // pinned to `python3 tools/dino_art.py` (seed 7)
  const Walk w = make_walk(7);
  OK(w.flip && w.lane == 90 && w.stop_x == 279 && w.pause_ms == 2664 && w.word == 4, "seed 7: the walk");
  OK(std::fabs(w.speed - 96.902) < 0.01, "seed 7: speed");
  OK(std::fabs(w.amp - 0.2651) < 0.001 && std::fabs(w.period - 1533.0) < 0.5 && std::fabs(w.phase - 2.3101) < 0.001, "seed 7: how its speed varies");
  OK(w.t_stop == 4351 && w.t_end == 9199, "seed 7: the stop and the end");
  Frame f = at(w, 0);       OK(f.x == 480 && f.y == 90 && f.pose == WALK0 && !f.bubble, "t=0: off the right edge");
  f = at(w, 1000);          OK(f.x == 387 && f.pose == WALK1, "t=1000");
  f = at(w, 2500);          OK(f.x == 242 && f.pose == WALK0, "t=2500: the legs follow the distance, not the clock");
  f = at(w, w.t_stop);      OK(f.x == 57 && f.pose == STAND && !f.bubble, "stopped, mouth shut, no bubble yet");
  f = at(w, w.t_stop + 400); OK(f.x == 57 && f.pose == STAND && f.bubble, "the bubble appears");
  f = at(w, w.t_stop + 1000); OK(f.pose == ROAR_OPEN && f.bubble, "the mouth opens");
  f = at(w, w.t_end - 1);   OK(f.x == -144 && f.pose == WALK3, "leaving off the left edge");
  OK(next_gap_ms(7) == 180237, "seed 7: the gap");
  // every walk stays on screen vertically, stops inside the screen, speaks a word, and is continuous
  bool lanes = true, stops = true, words = true, cont = true, both = false, anyr = false, anyl = false;
  for (uint32_t s = 1; s <= 2000; s++) {
    const Walk k = make_walk(s * 2654435761u);
    lanes &= k.lane >= LANE_MIN && k.lane + SH <= SCREEN_H;
    stops &= k.stop_x >= STOP_MIN && k.stop_x <= STOP_MAX;
    words &= k.word >= 0 && k.word < N_WORDS;
    (k.flip ? anyl : anyr) = true;
    int px = at(k, 0).x;
    for (uint32_t t = 40; t <= k.t_end; t += 40) {
      const int x = at(k, t).x;
      cont &= std::abs(x - px) <= 9;       // at most ~150 px/s * 40 ms + rounding
      px = x;
    }
    OK(finished(k, k.t_end) && !finished(k, k.t_end - 1), "a walk ends at t_end");
    const uint32_t g = next_gap_ms(s);
    words &= g >= GAP_MIN_MS && g <= GAP_MAX_MS;
  }
  both = anyl && anyr;
  OK(lanes, "the lane keeps the sprite on screen");
  OK(stops, "the stop is inside the screen");
  OK(words, "words and gaps in range");
  OK(cont, "no jump: the walk is continuous across the stop");
  OK(both, "both directions occur");
  // the speed really varies: some walk covers more than 30 px more in 500 ms in one stretch than in another
  bool varies = false, fwd = true;
  for (uint32_t s = 1; s <= 200 && !varies; s++) {
    const Walk k = make_walk(s * 2654435761u);
    double lo = 1e9, hi = 0;
    for (uint32_t t = 0; t + 500 <= k.t_stop; t += 100) {
      const double dd = k.dist(t + 500) - k.dist(t);
      lo = dd < lo ? dd : lo;
      hi = dd > hi ? dd : hi;
      fwd &= dd > 0;
    }
    varies |= hi - lo > 30;
  }
  OK(varies, "the speed changes along a walk");
  OK(fwd, "and it never goes backwards");
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
