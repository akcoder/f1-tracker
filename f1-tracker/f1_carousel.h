#pragma once
// The carousel rotation (UI-20a / decision 68). LVGL-free so tests/ can build
// it on the host.
//
// Three card types share one rotation. A random draw clusters - with 40
// circuits, 22 drivers and 32 legends you get three legends in a row and then
// eleven circuits - so the order is a fixed interleave instead, with each list
// keeping its own cursor. Lists need not be the same length and every card is
// reached.
#include <cstdint>

namespace f1 {
namespace carousel {

enum CardType : uint8_t { CIRCUIT = 0, DRIVER = 1, LEGEND = 2, N_TYPES = 3 };

// UI-20a: circuit -> driver -> circuit -> legend, repeating. Circuits appear
// twice as often because there are more of them and they are the subject.
inline constexpr CardType PATTERN[] = {CIRCUIT, DRIVER, CIRCUIT, LEGEND};
inline constexpr int PATTERN_LEN = 4;

// Which types the Carousel Content setting admits (section 7).
enum Content : uint8_t {
  ALL = 0, CIRCUITS_ONLY, DRIVERS_ONLY, LEGENDS_ONLY, CIRCUITS_AND_DRIVERS
};

inline bool allows(Content c, CardType t) {
  switch (c) {
    case CIRCUITS_ONLY:        return t == CIRCUIT;
    case DRIVERS_ONLY:         return t == DRIVER;
    case LEGENDS_ONLY:         return t == LEGEND;
    case CIRCUITS_AND_DRIVERS: return t == CIRCUIT || t == DRIVER;
    case ALL: default:         return true;
  }
}

struct Card { CardType type; int index; };

// One cursor per list plus a position in the pattern.
class Rotation {
 public:
  void configure(int n_circuits, int n_drivers, int n_legends, Content content) {
    n_[CIRCUIT] = n_circuits; n_[DRIVER] = n_drivers; n_[LEGEND] = n_legends;
    content_ = content;
    if (step_ >= PATTERN_LEN) step_ = 0;
    for (int t = 0; t < N_TYPES; t++)
      if (n_[t] <= 0 || cur_[t] >= n_[t]) cur_[t] = 0;
  }

  // UI-20c: start the circuit list at the next round during a race week, so
  // the carousel feels like it knows what is coming up.
  void set_circuit_cursor(int i) {
    if (n_[CIRCUIT] > 0 && i >= 0 && i < n_[CIRCUIT]) cur_[CIRCUIT] = i;
  }

  // A card injected every `every` draws, on top of the normal walk - used for a
  // driver's birthday. It does not consume a cursor, so nothing else is
  // starved: the rotation resumes exactly where it was.
  void set_priority(CardType t, int index, int every) {
    pri_type_ = t;
    pri_index_ = index;
    pri_every_ = every > 1 ? every : 2;
    if (index < 0) pri_count_ = 0;
  }
  void clear_priority() { pri_index_ = -1; pri_count_ = 0; }
  bool has_priority() const { return pri_index_ >= 0; }

  bool any() const {
    for (int t = 0; t < N_TYPES; t++)
      if (n_[t] > 0 && allows(content_, (CardType) t)) return true;
    return false;
  }

  // Next card, advancing the cursor of whichever list it came from. Skips
  // types the content filter excludes or that have no entries; returns false
  // only when nothing at all can be shown.
  bool next(Card &out) {
    if (!any()) return false;
    // The priority card goes first in its slot, and only if the content filter
    // admits its type - a birthday driver must not override "circuits only".
    if (pri_index_ >= 0 && allows(content_, pri_type_) &&
        pri_index_ < n_[pri_type_]) {
      if (++pri_count_ >= pri_every_) {
        pri_count_ = 0;
        out.type = pri_type_;
        out.index = pri_index_;
        return true;
      }
    }
    for (int tries = 0; tries < PATTERN_LEN * N_TYPES + N_TYPES; tries++) {
      const CardType t = PATTERN[step_];
      step_ = (step_ + 1) % PATTERN_LEN;
      if (n_[t] <= 0 || !allows(content_, t)) continue;
      out.type = t;
      out.index = cur_[t];
      cur_[t] = (cur_[t] + 1) % n_[t];
      return true;
    }
    return false;
  }

  int cursor(CardType t) const { return cur_[t]; }

 private:
  int n_[N_TYPES] = {0, 0, 0};
  int cur_[N_TYPES] = {0, 0, 0};
  int step_ = 0;
  Content content_ = ALL;
  CardType pri_type_ = DRIVER;
  int pri_index_ = -1;
  int pri_every_ = 4;
  int pri_count_ = 0;
};

// The interval setting is a restored number, and plane-tracker decision 60
// found that ESPHome restores stored bytes WITHOUT clamping to min/max - a
// select-to-number change there fed an old option index straight in. Clamp at
// boot and again at the point of use.
inline int clamp_interval_s(float v) {
  if (!(v >= 15.0f)) return 45;        // also catches NaN
  if (v > 120.0f) return 120;
  return (int) v;
}

}  // namespace carousel
}  // namespace f1
