#pragma once
// LVGL drawing for the circuit card and the carousel (MAP-5, UI-20).
//
// This is the only M1 file that touches LVGL; the geometry and the rotation
// live in f1_map.h and f1_carousel.h so they stay host-testable (section 8).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

#include "f1_birthday.h"
#include "f1_critter.h"
#include "f1_mem.h"
#include "f1_calendar.h"
#include "f1_carousel.h"
#include "f1_circuits.h"
#include "f1_drivers.h"
#include "f1_facts.h"
#include "f1_flags.h"
#include "f1_legends.h"
#include "f1_map.h"
#include "f1_photo.h"
#include "f1_portraits.h"
#include "f1_roster.h"

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
  lv_obj_t *north_n = nullptr;     // the "N" under the arrow (decision 202: a bare line read as a fragment)
  lv_obj_t *race_box = nullptr;    // decision 202: the race page's map, its own lines
  lv_obj_t *race_line = nullptr;
  lv_obj_t *race_tick = nullptr;
  lv_obj_t *race_north = nullptr;
  lv_obj_t *race_north_n = nullptr;
  lv_obj_t *badge = nullptr;       // UI-20b: Circuit / Driver / Legend
  lv_obj_t *title = nullptr;
  lv_obj_t *body = nullptr;        // facts
  const lv_font_t *font_small = nullptr;
};

struct State {
  Widgets w;
  lv_point_precise_t pts[MAX_PTS];
  lv_point_precise_t tick_pts[2];
  lv_point_precise_t north_pts[5];    // shaft and two barbs
  lv_point_precise_t rpts[MAX_PTS];   // the race page's trace: both maps can be live at once
  lv_point_precise_t rtick[2];
  lv_point_precise_t rnorth[5];
  carousel::Rotation rot;
  roster::View roster;             // RACE-13d: who is racing THIS season, live
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
  headshot::Slot *staged_hs = nullptr;   // its picture, being fetched (f1_headshot.h)
  char staged_headshot[8] = {0};   // a driver with no compiled portrait: fetch theirs (f1_headshot.h)
  photo::Photo card_photo{240, 320};                // the carousel card's portrait, full size
  char staged_badge[12] = {0};
  char staged_title[48] = {0};
  char staged_body[320] = {0};
  char staged_credit[64] = {0};
  const circuits::Circuit *staged_circuit = nullptr;
  const char *staged_iso3 = nullptr;
  bool staged_birthday = false;
  int bday_driver = -1;        // index into drivers::P, or -1
  int bday_age = 0;
  int bday_mmdd = 0;           // so the check runs once a day, not every tick
  // UI-20e: race-week focus. During a race week the carousel narrows to this
  // weekend's circuit and the entered drivers, instead of wandering off to
  // Kyalami and Fangio. The device should feel like it knows what is coming up.
  int content_idx = 0;             // the Carousel Content setting, kept for set_roster
  bool focus = false;
  int focus_circuit = -1;
  int body_x0 = -1, body_y0 = -1;  // the body label's YAML position (the circuit layout), read once
  // UI-99 (decision 202): circuits walk in calendar order. The rotation's circuit cursor is a
  // POSITION in circuit_order; circuit_pos maps a table index back to its position.
  int circuit_order[circuits::N] = {0};
  int circuit_pos[circuits::N] = {0};
  // UI-20c: swiping. What is on screen, so a swipe can step to its neighbour of the same kind.
  carousel::Card shown{};
  bool has_shown = false;
  uint32_t last_swipe_ms = 0;      // a swipe's release is not a tap (tap_consumed, open_circuit)
};

inline PSRAM_BSS State g;                           // 5.6 kB, in PSRAM (decision 203)

inline constexpr uint32_t COL_TRACE = 0xE8EDF7;
inline constexpr uint32_t COL_TICK  = 0xFF8A1F;
inline constexpr uint32_t COL_MUTED = 0x7E8BB3;

