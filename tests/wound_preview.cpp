// Draws Carl's bites over a portrait, to look at (not a test): wound_preview in.rgb out.ppm fx fy fw fh seed [n]
// in.rgb is 240x320 raw RGB. The face box and the seed pick the bites as the firmware does; the bite points
// are printed, one a line.
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "../f1-tracker/f1_critterbite.h"
#include "../f1-tracker/f1_wound.h"

using namespace f1;

int main(int argc, char **argv) {
  if (argc < 8) return 2;
  const int W = 240, H = 320;
  std::vector<uint8_t> img(W * H * 3);
  FILE *f = std::fopen(argv[1], "rb");
  if (!f || std::fread(img.data(), 1, img.size(), f) != img.size()) return 3;
  std::fclose(f);
  critter::Food fd;
  fd.fx = std::atoi(argv[3]); fd.fy = std::atoi(argv[4]); fd.fw = std::atoi(argv[5]); fd.fh = std::atoi(argv[6]);
  const uint32_t seed = (uint32_t) std::atoi(argv[7]);
  const int n = argc > 8 ? std::atoi(argv[8]) : 2 + (int) (critter::Rng(seed).next() * 2);
  critter::Rng rng(seed ^ 0x51ED5u);
  critter::Pt pts[critter::BITES_MAX];
  critter::face_points(fd, n, rng, pts);
  for (int k = 0; k < n; k++) std::printf("%d %d\n", pts[k].x, pts[k].y);     // where the mouth goes, for the render tool
  std::vector<uint32_t> ov(W * H, 0);
  for (int k = 0; k < n; k++) {
    wound::Wound w;
    w.x = pts[k].x; w.y = pts[k].y; w.r = critter::BITE_R_MIN + (int) (rng.next() * critter::BITE_R_SPAN) - 3;
    w.left = true; w.seed = seed * 31 + k;
    const int ext = wound::extent(w);
    for (int y = w.y - ext; y <= w.y + ext; y++)
      for (int x = w.x - ext * 2; x <= w.x + ext; x++)
        if (x >= 0 && x < W && y >= 0 && y < H) ov[y * W + x] = wound::over(ov[y * W + x], wound::wound_argb(w, x, y));
    wound::Drip dr[wound::MAX_DRIPS];
    const int nd = wound::plan_drips(w, rng, dr);
    for (int i = 0; i < nd; i++)
      for (int y = dr[i].y - 1; y <= dr[i].y + dr[i].len + 4; y++)
        for (int x = dr[i].x - 6; x <= dr[i].x + 6; x++)
          if (x >= 0 && x < W && y >= 0 && y < H) ov[y * W + x] = wound::over(ov[y * W + x], wound::drip_argb(dr[i], dr[i].len, true, x, y));
  }
  for (int i = 0; i < W * H; i++) {
    const uint32_t c = ov[i], a = c >> 24;
    for (int ch = 0; ch < 3; ch++) {
      const uint32_t sc = (c >> (16 - 8 * ch)) & 255;
      img[i * 3 + ch] = (uint8_t) ((sc * a + img[i * 3 + ch] * (255 - a)) / 255);
    }
  }
  f = std::fopen(argv[2], "wb");
  std::fprintf(f, "P6\n%d %d\n255\n", W, H);
  std::fwrite(img.data(), 1, img.size(), f);
  std::fclose(f);
  return 0;
}
