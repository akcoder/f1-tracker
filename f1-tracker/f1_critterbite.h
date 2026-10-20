#pragma once
// A HUNGRY visit (MOTION-7), the part that needs no screen. One visit in four, the dinosaur or the toucan
// does not just stand and say a word: it stops at the card's picture - the circuit's trace, or a person's
// portrait - and eats it. After plane-tracker's UI-68 eating is not plowing a path through the screen: it
// is ONE thing, taken in 3 to 5 bites, each a different size and in a different place along it. The
// critter's head goes to each bite (a lunge: the sprite is one picture per pose, so the whole bird moves),
// the beak opens going in and chews after, crumbs fall, and it moves on along the item to the next bite.
// When it has had enough it says its word and leaves; the bite marks stay until the card changes.
//
// Here: the plan (where the bites are, when, where the mouth is at any moment) and the walk along a trace.
// The drawing is f1_critter.h; tools/render_pages.py (_Hungry) is the picture this was built from.
#include <cmath>
#include <cstdint>

#include "f1_critterlogic.h"

namespace f1 {
namespace critter {

inline constexpr int BITES_MIN = 3, BITES_MAX = 5;
inline constexpr uint32_t HUNGRY_ONE_IN = 4;          // a visit is hungry one time in this many
inline constexpr int BITE_R_MIN = 17, BITE_R_SPAN = 10;
inline constexpr uint32_t OPEN_MS = 260, CHEW_MS = 600, MOVE_PAD_MS = 150;   // beak open before a bite; chewing after; a beat each side of a move

struct Pt { int x = 0, y = 0; };

// The shape of a bite is the shape of the mouth that took it (owner: "toucan does not have a round mouth,
// neither does the dinosaur"): the toucan's long bill makes a narrow lens-shaped notch, pointing in; the
// dinosaur's wide jaw a broad bite with a toothed edge. Both are drawn forward from the mouth point P.
enum MouthKind { JAW = 0, BILL = 1, WOUND = 2 };   // WOUND: Carl's, drawn by f1_wound.h
inline constexpr int BITE_REACH_X10 = 28;               // a bite reaches no further than this x 0.1 r from P (+ the rim)

// Is the pixel (dx, dy) from the mouth point inside a bite of size r? `left`: the critter faces left.
inline bool bite_in(int kind, bool left, int dx, int dy, int r) {
  const double u = left ? -dx : dx, v = dy, R = r;
  if (kind == BILL) {
    const double L = 2.6 * R, H = 0.55 * R;
    const double t = (u + 0.3 * L) / L;                  // along the lens, 0..1: centred a little ahead of P
    if (t < 0 || t > 1) return false;
    const double w = 2 * t - 1;
    return std::fabs(v) <= H * (1 - w * w);
  }
  if (u < -0.3 * R || std::fabs(v) > 0.85 * R) return false;
  const double w = v / (0.42 * R) + 100.0, f = w - std::floor(w);   // teeth: a triangle wave along the jaw
  const double tooth = 0.35 * R * std::fabs(f - 0.5) * 2;
  return u <= 1.1 * R - tooth;
}

// What to eat, as the project sees it: the box the picture is in, and whether it is a trace (a thin line
// the critter walks along) or a portrait (bitten along its right edge, the critter facing it).
struct Food {
  int x = 0, y = 0, w = 0, h = 0;
  bool trace = false;
  int fx = 0, fy = 0, fw = 0, fh = 0;      // a portrait with a known face: the face's box on the screen (fw 0: none)
};

inline double smooth(double u) {
  u = u < 0 ? 0 : (u > 1 ? 1 : u);
  return u * u * (3 - 2 * u);
}

// How many bites this visit takes, from its seed.
inline int bite_count(uint32_t seed) { return BITES_MIN + (int) (Rng(seed ^ 0xB17Eu).next() * (BITES_MAX - BITES_MIN + 1)); }

// A portrait: n bites down its right edge, in no particular order (the bird hops about), with a little
// jitter in and down, from 26 % to 80 % of the way down.
inline int edge_points(const Food &f, int n, Rng &r, Pt *out) {
  int ys[BITES_MAX];
  const int y0 = f.y + f.h * 26 / 100, span = f.h * 54 / 100;
  for (int i = 0; i < n; i++) ys[i] = y0 + (n > 1 ? i * span / (n - 1) : span / 2) + (int) ((r.next() - 0.5) * 24);
  for (int i = n - 1; i > 0; i--) {                       // not top to bottom
    const int j = (int) (r.next() * (i + 1));
    const int t = ys[i]; ys[i] = ys[j]; ys[j] = t;
  }
  for (int i = 0; i < n; i++) { out[i].x = f.x + f.w - (int) (r.next() * 10); out[i].y = ys[i]; }
  return n;
}

// A face (Carl's): n bites in the box, on the side the biter comes from (it faces left, so the right of the
// face), spread from the brow to the chin in no particular order, each at its own depth in.
inline int face_points(const Food &f, int n, Rng &r, Pt *out) {
  int ys[BITES_MAX];
  for (int i = 0; i < n; i++) ys[i] = f.fy + (int) (f.fh * (0.28 + 0.56 * (n > 1 ? (double) i / (n - 1) : 0.5)) + (r.next() - 0.5) * 0.12 * f.fh);
  for (int i = n - 1; i > 0; i--) {
    const int j = (int) (r.next() * (i + 1));
    const int t = ys[i]; ys[i] = ys[j]; ys[j] = t;
  }
  for (int i = 0; i < n; i++) { out[i].x = f.fx + (int) (f.fw * (0.45 + 0.38 * r.next())); out[i].y = ys[i]; }
  return n;
}

// A trace: n points along the line from p0, each 34-52 px on from the last and keeping the way it was
// going (up the line first). `is_trace(x, y)` says whether a pixel is part of the line; only pixels inside
// the box and at or right of min_x count, so the visitor stays on screen. Fewer than n if the line ends.
template <class IsTrace>
inline int trace_walk(IsTrace is_trace, const Food &box, int min_x, Pt p0, int n, Rng &r, Pt *out) {
  out[0] = p0;
  int k = 1;
  double dx0 = 0, dy0 = -1;
  while (k < n) {
    const Pt c = out[k - 1];
    struct Cand { double score; Pt p; } best[6];
    int nb = 0;
    for (int dy = -52; dy <= 52; dy++)
      for (int dx = -52; dx <= 52; dx++) {
        const double d = std::sqrt((double) (dx * dx + dy * dy));
        if (d < 34 || d > 52) continue;
        const int x = c.x + dx, y = c.y + dy;
        if (x < box.x || x >= box.x + box.w || y < box.y || y >= box.y + box.h || x < min_x || !is_trace(x, y)) continue;
        const double s = (dx * dx0 + dy * dy0) / d;
        int i;
        if (nb < 6) i = nb++;                               // keep the six best, best first
        else if (s > best[5].score) i = 5;
        else continue;
        best[i] = {s, {x, y}};
        while (i > 0 && best[i].score > best[i - 1].score) { const Cand t = best[i]; best[i] = best[i - 1]; best[i - 1] = t; i--; }
      }
    if (nb == 0) break;
    int ok = 0;
    while (ok < nb && best[ok].score > 0.4) ok++;
    if (ok == 0) ok = nb < 3 ? nb : 3;
    const Pt q = best[(int) (r.next() * ok) % ok].p;
    const double d = std::sqrt((double) ((q.x - c.x) * (q.x - c.x) + (q.y - c.y) * (q.y - c.y)));
    dx0 = (q.x - c.x) / d;
    dy0 = (q.y - c.y) / d;
    out[k++] = q;
  }
  return k;
}

// The visit once the critter has arrived at the first bite (t0): chomp k at tb[k], a chew of its own
// length after it, a move along the item to the next point; then the word, then away.
struct Hunger {
  int n = 0;                                             // the bites at the item, with moves between them
  int nb = 0;                                            // all the bites: n, and the afterthought on the way out
  Pt pt[BITES_MAX + 1];
  int r[BITES_MAX + 1] = {0};
  uint32_t tb[BITES_MAX + 1] = {0};
  uint32_t mv0[BITES_MAX] = {0}, mv1[BITES_MAX] = {0};   // move k -> k+1
  uint32_t t0 = 0, t_say = 0, t_exit = 0, t_end = 0;
  double speed = 70.0;
  bool extra = false;                                    // one more bite on the way out (owner: stops at a random point)
  uint32_t ex_walk = 0, ex_pause = 0;                    // ms of walking from t_exit to the stop; ms stood there

