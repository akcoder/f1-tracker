#pragma once
// LVGL drawing for the circuit card and the carousel (MAP-5, UI-20).
//
// This is the only M1 file that touches LVGL; the geometry and the rotation
// live in f1_map.h and f1_carousel.h so they stay host-testable (section 10).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "f1_calendar.h"
#include "f1_carousel.h"
#include "f1_circuits.h"
#include "f1_drivers.h"
#include "f1_flags.h"
#include "f1_legends.h"
#include "f1_map.h"
#include "f1_portraits.h"

namespace f1 {
namespace ui {

// MAP-5: one lv_line per trace. LVGL keeps the pointer, not a copy, so the
// buffer must outlive the widget - hence a single static one, sized for the
// longest trace we ship (Paul Ricard, 203 points).
inline constexpr int MAX_PTS = 256;

struct Widgets {
  lv_obj_t *map_box = nullptr;     // container the trace is drawn inside
  lv_obj_t *photo = nullptr;       // portrait on a driver/legend card
  lv_obj_t *flag = nullptr;        // nationality / circuit country flag
  lv_obj_t *credit = nullptr;      // photographer - MANDATORY where a photo shows
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

  // Prepare-then-commit. A card is resolved into this staging buffer during the
  // CURRENT card's dwell, and only swapped onto the screen once it is complete.
  // Nothing half-built is ever shown: no empty portrait frame that fills in a
  // moment later, no name without its flag.
  carousel::Card staged{};
  bool staged_ready = false;
  int staged_attempts = 0;
  lv_image_dsc_t flag_dsc{};
  const portraits::Portrait *staged_photo = nullptr;
  char staged_badge[12] = {0};
  char staged_title[48] = {0};
  char staged_body[320] = {0};
  char staged_credit[64] = {0};
  const circuits::Circuit *staged_circuit = nullptr;
  const char *staged_iso3 = nullptr;
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

// ---- profile cards (5.6) -------------------------------------------------
// A driver who is ALSO a legend gets one card - their driver card, badged
// LEGEND - and the overlap is resolved HERE, at runtime, against the live
// entry list rather than at build time (RACE-13d). Baking it would make a
// newly retired driver vanish from both rotations.
inline const legends::Profile *legend_row(const char *driver_id) {
  for (int i = 0; i < legends::N; i++)
    if (strcmp(legends::P[i].driver_id, driver_id) == 0) return &legends::P[i];
  return nullptr;
}

inline bool currently_racing(const char *driver_id) {
  for (int i = 0; i < drivers::N; i++)
    if (strcmp(drivers::P[i].driver_id, driver_id) == 0) return true;
  return false;
}

inline int age_from_dob(const char *dob, uint16_t season) {
  if (dob == nullptr || strlen(dob) < 4) return 0;
  const int y = atoi(dob);
  return (y > 1900 && season > y) ? (int) season - y : 0;
}

template <typename P>
inline void compose_profile(const P &p, bool legend_badge, uint16_t season) {
  snprintf(g.staged_badge, sizeof(g.staged_badge), "%s",
           legend_badge ? "LEGEND" : "DRIVER");
  snprintf(g.staged_title, sizeof(g.staged_title), "%s %s", p.given, p.family);

  char line2[64] = {0};
  // DATA-7: no code and no number for the pre-1980s drivers. The card renders
  // without either rather than printing a zero.
  if (p.number > 0 && p.code[0])
    snprintf(line2, sizeof(line2), "#%u  %s", (unsigned) p.number, p.code);
  else if (p.code[0])
    snprintf(line2, sizeof(line2), "%s", p.code);

  char eras[32];
  if (p.first_season && p.last_season)
    snprintf(eras, sizeof(eras), "%u-%u", (unsigned) p.first_season,
             (unsigned) p.last_season);
  else
    eras[0] = '\0';

  int k = 0;
  if (line2[0]) k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "%s\n", line2);
  if (eras[0]) k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "%s", eras);
  const int age = age_from_dob(p.dob, season);
  if (age) k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "   age %d", age);
  k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "\n\n%u starts   %u wins",
                (unsigned) p.starts, (unsigned) p.wins);
  // DATA-8: poles is -1 when no trustworthy figure exists. A blank is correct;
  // a zero would be a lie, and "Prost - 0 poles" is the worst thing this device
  // could print.
  if (p.poles >= 0)
    k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "   %d poles", (int) p.poles);
  if (p.titles)
    k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "\n%u world title%s",
                  (unsigned) p.titles, p.titles > 1 ? "s" : "");
  g.staged_iso3 = p.iso3[0] ? p.iso3 : nullptr;
  g.staged_photo = portraits::find(p.driver_id);
}

