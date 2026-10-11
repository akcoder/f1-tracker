// Auto-dim built on the Sun's elevation (UI-44; the elevation itself is tested in tracker-common). Everything here is a fact about
// the sky that can be checked by hand, so a wrong sign or a missing equation of
// time shows up as a number rather than as a screen that is slightly too dim.
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../f1-tracker/f1_display.h"

static int checks = 0, failures = 0;
static void okf(bool c, const char *fmt, ...) {
  checks++;
  if (!c) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

using namespace f1;

// days since the epoch for a civil date (Howard Hinnant)
static double day0(int y, int m, int d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned) (y - era * 400);
  const unsigned doy = (unsigned) ((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return ((double) era * 146097 + (double) doe - 719468) * 86400.0;
}

static void test_auto_dim() {
  std::printf("auto-dim from latitude and longitude (UI-44)\n");
  const double LAT = 61.581, LON = -149.439;

  // Midsummer: the Sun never gets below ~-5 deg, so the panel never reaches the
  // 25 % floor - it only dips to ~42 %. That is what a latitude-aware curve is
  // for; a clock schedule would have dimmed it all night.
  double lo = 2.0;
  const double t0 = day0(2026, 6, 21);
  for (int mi = 0; mi < 1440; mi += 5) {
    const float f = display::auto_factor(t0 + mi * 60.0, LAT, LON, true);
    if (f < lo) lo = f;
  }
  okf(lo > 0.40 && lo < 0.45, "midsummer dips only to ~42 %%, got %.3f", lo);

  double lo2 = 2.0, hi2 = 0.0;
  const double w0 = day0(2026, 12, 21);
  for (int mi = 0; mi < 1440; mi += 5) {
    const float f = display::auto_factor(w0 + mi * 60.0, LAT, LON, true);
    if (f < lo2) lo2 = f;
    if (f > hi2) hi2 = f;
  }
  okf(lo2 == 0.25f, "midwinter reaches the 25 %% floor, got %.3f", lo2);
  okf(hi2 > 0.99, "and the winter noon (~5.2 deg, past the +5 deg knee) just reaches full, got %.3f", hi2);

  okf(display::auto_factor(t0, LAT, LON, false) == 1.0f, "an unsynced clock never dims");

  // The position is an INPUT: move the device and the answer follows, at the
  // same instant. 12:00 UTC on the June solstice is 03:00 in Alaska (sun just
  // under the horizon) and 22:00 in Melbourne (dark).
  const double t = t0 + 12 * 3600.0;
  const float ak = display::auto_factor(t, 61.581, -149.439, true);
  const float mel = display::auto_factor(t, -37.8, 145.0, true);
  okf(mel == 0.25f, "Melbourne at 22:00 is dark, got %.3f", mel);
  okf(ak > mel + 0.1f, "the same instant in Alaska is brighter (%.3f vs %.3f)", ak, mel);
}

int main() {
  std::printf("\nF1 Tracker - auto-dim tests\n\n");
  test_auto_dim();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