// Draw circuit c into the map box. Safe to call repeatedly: the widgets are
// created once and only their point arrays change.
// The trace, its start/finish tick and the north arrow, fitted into a box: the circuit
// card's (draw_circuit) or the race page's (draw_race_map). LVGL's line keeps a pointer to
// its points, so each box has arrays of its own. False when the box cannot be drawn into.
inline bool draw_trace(const circuits::Circuit &c, lv_obj_t *box, lv_obj_t *line, lv_obj_t *tick,
                       lv_obj_t *north, lv_obj_t *north_n, lv_point_precise_t *pts,
                       lv_point_precise_t *tick_pts, lv_point_precise_t *north_pts) {
  if (box == nullptr) return false;
  // The box's size is only known once its screen has been laid out, and the first card is
  // prepared before the circuit page has ever been shown: on the first board the box read
  // 0 x 0 here and the card came up with no trace at all (decision 202).
  lv_obj_update_layout(box);
  const int bw = lv_obj_get_width(box);
  const int bh = lv_obj_get_height(box);
  if (bw <= 0 || bh <= 0) return false;

  const auto fit = map::fit_box(c, bw, bh);
  const int n = c.n_pts > MAX_PTS - 1 ? MAX_PTS - 1 : c.n_pts;
  for (int i = 0; i < n; i++) {
    float x, y;
    map::point_at(c, i, fit, x, y);
    pts[i].x = (lv_value_precise_t) lroundf(x);
    pts[i].y = (lv_value_precise_t) lroundf(y);
  }
  int count = n;
  // MAP-5: close the lap explicitly. The GeoJSON traces do not repeat the
  // first point, so without this every circuit has a visible gap at the line.
  if (c.closed && count < MAX_PTS) { pts[count] = pts[0]; count++; }

  if (line == nullptr) return false;
  lv_line_set_points(line, pts, count);

  // start/finish tick, perpendicular to the track at point 0
  float x0, y0, x1, y1;
  map::start_tick(c, fit, 16.0f, x0, y0, x1, y1);
  tick_pts[0].x = (lv_value_precise_t) lroundf(x0);
  tick_pts[0].y = (lv_value_precise_t) lroundf(y0);
  tick_pts[1].x = (lv_value_precise_t) lroundf(x1);
  tick_pts[1].y = (lv_value_precise_t) lroundf(y1);
  if (tick != nullptr) lv_line_set_points(tick, tick_pts, 2);

  // MAP-3: a rotated map with no orientation cue is a quietly wrong map, so
  // the arrow appears exactly when the generator applied a rotation.
  const bool show_north = !map::is_north_up(c);
  if (north == nullptr) return true;
  if (show_north) {
    // Decision 202: an arrow with barbs and an "N" under it. The bare 14 px line it was
    // read, on the board, as "a random fragment" in the corner.
    const float th = map::north_deg(c) * (float) M_PI / 180.0f;
    const float ox = bw - 22.0f, oy = 20.0f, len = 16.0f;
    const float dx = -sinf(th), dy = -cosf(th);              // towards north on the screen
    const float px = -dy, py = dx;                           // across it
    const float hx = ox + dx * len * 0.5f, hy = oy + dy * len * 0.5f;   // the head
    const float tx = ox - dx * len * 0.5f, ty = oy - dy * len * 0.5f;   // the tail
    auto put = [&](int i, float x, float y) {
      north_pts[i].x = (lv_value_precise_t) lroundf(x);
      north_pts[i].y = (lv_value_precise_t) lroundf(y);
    };
    put(0, tx, ty);
    put(1, hx, hy);
    put(2, hx - dx * 5.0f + px * 4.0f, hy - dy * 5.0f + py * 4.0f);
    put(3, hx, hy);
    put(4, hx - dx * 5.0f - px * 4.0f, hy - dy * 5.0f - py * 4.0f);
    lv_line_set_points(north, north_pts, 5);
    lv_obj_remove_flag(north, LV_OBJ_FLAG_HIDDEN);
    if (north_n != nullptr) {
      lv_obj_set_pos(north_n, (int) lroundf(ox) - 4, (int) lroundf(oy) + 10);
      lv_obj_remove_flag(north_n, LV_OBJ_FLAG_HIDDEN);
    }
  } else {
    lv_obj_add_flag(north, LV_OBJ_FLAG_HIDDEN);
    if (north_n != nullptr) lv_obj_add_flag(north_n, LV_OBJ_FLAG_HIDDEN);
  }
  return true;
}

// Decision 202: the race page's map - the round the page is about. Redrawn only when the
// circuit changes; no circuit hides the box. The firmware had never drawn into this box
// (the renders did), so on the first board the race page had facts and no trace.
inline void draw_race_map(const circuits::Circuit *c) {
  static const circuits::Circuit *drawn = nullptr;
  if (g.w.race_box == nullptr) return;
  if (c == nullptr) {
    lv_obj_add_flag(g.w.race_box, LV_OBJ_FLAG_HIDDEN);
    drawn = nullptr;
    return;
  }
  lv_obj_remove_flag(g.w.race_box, LV_OBJ_FLAG_HIDDEN);
  if (c == drawn) return;
  if (draw_trace(*c, g.w.race_box, g.w.race_line, g.w.race_tick, g.w.race_north, g.w.race_north_n,
                 g.rpts, g.rtick, g.rnorth))
    drawn = c;
}

