// Driver birthdays (section 5.6) and the rotation bias they create.
// A feature that fires once a year is one you cannot debug by waiting for it.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <set>

#include "../f1-tracker/f1_birthday.h"
#include "../f1-tracker/f1_carousel.h"

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

int main() {
  std::printf("\nF1 Tracker - birthday tests\n\n");

  std::printf("date matching\n");
  okf(birthday::is_today("1997-09-30", 9, 30), "exact match");
  okf(!birthday::is_today("1997-09-30", 9, 29), "day before");
  okf(!birthday::is_today("1997-09-30", 10, 30), "same day, wrong month");
  okf(!birthday::is_today("", 9, 30), "empty dob");
  okf(!birthday::is_today(nullptr, 9, 30), "null dob");
  okf(!birthday::is_today("bad", 9, 30), "short dob");
  okf(!birthday::is_today("1997-13-45", 13, 45), "impossible date is rejected");
  // 29 February: marked on the 28th in a non-leap year rather than skipped
  // three years in four.
  okf(birthday::is_today("1996-02-29", 2, 29), "leap-day birthday on a leap day");
  okf(birthday::is_today("1996-02-29", 2, 28), "leap-day birthday falls back to the 28th");
  okf(!birthday::is_today("1996-02-28", 2, 29), "a 28 Feb birthday does NOT also fire on the 29th");

  std::printf("age\n");
  okf(birthday::age_today("1997-09-30", 2026, 9, 30) == 29, "age on the day");
  okf(birthday::age_today("1997-09-30", 2026, 9, 29) == 28, "the day before, still 28");
  okf(birthday::age_today("1997-09-30", 2026, 10, 1) == 29, "the day after");
  okf(birthday::age_today("1997-09-30", 1990, 1, 1) == 0, "before birth yields 0");
  okf(birthday::age_today(nullptr, 2026, 1, 1) == 0, "null dob yields 0");

  std::printf("against the real driver table\n");
  okf(drivers::N > 0, "drivers are compiled in");
  int with_dob = 0, hits = 0;
  (void) hits;
  for (int i = 0; i < drivers::N; i++)
    if (drivers::P[i].dob[0]) with_dob++;
  okf(with_dob == drivers::N, "every driver has a date of birth (%d of %d)",
      with_dob, drivers::N);
  // Walk a whole year: every driver must be found exactly once, and no day may
  // ever report more drivers than exist.
  std::set<int> found;
  static const int DAYS[] = {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  for (int m = 1; m <= 12; m++)
    for (int d = 1; d <= DAYS[m - 1]; d++) {
      const int n = birthday::count_today(m, d);
      hits += n;
      okf(n <= drivers::N, "%02d-%02d reports %d drivers", m, d, n);
      const int i = birthday::today(m, d);
      if (i >= 0) found.insert(i);
    }
  okf((int) found.size() == drivers::N,
      "every driver's birthday is reachable: found %zu of %d", found.size(), drivers::N);
  okf(hits >= drivers::N, "the year covers at least every driver once (%d hits)", hits);
  okf(birthday::today(0, 0) < 0, "an impossible date finds nobody");

  std::printf("rotation bias\n");
  using namespace carousel;
  {
    Rotation r;
    r.configure(40, drivers::N, 32, ALL);
    r.set_priority(DRIVER, 3, birthday::EVERY);
    okf(r.has_priority(), "priority set");
    Card c;
    int pri = 0, total = 2000;
    std::set<int> circuits_seen, drivers_seen;
    for (int i = 0; i < total; i++) {
      r.next(c);
      if (c.type == DRIVER && c.index == 3) pri++;
      if (c.type == CIRCUIT) circuits_seen.insert(c.index);
      if (c.type == DRIVER) drivers_seen.insert(c.index);
    }
    // Every 4th card from the priority slot, PLUS however often the ordinary
    // walk happens to reach that same driver - the priority slot does not
    // consume a cursor, so both paths can land on them. The floor is what
    // matters; the ceiling just guards against the card taking over.
    const int expect = total / birthday::EVERY;
    okf(pri >= expect,
        "birthday card appeared %d times in %d, expected at least %d from the "
        "priority slot alone", pri, total, expect);
    okf(pri <= expect + total / drivers::N + 4,
        "birthday card appeared %d times in %d - too often, it is taking over",
        pri, total);
    // The rate must stay well under the "wallpaper" threshold 5.13 warns about.
    // One card in eight would be visible on almost every glance.
    okf(pri < total / 8,
        "birthday card is %d of %d cards - too frequent to stay special",
        pri, total);
    // and nothing is starved: the rotation resumes where it was.
    okf((int) circuits_seen.size() == 40, "circuits still fully reached (%zu/40)",
        circuits_seen.size());
    okf((int) drivers_seen.size() == drivers::N,
        "other drivers still reached (%zu/%d)", drivers_seen.size(), drivers::N);
  }
  {
    // A birthday driver must NOT override a content filter that excludes them.
    Rotation r;
    r.configure(40, drivers::N, 32, CIRCUITS_ONLY);
    r.set_priority(DRIVER, 3, birthday::EVERY);
    Card c;
    for (int i = 0; i < 60; i++) {
      r.next(c);
      okf(c.type == CIRCUIT, "a birthday leaked past 'circuits only'");
    }
  }
  {
    Rotation r;
    r.configure(40, drivers::N, 32, ALL);
    r.set_priority(DRIVER, 3, birthday::EVERY);
    r.clear_priority();
    okf(!r.has_priority(), "priority cleared");
    Card c;
    int pri = 0;
    for (int i = 0; i < 400; i++) { r.next(c); if (c.type == DRIVER && c.index == 3) pri++; }
    okf(pri < 10, "after clearing, the card is ordinary again (%d in 400)", pri);
  }
  {
    // An out-of-range priority index must be ignored, not crash or stall.
    Rotation r;
    r.configure(40, drivers::N, 32, ALL);
    r.set_priority(DRIVER, 9999, birthday::EVERY);
    Card c;
    for (int i = 0; i < 50; i++) okf(r.next(c), "rotation stalled on a bad priority index");
  }

  std::printf("scope\n");
  okf(!birthday::LEGENDS_INCLUDED,
      "legends are excluded - no death date exists, so the device cannot tell a "
      "birthday from the anniversary of someone long dead");

  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