// Resolve a card completely into the staging buffer. Returns false when the
// card cannot be shown at all, in which case the caller skips it rather than
// displaying a blank.
inline bool prepare(const carousel::Card &c) {
  g.staged_circuit = nullptr;
  g.staged_photo = nullptr;
  g.staged_iso3 = nullptr;
  g.staged_badge[0] = g.staged_title[0] = g.staged_body[0] = g.staged_credit[0] = '\0';

  switch (c.type) {
    case carousel::CIRCUIT: {
      if (c.index < 0 || c.index >= circuits::N) return false;
      const auto &ct = circuits::C[c.index];
      g.staged_circuit = &ct;
      snprintf(g.staged_badge, sizeof(g.staged_badge), "CIRCUIT");
      snprintf(g.staged_title, sizeof(g.staged_title), "%s", ct.name);
      int round = 0;
      for (int i = 0; i < calendar::N_ROUNDS && !round; i++)
        if (map::by_circuit_id(calendar::R[i].circuit_id) == &ct) {
          round = calendar::R[i].round;
          g.staged_iso3 = calendar::R[i].iso3;   // decision 18: the CIRCUIT's country
        }
      int k = snprintf(g.staged_body, sizeof(g.staged_body), "%s\n%.3f km",
                       ct.location, ct.length_m / 1000.0f);
      if (ct.firstgp)
        k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "   first GP %u",
                      (unsigned) ct.firstgp);
      if (ct.altitude_m)
        k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "\n%d m above sea level",
                      (int) ct.altitude_m);
      // decision 33: all 40 ship, so a circuit that is not racing must say so
      // or the device looks like it invented a race.
      if (round) snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                          "\nRound %d, %u", round, (unsigned) calendar::SEASON);
      else snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                    "\nnot on the %u calendar", (unsigned) calendar::SEASON);
      return true;
    }
    case carousel::DRIVER: {
      if (c.index < 0 || c.index >= drivers::N) return false;
      const auto &p = drivers::P[c.index];
      compose_profile(p, legend_row(p.driver_id) != nullptr, drivers::SOURCE_SEASON);
      return true;
    }
    case carousel::LEGEND: {
      if (c.index < 0 || c.index >= legends::N) return false;
      const auto &p = legends::P[c.index];
      // RACE-13d: if they are still racing, their DRIVER card covers them.
      if (currently_racing(p.driver_id)) return false;
      compose_profile(p, true, legends::SOURCE_SEASON);
      return true;
    }
    default:
      return false;
  }
}

// Swap the prepared card onto the screen. Everything it needs is already
// resolved, so this is only widget updates - no lookups, no decoding, nothing
// that could leave the card half-built.
inline void commit() {
  if (g.w.badge) lv_label_set_text(g.w.badge, g.staged_badge);
  if (g.w.title) lv_label_set_text(g.w.title, g.staged_title);
  if (g.w.body) lv_label_set_text(g.w.body, g.staged_body);

  if (g.w.flag != nullptr) {
    const flags::Flag *f = g.staged_iso3 ? flags::find(g.staged_iso3) : nullptr;
    if (f != nullptr) {
      g.flag_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      g.flag_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
      g.flag_dsc.header.w = flags::CARD_W;
      g.flag_dsc.header.h = flags::CARD_H;
      g.flag_dsc.header.stride = flags::CARD_W * 2;
      g.flag_dsc.data_size = flags::CARD_W * flags::CARD_H * 2;
      g.flag_dsc.data = (const uint8_t *) f->card;
      lv_image_set_src(g.w.flag, &g.flag_dsc);
      lv_obj_remove_flag(g.w.flag, LV_OBJ_FLAG_HIDDEN);
    } else {
      // decision 20: no resolvable country means NO flag, never a placeholder
      // that could be mistaken for one.
      lv_obj_add_flag(g.w.flag, LV_OBJ_FLAG_HIDDEN);
    }
  }

  const bool is_circuit = (g.staged_circuit != nullptr);
  if (g.w.map_box) {
    if (is_circuit) { lv_obj_remove_flag(g.w.map_box, LV_OBJ_FLAG_HIDDEN); draw_circuit(*g.staged_circuit); }
    else lv_obj_add_flag(g.w.map_box, LV_OBJ_FLAG_HIDDEN);
  }

  // decision 72: the photographer's credit is MANDATORY wherever a photo shows.
  // The two are set together here so a portrait can never appear without one.
  if (g.w.credit) {
    if (!is_circuit && g.staged_photo) {
      snprintf(g.staged_credit, sizeof(g.staged_credit), "photo: %s",
               g.staged_photo->credit);
      lv_label_set_text(g.w.credit, g.staged_credit);
      lv_obj_remove_flag(g.w.credit, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g.w.credit, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // RACE-13e: a person with no portrait gets a text-only card rather than
  // being skipped. The frame is hidden, not left empty.
  if (g.w.photo) {
    if (!is_circuit && g.staged_photo) lv_obj_remove_flag(g.w.photo, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g.w.photo, LV_OBJ_FLAG_HIDDEN);
  }
}

// Resolve the NEXT card ahead of time, during the current card's dwell. Called
// every tick; it returns as soon as something is staged, so the work is spread
// rather than landing all at once on the switch.
inline void prepare_next() {
  if (!g.ready || g.staged_ready) return;
  carousel::Card c;
  // Bounded: a content filter that admits nothing must not spin forever.
  for (int tries = 0; tries < carousel::N_TYPES * 4; tries++) {
    if (!g.rot.next(c)) return;
    if (prepare(c)) { g.staged = c; g.staged_ready = true; g.staged_attempts = 0; return; }
  }
  g.staged_attempts++;
}

inline void show_card(const carousel::Card &c) {
  if (prepare(c)) commit();
}

inline void advance(uint32_t now_ms, bool force = false) {
  if (!g.ready) return;
  // Resolve the next card during the CURRENT one's dwell, so the switch itself
  // is only widget updates - no lookups, no decode, nothing that could leave a
  // card half-built on screen.
  prepare_next();
  if (g.paused && !force) return;
  if (!force && (now_ms - g.last_advance_ms) < (uint32_t) g.interval_s * 1000u) return;
  // Hold the current card rather than show an unready one. A card that is late
  // is better than a card that is empty.
  if (!g.staged_ready) return;
  g.last_advance_ms = now_ms;
  commit();
  g.staged_ready = false;
  prepare_next();          // start resolving the one after, immediately
}

inline void set_interval(float seconds) {
  g.interval_s = carousel::clamp_interval_s(seconds);
}

inline void set_content(int idx) {
  g.rot.configure(circuits::N, drivers::N, legends::N, (carousel::Content) idx);
  g.staged_ready = false;     // whatever was staged may no longer be admissible
  prepare_next();
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
  prepare_next();
  advance(0, true);
}

}  // namespace ui
}  // namespace f1
