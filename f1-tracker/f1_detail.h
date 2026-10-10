#pragma once
// The detail card (section 6.11). Composition is kept free of LVGL so the text
// can be tested on the host; only open/close touches widgets.
//
// 6.1: with a card open, a tap ANYWHERE - including the gear - closes it rather
// than acting. Both siblings behave this way and it is what stops a card from
// becoming a thing you have to dismiss correctly.
#include <cstdio>
#include <atomic>
#include <cstring>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "f1_calendar.h"
#include "f1_circuits.h"
#include "f1_drivers.h"
#include "f1_facts.h"
#include "f1_jpg.h"
#include "f1_legends.h"
#include "f1_map.h"
#include "f1_portraits.h"
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

// ---- the portrait --------------------------------------------------------
// 6.11: a driver's picture beside the text, half size (120x160, from the 240x320
// portrait of decision 91) and shown TOP-DOWN as it decodes, as sky-tracker does
// for its pictures (UI-59f). The JPEG is in flash; a short-lived task on core 0
// decodes it band by band into one reused PSRAM buffer, and a timer on the
// display loop redraws the picture while rows arrive. Rows not yet decoded are
// black, the card's own colour, so nothing half-drawn is ever wrong.
inline constexpr int PHOTO_W = 120, PHOTO_H = 160;
inline constexpr int PHOTO_X = 304, PHOTO_Y = 48, TEXT_W_PHOTO = 280, TEXT_W_FULL = 408;
inline constexpr int PACE_MS = 14;     // between bands, so the reveal can be seen

struct Photo {
  lv_obj_t *img = nullptr, *credit = nullptr;
  uint16_t *buf = nullptr;
  lv_image_dsc_t dsc{};
  lv_timer_t *timer = nullptr;
  std::atomic<int> rows{0};
  std::atomic<bool> running{false};
  volatile bool cancel = false;
  int shown = -1;
  const portraits::Portrait *p = nullptr;
  char credit_text[64] = {0};
};
inline Photo ph;

inline void photo_rows(int done, void *) {
  ph.rows.store(done);
  vTaskDelay(pdMS_TO_TICKS(PACE_MS));
}
inline void photo_task(void *) {
  jpg::Progress pr;
  pr.rows = photo_rows;
  pr.cancel = &ph.cancel;
  jpg::decode_half(ph.p->jpeg, ph.p->len, ph.buf, PHOTO_W, PHOTO_H, &pr);
  ph.running.store(false);
  vTaskDelete(nullptr);
}
inline void photo_tick(lv_timer_t *t) {
  const int r = ph.rows.load();
  if (r != ph.shown) { ph.shown = r; if (ph.img) lv_obj_invalidate(ph.img); }
  if (!ph.running.load() && r >= PHOTO_H && ph.timer) { lv_timer_delete(ph.timer); ph.timer = nullptr; }
}
inline void photo_stop() {
  ph.cancel = true;
  for (int i = 0; i < 40 && ph.running.load(); i++) vTaskDelay(pdMS_TO_TICKS(5));
  if (ph.timer) { lv_timer_delete(ph.timer); ph.timer = nullptr; }
  if (ph.img) lv_obj_add_flag(ph.img, LV_OBJ_FLAG_HIDDEN);
  if (ph.credit) lv_obj_add_flag(ph.credit, LV_OBJ_FLAG_HIDDEN);
}
// Returns true when a picture is on its way (the text then makes room for it).
inline bool photo_start(const portraits::Portrait *p) {
  photo_stop();
  if (p == nullptr || ph.img == nullptr) return false;
  if (ph.buf == nullptr)
    ph.buf = (uint16_t *) heap_caps_malloc((size_t) PHOTO_W * PHOTO_H * 2, MALLOC_CAP_SPIRAM);
  if (ph.buf == nullptr || ph.running.load()) return false;      // no memory: a text-only card
  std::memset(ph.buf, 0, (size_t) PHOTO_W * PHOTO_H * 2);
  ph.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  ph.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  ph.dsc.header.w = PHOTO_W;
  ph.dsc.header.h = PHOTO_H;
  ph.dsc.header.stride = PHOTO_W * 2;
  ph.dsc.data_size = (uint32_t) PHOTO_W * PHOTO_H * 2;
  ph.dsc.data = (const uint8_t *) ph.buf;
  lv_image_set_src(ph.img, &ph.dsc);
  lv_obj_remove_flag(ph.img, LV_OBJ_FLAG_HIDDEN);
  // decision 72: the credit is drawn wherever the photograph is
  if (ph.credit) {
    std::snprintf(ph.credit_text, sizeof(ph.credit_text), "photo: %s", p->credit);
    lv_label_set_text(ph.credit, ph.credit_text);
    lv_obj_remove_flag(ph.credit, LV_OBJ_FLAG_HIDDEN);
  }
  ph.p = p;
  ph.cancel = false;
  ph.rows.store(0);
  ph.shown = -1;
  ph.running.store(true);
  ph.timer = lv_timer_create(photo_tick, 40, nullptr);
  if (xTaskCreatePinnedToCore(photo_task, "f1jpg", 6144, nullptr, 1, nullptr, 0) != pdPASS) {
    ph.running.store(false);
    photo_stop();
    return false;
  }
  return true;
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
  photo_stop();
  if (g.panel) lv_obj_add_flag(g.panel, LV_OBJ_FLAG_HIDDEN);
  g.open = false;
}

inline void show(const char *title, const char *body, const char *iso3,
                 const portraits::Portrait *photo = nullptr) {
  if (g.panel == nullptr) return;
  if (g.title) lv_label_set_text(g.title, title);
  if (g.body) {
    lv_label_set_text(g.body, body);
    lv_obj_set_width(g.body, photo_start(photo) ? TEXT_W_PHOTO : TEXT_W_FULL);
  } else {
    photo_start(photo);
  }
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

inline void setup(lv_obj_t *panel, lv_obj_t *title, lv_obj_t *body, lv_obj_t *flag,
                  lv_obj_t *photo = nullptr, lv_obj_t *credit = nullptr) {
  g.panel = panel; g.title = title; g.body = body; g.flag = flag;
  ph.img = photo; ph.credit = credit;
  close();
}

}  // namespace detail
}  // namespace f1
