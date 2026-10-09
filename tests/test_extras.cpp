// The OpenF1 post-session half of the summary (8.1, decision 134), run against
// the REAL captured responses for session 11377 - the same bytes the device
// will parse once a race's live window has closed.
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "../f1-tracker/f1_json.h"
#include "../f1-tracker/f1_store.h"

static int checks = 0, failures = 0;
static void okf(bool cond, const char *fmt, ...) {
  checks++;
  if (!cond) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

static std::string slurp(const char *rel) {
  std::string p = std::string("../reference/samples/") + rel;
  FILE *f = std::fopen(p.c_str(), "rb");
  if (!f) { std::printf("  FAIL  cannot open %s\n", p.c_str()); failures++; return {}; }
  std::string out; char buf[8192]; size_t r;
  while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, r);
  std::fclose(f);
  return out;
}

using namespace f1::store;

static uint32_t utc(int y, int mo, int d, int h, int mi) {
  char dd[16], tt[16];
  std::snprintf(dd, sizeof(dd), "%04d-%02d-%02d", y, mo, d);
  std::snprintf(tt, sizeof(tt), "%02d:%02d:00", h, mi);
  return to_epoch(dd, tt);
}

static void test_no_results() {
  std::printf("OpenF1's empty answer is a 404\n");
  const char *body = "{\"detail\":\"No results found.\"}";
  okf(openf1_no_results(404, body, std::strlen(body)),
      "404 + 'No results found' is an EMPTY RESULT (a race with no red flag)");
  okf(!openf1_no_results(404, "<html>Not Found</html>", 22), "a bare 404 is a real fault");
  okf(!openf1_no_results(500, body, std::strlen(body)), "only a 404 is an empty result");
  okf(!openf1_no_results(200, body, std::strlen(body)), "a 200 is not an empty result");
  okf(!openf1_no_results(404, nullptr, 0), "no body is not an empty result");
}

static void test_sessions() {
  std::printf("choosing the race to describe\n");
  const std::string j = slurp("openf1-2026-race-sessions.json");
  Store st;

  st.parse_now = utc(2026, 9, 27, 9, 0);     // the day after Baku
  okf(parse_sessions(j.data(), j.size(), st), "a closed race is found");
  okf(st.extras.session_key == 11377, "the latest closed race is Baku (11377), got %u",
      (unsigned) st.extras.session_key);
  okf(st.extras.race_day == utc(2026, 9, 26, 0, 0), "race day is the UTC date");

  Store mid;                                  // 13:10 - end 13:00, window closes 13:30
  mid.parse_now = utc(2026, 9, 26, 13, 10);
  okf(parse_sessions(j.data(), j.size(), mid) && mid.extras.session_key == 11369,
      "NET-14: a race still inside its window is NOT chosen - the previous one is");

  Store after;
  after.parse_now = utc(2026, 9, 26, 13, 31);
  okf(parse_sessions(j.data(), j.size(), after) && after.extras.session_key == 11377,
      "the moment the window closes, it is chosen");

  Store early;
  early.parse_now = utc(2026, 3, 1, 0, 0);
  okf(!parse_sessions(j.data(), j.size(), early), "before any race: nothing to describe");

  Store nonow;
  okf(!parse_sessions(j.data(), j.size(), nonow), "no clock: refuses rather than guesses");

  // A Sprint carries session_type "Race" too (measured: 2026 round 2).
  Store sp;
  sp.parse_now = utc(2026, 3, 15, 0, 0);
  okf(parse_sessions(j.data(), j.size(), sp), "found");
  okf(sp.extras.session_key == 11234,
      "the Sprint (session_type Race, name Sprint) is skipped - the GP is chosen, got %u",
      (unsigned) sp.extras.session_key);

  // A new race resets everything gathered for the old one.
  Store roll = st;
  roll.extras.have_stints = roll.extras.have_weather = true;
  roll.parse_now = utc(2026, 10, 5, 0, 0);
  okf(parse_sessions(j.data(), j.size(), roll) && roll.extras.session_key == 11731,
      "a later race is picked up");
  okf(!roll.extras.have_stints && !roll.extras.have_weather,
      "a new race must not inherit the last race's tyres and weather");
}

