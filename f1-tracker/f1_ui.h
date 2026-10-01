#pragma once
// LVGL drawing for the circuit card and the carousel (MAP-5, UI-20).
//
// This is the only M1 file that touches LVGL; the geometry and the rotation
// live in f1_map.h and f1_carousel.h so they stay host-testable (section 10).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "f1_calendar.h"
#include "f1_carousel.h"
#include "f1_circuits.h"
#include "f1_map.h"

namespace f1 {
namespace ui {

// MAP-5: one lv_line per trace. LVGL keeps the pointer, not a copy, so the
// buffer must outlive the widget - hence a single static one, sized for the
// longest trace we ship (Paul Ricard, 203 points).
inline constexpr int MAX_PTS = 256;

struct Widgets {
  lv_obj_t *map_box = nullptr;     // container the trace is drawn inside
  lv_obj_t *line = nullptr;        // MAP-5: declared in YAML so LV_USE_LINE is
  lv_obj_t *tick = nullptr;        // compiled in; we only swap their points
  lv_obj_t *north = nullptr;
  lv_obj_t *badge = nullptr;       // UI-20b: CIRCUIT / DRIVER / LEGEND
  lv_obj_t *title = nullptr;
  lv_obj_t *body = nullptr;        // facts
  const lv_font_t *font_small = nullptr;
};

struct State {
  Widgets w;
  lv_point_precise_t pts[MAX_PTS];
  lv_point_precise_t tick_pts[2];
  lv_point_precise_t north_pts[2];
  carousel::Rotation rot;
  int interval_s = 45;
  uint32_t last_advance_ms = 0;
  bool paused = false;
  bool ready = false;
};

inline State g;

inline constexpr uint32_t COL_TRACE = 0xE8EDF7;
inline constexpr uint32_t COL_TICK  = 0xFF8A1F;
inline constexpr uint32_t COL_MUTED = 0x7E8BB3;

// Draw circuit c into the map box. Safe to call repeatedly: the widgets are
// created once and only their point arrays change.
inline void draw_circuit(const circuits::Circuit &c) {
  if (g.w.map_box == nullptr) return;
  const int bw = lv_obj_get_width(g.w.map_box);
  const int bh = lv_obj_get_height(g.w.map_box);
  if (bw <= 0 || bh <= 0) return;

  const auto fit = map::fit_box(c, bw, bh);
  const int n = c.n_pts > MAX_PTS - 1 ? MAX_PTS - 1 : c.n_pts;
  for (int i = 0; i < n; i++) {
    float x, y;
    map::point_at(c, i, fit, x, y);
    g.pts[i].x = (lv_value_precise_t) lroundf(x);
    g.pts[i].y = (lv_value_precise_t) lroundf(y);
  }
  int count = n;
  // MAP-5: close the lap explicitly. The GeoJSON traces do not repeat the
  // first point, so without this every circuit has a visible gap at the line.
  if (c.closed && count < MAX_PTS) { g.pts[count] = g.pts[0]; count++; }

  if (g.w.line == nullptr) return;
  lv_line_set_points(g.w.line, g.pts, count);

  // start/finish tick, perpendicular to the track at point 0
  float x0, y0, x1, y1;
  map::start_tick(c, fit, 16.0f, x0, y0, x1, y1);
  g.tick_pts[0].x = (lv_value_precise_t) lroundf(x0);
  g.tick_pts[0].y = (lv_value_precise_t) lroundf(y0);
  g.tick_pts[1].x = (lv_value_precise_t) lroundf(x1);
  g.tick_pts[1].y = (lv_value_precise_t) lroundf(y1);
  if (g.w.tick != nullptr) lv_line_set_points(g.w.tick, g.tick_pts, 2);

  // MAP-3: a rotated map with no orientation cue is a quietly wrong map, so
  // the arrow appears exactly when the generator applied a rotation.
  const bool show_north = !map::is_north_up(c);
  if (g.w.north == nullptr) return;
  if (show_north) {
    const float th = map::north_deg(c) * (float) M_PI / 180.0f;
    const float ox = bw - 22.0f, oy = 22.0f, len = 14.0f;
    g.north_pts[0].x = (lv_value_precise_t) lroundf(ox + sinf(th) * len * 0.5f);
    g.north_pts[0].y = (lv_value_precise_t) lroundf(oy + cosf(th) * len * 0.5f);
    g.north_pts[1].x = (lv_value_precise_t) lroundf(ox - sinf(th) * len * 0.5f);
    g.north_pts[1].y = (lv_value_precise_t) lroundf(oy - cosf(th) * len * 0.5f);
    lv_line_set_points(g.w.north, g.north_pts, 2);
    lv_obj_remove_flag(g.w.north, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g.w.north, LV_OBJ_FLAG_HIDDEN);
  }

  // ---- card text (section 6.4). Facts come straight from the GeoJSON
  // properties; the curated ones (turns, DRS, laps) land at M2.
  if (g.w.badge) lv_label_set_text(g.w.badge, "CIRCUIT");
  if (g.w.title) lv_label_set_text(g.w.title, c.name);
  if (g.w.body) {
    // Is this circuit on the compiled calendar? Decision 33: all 40 ship, so a
    // card for a circuit that is not racing must say so rather than looking
    // like the device invented a race.
    const circuits::Circuit *cal_c = nullptr;
    int round = 0;
    for (int i = 0; i < calendar::N_ROUNDS && cal_c == nullptr; i++) {
      const auto *m = map::by_circuit_id(calendar::R[i].circuit_id);
      if (m == &c) { cal_c = m; round = calendar::R[i].round; }
    }
    char b[320];
    int k = snprintf(b, sizeof(b), "%s\n%.3f km", c.location,
                     c.length_m / 1000.0f);
    if (c.firstgp) k += snprintf(b + k, sizeof(b) - k, "   first GP %u", c.firstgp);
    if (c.altitude_m) k += snprintf(b + k, sizeof(b) - k, "\n%d m above sea level",
                                    (int) c.altitude_m);
    if (cal_c) snprintf(b + k, sizeof(b) - k, "\nRound %d, %u", round, calendar::SEASON);
    else snprintf(b + k, sizeof(b) - k, "\nnot on the %u calendar", calendar::SEASON);
    lv_label_set_text(g.w.body, b);
  }
}

inline void show_card(const carousel::Card &c) {
  switch (c.type) {
    case carousel::CIRCUIT:
      if (c.index >= 0 && c.index < circuits::N) draw_circuit(circuits::C[c.index]);
      break;
    // DRIVER and LEGEND cards land at M2 with the generated profile tables.
    default:
      break;
  }
}

inline void advance(uint32_t now_ms, bool force = false) {
  if (!g.ready || (g.paused && !force)) return;
  if (!force && (now_ms - g.last_advance_ms) < (uint32_t) g.interval_s * 1000u) return;
  g.last_advance_ms = now_ms;
  carousel::Card c;
  if (g.rot.next(c)) show_card(c);
}

inline void set_interval(float seconds) {
  g.interval_s = carousel::clamp_interval_s(seconds);
}

inline void set_content(int idx) {
  g.rot.configure(circuits::N, 0, 0, (carousel::Content) idx);  // driver/legend lists at M2
}

inline void toggle_pause() { g.paused = !g.paused; }
inline bool paused() { return g.paused; }

// Settings-page helper: keep the slider's units beside it so the number means
// something without the user having to guess.
inline void settings_labels(lv_obj_t *lbl, int seconds) {
  if (lbl == nullptr) return;
  char b[16];
  snprintf(b, sizeof(b), "%d s", carousel::clamp_interval_s((float) seconds));
  lv_label_set_text(lbl, b);
}

inline void setup(const Widgets &w, float interval_s, int content_idx) {
  g.w = w;
  set_interval(interval_s);
  g.rot.configure(circuits::N, 0, 0, (carousel::Content) content_idx);
  // UI-20c: start at the next round so the carousel feels like it knows what
  // is coming up. Without a clock yet we start at round 1; M3 refines this.
  const auto *first = map::by_circuit_id(calendar::R[0].circuit_id);
  if (first != nullptr) {
    for (int i = 0; i < circuits::N; i++)
      if (&circuits::C[i] == first) { g.rot.set_circuit_cursor(i); break; }
  }
  g.ready = true;
  advance(0, true);
}

}  // namespace ui
}  // namespace f1