inline void draw_circuit(const circuits::Circuit &c) {
  if (!draw_trace(c, g.w.map_box, g.w.line, g.w.tick, g.w.north, g.w.north_n, g.pts, g.tick_pts,
                  g.north_pts))
    return;

  // ---- card text (section 4.4). Facts come straight from the GeoJSON
  // properties; the curated ones (turns, DRS, laps) land at M2.
  if (g.w.badge) lv_label_set_text(g.w.badge, "Circuit");          // UI-194: not capitals
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

// RACE-13d: answered by the LIVE roster, with the compiled table only as the
// floor before one has been fetched. It used to read the compiled table alone,
// which made a driver who retired over the winter look like they were still
// racing - suppressing their legend card while the stale driver card stayed.
inline bool currently_racing(const char *driver_id) {
  return roster::racing(g.roster, driver_id);
}

inline int age_from_dob(const char *dob, uint16_t season) {
  if (dob == nullptr || strlen(dob) < 4) return 0;
  const int y = atoi(dob);
  return (y > 1900 && season > y) ? (int) season - y : 0;
}

template <typename P>
inline void compose_profile(const P &p, bool legend_badge, uint16_t season) {
  snprintf(g.staged_badge, sizeof(g.staged_badge), "%s",
           legend_badge ? "Legend" : "Driver");
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
  if (g.staged_photo == nullptr && p.code[0]) std::snprintf(g.staged_headshot, sizeof(g.staged_headshot), "%s", p.code);
}

// 5.6.2: the curated line, appended only where one exists. Drivers have none;
// legends do.
inline void append_line(const char *line) {
  if (line == nullptr || !*line) return;
  const size_t k = strlen(g.staged_body);
  snprintf(g.staged_body + k, sizeof(g.staged_body) - k, "\n\n%s", line);
}

// Resolve a card completely into the staging buffer. Returns false when the
// card cannot be shown at all, in which case the caller skips it rather than
// displaying a blank.
inline bool prepare(const carousel::Card &c) {
  g.staged_birthday = false;
  g.staged_circuit = nullptr;
  g.staged_photo = nullptr;
  g.staged_headshot[0] = '\0';
  g.staged_iso3 = nullptr;
  g.staged_badge[0] = g.staged_title[0] = g.staged_body[0] = g.staged_credit[0] = '\0';

  switch (c.type) {
    case carousel::CIRCUIT: {
      if (c.index < 0 || c.index >= circuits::N) return false;
      // UI-20e (decision 202): race-week focus no longer rejects the other circuits - the
      // weekend's circuit is a priority card in the rotation instead, and the rest still show.
      const auto &ct = circuits::C[c.index];
      g.staged_circuit = &ct;
      snprintf(g.staged_badge, sizeof(g.staged_badge), "Circuit");
      snprintf(g.staged_title, sizeof(g.staged_title), "%s", ct.name);
      int round = 0;
      for (int i = 0; i < calendar::N_ROUNDS && !round; i++)
        if (map::by_circuit_id(calendar::R[i].circuit_id) == &ct) {
          round = calendar::R[i].round;
          g.staged_iso3 = calendar::R[i].iso3;   // decision 18: the CIRCUIT's country
        }
      // decision 202: four lines under the full-width trace, in mono14 - one of facts
      // about the place, then a 13-character label column. No blank line, no wrapped
      // continuation; the event name of the last win is on the card's title anyway.
      int k = snprintf(g.staged_body, sizeof(g.staged_body), "%s    · %.3f km",   // the gap after the name is for the flag
                       ct.location, ct.length_m / 1000.0f);
      if (ct.firstgp)
        k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k, " · first GP %u",
                      (unsigned) ct.firstgp);
      // decision 33: all 40 ship, so a circuit that is not racing must say so
      // or the device looks like it invented a race.
      if (round) k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                               " · Round %d, %u", round, (unsigned) calendar::SEASON);
      else k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                         " · not on the %u calendar", (unsigned) calendar::SEASON);

      // 5.5 / MAP-4: the facts worth reading, generated offline (decision 27).
      // Look-up is by the circuitId the calendar uses, not the trace id.
      const char *cid = nullptr;
      for (int i = 0; i < calendar::N_ROUNDS && !cid; i++)
        if (map::by_circuit_id(calendar::R[i].circuit_id) == &ct)
          cid = calendar::R[i].circuit_id;
      const facts::Fact *f = cid ? facts::find(cid) : nullptr;
      if (f == nullptr) f = facts::find(ct.id);     // historical circuits
      if (f != nullptr) {
        // DATA-11: fastest-lap data starts in 2004, so this is NOT an all-time
        // lap record for any circuit that raced before then. Say which it is.
        if (f->fl_time[0])
          k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                        "\n%-13s%s  %s %u (since %u)", "Fastest lap",
                        f->fl_time, f->fl_who, (unsigned) f->fl_year,
                        (unsigned) facts::FASTEST_LAP_FROM);
        if (f->last_year)
          k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                        "\n%-13s%s, %s %u", "Last winner",
                        f->last_driver, f->last_team, (unsigned) f->last_year);
        if (f->most_wins_n > 1)
          k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                        "\n%-13s%s (%u)", "Most wins", f->most_wins,
                        (unsigned) f->most_wins_n);
        if (f->races)
          k += snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                        " · %u races held", (unsigned) f->races);
      }
      return true;
    }
    case carousel::DRIVER: {
      const roster::SlotKind kind = roster::slot_kind(g.roster, c.index);
      if (kind == roster::SLOT_ROOKIE) {
        // RACE-13e: a driver who joined after the build has no portrait and no
        // career record. Show exactly what the roster knows rather than
        // skipping them - which for a rookie is very nearly everything.
        const auto *r = roster::live_only(g.roster, c.index - drivers::N);
        snprintf(g.staged_badge, sizeof(g.staged_badge), "Driver");
        snprintf(g.staged_title, sizeof(g.staged_title), "%s %s", r->given, r->family);
        snprintf(g.staged_body, sizeof(g.staged_body), "#%d  %s\n\nNew this season", r->number, r->code);
        g.staged_birthday = false;
        g.staged_iso3 = r->iso3[0] ? r->iso3 : nullptr;
        g.staged_photo = nullptr;
        std::snprintf(g.staged_headshot, sizeof(g.staged_headshot), "%s", r->code);
        return true;
      }
      // SLOT_RETIRED: in the compiled table but not in this season's roster, so
      // no driver card. If they are a legend, the legend card now covers them.
      if (kind != roster::SLOT_COMPILED) return false;
      const auto &p = drivers::P[c.index];
      const auto *lg = legend_row(p.driver_id);
      compose_profile(p, lg != nullptr, drivers::SOURCE_SEASON);
      // A driver's birthday takes over the badge, because it is the most
      // interesting thing true about them today.
      g.staged_birthday = (c.index == g.bday_driver);
      if (g.staged_birthday) {
        snprintf(g.staged_badge, sizeof(g.staged_badge), "Birthday");
        const size_t k = strlen(g.staged_body);
        snprintf(g.staged_body + k, sizeof(g.staged_body) - k,
                 "\n\n%s is %d today.", p.given, g.bday_age);
      }
      // A driver who is also a legend carries the legend line on their driver
      // card - one card, both facts (decision 86).
      if (lg) append_line(lg->line);
      return true;
    }
    case carousel::LEGEND: {
      if (c.index < 0 || c.index >= legends::N) return false;
      const auto &p = legends::P[c.index];
      // RACE-13d: if they are still racing, their DRIVER card covers them.
      if (currently_racing(p.driver_id)) return false;
      compose_profile(p, true, legends::SOURCE_SEASON);
      append_line(p.line);
      return true;
    }
    default:
      return false;
  }
}

