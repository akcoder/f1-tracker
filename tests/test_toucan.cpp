// The toucan's timing (f1_toucanlogic.h): the flight, the perch and the beak, the way out, and the
// numbers tools/toucan_art.py (its Python twin) prints for seed 7.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "../f1-tracker/f1_toucanlogic.h"
using namespace f1::toucan;
static int checks = 0, failures = 0;
#define OK(c, m) do { checks++; if (!(c)) { failures++; std::printf("  FAIL  %s\n", m); } } while (0)
int main() {
  std::printf("\nF1 Tracker - toucan tests\n\n");
  // pinned to `python3 tools/toucan_art.py` (seed 7)
  const Flight w = make_flight(7);
  OK(w.flip && w.lane == 97 && w.stop_x == 257 && w.pause_ms == 2864 && w.word == 3, "seed 7: the flight");
  OK(std::fabs(w.speed - 136.657) < 0.01, "seed 7: speed");
  OK(w.t_stop == 3238 && w.t_end == 7768, "seed 7: the perch and the end");
  Frame f = at(w, 0);        OK(f.x == 480 && f.y == 97 && f.pose == FLY0 && !f.bubble, "t=0: off the right edge");
  f = at(w, 1000);           OK(f.x == 349 && f.y == 89 && f.pose == FLY1, "t=1000");
  f = at(w, 2500);           OK(f.x == 145 && f.y == 108 && f.pose == FLY3, "t=2500: the wings follow the distance");
  f = at(w, w.t_stop);       OK(f.x == 43 && f.y == 97 && f.pose == PERCH && !f.bubble, "perched, beak shut, no bubble yet, no swoop left");
  f = at(w, w.t_stop + 400); OK(f.pose == PERCH && f.bubble, "the bubble appears");
  f = at(w, w.t_stop + 1000); OK(f.pose == BEAK_HALF && f.bubble, "the beak clacks");
  f = at(w, w.t_end - 1);    OK(f.x == -180 && f.pose == FLY1, "leaving off the left edge");
  OK(next_gap_ms(7) == 180237, "seed 7: the gap");
  OK(std::strcmp(WORDS[0], "Just follow your nose!") == 0, "it says what it was asked to say");
  bool lanes = true, stops = true, words = true, cont = true, anyr = false, anyl = false, nose = false;
  for (uint32_t s = 1; s <= 2000; s++) {
    const Flight k = make_flight(s * 2654435761u);
    lanes &= k.lane - (int) WAVE_AMP - 1 >= 0 && k.lane + (int) WAVE_AMP + 1 + SH <= SCREEN_H;
    stops &= k.stop_x >= STOP_MIN && k.stop_x <= STOP_MAX;
    words &= k.word >= 0 && k.word < N_WORDS;
    nose |= std::strcmp(WORDS[k.word], "Just follow your nose!") == 0;
    (k.flip ? anyl : anyr) = true;
    Frame p = at(k, 0);
    for (uint32_t t = 40; t <= k.t_end; t += 40) {
      const Frame c = at(k, t);
      cont &= std::abs(c.x - p.x) <= 9 && std::abs(c.y - p.y) <= 6;      // at most ~200 px/s * 40 ms; the swoop is gentle
      p = c;
    }
    OK(finished(k, k.t_end) && !finished(k, k.t_end - 1), "a flight ends at t_end");
    const uint32_t g = next_gap_ms(s);
    words &= g >= GAP_MIN_MS && g <= GAP_MAX_MS;
  }
  OK(lanes, "the swoops keep the sprite on screen");
  OK(stops, "the perch is inside the screen");
  bool whole = true;
  for (uint32_t s2 = 1; s2 <= 2000; s2++) {
    const Flight k = make_flight(s2 * 2654435761u);
    const Frame p = at(k, k.t_stop);
    whole &= p.x >= 0 && p.x + SW <= SCREEN_W;
  }
  OK(whole, "perched, the whole bird is on screen, flying either way");
  OK(words, "words and gaps in range");
  OK(nose, "'Just follow your nose!' does come up");
  OK(cont, "no jump: the flight is continuous across the perch");
  OK(anyl && anyr, "both directions occur");
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
