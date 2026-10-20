#pragma once
// Carl, the grey llama in the green stocking cap (MOTION-8). He walks in like the dinosaur (the timing is
// f1_dinologic.h's, the pictures f1_carl_data.h), and every visit he makes is to bite a person in the face: the
// driver or legend whose card is up, where f1_faces.h says the face is. He comes about once a visit in a hundred
// (the gap is long, see below), and not at all on a card with no face. The bite itself is f1_critter.h's hungry
// visit, with the marks drawn as wounds (f1_wound.h) that bleed and stay until the card changes.
#include "f1_critter.h"
#include "f1_dinologic.h"
#include "f1_carl_data.h"

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
namespace f1 {
namespace carl {

// One visit in a hundred: the dinosaur and the toucan come every 3-12 minutes between them (a visit every
// 3.75 minutes), so Carl's gap averages a hundred times that, six and a quarter hours.
inline constexpr uint32_t GAP_MIN_MS = 3u * 3600000u, GAP_MAX_MS = 9u * 3600000u + 30u * 60000u;
inline constexpr int N_HUNGRY_WORDS = 6;
inline const char *const WORDS_[N_HUNGRY_WORDS] = {"Oh, hello.", "I'm Carl.", "Nice face.", "Mmm. Yum.", "Oh, hi!", "Hello there."};

struct Traits {
  using Walk = dino::Walk;
  using Frame = dino::Frame;
  static constexpr bool ONLY_FACE = true;
  static constexpr int W = DATA_W, H = DATA_H, SCREEN_W = dino::SCREEN_W, SCREEN_H = dino::SCREEN_H, N_POSES = dino::N_POSES;
  static constexpr const char *TAG = "carl";
  static Walk make(uint32_t seed) { return dino::make_walk(seed); }
  static Frame at(const Walk &w, uint32_t ms) { return dino::at(w, ms); }
  static bool finished(const Walk &w, uint32_t ms) { return dino::finished(w, ms); }
  static uint32_t gap(uint32_t seed) {
    return GAP_MIN_MS + (uint32_t) (critter::Rng(seed).next() * (GAP_MAX_MS - GAP_MIN_MS));
  }
  static const char *word(const Walk &) { return WORDS_[0]; }
  static const uint8_t *pose(int p) { return POSES[p]; }
  static constexpr int P_REST = dino::STAND, P_HALF = dino::ROAR_HALF, P_OPEN = dino::ROAR_OPEN, MOUTH_DY = DATA_H * 34 / 100;
  static constexpr int LUNGE_X = 14, LUNGE_Y = 6, MOUTH_KIND = critter::WOUND;
  static const char *hungry_word(uint32_t seed) { return WORDS_[(uint32_t) (critter::Rng(seed ^ 0xCA41u).next() * N_HUNGRY_WORDS) % N_HUNGRY_WORDS]; }
  static constexpr double STEP = dino::STEP_PX, MOVE_SPEED = 70.0;
  static void aim(Walk &w, int lane, int stop_x, bool flip) {
    w.flip = flip;
    w.lane = lane;
    w.stop_x = stop_x;
    w.t_stop = w.time_for(stop_x + dino::SW);
    w.t_end = w.t_stop + w.pause_ms + (w.time_for(dino::SCREEN_W + dino::SW) - w.t_stop);
  }
};
using C = critter::Critter<Traits>;     // C::bubble_font and C::enabled are the project's to set

inline void tick(uint32_t now_ms, bool welcome) { C::tick(now_ms, welcome); }
inline bool walking() { return C::active(); }
// A web button: Carl comes at the next tick that finds a face on the screen.
inline void visit_now() { C::visit_now(); }

}  // namespace carl
}  // namespace f1
#endif  // device only
