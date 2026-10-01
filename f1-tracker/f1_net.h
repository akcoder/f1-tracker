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
struct Plan {
  bool want_calendar = false;
  bool want_entry_list = false;
  bool want_qualifying = false;
  bool want_results = false;
  bool want_standings = false;
  bool want_summary = false;      // 8.1: fastest lap + pit stops, Jolpica only
  bool openf1_blocked = false;
};

inline Plan plan_for(const state::Calendar &cal, const state::Status &st,
                     uint32_t now_utc, uint32_t last_cal_ms, uint32_t now_ms) {
  Plan p;
  const auto iv = state::intervals_for(st.weekend);
  const bool cal_due = (now_ms - last_cal_ms) >= (uint32_t) iv.jolpica_s * 1000u;
  p.want_calendar = cal_due;
  p.openf1_blocked = state::any_window_open(cal, now_utc);

  switch (st.weekend) {
    case state::RACE_WEEK:
    case state::SESSION_SOON:
      p.want_entry_list = cal_due;
      p.want_qualifying = cal_due && st.order != state::ENTRY_LIST;
      break;
    case state::POST_SESSION:
      p.want_results = true;
      p.want_standings = cal_due;
      // 8.1: the summary needs no OpenF1 - Jolpica carries both the fastest lap
      // and the pit stops - so there is no window to wait out.
      p.want_summary = cal_due;
      break;
    case state::OFF_SEASON:
      p.want_standings = cal_due;     // the completed season's final table
      break;
    default:
      break;
  }
  return p;
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
  uint32_t last_cal_ms = 0;
  uint32_t backoff_ms = 0;
  for (;;) {
    const uint32_t now_ms = (uint32_t) (esp_timer_get_time() / 1000);
    if (g_paused || !g_wifi_up) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    if (backoff_ms && now_ms < backoff_ms) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

    // The state machine decides what is worth asking for (3.6), and refuses to
    // plan any OpenF1 request inside a live window (NET-14).
    state::Calendar cal;
    if (g_front.have_calendar) {
      cal.rounds = g_front.rounds; cal.n = g_front.n_rounds;
      cal.season = g_front.season; cal.fetched = true;
    }
    const uint32_t utc = (uint32_t) ::time(nullptr);   // ESPHome has its own time:: namespace
    const state::Status st = state::evaluate(cal, utc, utc > 1700000000u);
    const Plan p = plan_for(cal, st, utc, last_cal_ms, now_ms);
    bool did = false;

    if (p.want_calendar) {
      // RACE-13a: the season is resolved from /current/ and never pinned.
      std::snprintf(url, sizeof(url), "%s/current/races/?format=json&limit=40", JOLPICA);
      if (get_and_parse(g_jolpica, url, store::parse_calendar, now_ms,
                        &g_stats.jolpica_ok_ms)) {
        publish();
        last_cal_ms = now_ms;
      }
      g_stats.jolpica_fetches++;
      did = true;
    }

    if (p.want_qualifying && !did) {
      std::snprintf(url, sizeof(url), "%s/current/last/qualifying/?format=json", JOLPICA);
      if (get_and_parse(g_jolpica, url, store::parse_qualifying, now_ms,
                        &g_stats.jolpica_ok_ms))
        publish();
      g_stats.jolpica_fetches++;
      did = true;
    }

    if (p.want_results && !did) {
      std::snprintf(url, sizeof(url), "%s/current/last/results/?format=json", JOLPICA);
      if (get_and_parse(g_jolpica, url, store::parse_results, now_ms,
                        &g_stats.jolpica_ok_ms))
        publish();
      g_stats.jolpica_fetches++;
      did = true;
    }

    if (p.want_standings && !did) {
      std::snprintf(url, sizeof(url), "%s/current/driverStandings/?format=json", JOLPICA);
      if (get_and_parse(g_jolpica, url, store::parse_standings, now_ms,
                        &g_stats.jolpica_ok_ms))
        publish();
      g_stats.jolpica_fetches++;
      did = true;
    }

    if (p.want_summary && !did) {
      std::snprintf(url, sizeof(url), "%s/current/last/fastest/1/results/?format=json",
                    JOLPICA);
      if (get_and_parse(g_jolpica, url, store::parse_fastest, now_ms,
                        &g_stats.jolpica_ok_ms))
        publish();
      g_stats.jolpica_fetches++;
      std::snprintf(url, sizeof(url), "%s/current/last/pitstops/?format=json&limit=100",
                    JOLPICA);
      if (get_and_parse(g_jolpica, url, store::parse_pitstops, now_ms,
                        &g_stats.jolpica_ok_ms))
        publish();
      g_stats.jolpica_fetches++;
      did = true;
    }

    if (p.openf1_blocked) g_stats.skipped_window++;

    // 3.7.4: back off HARD on a rate limit or a bad request shape. Only a
    // network failure gets a brisk retry.
    if (g_stats.last_fault == RATE_LIMITED) backoff_ms = now_ms + 10 * 60 * 1000;
    else if (g_stats.last_fault == HTTP_ERROR) backoff_ms = now_ms + 5 * 60 * 1000;
    else backoff_ms = 0;

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
