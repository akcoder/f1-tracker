#pragma once
// The data task: HTTPS to Jolpica and OpenF1, selective parse, and the poll
// scheduling the state machine drives (sections 3.6, 4.2).
//
// Runs on its own FreeRTOS task so a fetch, a TLS handshake or a parse can
// never block rendering (section 9). The UI thread only ever reads the result
// under a short critical section.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "f1_json.h"
#include "f1_state.h"
#include "f1_store.h"

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include <ctime>
#endif

namespace f1 {
namespace net {

inline constexpr const char *TAG = "f1_net";

// NET-15 / decision 102: the esp_http_client receive buffer defaults to 512 B,
// which would make a 47 KB response ~94 read calls, each resuming the parser.
inline constexpr int HTTP_RX_BUFFER = 4096;

// Sized for the largest response the device will ever ask for: OpenF1
// sessions?year=X at 47.0 KB measured (3.0). Allocated from PSRAM.
inline constexpr size_t BODY_MAX = 64 * 1024;

inline constexpr const char *JOLPICA = "https://api.jolpi.ca/ergast/f1";
inline constexpr const char *OPENF1 = "https://api.openf1.org/v1";

enum Fault : uint8_t { OK = 0, NO_WIFI, TIMEOUT, RATE_LIMITED, HTTP_ERROR, PARSE_ERROR };

inline const char *fault_name(Fault f) {
  switch (f) {
    case OK:           return "";
    case NO_WIFI:      return "NO WI-FI";
    case TIMEOUT:      return "NO DATA";
    // 3.7.4 / decision 57: 429 and a network failure need OPPOSITE responses,
    // so they must never look the same on screen.
    case RATE_LIMITED: return "RATE LIMITED";
    case HTTP_ERROR:   return "API ERROR";
    case PARSE_ERROR:  return "BAD DATA";
    default:           return "?";
  }
}

struct Stats {
  uint32_t jolpica_ok_ms = 0;     // decision 50: stamped on a successful PARSE,
  uint32_t openf1_ok_ms = 0;      // never on an HTTP 200 carrying garbage
  uint32_t jolpica_fetches = 0;
  uint32_t openf1_fetches = 0;
  uint32_t skipped_window = 0;    // NET-14: requests not made, deliberately
  Fault last_fault = OK;
  uint16_t last_status = 0;
};

inline Stats g_stats;

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)

struct Session {
  esp_http_client_handle_t h = nullptr;
  char host[48] = {0};
};

// NET-12 / decision 65: keep the TLS session open between fetches. A fresh
// handshake measured ~1.2 s of crypto on plane-tracker, and with
// CONFIG_SPIRAM_XIP_FROM_PSRAM that crypto runs on the same bus the RGB panel
// refills from - the measured cause of "lvgl took a long time" warnings. Two
// hosts means two handles.
inline Session g_jolpica, g_openf1;
inline char *g_body = nullptr;
inline size_t g_body_len = 0;
inline SemaphoreHandle_t g_lock = nullptr;
inline volatile bool g_paused = false;   // held across an OTA (2.4.6)

inline bool ensure_body() {
  if (g_body != nullptr) return true;
  g_body = (char *) heap_caps_malloc(BODY_MAX, MALLOC_CAP_SPIRAM);
  if (g_body == nullptr) {
    ESP_LOGE(TAG, "no PSRAM for the %u kB body buffer", (unsigned) (BODY_MAX / 1024));
    return false;
  }
  return true;
}

// One GET. Returns the body length, or 0 on failure with g_stats.last_fault set.
inline size_t fetch(Session &s, const char *url, uint32_t now_ms) {
  if (!ensure_body()) return 0;

  if (s.h == nullptr) {
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;
    cfg.timeout_ms = 15000;
    cfg.buffer_size = HTTP_RX_BUFFER;       // NET-15
    cfg.keep_alive_enable = true;           // NET-12
    s.h = esp_http_client_init(&cfg);
    if (s.h == nullptr) { g_stats.last_fault = HTTP_ERROR; return 0; }
  } else {
    esp_http_client_set_url(s.h, url);
  }

  esp_err_t err = esp_http_client_open(s.h, 0);
  if (err != ESP_OK) {
    // Drop the handle: a half-open connection would desynchronise the next
    // request (decision 65).
    esp_http_client_cleanup(s.h); s.h = nullptr;
    g_stats.last_fault = TIMEOUT;
    return 0;
  }
  esp_http_client_fetch_headers(s.h);
  const int status = esp_http_client_get_status_code(s.h);
  g_stats.last_status = (uint16_t) status;

  size_t total = 0;
  int r;
  while ((r = esp_http_client_read(s.h, g_body + total,
                                   (int) (BODY_MAX - 1 - total))) > 0) {
    total += (size_t) r;
    if (total >= BODY_MAX - 1) break;
  }
  g_body[total] = '\0';
  const bool complete = esp_http_client_is_complete_data_received(s.h);
  esp_http_client_close(s.h);

  if (!complete) {
    // decision 65: a body not read to completion leaves the stream out of step,
    // so the handle cannot be reused.
    esp_http_client_cleanup(s.h); s.h = nullptr;
  }

  if (&s == &g_openf1 && store::openf1_no_results(status, g_body, total)) {
    // OpenF1 answers a filter that matches nothing with 404 "No results found".
    // That is an empty result (a race with no red flag), not a fault - hand the
    // parser an empty array rather than backing the whole task off.
    std::memcpy(g_body, "[]", 3);
    g_body_len = 2;
    g_stats.last_status = 200;
    return 2;
  }
  if (status == 429) {
    // 3.7.4: a 429 is NOT a bad request shape. Back off hard; never retry it
    // tightly, because a loop is what earns an IP block.
    g_stats.last_fault = RATE_LIMITED;
    return 0;
  }
  if (status < 200 || status >= 300) {
    g_stats.last_fault = HTTP_ERROR;
    return 0;
  }
  if (total == 0) {
    // 3.4: an empty body is a FAILED poll - keep the existing state, do not
    // clear it, and retry next cycle.
    g_stats.last_fault = PARSE_ERROR;
    return 0;
  }
  g_body_len = total;
  return total;
}

inline void pause() { g_paused = true; }
inline void resume() { g_paused = false; }

// 2.4.6: the data task is paused for the duration of an OTA and resumed if the
// upgrade fails.
inline void ota_begin() { pause(); }
inline void ota_error() { resume(); }

#else   // host build: the fetch is device-only, the parse is not

inline void pause() {}
inline void resume() {}
inline void ota_begin() {}
inline void ota_error() {}

#endif

// ---- what we ask for, and when ------------------------------------------
// 3.6: the mapping lives in ONE place. Nothing here issues a request inside an
// OpenF1 live window - NET-14 means the device computes the window and skips,
// which cannot be rate-limited, mistaken for a fault, or return a stale
// snapshot that looks like success.
//
// Every resource has its OWN timer. The first version shared one - the
// calendar's - so the moment the calendar succeeded nothing else was due, and
// qualifying, standings, constructors and the summary were never fetched; while
// POST_SESSION's results had no interval at all and would have been re-requested
// every ~2 s (1,800/hour against Jolpica's 500). Neither was visible without a
// board, which is why the schedule is now pure and has its own host test.
enum Res : uint8_t {
  RES_CALENDAR, RES_ROSTER, RES_QUALI, RES_RESULTS, RES_STANDINGS,
  RES_CONSTRUCTORS, RES_SUMMARY, RES_EXTRAS, RES_N, RES_NONE = 255
};

inline constexpr uint32_t RETRY_AFTER_FAIL_S = 60;   // 3.7.4: never a tight retry

struct Clock {
  uint32_t ok_ms[RES_N] = {0};        // decision 50: stamped on a successful PARSE
  bool have[RES_N] = {false};
  uint32_t not_before_ms[RES_N] = {0};
  bool gated[RES_N] = {false};