// MOTION-7: what a hungry visitor could eat now: the circuit card's trace (the map box), or a person's
// portrait. Only while the carousel page is the one showing and the picture is on it.
inline bool food(critter::Food &f) {
  if (!g.has_shown || g.w.map_box == nullptr || lv_obj_get_screen(g.w.map_box) != lv_screen_active()) return false;
  lv_obj_t *box = g.shown.type == carousel::CIRCUIT ? g.w.map_box : g.w.photo;
  if (box == nullptr || lv_obj_has_flag(box, LV_OBJ_FLAG_HIDDEN)) return false;
  lv_obj_update_layout(box);
  lv_area_t a;
  lv_obj_get_coords(box, &a);
  f = critter::Food{a.x1, a.y1, a.x2 - a.x1 + 1, a.y2 - a.y1 + 1, g.shown.type == carousel::CIRCUIT};
  return f.w > 100 && f.h > 100;
}

// Swap the prepared card onto the screen. Everything it needs is already
// resolved, so this is only widget updates - no lookups, no decoding, nothing
// that could leave the card half-built.
inline void commit() {
  critter::clear_marks();          // MOTION-7: a hungry visitor's bite marks go with the card they were on
  g.shown = g.staged;              // UI-20c: what the screen shows from now on
  g.has_shown = true;
  if (g.w.badge) {
    lv_label_set_text(g.w.badge, g.staged_badge);
    // The badge carries the highlight: orange is the section colour everywhere
    // else, so a birthday card reads as different without a new visual idiom.
    lv_obj_set_style_text_color(
        g.w.badge, lv_color_hex(g.staged_birthday ? 0xFFD54A : 0xFF8A1F), 0);
  }
  if (g.w.title) {
    lv_label_set_text(g.w.title, g.staged_title);
    lv_obj_set_style_text_color(
        g.w.title, lv_color_hex(g.staged_birthday ? 0xFFD54A : 0xC9D3F2), 0);
  }
  if (g.w.body) {
    lv_label_set_text(g.w.body, g.staged_body);
    // 6.4: a circuit's text sits under its map; a person's sits beside the portrait
    // (24,56 - 240x320), or fills the width when there is none. Without this the
    // text lay across the picture.
    // The circuit layout is the YAML's own (card_body x/y), read once before the first
    // move: a hardcoded 300 here outlived the map growing to y 344, and on a tall trace
    // like Baku the text lay across it while the renders, which read the YAML, did not.
    if (g.body_y0 < 0) {
      g.body_x0 = lv_obj_get_style_x(g.w.body, LV_PART_MAIN);
      g.body_y0 = lv_obj_get_style_y(g.w.body, LV_PART_MAIN);
    }
    if (g.staged_circuit) { lv_obj_set_pos(g.w.body, g.body_x0, g.body_y0); lv_obj_set_width(g.w.body, 456); }
    else if (g.staged_photo || g.staged_hs) { lv_obj_set_pos(g.w.body, 282, 60); lv_obj_set_width(g.w.body, 190); }
    else { lv_obj_set_pos(g.w.body, 12, 60); lv_obj_set_width(g.w.body, 456); }
  }

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
      // Decision 202: the flag follows the end of the title, never a fixed x - at x 300 a
      // long circuit name ran under it on the board. Clamped so it stays on the page.
      if (g.staged_circuit) {
        // A circuit's flag sits AFTER the place name, in the gap the body leaves for it
        // (prepare writes the name, then four spaces, then the facts). The name's width is
        // its length in the font's character cell (mono).
        const lv_font_t *font = lv_obj_get_style_text_font(g.w.body, LV_PART_MAIN);
        const int cell = lv_font_get_glyph_width(font, '0', '0');
        const int x0 = g.body_x0 >= 0 ? g.body_x0 : 12;
        lv_obj_set_pos(g.w.flag, x0 + (int) strlen(g.staged_circuit->location) * cell + cell,
                       (g.body_y0 >= 0 ? g.body_y0 : 350) + 3);
      } else if (g.w.title != nullptr) {
        lv_obj_update_layout(g.w.title);
        int x = lv_obj_get_style_x(g.w.title, LV_PART_MAIN) + lv_obj_get_width(g.w.title) + 12;
        const int max_x = 480 - 12 - 32;
        if (x > max_x) x = max_x;
        lv_obj_set_pos(g.w.flag, x, 28);       // by the title's mono16 line (24 px down)
      }
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
    } else if (!is_circuit && g.staged_hs) {
      // the F1 media server's own picture: say where it is from
      lv_label_set_text(g.w.credit, "photo: formula1.com");
      lv_obj_remove_flag(g.w.credit, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g.w.credit, LV_OBJ_FLAG_HIDDEN);
    }
  }
  // RACE-13e: a person with no portrait gets a text-only card rather than
  // being skipped. The frame is hidden, not left empty.
  // The portrait appears top-down as it decodes (f1_photo.h); the previous card's
  // decode, if still running, is cancelled first. A picture that cannot be had
  // (no memory, no task) leaves a text-only card.
  if (g.w.photo) {
    g.card_photo.img = g.w.photo;
    if (!is_circuit && g.staged_photo) {
      if (!photo::start(g.card_photo, g.staged_photo)) {
        lv_obj_add_flag(g.w.credit, LV_OBJ_FLAG_HIDDEN);   // no photo, so no credit
      }
    } else if (!is_circuit && g.staged_hs) {
      // a headshot from the F1 media server: its rows appear as the download decodes
      if (!photo::show_external(g.card_photo, g.staged_hs)) lv_obj_add_flag(g.w.credit, LV_OBJ_FLAG_HIDDEN);
    } else {
      photo::stop(g.card_photo);
    }
  }
}

