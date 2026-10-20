// Carl's visit, frame by frame, for the render tool (not a test): carl_frames out.bin fx fy fw fh seed fps px py
// A mirror of f1_critter.h's hungry visit (planning, the lunge, the marks and the blood as they land) using the
// firmware's own f1_critterbite.h, f1_wound.h and f1_dinologic.h. Writes, per frame, "x y pose bubble" to stdout and
// the 240x320 marks overlay (RGBA) to out.bin. The first lines are: frames, word index, and the bite points.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "../f1-tracker/f1_critterbite.h"
#include "../f1-tracker/f1_dinologic.h"
#include "../f1-tracker/f1_wound.h"

using namespace f1;
using critter::Pt;

int main(int argc, char **argv) {
  if (argc < 10) return 2;
  const int fx = std::atoi(argv[2]), fy = std::atoi(argv[3]), fw = std::atoi(argv[4]), fh = std::atoi(argv[5]);
  const uint32_t seed = (uint32_t) std::atoi(argv[6]);
  const int fps = std::atoi(argv[7]), PX = std::atoi(argv[8]), PY = std::atoi(argv[9]);
  const int W = 240, H = 320, SPR_W = 144, SPR_H = 120, MOUTH_DY = SPR_H * 34 / 100, LUNGE_X = 14, LUNGE_Y = 6, STAND = 4, HALF = 5, OPEN = 6;
  critter::Food box;
  box.x = PX; box.y = PY; box.w = W; box.h = H;
  box.fx = PX + fx; box.fy = PY + fy; box.fw = fw; box.fh = fh;
  critter::Rng rng(seed ^ 0x51ED5u);
  const int n = 2 + (int) ((seed >> 5) % 2);
  Pt pts[critter::BITES_MAX];
  critter::face_points(box, n, rng, pts);
  const int mdx = -2;
  dino::Walk w = dino::make_walk(seed);
  w.flip = true;
  w.lane = pts[0].y - MOUTH_DY;
  w.stop_x = 480 - SPR_W - (pts[0].x - mdx);
  w.t_stop = w.time_for(w.stop_x + dino::SW);
  w.t_end = w.t_stop + w.pause_ms + (w.time_for(480 + dino::SW) - w.t_stop);
  critter::Hunger hg;
  hg.plan(pts, n, w.t_stop, 70.0, 1500, rng);
  const Pt last = pts[n - 1];
  const uint32_t t_end = hg.t_exit + (uint32_t) ((last.x + 40 + SPR_W) / 70.0 * 1000.0) + 300;
  std::printf("%u %d\n", (unsigned) (t_end * fps / 1000 + 1), 0);
  for (int k = 0; k < n; k++) std::printf("%d %d\n", pts[k].x, pts[k].y);

  std::vector<uint32_t> ov(W * H, 0);
  wound::Drip drips[critter::BITES_MAX][wound::MAX_DRIPS];
  int nd[critter::BITES_MAX] = {0}, dlen[critter::BITES_MAX][wound::MAX_DRIPS] = {{0}};
  bool bead[critter::BITES_MAX][wound::MAX_DRIPS] = {{false}};
  int painted = 0;
  FILE *out = std::fopen(argv[1], "wb");
  auto put = [&](int x, int y, uint32_t c) {
    x -= PX; y -= PY;
    if (c && x >= 0 && x < W && y >= 0 && y < H) ov[y * W + x] = wound::over(ov[y * W + x], c);
  };
  for (int f = 0; f * 1000u / fps <= t_end; f++) {
    const uint32_t t = f * 1000u / fps;
    // the frame, as hungry_frame
    int x, y, pose;
    bool bubble = false;
    if (t < hg.t0) {
      const auto a = dino::at(w, t);
      x = a.x; y = a.y; pose = (int) a.pose;
    } else if (t < hg.t_exit) {
      const auto a = hg.anchor(t);
      const double e = hg.lunge(t);
      x = (int) std::lround(a.x - mdx - LUNGE_X * e);
      y = (int) std::lround(a.y - MOUTH_DY + LUNGE_Y * e);
      const auto m = hg.mouth(t);
      pose = a.moving ? (int) (t / 90) % 4 : (m == critter::Hunger::OPEN ? OPEN : (m == critter::Hunger::HALF ? HALF : STAND));
      bubble = !a.moving && t >= hg.t_say;
    } else {
      const double d = (t - hg.t_exit) * 70.0 / 1000.0;
      x = (int) std::lround(last.x - mdx - d);
      y = last.y - MOUTH_DY;
      pose = (int) (d / dino::STEP_PX) % 4;
    }
    // the marks, as they land
    const int k = hg.bites_done(t);
    for (; painted < k; painted++) {
      wound::Wound wd;
      wd.x = pts[painted].x; wd.y = pts[painted].y; wd.r = hg.r[painted] - 3; wd.left = true; wd.seed = seed * 31u + painted;
      const int ext = wound::extent(wd);
      for (int yy = wd.y - ext; yy <= wd.y + ext; yy++)
        for (int xx = wd.x - 2 * ext; xx <= wd.x + 2 * ext; xx++) put(xx, yy, wound::wound_argb(wd, xx, yy));
      critter::Rng rd(wd.seed ^ 0xB10Du);
      nd[painted] = wound::plan_drips(wd, rd, drips[painted]);
    }
    for (int i = 0; i < k; i++)
      for (int j = 0; j < nd[i]; j++) {
        const wound::Drip &d = drips[i][j];
        const int now = wound::drip_len(d, t - hg.tb[i]);
        const bool b = now >= d.len && !bead[i][j];
        if (now <= dlen[i][j] && !b) continue;
        for (int yy = d.y + dlen[i][j] - 1; yy <= d.y + now + 4; yy++)
          for (int xx = d.x - 6; xx <= d.x + 6; xx++) put(xx, yy, wound::drip_argb(d, now, now >= d.len, xx, yy));
        dlen[i][j] = now;
        if (now >= d.len) bead[i][j] = true;
      }
    std::printf("%d %d %d %d\n", x, y, pose, bubble ? 1 : 0);
    for (uint32_t c : ov) {                                    // RGBA bytes
      const uint8_t px[4] = {(uint8_t) (c >> 16), (uint8_t) (c >> 8), (uint8_t) c, (uint8_t) (c >> 24)};
      std::fwrite(px, 1, 4, out);
    }
  }
  std::fclose(out);
  return 0;
}
