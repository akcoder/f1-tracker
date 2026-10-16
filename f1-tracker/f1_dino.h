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
};
using C = critter::Critter<Traits>;     // C::bubble_font and C::enabled are the project's to set

inline void tick(uint32_t now_ms, bool welcome) { C::tick(now_ms, welcome); }
inline bool walking() { return C::active(); }
// Decision 202: a web button asks for a walk - the next tick that finds the panel free starts one.
inline void walk_now() { C::visit_now(); }

}  // namespace dino
}  // namespace f1
#endif  // device only
