#pragma once
// A dinosaur walks across the screen now and then, stops in the middle, roars a word in a speech
// bubble, and walks on (the timing is f1_dinologic.h, the pictures f1_dino_data.h). The drawing, the
// snapshot backdrop and the timers are the shared engine, f1_critter.h: read its notes.
#include "f1_critter.h"
#include "f1_dinologic.h"
#include "f1_dino_data.h"

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
namespace f1 {
namespace dino {

struct Traits {
  using Walk = dino::Walk;
  using Frame = dino::Frame;
  static constexpr int W = DATA_W, H = DATA_H, SCREEN_W = dino::SCREEN_W, SCREEN_H = dino::SCREEN_H, N_POSES = dino::N_POSES;
  static constexpr const char *TAG = "dino";
  static Walk make(uint32_t seed) { return make_walk(seed); }
  static Frame at(const Walk &w, uint32_t ms) { return dino::at(w, ms); }
  static bool finished(const Walk &w, uint32_t ms) { return dino::finished(w, ms); }
  static uint32_t gap(uint32_t seed) { return next_gap_ms(seed); }
  static const char *word(const Walk &w) { return WORDS[w.word]; }
  static const uint8_t *pose(int p) { return POSES[p]; }
  // a hungry visit (f1_critterbite.h): the poses, where the mouth is, how it moves and lunges, what it says
  static constexpr int P_REST = STAND, P_HALF = ROAR_HALF, P_OPEN = ROAR_OPEN, MOUTH_DY = DATA_H * 30 / 100;
  static constexpr int LUNGE_X = 12, LUNGE_Y = 5, MOUTH_KIND = critter::JAW;
  static const char *hungry_word(uint32_t seed) { return HUNGRY_WORDS[(uint32_t) (Rng(seed ^ 0x4E6Fu).next() * N_HUNGRY_WORDS) % N_HUNGRY_WORDS]; }
  static constexpr double STEP = STEP_PX, MOVE_SPEED = 70.0;
  static void aim(Walk &w, int lane, int stop_x, bool flip) {
    w.flip = flip;
    w.lane = lane;
    w.stop_x = stop_x;
    w.t_stop = w.time_for(stop_x + SW);
    w.t_end = w.t_stop + w.pause_ms + (w.time_for(dino::SCREEN_W + SW) - w.t_stop);
  }
};
using C = critter::Critter<Traits>;     // C::bubble_font and C::enabled are the project's to set

inline void tick(uint32_t now_ms, bool welcome) { C::tick(now_ms, welcome); }
inline bool walking() { return C::active(); }
// Decision 202: a web button asks for a walk - the next tick that finds the panel free starts one.
inline void walk_now() { C::visit_now(); }
inline void eat_now() { C::hungry_now(); }      // MOTION-7: ... and it eats the card's picture

}  // namespace dino
}  // namespace f1
#endif  // device only
