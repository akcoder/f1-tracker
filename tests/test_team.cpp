// Favourite team and teammate head-to-head (section 8.2).
#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "../f1-tracker/f1_team.h"

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
using namespace f1::team;

static store::Entry mk(int pos, const char *name, const char *tm, bool out = false) {
  store::Entry e;
  e.pos = pos; e.out = out;
  std::snprintf(e.name, sizeof(e.name), "%s", name);
  std::snprintf(e.team, sizeof(e.team), "%s", tm);
  return e;
}

int main() {
  std::printf("\nF1 Tracker - team tests\n\n");

  store::Entry e[] = {
      mk(1, "VERSTAPPEN", "Red Bull"), mk(2, "NORRIS", "McLaren"),
      mk(3, "HADJAR", "Red Bull"),     mk(4, "PIASTRI", "McLaren"),
      mk(5, "RUSSELL", "Mercedes"),    mk(6, "SOLO", "Reserve"),
  };
  const int n = (int) (sizeof(e) / sizeof(e[0]));

  std::printf("pairing\n");
  Pair p[MAX_TEAMS];
  const int k = pairs(e, n, p, MAX_TEAMS);
  okf(k == 4, "four distinct teams, got %d", k);
  for (int i = 0; i < k; i++) {
    if (std::strcmp(p[i].team, "Red Bull") == 0)
      okf(p[i].a == 0 && p[i].b == 2, "Red Bull pairs rows 0 and 2");
    if (std::strcmp(p[i].team, "Mercedes") == 0)
      okf(p[i].a == 4 && p[i].b < 0, "a one-car team has no second entry");
  }

  std::printf("head to head\n");
  {
    Pair rb; std::strcpy(rb.team, "Red Bull"); rb.a = 0; rb.b = 2;
    okf(ahead(e, rb, false) == 0, "P1 beats P3");
    char b[64];
    okf(h2h_line(e, rb, b, sizeof(b)) && std::strstr(b, "VERSTAPPEN"), "line: '%s'", b);
    okf(std::strstr(b, "P1") && std::strstr(b, "P3"), "both positions appear: '%s'", b);
  }
  {
    // A retirement is NOT a loss on merit. The comparison is withheld rather
    // than handed to the car that happened to keep running.
    store::Entry r[] = {mk(1, "A", "T"), mk(18, "B", "T", true)};
    Pair t; std::strcpy(t.team, "T"); t.a = 0; t.b = 1;
    okf(ahead(r, t, false) == -1, "a retirement yields no result");
    okf(ahead(r, t, true) == 0, "unless retirements are counted deliberately");
    char b[64];
    okf(h2h_line(r, t, b, sizeof(b)) && std::strstr(b, "no result"),
        "and it SAYS so rather than going quiet: '%s'", b);
  }
  {
    Pair one; std::strcpy(one.team, "Mercedes"); one.a = 4; one.b = -1;
    okf(ahead(e, one, false) == -1, "a one-car team has no head-to-head");
    char b[64];
    okf(!h2h_line(e, one, b, sizeof(b)), "and no line");
  }
  {
    // No position yet - the entry list before qualifying.
    store::Entry q[] = {mk(0, "A", "T"), mk(0, "B", "T")};
    Pair t; std::strcpy(t.team, "T"); t.a = 0; t.b = 1;
    okf(ahead(q, t, false) == -1, "no positions means no comparison");
  }

  std::printf("favourite team\n");
  {
    Favourite f;
    okf(!f.set(), "unset by default");
    okf(!is_favourite(f, e[0]), "an unset favourite matches nothing");
    std::snprintf(f.name, sizeof(f.name), "McLaren");
    okf(f.set(), "set once named");
    okf(is_favourite(f, e[1]) && is_favourite(f, e[3]), "both McLaren rows match");
    okf(!is_favourite(f, e[0]), "Red Bull does not");

    store::Store s;
    s.n_constructors = 2;
    std::snprintf(s.constructors[0].name, sizeof(s.constructors[0].name), "Red Bull");
    s.constructors[0].pos = 1;
    std::snprintf(s.constructors[1].name, sizeof(s.constructors[1].name), "McLaren");
    s.constructors[1].pos = 2;
    okf(constructor_pos(s, f) == 2, "constructors' position found");
    Favourite none;
    okf(constructor_pos(s, none) == 0, "no favourite yields no position");
    Favourite ghost;
    std::snprintf(ghost.name, sizeof(ghost.name), "Brabham");
    okf(constructor_pos(s, ghost) == 0, "a team not in the table yields 0");
  }

  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
