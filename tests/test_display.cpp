// Backlight behaviour: auto-dim and Auto Off (UI-44, UI-44c).
// A feature that only acts in the middle of the night cannot be debugged by
// watching, so the schedule arithmetic is pinned here.
#include <cstdarg>
#include <cstdio>
#include <initializer_list>

#include "../f1-tracker/f1_display.h"

static int checks = 0, failures = 0;
static void okf(bool c, const char *fmt, ...) {
  checks++;
  if (!c) {
    failures++;
    va_list ap; va_start(ap, fmt);
    std::printf("  FAIL  "); std::vprintf(fmt, ap); std::printf("\n"); va_end(ap);
  }
}

using namespace f1::display;

int main() {
  std::printf("\nF1 Tracker - display tests\n\n");

  std::printf("auto-dim curve (UI-44)\n");
  okf(dim_factor(40.0f, true) == 1.0f, "full brightness in daylight");
  okf(dim_factor(5.0f, true) > 0.9f, "still near full just above the horizon");
  okf(dim_factor(-8.0f, true) == 0.25f, "25%% once the Sun is 8 deg down");
  okf(dim_factor(-30.0f, true) == 0.25f, "never below 25%%");
  okf(dim_factor(-30.0f, false) == 1.0f, "an unsynced clock must not dim");
  // Monotonic: the panel must never get brighter as the Sun sets.
  float prev = 2.0f;
  for (float el = 40.0f; el >= -40.0f; el -= 0.5f) {
    const float f = dim_factor(el, true);
    okf(f <= prev + 1e-6f, "brightness rose at elevation %.1f", el);
    prev = f;
  }

  std::printf("the off window wraps midnight (UI-44c)\n");
  {
    OffWindow w{true, 23, 7};
    for (int h : {23, 0, 1, 3, 6}) okf(in_window(w, h), "%02d:00 should be inside 23-07", h);
    for (int h : {7, 8, 12, 18, 22}) okf(!in_window(w, h), "%02d:00 should be outside 23-07", h);
  }
  {
    // A window that does not wrap must still work.
    OffWindow w{true, 1, 5};
    for (int h : {1, 2, 4}) okf(in_window(w, h), "%02d:00 inside 01-05", h);
    for (int h : {0, 5, 6, 23}) okf(!in_window(w, h), "%02d:00 outside 01-05", h);
  }
  {
    OffWindow off{false, 23, 7};
    for (int h = 0; h < 24; h++) okf(!in_window(off, h), "disabled must never be in window");
    OffWindow zero{true, 4, 4};
    for (int h = 0; h < 24; h++)
      okf(!in_window(zero, h), "a zero-length window must never fire (%02d)", h);
    OffWindow w{true, 23, 7};
    okf(!in_window(w, -1) && !in_window(w, 24), "an impossible hour is not in any window");
  }

  std::printf("touch wakes the panel, then it goes back\n");
  {
    State s;
    s.window = OffWindow{true, 23, 7};
    okf(should_be_off(s, 2, 1000), "02:00 is inside the window");
    touch(s, 1000);
    okf(!should_be_off(s, 2, 1000), "a touch wakes it");
    okf(!should_be_off(s, 2, 1000 + WAKE_MS - 1), "still awake just before the timeout");
    okf(should_be_off(s, 2, 1000 + WAKE_MS), "back off at the timeout");
    okf(should_be_off(s, 2, 1000 + WAKE_MS + 60000), "and stays off");
    // Leaving the window clears the override, so a touch at 03:00 cannot keep
    // the panel awake into the following night.
    touch(s, 2000);
    okf(!should_be_off(s, 12, 2000), "midday is never off");
    okf(should_be_off(s, 2, 3000), "the override did not survive leaving the window");
  }

  std::printf("dim and off are independent (UI-44c)\n");
  {
    // Off is about the household being asleep, dim is about the light. Someone
    // may want either, both or neither - so one must not imply the other.
    State s;
    s.window = OffWindow{false, 23, 7};
    okf(!should_be_off(s, 2, 0), "auto-dim alone must not blank the panel");
    okf(dim_factor(-20.0f, true) > 0.0f, "a dimmed panel is still lit");
  }

  std::printf("\n%d checks, %d failures\n\n", checks, failures);
  return failures ? 1 : 0;
}
