#pragma once
// The animated logos: the start lights and a car lapping the logo's circuit, on the boot
// (Wi-Fi status) page and the About page. Timing and path are in f1_animlogic.h (host
// tested); this file only makes the objects and moves them.
//
// Like sky-tracker's launch over its boot screen (UI-69k), nothing runs until start-up
// has ended: LVGL draws only when the main loop runs, and setup blocks it. So the
// sequence starts the first time the page is seen after setup.
//
// Cost (sky-tracker PERF-9, UI-69i): a handful of small opaque discs, set_pos'd at 25 Hz,
// and only while their page is the active screen. No resizing, no blending.
#include "f1_animlogic.h"

namespace f1 {
namespace anim {

// Geometry, read by tools/render_pages.py so the renders cannot drift from the firmware.
inline constexpr int BOOT_LOGO = 200, ABOUT_LOGO = 112;
inline constexpr int BOOT_LAMP_D = 20, BOOT_LAMP_GAP = 32, BOOT_LAMP_Y = 404;      // screen y of the centres
inline constexpr int ABOUT_LAMP_D = 12, ABOUT_LAMP_GAP = 18, ABOUT_LAMP_DY = 30;    // below the logo's centre
inline constexpr int CAR_D = 11;                                                     // the head; the trail shrinks
inline constexpr uint32_t LAMP_OFF = 0x2A1417, LAMP_ON = 0xE53935;
inline constexpr uint32_t CAR_COL[animlogic::N_TRAIL + 1] = {0xFF8A1F, 0xB8661A, 0x7A4418, 0x45302A};
inline constexpr int PERIOD_MS = 40;

struct Scene {
  lv_obj_t *screen = nullptr, *logo = nullptr;
  int size = 0, lamp_d = 0, lamp_gap = 0, lamp_cy = 0;   // lamp_cy: -1 = below the logo's centre (About)
  bool about = false;
  animlogic::Track track;
  lv_obj_t *lamp[animlogic::N_LIGHTS] = {nullptr};
  lv_obj_t *car[animlogic::N_TRAIL + 1] = {nullptr};
  bool active = false, ready = false;
  uint32_t t0 = 0;
  int lx = 0, ly = 0;                   // the logo's top-left on its screen
  int shown_lit = -2;
  bool lamps_shown = false;
};
inline Scene boot_, about_;

inline lv_obj_t *disc(lv_obj_t *parent, int d, uint32_t col) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(col), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
  lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  return o;
}

inline void hide_all(Scene &s) {
  for (auto *o : s.lamp) if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  for (auto *o : s.car) if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  s.lamps_shown = false;
  s.shown_lit = -2;
}

inline void begin(Scene &s) {
  lv_obj_update_layout(s.screen);              // the logo sits in a flex column on the boot page
  lv_area_t a;
  lv_obj_get_coords(s.logo, &a);
  s.lx = a.x1;
  s.ly = a.y1;
  const int span = (animlogic::N_LIGHTS - 1) * s.lamp_gap;
  const int cy = s.about ? s.ly + s.size / 2 + ABOUT_LAMP_DY : s.lamp_cy;
  for (int i = 0; i < animlogic::N_LIGHTS; i++) {
    lv_obj_set_pos(s.lamp[i], 240 - span / 2 + i * s.lamp_gap - s.lamp_d / 2, cy - s.lamp_d / 2);
    lv_obj_move_foreground(s.lamp[i]);
  }
  for (auto *o : s.car) lv_obj_move_foreground(o);
  s.t0 = lv_tick_get();
  s.active = true;
  s.shown_lit = -2;
}

inline void step(Scene &s) {
  if (!s.ready) return;
  const bool on = lv_screen_active() == s.screen;
  if (!on) {
    if (s.active) { s.active = false; hide_all(s); }
    return;
  }
  if (!s.active) begin(s);
  const int32_t t = (int32_t) (lv_tick_get() - s.t0);

  // the start lights: once on boot, repeating (with a rest) on About
  const int32_t lt = s.about ? animlogic::about_phase(t) : t;
  const bool gantry = animlogic::gantry_visible(lt);
  const int lit = animlogic::lit(lt);
  if (gantry != s.lamps_shown || lit != s.shown_lit) {
    for (int i = 0; i < animlogic::N_LIGHTS; i++) {
      if (gantry) lv_obj_remove_flag(s.lamp[i], LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(s.lamp[i], LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_bg_color(s.lamp[i], lv_color_hex(i < lit ? LAMP_ON : LAMP_OFF), 0);
    }
    s.lamps_shown = gantry;
    s.shown_lit = lit;
  }

  // the car: the head and a trail behind it, on the logo's own circuit
  const float f = animlogic::lap_frac(t);
  for (int k = 0; k <= animlogic::N_TRAIL; k++) {
    float x, y;
    animlogic::at(s.track, f - k * animlogic::TRAIL_LAG, x, y);
    const int d = CAR_D - 2 * k;
    lv_obj_set_size(s.car[k], d, d);                  // (set once per size; LVGL ignores no-ops)
    lv_obj_set_pos(s.car[k], s.lx + (int) lroundf(x) - d / 2, s.ly + (int) lroundf(y) - d / 2);
    lv_obj_remove_flag(s.car[k], LV_OBJ_FLAG_HIDDEN);
  }
}

inline void make(Scene &s, lv_obj_t *screen, lv_obj_t *logo, int size, int lamp_d, int lamp_gap,
                 int lamp_cy, bool about) {
  if (screen == nullptr || logo == nullptr) return;
  const auto *c = animlogic::find_circuit("it-1922");     // the circuit the logo is made from
  if (c == nullptr || !animlogic::build(s.track, *c, size)) return;
  s.screen = screen; s.logo = logo; s.size = size;
  s.lamp_d = lamp_d; s.lamp_gap = lamp_gap; s.lamp_cy = lamp_cy; s.about = about;
  for (int i = 0; i < animlogic::N_LIGHTS; i++) s.lamp[i] = disc(screen, lamp_d, LAMP_OFF);
  for (int k = 0; k <= animlogic::N_TRAIL; k++) s.car[k] = disc(screen, CAR_D - 2 * k, CAR_COL[k]);
  s.ready = true;
}

// Call once, at the end of setup, with the two pages and their logos.
inline void init(lv_obj_t *boot_screen, lv_obj_t *boot_logo, lv_obj_t *about_screen, lv_obj_t *about_logo) {
  make(boot_, boot_screen, boot_logo, BOOT_LOGO, BOOT_LAMP_D, BOOT_LAMP_GAP, BOOT_LAMP_Y, false);
  make(about_, about_screen, about_logo, ABOUT_LOGO, ABOUT_LAMP_D, ABOUT_LAMP_GAP, 0, true);
  lv_timer_create([](lv_timer_t *) { step(boot_); step(about_); }, PERIOD_MS, nullptr);
}

}  // namespace anim
}  // namespace f1
