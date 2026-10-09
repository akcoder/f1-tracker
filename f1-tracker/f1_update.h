#pragma once
// Firmware updates, the card (UI-68), ported from sky-tracker 4.6.27's
// sky_update.h. The logic that can be tested without a screen is in
// f1_updlogic.h; this is the LVGL half and the glue to ESPHome's update entity.
//
// The firmware is published as a GitHub release with a manifest beside it;
// ESPHome's http_request update entity reads the manifest (3 min after boot, then
// hourly, and when Settings > Check for updates or the Upgrade Check button is
// used). This shows what it finds: "Checking for updates", then "Up to date" or
// "Update available" (Not now / Update) with the release notes, then the
// download's progress. An update found by the hourly check does NOT open
// anything - an icon appears beside the gear and tapping it shows the prompt.
#include <cstdio>
#include <string>

#include "f1_updlogic.h"

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
#include "esphome/components/update/update_entity.h"
#include "esphome/core/log.h"
#endif

namespace f1 {
namespace upd {

// Geometry, kept as named constants so tools/render_pages.py can read the same
// numbers instead of carrying a copy that drifts.
inline constexpr int CARD_W = 400, CARD_H = 260;          // the short card
inline constexpr int CARD_TALL_W = 440, CARD_TALL_H = 440;  // with release notes
inline constexpr int NOTES_W = 400, NOTES_H = 244, NOTES_Y = 112;
inline constexpr int BTN_W = 170, BTN_H = 44, BTN_Y = 196, BTN_TALL_Y = 374;
inline constexpr int BAR_W = 340, BAR_H = 16, BAR_Y = 180;
inline constexpr int ICON_W = 46, ICON_H = 46, ICON_X = -50, ICON_Y = -2;   // beside the gear

inline const char *source = "";           // the manifest URL, for the log (NET-13a)

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)

inline esphome::update::UpdateEntity *ent = nullptr;
inline int st() { return ent ? (int) ent->state : 0; }
inline std::string latest() { return ent ? ent->update_info.latest_version : std::string(); }
inline std::string current() { return ent ? ent->update_info.current_version : std::string(); }
inline std::string summary() { return ent ? ent->update_info.summary : std::string(); }
inline bool has_prog() { return ent && ent->update_info.has_progress; }
inline float prog() { return ent ? ent->update_info.progress : 0.0f; }
inline void do_check() {
  ESP_LOGD("upd", "firmware: GET %s", source);     // NET-13a: ESPHome's own check logs nothing
  if (ent) ent->check();
}
inline void do_perform() { if (ent) ent->perform(); }
inline uint32_t now_ms() { return esphome::millis(); }

inline const lv_font_t *f_title = nullptr, *f_body = nullptr, *f_small = nullptr;

struct Ui {
  lv_obj_t *root = nullptr, *card = nullptr, *title = nullptr, *l1 = nullptr, *l2 = nullptr;
  lv_obj_t *bar = nullptr, *notes = nullptr, *notes_lbl = nullptr;
  lv_obj_t *btn[2] = {nullptr, nullptr}, *btn_lbl[2] = {nullptr, nullptr};
  int card_w = CARD_W, btn_y = BTN_Y;
  Mode mode = M_NONE;
  uint32_t since = 0;                  // when CHECKING / INSTALLING began
  char what[24] = "Downloading";       // the first line while installing: where it comes from
};
inline Ui ui_;
inline lv_obj_t *icon = nullptr;       // beside the gear, on the top layer
inline bool page_ok = false;           // is a page with the gear showing? (set from the app tick)

inline void close() {
  if (ui_.root) lv_obj_delete(ui_.root);
  ui_ = Ui();
}

inline lv_obj_t *label(lv_obj_t *p, const lv_font_t *f, uint32_t col, int y) {
  lv_obj_t *l = lv_label_create(p);
  if (f) lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(col), 0);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_width(l, 380);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_label_set_text(l, "");
  lv_obj_align(l, LV_ALIGN_TOP_MID, 0, y);
  return l;
}

inline void btn_cb(lv_event_t *e);

