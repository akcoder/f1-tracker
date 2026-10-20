#pragma once
// A visitor that crosses the screen now and then, stops in the middle, says a word in a speech bubble
// and goes on: the dinosaur's walk (f1_dino.h) and the toucan's flight (f1_toucan.h) are two Traits
// of this one engine. A Traits supplies the timing (its logic header), the pictures and the words:
//
//   using Walk;                          // make(seed) -> Walk with .flip and .word
//   using Frame;                         // at(walk, ms) -> Frame with .x .y .pose .bubble
//   static constexpr int W, H;           // the sprite; N_POSES of them, from pose 0
//   static constexpr int SCREEN_W, N_POSES;
//   static constexpr const char *TAG;
//   static Walk make(uint32_t seed);   static Frame at(const Walk &, uint32_t ms);
//   static bool finished(const Walk &, uint32_t ms);   static uint32_t gap(uint32_t seed);
//   static const char *word(const Walk &);   static const uint8_t *pose(int p);   // RGB565A8, facing right
//
// What keeps it fast (what sky-tracker's launch and plane-tracker's cheer learned on this panel):
//  * A SNAPSHOT of the screen is the visit's opaque backdrop, an image that sits on the active screen
//    as its top child. LVGL then culls everything beneath it, so a dirty box costs one image blit and
//    the sprite, not a redraw of the page's rows, flags and bars (plane-tracker's UI-66b). The page
//    stands still for the visit. With no memory for the snapshot, no visit.
//  * frame() runs on the display's LV_EVENT_REFR_START, from the clock at that moment, so every frame
//    drawn shows a fresh position; the objects it moves re-arm the next refresh. A 40 ms timer only
//    covers a stretch with no refresh (sky-tracker's UI-69a).
//  * Opaque, small, nothing translucent but the sprite's own antialiased edge; the sprite is an
//    RGB565A8 image drawn 1:1 (LVGL's fast path); a visit to the left uses mirrored copies made once,
//    in PSRAM, for that visit. The bubble is drawn once, when it appears.
//  * A visit logs its frames, rate and longest gap, as sky-tracker's animations do.
//
// Needs the build flag -DLV_USE_SNAPSHOT=1, the font for the bubble, `enabled` (the visitor's switch);
// the application tick calls tick() about once a second with whether a visit is welcome now (no card
// over the screen, not the boot, settings, About or debug page, the panel not blank). A touch ends a
// visit. The speech bubble's two pointers are the dinosaur's (f1_dino_data.h), for both.
//
// HUNGRY visits (MOTION-7, f1_critterbite.h): one in four, and only where the project says there is
// something to eat (`food_hook`: the circuit card's trace, a person's portrait), the visitor eats it in 3-5
// bites instead of just standing and speaking. A Traits then also supplies the aim, the mouth and the
// lunge (see the constants below). The bite marks are left as an image on the page when the visit ends,
// and `clear_marks()` takes them away: the project calls it when the card changes.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "f1_critterbite.h"
#include "f1_dino_data.h"

