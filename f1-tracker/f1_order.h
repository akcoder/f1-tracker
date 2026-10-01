#pragma once
// The order page (section 6.3): 22 rows, mono12, 18 px each, TEAM column kept.
//
// UI-10a: rows are built here in C++ rather than as an LVGL table, because each
// one needs a flag IMAGE and a team-colour BAR and a table cell holds neither.
// 22 rows x 5 children is affordable precisely because nothing here moves
// per-frame.
//
// The layout is not guessed: tools/mock_order_page.py rendered it at 480x480
// with real Roboto Mono metrics and the real entry list, and 6.3.1 records the
// result - 18 px rows fit with 22 px to spare, which the state line spends.
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "f1_flags.h"
#include "f1_state.h"

namespace f1 {
namespace order {

inline constexpr int MAX_ROWS = 24;      // 22 in 2026, with headroom
inline constexpr int ROW_H = 18;         // decision 88, measured
inline constexpr int BAR_W = 4;

// Column x positions, from the measured render (6.3.1). Roboto Mono at 12 px
// advances exactly 7.0 px, so these are character-aligned.
inline constexpr int X_BAR = 2, X_POS = 12, X_NUM = 44, X_FLAG = 72;
inline constexpr int X_NAME = 96, X_TEAM = 178, X_GAP = 400;

inline constexpr uint32_t COL_TEXT = 0xC9D3F2;
inline constexpr uint32_t COL_MUTED = 0x7E8BB3;
inline constexpr uint32_t COL_WATCH_BG = 0x1A2547;
inline constexpr uint32_t COL_WATCH_TX = 0xFFFFFF;
inline constexpr uint32_t COL_OUT = 0x5A6687;   // retired: greyed, never removed

struct Entry {
  int pos = 0;                 // 0 = no position yet (ENTRY LIST)
  int number = 0;
  char code[8] = {0};
  char name[20] = {0};         // surname, upper case
  char team[20] = {0};
  char iso3[4] = {0};          // "" = draw NO flag (decision 20)
  char gap[12] = {0};
  uint32_t colour = 0x888888;  // team_colour, cached (6.10)
  bool out = false;            // retired: keeps its row, greyed (6.3)
  bool watched = false;
};

struct Row {
  lv_obj_t *bg = nullptr;
  lv_obj_t *bar = nullptr;
  lv_obj_t *flag = nullptr;
  lv_obj_t *text = nullptr;
  lv_obj_t *gap = nullptr;
  lv_image_dsc_t dsc{};        // points into f1_flags.h; never copied
};

struct View {
  lv_obj_t *parent = nullptr;
  lv_obj_t *header = nullptr;
  const lv_font_t *font = nullptr;
  Row rows[MAX_ROWS];
  int built = 0;
};

inline View g;

// One row's widgets, created once and then only updated.
inline void build_row(View &v, int i) {
  Row &r = v.rows[i];
  const int y = i * ROW_H;

  r.bg = lv_obj_create(v.parent);
  lv_obj_set_pos(r.bg, 0, y);
  lv_obj_set_size(r.bg, lv_obj_get_width(v.parent), ROW_H);
  lv_obj_set_style_bg_opa(r.bg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(r.bg, 0, 0);
  lv_obj_set_style_pad_all(r.bg, 0, 0);
  lv_obj_set_style_radius(r.bg, 0, 0);
  lv_obj_remove_flag(r.bg, LV_OBJ_FLAG_SCROLLABLE);
  // decision 53: the whole row is the tap target, not just the symbol. To a
  // finger the label is part of the driver, and it is the larger of the two.
  lv_obj_add_flag(r.bg, LV_OBJ_FLAG_CLICKABLE);

  r.bar = lv_obj_create(r.bg);
  lv_obj_set_pos(r.bar, X_BAR, 2);
  lv_obj_set_size(r.bar, BAR_W, ROW_H - 5);
  lv_obj_set_style_border_width(r.bar, 0, 0);
  lv_obj_set_style_radius(r.bar, 1, 0);
  lv_obj_set_style_pad_all(r.bar, 0, 0);
  lv_obj_remove_flag(r.bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(r.bar, LV_OBJ_FLAG_CLICKABLE);

  r.flag = lv_image_create(r.bg);
  lv_obj_set_pos(r.flag, X_FLAG, 3);
  lv_obj_add_flag(r.flag, LV_OBJ_FLAG_HIDDEN);

  r.text = lv_label_create(r.bg);
  lv_obj_set_pos(r.text, X_POS, 2);
  lv_label_set_text(r.text, "");
  if (v.font) lv_obj_set_style_text_font(r.text, v.font, 0);
  lv_obj_set_style_text_color(r.text, lv_color_hex(COL_TEXT), 0);

  r.gap = lv_label_create(r.bg);
  lv_obj_set_pos(r.gap, X_GAP, 2);
  lv_label_set_text(r.gap, "");
  if (v.font) lv_obj_set_style_text_font(r.gap, v.font, 0);
  lv_obj_set_style_text_color(r.gap, lv_color_hex(COL_MUTED), 0);
}

inline void setup(lv_obj_t *parent, lv_obj_t *header, const lv_font_t *font) {
  g.parent = parent;
  g.header = header;
  g.font = font;
  if (parent == nullptr) return;
  for (int i = 0; i < MAX_ROWS; i++) build_row(g, i);
  g.built = MAX_ROWS;
  for (int i = 0; i < MAX_ROWS; i++) lv_obj_add_flag(g.rows[i].bg, LV_OBJ_FLAG_HIDDEN);
}

// The column header reflects the mode, so the order is never ambiguous (6.3).
inline void set_mode(state::OrderMode m) {
  if (g.header == nullptr) return;
  char b[80];
  // UI-10d: the GAP column carries the gap to POLE in grid modes and the race
  // gap in FINAL - it is only empty in ENTRY LIST (decision 93).
  const char *gaphdr = (m == state::ENTRY_LIST) ? "" :
                       (m == state::FINAL) ? "GAP" : "TO POLE";
  std::snprintf(b, sizeof(b), "POS  #   NAT DRIVER     TEAM              %s", gaphdr);
  lv_label_set_text(g.header, b);
}

// Point an image descriptor at a flag's pixels in flash. Nothing is copied: the
// bitmaps are constexpr and outlive every widget.
inline void bind_flag(Row &r, const flags::Flag *f) {
  if (f == nullptr) { lv_obj_add_flag(r.flag, LV_OBJ_FLAG_HIDDEN); return; }
  r.dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  r.dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  r.dsc.header.w = flags::CARD_W;
  r.dsc.header.h = flags::CARD_H;
  r.dsc.header.stride = flags::CARD_W * 2;
  r.dsc.data_size = flags::CARD_W * flags::CARD_H * 2;
  r.dsc.data = (const uint8_t *) f->card;
  lv_image_set_src(r.flag, &r.dsc);
  lv_obj_remove_flag(r.flag, LV_OBJ_FLAG_HIDDEN);
}

inline void render(const Entry *e, int n, state::OrderMode mode) {
  if (g.built == 0) return;
  set_mode(mode);
  if (n > g.built) n = g.built;

  for (int i = 0; i < g.built; i++) {
    Row &r = g.rows[i];
    if (i >= n) { lv_obj_add_flag(r.bg, LV_OBJ_FLAG_HIDDEN); continue; }
    const Entry &x = e[i];
    lv_obj_remove_flag(r.bg, LV_OBJ_FLAG_HIDDEN);

    // 6.14.3 ambient tier: his row highlighted. This is the watched-driver
    // feature most of the time, and the part worth polishing.
    if (x.watched) {
      lv_obj_set_style_bg_color(r.bg, lv_color_hex(COL_WATCH_BG), 0);
      lv_obj_set_style_bg_opa(r.bg, LV_OPA_COVER, 0);
    } else {
      lv_obj_set_style_bg_opa(r.bg, LV_OPA_TRANSP, 0);
    }

    lv_obj_set_style_bg_color(r.bar, lv_color_hex(x.colour), 0);
    bind_flag(r, x.iso3[0] ? flags::find(x.iso3) : nullptr);

    char b[64];
    if (x.pos > 0)
      std::snprintf(b, sizeof(b), "%-4d%-4d    %-10s %-16s", x.pos, x.number,
                    x.name, x.team);
    else
      std::snprintf(b, sizeof(b), "    %-4d    %-10s %-16s", x.number, x.name, x.team);
    lv_label_set_text(r.text, b);
    lv_obj_set_style_text_color(
        r.text, lv_color_hex(x.out ? COL_OUT : (x.watched ? COL_WATCH_TX : COL_TEXT)), 0);
    lv_label_set_text(r.gap, x.gap);
    lv_obj_set_style_text_color(r.gap, lv_color_hex(x.out ? COL_OUT : COL_MUTED), 0);
  }
}

}  // namespace order
}  // namespace f1