  // pts: where the mouth goes, first to last; t0: when the critter is at the first.
  void plan(const Pt *pts, int count, uint32_t t_arrive, double move_speed, uint32_t bubble_ms, Rng &rng) {
    n = count > BITES_MAX ? BITES_MAX : count;
    nb = n;
    extra = false;
    t0 = t_arrive;
    speed = move_speed;
    for (int i = 0; i < n; i++) { pt[i] = pts[i]; r[i] = BITE_R_MIN + (int) (rng.next() * BITE_R_SPAN); }
    uint32_t t = t0 + 500;
    for (int k = 0; k < n; k++) {
      tb[k] = t;
      t += 750 + (uint32_t) (rng.next() * 500);              // the chew, a different length each time
      if (k + 1 < n) {
        const double dx = pt[k + 1].x - pt[k].x, dy = pt[k + 1].y - pt[k].y;
        const uint32_t mv = (uint32_t) (std::sqrt(dx * dx + dy * dy) / speed * 1000.0) + MOVE_PAD_MS;
        mv0[k] = t;
        mv1[k] = t + mv;
        t += mv + MOVE_PAD_MS;
      }
    }
    t_say = t;
    t_exit = t_say + bubble_ms;
  }

  // After the word, walking off, it stops `dist` px along the way out and takes one more bite at p (the mouth
  // there, on the item). Call after plan().
  void plan_extra(Pt p, double dist, Rng &rng) {
    pt[n] = p;
    r[n] = BITE_R_MIN + (int) (rng.next() * BITE_R_SPAN);
    ex_walk = (uint32_t) (dist / speed * 1000.0);
    tb[n] = t_exit + ex_walk + OPEN_MS + 100;
    ex_pause = OPEN_MS + 100 + CHEW_MS + 250;
    extra = true;
    nb = n + 1;
  }

