// Headshot URLs (f1_store.h): OpenF1's /drivers parsed, the URL moved to the size the device
// wants, and only the F1 media server trusted. Real session 11377 data.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define F1_HOST_TEST 1
#include "../f1-tracker/f1_headshot.h"
#include "../f1-tracker/f1_json.h"
#include "../f1-tracker/f1_store.h"

static int checks = 0, failures = 0;
#define OK(c, ...) do { checks++; if (!(c)) { failures++; std::printf("  FAIL  line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static std::string slurp(const char *rel) {
  std::string p = std::string("../reference/samples/") + rel;
  FILE *f = std::fopen(p.c_str(), "rb");
  if (!f) { std::printf("  FAIL  cannot open %s\n", p.c_str()); failures++; return {}; }
  std::string out; char buf[8192]; size_t r;
  while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, r);
  std::fclose(f);
  return out;
}

int main() {
  using namespace f1::store;
  char u[200], full[260];
  const std::string P = HEADSHOT_PREFIX;
  OK(headshot_path((P + "G/GABBOR01_Gabriel_Bortoleto/gabbor01.png.transform/1col/image.png").c_str(), u, sizeof(u)) &&
     !std::strcmp(u, "G/GABBOR01_Gabriel_Bortoleto/gabbor01.png.transform/2col/image.png"), "1col becomes 2col, prefix dropped: %s", u);
  OK(headshot_path((P + "x/y.png.transform/3col/image.png").c_str(), u, sizeof(u)) && std::strstr(u, "/2col/image.png"),
     "any width is set to 2col");
  Headshot h; std::snprintf(h.path, sizeof(h.path), "%s", "G/GABBOR01_Gabriel_Bortoleto/gabbor01.png.transform/2col/image.png");
  OK(headshot_full_url(h, full, sizeof(full)) && !std::strncmp(full, "https://media.formula1.com/", 27) && std::strstr(full, "/2col/image.png"),
     "the prefix goes back on: %s", full);
  OK(!headshot_full_url(h, full, 20), "a URL that does not fit is refused, not cut");
  OK(!headshot_path("http://media.formula1.com/d_driver_fallback_image.png/content/dam/fom-website/drivers/x.png.transform/1col/image.png", u, sizeof(u)), "plain http refused");
  OK(!headshot_path("https://evil.example.com/https://media.formula1.com/x.png.transform/1col/image.png", u, sizeof(u)), "another host refused");
  OK(!headshot_path("https://media.formula1.com.evil.example/d_driver_fallback_image.png/content/dam/fom-website/drivers/x.png.transform/1col/image.png", u, sizeof(u)), "a look-alike host refused");
  OK(!headshot_path("https://media.formula1.com/other/folder/x.png.transform/1col/image.png", u, sizeof(u)), "another folder on the server refused");
  OK(!headshot_path((P + "x/y.png").c_str(), u, sizeof(u)), "a URL with no size part refused");
  OK(!headshot_path((P + "../../secret.png.transform/1col/image.png").c_str(), u, sizeof(u)), "a path that climbs out refused");
  OK(!headshot_path("", u, sizeof(u)) && !headshot_path(nullptr, u, sizeof(u)), "empty and null refused");
  char small[20];
  OK(!headshot_path((P + "G/GABBOR01_Gabriel_Bortoleto/gabbor01.png.transform/1col/image.png").c_str(), small, sizeof(small)),
     "a path that does not fit the buffer is refused, not cut");

  const std::string js = slurp("openf1-drivers-11377.json");
  static Store st;
  OK(parse_headshots(js.data(), js.size(), st), "the real /drivers reply parses");
  OK(st.n_headshots == 22, "22 drivers, got %d", st.n_headshots);
  const Headshot *nor = find_headshot(st, "NOR");
  OK(nor && std::strstr(nor->path, "LANNOR01_Lando_Norris") && std::strstr(nor->path, "/2col/image.png"), "NOR found, at 2col: %s", nor ? nor->path : "-");
  OK(find_headshot(st, "ZZZ") == nullptr && find_headshot(st, "") == nullptr && find_headshot(st, nullptr) == nullptr, "an unknown code finds nothing");
  bool all = true;
  for (int i = 0; i < st.n_headshots; i++)
    if (!st.headshots[i].path[0] || !st.headshots[i].code[0] || std::strstr(st.headshots[i].path, "http")) all = false;
  OK(all, "every kept entry is a bare path under the media server's folder");
  OK(!parse_headshots("[]", 2, st), "an empty list is a failed parse (the old table is kept)");
  OK(st.n_headshots == 22, "and the table is untouched");
  const char bad[] = "[{\"name_acronym\":\"AAA\",\"headshot_url\":\"https://evil.example/a.png\"},{\"name_acronym\":\"BBB\"}]";
  Store s2;
  OK(!parse_headshots(bad, sizeof(bad) - 1, s2) && s2.n_headshots == 0, "no good URL, no headshots");

  // ---- the picture slots (f1_headshot.h)
  {
    using namespace f1::headshot;
    OK(want("", 0) == nullptr && want(nullptr, 0) == nullptr, "no code, no slot");
    OK(next_job() == nullptr && !list_wanted(0), "nothing waiting at first");
    Slot *a = want("AAA", 1000);
    OK(a && a->state.load() == LOADING && a->queued.load() && a->rows.load() == 0, "a new request is queued, loading");
    OK(want("AAA", 2000) == a, "asking again returns the same slot, not a second one");
    OK(next_job() == a && list_wanted(0) && !list_wanted(22), "the job is waiting; the URL list is wanted only while none is known");
    a->rows.store(120);
    finish(*a, true, 3000);
    OK(a->state.load() == READY && a->rows.load() == H && !a->queued.load() && next_job() == nullptr, "finished: ready, whole picture, not queued");
    Slot *b = want("BBB", 4000), *c = want("CCC", 5000), *d = want("DDD", 6000);
    OK(b && c && d && b != a && c != b && d != c, "four slots, four drivers");
    for (Slot *x : {b, c, d}) finish(*x, true, 7000);
    want("AAA", 8000);                                               // A is now the most recently used
    Slot *e = want("EEE", 9000);
    OK(e && e == b, "a fifth driver takes the least recently used slot (B)");
    OK(find("BBB") == nullptr && find("AAA") == a, "B is gone, A is kept");
    OK(e->rows.load() == 0 && e->state.load() == LOADING, "the reused slot starts empty");
    // a failure waits ten minutes
    finish(*e, false, 10000);
    OK(e->state.load() == FAILED && want("EEE", 10000 + 60 * 1000) == nullptr, "after a failure, no new try for ten minutes");
    Slot *e2 = want("EEE", 10000 + RETRY_MS + 1);
    OK(e2 == e && e2->state.load() == LOADING && e2->queued.load(), "then it is asked for again");
    // slots being fetched are not evicted
    finish(*e2, true, 20000);
    Slot *f = want("FFF", 21000), *g = want("GGG", 21000), *h = want("HHH", 21000), *i = want("III", 21000);
    OK(f && g && h && i, "four loading at once");
    OK(want("JJJ", 21000) == nullptr, "all four mid-fetch: a fifth waits (no picture, no crash)");
    for (Slot *x : {f, g, h, i}) finish(*x, true, 22000);
  }

  std::printf("%d checks, %d failures\n", checks, failures);
  return failures ? 1 : 0;
}
