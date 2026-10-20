#pragma once
// A toucan flies across the screen now and then, perches in the middle, says something in a speech
// bubble, and flies on (the timing is f1_toucanlogic.h, the pictures f1_toucan_data.h). The drawing,
// the snapshot backdrop and the timers are the shared engine, f1_critter.h: read its notes.
#include "f1_critter.h"
#include "f1_toucanlogic.h"
#include "f1_toucan_data.h"

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
namespace f1 {
namespace toucan {

struct Traits {
  using Walk = Flight;
  using Frame = toucan::Frame;
  static constexpr int W = DATA_W, H = DATA_H, SCREEN_W = toucan::SCREEN_W, SCREEN_H = toucan::SCREEN_H, N_POSES = toucan::N_POSES;
  static constexpr const char *TAG = "toucan";
  static Walk make(uint32_t seed) { return make_flight(seed); }
  static Frame at(const Walk &w, uint32_t ms) { return toucan::at(w, ms); }
  static bool finished(const Walk &w, uint32_t ms) { return toucan::finished(w, ms); }
  static uint32_t gap(uint32_t seed) { return next_gap_ms(seed); }
  static const char *word(const Walk &w) { return WORDS[w.word]; }
  static const uint8_t *pose(int p) { return POSES[p]; }
  // a hungry visit (f1_critterbite.h): the poses, where the mouth is, how it moves and lunges, what it says
  static constexpr int P_REST = PERCH, P_HALF = BEAK_HALF, P_OPEN = BEAK_OPEN, MOUTH_DY = DATA_H * 45 / 100;
  static constexpr int LUNGE_X = 18, LUNGE_Y = 9, MOUTH_KIND = critter::BILL;
  static const char *hungry_word(uint32_t) { return "Fruit salad time!"; }
  static constexpr double STEP = STEP_PX, MOVE_SPEED = 130.0;
  static void aim(Walk &w, int lane, int stop_x, bool flip) {
    w.flip = flip;
    w.lane = lane;
    w.stop_x = stop_x;
    w.t_stop = w.time_for(stop_x + SW);
    w.t_end = w.t_stop + w.pause_ms + (w.time_for(toucan::SCREEN_W + SW) - w.t_stop);
  }
};
using C = critter::Critter<Traits>;     // C::bubble_font and C::enabled are the project's to set

inline void tick(uint32_t now_ms, bool welcome) { C::tick(now_ms, welcome); }
inline bool flying() { return C::active(); }
// Decision 204: a web button asks for a visit - the next tick that finds the panel free starts one.
inline void visit_now() { C::visit_now(); }
inline void eat_now() { C::hungry_now(); }      // MOTION-7: ... and it eats the card's picture

}  // namespace toucan
}  // namespace f1
#endif  // device only