#if defined(USE_ESP32) && !defined(F1_HOST_TEST)
#include "esp_heap_caps.h"
#include "esphome/core/log.h"
namespace f1 {
namespace critter {

inline void *big_alloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
inline void big_free(void *p) { heap_caps_free(p); }

// An RGB565A8 image flipped left to right: its RGB plane (2 bytes a pixel), then its alpha plane.
inline void mirror_into(uint8_t *dst, const uint8_t *src, int w, int h) {
  const uint16_t *s565 = (const uint16_t *) src;
  uint16_t *d565 = (uint16_t *) dst;
  const uint8_t *sa = src + (size_t) w * h * 2;
  uint8_t *da = dst + (size_t) w * h * 2;
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      d565[(size_t) y * w + x] = s565[(size_t) y * w + (w - 1 - x)];
      da[(size_t) y * w + x] = sa[(size_t) y * w + (w - 1 - x)];
    }
}

inline void describe(lv_image_dsc_t &d, const uint8_t *data, int w, int h) {
  std::memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
  d.header.cf = LV_COLOR_FORMAT_RGB565A8;
  d.header.w = w;
  d.header.h = h;
  d.header.stride = w * 2;                 // the RGB plane's
  d.data_size = (uint32_t) w * h * 3;
  d.data = data;
}

inline constexpr int TAIL_W = dino::TAIL_W, TAIL_H = dino::TAIL_H;

// ---- the bite marks, left on the page after a hungry visit ---------------------------------------
inline lv_obj_t *marks_obj = nullptr;
inline lv_draw_buf_t *marks_buf = nullptr;
inline bool eating = false;          // a hungry visit is on: the project holds the card it is eating

inline void clear_marks() {
  if (marks_obj) lv_obj_delete(marks_obj);
  marks_obj = nullptr;
  if (marks_buf) lv_draw_buf_destroy(marks_buf);
  marks_buf = nullptr;
}

inline int lum565(const lv_draw_buf_t *s, int x, int y) {
  const uint16_t p = ((const uint16_t *) ((const uint8_t *) s->data + (size_t) y * s->header.stride))[x];
  return (((p >> 11) & 31) * 8 * 30 + ((p >> 5) & 63) * 4 * 59 + (p & 31) * 8 * 11) / 100;
}

// The marks as an image the size of the item: black where it was bitten, pale along the bite (the flesh),
// painted from the snapshot taken when the visit began, so it is only the item that is touched. It is made
// when a hungry visit starts (empty, behind the visitor), repainted as each bite lands, and kept on the
// page when the visit ends.
inline bool marks_begin(lv_obj_t *parent, const Food &it) {
  clear_marks();
  marks_buf = lv_draw_buf_create(it.w, it.h, LV_COLOR_FORMAT_ARGB8888, 0);
  if (marks_buf == nullptr) return false;
  lv_draw_buf_clear(marks_buf, nullptr);
  marks_obj = lv_image_create(parent);
  lv_image_set_src(marks_obj, marks_buf);
  lv_obj_set_pos(marks_obj, it.x, it.y);
  lv_obj_remove_flag(marks_obj, LV_OBJ_FLAG_CLICKABLE);
  return true;
}

// Paints bites from..k-1 (those before are on it already): black where the mouth took it, the rim around.
inline void marks_paint(const lv_draw_buf_t *snap, const Food &it, int kind, bool left, const Pt *pt, const int *r, int from, int k) {
  if (marks_obj == nullptr || marks_buf == nullptr) return;
  for (int i = from; i < k; i++) {
    const int ext = r[i] * BITE_REACH_X10 / 10 + 4;
    for (int y = pt[i].y - ext; y <= pt[i].y + ext; y++)
      for (int x = pt[i].x - ext; x <= pt[i].x + ext; x++) {
        if (x < it.x || x >= it.x + it.w || y < it.y || y >= it.y + it.h) continue;
        uint32_t *row = (uint32_t *) ((uint8_t *) marks_buf->data + (size_t) (y - it.y) * marks_buf->header.stride);
        uint32_t &px = row[x - it.x];
        if (bite_in(kind, left, x - pt[i].x, y - pt[i].y, r[i])) { px = 0xFF000000u; continue; }
        if (px != 0 || lum565(snap, x, y) <= 24) continue;      // black already, or not part of the picture
        bool near = false;                                      // within 3 px of the bite: the flesh
        for (int d = 0; d < 8 && !near; d++) {
          static const int8_t DX[8] = {3, -3, 0, 0, 2, -2, 2, -2}, DY[8] = {0, 0, 3, -3, 2, 2, -2, -2};
          near = bite_in(kind, left, x + DX[d] - pt[i].x, y + DY[d] - pt[i].y, r[i]);
        }
        if (near) px = 0xFFEADCB4u;
      }
  }
  lv_obj_invalidate(marks_obj);
}

template <class T>
struct Critter {
  static constexpr int N = T::N_POSES, SCREEN_W = T::SCREEN_W;
  static constexpr size_t POSE_BYTES = (size_t) T::W * T::H * 3;

