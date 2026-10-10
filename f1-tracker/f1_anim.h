#pragma once
// The animated logos: a small pack of cars lapping the logo's own circuit, on the boot
// (Wi-Fi status) page and the About page; the boot page also has the start lights. Timing and path are in f1_animlogic.h (host
// tested); the sprites are f1_cars.h (tools/gen_cars.py); this file only makes the
// objects and moves them.
//
// Like sky-tracker's launch over its boot screen (UI-69k), nothing runs until start-up
// has ended: LVGL draws only when the main loop runs, and setup blocks it. So the lights
// and cars start the first time the page is seen after setup.
//
// Cost (sky-tracker PERF-9, UI-69i): three small images and five opaque discs, set_pos'd at 25 Hz, only while
// their page is the active screen. No resizing, no blending of anything large.
#include "f1_animlogic.h"
#include "f1_cars.h"

namespace f1 {
namespace anim {

// Read by tools/render_pages.py so the renders cannot drift from the firmware.
inline constexpr int BOOT_LOGO = 200, ABOUT_LOGO = 112;
inline constexpr int BOOT_LAMP_D = 20, BOOT_LAMP_GAP = 32, BOOT_LAMP_Y = 404;      // screen y of the centres
inline constexpr uint32_t LAMP_OFF = 0x2A1417, LAMP_ON = 0xE53935;
// The pack: the logo's orange, then two plain colours. Generic silhouettes, no liveries.
inline constexpr uint32_t CAR_COL[animlogic::N_CARS] = {0xFF8A1F, 0x4FC3F7, 0xFFD54A};
inline constexpr int PERIOD_MS = 40;

struct Scene {
  lv_obj_t *screen = nullptr, *logo = nullptr;
  int size = 0;
  animlogic::Track track;
  lv_obj_t *car[animlogic::N_CARS] = {nullptr};
  lv_obj_t *lamp[animlogic::N_LIGHTS] = {nullptr};   // the start lights: the boot page only
  bool lights = false, lamps_shown = false;
  int shown_lit = -2;
  lv_image_dsc_t sprite[animlogic::HEADINGS]{};      // the alpha masks of f1_cars.h, one per heading
  int car_px = 0;                                    // sprite size
  int shown_head[animlogic::N_CARS] = {-1, -1, -1};
  bool active = false, ready = false;
  uint32_t t0 = 0;
  int lx = 0, ly = 0;                                // the logo's top-left on its screen
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
  for (auto *o : s.car) if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  for (auto *o : s.lamp) if (o) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  s.lamps_shown = false;
  s.shown_lit = -2;
}

inline void begin(Scene &s) {
  lv_obj_update_layout(s.screen);              // the logo sits in a flex column on the boot page
  lv_area_t a;
  lv_obj_get_coords(s.logo, &a);
  s.lx = a.x1;
  s.ly = a.y1;
  if (s.lights) {
    const int span = (animlogic::N_LIGHTS - 1) * BOOT_LAMP_GAP;
    for (int i = 0; i < animlogic::N_LIGHTS; i++) {
      lv_obj_set_pos(s.lamp[i], 240 - span / 2 + i * BOOT_LAMP_GAP - BOOT_LAMP_D / 2, BOOT_LAMP_Y - BOOT_LAMP_D / 2);
      lv_obj_move_foreground(s.lamp[i]);
    }
  }
  for (auto *o : s.car) lv_obj_move_foreground(o);
  for (int &h : s.shown_head) h = -1;
  s.shown_lit = -2;
  s.t0 = lv_tick_get();
  s.active = true;
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

  // the start lights: once, as the boot page opens
  if (s.lights) {
    const bool gantry = animlogic::gantry_visible(t);
    const int lit = animlogic::lit(t);
    if (gantry != s.lamps_shown || lit != s.shown_lit) {
      for (int i = 0; i < animlogic::N_LIGHTS; i++) {
        if (gantry) lv_obj_remove_flag(s.lamp[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s.lamp[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(s.lamp[i], lv_color_hex(i < lit ? LAMP_ON : LAMP_OFF), 0);
      }
      s.lamps_shown = gantry;
      s.shown_lit = lit;
    }
  }

  // the pack, each car turned to the way the track runs under it
  const float f = animlogic::lap_frac(t);
  for (int k = 0; k < animlogic::N_CARS; k++) {
    const float fk = f - k * animlogic::CAR_GAP;
    float x, y;
    animlogic::at(s.track, fk, x, y);
    const int h = animlogic::heading(s.track, fk);
    if (h != s.shown_head[k]) { lv_image_set_src(s.car[k], &s.sprite[h]); s.shown_head[k] = h; }
    lv_obj_set_pos(s.car[k], s.lx + (int) lroundf(x) - s.car_px / 2, s.ly + (int) lroundf(y) - s.car_px / 2);
    lv_obj_remove_flag(s.car[k], LV_OBJ_FLAG_HIDDEN);
  }
}

inline void make(Scene &s, lv_obj_t *screen, lv_obj_t *logo, int size, bool lights) {
  if (screen == nullptr || logo == nullptr) return;
  const auto *c = animlogic::find_circuit("it-1922");     // the circuit the logo is made from
  if (c == nullptr || !animlogic::build(s.track, *c, size)) return;
  s.screen = screen; s.logo = logo; s.size = size; s.lights = lights;
  if (lights)
    for (int i = 0; i < animlogic::N_LIGHTS; i++) s.lamp[i] = disc(screen, BOOT_LAMP_D, LAMP_OFF);
  s.car_px = size >= 150 ? cars::SZ_LARGE : cars::SZ_SMALL;
  for (int h = 0; h < animlogic::HEADINGS; h++) {
    auto &d = s.sprite[h];
    d.header.magic = LV_IMAGE_HEADER_MAGIC;
    d.header.cf = LV_COLOR_FORMAT_A8;
    d.header.w = s.car_px;
    d.header.h = s.car_px;
    d.header.stride = s.car_px;
    d.data_size = (uint32_t) s.car_px * s.car_px;
    d.data = size >= 150 ? (const uint8_t *) cars::LARGE[h] : (const uint8_t *) cars::SMALL[h];
  }
  for (int k = 0; k < animlogic::N_CARS; k++) {
    lv_obj_t *im = lv_image_create(screen);
    lv_image_set_src(im, &s.sprite[0]);
    lv_obj_set_style_image_recolor(im, lv_color_hex(CAR_COL[k]), 0);       // the mask takes the car's colour
    lv_obj_set_style_image_recolor_opa(im, LV_OPA_COVER, 0);
    lv_obj_remove_flag(im, (lv_obj_flag_t) (LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_add_flag(im, LV_OBJ_FLAG_HIDDEN);
    s.car[k] = im;
  }
  s.ready = true;
}

// Call once, at the end of setup, with the two pages and their logos.
inline void init(lv_obj_t *boot_screen, lv_obj_t *boot_logo, lv_obj_t *about_screen, lv_obj_t *about_logo) {
  make(boot_, boot_screen, boot_logo, BOOT_LOGO, true);           // lights and cars
  make(about_, about_screen, about_logo, ABOUT_LOGO, false);      // cars alone
  lv_timer_create([](lv_timer_t *) { step(boot_); step(about_); }, PERIOD_MS, nullptr);
}

}  // namespace anim
}  // namespace f1