// A driver with no compiled portrait: ask for theirs now, during the previous card's dwell,
// so the download is under way (or done) when the card comes up. nullptr = none to show.
inline void stage_headshot() {
  g.staged_hs = nullptr;
  if (!g.staged_circuit && !g.staged_photo && g.staged_headshot[0])
    g.staged_hs = headshot::want(g.staged_headshot, lv_tick_get());
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
    if (c.type == carousel::CIRCUIT) c.index = g.circuit_order[c.index];   // position -> table index
    if (prepare(c)) { stage_headshot(); g.staged = c; g.staged_ready = true; g.staged_attempts = 0; return; }
  }
  g.staged_attempts++;
}

inline void show_card(const carousel::Card &c) {
  if (prepare(c)) { stage_headshot(); g.staged = c; commit(); }
}

inline void advance(uint32_t now_ms, bool force = false) {
  if (!g.ready) return;
  // Resolve the next card during the CURRENT one's dwell, so the switch itself
  // is only widget updates - no lookups, no decode, nothing that could leave a
  // card half-built on screen.
  prepare_next();
  // MOTION-7: a hungry visitor is eating THIS card; it stays until the visit is over, and then for a
  // whole dwell, so the bite marks are seen. (Held here, the card cannot change under the visit and put
  // the old card's bites on the new picture.)
  if (critter::eating && !force) { g.last_advance_ms = now_ms; return; }
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

// UI-20c: a swipe steps to the neighbouring card OF THE SAME KIND - left the next circuit,
// driver or legend, right the one before - wrapping round its list. The rotation's cursor
// follows, so the walk resumes from there, and the dwell starts over. A card the content
// filter or the race-week focus rejects is skipped; if none in the list can be shown,
// nothing moves. (The first cut walked the rotation instead, which read as random.)
inline void swipe(int dir, uint32_t now_ms) {
  if (!g.ready) return;
  g.last_swipe_ms = now_ms;
  if (!g.has_shown) return;
  const carousel::Card cur = g.shown;
  const int n = g.rot.count(cur.type);
  if (n <= 1) return;
  g.staged_ready = false;                        // prepare() below overwrites what was staged
  const bool circ = cur.type == carousel::CIRCUIT;   // circuits step in calendar order (UI-99)
  const int at = circ ? g.circuit_pos[cur.index] : cur.index;
  for (int step = 1; step < n; step++) {
    const int p = ((at + dir * step) % n + n) % n;
    const carousel::Card c{cur.type, circ ? g.circuit_order[p] : p};
    if (!prepare(c)) continue;
    stage_headshot();
    g.staged = c;
    commit();
    g.rot.set_cursor(cur.type, (p + 1) % n);
    g.last_advance_ms = now_ms;
    break;
  }
  prepare_next();
}

// UI-20c: a swipe up or down changes the KIND of card - circuit, driver, legend, round again -
// to the next card of that kind the rotation would have shown (owner, on the board: the tap
// order is pages and the sideways swipe stays within a kind, so a legend could only be waited
// for). Kinds the content setting excludes are skipped; the dwell starts over.
// Show the next card of `type` the rotation would have reached, now. False when the content
// setting excludes the kind or nothing of it can be shown. Also behind the web UI's
// "Portrait reveal" button.
inline bool show_kind(carousel::CardType type, uint32_t now_ms) {
  if (!g.ready) return false;
  const int n = g.rot.count(type);
  if (n <= 0 || !carousel::allows((carousel::Content) g.content_idx, type)) return false;
  g.staged_ready = false;
  const int from = g.rot.cursor(type);
  for (int step = 0; step < n; step++) {
    const int p = (from + step) % n;
    const carousel::Card c{type, type == carousel::CIRCUIT ? g.circuit_order[p] : p};
    if (!prepare(c)) continue;
    stage_headshot();
    g.staged = c;
    commit();
    g.rot.set_cursor(type, (p + 1) % n);
    g.last_advance_ms = now_ms;
    prepare_next();
    return true;
  }
  prepare_next();
  return false;
}

// UI-20f (decision 202): the carousel page is three stops in the tap order - a circuit, a
// driver, a legend - so tapping through the screens reaches all three kinds. Arriving shows
// the circuit; each tap shows the next kind; the tap after the legend leaves for the next
// page. A kind the content setting excludes, or that has nothing to show, is skipped.
inline constexpr carousel::CardType STOPS[3] = {carousel::CIRCUIT, carousel::DRIVER, carousel::LEGEND};
inline int tap_slot = 0;                       // how many of the three stops have been shown

inline bool tap_next(uint32_t now_ms) {
  while (tap_slot < 3)
    if (show_kind(STOPS[tap_slot++], now_ms)) return true;
  tap_slot = 0;
  return false;                                // done: the caller flips the page
}

inline void enter_stops(uint32_t now_ms) {
  tap_slot = 0;
  tap_next(now_ms);
}

inline void swipe_kind(int dir, uint32_t now_ms) {
  if (!g.ready) return;
  g.last_swipe_ms = now_ms;
  if (!g.has_shown) return;
  int t = (int) g.shown.type;
  for (int tries = 1; tries < carousel::N_TYPES; tries++) {
    t = ((t + dir) % carousel::N_TYPES + carousel::N_TYPES) % carousel::N_TYPES;
    if (show_kind((carousel::CardType) t, now_ms)) return;
  }
}

// Called once with the carousel page and its map box (the one clickable child, so presses on
// it never reach the page). A swipe is read from the press and the release, not from LVGL's
// gesture: on the board that gesture never fired (the GT911 reports too few points for its
// velocity test) and the release fell through to the short click that flips the page. A
// release at least SWIPE_PX left or right of its press, mostly sideways and within
// SWIPE_MS, is a swipe; LVGL sends RELEASED before SHORT_CLICKED/CLICKED, so just_swiped()
// is already true when those handlers ask.
inline constexpr int SWIPE_PX = 28;    // was 40: a short swipe read as a tap and flipped the page (owner)
inline constexpr int DRAG_PX = 10;     // moved this far, a release is a drag, not a tap - even if not a swipe
inline constexpr uint32_t SWIPE_MS = 700;
inline lv_point_t swipe_from{};
inline uint32_t swipe_press_ms = 0;

inline bool gesture_fired = false;     // LVGL's own gesture fired during this press

// LVGL's gesture (decision 202). It never fired on this panel because the GT911 reports every
// 50 ms against LVGL's 16 ms reads, so two reads in three moved 0 px and each zeroed the running
// sum (minimum velocity 3). With the velocity floor at 0 the sum accumulates over the press and
// the distance alone decides; the swipe_cb below stays as the fallback for a press it misses.
inline void gesture_cb(lv_event_t *e) {
  const lv_dir_t d = lv_indev_get_gesture_dir(lv_event_get_indev(e));
  const uint32_t now = lv_tick_get();
  ESP_LOGD("swipe", "LVGL gesture dir %d", (int) d);
  gesture_fired = true;
  if (d == LV_DIR_LEFT) swipe(1, now);
  else if (d == LV_DIR_RIGHT) swipe(-1, now);
  else if (d == LV_DIR_TOP) swipe_kind(1, now);
  else if (d == LV_DIR_BOTTOM) swipe_kind(-1, now);
}

inline void swipe_cb(lv_event_t *e) {
  lv_indev_t *in = lv_indev_active();
  if (in == nullptr) return;
  lv_point_t p;
  lv_indev_get_point(in, &p);
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
    swipe_from = p;
    swipe_press_ms = lv_tick_get();
    gesture_fired = false;
    return;
  }
  if (gesture_fired) { g.last_swipe_ms = lv_tick_get(); return; }   // already handled by LVGL's gesture
  const uint32_t now = lv_tick_get();
  const int dx = p.x - swipe_from.x, dy = p.y - swipe_from.y;
  const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  if (ax >= DRAG_PX || ay >= DRAG_PX) g.last_swipe_ms = now;   // a drag is never a tap, swipe or not
  if (now - swipe_press_ms > SWIPE_MS) return;                 // a hold, not a swipe
  if (ax >= SWIPE_PX && ax >= 2 * ay) swipe(dx < 0 ? 1 : -1, now);        // sideways: the next of this kind
  else if (ay >= SWIPE_PX && ay >= 2 * ax) swipe_kind(dy < 0 ? 1 : -1, now);   // up or down: another kind
}

