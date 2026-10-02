#pragma once
// The application tick: one place where the state machine, the race page, the
// carousel and the watched driver meet.
//
// Everything on screen is a function of ONE state (section 4.1), so this is
// deliberately the only file that reads that state and writes widgets. Keeping
// it in one place is what stops the header, the top-5 strip and the order page
// disagreeing - plane-tracker decisions 47 and 57 are both that bug.
#include <cstdio>
#include <cstring>

#include "f1_calendar.h"
#include "f1_detail.h"
#include "f1_net.h"
#include "f1_order.h"
#include "f1_state.h"
#include "f1_store.h"
#include "f1_ui.h"
#include "f1_watch.h"

namespace f1 {
namespace app {

struct Widgets {
  lv_obj_t *standings = nullptr;     // label on the championship page
  lv_obj_t *summary = nullptr;       // label on the post-session summary
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
};

struct App {
  Widgets w;
  state::Calendar cal;
  state::Status st;
  watch::Config watch_cfg;
  watch::Latch latch;
  watch::Alert alert;
  uint32_t last_eval_ms = 0;
  bool clock_valid = false;
  uint32_t now_utc = 0;
  store::Store data;            // the last published snapshot
  uint32_t data_gen = 0;        // generation we have rendered
  lv_obj_t *standings_rows = nullptr;
  lv_obj_t *standings_label = nullptr;
  // The top-5 strip's widgets, built once in setup().
  lv_obj_t *t5_bar[5] = {nullptr};
  lv_obj_t *t5_flag[5] = {nullptr};
  lv_obj_t *t5_text[5] = {nullptr};
  lv_image_dsc_t t5_dsc[5]{};
  lv_image_dsc_t flag_dsc{};
};

inline App g;

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
    const facts::Fact *f = facts::find(g.cal.rounds[ridx2].circuit_id);
    char b[160];
    int k = 0;
    if (c) k += std::snprintf(b + k, sizeof(b) - k, "%.3f km\n", c->length_m / 1000.0f);
    if (f && f->fl_time[0])
      k += std::snprintf(b + k, sizeof(b) - k, "FL %s\n", f->fl_time);
    if (f && f->last_year)
      std::snprintf(b + k, sizeof(b) - k, "last %s", f->last_driver);
    lv_label_set_text(g.w.race_facts, b);
  }
}

// ---- the watched-driver banner (6.14.3) ---------------------------------
inline void update_banner(uint32_t now_ms) {
  if (g.w.banner == nullptr) return;
  if (g.alert.active) {
    if (g.w.banner_text) lv_label_set_text(g.w.banner_text, g.alert.line);
    // decision 92: loud. A milestone gets a taller strip, an event a shorter
    // one - but NEITHER covers the circuit trace. The map is the thing a reader
    // is actually looking at; the licence to be loud was about there being no
    // live data underneath, not about covering the subject.
    // Both sit below the trace (70-310) with clear black between, and both end
    // above the gear at 432 so settings stay reachable without waiting it out.
    const bool big = (g.alert.tier == watch::MILESTONE);
    lv_obj_set_height(g.w.banner, big ? 100 : 92);
    lv_obj_set_y(g.w.banner, big ? 326 : 330);
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

// 6.1: with a card open, a tap ANYWHERE closes it rather than acting. Returns
// true when the tap was consumed, so callers do nothing else.
inline bool tap_consumed() {
  if (!detail::is_open()) return false;
  detail::close();
  return true;
}

// 6.11 / decision 53: the whole row is the tap target. Opens the driver card
// for the row at `idx` of whatever the order currently holds.
inline void open_driver(int idx) {
  if (idx < 0 || idx >= g.data.n_entries) return;
  const auto &e = g.data.entries[idx];
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
  detail::show(title, body, prof ? prof->iso3 : e.iso3);
}

// Tapping the map opens the circuit card (6.11).
inline void open_circuit() {
  const int ridx = g.st.current.valid() ? g.st.current.round_idx
                                        : (g.st.next.valid() ? g.st.next.round_idx : 0);
  if (ridx < 0 || ridx >= g.cal.n) return;
  const calendar::Round &r = g.cal.rounds[ridx];
  const auto *c = map::by_circuit_id(r.circuit_id);
  if (c == nullptr) return;
  char title[48], body[480];
  detail::compose_circuit(*c, &r, title, sizeof(title), body, sizeof(body));
  detail::show(title, body, r.iso3);
}

// ---- the tick -----------------------------------------------------------
// Pull a new snapshot only when the data task says the generation moved. A
// fetch must never block rendering, and the lock is held for a memcpy only.
inline void refresh_data() {
  const uint32_t gen = net::snapshot(g.data);
  if (gen == g.data_gen) return;
  g.data_gen = gen;

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

  // The watched driver's row highlight and the ambient marker (6.14.3).
  if (g.data.n_entries > 0) {
    for (int i = 0; i < g.data.n_entries; i++) {
      auto &e = g.data.entries[i];
      e.watched = g.watch_cfg.code[0] && std::strcmp(e.code, g.watch_cfg.code) == 0;
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
    if (sm.have_fastest || sm.have_stops) {
      char b[420];
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
      lv_label_set_text(g.w.summary, b);
    }
  }

  if (g.data.n_standings > 0 && g.w.standings) {
    char b[900];
    int k = 0;
    for (int i = 0; i < g.data.n_standings && k < (int) sizeof(b) - 48; i++) {
      const auto &s2 = g.data.standings[i];
      k += std::snprintf(b + k, sizeof(b) - k, "%2d  %-10s %-14s %4d %s\n",
                         s2.pos, s2.name, s2.team, s2.points,
                         s2.wins ? "W" : " ");
    }
    lv_label_set_text(g.w.standings, b);
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
  for (int i = 0; i < g.data.n_entries; i++)
    if (g.data.entries[i].watched) {
      if (g.data.entries_mode == state::FINAL) finish_pos = g.data.entries[i].pos;
      else grid_pos = g.data.entries[i].pos;
      if (g.data.entries[i].out) status = g.data.entries[i].gap;
      g.watch_cfg.entered = true;
    }
  watch::step(g.watch_cfg, g.st, g.latch, g.alert, now_ms, g.cal.season, round,
              grid_pos, finish_pos, status);

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
}

}  // namespace app
}  // namespace f1
