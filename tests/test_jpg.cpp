// tracker_jpg.h: the half-size progressive decoder, against all 48 compiled portraits.
// Host build of LVGL's TJpgDec (see the Makefile); the device uses the ROM's.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#define TRACKER_HOST_TEST 1
#include "tracker_jpg.h"
#include "../f1-tracker/f1_portraits.h"

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

constexpr int TW = 120, TH = 160;
constexpr uint16_t SENTINEL = 0x1234;

struct Trace {
  std::vector<int> rows;
  const uint16_t *buf;
  std::vector<std::vector<uint16_t>> snaps;   // the picture as each report arrived
};
static void on_rows(int done, void *ctx) {
  auto *t = (Trace *) ctx;
  t->rows.push_back(done);
  t->snaps.emplace_back(t->buf, t->buf + (size_t) TW * TH);
}

int main() {
  using namespace f1;
  using namespace tracker;
  std::vector<uint16_t> full((size_t) TW * TH);
  for (int i = 0; i < portraits::N; i++) {
    const auto &p = portraits::P[i];
    // A pixel nobody wrote keeps its sentinel; a real pixel can equal either one, but
    // not both, so decode over two different sentinels and compare.
    std::vector<uint16_t> other((size_t) TW * TH, 0xEDCB);
    std::fill(full.begin(), full.end(), SENTINEL);
    const char *err = jpg::decode_half(p.jpeg, p.len, full.data(), TW, TH);
    CHECK(err == nullptr, "%s: %s", p.driver_id, err ? err : "");
    if (err) continue;
    CHECK(jpg::decode_half(p.jpeg, p.len, other.data(), TW, TH) == nullptr, "%s: second decode", p.driver_id);
    int unset = 0, dark = 0;
    for (size_t k = 0; k < full.size(); k++) { unset += full[k] != other[k]; dark += full[k] == 0; }
    CHECK(unset == 0, "%s: %d pixels never written", p.driver_id, unset);
    CHECK(dark < TW * TH / 2, "%s: mostly black (%d)", p.driver_id, dark);
  }

  // Progressive: reports are non-decreasing, end at TH, and every row reported as
  // done is already final - the same as in the finished picture.
  const auto *v = portraits::find("max_verstappen");
  CHECK(v != nullptr, "max_verstappen has a portrait");
  std::vector<uint16_t> buf((size_t) TW * TH, SENTINEL), ref((size_t) TW * TH, SENTINEL);
  CHECK(jpg::decode_half(v->jpeg, v->len, ref.data(), TW, TH) == nullptr, "reference decode");
  Trace t{{}, buf.data(), {}};
  jpg::Progress pr; pr.rows = on_rows; pr.ctx = &t;
  CHECK(jpg::decode_half(v->jpeg, v->len, buf.data(), TW, TH, &pr) == nullptr, "progressive decode");
  CHECK(buf == ref, "progressive result equals the plain decode");
  CHECK(t.rows.size() >= 8, "several bands reported, got %zu", t.rows.size());
  CHECK(!t.rows.empty() && t.rows.back() == TH, "the last report is the whole picture");
  bool mono = true, final_prefix = true;
  for (size_t i = 0; i < t.rows.size(); i++) {
    if (i && t.rows[i] < t.rows[i - 1]) mono = false;
    for (int y = 0; y < t.rows[i] && y < TH; y++)
      for (int x = 0; x < TW; x++)
        if (t.snaps[i][(size_t) y * TW + x] != ref[(size_t) y * TW + x]) final_prefix = false;
    // and nothing below the reported rows has been written yet (top-down)
  }
  CHECK(mono, "reports never go backwards");
  CHECK(final_prefix, "every row reported done is already final");
  bool topdown = true;
  for (size_t i = 0; i + 1 < t.rows.size(); i++)
    if (t.rows[i] < TH && t.snaps[i][(size_t) (TH - 1) * TW + TW / 2] != SENTINEL) topdown = false;
  CHECK(topdown, "the bottom row is untouched until the end");

  // Cancel: stops early, reports it, and stops reporting.
  volatile bool cancel = false;
  struct C { volatile bool *c; int n; } cc{&cancel, 0};
  jpg::Progress p2;
  p2.ctx = &cc; p2.cancel = &cancel;
  p2.rows = [](int done, void *x) { auto *c = (C *) x; c->n++; if (done >= 40) *c->c = true; };
  std::vector<uint16_t> b2((size_t) TW * TH, SENTINEL);
  const char *e2 = jpg::decode_half(v->jpeg, v->len, b2.data(), TW, TH, &p2);
  CHECK(e2 != nullptr && std::strcmp(e2, "cancelled") == 0, "cancel gives 'cancelled', got %s", e2 ? e2 : "null");
  CHECK(cc.n < 20, "stopped soon after the cancel, %d reports", cc.n);

  // The 1:1 decode (the carousel card): every portrait fills 240x320, bands never go
  // backwards, the rows reported are final, and it cancels.
  {
    constexpr int FW = 240, FH = 320;
    int bad = 0;
    for (int i = 0; i < portraits::N; i++) {
      const auto &p = portraits::P[i];
      std::vector<uint16_t> a((size_t) FW * FH, SENTINEL), b((size_t) FW * FH, 0xEDCB);
      if (jpg::decode_full(p.jpeg, p.len, a.data(), FW, FH) != nullptr ||
          jpg::decode_full(p.jpeg, p.len, b.data(), FW, FH) != nullptr) { bad++; continue; }
      for (size_t k = 0; k < a.size(); k++) if (a[k] != b[k]) { bad++; break; }
    }
    CHECK(bad == 0, "full decode: %d of %d portraits failed or left pixels unwritten", bad, portraits::N);

    struct T { std::vector<int> rows; std::vector<std::vector<uint16_t>> snaps; const uint16_t *buf; } tt;
    std::vector<uint16_t> fb((size_t) FW * FH, SENTINEL), fr((size_t) FW * FH, SENTINEL);
    tt.buf = fb.data();
    CHECK(jpg::decode_full(v->jpeg, v->len, fr.data(), FW, FH) == nullptr, "full reference");
    jpg::Progress pf;
    pf.ctx = &tt;
    pf.rows = [](int done, void *c) { auto *t = (T *) c; t->rows.push_back(done);
                                       t->snaps.emplace_back(t->buf, t->buf + (size_t) FW * FH); };
    CHECK(jpg::decode_full(v->jpeg, v->len, fb.data(), FW, FH, &pf) == nullptr, "full progressive");
    CHECK(fb == fr, "full: progressive result equals the plain decode");
    CHECK(tt.rows.size() >= 10 && tt.rows.back() == FH, "full: bands reported, last is the whole picture");
    bool ok = true;
    for (size_t i = 0; i < tt.rows.size(); i++) {
      if (i && tt.rows[i] < tt.rows[i - 1]) ok = false;
      for (int y = 0; y < tt.rows[i] && y < FH && ok; y++)
        if (std::memcmp(&tt.snaps[i][(size_t) y * FW], &fr[(size_t) y * FW], FW * 2) != 0) ok = false;
    }
    CHECK(ok, "full: bands in order and final");
    volatile bool cn = false;
    struct C2 { volatile bool *c; } c2{&cn};
    jpg::Progress p3;
    p3.ctx = &c2; p3.cancel = &cn;
    p3.rows = [](int done, void *x) { if (done >= 64) *((C2 *) x)->c = true; };
    std::vector<uint16_t> fc((size_t) FW * FH, SENTINEL);
    const char *e3 = jpg::decode_full(v->jpeg, v->len, fc.data(), FW, FH, &p3);
    CHECK(e3 && std::strcmp(e3, "cancelled") == 0, "full: cancel");
    CHECK(fc[(size_t) (FH - 1) * FW + FW / 2] == SENTINEL, "full: nothing below the cancel point was written");
  }

  // Bad input.
  const uint8_t junk[16] = {1, 2, 3};
  CHECK(jpg::decode_half(junk, sizeof(junk), buf.data(), TW, TH) != nullptr, "junk is refused");
  CHECK(jpg::decode_half(v->jpeg, v->len, buf.data(), 200, 300) != nullptr, "a target larger than half the picture is refused");

  // decode_copy: 1:1 into part of a larger picture. With the same size it equals decode_full.
  {
    constexpr int CW = 240, CH = 320, STRIDE = 300;
    std::vector<uint16_t> full2((size_t) CW * CH, SENTINEL), big((size_t) STRIDE * CH, SENTINEL);
    CHECK(jpg::decode_full(v->jpeg, v->len, full2.data(), CW, CH) == nullptr, "copy: reference");
    CHECK(jpg::decode_copy(v->jpeg, v->len, big.data(), STRIDE, CW, CH) == nullptr, "copy: decodes");
    bool same = true, margin = true;
    for (int y = 0; y < CH; y++)
      for (int x = 0; x < STRIDE; x++) {
        const uint16_t g = big[(size_t) y * STRIDE + x];
        if (x < CW) same &= g == full2[(size_t) y * CW + x]; else margin &= g == SENTINEL;
      }
    CHECK(same, "copy: pixels equal decode_full");
    CHECK(margin, "copy: the stride's margin is untouched");
    bool stop = false;
    jpg::Progress pc; pc.cancel = &stop; stop = true;
    const char *ec = jpg::decode_copy(v->jpeg, v->len, big.data(), STRIDE, CW, CH, &pc);
    CHECK(ec && std::strcmp(ec, "cancelled") == 0, "copy: cancel");
  }

  // decode_fit: a box filter to any size. Its average colour stays that of the picture, the
  // progress is monotone and ends at the height, and a cancel is honoured.
  {
    constexpr int FW = 200, FH = 266;
    std::vector<uint16_t> fit((size_t) FW * FH, SENTINEL), acc((size_t) FW * FH * 3), ref2((size_t) 240 * 320);
    CHECK(jpg::decode_full(v->jpeg, v->len, ref2.data(), 240, 320) == nullptr, "fit: reference");
    auto rows_fit = [](int done, void *ctx) { ((std::vector<int> *) ctx)->push_back(done); };
    std::vector<int> seen;
    jpg::Progress pf; pf.rows = rows_fit; pf.ctx = &seen;
    int sw = 0, sh = 0;
    const char *ef = jpg::decode_fit(v->jpeg, v->len, fit.data(), FW, FH, acc.data(), &pf, &sw, &sh);
    CHECK(ef == nullptr, "fit: decodes (%s)", ef ? ef : "");
    CHECK(sw == 240 && sh == 320, "fit: reports the source size %dx%d", sw, sh);
    CHECK(!seen.empty() && seen.back() == FH, "fit: the last report is the whole picture");
    bool mono2 = true;
    for (size_t i = 1; i < seen.size(); i++) mono2 &= seen[i] >= seen[i - 1];
    CHECK(mono2, "fit: progress never goes back");
    auto mean = [](const std::vector<uint16_t> &b, double *r, double *g, double *bl) {
      double R = 0, G = 0, B = 0;
      for (uint16_t p : b) { R += (p >> 11) << 3; G += ((p >> 5) & 63) << 2; B += (p & 31) << 3; }
      *r = R / b.size(); *g = G / b.size(); *bl = B / b.size();
    };
    double r1, g1, b1, r2, g2, b2;
    mean(fit, &r1, &g1, &b1); mean(ref2, &r2, &g2, &b2);
    CHECK(std::abs(r1 - r2) < 6 && std::abs(g1 - g2) < 6 && std::abs(b1 - b2) < 6,
          "fit: average colour %.0f/%.0f/%.0f vs %.0f/%.0f/%.0f", r1, g1, b1, r2, g2, b2);
    bool stop = true;
    jpg::Progress pc2; pc2.cancel = &stop;
    const char *ec2 = jpg::decode_fit(v->jpeg, v->len, fit.data(), FW, FH, acc.data(), &pc2);
    CHECK(ec2 && std::strcmp(ec2, "cancelled") == 0, "fit: cancel");
  }

  // A caller-supplied work area gives the same pixels as the stack's.
  {
    alignas(4) static uint8_t wk[4096];
    std::vector<uint16_t> a((size_t) TW * TH, SENTINEL), b((size_t) 240 * 320, SENTINEL), c((size_t) 240 * 320, SENTINEL);
    CHECK(jpg::decode_half(v->jpeg, v->len, a.data(), TW, TH, nullptr, nullptr, nullptr, wk) == nullptr, "pool: half");
    CHECK(a == ref, "pool: half equals the stack decode");
    CHECK(jpg::decode_full(v->jpeg, v->len, b.data(), 240, 320, nullptr, nullptr, nullptr, wk) == nullptr, "pool: full");
    CHECK(jpg::decode_full(v->jpeg, v->len, c.data(), 240, 320) == nullptr && b == c, "pool: full equals the stack decode");
    std::vector<uint16_t> acc((size_t) 200 * 266 * 3), f1v((size_t) 200 * 266), f2v((size_t) 200 * 266);
    CHECK(jpg::decode_fit(v->jpeg, v->len, f1v.data(), 200, 266, acc.data(), nullptr, nullptr, nullptr, 1.0f, 0, false, wk) == nullptr, "pool: fit");
    CHECK(jpg::decode_fit(v->jpeg, v->len, f2v.data(), 200, 266, acc.data()) == nullptr && f1v == f2v, "pool: fit equals the stack decode");
    std::vector<uint16_t> g1((size_t) 240 * 320), g2((size_t) 240 * 320);
    CHECK(jpg::decode_copy(v->jpeg, v->len, g1.data(), 240, 240, 320, nullptr, wk) == nullptr, "pool: copy");
    CHECK(jpg::decode_copy(v->jpeg, v->len, g2.data(), 240, 240, 320) == nullptr && g1 == g2, "pool: copy equals the stack decode");
  }

  std::printf("%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
