// The boot / Wi-Fi status page (NET-2b, NET-2c, BOOT-2). The decision of when
// to leave it and when to come back is pure, so it is tested as one.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "../f1-tracker/f1_boot.h"

static int checks = 0, failures = 0;
static void okf(bool cond, const char *fmt, ...) {
  checks++;
  if (!cond) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

using namespace f1;
using namespace f1::boot;

static void test_leaving() {
  std::printf("leaving the status page\n");
  Tracker t;
  // The boot page is showing, Wi-Fi is still coming up.
  Step s = step(t, CONNECTING, 1000, true, false, state::IDLE);
  okf(s.page == STAY && s.link_changed, "connecting: stay, and the text needs writing");
  s = step(t, CONNECTING, 3000, true, false, state::IDLE);
  okf(s.page == STAY && !s.link_changed, "still connecting: nothing to rewrite");

  s = step(t, CONNECTED, 5000, true, false, state::IDLE);
  okf(s.page == TO_CIRCUIT, "connected in IDLE: fade to the carousel (UI-2a)");

  Tracker t2;
  s = step(t2, CONNECTED, 5000, true, false, state::SESSION_SOON);
  okf(s.page == TO_RACE, "connected with a session near: the race page");
  for (state::Weekend w : {state::SESSION_LIVE, state::RACE_LIVE, state::POST_SESSION}) {
    Tracker t3;
    okf(step(t3, CONNECTED, 1, true, false, w).page == TO_RACE, "%s is a race-page state",
        state::weekend_name(w));
  }
  for (state::Weekend w : {state::IDLE, state::RACE_WEEK, state::OFF_SEASON}) {
    Tracker t3;
    okf(step(t3, CONNECTED, 1, true, false, w).page == TO_CIRCUIT, "%s lands on the carousel",
        state::weekend_name(w));
  }

  // Already off the status page and connected: never yanked anywhere.
  Tracker t4;
  okf(step(t4, CONNECTED, 9000, false, false, state::IDLE).page == STAY,
      "connected and already on a real page: stay put");
  // ...including the settings page, which is simply "not the status page".
  okf(step(t4, CONNECTED, 11000, false, true, state::IDLE).page == STAY, "settings stay open");
}

static void test_returning() {
  std::printf("coming back after 10 s offline\n");
  Tracker t;
  step(t, CONNECTED, 1000, false, false, state::IDLE);
  Step s = step(t, CONNECTING, 20000, false, false, state::IDLE);   // lost at 20 s
  okf(s.page == STAY, "just lost: not yet");
  s = step(t, CONNECTING, 29000, false, false, state::IDLE);
  okf(s.page == STAY, "9 s offline: not yet");
  s = step(t, CONNECTING, 30001, false, false, state::IDLE);
  okf(s.page == TO_STATUS, "over 10 s offline: the status page returns");
  s = step(t, CONNECTING, 32000, true, false, state::IDLE);
  okf(s.page == STAY, "already showing it: no repeat");

  // NET-2b: never over the settings page.
  Tracker t2;
  step(t2, CONNECTING, 1000, false, true, state::IDLE);
  s = step(t2, CONNECTING, 60000, false, true, state::IDLE);
  okf(s.page == STAY, "a minute offline with settings open: the form is not thrown away");
  s = step(t2, CONNECTING, 62000, false, false, state::IDLE);
  okf(s.page == TO_STATUS, "the moment settings close, it comes back");

  // A blip shorter than the grace period resets the clock.
  Tracker t3;
  step(t3, CONNECTING, 1000, false, false, state::IDLE);
  step(t3, CONNECTED, 8000, false, false, state::IDLE);
  okf(t3.lost_since == 0, "reconnecting clears the offline clock");
  s = step(t3, CONNECTING, 9000, false, false, state::IDLE);
  s = step(t3, CONNECTING, 15000, false, false, state::IDLE);
  okf(s.page == STAY, "a fresh blip is timed from ITS start, not the old one");

  // millis() wraps at ~49.7 days, which a device on a wall will cross.
  Tracker w;
  step(w, CONNECTING, 0xFFFFF000u, false, false, state::IDLE);
  s = step(w, CONNECTING, 0x00000200u, false, false, state::IDLE);   // ~0.5 s later, wrapped
  okf(s.page == STAY, "a wrapped counter does not read as a long outage");
  s = step(w, CONNECTING, 0x00003000u, false, false, state::IDLE);   // ~12 s later
  okf(s.page == TO_STATUS, "...and 12 s across the wrap is a real outage");

  // lost_since can never be 0 while offline, even at millis() == 0.
  Tracker z;
  step(z, CONNECTING, 0, false, false, state::IDLE);
  okf(z.lost_since != 0, "an outage that begins at t=0 is still recorded");
}

static void test_text() {
  std::printf("the words\n");
  okf(classify(true, false) == CONNECTED && classify(true, true) == CONNECTED,
      "connected wins even with the AP still up");
  okf(classify(false, false) == CONNECTING && classify(false, true) == AP_UP, "classify");
  okf(std::strcmp(title_for(CONNECTING), "Connecting to Wi-Fi") == 0, "connecting title");
  okf(std::strcmp(title_for(AP_UP), "Set up Wi-Fi") == 0, "AP title");

  char b[96];
  body_for(CONNECTING, "home-net", "F1 Tracker - 9CAD", b, sizeof(b));
  okf(std::strcmp(b, "home-net") == 0, "the network being tried (NET-2c)");
  body_for(CONNECTING, "", "F1 Tracker - 9CAD", b, sizeof(b));
  okf(std::strcmp(b, "Waiting for Wi-Fi details") == 0, "no credentials yet says what it waits for");
  body_for(CONNECTING, nullptr, nullptr, b, sizeof(b));
  okf(std::strcmp(b, "Waiting for Wi-Fi details") == 0, "a null name is not a crash");
  body_for(AP_UP, "home-net", "F1 Tracker - 9CAD", b, sizeof(b));
  okf(std::strcmp(b, "Join 'F1 Tracker - 9CAD'\nfrom your phone") == 0, "the AP instruction names the AP");
  char tiny[8];
  body_for(AP_UP, "", "F1 Tracker - 9CAD", tiny, sizeof(tiny));
  okf(std::strlen(tiny) < sizeof(tiny), "a short buffer is truncated, not overrun");

  // Changes of link are reported once, so the labels are written once.
  Tracker t;
  okf(step(t, CONNECTING, 1, true, false, state::IDLE).link_changed, "first sighting");
  okf(!step(t, CONNECTING, 2001, true, false, state::IDLE).link_changed, "unchanged");
  okf(step(t, AP_UP, 4001, true, false, state::IDLE).link_changed, "AP came up");
}

int main() {
  std::printf("\nF1 Tracker - boot / Wi-Fi page\n\n");
  test_leaving();
  test_returning();
  test_text();
  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
