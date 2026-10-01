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
    std::snprintf(out, n, "CLOCK NOT SYNCED");
    std::snprintf(sub, subn, "waiting for time");
    return;
  }

  switch (st.weekend) {
    case state::RACE_LIVE:
    case state::SESSION_LIVE: {
      const char *lbl = st.current.valid()
                            ? state::session_label(st.current.session) : "SESSION";
      std::snprintf(out, n, "%s UNDER WAY", lbl);
      if (st.to_results > 0)
        std::snprintf(sub, subn, "RESULTS IN ~%02d:%02d",
                      (int) (st.to_results / 3600), (int) ((st.to_results % 3600) / 60));
      break;
    }
    case state::POST_SESSION:
      std::snprintf(out, n, "%s", state::order_name(st.order));
      if (st.next.valid() && st.to_next > 0)
        std::snprintf(sub, subn, "NEXT: %s IN %dd %02dh",
                      state::session_label(st.next.session),
                      (int) (st.to_next / 86400), (int) ((st.to_next % 86400) / 3600));
      break;
    case state::SESSION_SOON:
      if (st.next.valid()) {
        const char *lbl = state::session_label(st.next.session);
        if (state::is_race(st.next.session))
          std::snprintf(out, n, "LIGHTS OUT IN %02d:%02d:%02d",
                        (int) (st.to_next / 3600), (int) ((st.to_next % 3600) / 60),
                        (int) (st.to_next % 60));
        else
          std::snprintf(out, n, "%s IN %02d:%02d", lbl,
                        (int) (st.to_next / 3600), (int) ((st.to_next % 3600) / 60));
        std::snprintf(sub, subn, "%s", state::order_name(st.order));
      }
      break;
    case state::RACE_WEEK:
      if (st.next.valid())
        std::snprintf(out, n, "%s IN %dd %02dh",
                      state::session_label(st.next.session),
                      (int) (st.to_next / 86400), (int) ((st.to_next % 86400) / 3600));
      std::snprintf(sub, subn, "%s", state::order_name(st.order));
      break;
    case state::IDLE:
      if (st.next.valid())
        std::snprintf(out, n, "NEXT SESSION IN %d DAYS", (int) (st.to_next / 86400));
      break;
    case state::OFF_SEASON:
    default:
      // RACE-13c: the months-long gap before the next calendar is published is
      // NORMAL, not a fault. It must never read as broken.
      std::snprintf(out, n, "%u SEASON COMPLETE", (unsigned) g.cal.season);
      std::snprintf(sub, subn, "next calendar not yet published");
      break;
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
      std::snprintf(t, sizeof(t), "R%u %s", g.cal.rounds[ridx].round,
                    g.cal.rounds[ridx].name);
    else
      std::snprintf(t, sizeof(t), "F1 TRACKER");
    lv_label_set_text(g.w.race_title, t);
  }
  if (g.w.order_title) lv_label_set_text(g.w.order_title, state::order_name(g.st.order));
}

// ---- the watched-driver banner (6.14.3) ---------------------------------
inline void update_banner(uint32_t now_ms) {
  if (g.w.banner == nullptr) return;
  if (g.alert.active) {
    if (g.w.banner_text) lv_label_set_text(g.w.banner_text, g.alert.line);
    // decision 92: loud. A milestone is a brief takeover, an event is a
    // full-width banner. Safe here because nothing underneath is changing.
    const bool big = (g.alert.tier == watch::MILESTONE);
    lv_obj_set_height(g.w.banner, big ? 200 : 96);
    lv_obj_remove_flag(g.w.banner, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(g.w.banner, LV_OBJ_FLAG_HIDDEN);
  }
}

inline void dismiss_banner() { watch::dismiss(g.alert); update_banner(0); }

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
  update_banner(now_ms);
  ui::advance(now_ms);
}

inline void setup(const Widgets &w) {
  g.w = w;
  g.cal = state::Calendar();          // the compiled floor until a fetch lands
  std::snprintf(g.watch_cfg.driver_id, sizeof(g.watch_cfg.driver_id), "%s",
                watch::DEFAULT_DRIVER_ID);
  std::snprintf(g.watch_cfg.code, sizeof(g.watch_cfg.code), "VER");
  std::snprintf(g.watch_cfg.given, sizeof(g.watch_cfg.given), "MAX");
  std::snprintf(g.watch_cfg.iso3, sizeof(g.watch_cfg.iso3), "NLD");
  g.watch_cfg.entered = true;
}

}  // namespace app
}  // namespace f1
