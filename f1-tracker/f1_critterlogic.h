#pragma once
// What the screen's visitors (the dinosaur's walk, the toucan's flight) share and that needs no screen:
// the random numbers, and the gap between visits. tools/critter_common.py is its Python twin.
#include <cstdint>

namespace f1 {
namespace critter {

inline constexpr uint32_t GAP_MIN_MS = 3u * 60000u, GAP_MAX_MS = 12u * 60000u;   // between visits

// xorshift32: the same numbers on the device and in the render tool.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
  double next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (s >> 8) / 16777216.0;
  }
};

inline uint32_t next_gap_ms(uint32_t seed) {
  Rng r(seed);
  return GAP_MIN_MS + (uint32_t) (r.next() * (GAP_MAX_MS - GAP_MIN_MS));
}

}  // namespace critter
}  // namespace f1