static void test_stints() {
  std::printf("tyre stints\n");
  const std::string j = slurp("openf1-stints-11377.json");
  Store st;
  okf(parse_stints(j.data(), j.size(), st), "parses");
  okf(st.extras.n_stints == 22, "22 drivers, got %d", st.extras.n_stints);
  const DriverStints *d = nullptr;
  for (int i = 0; i < st.extras.n_stints; i++) if (st.extras.stints[i].number == 1) d = &st.extras.stints[i];
  okf(d != nullptr, "car 1 present");
  if (d) {
    okf(d->n == 2, "two stints, got %d", d->n);
    okf(d->compound[0] == 'M' && d->laps[0] == 30, "stint 1: medium, 30 laps (1-30)");
    okf(d->compound[1] == 'S' && d->laps[1] == 5, "stint 2: soft, 5 laps (31-35)");
  }
  Store e;
  okf(!parse_stints("[]", 2, e), "no stints is a failed poll, not an empty strategy");
  okf(!parse_stints("", 0, e), "empty body fails");
  okf(compound_letter("SOFT") == 'S' && compound_letter("MEDIUM") == 'M' &&
      compound_letter("HARD") == 'H' && compound_letter("INTERMEDIATE") == 'I' &&
      compound_letter("WET") == 'W' && compound_letter("HYPERSOFT") == '?',
      "compound letters, with an unknown one shown as '?'");
}

static void test_weather() {
  std::printf("weather\n");
  const std::string j = slurp("openf1-weather-11377.json");
  Store st;
  okf(parse_weather(j.data(), j.size(), st), "parses");
  okf(st.extras.air_min > 25.9f && st.extras.air_max < 26.7f, "air 26.0-26.6, got %.1f-%.1f",
      (double) st.extras.air_min, (double) st.extras.air_max);
  okf(st.extras.track_min > 30.0f,
      "DATA-12: the 0.0 track-sensor dropouts are ignored, got min %.1f",
      (double) st.extras.track_min);
  okf(st.extras.track_max > 48.0f && st.extras.track_max < 48.2f, "track max 48.1");
  okf(!st.extras.rain, "a dry race reports dry");
  Store e;
  okf(!parse_weather("[]", 2, e), "no rows is a failed poll");
}

static void test_incidents() {
  std::printf("safety cars and red flags\n");
  const std::string sc = slurp("openf1-safetycar-11377.json");
  Store st;
  okf(parse_safety(sc.data(), sc.size(), st), "parses");
  okf(st.extras.have_safety, "answered");
  okf(st.extras.n_incidents == 2, "two DEPLOYMENTS (laps 31 and 36), not four messages: got %d",
      st.extras.n_incidents);
  okf(st.extras.incidents[0].lap == 31 && st.extras.incidents[0].kind == 'S', "first at lap 31");
  okf(st.extras.incidents[1].lap == 36, "second at lap 36");

  // The empty answer OpenF1 gives for a race with no red flag, post-translation.
  okf(parse_red("[]", 2, st) && st.extras.have_red, "an empty red-flag list is a valid answer");
  okf(st.extras.n_incidents == 2, "...and adds nothing");

  const char *vsc = "[{\"lap_number\":9,\"category\":\"SafetyCar\",\"message\":\"VIRTUAL SAFETY CAR DEPLOYED\"}]";
  Store v;
  okf(parse_safety(vsc, std::strlen(vsc), v) && v.extras.incidents[0].kind == 'V', "VSC is its own kind");

  const char *red = "[{\"lap_number\":12,\"flag\":\"RED\",\"scope\":\"Track\"},"
                    "{\"lap_number\":13,\"flag\":\"RED\",\"scope\":\"Sector\"}]";
  Store r;
  okf(parse_red(red, std::strlen(red), r), "parses");
  okf(r.extras.n_incidents == 1 && r.extras.incidents[0].kind == 'R' && r.extras.incidents[0].lap == 12,
      "only the track-wide red flag counts");
  okf(!parse_red("{\"detail\":\"x\"}", 14, r), "an object where an array belongs fails");
}

