// f1_png.h: the streaming PNG decoder, with palette and transparency, row by row.
#include <cstdio>
#include <vector>

#define F1_HOST_TEST 1
#include "../f1-tracker/f1_png.h"
#include "fixture_png.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static uint16_t rgb565(int r, int g, int b) { return (uint16_t) ((r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)); }

struct Rows { std::vector<int> ys; std::vector<std::vector<uint16_t>> snaps; const uint16_t *buf; int w; };
static void on_row(uint16_t *, int y, void *c) {
  auto *r = (Rows *) c;
  r->ys.push_back(y);
  r->snaps.emplace_back(r->buf, r->buf + (size_t) r->w * 64);
}

int main() {
  using f1::png::Decoder;
  const size_t N = sizeof(FIXTURE_PNG);
  const uint32_t BG = 0x102030;                   // composite over this
  std::vector<uint16_t> out(64 * 64, 0xABCD);

  // whole file at once, 1:1
  {
    Decoder d;
    CHECK(d.begin(out.data(), 64, 64, BG), "begin");
    CHECK(d.feed(FIXTURE_PNG, N) && d.done && !d.error, "decodes: %s", d.error ? d.error : "ok");
    CHECK(d.W == 64 && d.H == 64, "size %ux%u", d.W, d.H);
    CHECK(out[0] == rgb565(0x10, 0x20, 0x30), "a transparent pixel is the background (%04x)", out[0]);
    CHECK(out[32 * 64 + 32] == rgb565(200, 120, 60), "the opaque disc keeps its palette colour (%04x)", out[32 * 64 + 32]);
    // the ring is half transparent: halfway between 250 and the background (16,32,48)
    const uint16_t ring = out[32 * 64 + 10];
    const int rr = ((ring >> 11) & 31) << 3, gg = ((ring >> 5) & 63) << 2;
    CHECK(rr > 110 && rr < 150 && gg > 120 && gg < 150, "the ring is half way to the background (%d,%d)", rr, gg);
  }

  // streamed a byte at a time: the same picture, rows reported top-down and final
  {
    std::vector<uint16_t> one(64 * 64, 0), slow(64 * 64, 0xABCD);
    Decoder a; a.begin(one.data(), 64, 64, BG); a.feed(FIXTURE_PNG, N);
    Rows rows{{}, {}, slow.data(), 64};
    Decoder d; d.begin(slow.data(), 64, 64, BG); d.on_row = on_row; d.row_ctx = &rows;
    bool ok = true;
    for (size_t i = 0; i < N && ok; i++) ok = d.feed(FIXTURE_PNG + i, 1);
    CHECK(ok && d.done, "a byte at a time: %s", d.error ? d.error : "ok");
    CHECK(slow == one, "streaming gives the same picture as one go");
    CHECK(rows.ys.size() == 64, "a report for every row, got %zu", rows.ys.size());
    bool mono = true, final_rows = true;
    for (size_t i = 0; i < rows.ys.size(); i++) {
      if (rows.ys[i] != (int) i) mono = false;
      for (int x = 0; x < 64; x++) if (rows.snaps[i][(size_t) rows.ys[i] * 64 + x] != one[(size_t) rows.ys[i] * 64 + x]) final_rows = false;
    }
    CHECK(mono, "rows arrive in order, top first");
    CHECK(final_rows, "a row reported is already final");
  }

  // box filter to half size: a 2x2 block of one colour stays that colour; edges average
  {
    std::vector<uint16_t> half(32 * 32, 0);
    Decoder d; d.begin(half.data(), 32, 32, BG);
    CHECK(d.feed(FIXTURE_PNG, N) && d.done, "half-size decode");
    CHECK(half[16 * 32 + 16] == rgb565(200, 120, 60), "the disc centre survives scaling (%04x)", half[16 * 32 + 16]);
    CHECK(half[0] == rgb565(0x10, 0x20, 0x30), "the corner is still background");
  }

  // refusals
  {
    std::vector<uint16_t> o(64 * 64);
    Decoder d; d.begin(o.data(), 64, 64, BG);
    CHECK(!d.feed((const uint8_t *) "not a png at all", 16) && d.error, "garbage is refused");
    Decoder t; t.begin(o.data(), 64, 64, BG);
    t.feed(FIXTURE_PNG, N / 2);
    CHECK(!t.done, "half a file is not done");
    Decoder big; big.begin(o.data(), 128, 128, BG);                 // wants more than the picture has
    CHECK(!big.feed(FIXTURE_PNG, N) && big.error, "a target larger than the picture is refused");
  }

  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