inline void open() {
  if (ui_.root) return;
  // On the TOP layer, so it covers whichever page is showing without disturbing
  // it (the same reason the OTA panel lives there, FAIL-12).
  lv_obj_t *dim = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(dim);
  lv_obj_set_size(dim, 480, 480);
  lv_obj_set_style_bg_color(dim, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(dim, LV_OPA_70, 0);
  lv_obj_add_flag(dim, LV_OBJ_FLAG_CLICKABLE);       // swallows taps meant for the page
  ui_.root = dim;

  lv_obj_t *c = lv_obj_create(dim);
  lv_obj_remove_style_all(c);
  lv_obj_set_size(c, CARD_W, CARD_H);
  lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(c, lv_color_hex(0x0E1836), 0);
  lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(c, 14, 0);
  lv_obj_set_style_border_color(c, lv_color_hex(0x2A3A66), 0);
  lv_obj_set_style_border_width(c, 2, 0);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  ui_.card = c;

  ui_.title = label(c, f_title, 0xFFFFFF, 16);
  ui_.l1 = label(c, f_body, 0xDCE4F8, 76);
  ui_.l2 = label(c, f_body, 0x7E8BB3, 104);

  ui_.bar = lv_bar_create(c);
  lv_obj_set_size(ui_.bar, BAR_W, BAR_H);
  lv_obj_align(ui_.bar, LV_ALIGN_TOP_MID, 0, BAR_Y);
  lv_bar_set_range(ui_.bar, 0, 100);
  lv_obj_set_style_bg_color(ui_.bar, lv_color_hex(0x22305A), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(ui_.bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(ui_.bar, lv_color_hex(0x2D5BD0), LV_PART_INDICATOR);
  lv_obj_add_flag(ui_.bar, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < 2; i++) {
    lv_obj_t *b = lv_button_create(c);
    lv_obj_set_size(b, BTN_W, BTN_H);
    lv_obj_set_style_bg_color(b, lv_color_hex(i == 0 ? 0x1A2547 : 0x2D5BD0), 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_t *l = lv_label_create(b);
    if (f_body) lv_obj_set_style_text_font(l, f_body, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, btn_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
    ui_.btn[i] = b;
    ui_.btn_lbl[i] = l;
  }

  // UI-68c: the release notes, in a box that scrolls (shown with "Update available")
  lv_obj_t *n = lv_obj_create(c);
  lv_obj_remove_style_all(n);
  lv_obj_set_size(n, NOTES_W, NOTES_H);
  lv_obj_align(n, LV_ALIGN_TOP_MID, 0, NOTES_Y);
  lv_obj_set_style_bg_color(n, lv_color_hex(0x0A1128), 0);
  lv_obj_set_style_bg_opa(n, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(n, 8, 0);
  lv_obj_set_style_pad_all(n, 8, 0);
  lv_obj_set_scroll_dir(n, LV_DIR_VER);
  lv_obj_add_flag(n, LV_OBJ_FLAG_HIDDEN);
  ui_.notes = n;
  ui_.notes_lbl = lv_label_create(n);
  if (f_small) lv_obj_set_style_text_font(ui_.notes_lbl, f_small, 0);
  lv_obj_set_style_text_color(ui_.notes_lbl, lv_color_hex(0xC9D3F2), 0);
  lv_obj_set_width(ui_.notes_lbl, NOTES_W - 16);
  lv_label_set_long_mode(ui_.notes_lbl, LV_LABEL_LONG_WRAP);
  lv_label_set_text(ui_.notes_lbl, "");
}

// Two layouts: tall with the notes (an update offered), otherwise the short one.
inline void layout(bool tall) {
  ui_.card_w = tall ? CARD_TALL_W : CARD_W;
  lv_obj_set_size(ui_.card, ui_.card_w, tall ? CARD_TALL_H : CARD_H);
  lv_obj_align(ui_.card, LV_ALIGN_CENTER, 0, 0);
  for (lv_obj_t *l : {ui_.title, ui_.l1, ui_.l2}) lv_obj_set_width(l, tall ? 420 : 380);
  lv_obj_align(ui_.l1, LV_ALIGN_TOP_MID, 0, tall ? 58 : 76);
  lv_obj_align(ui_.l2, LV_ALIGN_TOP_MID, 0, tall ? 82 : 104);
  ui_.btn_y = tall ? BTN_TALL_Y : BTN_Y;
  if (!tall) lv_obj_add_flag(ui_.notes, LV_OBJ_FLAG_HIDDEN);
}

// nullptr: hidden; b == nullptr: a alone, centred
inline void buttons(const char *a, const char *b) {
  for (int i = 0; i < 2; i++) {
    const char *t = i == 0 ? a : b;
    if (t == nullptr) { lv_obj_add_flag(ui_.btn[i], LV_OBJ_FLAG_HIDDEN); continue; }
    lv_obj_remove_flag(ui_.btn[i], LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(ui_.btn_lbl[i], t);
    const int w = ui_.card_w;
    lv_obj_align(ui_.btn[i], LV_ALIGN_TOP_LEFT, i == 0 ? w / 2 - 186 : w / 2 + 16, ui_.btn_y);
  }
  if (a && !b) lv_obj_align(ui_.btn[0], LV_ALIGN_TOP_MID, 0, ui_.btn_y);
}

inline void show(Mode m, const char *why = nullptr) {
  open();
  ui_.mode = m;
  char b[96];
  lv_obj_add_flag(ui_.bar, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_text_color(ui_.l2, lv_color_hex(0x7E8BB3), 0);
  const std::string cur = current(), lat = latest();
  const std::string notes = (m == M_AVAILABLE) ? notes_text(summary()) : std::string();
  layout(!notes.empty());
  if (!notes.empty()) {
    lv_label_set_text(ui_.notes_lbl, notes.c_str());
    lv_obj_remove_flag(ui_.notes, LV_OBJ_FLAG_HIDDEN);
    lv_obj_scroll_to_y(ui_.notes, 0, LV_ANIM_OFF);
  }
  switch (m) {
    case M_CHECKING:
      lv_label_set_text(ui_.title, "Checking for updates");
      lv_label_set_text(ui_.l1, "Asking GitHub for the latest release");
      snprintf(b, sizeof(b), "Installed: %s", cur.c_str());
      lv_label_set_text(ui_.l2, b);
      buttons("Close", nullptr);
      break;
    case M_LATEST:
      lv_label_set_text(ui_.title, "Up to date");
      snprintf(b, sizeof(b), "%s is the latest version", cur.c_str());
      lv_label_set_text(ui_.l1, b);
      lv_label_set_text(ui_.l2, "Checks again every hour");
      buttons("Close", nullptr);
      break;
    case M_AVAILABLE:
      lv_label_set_text(ui_.title, "Update available");
      snprintf(b, sizeof(b), "New version: %s", lat.c_str());
      lv_label_set_text(ui_.l1, b);
      snprintf(b, sizeof(b), notes.empty() ? "Installed: %s" : "Installed: %s - what's new:", cur.c_str());
      lv_label_set_text(ui_.l2, b);
      buttons("Not now", "Update");
      break;
    case M_INSTALLING:
      lv_label_set_text(ui_.title, "Updating firmware");
      lv_label_set_text(ui_.l1, ui_.what);
      lv_label_set_text(ui_.l2, "Keep the power on");
      lv_obj_set_style_text_color(ui_.l2, lv_color_hex(0xFFB547), 0);
      lv_obj_remove_flag(ui_.bar, LV_OBJ_FLAG_HIDDEN);
      lv_bar_set_value(ui_.bar, 0, LV_ANIM_OFF);
      buttons(nullptr, nullptr);
      break;
    case M_INSTALL_FAILED:
      lv_label_set_text(ui_.title, "Update failed");
      lv_label_set_text(ui_.l1, "The new firmware was not installed");
      snprintf(b, sizeof(b), "Still running %s", cur.c_str());
      lv_label_set_text(ui_.l2, b);
      buttons("Close", nullptr);
      break;
    case M_FAILED:
      lv_label_set_text(ui_.title, "Couldn't check");
      lv_label_set_text(ui_.l1, why ? why : "No answer from the update server");
      lv_label_set_text(ui_.l2, "Check the Wi-Fi and try again later");
      buttons("Close", nullptr);
      break;
    default:
      break;
  }
}

inline void btn_cb(lv_event_t *e) {
  const int i = (int) (intptr_t) lv_event_get_user_data(e);
  if (ui_.mode == M_AVAILABLE && i == 1) {
    std::snprintf(ui_.what, sizeof(ui_.what), "Downloading");
    ui_.since = now_ms();
    show(M_INSTALLING);
    lv_refr_now(nullptr);        // perform() blocks the loop; draw the card first
    do_perform();
    return;
  }
  close();
}

// NET-13a: log each check's answer once (what the manifest says, against what is installed)
inline void log_result(int s) {
  static int last_s = -1;
  static std::string last_v;
  const std::string v = latest();
  if (s == last_s && v == last_v) return;
  last_s = s;
  last_v = v;
  if ((s == E_NO_UPDATE || s == E_AVAILABLE) && !v.empty())
    ESP_LOGI("upd", "firmware: manifest has %s, installed %s (%s)", v.c_str(), current().c_str(),
             s == E_AVAILABLE ? "newer, offered" : "nothing newer");
}

// Every 500 ms: follow the entity.
inline void tick() {
  const int s = newer_only(st(), latest().c_str(), current().c_str());
  log_result(s);
  const Mode next = next_mode(ui_.mode, s, now_ms() - ui_.since);
  if (next != ui_.mode) {
    if (ui_.mode == M_CHECKING && next == M_AVAILABLE) show(M_AVAILABLE);
    else show(next);
  } else if (ui_.mode == M_INSTALLING && has_prog()) {
    lv_bar_set_value(ui_.bar, (int) (prog() + 0.5f), LV_ANIM_OFF);
  }
  if (icon) {
    const bool want = icon_wanted(s, ui_.mode, page_ok);
    if (want == lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN)) {
      if (want) lv_obj_remove_flag(icon, LV_OBJ_FLAG_HIDDEN);
      else lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// ---- the entry points the YAML calls ------------------------------------
inline void offer_now() {                       // the icon was tapped
  if (newer_only(st(), latest().c_str(), current().c_str()) != E_AVAILABLE) return;
  close();
  show(M_AVAILABLE);
}

inline void check_quiet(const char *why) {      // hourly, at boot, the web/HA button: no prompt
  ESP_LOGD("upd", "firmware check (%s)", why);
  do_check();
}

inline void check_now(bool wifi_up) {           // Settings > Check for updates
  close();
  ui_.since = now_ms();
  if (!wifi_up) { show(M_FAILED, "No network connection"); return; }
  show(M_CHECKING);
  do_check();
}

inline void set_source(const char *url) { source = url; }
inline void set_page_ok(bool ok) { page_ok = ok; }

// The internet install downloads and writes in ONE blocking call on the main
// loop: no LVGL timer runs and nothing redraws until it ends, so the bar would
// sit at 0. The OTA component's own callbacks move it and force a redraw.
inline void ota_begin(const char *what = "Downloading") {
  std::snprintf(ui_.what, sizeof(ui_.what), "%s", what);
  if (ui_.mode != M_INSTALLING) {               // started from HA: show the card too
    ui_.since = now_ms();
    show(M_INSTALLING);
  }
  lv_refr_now(nullptr);
}

inline void ota_progress(float pc) {
  static int last = -10;
  const int p = (int) pc;
  if (ui_.mode != M_INSTALLING || (p < last + 2 && p >= last && p < 100)) return;
  last = p;
  lv_bar_set_value(ui_.bar, p, LV_ANIM_OFF);
  lv_refr_now(nullptr);
}

inline void ota_end(bool ok) {
  if (ui_.mode != M_INSTALLING) return;
  if (!ok) { show(M_INSTALL_FAILED); lv_refr_now(nullptr); return; }
  lv_label_set_text(ui_.l1, "Installed, restarting");
  lv_bar_set_value(ui_.bar, 100, LV_ANIM_OFF);
  lv_refr_now(nullptr);
}

// The icon: LV_SYMBOL_DOWNLOAD in 0xFFB547, beside the gear. Created here on the
// top layer rather than in the YAML, because it belongs to no one page - the gear
// is on several and the icon follows them (page_ok).
inline void init(esphome::update::UpdateEntity *e, const lv_font_t *title, const lv_font_t *body,
                 const lv_font_t *small, const lv_font_t *icon_font) {
  ent = e;
  f_title = title; f_body = body; f_small = small;
  icon = lv_button_create(lv_layer_top());
  lv_obj_set_size(icon, ICON_W, ICON_H);
  lv_obj_align(icon, LV_ALIGN_BOTTOM_RIGHT, ICON_X, ICON_Y);
  lv_obj_set_style_bg_opa(icon, LV_OPA_TRANSP, 0);
  lv_obj_set_style_shadow_width(icon, 0, 0);
  lv_obj_set_style_border_width(icon, 0, 0);
  lv_obj_t *l = lv_label_create(icon);
  lv_label_set_text(l, LV_SYMBOL_DOWNLOAD);
  if (icon_font) lv_obj_set_style_text_font(l, icon_font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(0xFFB547), 0);
  lv_obj_center(l);
  lv_obj_add_event_cb(icon, [](lv_event_t *) { offer_now(); }, LV_EVENT_SHORT_CLICKED, nullptr);
  lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);
  lv_timer_create([](lv_timer_t *) { tick(); }, 500, nullptr);
}

#endif  // device only

}  // namespace upd
}  // namespace f1
