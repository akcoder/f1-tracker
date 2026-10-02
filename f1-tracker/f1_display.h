#pragma once
// Backlight behaviour (UI-44): auto-dim, and Auto Off.
//
// Free of LVGL so the schedule arithmetic is host-testable - a feature that
// only acts in the middle of the night is one you cannot debug by watching.
#include <cmath>
#include <cstdint>

namespace f1 {
namespace display {

// UI-44: the stored brightness is the DAYTIME level, scaled down as the Sun
// sets - to 25 % of it once the Sun is 8 deg below the horizon. The user's
// slider sets the daytime level while auto is on, not the current level.
inline float dim_factor(float sun_elevation_deg, bool clock_valid) {
  if (!clock_valid) return 1.0f;
  const float f = 0.25f + 0.75f * (sun_elevation_deg + 8.0f) / 13.0f;
  return f < 0.25f ? 0.25f : (f > 1.0f ? 1.0f : f);
}

// UI-44c: Auto Off. Dimming helps; at 61.58 N in December the panel is still
// lit for nineteen hours of darkness, and a display nobody is looking at is
// just a light. Auto Off blanks it entirely between two local hours and wakes
// it on a touch.
//
// Separate from auto-dim on purpose: dim is about the ambient light, off is
// about the household being asleep. Someone may want either, both or neither.
struct OffWindow {
  bool enabled = false;
  uint8_t from_hour = 23;   // inclusive
  uint8_t to_hour = 7;      // exclusive
};

// Wraps midnight, which is the normal case - 23:00 to 07:00 is not "hours 23
// through 7" on a number line.
inline bool in_window(const OffWindow &w, int hour) {
  if (!w.enabled || hour < 0 || hour > 23) return false;
  if (w.from_hour == w.to_hour) return false;        // a zero-length window is off
  if (w.from_hour < w.to_hour) return hour >= w.from_hour && hour < w.to_hour;
  return hour >= w.from_hour || hour < w.to_hour;    // wraps midnight
}

// A touch wakes the panel for this long before the window reclaims it, so
// somebody up at 3 a.m. can look at the thing without changing a setting.
inline constexpr uint32_t WAKE_MS = 60u * 1000u;

struct State {
  OffWindow window;
  uint32_t woke_at_ms = 0;
  bool awake_override = false;
};

inline void touch(State &s, uint32_t now_ms) {
  s.awake_override = true;
  s.woke_at_ms = now_ms;
}

// Should the panel be dark right now?
inline bool should_be_off(State &s, int hour, uint32_t now_ms) {
  if (!in_window(s.window, hour)) {
    s.awake_override = false;
    return false;
  }
  if (s.awake_override && (now_ms - s.woke_at_ms) < WAKE_MS) return false;
  s.awake_override = false;
  return true;
}

// The device's single display state. Lives here so the YAML interval and the
// touch handler act on the same object.
inline State g_state;

}  // namespace display
}  // namespace f1