  struct St {
    bool running = false, done = false;
    typename T::Walk w;
    uint32_t t0 = 0, last_frame = 0, next_at = 0, seed = 1;
    lv_draw_buf_t *snap = nullptr;
    lv_obj_t *root = nullptr, *img = nullptr, *bubble = nullptr, *label = nullptr, *tail = nullptr;
    lv_timer_t *timer = nullptr;
    uint8_t *mirror = nullptr;                  // the poses facing left: one block in PSRAM, per visit
    lv_image_dsc_t pose_dsc[N];
    lv_image_dsc_t tail_dsc;
    int pose = -1, x = -99999, y = -99999;
    bool bubble_on = false;
    uint32_t frames = 0, max_gap = 0;
    bool hungry = false;                         // this visit eats the card's picture
    Hunger hg;
    Food item;                                   // what it eats: the picture's own box
    int mdx = 0;                                 // the mouth, from the sprite's left edge
    int marks_k = 0;                             // bites painted so far
    const char *hungry_text = nullptr;           // what it says while it eats
  };
  static inline St st;
  static inline bool (*enabled)() = nullptr;           // the project's switch (null: always on)
  static inline const lv_font_t *bubble_font = nullptr;
  static inline bool (*food_hook)(Food &) = nullptr;   // the project: what could be eaten on the screen now
  static inline bool force_hungry = false;             // the next visit eats (a web button)

  struct Fr { int x, y, pose; bool bubble; };

  // The visit's frame at t: the walk in to the first bite, then the bites, the word, the way out.
  static Fr hungry_frame(uint32_t t) {
    const Hunger &h = st.hg;
    if (t < h.t0) {
      const auto f = T::at(st.w, t);
      return {f.x, f.y, (int) f.pose, false};
    }
    const bool flip = st.w.flip;
    if (t < h.t_exit) {
      const auto a = h.anchor(t);
      const double e = h.lunge(t);
      const int x = (int) std::lround(a.x - st.mdx + (flip ? -1 : 1) * T::LUNGE_X * e);
      const int y = (int) std::lround(a.y - T::MOUTH_DY + T::LUNGE_Y * e);
      if (a.moving) return {x, y, (int) (t / 90) % 4, false};
      const auto m = h.mouth(t);
      return {x, y, m == Hunger::OPEN ? T::P_OPEN : (m == Hunger::HALF ? T::P_HALF : T::P_REST), t >= h.t_say};
    }
    const double d = h.exit_walked(t);
    const Pt last = h.pt[h.n - 1];
    const double x0 = last.x - st.mdx;
    if (h.exit_standing(t)) {                                    // the afterthought: stopped, a bite
      const double e = h.lunge(t);
      const auto m = h.mouth(t);
      return {(int) std::lround((flip ? x0 - d : x0 + d) + (flip ? -1 : 1) * T::LUNGE_X * e),
              (int) std::lround(last.y - T::MOUTH_DY + T::LUNGE_Y * e),
              m == Hunger::OPEN ? T::P_OPEN : (m == Hunger::HALF ? T::P_HALF : T::P_REST), false};
    }
    return {(int) std::lround(flip ? x0 - d : x0 + d), last.y - T::MOUTH_DY, (int) (d / T::STEP) % 4, false};
  }

