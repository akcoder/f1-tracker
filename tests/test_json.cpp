// The selective JSON scanner, run against the REAL captured responses in
// reference/samples/ - the same bytes the device will parse (section 4.2).
// Every data-quality case in section 3.4 is asserted here, because each one was
// observed in these exact files.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "../f1-tracker/f1_json.h"

static int checks = 0, failures = 0;
static void okf(bool cond, const char *fmt, ...) {
  checks++;
  if (!cond) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

using namespace f1::json;

static std::string slurp(const char *rel) {
  std::string p = std::string("../reference/samples/") + rel;
  FILE *f = std::fopen(p.c_str(), "rb");
  if (!f) { std::printf("  FAIL  cannot open %s\n", p.c_str()); failures++; return {}; }
  std::string out;
  char buf[8192]; size_t r;
  while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, r);
  std::fclose(f);
  return out;
}

static void test_primitives() {
  std::printf("scanner primitives\n");
  const char *s = R"({"a":"one","b":2,"c":null,"d":{"a":"nested"},"e":[1,2,3],"f":"quo\"te"})";
  const size_t n = std::strlen(s);
  char buf[64]; long i; double d;

  okf(get_str(s, 0, n, "a", buf, sizeof(buf)) && std::strcmp(buf, "one") == 0, "string");
  okf(get_int(s, 0, n, "b", i) && i == 2, "number");
  okf(!get_str(s, 0, n, "c", buf, sizeof(buf)), "null is absent, not empty");
  okf(!get_str(s, 0, n, "zz", buf, sizeof(buf)), "missing key is absent");
  // A nested object must not leak its keys to the top level.
  okf(get_str(s, 0, n, "a", buf, sizeof(buf)) && std::strcmp(buf, "one") == 0,
      "nested 'a' must not shadow the top-level one");
  okf(get_str(s, 0, n, "f", buf, sizeof(buf)) && std::strcmp(buf, "quo\"te") == 0,
      "escaped quote inside a value");
  const size_t arr = find_key(s, 0, n, "e");
  okf(array_len(s, arr, n) == 3, "array length");
  okf(get_double(s, 0, n, "b", d) && d == 2.0, "double");
  // Jolpica quotes its numbers; both forms must read.
  const char *q = R"({"lat":"-37.8497","n":5})";
  okf(get_double(q, 0, std::strlen(q), "lat", d) && d < -37.8 && d > -37.9,
      "quoted number");
}

static void test_jolpica_races() {
  std::printf("Jolpica races (the real 2026 calendar)\n");
  const std::string j = slurp("jolpica-2026-races.json");
  if (j.empty()) return;
  const char *s = j.c_str(); const size_t n = j.size();

  const size_t races = path(s, n, "MRData.RaceTable.Races");
  okf(races != NPOS, "found MRData.RaceTable.Races");
  const int nr = array_len(s, races, n);
  okf(nr == 23, "expected 23 rounds, got %d", nr);

  char buf[128];
  bool saw_sprint = false, saw_mismatch = false;
  for (int i = 0; i < nr; i++) {
    const size_t r = array_at(s, races, n, i);
    okf(r != NPOS, "round %d missing", i);
    okf(get_str(s, r, n, "raceName", buf, sizeof(buf)), "round %d has no raceName", i);
    long round = 0;
    okf(get_int(s, r, n, "round", round) && round == i + 1, "round number %d", i);

    const size_t ci = find_key(s, r, n, "Circuit");
    okf(ci != NPOS, "round %d has no Circuit", i);
    char cid[64];
    okf(get_str(s, ci, n, "circuitId", cid, sizeof(cid)), "round %d has no circuitId", i);
    const size_t loc = find_key(s, ci, n, "Location");
    char country[64];
    okf(get_str(s, loc, n, "country", country, sizeof(country)),
        "round %d has no Location.country", i);

    if (find_key(s, r, n, "Sprint") != NPOS) saw_sprint = true;

    // decision 18: the race NAME and the circuit's country can disagree.
    // 2026 round 16 is "Bahrain Grand Prix in Malaysia" at Sepang. This is the
    // fixture that proves the flag must key on Location.country.
    if (std::strcmp(cid, "sepang") == 0) {
      saw_mismatch = true;
      okf(std::strcmp(country, "Malaysia") == 0,
          "sepang country should be Malaysia, got '%s'", country);
      okf(std::strstr(buf, "Bahrain") != nullptr,
          "the fixture's name/country mismatch has gone - check decision 18");
    }
  }
  okf(saw_sprint, "the 2026 fixture contains sprint weekends");
  okf(saw_mismatch, "the name/country mismatch fixture is present (decision 18)");
}

