#pragma once
// A baseline JPEG from memory, decoded at half size into RGB565 and reported a
// band at a time, so a picture can appear top-down (the detail card's portrait,
// copied from sky-tracker's UI-59f approach in sky_jpg.h).
//
// TJpgDec hands over MCU blocks (8 or 16 px, even-sized for even image sizes), so
// every 2x2 square of source pixels lies inside one block and is averaged there:
// no work buffer. The device uses the ESP32-S3 ROM's TJpgDec (R0.01, RGB888 out);
// the host tests build LVGL's copy of ChaN's later release (JD_FORMAT 0).
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifdef F1_HOST_TEST
extern "C" {
#include "tjpgd.h"
}
namespace f1jpg_rom {
using ::JDEC;
using ::JRECT;
using ::JRESULT;
using ::jd_decomp;
using ::jd_prepare;
typedef size_t in_len_t;
typedef int out_ret_t;
}  // namespace f1jpg_rom
#else
namespace f1jpg_rom {   // the ROM header's short type names (BYTE, WORD, ...) stay in here
#include "rom/tjpgd.h"
typedef UINT in_len_t;
typedef UINT out_ret_t;
}  // namespace f1jpg_rom
#endif

namespace f1 {
namespace jpg {

// `rows` is told how many output rows are finished, top down, at the end of each
// row of blocks. `cancel`, when it turns true, ends the decode at the next read.
struct Progress {
  void (*rows)(int done, void *ctx) = nullptr;
  void *ctx = nullptr;
  const volatile bool *cancel = nullptr;
};

struct Src {
  const uint8_t *p;
  size_t len, pos;
  uint16_t *dst;
  int tw, th, offx, offy;
  const Progress *pr;
};

inline f1jpg_rom::in_len_t in(f1jpg_rom::JDEC *jd, uint8_t *buf, f1jpg_rom::in_len_t n) {
  Src *s = (Src *) jd->device;
  if (s->pr && s->pr->cancel && *s->pr->cancel) return 0;
  const size_t k = n < s->len - s->pos ? (size_t) n : s->len - s->pos;
  if (buf) std::memcpy(buf, s->p + s->pos, k);
  s->pos += k;
  return (f1jpg_rom::in_len_t) k;
}

inline f1jpg_rom::out_ret_t out(f1jpg_rom::JDEC *jd, void *bitmap, f1jpg_rom::JRECT *r) {
  Src *s = (Src *) jd->device;
  const uint8_t *px = (const uint8_t *) bitmap;      // RGB888, the rectangle row by row
  const int w = r->right - r->left + 1, stride = 3 * w;
  for (int y = r->top; y + 1 <= r->bottom; y += 2) {
    const int ty = y / 2 - s->offy;
    if (ty < 0 || ty >= s->th) continue;
    uint16_t *o = s->dst + (size_t) ty * s->tw;
    const uint8_t *a = px + (size_t) (y - r->top) * stride;
    for (int x = r->left; x + 1 <= r->right; x += 2, a += 6) {
      const int tx = x / 2 - s->offx;
      if (tx < 0 || tx >= s->tw) continue;
      const uint8_t *b = a + stride;
      const unsigned R = (a[0] + a[3] + b[0] + b[3] + 2) >> 2,
                     G = (a[1] + a[4] + b[1] + b[4] + 2) >> 2,
                     B = (a[2] + a[5] + b[2] + b[5] + 2) >> 2;
      o[tx] = (uint16_t) ((R >> 3) << 11 | (G >> 2) << 5 | (B >> 3));
    }
  }
  if (s->pr && s->pr->rows && r->right + 1 >= (int) jd->width)    // the end of a row of blocks
    s->pr->rows(std::max(0, std::min(s->th, (r->bottom + 1) / 2 - s->offy)), s->pr->ctx);
  if (s->pr && s->pr->cancel && *s->pr->cancel) return 0;
  return 1;
}

// nullptr on success, else a short reason. The picture must be at least 2*tw x 2*th;
// a larger one is centred and cropped. The caller owns `dst` (tw*th pixels).
inline const char *decode_half(const uint8_t *jpg, size_t len, uint16_t *dst, int tw, int th,
                               const Progress *pr = nullptr) {
  uint8_t pool[4096] __attribute__((aligned(4)));   // TJpgDec's work area (~3.1 KB), on the caller's stack: two decodes can overlap
  Src s{jpg, len, 0, dst, tw, th, 0, 0, pr};
  f1jpg_rom::JDEC jd;
  std::memset(&jd, 0, sizeof(jd));
  f1jpg_rom::JRESULT r = f1jpg_rom::jd_prepare(&jd, in, pool, sizeof(pool), &s);
  if (r != 0) return r == 8 || r == 7 ? "unsupported JPEG (progressive?)" : "not a readable JPEG";
  const int hw = (int) (jd.width + 1) / 2, hh = (int) (jd.height + 1) / 2;
  if (hw < tw || hh < th) return "picture smaller than expected";
  s.offx = (hw - tw) / 2;
  s.offy = (hh - th) / 2;
  r = f1jpg_rom::jd_decomp(&jd, out, 0);
  if (pr && pr->cancel && *pr->cancel) return "cancelled";
  if (r == 0 && pr && pr->rows) pr->rows(th, pr->ctx);
  return r == 0 ? nullptr : "corrupt JPEG data";
}


// The picture 1:1 (the carousel card's 240x320 portrait). Same contract as decode_half.
inline f1jpg_rom::out_ret_t out_full(f1jpg_rom::JDEC *jd, void *bitmap, f1jpg_rom::JRECT *r) {
  Src *s = (Src *) jd->device;
  const uint8_t *px = (const uint8_t *) bitmap;
  for (int y = r->top; y <= r->bottom; y++) {
    const int ty = y - s->offy;
    for (int x = r->left; x <= r->right; x++, px += 3) {
      const int tx = x - s->offx;
      if (ty < 0 || ty >= s->th || tx < 0 || tx >= s->tw) continue;
      s->dst[(size_t) ty * s->tw + tx] =
          (uint16_t) ((px[0] >> 3) << 11 | (px[1] >> 2) << 5 | (px[2] >> 3));
    }
  }
  if (s->pr && s->pr->rows && r->right + 1 >= (int) jd->width)
    s->pr->rows(std::max(0, std::min(s->th, r->bottom + 1 - s->offy)), s->pr->ctx);
  if (s->pr && s->pr->cancel && *s->pr->cancel) return 0;
  return 1;
}

inline const char *decode_full(const uint8_t *jpg, size_t len, uint16_t *dst, int tw, int th,
                               const Progress *pr = nullptr) {
  uint8_t pool[4096] __attribute__((aligned(4)));
  Src s{jpg, len, 0, dst, tw, th, 0, 0, pr};
  f1jpg_rom::JDEC jd;
  std::memset(&jd, 0, sizeof(jd));
  f1jpg_rom::JRESULT r = f1jpg_rom::jd_prepare(&jd, in, pool, sizeof(pool), &s);
  if (r != 0) return r == 8 || r == 7 ? "unsupported JPEG (progressive?)" : "not a readable JPEG";
  if ((int) jd.width < tw || (int) jd.height < th) return "picture smaller than expected";
  s.offx = ((int) jd.width - tw) / 2;
  s.offy = ((int) jd.height - th) / 2;
  r = f1jpg_rom::jd_decomp(&jd, out_full, 0);
  if (pr && pr->cancel && *pr->cancel) return "cancelled";
  if (r == 0 && pr && pr->rows) pr->rows(th, pr->ctx);
  return r == 0 ? nullptr : "corrupt JPEG data";
}

}  // namespace jpg
}  // namespace f1