  // Decide whether this visit eats, and plan it from the snapshot. False: an ordinary visit.
  static bool plan_hunger(uint32_t seed) {
    Food box;
    if (marks_obj != nullptr) return false;                      // this card has been bitten already
    if (!food_hook || !food_hook(box) || box.w <= 0 || box.h <= 0 || st.snap == nullptr) return false;
    Rng rng(seed ^ 0x51ED5u);
    const int n = bite_count(seed);
    Pt pts[BITES_MAX];
    int got = 0;
    bool flip = false;
    Food item = box;
    const int sw = (int) st.snap->header.w, sh = (int) st.snap->header.h;
    if (box.trace) {
      int x0 = sw, y0 = sh, x1 = -1, y1 = -1;                    // what is drawn in the box
      for (int y = box.y; y < box.y + box.h && y < sh; y++)
        for (int x = box.x; x < box.x + box.w && x < sw; x++)
          if (lum565(st.snap, x, y) > 24) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
      if (x1 < x0) return false;
      item = Food{x0, y0, x1 - x0 + 1, y1 - y0 + 1, true};
      const int min_x = T::W + 2;                                // facing right: its left edge stays on screen
      const int tx = x0 + (x1 - x0) / 3, ty = y0 + (y1 - y0) * 93 / 100;
      long best = -1;
      Pt p0;
      for (int y = y0; y <= y1; y++)
        for (int x = x0 > min_x ? x0 : min_x; x <= x1; x++)
          if (lum565(st.snap, x, y) > 128) {
            const long dd = (long) (x - tx) * (x - tx) + (long) (y - ty) * (y - ty);
            if (best < 0 || dd < best) { best = dd; p0 = Pt{x, y}; }
          }
      if (best < 0) return false;
      got = trace_walk([](int x, int y) { return lum565(st.snap, x, y) > 128; }, item, min_x, p0, n, rng, pts);
    } else {
      flip = true;                                               // the portrait's edge, the bird facing it
      got = edge_points(item, n, rng, pts);
    }
    if (got < BITES_MIN) return false;
    const int mdx = flip ? -2 : T::W + 2;
    for (int i = 0; i < got; i++) {                              // the whole bird on screen at every bite
      const int left = pts[i].x - mdx, top = pts[i].y - T::MOUTH_DY;
      if (left < 0 || left + T::W > SCREEN_W || top < 0 || top + T::H > T::SCREEN_H) return false;
    }
    const int left0 = pts[0].x - mdx;
    T::aim(st.w, pts[0].y - T::MOUTH_DY, flip ? SCREEN_W - T::W - left0 : left0, flip);
    st.hungry_text = T::hungry_word(seed);
    st.hg.plan(pts, got, st.w.t_stop, T::MOVE_SPEED, 1500, rng);
    const Pt last = pts[got - 1];
    // ... and on its way out it may stop at a random spot over the picture for one more bite
    {
      int xs[SCREEN_W], nx = 0;
      if (!box.trace) {
        for (int x = item.x + 14; x <= last.x - 30; x += 4) xs[nx++] = x;
      } else {
        for (int x = last.x + 30; x <= item.x + item.w - 6 && x < SCREEN_W - 4; x += 3)
          for (int y = last.y - 7; y <= last.y + 7; y++)
            if (y >= item.y && y < item.y + item.h && lum565(st.snap, x, y) > 128) { xs[nx++] = x; break; }
      }
      if (nx > 0) {
        const int x = xs[(int) (rng.next() * nx) % nx];
        st.hg.plan_extra(Pt{x, last.y}, std::fabs((double) (x - last.x)), rng);
      }
    }
    st.hg.t_end = st.hg.t_exit + (st.hg.extra ? st.hg.ex_pause : 0) +
                  (uint32_t) (((flip ? last.x + 40 + T::W : 520 - last.x + mdx)) / T::MOVE_SPEED * 1000.0) + 300;
    st.item = item;
    st.mdx = mdx;
    return true;
  }