static void test_format() {
  std::printf("the summary text\n");
  Store st;
  st.parse_now = utc(2026, 9, 27, 9, 0);
  const std::string sess = slurp("openf1-2026-race-sessions.json");
  const std::string stints = slurp("openf1-stints-11377.json");
  const std::string wx = slurp("openf1-weather-11377.json");
  const std::string sc = slurp("openf1-safetycar-11377.json");
  parse_sessions(sess.data(), sess.size(), st);
  parse_stints(stints.data(), stints.size(), st);
  parse_weather(wx.data(), wx.size(), st);
  parse_safety(sc.data(), sc.size(), st);
  parse_red("[]", 2, st);
  okf(st.extras.complete(), "everything gathered");

  PodiumRef pod[3];
  pod[0].number = 1;  std::snprintf(pod[0].code, sizeof(pod[0].code), "NOR");
  char b[400];

  st.summary.race_day = utc(2026, 9, 26, 0, 0);
  const int k = format_extras(st, pod, b, sizeof(b));
  okf(k > 0, "text produced");
  okf(std::strstr(b, "NOR M30 S5") != nullptr, "tyre line: NOR M30 S5\n%s", b);
  okf(std::strstr(b, "Weather") && std::strstr(b, "dry"), "weather line");
  okf(std::strstr(b, "Safety car, lap 31") && std::strstr(b, "Safety car, lap 36"), "both safety cars");
  okf(std::strstr(b, "Track 3") != nullptr, "track temperature is the real one, not 0\n%s", b);

  // The mismatch guard: another race's data under this race's summary is the
  // kind of error that looks like correct data.
  st.summary.race_day = utc(2026, 10, 4, 0, 0);
  okf(format_extras(st, pod, b, sizeof(b)) == 0 && b[0] == '\0',
      "extras for Baku are NOT shown under a later race's summary");
  st.summary.race_day = 0;
  okf(format_extras(st, pod, b, sizeof(b)) == 0, "no summary date: show nothing");

  // A clean race says so rather than staying silent.
  Store clean = st;
  clean.summary.race_day = utc(2026, 9, 26, 0, 0);
  clean.extras.n_incidents = 0;
  format_extras(clean, pod, b, sizeof(b));
  okf(std::strstr(b, "No safety car or red flag") != nullptr, "a clean race says so");

  // Incidents come out in lap order whatever order they were gathered in.
  Store mix = st;
  mix.summary.race_day = utc(2026, 9, 26, 0, 0);
  mix.extras.n_incidents = 0;
  add_incident(mix.extras, 40, 'S'); add_incident(mix.extras, 5, 'R');
  format_extras(mix, pod, b, sizeof(b));
  const char *r5 = std::strstr(b, "Red flag, lap 5"), *s40 = std::strstr(b, "Safety car, lap 40");
  okf(r5 && s40 && r5 < s40, "sorted by lap\n%s", b);

  // Not complete yet: show only what has arrived.
  Store part;
  part.parse_now = st.parse_now;
  parse_sessions(sess.data(), sess.size(), part);
  parse_weather(wx.data(), wx.size(), part);
  part.summary.race_day = utc(2026, 9, 26, 0, 0);
  format_extras(part, pod, b, sizeof(b));
  okf(std::strstr(b, "Weather") && !std::strstr(b, "Tyres") && !std::strstr(b, "Incidents"),
      "a partial fetch shows what it has and invents nothing");
}

int main() {
  std::printf("\nF1 Tracker - OpenF1 post-session extras (real fixtures)\n\n");
  test_no_results();
  test_sessions();
  test_stints();
  test_weather();
  test_incidents();
  test_format();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
