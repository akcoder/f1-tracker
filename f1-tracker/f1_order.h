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
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "f1_flags.h"
#include "f1_state.h"
#include "f1_store.h"

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
inline constexpr uint32_t COL_FAV_BG = 0x14203A;  // 8.2: the favourite team's rows

using store::Entry;

struct Row {
  lv_obj_t *bg = nullptr;
  lv_obj_t *bar = nullptr;
  lv_obj_t *flag = nullptr;
  lv_obj_t *text = nullptr;
  lv_obj_t *gap = nullptr;
  lv_image_dsc_t dsc{};        // points into f1_flags.h; never copied
};

// decision 53: the whole row is the tap target. The handler is supplied by the
// app rather than wired here, so this file stays free of everything above it.
using RowTap = void (*)(int index);

struct View {
  RowTap on_tap = nullptr;
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
  lv_obj_set_user_data(r.bg, (void *) (intptr_t) i);
  lv_obj_add_event_cb(r.bg, [](lv_event_t *e) {
    if (g.on_tap == nullptr) return;
    lv_obj_t *t = (lv_obj_t *) lv_event_get_current_target(e);
    g.on_tap((int) (intptr_t) lv_obj_get_user_data(t));
  }, LV_EVENT_SHORT_CLICKED, nullptr);

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

inline void setup(lv_obj_t *parent, lv_obj_t *header, const lv_font_t *font,
                  RowTap on_tap = nullptr) {
  g.on_tap = on_tap;
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
                       (m == state::FINAL) ? "Gap" : "To pole";
  std::snprintf(b, sizeof(b), "Pos  #   Nat Driver     Team               %s", gaphdr);
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

  // 22 rows fill the box exactly (decision 88), so a 23rd or 24th - a larger
  // field, or a reserve on the grid - would be clipped by the container's edge
  // and simply not be there. When the rows are taller than the box the list
  // scrolls instead, with room under the last row so it can be brought fully
  // into view; when they fit it stays still and the page flip is undisturbed.
  {
    const int view_h = lv_obj_get_height(g.parent);
    const bool over = n * ROW_H > view_h;
    if (over) {
      lv_obj_add_flag(g.parent, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_scroll_dir(g.parent, LV_DIR_VER);
      lv_obj_set_scrollbar_mode(g.parent, LV_SCROLLBAR_MODE_AUTO);
      lv_obj_set_style_pad_bottom(g.parent, 10, 0);
    } else {
      lv_obj_remove_flag(g.parent, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_pad_bottom(g.parent, 0, 0);
      lv_obj_scroll_to_y(g.parent, 0, LV_ANIM_OFF);
    }
  }

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
    } else if (x.favourite) {
      // 8.2: a quieter tint than the watched driver's, so the two can both be
      // on screen and still be told apart.
      lv_obj_set_style_bg_color(r.bg, lv_color_hex(COL_FAV_BG), 0);
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