inline void enable_swipe(lv_obj_t *page, lv_obj_t *map_box) {
  for (lv_indev_t *i = lv_indev_get_next(nullptr); i != nullptr; i = lv_indev_get_next(i)) {
    lv_indev_set_gesture_min_velocity(i, 0);       // never zero the sum on a stale read
    lv_indev_set_gesture_min_distance(i, SWIPE_PX); // the distance alone decides
  }
  if (page != nullptr) lv_obj_add_event_cb(page, gesture_cb, LV_EVENT_GESTURE, nullptr);
  for (lv_obj_t *o : {page, map_box}) {
    if (o == nullptr) continue;
    lv_obj_add_event_cb(o, swipe_cb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(o, swipe_cb, LV_EVENT_RELEASED, nullptr);
  }
}

// True within 600 ms of a swipe: its release is not a tap.
inline bool just_swiped() { return (uint32_t) (lv_tick_get() - g.last_swipe_ms) < 600u; }

// RACE-13d/e: a new roster arrives (or the season rolls over). Re-count the
// driver slots and drop whatever was staged, because it may now be a driver who
// is no longer racing. The content filter is kept as it was.
inline void set_roster(const roster::View &v) {
  g.roster = v;
  g.rot.configure(circuits::N, roster::driver_slots(g.roster), legends::N, (carousel::Content) g.content_idx);
  g.staged_ready = false;
  prepare_next();
}

inline void set_interval(float seconds) {
  g.interval_s = carousel::clamp_interval_s(seconds);
}

inline void set_content(int idx) {
  g.content_idx = idx;
  g.rot.configure(circuits::N, roster::driver_slots(g.roster), legends::N, (carousel::Content) idx);
  g.staged_ready = false;     // whatever was staged may no longer be admissible
  prepare_next();
}

// The rotation has one priority slot. A birthday (one day, decision 141) takes it; otherwise
// race-week focus puts this weekend's circuit in it (UI-20e, decision 202), every FOCUS_EVERY
// draws, and the walk through everything else goes on underneath.
inline constexpr int FOCUS_EVERY = 4;
inline void apply_priority() {
  if (g.bday_driver >= 0) g.rot.set_priority(carousel::DRIVER, g.bday_driver, birthday::EVERY);
  else if (g.focus && g.focus_circuit >= 0)
    g.rot.set_priority(carousel::CIRCUIT, g.circuit_pos[g.focus_circuit], FOCUS_EVERY);
  else g.rot.clear_priority();
}

// UI-20e. `circuit_idx` is the index into circuits::C for this weekend, or -1. Decision 202:
// focus no longer narrows the carousel (on the first board, in the week of the United States
// GP, it showed one circuit, no legends, and read as broken - "track details always shows
// Circuit of the Americas"); the weekend's circuit is a priority card instead, and the
// circuit walk starts from it.
inline void set_focus(bool on, int circuit_idx) {
  if (on == g.focus && circuit_idx == g.focus_circuit) return;
  g.focus = on;
  g.focus_circuit = circuit_idx;
  if (g.focus && circuit_idx >= 0) g.rot.set_circuit_cursor(g.circuit_pos[circuit_idx]);
  apply_priority();
  g.staged_ready = false;
  prepare_next();
}

// Re-evaluated once a day rather than every tick: a birthday does not change
// between seconds, and drivers::N is small but the rotation is hot.

inline void update_birthday(int year, int month, int day) {
  const int mmdd = month * 100 + day;
  if (mmdd == g.bday_mmdd) return;
  g.bday_mmdd = mmdd;
  g.bday_driver = birthday::today(month, day);
  g.bday_age = (g.bday_driver >= 0)
                   ? birthday::age_today(drivers::P[g.bday_driver].dob, year, month, day)
                   : 0;
  apply_priority();            // the birthday first, else the weekend's circuit, else none
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

// Settings tabs (7 / UI-16a): one panel visible at a time. The selected tab's
// button takes the accent colour so the current panel is never ambiguous.
inline void settings_tab(lv_obj_t *const pnl[3], lv_obj_t *const btn[3], int sel,
                         lv_obj_t *title = nullptr) {
  static const char *const NAMES[3] = {"Display", "Race", "Location"};   // sky-tracker UI-16a
  if (title != nullptr && sel >= 0 && sel < 3) lv_label_set_text(title, NAMES[sel]);
  for (int i = 0; i < 3; i++) {
    if (pnl[i] == nullptr || btn[i] == nullptr) continue;
    if (i == sel) lv_obj_remove_flag(pnl[i], LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(pnl[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(btn[i], lv_color_hex(i == sel ? 0x2D5BD0 : 0x1A2547), 0);
  }
}

// UI-99 (decision 202): the circuit walk is calendar order - the season's rounds first, then
// the circuits not on the calendar in table order. The rotation's cursor and priority for
// circuits are positions in this order, translated to table indices at the boundary
// (prepare_next, swipe, swipe_kind), so a swipe steps round by round, not through the table.
inline void build_circuit_order() {
  bool used[circuits::N] = {false};
  int n = 0;
  for (int r = 0; r < calendar::N_ROUNDS && n < circuits::N; r++) {
    const circuits::Circuit *c = map::by_circuit_id(calendar::R[r].circuit_id);
    if (c == nullptr) continue;
    const int i = (int) (c - circuits::C);
    if (i < 0 || i >= circuits::N || used[i]) continue;
    used[i] = true;
    g.circuit_order[n++] = i;
  }
  for (int i = 0; i < circuits::N && n < circuits::N; i++)
    if (!used[i]) g.circuit_order[n++] = i;
  for (int p = 0; p < circuits::N; p++) g.circuit_pos[g.circuit_order[p]] = p;
}

inline void setup(const Widgets &w, float interval_s, int content_idx) {
  g.w = w;
  g.content_idx = content_idx;
  set_interval(interval_s);
  // The counts must be real from the first frame. They were 0, 0 ("M2 fills them
  // in"), and set_focus() returns early when nothing changed - so in IDLE, the
  // state the device spends most of the year in, the carousel showed circuits
  // only until someone touched the Carousel Content setting.
  g.rot.configure(circuits::N, roster::driver_slots(g.roster), legends::N,
                  (carousel::Content) content_idx);
  // UI-20c: start at the next round so the carousel feels like it knows what
  // is coming up. Without a clock yet we start at round 1; M3 refines this.
  build_circuit_order();
  g.rot.set_circuit_cursor(0);           // position 0 is round 1 (UI-99, decision 202)
  g.ready = true;
  prepare_next();
  advance(0, true);
}

}  // namespace ui
}  // namespace f1
