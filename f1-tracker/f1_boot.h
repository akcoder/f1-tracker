#pragma once
// The boot / Wi-Fi status page's behaviour (NET-2b, NET-2c, BOOT-2), ported
// from sky-tracker 4.6.27 and kept LVGL-free so tests/ can build it.
//
// The page is the first LVGL page, so it is what the device boots into. Nothing
// else leaves it: the first version of this project had the page but no code
// that ever left it, so a device that connected would have sat on "Connecting
// to Wi-Fi" for ever. That is the thing this module exists to prevent, and why
// the decision is a pure function with a test.
#include <cstdint>
#include <cstdio>

#include "f1_state.h"

namespace f1 {
namespace boot {

enum Link : uint8_t { CONNECTED = 0, CONNECTING = 1, AP_UP = 2 };

inline Link classify(bool connected, bool ap_active) {
  if (connected) return CONNECTED;
  return ap_active ? AP_UP : CONNECTING;
}

// NET-2b: after this long without Wi-Fi the status page comes back.
inline constexpr uint32_t OFFLINE_RETURN_MS = 10000;

enum Page : uint8_t { STAY = 0, TO_RACE, TO_CIRCUIT, TO_STATUS };

// UI-2a: the page a connect lands on is whichever the state machine calls
// primary (6.1) - the race page around a session, the carousel otherwise.
inline bool race_is_primary(state::Weekend w) {
  return w == state::SESSION_SOON || w == state::SESSION_LIVE ||
         w == state::RACE_LIVE || w == state::POST_SESSION;
}

struct Tracker {
  uint32_t lost_since = 0;     // 0 = not currently offline
  int state = -1;              // the last Link seen; -1 = none yet
};

struct Step {
  Page page = STAY;
  bool link_changed = false;   // the title and body need rewriting
};

// Called every 2 s. `on_status` is whether the status page is showing now;
// `settings_open` stops it ever coming back over the settings page (6.1) - a
// Wi-Fi blip must not throw away someone's half-entered form.
inline Step step(Tracker &t, Link link, uint32_t now_ms, bool on_status,
                 bool settings_open, state::Weekend weekend) {
  Step s;
  if (link == CONNECTED) {
    t.lost_since = 0;
    if (on_status) s.page = race_is_primary(weekend) ? TO_RACE : TO_CIRCUIT;
  } else {
    // 0 means "not offline", so a start at exactly t=0 is stored as 1. (sky-tracker
    // writes `now | 1`, which for an EVEN now is one tick in the future: the
    // subtraction below then wraps to ~4 billion and the status page returns
    // at once instead of after 10 s, on half of all outages.)
    if (t.lost_since == 0) t.lost_since = now_ms ? now_ms : 1u;
    if (!on_status && (uint32_t) (now_ms - t.lost_since) > OFFLINE_RETURN_MS && !settings_open)
      s.page = TO_STATUS;
  }
  s.link_changed = ((int) link != t.state);
  t.state = (int) link;
  return s;
}

inline const char *title_for(Link l) {
  return l == AP_UP ? "Set up Wi-Fi" : "Connecting to Wi-Fi";
}

// NET-2c: the second line is the network being tried - the entry ESPHome has
// selected, which with Improv is the one saved to flash. Credentials are no
// longer a compile-time fact here, so with none at all the line says what the
// device is waiting for instead of showing a blank or a stale name.
inline void body_for(Link l, const char *sta_ssid, const char *ap_ssid, char *out, size_t n) {
  if (n == 0) return;
  if (l == AP_UP)
    std::snprintf(out, n, "Join '%s'\nfrom your phone", ap_ssid ? ap_ssid : "");
  else if (sta_ssid && sta_ssid[0])
    std::snprintf(out, n, "%s", sta_ssid);
  else
    std::snprintf(out, n, "Waiting for Wi-Fi details");
}

}  // namespace boot
}  // namespace f1
