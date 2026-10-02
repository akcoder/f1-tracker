// Championship permutations (section 8.1). The interesting cases happen once a
// year and the embarrassing ones are off-by-one, so they are pinned here.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../f1-tracker/f1_champ.h"

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
using namespace f1::champ;

static store::Standing mk(int pos, int pts, const char *name) {
  store::Standing s;
  s.pos = pos; s.points = pts;
  std::snprintf(s.name, sizeof(s.name), "%s", name);
  return s;
}

int main() {
  std::printf("\nF1 Tracker - championship tests\n\n");

  std::printf("points available\n");
  okf(max_from_round() == 26, "a win plus the fastest lap is 26");
  okf(points_available(0, 0) == 0, "nothing left at the end of a season");
  okf(points_available(3, 0) == 78, "three rounds is 78");
  okf(points_available(3, 1) == 86, "three rounds and a sprint is 86");
  okf(points_available(-1, -1) == 0, "negative rounds cannot award points");

  std::printf("clinched\n");
  {
    std::vector<store::Standing> s{mk(1, 400, "VERSTAPPEN"), mk(2, 300, "NORRIS"),
                                   mk(3, 250, "LECLERC")};
    // 3 rounds left = 78 available; 300 + 78 = 378 < 400.
    Outcome o = evaluate(s.data(), (int) s.size(), 3, 0);
    okf(o.verdict == CLINCHED, "100 ahead with 78 available is decided");
    okf(o.contenders == 1, "one contender, got %d", o.contenders);
    char b[80];
    okf(line(o, b, sizeof(b)) && std::strstr(b, "has won"), "line: '%s'", b);
  }

  std::printf("a tie is NOT decided\n");
  {
    // 300 + 78 = 378, leader on 378: equal on points falls to countback, so the
    // title is still open. This is the off-by-one the >= guards.
    std::vector<store::Standing> s{mk(1, 378, "A"), mk(2, 300, "B")};
    Outcome o = evaluate(s.data(), (int) s.size(), 3, 0);
    okf(o.verdict != CLINCHED, "exactly reachable must not read as clinched");
    okf(o.contenders == 2, "two contenders when the totals can level");
    std::vector<store::Standing> t{mk(1, 379, "A"), mk(2, 300, "B")};
    okf(evaluate(t.data(), 2, 3, 0).verdict == CLINCHED, "one point more IS decided");
  }

  std::printf("can clinch this weekend\n");
  {
    // 2 rounds left (52 available). After this weekend 1 round remains = 26.
    // A 30-point lead cannot be overturned by the last round alone.
    std::vector<store::Standing> s{mk(1, 330, "A"), mk(2, 300, "B")};
    Outcome o = evaluate(s.data(), 2, 2, 0);
    okf(o.verdict == CAN_CLINCH, "30 ahead with one round after this is sealable");
    char b[80];
    okf(line(o, b, sizeof(b)) && std::strstr(b, "can clinch"), "line: '%s'", b);
    // 20 ahead is not enough - the last round is worth 26.
    std::vector<store::Standing> t{mk(1, 320, "A"), mk(2, 300, "B")};
    okf(evaluate(t.data(), 2, 2, 0).verdict == OPEN, "20 ahead is not sealable yet");
  }

  std::printf("open, and quiet when it should be\n");
  {
    // Mid-season with a big field: no line, because a permutation sentence in
    // April is noise rather than news.
    std::vector<store::Standing> s{mk(1, 200, "A"), mk(2, 180, "B"), mk(3, 170, "C"),
                                   mk(4, 160, "D")};
    Outcome o = evaluate(s.data(), (int) s.size(), 10, 3);
    okf(o.verdict == OPEN, "mid-season is open");
    okf(o.contenders == 4, "four contenders, got %d", o.contenders);
    char b[80];
    okf(!line(o, b, sizeof(b)), "no line while four drivers are alive");
  }
  {
    // A genuine two-horse race late on DOES get a line.
    std::vector<store::Standing> s{mk(1, 390, "A"), mk(2, 370, "B"), mk(3, 300, "C")};
    Outcome o = evaluate(s.data(), (int) s.size(), 2, 0);
    okf(o.contenders == 2, "two alive, got %d", o.contenders);
    char b[80];
    okf(line(o, b, sizeof(b)) && std::strstr(b, "leads"), "line: '%s'", b);
    okf(std::strstr(b, "20") != nullptr, "the gap should appear: '%s'", b);
  }

  std::printf("degenerate input\n");
  {
    char b[80];
    Outcome z = evaluate(nullptr, 0, 5, 0);
    okf(z.verdict == OPEN && z.contenders == 0, "no standings yields nothing");
    okf(!line(z, b, sizeof(b)), "and no line");
    std::vector<store::Standing> one{mk(1, 100, "A")};
    Outcome o = evaluate(one.data(), 1, 5, 0);
    okf(o.contenders == 1 && o.verdict == CLINCHED, "a field of one is decided");
  }

  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
