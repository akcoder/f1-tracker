// The hungry visit's plan (f1_critterbite.h): how many bites, where, when, and where the mouth is.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "../f1-tracker/f1_critterbite.h"
#include "../f1-tracker/f1_wound.h"
using namespace f1::critter;
namespace wound = f1::wound;
static int checks = 0, failures = 0;
#define OK(c, m) do { checks++; if (!(c)) { failures++; std::printf("  FAIL  %s\n", m); } } while (0)
int main() {
  std::printf("\nF1 Tracker - hungry visit tests\n\n");
  bool counts = true, seen[BITES_MAX + 1] = {false};
  for (uint32_t s = 1; s <= 500; s++) {
    const int n = bite_count(s * 2654435761u);
    counts &= n >= BITES_MIN && n <= BITES_MAX;
    if (n >= 0 && n <= BITES_MAX) seen[n] = true;
  }
  OK(counts, "3 to 5 bites, always");
  OK(seen[3] && seen[4] && seen[5], "and all three counts come up");

  // a portrait: bites down the right edge, inside it, in no fixed order
  const Food portrait{24, 56, 240, 320, false};
  bool edge_ok = true, shuffled = false;
  for (uint32_t s = 1; s <= 300; s++) {
    Rng r(s * 7919u);
    Pt p[BITES_MAX];
    const int n = edge_points(portrait, 5, r, p);
    for (int i = 0; i < n; i++)
      edge_ok &= p[i].x > 24 + 240 - 11 && p[i].x <= 24 + 240 && p[i].y >= 56 && p[i].y <= 56 + 320;
    for (int i = 1; i < n; i++) shuffled |= p[i].y < p[i - 1].y;
  }
  OK(edge_ok, "the edge bites sit on the portrait's right edge, inside it");
  OK(shuffled, "the bird hops about: not always top to bottom");

  // a trace: a long line; the walk follows it, 34-52 px a step, in the box, right of min_x
  const Food box{90, 70, 300, 240, true};
  auto is_line = [](int x, int y) { return y == 200 && x >= 100 && x <= 380; };     // a straight run
  Rng rr(5);
  Pt w[BITES_MAX];
  const int got = trace_walk(is_line, box, 146, Pt{150, 200}, 5, rr, w);
  OK(got == 5, "five points along a long line");
  bool steps = true, inside = true;
  for (int i = 1; i < got; i++) {
    const double d = std::hypot(w[i].x - w[i - 1].x, w[i].y - w[i - 1].y);
    steps &= d >= 34 && d <= 52;
  }
  for (int i = 0; i < got; i++) inside &= is_line(w[i].x, w[i].y) && w[i].x >= 146;
  OK(steps, "each step is 34-52 px");
  OK(inside, "every point is on the line, right of min_x");
  Rng r2(5);
  OK(trace_walk([](int, int) { return false; }, box, 0, Pt{150, 200}, 5, r2, w) == 1, "no line to follow: just the start");

  // the timeline
  Hunger h;
  Rng r3(11);
  const Pt pts[4] = {{200, 300}, {190, 260}, {200, 220}, {240, 215}};
  h.plan(pts, 4, 3000, 70.0, 1500, r3);
  bool order = h.tb[0] == 3500;
  for (int k = 1; k < h.n; k++) order &= h.tb[k] > h.tb[k - 1] + CHEW_MS && h.mv0[k - 1] > h.tb[k - 1] && h.mv1[k - 1] > h.mv0[k - 1] && h.tb[k] >= h.mv1[k - 1];
  OK(order, "each bite follows the last one's chew and the move after it");
  OK(h.t_say > h.tb[3] && h.t_exit == h.t_say + 1500, "then the word, then away");
  bool radii = true;
  for (int k = 0; k < h.n; k++) radii &= h.r[k] >= BITE_R_MIN && h.r[k] < BITE_R_MIN + BITE_R_SPAN;
  OK(radii, "bite sizes vary within their range");
  Hunger::Anchor a = h.anchor(h.tb[0]);
  OK(!a.moving && a.x == 200 && a.y == 300, "at the first bite at its time");
  a = h.anchor((h.mv0[0] + h.mv1[0]) / 2);
  OK(a.moving && a.x < 200 && a.x > 190 && a.y < 300 && a.y > 260, "midway through a move: between the two");
  a = h.anchor(h.tb[3]);
  OK(!a.moving && a.x == 240 && a.y == 215, "at the last bite at its time");
  bool cont = true;
  Hunger::Anchor p = h.anchor(h.t0);
  for (uint32_t t = h.t0 + 10; t < h.t_exit; t += 10) {
    const Hunger::Anchor c = h.anchor(t);
    cont &= std::fabs(c.x - p.x) <= 3 && std::fabs(c.y - p.y) <= 3;                   // 70 px/s, eased: at most ~1.5 px per 10 ms
    p = c;
  }
  OK(cont, "the mouth never jumps");
  OK(h.lunge(h.tb[0] - 130) == 0 && h.lunge(h.tb[0]) > 0.99 && h.lunge(h.tb[0] + 130) > 0.2 && h.lunge(h.tb[0] + 130) < 0.8 && h.lunge(h.tb[0] + 600) == 0, "the head goes in at the bite and is back after");
  OK(h.lunge(h.mv0[0]) == 0, "no lunge while moving");
  OK(h.bites_done(h.tb[0] - 1) == 0 && h.bites_done(h.tb[0]) == 1 && h.bites_done(h.t_end + 99999) == 4, "bites counted as they land");
  OK(h.mouth(h.tb[0] - 100) == Hunger::OPEN && h.mouth(h.tb[0] - 300) == Hunger::SHUT, "the beak opens going in");
  OK(h.mouth(h.tb[0] + 150) != Hunger::OPEN && h.mouth(h.t_say + 10) == Hunger::SHUT, "chews, then rests");
  bool chews = false;
  for (uint32_t t = h.tb[0]; t < h.tb[0] + CHEW_MS; t += 20) chews |= h.mouth(t) == Hunger::HALF;
  OK(chews, "the beak works while it chews");
  // the bite is the shape of the mouth: a bill is long and thin, a jaw short and wide, neither round
  const int R = 20;
  int bill_w = 0, bill_l = 0, jaw_w = 0, jaw_l = 0, bill_n = 0, jaw_n = 0;
  for (int dy = -60; dy <= 60; dy++)
    for (int dx = -90; dx <= 90; dx++) {
      if (bite_in(BILL, false, dx, dy, R)) { bill_n++; if (std::abs(dy) > bill_w) bill_w = std::abs(dy); if (dx > bill_l) bill_l = dx; }
      if (bite_in(JAW, false, dx, dy, R)) { jaw_n++; if (std::abs(dy) > jaw_w) jaw_w = std::abs(dy); if (dx > jaw_l) jaw_l = dx; }
    }
  OK(bill_l > 2 * bill_w, "the bill's bite is long and thin");
  OK(jaw_w > jaw_l / 2 && jaw_l < bill_l, "the jaw's bite is shorter and wider than the bill's");
  const double round_area = 3.14159 * R * R;
  OK(std::fabs(bill_n - round_area) > 0.25 * round_area && std::fabs(jaw_n - round_area) > 0.05 * round_area, "neither is a circle of the same size");
  OK(bite_in(BILL, true, -20, 0, R) && !bite_in(BILL, true, 20, 0, R), "facing left, the bite goes left of the mouth");
  OK(bite_in(JAW, false, 10, 0, R) && !bite_in(JAW, false, -20, 0, R), "facing right, it goes right");
  bool teeth = false;
  for (int dy = -16; dy <= 16 && !teeth; dy++) teeth |= bite_in(JAW, false, 20, dy, R) != bite_in(JAW, false, 20, 0, R);
  OK(teeth, "the jaw's inner edge is toothed");
  // the afterthought: stops partway along the way out, one more bite, then goes on
  Hunger e;
  Rng r4(5);
  e.plan(pts, 4, 3000, 70.0, 1500, r4);
  const uint32_t plain_end = e.t_exit;
  e.plan_extra(Pt{120, 215}, 70.0, r4);
  OK(e.nb == e.n + 1 && e.pt[e.n].x == 120 && e.pt[e.n].y == 215, "the extra bite is at the coordinates given");
  OK(e.tb[e.n] > plain_end + e.ex_walk && e.bites_done(e.tb[e.n] - 1) == e.n && e.bites_done(e.tb[e.n]) == e.n + 1, "it lands after the walk to the spot");
  OK(e.exit_walked(e.t_exit + e.ex_walk) == e.exit_walked(e.t_exit + e.ex_walk + e.ex_pause) && e.exit_standing(e.tb[e.n]), "it stands still for it");
  OK(std::fabs(e.exit_walked(e.t_exit + e.ex_walk) - 70.0) < 1.0, "having walked as far as the spot");
  OK(e.exit_walked(e.t_exit + e.ex_walk + e.ex_pause + 1000) > 70.0 && !e.exit_standing(e.t_exit + e.ex_walk + e.ex_pause + 5), "then walks on");
  OK(e.mouth(e.tb[e.n] - 100) == Hunger::OPEN && e.lunge(e.tb[e.n] - 130 + 130) > 0.9, "beak opens and head lunges into it");
  // Carl: the bites go in the face, from its right, two or three of them
  Food fd;
  fd.fx = 100; fd.fy = 80; fd.fw = 90; fd.fh = 110;
  bool inface = true, spread = true;
  for (uint32_t sd = 1; sd < 60; sd++) {
    Rng rf(sd);
    Pt fp[BITES_MAX];
    const int nf = 2 + (int) (sd % 2);
    face_points(fd, nf, rf, fp);
    for (int i = 0; i < nf; i++) {
      inface &= fp[i].x >= fd.fx + fd.fw * 0.44 && fp[i].x <= fd.fx + fd.fw * 0.84 && fp[i].y >= fd.fy + fd.fh * 0.2 && fp[i].y <= fd.fy + fd.fh * 0.9;
      for (int j = 0; j < i; j++) spread &= std::abs(fp[i].y - fp[j].y) > 8;
    }
  }
  OK(inface, "every face bite is inside the face box, on its right");
  OK(spread, "and no two are at the same height");

  // the wound
  wound::Wound wd;
  wd.x = 150; wd.y = 120; wd.r = 20; wd.left = true; wd.seed = 5;
  const wound::Shape sh = wound::shape(wd);
  const uint32_t mid = wound::wound_argb(wd, (int) sh.cx, (int) sh.cy);
  OK((mid >> 24) == 255 && ((mid >> 16) & 255) > ((mid >> 8) & 255) && ((mid >> 16) & 255) > (mid & 255), "the middle of a wound is opaque and red");
  OK(wound::wound_argb(wd, 150 + 3 * wound::extent(wd), 120) == 0 && wound::wound_argb(wd, 150, 120 - 3 * wound::extent(wd)) == 0, "nothing far from it");
  OK(sh.cx < wd.x, "facing left, it is on the left of the mouth");
  int opaque = 0, soft = 0;
  const int ex = wound::extent(wd);
  for (int y = wd.y - ex; y <= wd.y + ex; y++)
    for (int x = wd.x - 2 * ex; x <= wd.x + ex; x++) {
      const uint32_t c = wound::wound_argb(wd, x, y);
      opaque += (c >> 24) == 255;
      soft += c && (c >> 24) < 255;
    }
  OK(opaque > 400 && soft > 150, "an opaque wound inside a translucent bruise");
  OK(wound::wound_argb(wd, 120, 120) == wound::wound_argb(wd, 120, 120), "the same pixel every time");
  Rng rd(3);
  wound::Drip dr[wound::MAX_DRIPS];
  const int nd = wound::plan_drips(wd, rd, dr);
  OK(nd >= 2 && nd <= 4, "two to four drips");
  bool drips_ok = true, wide = false;
  for (int i = 0; i < nd; i++) {
    drips_ok &= dr[i].len >= 14 && dr[i].len < 72 && dr[i].y > wd.y && dr[i].x > sh.cx - sh.a && dr[i].x < sh.cx + sh.a;
    drips_ok &= wound::drip_len(dr[i], 0) == 0 && wound::drip_len(dr[i], wound::DRIP_MS) == dr[i].len && wound::drip_len(dr[i], wound::DRIP_MS / 2) > dr[i].len / 2;
    wide |= dr[i].w == 3;
  }
  OK(drips_ok && wide, "each drip leaves the lower lip, grows to its length (fast, then slow), and the longest is thickest");
  const uint32_t top = wound::drip_argb(dr[0], dr[0].len, true, dr[0].x, dr[0].y + 3);
  const uint32_t beyond = wound::drip_argb(dr[0], dr[0].len / 2, false, dr[0].x, dr[0].y + dr[0].len - 2);
  OK((top >> 24) == 255 && ((top >> 16) & 255) > 100 && beyond == 0, "a drip is red where it has run, and nothing further down");
  OK(wound::over(0, 0x80FF0000u) == 0x80FF0000u && wound::over(0xFF102030u, 0xFF405060u) == 0xFF405060u, "over: onto nothing, and opaque on opaque");
  const uint32_t half = wound::over(0xFF000000u, 0x80FF0000u);
  OK((half >> 24) == 255 && ((half >> 16) & 255) > 120 && ((half >> 16) & 255) < 136, "over: half-red on black is half red");
  std::printf("\n%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
