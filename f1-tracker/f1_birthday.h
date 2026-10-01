#pragma once
// Driver birthdays (section 5.6). On a driver's birthday their card is marked
// and shown more often.
//
// Free of LVGL so the date arithmetic and the rotation bias can be tested on
// the host - a feature that fires once a year is one you cannot debug by
// waiting for it.
#include <cstdint>
#include <cstring>

#include "f1_drivers.h"

namespace f1 {
namespace birthday {

// Scoped to CURRENT DRIVERS, deliberately. Legends carry a date of birth but no
// date of death, so the device cannot tell a living driver's birthday from the
// anniversary of someone long dead - and "HAPPY BIRTHDAY" over Ayrton Senna
// would be the single worst thing this device could display. Until a death
// date exists in the data, this stays with the people the feed says are racing.
inline constexpr bool LEGENDS_INCLUDED = false;

// Day-of-year comparison from an ISO "YYYY-MM-DD" and a UTC epoch. Month and
// day only - the year is what makes it a birthday rather than a date.
inline bool is_today(const char *dob, int month, int day) {
  if (dob == nullptr || std::strlen(dob) < 10) return false;
  const int m = (dob[5] - '0') * 10 + (dob[6] - '0');
  const int d = (dob[8] - '0') * 10 + (dob[9] - '0');
  if (m < 1 || m > 12 || d < 1 || d > 31) return false;
  // 29 February: on a non-leap year, mark it on the 28th rather than skipping a
  // driver's birthday three years in four.
  if (m == 2 && d == 29 && month == 2 && day == 28) return true;
  return m == month && d == day;
}

inline int age_today(const char *dob, int year, int month, int day) {
  if (dob == nullptr || std::strlen(dob) < 10) return 0;
  const int y = (dob[0] - '0') * 1000 + (dob[1] - '0') * 100 +
                (dob[2] - '0') * 10 + (dob[3] - '0');
  const int m = (dob[5] - '0') * 10 + (dob[6] - '0');
  const int d = (dob[8] - '0') * 10 + (dob[9] - '0');
  if (y < 1900 || year < y) return 0;
  int age = year - y;
  if (month < m || (month == m && day < d)) age--;
  return age;
}

// Index into drivers::P whose birthday is today, or -1. The first match wins;
// two drivers sharing a birthday is possible and the second simply waits for
// the rotation to reach them normally.
inline int today(int month, int day) {
  for (int i = 0; i < drivers::N; i++)
    if (is_today(drivers::P[i].dob, month, day)) return i;
  return -1;
}

inline int count_today(int month, int day) {
  int n = 0;
  for (int i = 0; i < drivers::N; i++)
    if (is_today(drivers::P[i].dob, month, day)) n++;
  return n;
}

// How often the birthday card is injected, as a count of cards.
//
// The first attempt used 4 and was far too frequent. The arithmetic, which is
// worth keeping because the intuition is bad:
//
//   The rotation is circuit -> driver -> circuit -> legend, so drivers are one
//   card in four. With 23 drivers, any ONE of them appears naturally every 92
//   cards - about once an hour at the 45 s default, 21 times a day.
//
//   every 4th : every  3 min, 480/day, 23.0x the natural rate   <- wallpaper
//   every 12th: every  9 min, 160/day,  7.7x
//   every 20th: every 15 min,  96/day,  4.6x                    <- chosen
//   every 46th: every 35 min,  42/day,  2.0x                    <- barely a bias
//
// 5.13's rule is "rare beats frequent": a thing that appears constantly becomes
// wallpaper within a week, and a birthday lasts one day. 4.6x is enough that
// someone glancing at the device through the day will meet it several times
// and notice, without it becoming what the device is showing.
//
// Note this is a CARD count, not a time: at the 15 s minimum interval it is one
// every 5 minutes, and at the 120 s maximum one every 40. That spread is
// acceptable - someone who sets a 15 s carousel has asked for more churn.
inline constexpr int EVERY = 20;

}  // namespace birthday
}  // namespace f1
