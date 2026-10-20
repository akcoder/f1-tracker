#pragma once
// The application tick: one place where the state machine, the race page, the
// carousel and the watched driver meet.
//
// Everything on screen is a function of ONE state (section 1.1), so this is
// deliberately the only file that reads that state and writes widgets. Keeping
// it in one place is what stops the header, the top-5 strip and the order page
// disagreeing - plane-tracker decisions 47 and 57 are both that bug.
#include <cstdio>
#include <cmath>
#include <cstring>

#include "f1_calendar.h"
#include "f1_mem.h"
#include "f1_champ.h"
#include "f1_team.h"
#include "f1_detail.h"
#include "f1_net.h"
#include "f1_order.h"
#include "f1_roster.h"
#include "f1_state.h"
#include "f1_store.h"
#include "f1_ui.h"
#include "f1_watch.h"

namespace f1 {
namespace app {

struct Widgets {
  lv_obj_t *standings = nullptr;     // label on the championship page
  lv_obj_t *summary = nullptr;       // label on the post-session summary
  bool *summary_skip = nullptr;      // the summary page's `skip`: out of the page order with nothing to show
  bool *order_skip = nullptr;        // the order page's, likewise: no entries, no page
  lv_obj_t *constructors = nullptr;  // 8.1: the teams' table
  lv_obj_t *champ_line = nullptr;    // 8.1: "X can clinch it this weekend"
  lv_obj_t *progress = nullptr;      // 8.2: round N of M
  lv_obj_t *race_flag = nullptr;     // circuit country flag (6.6)
  lv_obj_t *top5 = nullptr;          // decision 49: a summary strip, not a list
  lv_obj_t *race_facts = nullptr;
  lv_obj_t *race_title = nullptr;
  lv_obj_t *race_state = nullptr;    // UI-3: the page's most important element
  lv_obj_t *race_sub = nullptr;
  lv_obj_t *race_clock = nullptr;
  lv_obj_t *order_title = nullptr;
  lv_obj_t *banner = nullptr;        // 6.14.3: the watched-driver strip
  lv_obj_t *banner_text = nullptr;
  lv_obj_t *banner_flag = nullptr;
};

struct App {
  Widgets w;
  state::Calendar cal;
  state::Status st;
  watch::Config watch_cfg;
  team::Favourite favourite;
  watch::Latch latch;
  watch::Alert alert;
  uint32_t last_eval_ms = 0;
  bool ready = false;           // setup() has run: apply_settings may touch the UI
  bool clock_valid = false;
  uint32_t now_utc = 0;
  store::Store data;            // the last published snapshot
  uint32_t data_gen = 0;        // generation we have rendered
  lv_obj_t *standings_rows = nullptr;
  lv_obj_t *standings_label = nullptr;
  lv_obj_t *st_flag[store::MAX_STANDINGS] = {nullptr};   // 8.1: a flag beside each driver
  lv_image_dsc_t st_dsc[store::MAX_STANDINGS]{};
  lv_obj_t *ct_flag[store::MAX_CONSTRUCTORS] = {nullptr};
  lv_image_dsc_t ct_dsc[store::MAX_CONSTRUCTORS]{};
  // The top-5 strip's widgets, built once in setup().
  lv_obj_t *t5_bar[5] = {nullptr};
  lv_obj_t *t5_flag[5] = {nullptr};
  lv_obj_t *t5_text[5] = {nullptr};
  lv_image_dsc_t t5_dsc[5]{};
  lv_image_dsc_t flag_dsc{};
  lv_image_dsc_t banner_dsc{};
};

inline PSRAM_BSS App g;                             // 13.7 kB, in PSRAM (decision 203)

inline void set_clock(uint32_t utc, bool valid) {
  g.now_utc = utc;
  g.clock_valid = valid;
}

// ---- the state line (UI-3 / UI-3a) --------------------------------------
// Clock-driven, NEVER lap-driven (decision 62). "RESULTS IN ~mm:ss" is the
// state that replaces live timing: the device knows exactly when the data
// becomes readable, so counting down to it is truthful and is the only thing
// that moves during a race.
inline void format_state(char *out, size_t n, char *sub, size_t subn) {
  const auto &st = g.st;
  out[0] = sub[0] = '\0';

  if (!st.clock_valid) {
    std::snprintf(out, n, "Clock not synced");
    std::snprintf(sub, subn, "waiting for time");
    return;
  }

  switch (st.weekend) {
    case state::RACE_LIVE:
    case state::SESSION_LIVE: {
      const char *lbl = st.current.valid()
                            ? state::session_label(st.current.session) : "SESSION";
      std::snprintf(out, n, "%s under way", lbl);
      if (st.to_results > 0)
        std::snprintf(sub, subn, "Results in ~%d h %02d min",
                      (int) (st.to_results / 3600), (int) ((st.to_results % 3600) / 60));
      break;
    }
    case state::POST_SESSION:
      std::snprintf(out, n, "%s", state::order_name(st.order));
      if (st.next.valid() && st.to_next > 0)
        std::snprintf(sub, subn, "Next: %s in %dd %02dh",
                      state::session_label(st.next.session),
                      (int) (st.to_next / 86400), (int) ((st.to_next % 86400) / 3600));
      break;
    case state::SESSION_SOON:
      if (st.next.valid()) {
        const char *lbl = state::session_label(st.next.session);
        if (state::is_race(st.next.session))
          std::snprintf(out, n, "Lights out in %d:%02d:%02d",
                        (int) (st.to_next / 3600), (int) ((st.to_next % 3600) / 60),
                        (int) (st.to_next % 60));
        else
          std::snprintf(out, n, "%s in %d:%02d", lbl,
                        (int) (st.to_next / 3600), (int) ((st.to_next % 3600) / 60));
        std::snprintf(sub, subn, "%s", state::order_name(st.order));
      }
      break;
    case state::RACE_WEEK:
      if (st.next.valid())
        std::snprintf(out, n, "%s in %dd %02dh",
                      state::session_label(st.next.session),
                      (int) (st.to_next / 86400), (int) ((st.to_next % 86400) / 3600));
      std::snprintf(sub, subn, "%s", state::order_name(st.order));
      break;
    case state::IDLE:
      if (st.next.valid())
        std::snprintf(out, n, "Next session in %d days", (int) (st.to_next / 86400));
      break;
    case state::OFF_SEASON:
    default:
      // RACE-13c: the months-long gap before the next calendar is published is
      // NORMAL, not a fault. It must never read as broken.
      std::snprintf(out, n, "%u season complete", (unsigned) g.cal.season);
      std::snprintf(sub, subn, "next calendar not yet published");
      break;
  }
}

// decision 49: five rows, nothing scrollable. It must not try to be the order
// page - that is what a tap-flip is for.
inline void update_top5() {
  if (g.w.top5 == nullptr) return;
  for (int i = 0; i < 5; i++) {
    if (g.t5_text[i] == nullptr) continue;
    if (i >= g.data.n_entries) {
      lv_obj_add_flag(g.t5_text[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g.t5_bar[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(g.t5_flag[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    const auto &e = g.data.entries[i];
    char b[48];
    std::snprintf(b, sizeof(b), "P%-3d %-4s %s", e.pos ? e.pos : i + 1, e.code, e.team);
    lv_label_set_text(g.t5_text[i], b);
    lv_obj_set_style_text_color(
        g.t5_text[i], lv_color_hex(e.watched ? 0xFFFFFF : 0xC9D3F2), 0);
    lv_obj_remove_flag(g.t5_text[i], LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(g.t5_bar[i], lv_color_hex(e.colour), 0);
    lv_obj_remove_flag(g.t5_bar[i], LV_OBJ_FLAG_HIDDEN);
    const flags::Flag *f = e.iso3[0] ? flags::find(e.iso3) : nullptr;
    if (f) {
      g.t5_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
      g.t5_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
      g.t5_dsc[i].header.w = flags::CARD_W;
      g.t5_dsc[i].header.h = flags::CARD_H;
      g.t5_dsc[i].header.stride = flags::CARD_W * 2;
      g.t5_dsc[i].data_size = flags::CARD_W * flags::CARD_H * 2;
      g.t5_dsc[i].data = (const uint8_t *) f->card;
      lv_image_set_src(g.t5_flag[i], &g.t5_dsc[i]);
      lv_obj_remove_flag(g.t5_flag[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g.t5_flag[i], LV_OBJ_FLAG_HIDDEN);   // decision 20
    }
  }
}

inline void update_header() {
  if (g.w.race_state == nullptr) return;
  char line[64], sub[64];
  format_state(line, sizeof(line), sub, sizeof(sub));
  lv_label_set_text(g.w.race_state, line);
  if (g.w.race_sub) lv_label_set_text(g.w.race_sub, sub);

  if (g.w.race_title) {
    char t[72];
    const int ridx = g.st.current.valid() ? g.st.current.round_idx
                                          : (g.st.next.valid() ? g.st.next.round_idx : -1);
    if (ridx >= 0 && ridx < g.cal.n)
      std::snprintf(t, sizeof(t), "R%u \u00b7 %s", g.cal.rounds[ridx].round,
                    g.cal.rounds[ridx].name);
    else
      std::snprintf(t, sizeof(t), "F1 Tracker");
    lv_label_set_text(g.w.race_title, t);
  }
  if (g.w.order_title) lv_label_set_text(g.w.order_title, state::order_name(g.st.order));

  // Decision 202: the carousel's top-right line is about the CARD, not the season. It was
  // the season progress ("Round 18 of 23 \u00b7 6 to go"), which on a legend or a circuit never
  // changed and read as stuck; that line now sits with the race page's facts (8.2).
  // A circuit: its round this season. A driver: their place in the championship. A legend:
  // the seasons they raced and their titles.
  if (g.w.progress && ui::g.has_shown) {
    const carousel::Card c = ui::g.shown;
    char b[64] = "";
    if (c.type == carousel::CIRCUIT && c.index >= 0 && c.index < circuits::N) {
      const circuits::Circuit *ct = &circuits::C[c.index];
      int round = 0;
      for (int i = 0; i < g.cal.n && !round; i++)
        if (map::by_circuit_id(g.cal.rounds[i].circuit_id) == ct) round = g.cal.rounds[i].round;
      if (round) std::snprintf(b, sizeof(b), "Round %d of %d", round, g.cal.n);
      else std::snprintf(b, sizeof(b), "Not on the %u calendar", (unsigned) g.cal.season);
    } else if (c.type == carousel::DRIVER && c.index >= 0 && c.index < drivers::N) {
      const char *did = drivers::P[c.index].driver_id;
      for (int i = 0; i < g.data.n_standings && !b[0]; i++)
        if (std::strcmp(g.data.standings[i].driver_id, did) == 0)
          std::snprintf(b, sizeof(b), "Championship P%d \u00b7 %d pts", g.data.standings[i].pos,
                        g.data.standings[i].points);   // owner asked what "P26" meant: say so
      if (!b[0]) std::snprintf(b, sizeof(b), "%u season", (unsigned) g.cal.season);
    } else if (c.type == carousel::DRIVER) {
      std::snprintf(b, sizeof(b), "%u season", (unsigned) g.cal.season);   // a rookie: no record yet
    } else if (c.type == carousel::LEGEND && c.index >= 0 && c.index < legends::N) {
      const auto &p = legends::P[c.index];
      if (p.titles > 0)
        std::snprintf(b, sizeof(b), "%u-%u \u00b7 %u title%s", (unsigned) p.first_season,
                      (unsigned) p.last_season, (unsigned) p.titles, p.titles == 1 ? "" : "s");
      else
        std::snprintf(b, sizeof(b), "%u-%u", (unsigned) p.first_season, (unsigned) p.last_season);
    }
    if (std::strcmp(lv_label_get_text(g.w.progress), b) != 0) lv_label_set_text(g.w.progress, b);
  }

  // The circuit's country flag (6.6). Keyed on Circuit.Location.country via the
  // calendar's iso3, NEVER on the race name (decision 18).
  const int ridx2 = g.st.current.valid() ? g.st.current.round_idx
                                         : (g.st.next.valid() ? g.st.next.round_idx : -1);
  if (g.w.race_flag) {
    const flags::Flag *f = (ridx2 >= 0 && ridx2 < g.cal.n)
                               ? flags::find(g.cal.rounds[ridx2].iso3) : nullptr;
    if (f) {
      g.flag_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      g.flag_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
      g.flag_dsc.header.w = flags::CARD_W;
      g.flag_dsc.header.h = flags::CARD_H;
      g.flag_dsc.header.stride = flags::CARD_W * 2;
      g.flag_dsc.data_size = flags::CARD_W * flags::CARD_H * 2;
      g.flag_dsc.data = (const uint8_t *) f->card;
      lv_image_set_src(g.w.race_flag, &g.flag_dsc);
      lv_obj_remove_flag(g.w.race_flag, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g.w.race_flag, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // The short facts; the full set lives on the circuit card (6.2).
  if (g.w.race_facts && ridx2 >= 0 && ridx2 < g.cal.n) {
    const auto *c = map::by_circuit_id(g.cal.rounds[ridx2].circuit_id);
    ui::draw_race_map(c);                    // decision 202: the trace beside the facts; null hides it
    const facts::Fact *f = facts::find(g.cal.rounds[ridx2].circuit_id);
    char b[160] = "";      // empty when a venue is in neither table: never an uninitialised label
    int k = 0;
    if (c) k += std::snprintf(b + k, sizeof(b) - k, "%.3f km\n", c->length_m / 1000.0f);
    if (f && f->fl_time[0])
      k += std::snprintf(b + k, sizeof(b) - k, "FL %s\n", f->fl_time);
    if (f && f->last_year)
      k += std::snprintf(b + k, sizeof(b) - k, "last %s\n", f->last_driver);
    // 8.2: season progress, "how far through are we" - here with the round's facts
    // (decision 202; it was the carousel's top-right line, where it never changed).
    int done = 0;
    for (int i = 0; i < g.cal.n; i++) {
      const uint32_t race = g.cal.rounds[i].start[calendar::RACE];
      if (race && race < g.now_utc) done++;
    }
    if (g.cal.n > 0 && k < (int) sizeof(b) - 1)
      std::snprintf(b + k, sizeof(b) - (size_t) k, "%d of %d to go", g.cal.n - done, g.cal.n);
    lv_label_set_text(g.w.race_facts, b);
  }
}

// ---- the watched-driver banner (4.14.3) ---------------------------------
inline void update_banner(uint32_t now_ms) {
  if (g.w.banner == nullptr) return;
  if (g.alert.active) {
    if (g.w.banner_text) lv_label_set_text(g.w.banner_text, g.alert.line);
    // The watched driver's flag, at double card size so it reads at a glance.
    if (g.w.banner_flag) {
      const flags::Flag *f = flags::find(g.watch_cfg.iso3);
      if (f) {
        // Pre-scaled 3x (48x36) into a buffer of our own and drawn 1:1: LVGL's scaled RGB565
        // draws nothing on this board and logs "lv_draw_sw_transform: Color format 0x12 is not
        // enabled" (decision 202; the banner flag was the one scaled image left).
        static PSRAM_BSS uint16_t big[flags::CARD_W * 3 * flags::CARD_H * 3];
        const uint16_t *src = (const uint16_t *) f->card;
        for (int y = 0; y < flags::CARD_H * 3; y++)
          for (int x = 0; x < flags::CARD_W * 3; x++)
            big[y * flags::CARD_W * 3 + x] = src[(y / 3) * flags::CARD_W + x / 3];
        g.banner_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        g.banner_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        g.banner_dsc.header.w = flags::CARD_W * 3;
        g.banner_dsc.header.h = flags::CARD_H * 3;
        g.banner_dsc.header.stride = flags::CARD_W * 3 * 2;
        g.banner_dsc.data_size = sizeof(big);
        g.banner_dsc.data = (const uint8_t *) big;
        lv_image_set_src(g.w.banner_flag, &g.banner_dsc);
        lv_image_set_scale(g.w.banner_flag, 256);   // 1:1
        lv_obj_remove_flag(g.w.banner_flag, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(g.w.banner_flag, LV_OBJ_FLAG_HIDDEN);   // decision 20
      }
    }
    // decision 92: loud. A milestone gets a taller strip, an event a shorter
    // one - but NEITHER covers the circuit trace. The map is the thing a reader
    // is actually looking at; the licence to be loud was about there being no
    // live data underneath, not about covering the subject.
    // Both sit below the trace (70-310) with clear black between, and both end
    // above the gear at 432 so settings stay reachable without waiting it out.
    // One size for both (decision 202; owner: "banner height for alert and takeover are
    // different"): the milestone's louder look is its timing and text, not a taller strip.
    lv_obj_set_height(g.w.banner, 92);
    lv_obj_set_y(g.w.banner, 330);
    // The strip would otherwise sit half-hidden behind it. A banner that
    // REPLACES the summary has nothing to crowd.
    if (g.w.top5) lv_obj_add_flag(g.w.top5, LV_OBJ_FLAG_HIDDEN);
    if (g.w.race_facts) lv_obj_add_flag(g.w.race_facts, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(g.w.banner, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g.w.banner, LV_OBJ_FLAG_HIDDEN);
    if (g.w.top5) lv_obj_remove_flag(g.w.top5, LV_OBJ_FLAG_HIDDEN);
    if (g.w.race_facts) lv_obj_remove_flag(g.w.race_facts, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void dismiss_banner() { watch::dismiss(g.alert); update_banner(0); }

// Decision 202: the web UI's Animations buttons - the two alert looks on demand, with a
// made-up line, so they can be seen without waiting for a race.
inline void demo_alert(bool milestone, uint32_t now_ms) {
  g.alert.event = milestone ? watch::EV_MILESTONE : watch::EV_RESULT;
  g.alert.tier = milestone ? watch::MILESTONE : watch::EVENT;
  std::snprintf(g.alert.line, sizeof(g.alert.line), "%s",
                milestone ? "Max takes his 80th win" : "Max wins");
  g.alert.raised_ms = now_ms;
  g.alert.active = true;
  update_banner(now_ms);
}

// 6.1: with a card open, a tap ANYWHERE closes it rather than acting. Returns
// true when the tap was consumed, so callers do nothing else.
inline bool tap_consumed() {
  if (ui::just_swiped()) return true;        // UI-20c: a swipe's release is not a page flip
  if (!detail::is_open()) return false;
  detail::close();
  return true;
}

// 6.11 / decision 53: the whole row is the tap target. Opens the driver card
// for the row at `idx` of whatever the order currently holds.
inline void open_entry(const store::Entry &e) {
  const drivers::Profile *prof = nullptr;
  for (int i = 0; i < drivers::N; i++)
    if (e.code[0] && strcmp(drivers::P[i].code, e.code) == 0) { prof = &drivers::P[i]; break; }
  const legends::Profile *leg = nullptr;
  if (prof)
    for (int i = 0; i < legends::N; i++)
      if (strcmp(legends::P[i].driver_id, prof->driver_id) == 0) { leg = &legends::P[i]; break; }
  char title[48], body[480];
  detail::compose_driver(e, prof, leg, g.data.entries_mode, title, sizeof(title),
                         body, sizeof(body));
  const portraits::Portrait *pic = prof ? portraits::find(prof->driver_id) : nullptr;
  // No compiled portrait (a rookie, or a driver new this season): ask for the F1 media
  // server's headshot, which appears in the card as it downloads (decision 193).
  headshot::Slot *hs = (pic == nullptr && e.code[0]) ? headshot::want(e.code, lv_tick_get()) : nullptr;
  detail::show(title, body, prof ? prof->iso3 : e.iso3, pic, hs);
}

inline void open_driver(int idx) {
  if (idx < 0 || idx >= g.data.n_entries) return;
  open_entry(g.data.entries[idx]);
}

// A championship row: the same card, with the standing in place of a grid slot.
inline void open_standing(int idx) {
  if (idx < 0 || idx >= g.data.n_standings) return;
  const auto &s = g.data.standings[idx];
  store::Entry e;
  e.pos = 0;
  std::snprintf(e.code, sizeof(e.code), "%s", s.code);
  std::snprintf(e.name, sizeof(e.name), "%s", s.name);
  std::snprintf(e.team, sizeof(e.team), "%s", s.team);
  std::snprintf(e.iso3, sizeof(e.iso3), "%s", s.iso3);
  open_entry(e);
}

// The standings label is the tap target; the row is worked out from the touch point
// on the same line pitch place_flags uses. Added once, on first data.
inline void wire_standings_tap() {
  static bool done = false;
  lv_obj_t *l = g.w.standings;
  if (done || l == nullptr) return;
  done = true;
  lv_obj_add_flag(l, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(l, [](lv_event_t *) {
    lv_indev_t *in = lv_indev_active();
    if (in == nullptr || g.w.standings == nullptr) return;
    lv_point_t p;
    lv_indev_get_point(in, &p);
    const lv_font_t *font = lv_obj_get_style_text_font(g.w.standings, LV_PART_MAIN);
    const int pitch = lv_font_get_line_height(font) +
                      lv_obj_get_style_text_line_space(g.w.standings, LV_PART_MAIN);
    lv_area_t a;
    lv_obj_get_coords(g.w.standings, &a);
    if (pitch <= 0 || p.y < a.y1) return;
    open_standing((p.y - a.y1) / pitch);
  }, LV_EVENT_SHORT_CLICKED, nullptr);
}

// Tapping the map opens the circuit card (6.11).
// The circuit ON THE CARD (decision 202). It used to open the next round's circuit whatever
// card was tapped, so every circuit's detail said Circuit of the Americas in the week of
// the United States GP ("circuit details not accurate, always shows Austin").
inline void open_circuit() {
  if (ui::just_swiped()) return;             // UI-20c: a swipe that began on the map is not a tap
  const circuits::Circuit *c = nullptr;
  if (ui::g.has_shown && ui::g.shown.type == carousel::CIRCUIT &&
      ui::g.shown.index >= 0 && ui::g.shown.index < circuits::N)
    c = &circuits::C[ui::g.shown.index];
  if (c == nullptr) return;
  // Its round this season, if it has one: the detail then carries the date and the flag.
  const calendar::Round *r = nullptr;
  for (int i = 0; i < g.cal.n && r == nullptr; i++)
    if (map::by_circuit_id(g.cal.rounds[i].circuit_id) == c) r = &g.cal.rounds[i];
  char title[48], body[480];
  detail::compose_circuit(*c, r, title, sizeof(title), body, sizeof(body));
  detail::show(title, body, r ? r->iso3 : nullptr);
}

// ---- the tick -----------------------------------------------------------
// Pull a new snapshot only when the data task says the generation moved. A
// fetch must never block rendering, and the lock is held for a memcpy only.
// 8.1: a flag beside each row of a championship table (drivers, constructors). The
// images are made once, as children of the label's page, and placed on the label's own
// line pitch so they follow the text. `iso3[i]` null or unknown = no flag (decision 20:
// none beats a wrong one). The flag sits `col` characters in.
inline void place_flags(lv_obj_t *lbl, lv_obj_t **img, lv_image_dsc_t *dsc, const char *const *iso3,
                        int n, int cap, int col) {
  if (lbl == nullptr) return;
  lv_obj_t *page = lv_obj_get_parent(lbl);
  const lv_font_t *font = lv_obj_get_style_text_font(lbl, LV_PART_MAIN);
  const int pitch = lv_font_get_line_height(font) + lv_obj_get_style_text_line_space(lbl, LV_PART_MAIN);
  // The label's position as the YAML set it (its style x/y), not lv_obj_get_x/y: those read
  // the laid-out coords, which on the first board were still 0 when the first standings
  // arrived - the page had never been shown - so every flag sat over the header and the
  // position column (decision 202).
  const int lx = lv_obj_get_style_x(lbl, LV_PART_MAIN), ly = lv_obj_get_style_y(lbl, LV_PART_MAIN);
  const int x = lx + col * lv_font_get_glyph_width(font, '0', '0');
  for (int i = 0; i < cap; i++) {
    const flags::Flag *f = (i < n && iso3[i] && iso3[i][0]) ? flags::find(iso3[i]) : nullptr;
    if (f == nullptr) {
      if (img[i] != nullptr) lv_obj_add_flag(img[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    if (img[i] == nullptr) {
      img[i] = lv_image_create(page);
      lv_obj_remove_flag(img[i], LV_OBJ_FLAG_CLICKABLE);
    }
    auto &d = dsc[i];
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_RGB565;
    d.header.w = flags::CARD_W;
    d.header.h = flags::CARD_H;
    d.header.stride = flags::CARD_W * 2;
    d.data_size = flags::CARD_W * flags::CARD_H * 2;
    d.data = (const uint8_t *) f->card;
    lv_image_set_src(img[i], &d);
    lv_obj_set_pos(img[i], x, ly + i * pitch + (pitch - flags::CARD_H) / 2);
    lv_obj_remove_flag(img[i], LV_OBJ_FLAG_HIDDEN);
  }
}

// Drivers: the flag comes from the compiled driver table by driverId (DATA-6).
inline void update_standing_flags() {
  const char *iso[store::MAX_STANDINGS] = {nullptr};
  for (int i = 0; i < g.data.n_standings && i < store::MAX_STANDINGS; i++) {
    const char *id = g.data.standings[i].driver_id;
    for (int k = 0; k < drivers::N && id[0]; k++)
      if (std::strcmp(drivers::P[k].driver_id, id) == 0) { iso[i] = drivers::P[k].iso3; break; }
  }
  place_flags(g.w.standings, g.st_flag, g.st_dsc, iso, g.data.n_standings, store::MAX_STANDINGS, 3);
}

// Constructors: the team's nationality flag, from the demonym Jolpica gives (store.h).
inline void update_constructor_flags() {
  const char *iso[store::MAX_CONSTRUCTORS] = {nullptr};
  for (int i = 0; i < g.data.n_constructors && i < store::MAX_CONSTRUCTORS; i++)
    iso[i] = g.data.constructors[i].iso3;
  place_flags(g.w.constructors, g.ct_flag, g.ct_dsc, iso, g.data.n_constructors,
              store::MAX_CONSTRUCTORS, 3);
}

inline void refresh_data() {
  const uint32_t gen = net::snapshot(g.data);
  // The snapshot copies the whole store EVERY tick, which wipes the iso3 resolved below (the
  // parse leaves it empty), so the calendar is resolved every tick, not only when a new
  // generation arrives: gated, the race page's flag showed for one tick and then vanished.
  // DATA-3: a fetched calendar supersedes the compiled floor. The ISO3 for the
  // flag is resolved against the compiled table, because the feed gives a
  // country NAME and the flag is keyed on a code.
  if (g.data.have_calendar && g.data.n_rounds > 0) {
    for (int i = 0; i < g.data.n_rounds; i++) {
      g.data.rounds[i].iso3 = "";
      for (int k = 0; k < calendar::N_ROUNDS; k++)
        if (std::strcmp(g.data.rounds[i].circuit_id, calendar::R[k].circuit_id) == 0) {
          g.data.rounds[i].iso3 = calendar::R[k].iso3;
          break;
        }
    }
    g.cal.rounds = g.data.rounds;
    g.cal.n = g.data.n_rounds;
    g.cal.season = g.data.season;
    g.cal.fetched = true;
  }
  if (gen == g.data_gen) return;
  g.data_gen = gen;

  // RACE-13d/e: the carousel follows who is racing THIS season, so a
  // retirement or a rookie needs no rebuild (decision 98).
  {
    static PSRAM_BSS roster::View rv;
    roster::build(g.data, rv);
    if (!roster::same(rv, ui::g.roster)) ui::set_roster(rv);
  }

  // Decision 202: an empty entry list is not a page - the order page leaves the tap-through
  // order until the list arrives, and comes back with it.
  if (g.w.order_skip) *g.w.order_skip = g.data.n_entries <= 0;

  // The watched driver's row highlight and the ambient marker (4.14.3).
  if (g.data.n_entries > 0) {
    for (int i = 0; i < g.data.n_entries; i++) {
      auto &e = g.data.entries[i];
      e.watched = g.watch_cfg.code[0] && std::strcmp(e.code, g.watch_cfg.code) == 0;
      e.favourite = team::is_favourite(g.favourite, e);
      // The flag comes from the compiled driver table, keyed on the acronym -
      // the feed's qualifying rows carry no nationality.
      if (!e.iso3[0])
        for (int k = 0; k < drivers::N; k++)
          if (std::strcmp(drivers::P[k].code, e.code) == 0) {
            std::snprintf(e.iso3, sizeof(e.iso3), "%s", drivers::P[k].iso3);
            break;
          }
    }
    order::render(g.data.entries, g.data.n_entries, g.data.entries_mode);
  }

  // 8.1: the post-session summary. Jolpica carries both halves, so this page
  // appears as soon as the results are published rather than waiting out a
  // live window.
  if (g.w.summary) {
    const auto &sm = g.data.summary;
    const bool have = sm.have_fastest || sm.have_stops;
    // Decision 202: a page that only says "waiting for a completed session" is not a page -
    // it leaves the tap-through order until there is a summary, and comes back when there is.
    if (g.w.summary_skip) *g.w.summary_skip = !have;
    if (have) {
      char b[760];
      int k = 0;
      if (sm.event[0])
        k += std::snprintf(b + k, sizeof(b) - k, "%u %s\n\n", (unsigned) sm.season,
                           sm.event);
      if (sm.have_fastest)
        k += std::snprintf(b + k, sizeof(b) - k, "%-16s %s\n%-16s %s, lap %d\n\n",
                           "Fastest lap", sm.fl_time, "", sm.fl_driver, sm.fl_lap);
      if (sm.have_stops) {
        k += std::snprintf(b + k, sizeof(b) - k, "%-16s %d\n", "Pit stops", sm.n_stops);
        if (sm.best_stop[0])
          k += std::snprintf(b + k, sizeof(b) - k, "%-16s %s, %s\n", "Quickest",
                             sm.best_stop_driver, sm.best_stop);
      }
      if (g.data.entries_mode == state::FINAL && g.data.n_entries >= 3) {
        k += std::snprintf(b + k, sizeof(b) - k, "\n%-16s", "Podium");
        for (int i = 0; i < 3 && i < g.data.n_entries; i++)
          k += std::snprintf(b + k, sizeof(b) - k, " %s", g.data.entries[i].name);
      }
      // Decision 134: OpenF1's half - tyres, weather, incidents - appended only
      // when it describes the same race as the Jolpica half above.
      store::PodiumRef pod[3];
      if (g.data.entries_mode == state::FINAL)
        for (int i = 0; i < 3 && i < g.data.n_entries; i++) {
          pod[i].number = g.data.entries[i].number;
          std::snprintf(pod[i].code, sizeof(pod[i].code), "%s", g.data.entries[i].code);
        }
      char ex[300];
      if (store::format_extras(g.data, pod, ex, sizeof(ex)) > 0 && k < (int) sizeof(b) - 2)
        std::snprintf(b + k, sizeof(b) - (size_t) k, "\n%s", ex);
      lv_label_set_text(g.w.summary, b);
    }
  }

  // 8.1: the constructors' table, beside the drivers'.
  if (g.data.n_constructors > 0 && g.w.constructors) {
    char b[420];
    int k = 0;
    for (int i = 0; i < g.data.n_constructors && k < (int) sizeof(b) - 40; i++) {
      const auto &c = g.data.constructors[i];
      k += std::snprintf(b + k, sizeof(b) - k, "%2d      %-13.13s %4d\n",
                         c.pos, c.name, c.points);
    }
    lv_label_set_text(g.w.constructors, b);
    update_constructor_flags();
  }

  // 8.1: the permutation line. It stays BLANK for most of a season on purpose -
  // a sentence about arithmetic in April is noise, not news.
  if (g.w.champ_line) {
    char b[96];
    b[0] = '\0';
    if (g.data.n_standings > 0 && g.cal.n > 0) {
      int rounds_left = 0, sprints_left = 0;
      for (int i = 0; i < g.cal.n; i++) {
        const uint32_t race = g.cal.rounds[i].start[calendar::RACE];
        if (race && race > g.now_utc) {
          rounds_left++;
          if (g.cal.rounds[i].start[calendar::SPRINT]) sprints_left++;
        }
      }
      const auto o = champ::evaluate(g.data.standings, g.data.n_standings,
                                     rounds_left, sprints_left);
      if (!champ::line(o, b, sizeof(b))) b[0] = '\0';
    }
    lv_label_set_text(g.w.champ_line, b);
  }

  if (g.data.n_standings > 0 && g.w.standings) {
    // The flag column sits between the position and the name: 6 spaces after the
    // position leave room for a 16 px flag at the third character.
    char b[900];
    int k = 0;
    for (int i = 0; i < g.data.n_standings && k < (int) sizeof(b) - 48; i++) {
      const auto &s2 = g.data.standings[i];
      k += std::snprintf(b + k, sizeof(b) - k, "%2d      %-10s %4d %s\n",
                         s2.pos, s2.name, s2.points, s2.wins ? "W" : " ");
    }
    lv_label_set_text(g.w.standings, b);
    wire_standings_tap();
    update_standing_flags();
  }
}

inline void tick(uint32_t now_ms) {
  refresh_data();
  g.st = state::evaluate(g.cal, g.now_utc, g.clock_valid);

  const int ridx = g.st.current.valid() ? g.st.current.round_idx
                                        : (g.st.next.valid() ? g.st.next.round_idx : 0);
  const uint8_t round = (ridx >= 0 && ridx < g.cal.n) ? g.cal.rounds[ridx].round : 0;

  // grid_pos / finish_pos arrive with the order data at M4; until then the
  // watched driver reports sessions and the grid being set, which needs neither.
  // The watched driver's grid slot and result, from whatever the store holds.
  int grid_pos = 0, finish_pos = 0;
  const char *status = "";
  // DATA-6: entered follows the season's roster; the entry list below can only
  // confirm it (a driver found on the grid is entered whatever the roster says).
  g.watch_cfg.entered = roster::watched_entered(ui::g.roster, g.watch_cfg.driver_id,
                                                g.watch_cfg.entered);
  // 6.14.2: only an order list for THIS round says anything about this round. Until the new
  // results land, the store still holds the previous race's, and those must not become a
  // "finishes P3" the moment the window closes.
  const bool this_round = g.data.entries_season == g.cal.season && g.data.entries_round == round;
  for (int i = 0; this_round && i < g.data.n_entries; i++)
    if (g.data.entries[i].watched) {
      if (g.data.entries_mode == state::FINAL) finish_pos = g.data.entries[i].pos;
      else grid_pos = g.data.entries[i].pos;
      if (g.data.entries[i].out) status = g.data.entries[i].gap;
      g.watch_cfg.entered = true;
    }
  watch::step(g.watch_cfg, g.st, g.latch, g.alert, now_ms, g.cal.season, round,
              grid_pos, finish_pos, status);

  // UI-20e: narrow the carousel during a race week.
  {
    const bool want = (g.st.weekend == state::RACE_WEEK ||
                       g.st.weekend == state::SESSION_SOON ||
                       g.st.weekend == state::SESSION_LIVE ||
                       g.st.weekend == state::RACE_LIVE ||
                       g.st.weekend == state::POST_SESSION);
    int ci = -1;
    if (want && ridx >= 0 && ridx < g.cal.n) {
      const auto *c = map::by_circuit_id(g.cal.rounds[ridx].circuit_id);
      for (int i = 0; c && i < circuits::N; i++)
        if (&circuits::C[i] == c) { ci = i; break; }
    }
    ui::set_focus(want && ci >= 0, ci);
  }

  update_header();
  update_top5();
  update_banner(now_ms);
  ui::advance(now_ms);
}

inline void setup(const Widgets &w) {
  g.w = w;
  // Build the top-5 strip once.
  if (w.top5 != nullptr) {
    for (int i = 0; i < 5; i++) {
      const int y = i * 20;
      g.t5_bar[i] = lv_obj_create(w.top5);
      lv_obj_set_pos(g.t5_bar[i], 0, y + 3);
      lv_obj_set_size(g.t5_bar[i], 4, 14);
      lv_obj_set_style_border_width(g.t5_bar[i], 0, 0);
      lv_obj_set_style_radius(g.t5_bar[i], 1, 0);
      lv_obj_set_style_pad_all(g.t5_bar[i], 0, 0);
      lv_obj_remove_flag(g.t5_bar[i], LV_OBJ_FLAG_SCROLLABLE);
      g.t5_flag[i] = lv_image_create(w.top5);
      lv_obj_set_pos(g.t5_flag[i], 48, y + 4);
      lv_obj_add_flag(g.t5_flag[i], LV_OBJ_FLAG_HIDDEN);
      g.t5_text[i] = lv_label_create(w.top5);
      lv_obj_set_pos(g.t5_text[i], 12, y + 2);
      lv_label_set_text(g.t5_text[i], "");
      lv_obj_add_flag(g.t5_text[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
  g.cal = state::Calendar();          // the compiled floor until a fetch lands
  std::snprintf(g.watch_cfg.driver_id, sizeof(g.watch_cfg.driver_id), "%s",
                watch::DEFAULT_DRIVER_ID);
  std::snprintf(g.watch_cfg.code, sizeof(g.watch_cfg.code), "VER");
  std::snprintf(g.watch_cfg.given, sizeof(g.watch_cfg.given), "Max");
  std::snprintf(g.watch_cfg.iso3, sizeof(g.watch_cfg.iso3), "NLD");
  g.watch_cfg.entered = true;
  g.ready = true;
}

// UI-181: every setting lands here, whichever surface changed it - the panel's Save, the
// web UI or Home Assistant (each entity's own trigger calls it) - and once at boot. It is
// idempotent, so calling it for an unchanged value costs nothing visible. `team` and
// `driver` are the select options as written ("(none)" for neither).
inline void apply_settings(float carousel_s, int content_idx, const char *team, const char *driver,
                           bool alerts, bool milestones_only, bool loud) {
  if (!g.ready) return;              // an entity restored before setup: boot applies them after
  ui::set_interval(carousel_s);
  if (content_idx != ui::g.content_idx) ui::set_content(content_idx);   // restages the carousel
  const bool have_team = team && *team && std::strcmp(team, "(none)") != 0;
  std::snprintf(g.favourite.name, sizeof(g.favourite.name), "%s", have_team ? team : "");
  // 6.14.1: the option is "Given Family"; what is kept is the driverId, resolved from the
  // compiled table. No match (or "(none)") watches nobody.
  g.watch_cfg.driver_id[0] = g.watch_cfg.code[0] = g.watch_cfg.given[0] = g.watch_cfg.iso3[0] = '\0';
  for (int k = 0; driver && *driver && k < drivers::N; k++) {
    char full[48];
    std::snprintf(full, sizeof(full), "%s %s", drivers::P[k].given, drivers::P[k].family);
    if (std::strcmp(full, driver) != 0) continue;
    std::snprintf(g.watch_cfg.driver_id, sizeof(g.watch_cfg.driver_id), "%s", drivers::P[k].driver_id);
    std::snprintf(g.watch_cfg.code, sizeof(g.watch_cfg.code), "%s", drivers::P[k].code);
    std::snprintf(g.watch_cfg.given, sizeof(g.watch_cfg.given), "%s", drivers::P[k].given);
    std::snprintf(g.watch_cfg.iso3, sizeof(g.watch_cfg.iso3), "%s", drivers::P[k].iso3);
    break;
  }
  g.watch_cfg.enabled = alerts && g.watch_cfg.driver_id[0] != '\0';
  g.watch_cfg.milestones_only = milestones_only;
  g.watch_cfg.loud = loud;
  g.data_gen = 0;                    // the row marks (watched, favourite) are re-derived next tick
}

}  // namespace app
}  // namespace f1
