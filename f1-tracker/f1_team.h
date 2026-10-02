#pragma once
// Favourite team (§8.2) and teammate head-to-head (§8.2).
//
// Free of LVGL so both can be tested on the host. The head-to-head is the
// comparison people actually argue about, and it needs no data the device does
// not already hold.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "f1_store.h"

namespace f1 {
namespace team {

inline constexpr int MAX_TEAMS = 12;

struct Pair {
  char team[20] = {0};
  int a = -1, b = -1;        // indices into the entry list
  uint32_t colour = 0x888888;
};

// The two drivers of each team, from whatever order list is current. A team
// with one entry (a reserve weekend) yields a pair with b == -1, which the
// caller renders as a single driver rather than an empty comparison.
inline int pairs(const store::Entry *e, int n, Pair *out, int cap) {
  int k = 0;
  for (int i = 0; i < n; i++) {
    if (!e[i].team[0]) continue;
    int at = -1;
    for (int j = 0; j < k; j++)
      if (std::strcmp(out[j].team, e[i].team) == 0) { at = j; break; }
    if (at < 0) {
      if (k >= cap) continue;
      at = k++;
      out[at] = Pair();
      std::snprintf(out[at].team, sizeof(out[at].team), "%s", e[i].team);
      out[at].colour = e[i].colour;
    }
    if (out[at].a < 0) out[at].a = i;
    else if (out[at].b < 0) out[at].b = i;
  }
  return k;
}

// Which side of a teammate pair finished ahead. Returns -1 when the comparison
// cannot be made - a retirement is NOT a loss on merit, so a pair where one car
// did not finish is reported as no result rather than a win for the other.
inline int ahead(const store::Entry *e, const Pair &p, bool count_retirements) {
  if (p.a < 0 || p.b < 0) return -1;
  const store::Entry &A = e[p.a], &B = e[p.b];
  if (!count_retirements && (A.out || B.out)) return -1;
  if (A.pos <= 0 || B.pos <= 0) return -1;
  return A.pos < B.pos ? p.a : p.b;
}

// One line: "VERSTAPPEN beat HADJAR". Returns false when there is nothing to
// say, which is the honest answer for a one-car team or a double retirement.
inline bool h2h_line(const store::Entry *e, const Pair &p, char *out, size_t n) {
  const int w = ahead(e, p, false);
  if (w < 0) {
    if (p.a >= 0 && p.b >= 0 && (e[p.a].out || e[p.b].out)) {
      std::snprintf(out, n, "%s · no result", p.team);
      return true;
    }
    return false;
  }
  const int l = (w == p.a) ? p.b : p.a;
  std::snprintf(out, n, "%s %s · P%d to P%d", e[w].name, "ahead of",
                e[w].pos, e[l].pos);
  return true;
}

// §8.2 favourite team: which rows to tint, and the constructors' position.
struct Favourite {
  char name[20] = {0};
  bool set() const { return name[0] != '\0'; }
};

inline bool is_favourite(const Favourite &f, const store::Entry &e) {
  return f.set() && e.team[0] && std::strcmp(f.name, e.team) == 0;
}

inline int constructor_pos(const store::Store &s, const Favourite &f) {
  if (!f.set()) return 0;
  for (int i = 0; i < s.n_constructors; i++)
    if (std::strcmp(s.constructors[i].name, f.name) == 0) return s.constructors[i].pos;
  return 0;
}

}  // namespace team
}  // namespace f1