  // Px walked on the way out at t (the stop at the afterthought bite takes no distance), and whether it is stood.
  double exit_walked(uint32_t t) const {
    if (t <= t_exit) return 0;
    const uint32_t u = t - t_exit;
    const uint32_t ms = !extra || u <= ex_walk ? u : (u < ex_walk + ex_pause ? ex_walk : u - ex_pause);
    return ms * speed / 1000.0;
  }
  bool exit_standing(uint32_t t) const { return extra && t > t_exit + ex_walk && t < t_exit + ex_walk + ex_pause; }

  // Where the mouth is at t, and whether it is travelling.
  struct Anchor { double x, y; bool moving; };
  Anchor anchor(uint32_t t) const {
    for (int k = 0; k + 1 < n; k++)
      if (t >= mv0[k] && t < mv1[k]) {
        const double u = smooth((double) (t - mv0[k]) / (double) (mv1[k] - mv0[k]));
        return {pt[k].x + (pt[k + 1].x - pt[k].x) * u, pt[k].y + (pt[k + 1].y - pt[k].y) * u, true};
      }
    int k = 0;
    while (k + 1 < n && t >= mv1[k]) k++;
    return {(double) pt[k].x, (double) pt[k].y, false};
  }

  // 0..1: the head going into a bite (fast), then back (slower).
  double lunge(uint32_t t) const {
    double e = 0;
    for (int k = 0; k < nb; k++) {
      if (t + 130 < tb[k]) continue;
      const uint32_t u = t + 130 - tb[k];
      if (u < 130) e = std::fmax(e, smooth(u / 130.0));
      else if (u < 430) e = std::fmax(e, 1 - smooth((u - 130) / 300.0));
    }
    return e;
  }

  int bites_done(uint32_t t) const {
    int c = 0;
    for (int k = 0; k < nb; k++) c += t >= tb[k];
    return c;
  }

  enum Mouth { SHUT, HALF, OPEN };
  // The beak: open going in to a bite, working while it chews, shut at rest.
  Mouth mouth(uint32_t t) const {
    for (int k = 0; k < nb; k++) {
      if (t + OPEN_MS >= tb[k] && t < tb[k]) return OPEN;
      if (t >= tb[k] && t < tb[k] + CHEW_MS) return (t / 140) % 2 ? HALF : SHUT;
    }
    return SHUT;
  }
};

}  // namespace critter
}  // namespace f1
