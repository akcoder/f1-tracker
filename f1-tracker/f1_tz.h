#pragma once
// The clock's time zone, chosen at runtime (Settings > Location, the "Time Zone" select in HA and the
// web page). Ported from sky-tracker's sky_tz.h (UI-70). "Auto" takes Home Assistant's zone, which it
// sends with each time sync (HA 2026.3+); any other choice is applied over it, and because HA re-sends
// its zone every time it syncs the clock, a 500 ms timer puts the chosen one back. The zones are
// pre-parsed POSIX rules (ESPHome's ParsedTimezone), so nothing is parsed on the device. The build's
// own `timezone:` is only the zone before the first sync; the select's default (Alaska) matches it.
#include <cstdint>
#include <cstring>

#if defined(F1_HOST_TEST)
namespace f1 {
namespace tz {
struct DSTRule {
  int32_t time_seconds;
  uint16_t day;
  uint8_t type, month, week, day_of_week;
};
struct ParsedTimezone {
  int32_t std_offset_seconds, dst_offset_seconds;
  DSTRule dst_start, dst_end;
};
inline ParsedTimezone host_global{};
inline void set_global(const ParsedTimezone &z) { host_global = z; }
inline const ParsedTimezone &get_global() { return host_global; }
constexpr uint8_t MWD = 1, NONE = 0;
#elif defined(USE_TIME_TIMEZONE)
#include "esphome/components/time/posix_tz.h"
namespace f1 {
namespace tz {
using esphome::time::DSTRule;
using esphome::time::ParsedTimezone;
inline void set_global(const ParsedTimezone &z) { esphome::time::set_global_tz(z); }
inline const ParsedTimezone &get_global() { return esphome::time::get_global_tz(); }
constexpr esphome::time::DSTRuleType MWD = esphome::time::DSTRuleType::MONTH_WEEK_DAY;
constexpr esphome::time::DSTRuleType NONE = esphome::time::DSTRuleType::NONE;
#else
#define F1_TZ_OFF       // a build without time zones: the picker does nothing
namespace f1 {
namespace tz {
#endif

#ifdef F1_TZ_OFF
inline void init() {}
inline void set(int) {}
#else
// Mm.w.d/time; offsets are POSIX (seconds WEST of UTC)
constexpr DSTRule R(int month, int week, int dow, int hour) {
  return DSTRule{hour * 3600, 0, MWD, (uint8_t) month, (uint8_t) week, (uint8_t) dow};
}
constexpr DSTRule NO = DSTRule{0, 0, NONE, 0, 0, 0};
constexpr int H = 3600;

// The select's options, in this order (index 0 is Auto). tools/check_tz.py checks each against the
// IANA zone named in its comment.
constexpr int N_ZONES = 20;
inline const ParsedTimezone ZONES[N_ZONES] = {
    {0, 0, NO, NO},                                                       // Auto (unused)
    {10 * H, 10 * H, NO, NO},                                             // Hawaii         Pacific/Honolulu
    {9 * H, 8 * H, R(3, 2, 0, 2), R(11, 1, 0, 2)},                        // Alaska         America/Anchorage
    {8 * H, 7 * H, R(3, 2, 0, 2), R(11, 1, 0, 2)},                        // Pacific        America/Los_Angeles
    {7 * H, 6 * H, R(3, 2, 0, 2), R(11, 1, 0, 2)},                        // Mountain       America/Denver
    {7 * H, 7 * H, NO, NO},                                               // Arizona        America/Phoenix
    {6 * H, 5 * H, R(3, 2, 0, 2), R(11, 1, 0, 2)},                        // Central        America/Chicago
    {5 * H, 4 * H, R(3, 2, 0, 2), R(11, 1, 0, 2)},                        // Eastern        America/New_York
    {4 * H, 3 * H, R(3, 2, 0, 2), R(11, 1, 0, 2)},                        // Atlantic       America/Halifax
    {3 * H + 1800, 2 * H + 1800, R(3, 2, 0, 2), R(11, 1, 0, 2)},          // Newfoundland   America/St_Johns
    {0, 0, NO, NO},                                                       // UTC            UTC
    {0, -1 * H, R(3, 5, 0, 1), R(10, 5, 0, 2)},                           // London         Europe/London
    {-1 * H, -2 * H, R(3, 5, 0, 2), R(10, 5, 0, 3)},                      // Central Europe Europe/Paris
    {-2 * H, -3 * H, R(3, 5, 0, 3), R(10, 5, 0, 4)},                      // Eastern Europe Europe/Athens
    {-3 * H, -3 * H, NO, NO},                                             // Moscow         Europe/Moscow
    {-5 * H - 1800, -5 * H - 1800, NO, NO},                               // India          Asia/Kolkata
    {-8 * H, -8 * H, NO, NO},                                             // China          Asia/Shanghai
    {-9 * H, -9 * H, NO, NO},                                             // Japan          Asia/Tokyo
    {-10 * H, -11 * H, R(10, 1, 0, 2), R(4, 1, 0, 3)},                    // Sydney         Australia/Sydney
    {-12 * H, -13 * H, R(9, 5, 0, 2), R(4, 1, 0, 3)},                     // Auckland       Pacific/Auckland
};

inline bool same(const ParsedTimezone &a, const ParsedTimezone &b) { return std::memcmp(&a, &b, sizeof(a)) == 0; }

struct St {
  int index = 0;
  ParsedTimezone chosen{};
  ParsedTimezone ha{};  // the zone from Home Assistant (or the build) while a choice overrides it
  bool have_ha = false;
};
inline St st;

// every 500 ms: an HA time sync has put its zone back; re-apply the chosen one
inline void tick() {
  if (st.index == 0) return;
  const ParsedTimezone &cur = get_global();
  if (!same(cur, st.chosen)) {
    st.ha = cur;
    st.have_ha = true;
    set_global(st.chosen);
  }
}

// the select's index: 0 = Auto (Home Assistant's zone)
inline void set(int index) {
  if (index < 0 || index >= N_ZONES) index = 0;
  if (index == 0) {
    if (st.index != 0 && st.have_ha) set_global(st.ha);   // back to HA's zone (its next sync sends it again anyway)
    st.index = 0;
    return;
  }
  if (st.index == 0) {                                    // keep the zone in force, for Auto later
    st.ha = get_global();
    st.have_ha = true;
  }
  st.index = index;
  st.chosen = ZONES[index];
  set_global(st.chosen);
}

#ifndef F1_HOST_TEST
inline void init() {
  lv_timer_create([](lv_timer_t *) { tick(); }, 500, nullptr);
}
#endif
#endif  // F1_TZ_OFF

}  // namespace tz
}  // namespace f1
