#pragma once
// What a bite looks like on a face (MOTION-8, Carl): not a hole cut out of the picture but a wound on it.
// Pure pixel logic, no screen, so it is host-tested and previewed (tests/wound_preview.cpp draws it over
// the portraits).
//
// A bite is an oval a little wider than it is high, torn at the edge (the radius wanders with the angle
// and a per-pixel jitter), in layers from the middle out:
//   - the wound bed, near black at the middle to red, wet-looking (speckle and a small gloss);
//   - the torn flesh around it, brighter red, with a thin line of pale tissue at the lip;
//   - the bruise: a translucent red-to-purple halo that fades into the skin;
//   - the tooth marks: two rows of punctures, along the top and the bottom of the oval, each a dark pit
//     with a darker red rim.
// From the bottom lip blood runs: 2-4 drips of different lengths and widths, each wavering a little as
// it goes, with a gloss streak, ending in a bead. They grow after the bite (drip_len), so the wound is
// seen to bleed, and stay.
//
// All colours are ARGB8888 with straight alpha, as LVGL's overlay image holds them; `over` composites.
#include <cmath>
#include <cstdint>

#include "f1_critterlogic.h"

namespace f1 {
namespace wound {

inline constexpr int MAX_DRIPS = 4;
inline constexpr uint32_t DRIP_MS = 3200;            // a drip takes this long to reach its length

struct Wound {
  int x = 0, y = 0;          // the mouth point, in the overlay's own pixels
  int r = 20;                // size
  bool left = false;         // the biter faces left: the face is to the left of the point
  uint32_t seed = 1;
};

struct Drip {
  int x = 0, y = 0;          // where it leaves the wound
  int len = 0;               // its length when it has stopped
  int w = 2;                 // its width
  double ph = 0;             // how it wavers
};

inline uint32_t hash2(uint32_t a, uint32_t b, uint32_t s) {
  uint32_t h = a * 374761393u + b * 668265263u + s * 2246822519u;
  h = (h ^ (h >> 13)) * 1274126177u;
  return h ^ (h >> 16);
}
inline double unit(uint32_t h) { return (h & 0xFFFFu) / 65536.0; }

inline uint32_t argb(int a, int r, int g, int b) {
  auto c = [](int v) { return (uint32_t) (v < 0 ? 0 : (v > 255 ? 255 : v)); };
  return (c(a) << 24) | (c(r) << 16) | (c(g) << 8) | c(b);
}
inline uint32_t mix(uint32_t p, uint32_t q, double t) {      // straight per-channel blend, alpha too
  t = t < 0 ? 0 : (t > 1 ? 1 : t);
  uint32_t o = 0;
  for (int s = 0; s < 32; s += 8) o |= (uint32_t) (((p >> s) & 255) * (1 - t) + ((q >> s) & 255) * t + 0.5) << s;
  return o;
}

// Source over destination, straight alpha.
inline uint32_t over(uint32_t d, uint32_t s) {
  const uint32_t sa = s >> 24, da = d >> 24;
  if (sa == 255 || da == 0) return s ? s : d;
  if (sa == 0) return d;
  const uint32_t oa = sa + da * (255 - sa) / 255;
  uint32_t o = oa << 24;
  for (int sh = 0; sh <= 16; sh += 8) {
    const uint32_t sc = (s >> sh) & 255, dc = (d >> sh) & 255;
    o |= ((sc * sa * 255 + dc * da * (255 - sa)) / (oa * 255)) << sh;
  }
  return o;
}

// The oval's half-width and half-height, and where its middle is (a little in from the mouth point).
struct Shape { double cx, cy, a, b; };
inline Shape shape(const Wound &w) {
  return {w.x + (w.left ? -0.55 : 0.55) * w.r, (double) w.y, 1.05 * w.r, 0.82 * w.r};
}
// Pixels from the mouth point beyond which nothing of the wound itself is drawn.
inline int extent(const Wound &w) { return (int) (1.7 * w.r + 8); }

// How ragged the edge is at an angle: about -1..1, from the seed.
inline double ragged(const Wound &w, double th) {
  const double p1 = unit(hash2(w.seed, 1, 7)) * 6.28, p2 = unit(hash2(w.seed, 2, 7)) * 6.28, p3 = unit(hash2(w.seed, 3, 7)) * 6.28;
  return 0.5 * std::sin(3 * th + p1) + 0.3 * std::sin(5 * th + p2) + 0.2 * std::sin(9 * th + p3);
}

// The tooth marks: where the punctures are (in the oval's own units), up to 14.
inline int teeth(const Wound &w, double *tx, double *ty) {
  const int per = 5 + (int) (hash2(w.seed, 9, 3) % 2);
  int n = 0;
  for (int row = 0; row < 2; row++)
    for (int i = 0; i < per; i++) {
      const double span = 2.2, t0 = row ? 0.47 : 3.14159 + 0.47;     // angles: top row from the left, bottom row
      const double jit = (unit(hash2(w.seed, 20 + row * 16 + i, 4)) - 0.5) * 0.16;
      const double th = t0 + span * (per > 1 ? (double) i / (per - 1) : 0.5) + jit;
      const double rr = 0.80 + (unit(hash2(w.seed, 50 + row * 16 + i, 5)) - 0.5) * 0.10;
      tx[n] = rr * std::cos(th);
      ty[n] = rr * std::sin(th);
      n++;
    }
  return n;
}

// The wound at pixel (px, py): 0 where there is none.
inline uint32_t wound_argb(const Wound &w, int px, int py) {
  const Shape s = shape(w);
  const double u = (px - s.cx) / s.a, v = (py - s.cy) / s.b;
  const double rad = std::sqrt(u * u + v * v);
  if (rad > 1.5) return 0;
  const double th = std::atan2(v, u);
  const double jit = (unit(hash2((uint32_t) px, (uint32_t) py, w.seed)) - 0.5) * 0.07;
  const double e = rad / (1.0 + 0.13 * ragged(w, th)) + jit;       // the torn edge
  // tooth marks first: they are pits in whatever is under them
  double tx[14], ty[14];
  const int nt = teeth(w, tx, ty);
  const double pr = 2.3, rim = 3.5;
  for (int i = 0; i < nt; i++) {
    const double dx = px - (s.cx + tx[i] * s.a), dy = py - (s.cy + ty[i] * s.b);
    const double d = std::sqrt(dx * dx + dy * dy);
    if (d <= pr) return argb(255, 24 + (int) (d * 6), 2, 5);
    if (d <= rim) return mix(argb(255, 120, 14, 22), argb(255, 70, 6, 12), (d - pr) / (rim - pr));
  }
  if (e < 0.64) {                                                   // the bed
    const double t = e / 0.64;
    uint32_t c = mix(argb(255, 44, 3, 8), argb(255, 150, 16, 24), t * t);
    const double sp = unit(hash2((uint32_t) px, (uint32_t) py, w.seed + 11));
    c = mix(c, sp < 0.5 ? argb(255, 20, 0, 4) : argb(255, 190, 40, 44), 0.18 * sp);     // wet speckle
    const double gx = px - (s.cx - 0.25 * s.a), gy = py - (s.cy - 0.38 * s.b);          // a gloss on it
    if (gx * gx + gy * gy < 5.0 && e > 0.18) c = mix(c, argb(255, 238, 120, 120), 0.75 - 0.15 * std::sqrt(gx * gx + gy * gy));
    return c;
  }
  if (e < 0.9) {                                                    // the torn flesh
    const double t = (e - 0.64) / 0.26;
    uint32_t c = mix(argb(255, 150, 16, 24), argb(255, 205, 58, 66), t);
    if (e > 0.82) c = mix(c, argb(255, 236, 176, 168), (e - 0.82) / 0.08);     // the pale lip of the tear
    return c;
  }
  if (e < 1.42) {                                                   // the bruise
    const double t = (e - 0.9) / 0.52, a = 175.0 * std::pow(1 - t, 1.4);
    const uint32_t c = mix(argb(0, 168, 40, 52), argb(0, 104, 52, 96), t);
    return (c & 0x00FFFFFFu) | ((uint32_t) (a < 0 ? 0 : a) << 24);
  }
  return 0;
}

// Where the blood runs from: n drips along the bottom lip.
inline int plan_drips(const Wound &w, critter::Rng &rng, Drip *out) {
  const Shape s = shape(w);
  const int n = 2 + (int) (rng.next() * 3);                            // 2..4
  int longest = 0;
  for (int i = 0; i < n; i++) {
    const double u = -0.62 + 1.24 * (n > 1 ? (double) i / (n - 1) : 0.5) + (rng.next() - 0.5) * 0.18;
    const double yy = s.cy + s.b * std::sqrt(1 - u * u) * 0.9;
    Drip d;
    d.x = (int) std::lround(s.cx + u * s.a);
    d.y = (int) std::lround(yy);
    d.len = 14 + (int) (rng.next() * 58);
    d.w = 2;
    d.ph = rng.next() * 6.28;
    out[i] = d;
    if (d.len > out[longest].len) longest = i;
  }
  out[longest].w = 3;
  return n;
}

// How long a drip is `age_ms` after the bite: fast at first, slowing.
inline int drip_len(const Drip &d, uint32_t age_ms) {
  const double u = age_ms >= DRIP_MS ? 1.0 : (double) age_ms / DRIP_MS;
  return (int) (d.len * (1 - (1 - u) * (1 - u)));
}

// The drip at pixel (px, py) when it is `len` long; `bead`: with the drop at its end (the finished drip).
inline uint32_t drip_argb(const Drip &d, int len, bool bead, int px, int py) {
  const int dy = py - d.y;
  if (dy < -1 || dy > len + 3) return 0;
  const double cx = d.x + 1.3 * std::sin(0.09 * (dy > 0 ? dy : 0) + d.ph);
  double hw = d.w / 2.0 + (dy < 7 ? 0.9 * (7 - dy) / 7.0 : 0.0);        // wider where it leaves the wound
  const double off = px - cx;
  if (dy <= len) {
    const double ao = std::fabs(off);
    if (ao <= hw + 2.0 && ao > hw + 0.5) return argb(60, 150, 16, 24);   // a wet film beside it
    if (ao > hw + 0.5) return 0;
    uint32_t c = ao > hw - 0.7 ? argb(255, 112, 7, 14) : argb(255, 184, 18, 28);
    if (ao > hw - 0.5) c = (c & 0x00FFFFFFu) | ((uint32_t) (255 * (hw + 0.5 - ao)) << 24);      // an antialiased edge
    if (off < -hw * 0.15 && off > -hw * 0.75 && ((dy / 3) % 5) != 4) c = argb(255, 224, 74, 82);   // the gloss
    return c;
  }
  if (!bead) return 0;
  const double bx = px - (d.x + 0.9 * std::sin(0.09 * len + d.ph)), by = py - (d.y + len + 1.5);
  const double br = hw + 1.3, dd = std::sqrt(bx * bx + by * by);
  if (dd > br) return 0;
  uint32_t c = dd > br - 0.9 ? argb(255, 112, 7, 14) : argb(255, 176, 16, 26);
  if (bx < 0 && by < 0 && dd < br * 0.7) c = argb(255, 232, 96, 104);
  return c;
}

}  // namespace wound
}  // namespace f1