  void stamp(Res r, uint32_t now_ms) { ok_ms[r] = now_ms; have[r] = true; gated[r] = false; }
  // A failure is retried, but never tighter than this - a bad-request loop looks
  // exactly like the service being down, and Jolpica blocks without notice.
  void defer(Res r, uint32_t now_ms, uint32_t secs) {
    not_before_ms[r] = now_ms + secs * 1000u; gated[r] = true;
  }
  bool ready(Res r, uint32_t now_ms) const {
    return !gated[r] || (int32_t) (now_ms - not_before_ms[r]) >= 0;
  }
  bool due(Res r, uint32_t interval_s, uint32_t now_ms) const {
    if (!ready(r, now_ms)) return false;
    return !have[r] || (now_ms - ok_ms[r]) >= interval_s * 1000u;
  }
};

struct Plan {
  bool want_calendar = false;
  bool want_roster = false;       // RACE-13d/e: who is racing this season
  bool want_qualifying = false;
  bool want_results = false;
  bool want_standings = false;
  bool want_summary = false;      // 8.1: fastest lap + pit stops, Jolpica only
  bool want_constructors = false;
  bool want_extras = false;       // 8.1: OpenF1 tyres, weather, incidents
  bool openf1_blocked = false;
};

// `extras_pending`: the caller knows whether the store still lacks a piece of
// the post-session extras (store::extras_pending). Fetched ONCE per race (3.6.2).
inline Plan plan_for(const state::Calendar &cal, const state::Status &st,
                     uint32_t now_utc, const Clock &clk, uint32_t now_ms,
                     bool extras_pending) {
  Plan p;
  const auto iv = state::intervals_for(st.weekend);
  p.openf1_blocked = state::any_window_open(cal, now_utc);
  const uint32_t j = (uint32_t) iv.jolpica_s;

  p.want_calendar = clk.due(RES_CALENDAR, j, now_ms);
  // The roster rides the calendar's cadence: a season changes once a year, so
  // this costs one 5.6 KB request per interval and follows a rollover by itself.
  p.want_roster = clk.due(RES_ROSTER, j, now_ms);

  switch (st.weekend) {
    case state::RACE_WEEK:
    case state::SESSION_SOON:
      p.want_qualifying = st.order != state::ENTRY_LIST && clk.due(RES_QUALI, j, now_ms);
      p.want_standings = clk.due(RES_STANDINGS, j, now_ms);
      p.want_constructors = clk.due(RES_CONSTRUCTORS, j, now_ms);
      break;
    case state::POST_SESSION: {
      p.want_standings = clk.due(RES_STANDINGS, j, now_ms);
      p.want_constructors = clk.due(RES_CONSTRUCTORS, j, now_ms);
      // POST_SESSION follows ANY session, and "last/results" is the last RACE's.
      // Asked for after qualifying it would hand back the previous Grand Prix's
      // finishing order and label it FINAL on a weekend that has not raced. So
      // the race-only requests wait for the Grand Prix itself, and after
      // qualifying the thing worth fetching is the grid (3.5).
      const bool grand_prix = st.current.valid() && st.current.session == calendar::RACE;
      if (grand_prix) {
        p.want_results = clk.due(RES_RESULTS, j, now_ms);
        // 8.1: the summary needs no OpenF1 - Jolpica carries both the fastest
        // lap and the pit stops - so there is no window to wait out.
        p.want_summary = clk.due(RES_SUMMARY, j, now_ms);
        // Decision 134: the rest of the summary needs OpenF1. POST_SESSION is
        // entered exactly when the window closes (decision 60), so this is the
        // first moment it is readable - but NET-14 still gates it.
        p.want_extras = !p.openf1_blocked && extras_pending &&
                        clk.ready(RES_EXTRAS, now_ms);
      } else {
        p.want_qualifying = st.order != state::ENTRY_LIST && clk.due(RES_QUALI, j, now_ms);
      }
      break;
    }
    case state::IDLE:
    case state::OFF_SEASON:
      // The standings page must not be empty after a reboot in the long gaps
      // between races; these are the completed season's final tables off-season.
      p.want_standings = clk.due(RES_STANDINGS, j, now_ms);
      p.want_constructors = clk.due(RES_CONSTRUCTORS, j, now_ms);
      break;
    default:
      break;
  }
  return p;
}

// One request per loop, in a fixed priority. Shared by the device task and the
// host test so the two cannot disagree about the order.
inline Res pick(const Plan &p) {
  if (p.want_calendar) return RES_CALENDAR;
  if (p.want_roster) return RES_ROSTER;
  if (p.want_qualifying) return RES_QUALI;
  if (p.want_results) return RES_RESULTS;
  if (p.want_standings) return RES_STANDINGS;
  if (p.want_constructors) return RES_CONSTRUCTORS;
  if (p.want_summary) return RES_SUMMARY;
  if (p.want_extras) return RES_EXTRAS;
  return RES_NONE;
}

// ---- the fetch task ------------------------------------------------------
#if defined(USE_ESP32) && !defined(F1_HOST_TEST)

// Double-buffered: the task parses into `back` and swaps under the lock, so the
// UI thread never reads a half-written store (section 9 - a fetch must never
// block rendering, and a render must never see a torn parse).
inline store::Store g_front, g_back;
inline volatile bool g_wifi_up = false;
inline volatile uint32_t g_published = 0;

inline void lock() { if (g_lock) xSemaphoreTake(g_lock, portMAX_DELAY); }
inline void unlock() { if (g_lock) xSemaphoreGive(g_lock); }

inline void publish() {
  lock();
  std::memcpy(&g_front, &g_back, sizeof(store::Store));
  // The parsed Round structs point at static buffers inside parse_calendar, so
  // the copy is safe: those buffers outlive both stores and are only rewritten
  // by the next successful parse, which also republishes.
  g_published = g_back.generation;
  unlock();
}

// Reads the published store under the lock. Callers copy what they need and
// get out - the lock is held for a memcpy, never for drawing.
inline uint32_t snapshot(store::Store &out) {
  lock();
  std::memcpy(&out, &g_front, sizeof(store::Store));
  const uint32_t g = g_published;
  unlock();
  return g;
}

inline bool get_and_parse(Session &sess, const char *url,
                          bool (*fn)(const char *, size_t, store::Store &),
                          uint32_t now_ms, uint32_t *stamp) {
  const size_t len = fetch(sess, url, now_ms);
  if (len == 0) return false;
  if (!fn(g_body, len, g_back)) {
    // 3.4: an unparseable body is a FAILED poll. Keep the existing state.
    g_stats.last_fault = PARSE_ERROR;
    return false;
  }
  // decision 50: the age stamp goes on a successful PARSE, never on an HTTP
  // 200 - a 200 carrying garbage is not fresh data.
  g_stats.last_fault = OK;
  if (stamp) *stamp = now_ms;
  return true;
}

inline void task_body(void *) {
  char url[160];
  Clock clk;
  uint32_t backoff_ms = 0;
  for (;;) {
    const uint32_t now_ms = (uint32_t) (esp_timer_get_time() / 1000);
    if (g_paused || !g_wifi_up) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (backoff_ms && (int32_t) (now_ms - backoff_ms) < 0) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

    // The state machine decides what is worth asking for (3.6), and refuses to
    // plan any OpenF1 request inside a live window (NET-14).
    state::Calendar cal;
    if (g_front.have_calendar) {
      cal.rounds = g_front.rounds; cal.n = g_front.n_rounds;
      cal.season = g_front.season; cal.fetched = true;
    }
    const uint32_t utc = (uint32_t) ::time(nullptr);   // ESPHome has its own time:: namespace
    const state::Status st = state::evaluate(cal, utc, utc > 1700000000u);
    const Plan p = plan_for(cal, st, utc, clk, now_ms, store::extras_pending(g_front));
    const Res r = pick(p);
    bool did = r != RES_NONE;
    bool ok = false;
    bool recheck = false;           // an extras re-resolve that may make no progress
    bool (*fn)(const char *, size_t, store::Store &) = nullptr;
    Session *sess = &g_jolpica;
    uint32_t *stamp = &g_stats.jolpica_ok_ms;

    switch (r) {
      case RES_CALENDAR:
        // RACE-13a: the season is resolved from /current/ and never pinned.
        std::snprintf(url, sizeof(url), "%s/current/races/?format=json&limit=40", JOLPICA);
        fn = store::parse_calendar; break;
      case RES_ROSTER:
        std::snprintf(url, sizeof(url), "%s/current/drivers/?format=json&limit=100", JOLPICA);
        fn = store::parse_roster; break;
      case RES_QUALI:
        std::snprintf(url, sizeof(url), "%s/current/last/qualifying/?format=json", JOLPICA);
        fn = store::parse_qualifying; break;
      case RES_RESULTS:
        std::snprintf(url, sizeof(url), "%s/current/last/results/?format=json", JOLPICA);
        fn = store::parse_results; break;
      case RES_STANDINGS:
        std::snprintf(url, sizeof(url), "%s/current/driverStandings/?format=json", JOLPICA);
        fn = store::parse_standings; break;
      case RES_CONSTRUCTORS:
        std::snprintf(url, sizeof(url), "%s/current/constructorStandings/?format=json", JOLPICA);
        fn = store::parse_constructors; break;
      case RES_SUMMARY:
        // Two requests, one resource: the fastest lap, then the pit stops. The
        // resource counts as done only if BOTH parse.
        std::snprintf(url, sizeof(url), "%s/current/last/fastest/1/results/?format=json", JOLPICA);
        fn = store::parse_fastest; break;
      case RES_EXTRAS: {
        // 8.1 / decision 134: tyres, weather and incidents, from OpenF1, fetched
        // ONCE per race - one piece per loop, the first the store lacks.
        const store::Extras &fx = g_front.extras;
        const bool stale = fx.complete() && !store::extras_match_summary(g_front) &&
                           g_front.summary.race_day != 0;
        sess = &g_openf1; stamp = &g_stats.openf1_ok_ms;
        recheck = stale;
        if (!fx.have_session || stale) {
          std::snprintf(url, sizeof(url), "%s/sessions?year=%u&session_type=Race", OPENF1,
                        (unsigned) g_front.season);
          fn = store::parse_sessions;
        } else if (!fx.have_stints) {
          std::snprintf(url, sizeof(url), "%s/stints?session_key=%u", OPENF1, (unsigned) fx.session_key);
          fn = store::parse_stints;
        } else if (!fx.have_weather) {
          std::snprintf(url, sizeof(url), "%s/weather?session_key=%u", OPENF1, (unsigned) fx.session_key);
          fn = store::parse_weather;
        } else if (!fx.have_safety) {
          std::snprintf(url, sizeof(url), "%s/race_control?session_key=%u&category=SafetyCar",
                        OPENF1, (unsigned) fx.session_key);
          fn = store::parse_safety;
        } else {
          std::snprintf(url, sizeof(url), "%s/race_control?session_key=%u&flag=RED", OPENF1,
                        (unsigned) fx.session_key);
          fn = store::parse_red;
        }
        break;
      }
      default: break;
    }

    if (fn != nullptr) {
      g_back.parse_now = utc;
      ok = get_and_parse(*sess, url, fn, now_ms, stamp);
      if (ok && r == RES_SUMMARY) {
        std::snprintf(url, sizeof(url), "%s/current/last/pitstops/?format=json&limit=100", JOLPICA);
        ok = get_and_parse(g_jolpica, url, store::parse_pitstops, now_ms, stamp);
        g_stats.jolpica_fetches++;
      }
      if (ok) {
        publish();
        // The calendar is also what moves the season, so its stamp is the one
        // the debug page reports.
        clk.stamp(r, now_ms);
        // A re-resolve that finds the same race again would otherwise repeat
        // every loop; every other extras step makes progress and may go straight on.
        if (recheck) clk.defer(r, now_ms, (uint32_t) state::intervals_for(st.weekend).openf1_s);
      } else {
        // 3.7.4: a failed resource is retried, but never tightly. OpenF1 steps
        // wait out the state's own interval (5 min in POST_SESSION).
        const uint32_t wait = (r == RES_EXTRAS)
            ? (uint32_t) state::intervals_for(st.weekend).openf1_s : RETRY_AFTER_FAIL_S;
        clk.defer(r, now_ms, wait ? wait : RETRY_AFTER_FAIL_S);
      }
      if (sess == &g_openf1) g_stats.openf1_fetches++; else g_stats.jolpica_fetches++;
    }

    if (p.openf1_blocked) g_stats.skipped_window++;

    // 3.7.4: back off HARD on a rate limit or a bad request shape; a network
    // failure gets a shorter wait, but never a tight loop.
    if (did && !ok) {
      if (g_stats.last_fault == RATE_LIMITED) backoff_ms = now_ms + 10 * 60 * 1000;
      else if (g_stats.last_fault == HTTP_ERROR) backoff_ms = now_ms + 5 * 60 * 1000;
      else backoff_ms = now_ms + 30 * 1000;
    } else {
      backoff_ms = 0;
    }

    vTaskDelay(pdMS_TO_TICKS(did ? 2000 : 5000));
  }
}

inline void start() {
  if (g_lock == nullptr) g_lock = xSemaphoreCreateMutex();
  // Pinned to core 1 so it cannot contend with the LVGL/display work on core 0.
  xTaskCreatePinnedToCore(task_body, "f1_net", 8192, nullptr, 3, nullptr, 1);
}

inline void wifi_up(bool up) { g_wifi_up = up; }

#else
inline void start() {}
inline void wifi_up(bool) {}
#endif

}  // namespace net
}  // namespace f1
