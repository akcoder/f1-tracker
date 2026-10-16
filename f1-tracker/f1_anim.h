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
#include "f1_mem.h"
#include "f1_cars.h"

namespace f1 {
namespace anim {

// Read by tools/render_pages.py so the renders cannot drift from the firmware.
inline constexpr int BOOT_LOGO = 200, ABOUT_LOGO = 112;
// Screen y of the lamp centres: under the body line even when it is the two-line AP
// instruction (which ends near y 410 on the panel) - at 404 the lamps sat on its second line.
inline constexpr int BOOT_LAMP_D = 20, BOOT_LAMP_GAP = 32, BOOT_LAMP_Y = 442;
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
  animlogic::Pack pack;                              // this lap's set (MOTION-3a); pack.lap says which
  uint32_t chain = 1;                                // the xorshift chain the laps' seeds come from
  int shown_head[animlogic::N_CARS] = {-1, -1, -1};
  bool active = false, ready = false;
  uint32_t t0 = 0;
  int lx = 0, ly = 0;                                // the logo's top-left on its screen
};
inline PSRAM_BSS Scene boot_, about_;               // 2.5 kB each, in PSRAM (decision 203)

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
  const uint32_t seed = s.t0 ^ (s.t0 << 7);              // different speeds each time the page opens
  s.pack = animlogic::make_pack(seed);                   // lap 0: the grid
  s.chain = seed ? seed : 1;
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
  {
    // Read afresh each frame: on the first board the column reflowed when the Wi-Fi body grew
    // to two lines, and cars placed from the opening's origin sat 13 px under the line.
    lv_area_t a;
    lv_obj_get_coords(s.logo, &a);
    s.lx = a.x1;
    s.ly = a.y1;
  }
  int32_t t = (int32_t) (lv_tick_get() - s.t0);
  // A page left open for days: `t` would overflow at 24.8 days, and long before that the float
  // lap count loses the sub-millisecond steps and the cars judder. Each car's position is
  // periodic in whole laps (f1_animlogic.h), so once the lights are done the origin is moved
  // forward by whole laps and nothing on screen changes.
  if (t > animlogic::GANTRY_END_MS + 60 * 1000) {
    const int32_t fold = (t - animlogic::GANTRY_END_MS) / animlogic::LAP_MS * animlogic::LAP_MS;
    s.t0 += (uint32_t) fold;
    t -= fold;
    s.pack.lap -= (int) (fold / animlogic::LAP_MS);       // the set's lap index moves with the origin
  }

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

  // the pack: each car at its own pace, turned to the way the track runs under it; the
  // faster of two close cars pulls out to the side to pass
  const float u = animlogic::lap_u(t);
  // MOTION-3a: a new set of swings each lap, handed over at the line without a jump
  for (const int lap = (int) u; s.pack.lap < lap;)
    s.pack = animlogic::next_pack(s.pack, animlogic::xorshift(s.chain));
  const float side = animlogic::SIDE_PX_PER_100 * (float) s.size / 100.0f;
  for (int k = 0; k < animlogic::N_CARS; k++) {
    const float fk = animlogic::car_pos(s.pack, k, u);
    float x, y, x2, y2;
    animlogic::at(s.track, fk, x, y);
    const int h = animlogic::heading(s.track, fk);
    if (h != s.shown_head[k]) { lv_image_set_src(s.car[k], &s.sprite[h]); s.shown_head[k] = h; }
    const float w = animlogic::pulling_out(s.pack, k, u);
    if (w > 0) {                                           // to the left of the direction of travel
      animlogic::at(s.track, fk + 0.004f, x2, y2);
      const float dx = x2 - x, dy = y2 - y, l = std::sqrt(dx * dx + dy * dy);
      if (l > 1e-4f) { x += dy / l * side * w; y -= dx / l * side * w; }
    }
    lv_obj_set_pos(s.car[k], s.lx + (int) lroundf(x) - s.car_px / 2, s.ly + (int) lroundf(y) - s.car_px / 2);
    lv_obj_remove_flag(s.car[k], LV_OBJ_FLAG_HIDDEN);
    if (w > 0) lv_obj_move_foreground(s.car[k]);            // the car passing is drawn over the one it passes
  }
}

inline void make(Scene &s, lv_obj_t *screen, lv_obj_t *logo, int size, bool lights) {
  if (screen == nullptr || logo == nullptr) return;
  if (!animlogic::build(s.track, size)) return;               // the logo's own outline
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