  // Move everything for this refresh, from the clock at this moment.
  static void frame() {
    if (!st.running || st.done) return;
    const uint32_t now = lv_tick_get();
    const uint32_t t = now - st.t0;
    if (st.last_frame) {
      const uint32_t gap = now - st.last_frame;
      if (gap > st.max_gap) st.max_gap = gap;
    }
    st.last_frame = now;
    st.frames++;
    // a touch since the start ends the visit; so does the page changing under it
    if (lv_display_get_inactive_time(nullptr) + 100 < t || lv_obj_get_parent(st.root) != lv_screen_active() ||
        (st.hungry ? t >= st.hg.t_end : T::finished(st.w, t))) {
      st.done = true;                        // torn down by the timer, outside the refresh
      return;
    }
    const Fr f = st.hungry ? hungry_frame(t) : [&] { const auto g = T::at(st.w, t); return Fr{g.x, g.y, (int) g.pose, g.bubble}; }();
    if (f.pose != st.pose) {
      st.pose = f.pose;
      lv_image_set_src(st.img, &st.pose_dsc[st.pose]);
    }
    if (st.hungry && marks_obj != nullptr) {                     // each bite shows the moment it lands
      const int k = st.hg.bites_done(t);
      if (k != st.marks_k) {
        marks_paint(st.snap, st.item, T::MOUTH_KIND, st.w.flip, st.hg.pt, st.hg.r, st.marks_k, k);
        st.marks_k = k;
      }
    }
    if (f.x != st.x || f.y != st.y) {
      st.x = f.x;
      st.y = f.y;
      lv_obj_set_pos(st.img, f.x, f.y);
    }
    if (f.bubble != st.bubble_on) {
      st.bubble_on = f.bubble;
      if (f.bubble) {
        lv_obj_update_layout(st.bubble);
        const int bw = lv_obj_get_width(st.bubble), bh = lv_obj_get_height(st.bubble);
        // over the head: right edge to the sprite's right edge going right, left edge to its left edge going left
        int bx = st.w.flip ? f.x - 6 : f.x + T::W + 6 - bw;
        if (bx < 4) bx = 4;
        if (bx + bw > SCREEN_W - 4) bx = SCREEN_W - 4 - bw;
        const int by = f.y - bh - TAIL_H + 4;
        lv_obj_set_pos(st.bubble, bx, by);
        lv_obj_set_pos(st.tail, st.w.flip ? bx + 14 : bx + bw - TAIL_W - 14, by + bh - 2);
        lv_obj_remove_flag(st.bubble, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(st.tail, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(st.bubble, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(st.tail, LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  static void on_refr(lv_event_t *) { frame(); }

  static void end_visit() {
    if (!st.running) return;
    if (lv_display_t *d = lv_display_get_default()) lv_display_remove_event_cb_with_user_data(d, on_refr, nullptr);
    if (st.timer) lv_timer_delete(st.timer);
    st.timer = nullptr;
    const uint32_t ms = lv_tick_get() - st.t0;
    if (st.hungry) {                                             // the bites it took stay on the page
      if (marks_obj != nullptr && st.snap != nullptr && st.root != nullptr) {
        const int k = st.hg.bites_done(ms);
        if (k > 0) {
          if (k != st.marks_k) marks_paint(st.snap, st.item, T::MOUTH_KIND, st.w.flip, st.hg.pt, st.hg.r, st.marks_k, k);
          lv_obj_set_parent(marks_obj, lv_obj_get_parent(st.root));   // out from under the snapshot, before it goes
        } else {
          clear_marks();
        }
      } else {
        clear_marks();
      }
    }
    st.hungry = false;
    eating = false;
    if (st.root) lv_obj_delete(st.root);                         // and its children
    st.root = st.img = st.bubble = st.label = st.tail = nullptr;
    if (st.snap) lv_draw_buf_destroy(st.snap);
    st.snap = nullptr;
    big_free(st.mirror);
    st.mirror = nullptr;
    ESP_LOGI(T::TAG, "visit: %u frames in %u ms (%.1f fps), longest gap %u ms", (unsigned) st.frames, (unsigned) ms,
             ms ? st.frames * 1000.0f / ms : 0.0f, (unsigned) st.max_gap);
    st.running = false;
    st.done = false;
  }

  // Start a visit now (the seed picks it). False: no visit (no memory).
  static bool begin_visit(uint32_t seed) {
    if (st.running || bubble_font == nullptr) return false;
    lv_obj_t *scr = lv_screen_active();
    st.w = T::make(seed);
    // the screen as it is, as the opaque backdrop
    st.snap = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
    if (st.snap == nullptr) {
      ESP_LOGW(T::TAG, "no memory for the snapshot: no visit");
      return false;
    }
    st.hungry = false;
    if (food_hook && (force_hungry || (seed >> 9) % HUNGRY_ONE_IN == 0)) {
      force_hungry = false;
      st.hungry = plan_hunger(seed);
      if (!st.hungry) st.w = T::make(seed);                      // plan_hunger may have changed it before giving up
    }
    const uint8_t *pose_src[N], *tail_src = st.w.flip ? dino::TAIL_L : dino::TAIL_R;
    for (int p = 0; p < N; p++) pose_src[p] = T::pose(p);
    if (st.w.flip) {                                             // facing left: mirrored copies for this visit
      st.mirror = (uint8_t *) big_alloc(POSE_BYTES * N);
      if (st.mirror == nullptr) {
        lv_draw_buf_destroy(st.snap);
        st.snap = nullptr;
        return false;
      }
      for (int p = 0; p < N; p++) {
        mirror_into(st.mirror + POSE_BYTES * p, T::pose(p), T::W, T::H);
        pose_src[p] = st.mirror + POSE_BYTES * p;
      }
    }
    for (int p = 0; p < N; p++) describe(st.pose_dsc[p], pose_src[p], T::W, T::H);
    describe(st.tail_dsc, tail_src, TAIL_W, TAIL_H);   // the pointer is not mirrored: there is one for each side

    st.root = lv_image_create(scr);
    lv_image_set_src(st.root, st.snap);
    lv_obj_set_pos(st.root, 0, 0);
    lv_obj_set_size(st.root, SCREEN_W, T::SCREEN_H);   // not SIZE_CONTENT: a sprite past x=480 would widen it and shift the picture
    lv_obj_remove_flag(st.root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(st.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(st.root);

    st.img = lv_image_create(st.root);
    lv_image_set_src(st.img, &st.pose_dsc[0]);
    lv_obj_remove_flag(st.img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(st.img, -T::W - 10, -T::H - 10);   // off screen until the first frame

    st.bubble = lv_obj_create(st.root);
    lv_obj_remove_style_all(st.bubble);
    lv_obj_set_size(st.bubble, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(st.bubble, lv_color_hex(0xF2F5FC), 0);
    lv_obj_set_style_bg_opa(st.bubble, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(st.bubble, lv_color_hex(0xFF8A1F), 0);
    lv_obj_set_style_border_width(st.bubble, 2, 0);
    lv_obj_set_style_radius(st.bubble, 10, 0);
    lv_obj_set_style_pad_hor(st.bubble, 10, 0);
    lv_obj_set_style_pad_ver(st.bubble, 5, 0);
    lv_obj_remove_flag(st.bubble, LV_OBJ_FLAG_CLICKABLE);
    st.label = lv_label_create(st.bubble);
    lv_obj_set_style_text_font(st.label, bubble_font, 0);
    lv_obj_set_style_text_color(st.label, lv_color_hex(0x101420), 0);
    lv_label_set_text(st.label, st.hungry ? st.hungry_text : T::word(st.w));
    lv_obj_add_flag(st.bubble, LV_OBJ_FLAG_HIDDEN);

    st.tail = lv_image_create(st.root);
    lv_image_set_src(st.tail, &st.tail_dsc);
    lv_obj_remove_flag(st.tail, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(st.tail, LV_OBJ_FLAG_HIDDEN);
    if (st.hungry) {                                             // the bites go behind the visitor
      st.marks_k = 0;
      if (marks_begin(st.root, st.item)) lv_obj_move_to_index(marks_obj, 0);
    }

    st.pose = -1;
    st.x = st.y = -99999;
    st.bubble_on = false;
    st.frames = st.max_gap = st.last_frame = 0;
    st.done = false;
    st.running = true;
    eating = st.hungry;
    st.t0 = lv_tick_get();
    if (lv_display_t *d = lv_display_get_default()) lv_display_add_event_cb(d, on_refr, LV_EVENT_REFR_START, nullptr);
    st.timer = lv_timer_create(
        [](lv_timer_t *) {
          if (st.done) { end_visit(); return; }
          if (lv_tick_get() - st.last_frame >= 150) frame();     // no refresh came: cover the gap
        },
        40, nullptr);
    if (st.timer == nullptr) {                                   // no timer, no way to end the visit
      ESP_LOGW(T::TAG, "no memory for the timer: no visit");
      end_visit();                                               // running is set, so this tears it down
      return false;
    }
    frame();
    return true;
  }

  // About once a second from the project: starts a visit when one is due, and ends one that is no longer
  // welcome. `welcome`: nothing over the screen, a page where a visitor is fun, the panel lit.
  static void tick(uint32_t now_ms, bool welcome) {
    if (st.next_at == 0) {                                       // first call: the first gap
      st.seed = now_ms * 2654435761u + 12345u;
      st.next_at = now_ms + T::gap(st.seed);
      return;
    }
    if (st.running) {
      if (!welcome) st.done = true;
      return;
    }
    if (!welcome || (enabled && !enabled()) || (int32_t) (now_ms - st.next_at) < 0) return;
    st.seed = st.seed * 1664525u + 1013904223u + now_ms;         // a new visit each time
    begin_visit(st.seed);
    st.next_at = now_ms + T::gap(st.seed ^ 0x9E3779B9u);
  }

  static bool active() { return st.running; }
  // A web button: the next visit eats the card's picture, where there is one.
  static void hungry_now() { force_hungry = true; visit_now(); }

  // Decision 202: a web button asks for a visit - the next tick that finds the panel free starts one.
  static void visit_now() { if (st.next_at != 0 && !st.running) st.next_at = 1; }
};

}  // namespace critter
}  // namespace f1
#endif  // device only
