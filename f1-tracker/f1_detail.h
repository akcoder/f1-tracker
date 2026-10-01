#pragma once
// The detail card (section 6.11). Composition is kept free of LVGL so the text
// can be tested on the host; only open/close touches widgets.
//
// 6.1: with a card open, a tap ANYWHERE - including the gear - closes it rather
// than acting. Both siblings behave this way and it is what stops a card from
// becoming a thing you have to dismiss correctly.
#include <cstdio>
#include <cstring>

#include "f1_calendar.h"
#include "f1_circuits.h"
#include "f1_drivers.h"
#include "f1_facts.h"
#include "f1_legends.h"
#include "f1_map.h"
#include "f1_state.h"
#include "f1_store.h"

namespace f1 {
namespace detail {

// Compose a driver's card from whatever is known. Everything is optional: a
// row from the live order, a compiled profile, or both.
inline void compose_driver(const store::Entry &e, const drivers::Profile *prof,
                           const legends::Profile *leg, state::OrderMode mode,
                           char *title, size_t tn, char *body, size_t bn) {
  if (prof) std::snprintf(title, tn, "%s %s", prof->given, prof->family);
  else std::snprintf(title, bn < tn ? bn : tn, "%s", e.name[0] ? e.name : e.code);

  int k = 0;
  // DATA-7: number and acronym are both optional.
  if (e.number > 0 && e.code[0])
    k += std::snprintf(body + k, bn - k, "#%d  %s", e.number, e.code);
  else if (e.code[0])
    k += std::snprintf(body + k, bn - k, "%s", e.code);
  if (e.team[0]) k += std::snprintf(body + k, bn - k, "%s%s", k ? "   " : "", e.team);

  if (e.pos > 0) {
    // The label depends on what the order currently MEANS (RACE-12), so the
    // card can never present a provisional grid as a result.
    const char *what = (mode == state::FINAL) ? "Finished"
                     : (mode == state::GRID) ? "Starts"
                     : (mode == state::GRID_PROVISIONAL) ? "Starts (provisional)"
                     : "Entry";
    k += std::snprintf(body + k, bn - k, "\n\n%-20s P%d", what, e.pos);
  }
  if (e.gap[0]) k += std::snprintf(body + k, bn - k, "\n%-20s %s",
                                   e.out ? "Status" : "Gap", e.gap);

  if (prof) {
    k += std::snprintf(body + k, bn - k, "\n\n%-20s %u", "Career starts",
                       (unsigned) prof->starts);
    k += std::snprintf(body + k, bn - k, "\n%-20s %u", "Wins", (unsigned) prof->wins);
    // DATA-8: -1 means no trustworthy figure. A blank is correct; a zero lies.
    if (prof->poles >= 0)
      k += std::snprintf(body + k, bn - k, "\n%-20s %d", "Poles", (int) prof->poles);
    if (prof->titles)
      k += std::snprintf(body + k, bn - k, "\n%-20s %u", "World titles",
                         (unsigned) prof->titles);
  }
  if (leg && leg->line[0])
    std::snprintf(body + k, bn - k, "\n\n%s", leg->line);
}

// 6.11 / UI-8a: session times in all three zones. The feed gives UTC, the
// device knows its own offset, and OpenF1 gives the track's - so a global
// calendar can say when a session is where it actually happens.
inline void compose_circuit(const circuits::Circuit &c, const calendar::Round *r,
                            char *title, size_t tn, char *body, size_t bn) {
  std::snprintf(title, tn, "%s", c.name);
  int k = std::snprintf(body, bn, "%-20s %s\n%-20s %.3f km", "Location", c.location,
                        "Length", c.length_m / 1000.0f);
  if (c.opened) k += std::snprintf(body + k, bn - k, "\n%-20s %u", "Opened",
                                   (unsigned) c.opened);
  if (c.firstgp) k += std::snprintf(body + k, bn - k, "\n%-20s %u", "First GP",
                                    (unsigned) c.firstgp);
  if (c.altitude_m) k += std::snprintf(body + k, bn - k, "\n%-20s %d m", "Altitude",
                                       (int) c.altitude_m);
  if (!map::is_north_up(c))
    k += std::snprintf(body + k, bn - k, "\n%-20s %.0f°", "Map rotated",
                       (double) c.rot_cdeg / 100.0);

  const facts::Fact *f = r ? facts::find(r->circuit_id) : facts::find(c.id);
  if (f) {
    if (f->fl_time[0])
      k += std::snprintf(body + k, bn - k, "\n\n%-20s %s\n%-20s %s, %u",
                         "Fastest lap", f->fl_time, "  (since 2004)", f->fl_who,
                         (unsigned) f->fl_year);
    if (f->last_year)
      k += std::snprintf(body + k, bn - k, "\n\n%-20s %s, %s\n%-20s %u %s",
                         "Last winner", f->last_driver, f->last_team, "",
                         (unsigned) f->last_year, f->last_event);
    if (f->races)
      k += std::snprintf(body + k, bn - k, "\n\n%-20s %u", "Races held",
                         (unsigned) f->races);
  }
  if (r) std::snprintf(body + k, bn - k, "\n%-20s %s", "Country", r->country);
}

// ---- the panel -----------------------------------------------------------
struct Card {
  lv_obj_t *panel = nullptr;
  lv_obj_t *title = nullptr;
  lv_obj_t *body = nullptr;
  lv_obj_t *flag = nullptr;
  lv_image_dsc_t dsc{};
  bool open = false;
};

inline Card g;

inline bool is_open() { return g.open; }

inline void close() {
  if (g.panel) lv_obj_add_flag(g.panel, LV_OBJ_FLAG_HIDDEN);
  g.open = false;
}

inline void show(const char *title, const char *body, const char *iso3) {
  if (g.panel == nullptr) return;
  if (g.title) lv_label_set_text(g.title, title);
  if (g.body) lv_label_set_text(g.body, body);
  if (g.flag) {
    const flags::Flag *f = (iso3 && *iso3) ? flags::find(iso3) : nullptr;
    if (f) {
      g.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      g.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
      g.dsc.header.w = flags::CARD_W;
      g.dsc.header.h = flags::CARD_H;
      g.dsc.header.stride = flags::CARD_W * 2;
      g.dsc.data_size = flags::CARD_W * flags::CARD_H * 2;
      g.dsc.data = (const uint8_t *) f->card;
      lv_image_set_src(g.flag, &g.dsc);
      lv_obj_remove_flag(g.flag, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(g.flag, LV_OBJ_FLAG_HIDDEN);   // decision 20
    }
  }
  lv_obj_remove_flag(g.panel, LV_OBJ_FLAG_HIDDEN);
  g.open = true;
}

inline void setup(lv_obj_t *panel, lv_obj_t *title, lv_obj_t *body, lv_obj_t *flag) {
  g.panel = panel; g.title = title; g.body = body; g.flag = flag;
  close();
}

}  // namespace detail
}  // namespace f1
