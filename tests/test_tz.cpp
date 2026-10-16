// The time zone picker (f1_tz.h, UI-70): a choice is applied, a Home Assistant sync that overwrites it
// is put back by the timer, and Auto gives HA's zone back.
#include <cstdio>
#define F1_HOST_TEST 1
#include "../f1-tracker/f1_tz.h"
using namespace f1::tz;
static int checks = 0, failures = 0;
#define OK(c, m) do { checks++; if (!(c)) { failures++; std::printf("  FAIL  %s\n", m); } } while (0)
int main() {
  std::printf("\nF1 Tracker - time zone tests\n\n");
  const ParsedTimezone ha = ZONES[7];                       // Eastern: what HA sends
  set_global(ha);
  set(2);                                                   // Alaska
  OK(same(get_global(), ZONES[2]) && get_global().std_offset_seconds == 9 * H, "a choice is applied");
  set_global(ha);                                           // an HA time sync puts its zone back
  tick();
  OK(same(get_global(), ZONES[2]), "the timer puts the chosen zone back");
  tick();
  OK(same(get_global(), ZONES[2]), "and leaves it alone when nothing changed");
  set(11);                                                  // London
  OK(get_global().dst_offset_seconds == -1 * H && get_global().dst_start.month == 3, "a second choice replaces the first");
  set(0);                                                   // Auto
  OK(same(get_global(), ha), "Auto gives Home Assistant's zone back");
  set_global(ZONES[3]);                                     // HA sends another
  tick();
  OK(same(get_global(), ZONES[3]), "with Auto the timer does not interfere");
  set(99);
  OK(same(get_global(), ZONES[3]), "an index out of range is Auto");
  bool distinct = true;
  for (int i = 1; i < N_ZONES; i++)
    for (int j = i + 1; j < N_ZONES; j++) distinct &= !same(ZONES[i], ZONES[j]) || (i == 10 && false);
  OK(distinct, "no two zones are the same rule");
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
