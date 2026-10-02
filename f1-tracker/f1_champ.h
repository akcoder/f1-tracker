#pragma once
// Championship permutations (section 8.1).
//
// "Max clinches if he finishes ahead of Norris" is the most interesting
// sentence this device can print, and it is pure arithmetic over the standings
// it already fetches - no extra request, no live window.
//
// Free of LVGL so every edge can be tested on the host, which matters because
// the interesting cases happen once a year and the embarrassing ones are
// off-by-one.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "f1_store.h"

namespace f1 {
namespace champ {

// Points for a Grand Prix finish, P1..P10, plus the fastest-lap point. The
// fastest-lap point is only awarded inside the top ten, which is why it is
// separate rather than folded into first place.
inline constexpr int GP_POINTS[] = {25, 18, 15, 12, 10, 8, 6, 4, 2, 1};
inline constexpr int SPRINT_POINTS[] = {8, 7, 6, 5, 4, 3, 2, 1};
inline constexpr int FASTEST_LAP = 1;

inline int max_from_round() { return GP_POINTS[0] + FASTEST_LAP; }
inline int max_from_sprint() { return SPRINT_POINTS[0]; }

// The most anyone can still score: every remaining Grand Prix won with the
// fastest lap, plus every remaining sprint won.
inline int points_available(int rounds_left, int sprints_left) {
  if (rounds_left < 0) rounds_left = 0;
  if (sprints_left < 0) sprints_left = 0;
  return rounds_left * max_from_round() + sprints_left * max_from_sprint();
}

enum Verdict : uint8_t {
  OPEN,            // more than one driver can still take it
  CLINCHED,        // mathematically decided
  CAN_CLINCH,      // the leader can seal it this weekend
};

struct Outcome {
  Verdict verdict = OPEN;
  int leader_pos = 0;
  int gap = 0;                 // leader's points over the nearest rival
  int available = 0;
  int contenders = 0;          // how many can still mathematically win
  char leader[20] = {0};
  char rival[20] = {0};
};

// `standings` must be sorted by position, which Jolpica returns it as.
inline Outcome evaluate(const store::Standing *st, int n, int rounds_left,
                        int sprints_left) {
  Outcome o;
  if (st == nullptr || n < 1) return o;
  o.available = points_available(rounds_left, sprints_left);
  o.leader_pos = st[0].pos;
  std::snprintf(o.leader, sizeof(o.leader), "%s", st[0].name);
  if (n > 1) std::snprintf(o.rival, sizeof(o.rival), "%s", st[1].name);
  o.gap = (n > 1) ? st[0].points - st[1].points : st[0].points;

  // Anyone whose maximum still reaches the leader's current total is alive.
  // A tie on points is NOT decided - it falls to countback - so the comparison
  // must be >=, not >.
  o.contenders = 1;
  for (int i = 1; i < n; i++)
    if (st[i].points + o.available >= st[0].points) o.contenders++;

  if (o.contenders == 1) {
    o.verdict = CLINCHED;
  } else if (rounds_left > 0 &&
             o.gap > points_available(rounds_left - 1, sprints_left)) {
    // After this weekend nobody else can reach them, whatever happens in it.
    o.verdict = CAN_CLINCH;
  }
  return o;
}

// One line for the screen. Returns false when there is nothing worth saying,
// which is most of the season - a permutation line in April is noise.
inline bool line(const Outcome &o, char *out, size_t n) {
  switch (o.verdict) {
    case CLINCHED:
      std::snprintf(out, n, "%s has won the championship", o.leader);
      return true;
    case CAN_CLINCH:
      std::snprintf(out, n, "%s can clinch it this weekend", o.leader);
      return true;
    case OPEN:
    default:
      // Only once it is genuinely a two-horse race, and only once the gap
      // matters - otherwise this is a sentence about arithmetic, not a season.
      if (o.contenders == 2 && o.available > 0 && o.gap > 0 &&
          o.gap <= o.available) {
        std::snprintf(out, n, "%s leads %s by %d, %d still available",
                      o.leader, o.rival, o.gap, o.available);
        return true;
      }
      return false;
  }
}

}  // namespace champ
}  // namespace f1