static void test_jolpica_drivers() {
  std::printf("Jolpica drivers (DATA-7, DATA-10, decision 20)\n");
  const std::string j = slurp("jolpica-2026-drivers.json");
  if (j.empty()) return;
  const char *s = j.c_str(); const size_t n = j.size();

  const size_t ds = path(s, n, "MRData.DriverTable.Drivers");
  okf(ds != NPOS, "found Drivers");
  const int nd = array_len(s, ds, n);
  okf(nd == 32, "expected 32 driver rows, got %d", nd);

  int with_code = 0, no_nat = 0;
  char buf[64];
  for (int i = 0; i < nd; i++) {
    const size_t d = array_at(s, ds, n, i);
    okf(get_str(s, d, n, "driverId", buf, sizeof(buf)), "driver %d has no driverId", i);
    if (get_str(s, d, n, "code", buf, sizeof(buf))) with_code++;
    if (!get_str(s, d, n, "nationality", buf, sizeof(buf))) no_nat++;
  }
  // DATA-10: the season list is not the entry list.
  okf(with_code == 23, "expected 23 rows with a code, got %d", with_code);
  // 3.4: nationality is MISSING, not null, for 9 of them.
  okf(no_nat == 9, "expected 9 rows with no nationality, got %d", no_nat);

  // DATA-6: Verstappen carries 3 in 2026 and Norris carries 1. Keying a
  // watched driver on the car number would follow the wrong person.
  bool checked_ver = false, checked_nor = false;
  for (int i = 0; i < nd; i++) {
    const size_t d = array_at(s, ds, n, i);
    char id[64]; get_str(s, d, n, "driverId", id, sizeof(id));
    long num = 0;
    if (std::strcmp(id, "max_verstappen") == 0) {
      okf(get_int(s, d, n, "permanentNumber", num) && num == 3,
          "Verstappen should carry 3 in 2026, got %ld", num);
      checked_ver = true;
    }
    if (std::strcmp(id, "norris") == 0) {
      okf(get_int(s, d, n, "permanentNumber", num) && num == 1,
          "Norris should carry 1 in 2026, got %ld", num);
      checked_nor = true;
    }
  }
  okf(checked_ver && checked_nor, "DATA-6 fixtures present");
}

static void test_openf1() {
  std::printf("OpenF1 (decision 16, decision 19)\n");
  const std::string j = slurp("openf1-position-11377.json");
  if (j.empty()) return;
  const char *s = j.c_str(); const size_t n = j.size();

  size_t arr = 0;
  while (arr < n && s[arr] != '[') arr++;
  const int rows = array_len(s, arr, n);
  okf(rows == 311, "expected 311 position rows, got %d", rows);

  // RACE-11 / decision 16: the earliest row per driver IS the grid. Reproduce
  // the extraction the device will do.
  struct First { long num; long pos; char date[40]; };
  std::vector<First> first;
  for (int i = 0; i < rows; i++) {
    const size_t r = array_at(s, arr, n, i);
    long num = 0, pos = 0;
    char date[40];
    if (!get_int(s, r, n, "driver_number", num)) continue;
    get_int(s, r, n, "position", pos);
    get_str(s, r, n, "date", date, sizeof(date));
    bool seen = false;
    for (auto &f : first)
      if (f.num == num) { seen = true; if (std::strcmp(date, f.date) < 0) { f.pos = pos; std::strcpy(f.date, date); } }
    if (!seen) { First f{num, pos, {0}}; std::strcpy(f.date, date); first.push_back(f); }
  }
  okf((int) first.size() == 22, "expected 22 drivers, got %zu", first.size());
  std::vector<long> seen_pos;
  for (auto &f : first) seen_pos.push_back(f.pos);
  std::sort(seen_pos.begin(), seen_pos.end());
  bool complete = seen_pos.size() == 22;
  for (size_t i = 0; i < seen_pos.size() && complete; i++)
    complete = seen_pos[i] == (long) i + 1;
  okf(complete, "earliest rows must form a complete P1..P22 grid");

  // decision 19: country_code is null for every driver, so it is not a
  // nationality source and the flag must come from Jolpica's demonym.
  const std::string d = slurp("openf1-drivers-11377.json");
  const char *t = d.c_str(); const size_t m = d.size();
  size_t da = 0; while (da < m && t[da] != '[') da++;
  const int nd = array_len(t, da, m);
  int has_cc = 0, has_colour = 0;
  char buf[64];
  for (int i = 0; i < nd; i++) {
    const size_t r = array_at(t, da, m, i);
    if (get_str(t, r, m, "country_code", buf, sizeof(buf))) has_cc++;
    if (get_str(t, r, m, "team_colour", buf, sizeof(buf))) has_colour++;
  }
  okf(has_cc == 0, "country_code should be null throughout, %d had one", has_cc);
  okf(has_colour == nd, "team_colour should be present for all %d, got %d", nd, has_colour);
}

#include <algorithm>
int main() {
  std::printf("\nF1 Tracker - JSON scanner tests (against real fixtures)\n\n");
  test_primitives();
  test_jolpica_races();
  test_jolpica_drivers();
  test_openf1();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
